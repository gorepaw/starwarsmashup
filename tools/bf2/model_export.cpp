#include "bf2/model_export.h"

#include <cctype>
#include <cmath>
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
        } else if (child.tag == "coll") {
            meshes_.try_emplace(lowercase(child.name()), &child);
        } else if (child.tag == "prim") {
            if (const UcfbNode* info = child.child("INFO")) {
                const auto* text = reinterpret_cast<const char*>(info->data.data());
                primitives_.try_emplace(lowercase(std::string(text, strnlen(text, info->data.size()))), &child);
            }
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

const UcfbNode* AssetLibrary::collision_mesh(std::string_view model) const {
    const auto it = meshes_.find(lowercase(model));
    return it == meshes_.end() ? nullptr : it->second;
}

const UcfbNode* AssetLibrary::collision_primitives(std::string_view model) const {
    const auto it = primitives_.find(lowercase(model));
    return it == primitives_.end() ? nullptr : it->second;
}

namespace {

using Point = std::array<float, 3>;

// A primitive's surface in its own space, as triangles.
std::vector<std::array<Point, 3>> tessellate(const CollisionPrimitive& primitive) {
    std::vector<std::array<Point, 3>> out;
    const auto quad = [&](Point a, Point b, Point c, Point d) {
        out.push_back({a, b, c});
        out.push_back({a, c, d});
    };
    const auto& s = primitive.size;
    if (primitive.shape == CollisionPrimitive::box) {
        const auto corner = [&](int i) {
            return Point{i & 1 ? s[0] : -s[0], i & 2 ? s[1] : -s[1], i & 4 ? s[2] : -s[2]};
        };
        const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
        for (const auto& f : faces) {
            quad(corner(f[0]), corner(f[1]), corner(f[2]), corner(f[3]));
        }
    } else if (primitive.shape == CollisionPrimitive::cylinder) {
        constexpr int sides = 12;
        const float radius = s[0], half = s[1] / 2;
        const auto rim = [&](int i, float y) {
            const double angle = 2 * 3.14159265358979 * i / sides;
            return Point{float(radius * std::cos(angle)), y, float(radius * std::sin(angle))};
        };
        for (int i = 0; i < sides; ++i) {
            quad(rim(i, -half), rim(i + 1, -half), rim(i + 1, half), rim(i, half));
            out.push_back({Point{0, half, 0}, rim(i, half), rim(i + 1, half)});
            out.push_back({Point{0, -half, 0}, rim(i + 1, -half), rim(i, -half)});
        }
    } else if (primitive.shape == CollisionPrimitive::sphere) {
        constexpr int rings = 6, sides = 8;
        const auto at = [&](int ring, int side) {
            const double polar = 3.14159265358979 * ring / rings, around = 2 * 3.14159265358979 * side / sides;
            return Point{float(s[0] * std::sin(polar) * std::cos(around)), float(s[0] * std::cos(polar)),
                         float(s[0] * std::sin(polar) * std::sin(around))};
        };
        for (int ring = 0; ring < rings; ++ring) {
            for (int side = 0; side < sides; ++side) {
                quad(at(ring, side), at(ring + 1, side), at(ring + 1, side + 1), at(ring, side + 1));
            }
        }
    }
    return out;
}

} // namespace

std::vector<CollisionTriangle> model_collision(const AssetLibrary& library, std::string_view model) {
    std::vector<CollisionTriangle> out;
    Skeleton skeleton;
    if (const UcfbNode* skel = library.skeleton(model)) {
        skeleton = read_skeleton(*skel);
    }
    if (const UcfbNode* coll = library.collision_mesh(model)) {
        const CollisionMesh mesh = read_collision_mesh(*coll);
        const Transform bone = skeleton.model_transform(mesh.bone);
        for (const auto& t : mesh.triangles) {
            out.push_back({bone.apply(mesh.vertices[t[0]]), bone.apply(mesh.vertices[t[1]]),
                           bone.apply(mesh.vertices[t[2]]), true, true});
        }
    }
    if (const UcfbNode* prim = library.collision_primitives(model)) {
        for (const CollisionPrimitive& primitive : read_collision_primitives(*prim).primitives) {
            const bool soldiers = primitive.stops('s'), ordnance = primitive.stops('o');
            if (!soldiers && !ordnance) {
                continue;
            }
            const Transform place = primitive.transform.then(skeleton.model_transform(primitive.parent));
            for (const auto& t : tessellate(primitive)) {
                out.push_back({place.apply(t[0]), place.apply(t[1]), place.apply(t[2]), soldiers, ordnance});
            }
        }
    }
    return out;
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
        const Transform bone = skeleton.model_transform(segment.bone);
        const Transform turn{bone.rotation, {0, 0, 0}};
        std::vector<Vec3> positions, normals;
        for (size_t i = 0; i < segment.positions.size(); ++i) {
            const auto p = bone.apply(segment.positions[i]);
            const auto n = turn.apply(segment.normals[i]);
            positions.push_back({-p[0], p[1], p[2]});
            normals.push_back({-n[0], n[1], n[2]});
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
