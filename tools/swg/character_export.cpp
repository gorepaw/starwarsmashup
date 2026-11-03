#include "swg/character_export.h"

#include <algorithm>
#include <map>
#include <ostream>
#include <stdexcept>

#include "swg/appearance.h"
#include "swg/gltf.h"
#include "swg/image.h"
#include "swg/manifest.h"
#include "swg/skeleton.h"
#include "swg/skinned_mesh.h"
#include "swg/static_mesh.h"

namespace swm::swg {
namespace {

constexpr int max_influences = 4;  // glTF JOINTS_0 / WEIGHTS_0

// After mirroring X into glTF space, SWG's triangles face inward (verified by
// rendering), so reverse each triangle's winding.
void append_triangle(std::vector<uint32_t>& indices, uint32_t a, uint32_t b, uint32_t c) {
    indices.insert(indices.end(), {a, c, b});
}

class Exporter {
public:
    Exporter(TreLibrary& library, const std::filesystem::path& out_dir, std::ostream& log)
        : library_(library), out_dir_(out_dir), log_(log) {}

    void run(const CharacterExport& request) {
        if (request.appearances.empty()) {
            throw std::runtime_error("no appearances to export");
        }
        std::filesystem::create_directories(out_dir_);
        std::vector<SkeletalAppearance> appearances;
        for (const std::string& path : request.appearances) {
            appearances.push_back(parse_skeletal_appearance(library_.read(path)));
        }

        build_skeleton(appearances.front());
        const int root = add_node({.name = request.name});
        for (size_t i = 0; i < skeleton_.joints.size(); ++i) {
            if (skeleton_.joints[i].parent < 0) {
                gltf_.nodes[size_t(root)].children.push_back(int(i));
            }
        }

        for (size_t a = 0; a < appearances.size(); ++a) {
            for (const std::string& generator : appearances[a].mesh_generators) {
                const std::string mgn_path = highest_detail_mgn(generator);
                log_ << "  " << request.appearances[a] << " -> " << mgn_path << "\n";
                const int mesh_node = add_skinned_mesh(mgn_path);
                gltf_.nodes[size_t(root)].children.push_back(mesh_node);
            }
        }

        if (!request.weapon_lod.empty()) {
            attach_weapon(request.weapon_lod);
        }

        gltf_.scene_nodes = {root};
        gltf_.write(out_dir_ / (request.name + ".gltf"));
    }

private:
    void build_skeleton(const SkeletalAppearance& appearance) {
        for (const auto& reference : appearance.skeletons) {
            skeleton_.attach(parse_skeleton(library_.read(reference.path)), reference.attach_to);
            log_ << "  skeleton " << reference.path
                 << (reference.attach_to.empty() ? "" : " at " + reference.attach_to) << "\n";
        }

        // Joint nodes come first, so joint i is node i.
        for (const Joint& joint : skeleton_.joints) {
            add_node({.name = joint.name, .translation = to_gltf(joint.translation), .rotation = to_gltf(joint.rotation)});
        }
        for (size_t i = 0; i < skeleton_.joints.size(); ++i) {
            if (skeleton_.joints[i].parent >= 0) {
                gltf_.nodes[size_t(skeleton_.joints[i].parent)].children.push_back(int(i));
            }
        }

        std::vector<Mat4> inverse_binds;
        for (const Mat4& global : skeleton_.global_transforms()) {
            inverse_binds.push_back(to_gltf(global).inverse_rigid());
        }
        Gltf::Skin skin;
        for (size_t i = 0; i < skeleton_.joints.size(); ++i) {
            skin.joints.push_back(int(i));
        }
        skin.inverse_bind_matrices = gltf_.add_matrices(inverse_binds);
        skin.skeleton = 0;
        gltf_.skins.push_back(std::move(skin));
    }

    std::string highest_detail_mgn(const std::string& generator) {
        if (normalize_path(generator).ends_with(".lmg")) {
            return parse_mesh_lod_list(library_.read(generator)).at(0);
        }
        return generator;
    }

    int add_skinned_mesh(const std::string& path) {
        const SkinnedMesh mesh = parse_skinned_mesh(library_.read(path));

        std::vector<int> bone_to_joint;
        for (const std::string& bone : mesh.bones) {
            const int joint = skeleton_.find(bone);
            if (joint < 0) {
                throw std::runtime_error(path + ": bone '" + bone + "' isn't in the skeleton");
            }
            bone_to_joint.push_back(joint);
        }

        Gltf::Mesh gltf_mesh{.name = std::filesystem::path(path).stem().string()};
        for (const SkinnedMesh::Part& part : mesh.parts) {
            std::vector<Vec3> positions, normals;
            std::vector<std::array<uint16_t, 4>> joints;
            std::vector<std::array<float, 4>> weights;
            for (size_t v = 0; v < part.position_index.size(); ++v) {
                const uint32_t p = part.position_index[v];
                positions.push_back(to_gltf(mesh.positions[p]));
                if (!part.normal_index.empty()) {
                    normals.push_back(to_gltf(mesh.normals[part.normal_index[v]]));
                }
                auto [vertex_joints, vertex_weights] = top_influences(mesh.weights[p], bone_to_joint);
                joints.push_back(vertex_joints);
                weights.push_back(vertex_weights);
            }
            std::vector<uint32_t> indices;
            for (const auto& triangle : part.triangles) {
                append_triangle(indices, triangle[0], triangle[1], triangle[2]);
            }

            Gltf::Primitive primitive;
            primitive.attributes["POSITION"] = gltf_.add_vec3(positions, true);
            if (!normals.empty()) {
                primitive.attributes["NORMAL"] = gltf_.add_vec3(normals, false);
            }
            if (!part.uv.empty()) {
                primitive.attributes["TEXCOORD_0"] = gltf_.add_vec2(part.uv);
            }
            primitive.attributes["JOINTS_0"] = gltf_.add_joints(joints);
            primitive.attributes["WEIGHTS_0"] = gltf_.add_vec4(weights);
            primitive.indices = gltf_.add_indices(indices);
            primitive.material = material_for(part.shader);
            gltf_mesh.primitives.push_back(std::move(primitive));
        }
        gltf_.meshes.push_back(std::move(gltf_mesh));
        return add_node({.name = gltf_.meshes.back().name, .mesh = int(gltf_.meshes.size() - 1), .skin = 0});
    }

    static std::pair<std::array<uint16_t, 4>, std::array<float, 4>> top_influences(
        std::vector<std::pair<uint32_t, float>> influences, const std::vector<int>& bone_to_joint) {
        std::sort(influences.begin(), influences.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::array<uint16_t, 4> joints{};
        std::array<float, 4> weights{};
        float total = 0;
        for (size_t i = 0; i < influences.size() && i < max_influences; ++i) {
            joints[i] = static_cast<uint16_t>(bone_to_joint[influences[i].first]);
            weights[i] = influences[i].second;
            total += weights[i];
        }
        if (total <= 0) {
            return {{0, 0, 0, 0}, {1, 0, 0, 0}};  // unweighted: follow the root
        }
        for (float& w : weights) {
            w /= total;
        }
        return {joints, weights};
    }

    void attach_weapon(const std::string& lod_path) {
        const std::string mesh_path = "appearance/" + lod_highest_detail(library_.read(lod_path));
        log_ << "  weapon " << lod_path << " -> " << mesh_path << "\n";
        const StaticMesh mesh = parse_static_mesh(library_.read(mesh_path));

        Gltf::Mesh gltf_mesh{.name = std::filesystem::path(mesh_path).stem().string()};
        for (const StaticMesh::Part& part : mesh.parts) {
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

        const int hand = skeleton_.find("hold_r");
        if (hand < 0) {
            throw std::runtime_error("skeleton has no hold_r joint for the weapon");
        }
        const int weapon_node = add_node({.name = gltf_.meshes.back().name, .mesh = int(gltf_.meshes.size() - 1)});
        gltf_.nodes[size_t(hand)].children.push_back(weapon_node);
    }

    int material_for(const std::string& shader_path) {
        if (const auto it = materials_.find(shader_path); it != materials_.end()) {
            return it->second;
        }
        Gltf::Material material{.name = std::filesystem::path(shader_path).stem().string()};
        try {
            const std::string texture = shader_main_texture(library_.read(shader_path));
            if (!texture.empty()) {
                material.texture = image_for(texture);
            }
        } catch (const std::exception& e) {
            log_ << "  warning: " << shader_path << ": " << e.what() << "\n";
        }
        gltf_.materials.push_back(std::move(material));
        materials_[shader_path] = int(gltf_.materials.size() - 1);
        return materials_[shader_path];
    }

    int image_for(const std::string& texture_path) {
        if (const auto it = images_.find(texture_path); it != images_.end()) {
            return it->second;
        }
        const std::string file_name = std::filesystem::path(texture_path).stem().string() + ".png";
        write_png(out_dir_ / file_name, decode_dds(library_.read(texture_path)));
        gltf_.images.push_back(file_name);
        images_[texture_path] = int(gltf_.images.size() - 1);
        return images_[texture_path];
    }

    int add_node(Gltf::Node node) {
        gltf_.nodes.push_back(std::move(node));
        return int(gltf_.nodes.size() - 1);
    }

    TreLibrary& library_;
    std::filesystem::path out_dir_;
    std::ostream& log_;
    Gltf gltf_;
    Skeleton skeleton_;
    std::map<std::string, int> materials_;
    std::map<std::string, int> images_;
};

} // namespace

void export_character(TreLibrary& library, const CharacterExport& request, const std::filesystem::path& out_dir,
                      std::ostream& log) {
    Exporter(library, out_dir, log).run(request);
}

} // namespace swm::swg
