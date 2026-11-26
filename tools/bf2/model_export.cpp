#include "bf2/model_export.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "bf2/hash.h"
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

void AssetLibrary::add_sublevel(std::vector<uint8_t> bytes, uint32_t sublevel_hash) {
    files_.push_back(std::move(bytes));
    roots_.push_back(parse_ucfb(files_.back()));
    const auto walk = [&](auto& self, const UcfbNode& node) -> void {
        for (const UcfbNode& child : node.children) {
            if (child.tag == "lvl_" && child.name_hash == sublevel_hash) {
                index(child);
                classes_.add(child);
            } else if (child.tag == "ucfb" || child.tag == "lvl_") {
                self(self, child);
            }
        }
    };
    walk(walk, roots_.back());
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
        } else if (child.tag == "zaa_") {
            banks_.try_emplace(lowercase(child.name()), &child);
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

CollisionUse collision_use(const OdfClass& resolved) {
    CollisionUse use;
    const auto collect = [&](std::string_view key, std::optional<std::vector<std::string>>& names) {
        for (const auto& [hash, value] : resolved.properties) {
            if (hash == odf_hash(key)) {
                if (!names) {
                    names.emplace();
                }
                names->push_back(lowercase(value));
            }
        }
    };
    collect("SoldierCollision", use.soldiers);
    collect("OrdnanceCollision", use.ordnance);
    collect("VehicleCollision", use.vehicles);
    return use;
}

std::vector<CollisionTriangle> model_collision(const AssetLibrary& library, std::string_view model,
                                               const CollisionUse& use) {
    std::vector<CollisionTriangle> out;
    const auto listed = [](const std::optional<std::vector<std::string>>& names, const std::string& name) {
        return std::find(names->begin(), names->end(), name) != names->end();
    };
    Skeleton skeleton;
    if (const UcfbNode* skel = library.skeleton(model)) {
        skeleton = read_skeleton(*skel);
    }
    const bool mesh_soldiers = !use.soldiers || listed(use.soldiers, "collisionmesh");
    const bool mesh_ordnance = !use.ordnance || listed(use.ordnance, "collisionmesh");
    const bool mesh_vehicles = !use.vehicles || listed(use.vehicles, "collisionmesh");
    const UcfbNode* coll = library.collision_mesh(model);
    if (coll != nullptr && (mesh_soldiers || mesh_ordnance || mesh_vehicles)) {
        const CollisionMesh mesh = read_collision_mesh(*coll);
        const Transform bone = skeleton.model_transform(mesh.bone);
        for (const auto& t : mesh.triangles) {
            out.push_back({bone.apply(mesh.vertices[t[0]]), bone.apply(mesh.vertices[t[1]]),
                           bone.apply(mesh.vertices[t[2]]), mesh_soldiers, mesh_ordnance, mesh_vehicles});
        }
    }
    if (const UcfbNode* prim = library.collision_primitives(model)) {
        for (const CollisionPrimitive& primitive : read_collision_primitives(*prim).primitives) {
            const std::string name = lowercase(primitive.name);
            const bool soldiers = use.soldiers ? listed(use.soldiers, name) : primitive.stops('s');
            const bool ordnance = use.ordnance ? listed(use.ordnance, name) : primitive.stops('o');
            const bool vehicles = use.vehicles ? listed(use.vehicles, name) : primitive.stops('v');
            if (!soldiers && !ordnance && !vehicles) {
                continue;
            }
            const Transform place = primitive.transform.then(skeleton.model_transform(primitive.parent));
            for (const auto& t : tessellate(primitive)) {
                out.push_back({place.apply(t[0]), place.apply(t[1]), place.apply(t[2]), soldiers, ordnance, vehicles});
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

const UcfbNode* AssetLibrary::animation_bank(std::string_view name) const {
    const auto it = banks_.find(lowercase(name));
    return it == banks_.end() ? nullptr : it->second;
}

namespace {

// A stored 3x3 rotation (columns) as a quaternion.
Quat matrix_quat(const std::array<float, 9>& c) {
    // m[row][col] = c[col * 3 + row]
    const auto m = [&](int row, int col) { return c[size_t(col * 3 + row)]; };
    const float trace = m(0, 0) + m(1, 1) + m(2, 2);
    Quat q;
    if (trace > 0) {
        const float s = std::sqrt(trace + 1) * 2;
        q = {s / 4, (m(2, 1) - m(1, 2)) / s, (m(0, 2) - m(2, 0)) / s, (m(1, 0) - m(0, 1)) / s};
    } else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
        const float s = std::sqrt(1 + m(0, 0) - m(1, 1) - m(2, 2)) * 2;
        q = {(m(2, 1) - m(1, 2)) / s, s / 4, (m(0, 1) + m(1, 0)) / s, (m(0, 2) + m(2, 0)) / s};
    } else if (m(1, 1) > m(2, 2)) {
        const float s = std::sqrt(1 + m(1, 1) - m(0, 0) - m(2, 2)) * 2;
        q = {(m(0, 2) - m(2, 0)) / s, (m(0, 1) + m(1, 0)) / s, s / 4, (m(1, 2) + m(2, 1)) / s};
    } else {
        const float s = std::sqrt(1 + m(2, 2) - m(0, 0) - m(1, 1)) * 2;
        q = {(m(1, 0) - m(0, 1)) / s, (m(0, 2) + m(2, 0)) / s, (m(1, 2) + m(2, 1)) / s, s / 4};
    }
    return normalized(q);
}

} // namespace

ModelExport export_model(const AssetLibrary& library, std::string_view name, const std::filesystem::path& model_dir,
                         const std::filesystem::path& texture_dir, std::set<std::string>& textures_written,
                         const std::vector<ModelClip>& clips,
                         const std::map<std::string, std::string>& bone_rename,
                         std::string_view out_name) {
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
    // With clips, a mesh per bone (segments left in their bone's space);
    // without, one mesh (index 0 here) in model space.
    const bool animated = !clips.empty() && !skeleton.bones.empty();
    std::map<std::string, swg::Gltf::Mesh> bone_meshes;  // bone -> its segments
    swg::Gltf::Mesh mesh{lowercase(name), {}};
    swg::Gltf::Mesh skinned_mesh{lowercase(name) + "_skinned", {}};  // animated: skinned segments
    std::map<std::string, int> images;  // texture name -> image index
    const std::filesystem::path relative_textures = std::filesystem::relative(texture_dir, model_dir);
    for (const ModelSegment& segment : model.segments) {
        if (segment.positions.empty() || segment.triangles.empty()) {
            ++result.skipped;
            continue;
        }
        // A skinned segment's vertices each into model space through their
        // own bone (its bind pose; animated, the skin moves them from there).
        const bool skinned = !segment.vertex_bones.empty() && !segment.bone_map.empty() && !skeleton.bones.empty();
        std::vector<std::array<uint16_t, 4>> joints;
        // Into model space through the segment's bone (unless it gets a
        // node of its own), then mirror x.
        const bool on_node = !skinned && animated &&
                             std::any_of(skeleton.bones.begin(), skeleton.bones.end(),
                                         [&](const ModelBone& b) { return b.name == segment.bone; });
        const Transform segment_bone = on_node || skinned ? Transform{} : skeleton.model_transform(segment.bone);
        std::vector<Vec3> positions, normals;
        for (size_t i = 0; i < segment.positions.size(); ++i) {
            Transform bone = segment_bone;
            if (skinned) {
                const size_t mapped = segment.vertex_bones[i] < segment.bone_map.size()
                                          ? segment.bone_map[segment.vertex_bones[i]]
                                          : 0;
                const size_t joint = mapped < skeleton.bones.size() ? mapped : 0;
                bone = skeleton.model_transform(skeleton.bones[joint].name);
                joints.push_back({uint16_t(joint), 0, 0, 0});
            }
            const Transform turn{bone.rotation, {0, 0, 0}};
            const auto p = bone.apply(segment.positions[i]);
            const auto n = turn.apply(segment.normals[i]);
            positions.push_back({-p[0], p[1], p[2]});
            normals.push_back({-n[0], n[1], n[2]});
            const std::array<float, 3> q{float(-p[0]), float(p[1]), float(p[2])};
            for (size_t k = 0; k < 3; ++k) {
                result.min[k] = std::min(result.min[k], q[k]);
                result.max[k] = std::max(result.max[k], q[k]);
            }
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
        const bool skin_it = skinned && animated;
        if (skin_it) {
            primitive.attributes["JOINTS_0"] = gltf.add_joints(joints);
            primitive.attributes["WEIGHTS_0"] =
                gltf.add_vec4(std::vector<std::array<float, 4>>(joints.size(), {1, 0, 0, 0}));
        }

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
        (skin_it ? skinned_mesh : on_node ? bone_meshes[segment.bone] : mesh).primitives.push_back(primitive);
        ++result.segments;
    }
    if (result.segments == 0) {
        throw std::runtime_error("model " + std::string(name) + " has no readable segments");
    }
    // The root node: the model, holding what isn't on a bone.
    swg::Gltf::Node node;
    node.name = lowercase(name);
    if (!mesh.primitives.empty()) {
        node.mesh = int(gltf.meshes.size());
        gltf.meshes.push_back(mesh);
    }
    gltf.nodes.push_back(node);
    gltf.scene_nodes = {0};
    if (animated) {
        if (!bone_rename.empty()) {
            const bool has_weapon = std::any_of(skeleton.bones.begin(), skeleton.bones.end(),
                                                [](const ModelBone& b) { return lowercase(b.name) == "hp_weapons"; });
            if (!has_weapon) {
                const auto hand = std::find_if(skeleton.bones.begin(), skeleton.bones.end(),
                                               [](const ModelBone& b) { return lowercase(b.name) == "bone_r_hand"; });
                if (hand != skeleton.bones.end()) {
                    skeleton.bones.push_back(ModelBone{.name = "hp_weapons",
                                                       .parent = hand->name,
                                                       .rotation = {1, 0, 0, 0, 1, 0, 0, 0, 1},
                                                       .position = {-0.142f, -0.008f, 0.109f}});
                }
            }
        }
        // A node per bone, under its parent (or the root), at its rest
        // transform; mirrored as the vertices are.
        std::map<std::string, int> bone_nodes;
        for (const ModelBone& bone : skeleton.bones) {
            swg::Gltf::Node b;
            b.name = lowercase(bone.name);
            if (const auto r = bone_rename.find(b.name); r != bone_rename.end()) {
                b.name = r->second;
            }
            b.translation = to_gltf(Vec3{bone.position[0], bone.position[1], bone.position[2]});
            b.rotation = to_gltf(matrix_quat(bone.rotation));
            if (const auto it = bone_meshes.find(bone.name); it != bone_meshes.end()) {
                it->second.name = b.name;
                b.mesh = int(gltf.meshes.size());
                gltf.meshes.push_back(it->second);
            }
            bone_nodes[bone.name] = int(gltf.nodes.size());
            gltf.nodes.push_back(b);
        }
        for (const ModelBone& bone : skeleton.bones) {
            const auto parent = bone_nodes.find(bone.parent);
            gltf.nodes[size_t(parent == bone_nodes.end() ? 0 : parent->second)].children.push_back(
                bone_nodes[bone.name]);
        }
        // Skinned segments: one mesh on a node of its own, skinned by every
        // bone (joint i is skeleton bone i), bound where the bones rest.
        if (!skinned_mesh.primitives.empty()) {
            swg::Gltf::Skin skin;
            std::vector<Mat4> inverse_binds;
            for (const ModelBone& bone : skeleton.bones) {
                skin.joints.push_back(bone_nodes[bone.name]);
                const Transform bind = skeleton.model_transform(bone.name);
                const Mat4 global = Mat4::from_rotation_translation(
                    matrix_quat(bind.rotation), Vec3{bind.position[0], bind.position[1], bind.position[2]});
                inverse_binds.push_back(to_gltf(global).inverse_rigid());
            }
            skin.inverse_bind_matrices = gltf.add_matrices(inverse_binds);
            gltf.skins.push_back(std::move(skin));
            swg::Gltf::Node skinned_node;
            skinned_node.name = skinned_mesh.name;
            skinned_node.mesh = int(gltf.meshes.size());
            skinned_node.skin = int(gltf.skins.size()) - 1;
            gltf.meshes.push_back(skinned_mesh);
            gltf.nodes[0].children.push_back(int(gltf.nodes.size()));
            gltf.nodes.push_back(skinned_node);
        }
        // Each clip's tracks on the bones they name (by name_hash).
        for (const ModelClip& clip : clips) {
            std::vector<float> times;
            for (uint32_t f = 0; f < clip.animation.frame_count; ++f) {
                times.push_back(float(f) / animation_fps);
            }
            if (times.empty()) {
                continue;
            }
            const int input = gltf.add_scalars(times);
            swg::Gltf::Animation animation{.name = clip.name};
            for (const ModelBone& bone : skeleton.bones) {
                const AnimationTrack* track = clip.animation.track(name_hash(bone.name));
                if (track == nullptr) {
                    continue;
                }
                const int target = bone_nodes[bone.name];
                if (track->rotations.size() == times.size()) {
                    std::vector<std::array<float, 4>> rotations;
                    for (const Quat& r : track->rotations) {
                        const Quat q = to_gltf(r);
                        rotations.push_back({q.x, q.y, q.z, q.w});
                    }
                    animation.channels.push_back({target, "rotation", input, gltf.add_vec4(rotations, false)});
                }
                if (track->translations.size() == times.size()) {
                    std::vector<Vec3> translations;
                    for (const Vec3& t : track->translations) {
                        translations.push_back(to_gltf(t));
                    }
                    animation.channels.push_back(
                        {target, "translation", input, gltf.add_vec3(translations, false, false)});
                }
            }
            if (!animation.channels.empty()) {
                gltf.animations.push_back(animation);
            }
        }
    }
    std::filesystem::create_directories(model_dir);
    const std::string file_stem = lowercase(out_name.empty() ? name : out_name);
    gltf.write(model_dir / (file_stem + ".gltf"));
    return result;
}

} // namespace swm::bf2
