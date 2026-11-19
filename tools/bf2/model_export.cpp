#include "bf2/model_export.h"

#include <cctype>
#include <cstring>
#include <stdexcept>

#include "swg/gltf.h"
#include "swg/image.h"

namespace swm::bf2 {

std::string lowercase(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

void AssetLibrary::add(std::vector<uint8_t> bytes) {
    files_.push_back(std::move(bytes));
    roots_.push_back(parse_ucfb(files_.back()));
    index(roots_.back());
    classes_.add(roots_.back());
}

void AssetLibrary::index(const UcfbNode& node) {
    for (const UcfbNode& child : node.children) {
        if (child.tag == "lvl_") {
            index(child);
        } else if (child.tag == "modl") {
            models_.try_emplace(lowercase(child.name()), &child);
        } else if (child.tag == "gmod") {
            // Resolved when looked up: the LOD's modl may come later.
            if (const UcfbNode* lod = child.child("LOD0")) {
                const auto* text = reinterpret_cast<const char*>(lod->data.data());
                lod_names_.try_emplace(lowercase(child.name()),
                                       lowercase(std::string(text, strnlen(text, lod->data.size()))));
            }
        } else if (child.tag == "skel") {
            if (const UcfbNode* info = child.child("INFO")) {
                const auto* text = reinterpret_cast<const char*>(info->data.data());
                skeletons_.try_emplace(lowercase(std::string(text, strnlen(text, info->data.size()))), &child);
            }
        } else if (child.tag == "tex_") {
            textures_.try_emplace(lowercase(child.name()), &child);
        }
    }
}

const UcfbNode* AssetLibrary::model(std::string_view name) const {
    const std::string key = lowercase(name);
    if (const auto it = models_.find(key); it != models_.end()) {
        return it->second;
    }
    if (const auto lod = lod_names_.find(key); lod != lod_names_.end()) {
        if (const auto it = models_.find(lod->second); it != models_.end()) {
            return it->second;
        }
    }
    return nullptr;
}

const UcfbNode* AssetLibrary::skeleton(std::string_view model) const {
    const auto it = skeletons_.find(lowercase(model));
    return it == skeletons_.end() ? nullptr : it->second;
}

const UcfbNode* AssetLibrary::texture(std::string_view name) const {
    const auto it = textures_.find(lowercase(name));
    return it == textures_.end() ? nullptr : it->second;
}

bool export_texture(const AssetLibrary& library, std::string_view name, const std::filesystem::path& path) {
    const UcfbNode* tex = library.texture(name);
    if (tex == nullptr) {
        return false;
    }
    const std::optional<swg::ImageRgba> image = read_texture(*tex);
    if (!image) {
        return false;
    }
    std::filesystem::create_directories(path.parent_path());
    swg::write_png(path, *image);
    return true;
}

ModelExport export_model(const AssetLibrary& library, std::string_view name, const std::filesystem::path& model_dir,
                         const std::filesystem::path& texture_dir, std::set<std::string>& textures_written) {
    const UcfbNode* modl = library.model(name);
    if (modl == nullptr) {
        throw std::runtime_error("no model " + std::string(name));
    }
    const Model model = read_model(*modl);
    Skeleton skeleton;
    if (const UcfbNode* skel = library.skeleton(model.name)) {
        skeleton = read_skeleton(*skel);
    }

    ModelExport result;
    swg::Gltf gltf;
    swg::Gltf::Mesh mesh{lowercase(name), {}};
    std::map<std::string, int> images;  // texture name -> image index
    const std::filesystem::path relative_textures = std::filesystem::relative(texture_dir, model_dir);
    for (const ModelSegment& segment : model.segments) {
        if (segment.positions.empty() || segment.triangles.empty()) {
            ++result.skipped;
            continue;
        }
        // Into model space through the segment's bone, then mirror x.
        std::array<float, 9> r;
        std::array<float, 3> t;
        skeleton.model_transform(segment.bone, r, t);
        std::vector<Vec3> positions, normals;
        for (size_t i = 0; i < segment.positions.size(); ++i) {
            const auto& p = segment.positions[i];
            const auto& n = segment.normals[i];
            positions.push_back({-(r[0] * p[0] + r[1] * p[1] + r[2] * p[2] + t[0]),
                                 r[3] * p[0] + r[4] * p[1] + r[5] * p[2] + t[1],
                                 r[6] * p[0] + r[7] * p[1] + r[8] * p[2] + t[2]});
            normals.push_back({-(r[0] * n[0] + r[1] * n[1] + r[2] * n[2]), r[3] * n[0] + r[4] * n[1] + r[5] * n[2],
                               r[6] * n[0] + r[7] * n[1] + r[8] * n[2]});
        }
        // Mirroring reverses the winding: swap two corners back.
        std::vector<uint32_t> triangles = segment.triangles;
        for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
            std::swap(triangles[i + 1], triangles[i + 2]);
        }

        swg::Gltf::Primitive primitive;
        primitive.attributes["POSITION"] = gltf.add_vec3(positions, true);
        primitive.attributes["NORMAL"] = gltf.add_vec3(normals, false);
        primitive.attributes["TEXCOORD_0"] = gltf.add_vec2(segment.uvs);
        if (!segment.colors.empty()) {
            std::vector<std::array<float, 4>> colors;
            for (const auto& c : segment.colors) {
                colors.push_back({c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f});
            }
            primitive.attributes["COLOR_0"] = gltf.add_vec4(colors);
        }
        primitive.indices = gltf.add_indices(triangles);

        swg::Gltf::Material material;
        material.name = segment.textures[0].empty() ? "untextured" : lowercase(segment.textures[0]);
        if (!segment.textures[0].empty()) {
            const std::string texture = lowercase(segment.textures[0]);
            if (!textures_written.contains(texture)) {
                if (export_texture(library, texture, texture_dir / (texture + ".png"))) {
                    textures_written.insert(texture);
                } else {
                    result.missing_textures.push_back(texture);
                }
            }
            if (textures_written.contains(texture)) {
                const auto [it, added] = images.try_emplace(texture, int(gltf.images.size()));
                if (added) {
                    gltf.images.push_back((relative_textures / (texture + ".png")).generic_string());
                }
                material.texture = it->second;
            }
        }
        for (size_t i = 0; i < 4; ++i) {
            material.color[i] = segment.diffuse[i] / 255.0f;
        }
        material.alpha_mode = segment.flags & material_transparent ? "BLEND"
                              : segment.flags & material_hardedged ? "MASK"
                                                                   : "OPAQUE";
        material.double_sided = segment.flags & material_doublesided;
        // Glowing parts are full bright; baked lighting replaces the engine's.
        material.unlit = (segment.flags & material_glow) || segment.baked_lighting;
        primitive.material = int(gltf.materials.size());
        gltf.materials.push_back(material);
        mesh.primitives.push_back(primitive);
        ++result.segments;
    }
    if (mesh.primitives.empty()) {
        throw std::runtime_error("model " + std::string(name) + " has no readable segments");
    }
    gltf.meshes.push_back(mesh);
    swg::Gltf::Node node;
    node.name = lowercase(name);
    node.mesh = 0;
    gltf.nodes.push_back(node);
    gltf.scene_nodes = {0};
    std::filesystem::create_directories(model_dir);
    gltf.write(model_dir / (lowercase(name) + ".gltf"));
    return result;
}

} // namespace swm::bf2
