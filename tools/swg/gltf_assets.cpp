#include "swg/gltf_assets.h"

#include <algorithm>
#include <cmath>
#include <ostream>
#include <stdexcept>

#include "swg/appearance.h"
#include "swg/iff.h"
#include "swg/image.h"
#include "swg/manifest.h"

namespace swbfo::swg {

namespace {

// Galaxies multiplied colors in gamma space; glTF's base color factor is
// linear. (a * b)^2.2 = a^2.2 * b^2.2, so the linear factor is the color's
// linear value, under a power-law gamma.
float linear(uint8_t c) {
    return std::pow(c / 255.0f, 2.2f);
}

} // namespace

int GltfAssets::material_for(const std::string& shader_path) {
    if (const auto it = materials_.find(shader_path); it != materials_.end()) {
        return it->second;
    }
    Gltf::Material material{.name = std::filesystem::path(shader_path).stem().string()};
    try {
        const std::vector<uint8_t> bytes = library_.read(shader_path);
        const ShaderCustomization customization = parse_shader_customization(bytes);
        if (!customization.owner_proxy.empty()) {
            // A stand-in for its wearer's shader: that shader's material.
            const auto owner = owner_shaders_.find(customization.owner_proxy);
            if (owner != owner_shaders_.end() && owner->second != shader_path) {
                materials_[shader_path] = material_for(owner->second);
                return materials_[shader_path];
            }
            log_ << "  warning: " << shader_path << ": no shader for its owner's '" << customization.owner_proxy
                 << "'\n";
        }
        std::string texture = shader_main_texture(bytes);
        // A texture swap's default choice is the main texture as Galaxies
        // draws it unpainted; each choice is exported for the client to swap.
        for (const TextureSwap& swap : customization.textures) {
            if (swap.factor != "MAIN") continue;
            texture = swap.textures[size_t(std::clamp(swap.default_index, 0, int(swap.textures.size()) - 1))];
            if (colors_mode_ == Colors::describe) {
                for (const std::string& choice : swap.textures) texture_file(choice);
            }
        }
        const std::string effect = shader_effect(bytes);
        const EffectBlend blend = effect.empty() ? EffectBlend::opaque : blend_of(effect);
        if (blend == EffectBlend::alpha) {
            material.alpha_mode = "BLEND";
        } else if (blend == EffectBlend::cutout) {
            material.alpha_mode = "MASK";
            material.alpha_cutoff = 0.5f;
        } else if (blend == EffectBlend::additive) {
            // Galaxies' emissive additive programs: lit by nothing but itself.
            material.alpha_mode = "BLEND";
            material.additive = true;
            material.unlit = true;
        }
        if (colors_mode_ == Colors::describe && customization.colors.empty() && !customization.textures.empty()) {
            colors_[material.name] = customization;  // (a swap with no colors of its own)
        }
        if (!customization.colors.empty()) {
            const std::array<uint8_t, 3> main = default_color(customization, "MAIN");
            if (colors_mode_ == Colors::bake && customization.two_colors() && !texture.empty()) {
                material.texture = hued_image_for(texture, material.name, main, default_color(customization, "HUEB"),
                                                  blend != EffectBlend::opaque);
            } else {
                material.color = {linear(main[0]), linear(main[1]), linear(main[2]), 1};
            }
            if (colors_mode_ == Colors::describe) {
                colors_[material.name] = customization;
            }
        }
        if (!texture.empty() && material.texture < 0) {
            material.texture = image_for(texture);
        }
    } catch (const std::exception& e) {
        log_ << "  warning: " << shader_path << ": " << e.what() << "\n";
    }
    gltf_.materials.push_back(std::move(material));
    materials_[shader_path] = int(gltf_.materials.size() - 1);
    return materials_[shader_path];
}

const std::vector<std::array<uint8_t, 3>>& GltfAssets::palette(const std::string& path) {
    auto it = palettes_.find(path);
    if (it == palettes_.end()) {
        it = palettes_.emplace(path, parse_palette(library_.read(path))).first;
    }
    return it->second;
}

std::array<uint8_t, 3> GltfAssets::default_color(const ShaderCustomization& customization, const std::string& factor) {
    for (const PaletteColor& color : customization.colors) {
        if (color.factor == factor) {
            const auto& colors = palette(color.palette);
            if (colors.empty()) {
                break;
            }
            return colors[size_t(std::clamp(color.default_index, 0, int(colors.size()) - 1))];
        }
    }
    for (const auto& [name, argb] : customization.factor_defaults) {
        if (name == factor) {
            return {uint8_t(argb >> 16), uint8_t(argb >> 8), uint8_t(argb)};
        }
    }
    return {255, 255, 255};
}

EffectBlend GltfAssets::blend_of(const std::string& effect_path) {
    auto it = blends_.find(effect_path);
    if (it == blends_.end()) {
        EffectBlend blend = EffectBlend::opaque;
        try {
            blend = effect_blend(library_.read(effect_path));
        } catch (const std::exception& e) {
            log_ << "  warning: " << effect_path << ": " << e.what() << "\n";
        }
        it = blends_.emplace(effect_path, blend).first;
    }
    return it->second;
}

int GltfAssets::hued_image_for(const std::string& texture_path, const std::string& name, std::array<uint8_t, 3> main,
                               std::array<uint8_t, 3> hueb, bool keep_alpha) {
    // As a color2 pixel program: the texture times MAIN blended to HUEB by the
    // texture's alpha. The alpha is the blend's mask, so the copy is opaque,
    // unless the effect is transparent too (`keep_alpha`).
    ImageRgba image = decode_dds(library_.read(texture_path));
    for (size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
        const int mask = image.pixels[i + 3];
        for (int c = 0; c < 3; ++c) {
            const int hue = (main[size_t(c)] * (255 - mask) + hueb[size_t(c)] * mask) / 255;
            image.pixels[i + size_t(c)] = uint8_t(image.pixels[i + size_t(c)] * hue / 255);
        }
        if (!keep_alpha) {
            image.pixels[i + 3] = 255;
        }
    }
    const std::string file_name = name + ".hue.png";
    write_png(out_dir_ / file_name, image);
    gltf_.images.push_back(file_name);
    return int(gltf_.images.size() - 1);
}

std::string GltfAssets::texture_file(const std::string& texture_path) {
    const std::string file_name = std::filesystem::path(texture_path).stem().string() + ".png";
    if (texture_files_.insert(texture_path).second) {
        write_png(out_dir_ / file_name, decode_dds(library_.read(texture_path)));
    }
    return file_name;
}

int GltfAssets::image_for(const std::string& texture_path) {
    if (const auto it = images_.find(texture_path); it != images_.end()) {
        return it->second;
    }
    const std::string file_name = texture_file(texture_path);
    gltf_.images.push_back(file_name);
    images_[texture_path] = int(gltf_.images.size() - 1);
    return images_[texture_path];
}

int GltfAssets::add_static_mesh(const StaticMesh& mesh, std::string name) {
    Gltf::Mesh gltf_mesh{.name = std::move(name)};
    for (const StaticMesh::Part& part : mesh.parts) {
        if (part.indices.empty()) {
            continue;
        }
        std::vector<Vec3> positions, normals;
        for (const Vec3& p : part.positions) {
            positions.push_back(to_gltf(p));
        }
        for (const Vec3& n : part.normals) {
            normals.push_back(to_gltf(n));
        }
        Gltf::Primitive primitive;
        primitive.attributes["POSITION"] = gltf_.add_vec3(positions, true);
        if (!normals.empty()) {
            primitive.attributes["NORMAL"] = gltf_.add_vec3(normals, false);
        }
        if (!part.uv.empty()) {
            primitive.attributes["TEXCOORD_0"] = gltf_.add_vec2(part.uv);
        }
        std::vector<uint32_t> indices;
        for (size_t i = 0; i + 2 < part.indices.size(); i += 3) {
            append_triangle(indices, part.indices[i], part.indices[i + 1], part.indices[i + 2]);
        }
        primitive.indices = gltf_.add_indices(indices);
        primitive.material = material_for(part.shader);
        gltf_mesh.primitives.push_back(std::move(primitive));
    }
    gltf_.meshes.push_back(std::move(gltf_mesh));
    return int(gltf_.meshes.size() - 1);
}

namespace {

// Moves a mesh's parts into `out`, transformed by `transform`.
void append_transformed(StaticMesh&& mesh, const Mat4& transform, StaticMesh& out) {
    for (StaticMesh::Part& part : mesh.parts) {
        for (Vec3& p : part.positions) {
            p = transform_point(transform, p);
        }
        for (Vec3& n : part.normals) {
            n = normalized(transform_point(transform, n) - transform_point(transform, Vec3{}));
        }
        out.parts.push_back(std::move(part));
    }
}

StaticMesh load_appearance(TreLibrary& library, const std::string& path, int depth) {
    if (depth > 8) {
        throw std::runtime_error(path + ": appearances nested too deeply");
    }
    const std::vector<uint8_t> bytes = library.read(path);
    const IffNode root = parse_iff_root(bytes);
    if (root.tag == "MESH") {
        return parse_static_mesh(bytes);
    }
    if (root.tag == "APT ") {
        // Redirect: FORM APT -> 0000 -> NAME (full path of another appearance).
        for (const IffNode& version : root.children) {
            if (const IffNode* name = version.form ? version.child("NAME") : nullptr) {
                return load_appearance(library, normalize_path(IffReader(name->data).cstring()), depth + 1);
            }
        }
        throw std::runtime_error(path + ": redirect has no NAME");
    }
    if (root.tag == "DTLA") {
        return load_appearance(library, "appearance/" + lod_highest_detail(bytes), depth + 1);
    }
    if (root.tag == "CMPA") {
        // Component appearance: PART chunks, each a child appearance path
        // (relative to appearance/) and a 3x4 row-major transform.
        StaticMesh combined;
        for (const IffNode& version : root.children) {
            if (!version.form) {
                continue;
            }
            for (const IffNode* part : version.children_with("PART")) {
                IffReader r(part->data);
                const std::string child = r.cstring();
                Mat4 transform;
                for (int row = 0; row < 3; ++row) {
                    for (int column = 0; column < 4; ++column) {
                        transform.at(row, column) = r.f32();
                    }
                }
                append_transformed(load_appearance(library, "appearance/" + child, depth + 1), transform, combined);
            }
        }
        return combined;
    }
    throw std::runtime_error(path + ": unsupported appearance type " + root.tag);
}

} // namespace

StaticMesh load_static_appearance(TreLibrary& library, const std::string& path) {
    return load_appearance(library, normalize_path(path), 0);
}

} // namespace swbfo::swg
