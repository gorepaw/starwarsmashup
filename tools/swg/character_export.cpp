#include "swg/character_export.h"

#include <algorithm>
#include <ostream>
#include <stdexcept>

#include "swg/appearance.h"
#include "swg/gltf_assets.h"
#include "swg/manifest.h"
#include "swg/skeleton.h"
#include "swg/skinned_mesh.h"
#include "swg/static_mesh.h"

namespace swm::swg {
namespace {

constexpr int max_influences = 4;  // glTF JOINTS_0 / WEIGHTS_0

class Exporter {
public:
    Exporter(TreLibrary& library, const std::filesystem::path& out_dir, std::ostream& log)
        : library_(library), out_dir_(out_dir), log_(log), assets_(library, gltf_, out_dir, log) {}

    void run(const CharacterExport& request) {
        if (request.appearances.empty()) {
            throw std::runtime_error("no appearances to export");
        }
        std::filesystem::create_directories(out_dir_);
        std::vector<SkeletalAppearance> appearances;
        for (const std::string& path : request.appearances) {
            appearances.push_back(parse_skeletal_appearance(library_.read(path)));
        }

        build_skeleton(load_appearance_skeleton(library_, request.appearances.front()));
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
        for (const AnimationClip& clip : request.clips) {
            add_animation(clip);
        }

        gltf_.scene_nodes = {root};
        gltf_.write(out_dir_ / (request.name + ".gltf"));
    }

private:
    void build_skeleton(Skeleton skeleton) {
        skeleton_ = std::move(skeleton);
        log_ << "  skeleton: " << skeleton_.joints.size() << " joints\n";

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
            primitive.material = assets_.material_for(part.shader);
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
        const int mesh = assets_.add_static_mesh(parse_static_mesh(library_.read(mesh_path)),
                                                 std::filesystem::path(mesh_path).stem().string());

        const int hand = skeleton_.find("hold_r");
        if (hand < 0) {
            throw std::runtime_error("skeleton has no hold_r joint for the weapon");
        }
        const int weapon_node = add_node({.name = gltf_.meshes.back().name, .mesh = mesh});
        gltf_.nodes[size_t(hand)].children.push_back(weapon_node);
    }

    void add_animation(const AnimationClip& clip) {
        if (clip.rotations.empty()) {
            return;
        }
        std::vector<float> times;
        for (size_t f = 0; f < clip.rotations.size(); ++f) {
            times.push_back(float(f) / clip.fps);
        }
        const int input = gltf_.add_scalars(times);

        Gltf::Animation animation{.name = clip.name};
        for (size_t j = 0; j < skeleton_.joints.size(); ++j) {
            std::vector<std::array<float, 4>> rotations;
            for (const auto& frame : clip.rotations) {
                const Quat q = to_gltf(frame.at(j));
                rotations.push_back({q.x, q.y, q.z, q.w});
            }
            animation.channels.push_back({int(j), "rotation", input, gltf_.add_vec4(rotations, false)});
        }
        std::vector<Vec3> translations;
        for (const Vec3& t : clip.root_translations) {
            translations.push_back(to_gltf(t));
        }
        animation.channels.push_back({0, "translation", input, gltf_.add_vec3(translations, false, false)});
        gltf_.animations.push_back(std::move(animation));
        log_ << "  animation " << clip.name << ": " << clip.rotations.size() << " frames\n";
    }

    int add_node(Gltf::Node node) {
        gltf_.nodes.push_back(std::move(node));
        return int(gltf_.nodes.size() - 1);
    }

    TreLibrary& library_;
    std::filesystem::path out_dir_;
    std::ostream& log_;
    Gltf gltf_;
    GltfAssets assets_;
    Skeleton skeleton_;
};

} // namespace

Skeleton load_appearance_skeleton(TreLibrary& library, const std::string& sat_path) {
    Skeleton skeleton;
    for (const auto& reference : parse_skeletal_appearance(library.read(sat_path)).skeletons) {
        skeleton.attach(parse_skeleton(library.read(reference.path)), reference.attach_to);
    }
    return skeleton;
}

void export_character(TreLibrary& library, const CharacterExport& request, const std::filesystem::path& out_dir,
                      std::ostream& log) {
    Exporter(library, out_dir, log).run(request);
}

} // namespace swm::swg
