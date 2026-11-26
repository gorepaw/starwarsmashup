#include "swg/character_export.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <ostream>
#include <set>
#include <stdexcept>

#include "swg/appearance.h"
#include "swg/gltf_assets.h"
#include "swg/manifest.h"
#include "swg/skeleton.h"
#include "swg/skinned_mesh.h"
#include "swg/static_mesh.h"
#include "swg/texture_renderer.h"

namespace swm::swg {
namespace {

constexpr int max_influences = 4;  // glTF JOINTS_0 / WEIGHTS_0

class Exporter {
public:
    Exporter(TreLibrary& library, const std::filesystem::path& out_dir, std::ostream& log)
        : library_(library), out_dir_(out_dir), log_(log),
          assets_(library, gltf_, out_dir, log, GltfAssets::Colors::describe) {}

    const CharacterBounds& bounds() const { return bounds_; }

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

        // The body's meshes and what it wears, all read first: what each
        // shows depends on what's worn over it (occlusion zones).
        struct Piece {
            std::string path;
            bool body;
            SkinnedMesh mesh;
        };
        std::vector<Piece> pieces;
        if (request.export_appearance_meshes) {
            for (size_t a = 0; a < appearances.size(); ++a) {
                for (const std::string& generator : appearances[a].mesh_generators) {
                    const std::string mgn_path = highest_detail_mgn(generator);
                    log_ << "  " << request.appearances[a] << " -> " << mgn_path << "\n";
                    pieces.push_back({mgn_path, true, parse_skinned_mesh(library_.read(mgn_path))});
                }
            }
        }
        for (const std::string& mesh : request.meshes) {
            const std::string mgn_path = highest_detail_mgn(mesh);
            log_ << "  worn: " << mgn_path << "\n";
            pieces.push_back({mgn_path, false, parse_skinned_mesh(library_.read(mgn_path))});
        }
        for (const Piece& piece : pieces) {
            // The zones covered over it: those of the meshes of higher layers.
            std::set<std::string> covered;
            for (const Piece& other : pieces) {
                if (other.mesh.occlusion_layer > piece.mesh.occlusion_layer) {
                    covered.insert(other.mesh.occludes.begin(), other.mesh.occludes.end());
                }
            }
            const int mesh_node = add_skinned_mesh(piece.path, piece.mesh, covered, piece.body);
            if (mesh_node >= 0) {  // (first: it may move the nodes)
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
        write_colors(out_dir_ / (request.name + ".colors.json"));
        write_occlusion(out_dir_ / (request.name + ".occlusion.json"));
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

    // `body`: one of the body's own meshes (not something worn). `covered`:
    // the zones worn over it, whose triangles aren't drawn (and none of it,
    // once all its fully occluded zones are covered: -1, no node).
    int add_skinned_mesh(const std::string& path, const SkinnedMesh& mesh, const std::set<std::string>& covered,
                         bool body) {
        const auto all_covered = [&](const std::vector<std::string>& zones) {
            return !zones.empty() && std::all_of(zones.begin(), zones.end(),
                                                 [&](const std::string& z) { return covered.contains(z); });
        };
        if (all_covered(mesh.fully_occluded)) {
            log_ << "  " << path << ": covered\n";
            return -1;
        }
        if (body) {
            // A garment's bare skin (an owner proxy shader, "body") is drawn
            // with the body's shader, which Galaxies names <species>_body.
            // ASSUMPTION: by that name; it holds for every species exported.
            for (const SkinnedMesh::Part& part : mesh.parts) {
                if (std::filesystem::path(part.shader).stem().string().ends_with("_body")) {
                    assets_.set_owner_shader("body", part.shader);
                }
            }
        }
        // Textures composited at runtime (a face's), by the material they
        // replace the main texture of.
        for (const SkinnedMesh::TextureRenderer& renderer : mesh.texture_renderers) {
            for (const auto& [part, tag] : renderer.targets) {
                if (tag == "MAIN") {
                    renderers_[std::filesystem::path(mesh.parts[size_t(part)].shader).stem().string()] = renderer.path;
                }
            }
        }

        std::vector<int> bone_to_joint;
        for (const std::string& bone : mesh.bones) {
            const int joint = skeleton_.find(bone);
            if (joint < 0) {
                throw std::runtime_error(path + ": bone '" + bone + "' isn't in the skeleton");
            }
            bone_to_joint.push_back(joint);
        }

        // Filter blend targets to those that actually affect at least one vertex in this mesh.
        std::vector<const SkinnedMesh::BlendTarget*> active_targets;
        std::vector<std::map<uint32_t, Vec3>> target_maps;
        for (const SkinnedMesh::BlendTarget& bt : mesh.blend_targets) {
            std::map<uint32_t, Vec3> by_pos;
            for (const auto& [idx, delta] : bt.position_deltas) {
                by_pos[idx] = delta;
            }
            bool hits_any = false;
            for (const SkinnedMesh::Part& part : mesh.parts) {
                for (const uint32_t p : part.position_index) {
                    if (by_pos.contains(p)) {
                        hits_any = true;
                        break;
                    }
                }
                if (hits_any) break;
            }
            if (hits_any) {
                active_targets.push_back(&bt);
                target_maps.push_back(std::move(by_pos));
            }
        }

        Gltf::Mesh gltf_mesh{.name = std::filesystem::path(path).stem().string()};
        size_t hidden_triangles = 0;
        std::string surfaces_json;  // each written primitive's triangles' combinations
        for (const auto* bt : active_targets) {
            gltf_mesh.target_names.push_back(bt->name);
        }
        for (const SkinnedMesh::Part& part : mesh.parts) {
            std::vector<Vec3> positions, normals;
            std::vector<std::array<uint16_t, 4>> joints;
            std::vector<std::array<float, 4>> weights;
            for (size_t v = 0; v < part.position_index.size(); ++v) {
                const uint32_t p = part.position_index[v];
                positions.push_back(to_gltf(mesh.positions[p]));
                const Vec3& at = positions.back();
                bounds_.min = {std::min(bounds_.min.x, at.x), std::min(bounds_.min.y, at.y), std::min(bounds_.min.z, at.z)};
                bounds_.max = {std::max(bounds_.max.x, at.x), std::max(bounds_.max.y, at.y), std::max(bounds_.max.z, at.z)};
                if (!part.normal_index.empty()) {
                    normals.push_back(to_gltf(mesh.normals[part.normal_index[v]]));
                }
                auto [vertex_joints, vertex_weights] = top_influences(mesh.weights[p], bone_to_joint);
                joints.push_back(vertex_joints);
                weights.push_back(vertex_weights);
            }
            std::vector<uint32_t> indices;
            std::string combinations_json;
            for (size_t t = 0; t < part.triangles.size(); ++t) {
                int combination = -1;
                if (t < part.triangle_occlusion.size()) {
                    combination = part.triangle_occlusion[t];
                    if (combination < 0 || size_t(combination) >= mesh.zone_combinations.size()) {
                        combination = -1;
                    } else if (all_covered(mesh.zone_combinations[size_t(combination)])) {
                        ++hidden_triangles;
                        continue;
                    }
                }
                const auto& triangle = part.triangles[t];
                append_triangle(indices, triangle[0], triangle[1], triangle[2]);
                combinations_json += std::format("{}{}", combinations_json.empty() ? "" : ",", combination);
            }
            if (indices.empty()) {
                continue;  // all of this part is covered
            }
            surfaces_json += std::format("{}[{}]", surfaces_json.empty() ? "" : ",", combinations_json);

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
            for (size_t t = 0; t < active_targets.size(); ++t) {
                std::vector<Vec3> deltas(part.position_index.size(), Vec3{0, 0, 0});
                const auto& by_pos = target_maps[t];
                for (size_t v = 0; v < part.position_index.size(); ++v) {
                    if (const auto it = by_pos.find(part.position_index[v]); it != by_pos.end()) {
                        deltas[v] = to_gltf(it->second);
                    }
                }
                primitive.targets.push_back({{"POSITION", gltf_.add_vec3(deltas, true, true)}});
            }
            primitive.indices = gltf_.add_indices(indices);
            primitive.material = assets_.material_for(part.shader);
            gltf_mesh.primitives.push_back(std::move(primitive));
        }
        if (hidden_triangles > 0) {
            log_ << std::format("  {}: {} triangle(s) covered\n", path, hidden_triangles);
        }
        if (gltf_mesh.primitives.empty()) {
            return -1;
        }
        if (!mesh.occlusion_zones.empty()) {
            std::string combinations;
            for (const auto& zones : mesh.zone_combinations) {
                combinations += std::format("{}{}", combinations.empty() ? "" : ",", names_json(zones));
            }
            occlusion_.push_back(std::format(
                R"({{"node":"{}","layer":{},"occludes":{},"fully":{},"combinations":[{}],"surfaces":[{}]}})",
                gltf_mesh.name, mesh.occlusion_layer, names_json(mesh.occludes), names_json(mesh.fully_occluded),
                combinations, surfaces_json));
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
        const StaticMesh static_mesh = parse_static_mesh(library_.read(mesh_path));
        const int mesh = assets_.add_static_mesh(static_mesh, std::filesystem::path(mesh_path).stem().string());

        const int hand = skeleton_.find("hold_r");
        if (hand < 0) {
            throw std::runtime_error("skeleton has no hold_r joint for the weapon");
        }
        const int weapon_node = add_node({.name = gltf_.meshes.back().name, .mesh = mesh});
        gltf_.nodes[size_t(hand)].children.push_back(weapon_node);
        // The weapon's hardpoints (e.g. its muzzle) as empty child nodes, so
        // the client can find where shots leave the barrel.
        for (const Hardpoint& hardpoint : static_mesh.hardpoints) {
            const Mat4 m = to_gltf(hardpoint.transform);
            const int node = add_node({.name = "hp_" + hardpoint.name,
                                       .translation = Vec3{m.at(0, 3), m.at(1, 3), m.at(2, 3)}});
            gltf_.nodes[size_t(weapon_node)].children.push_back(node);
            log_ << "    hardpoint " << hardpoint.name << "\n";
        }
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

    // <name>.colors.json: the customizable materials' colors, for the client
    // to choose (see CharacterExport).
    void write_colors(const std::filesystem::path& path) {
        std::set<std::string> palettes;
        std::map<std::string, std::string> entries;  // material -> its fields
        const auto add = [&](const std::string& material, const std::string& field) {
            std::string& entry = entries[material];
            entry += (entry.empty() ? "" : ",") + field;
        };
        for (const auto& [name, customization] : assets_.colors()) {
            for (const char* factor : {"MAIN", "HUEB"}) {
                if (std::string_view(factor) == "HUEB" && !customization.two_colors()) {
                    continue;
                }
                const auto color = std::find_if(customization.colors.begin(), customization.colors.end(),
                                                [&](const PaletteColor& c) { return c.factor == factor; });
                std::string value;
                if (color != customization.colors.end()) {
                    palettes.insert(color->palette);
                    value = std::format(R"({{"variable":"{}","shared":{},"palette":"{}","index":{}}})",
                                        color->variable, color->shared, color->palette, color->default_index);
                } else {
                    value = "\"" + hex(assets_.default_color(customization, factor)) + "\"";
                }
                add(name, std::format(R"("{}":{})", factor, value));
            }
        }
        for (const auto& [name, renderer_path] : renderers_) {
            try {
                add(name, "\"layers\":" + layers_json(renderer_path, palettes));
            } catch (const std::exception& e) {
                log_ << "  warning: " << renderer_path << ": " << e.what() << "\n";
            }
        }
        std::string materials;
        for (const auto& [name, fields] : entries) {
            materials += std::format("{}\n    \"{}\": {{{}}}", materials.empty() ? "" : ",", name, fields);
        }
        std::string palette_json;
        for (const std::string& palette : palettes) {
            std::string colors;
            for (const auto& color : assets_.palette(palette)) {
                colors += hex(color);
            }
            palette_json += std::format("{}\n    \"{}\": \"{}\"", palette_json.empty() ? "" : ",", palette, colors);
        }
        std::ofstream out(path, std::ios::binary);
        out << "{\n  \"palettes\": {" << palette_json << "\n  },\n  \"materials\": {" << materials << "\n  }\n}\n";
        if (!out) {
            throw std::runtime_error("can't write " + path.string());
        }
    }

    // A texture renderer's layers, as the client composites them (see
    // CharacterExport): each one's place, how it blends, its textures (the
    // choices of a style variable, or one), and its color.
    std::string layers_json(const std::string& renderer_path, std::set<std::string>& palettes) {
        const TextureRenderer renderer = parse_texture_renderer(library_.read(renderer_path));
        log_ << "  texture renderer " << renderer_path << ": " << renderer.layers.size() << " layers\n";
        std::string out;
        for (const TextureRendererLayer& layer : renderer.layers) {
            const bool blend = effect_blend(library_.read(layer.shader)) == EffectBlend::alpha;
            std::string textures;
            for (const std::string& texture : layer.textures) {
                textures += std::format(R"({}"{}")", textures.empty() ? "" : ",", assets_.texture_file(texture));
            }
            std::string color = "\"ffffff\"";
            if (!layer.palette.empty()) {
                palettes.insert(layer.palette);
                color = std::format(R"({{"variable":"{}","shared":{},"palette":"{}","index":{}}})",
                                    layer.color_variable, layer.color_shared, layer.palette,
                                    default_index(layer.color_variable));
            }
            out += std::format(
                R"({}{{"rect":[{},{},{},{}],"uv":[{},{},{},{}],"blend":{},"textures":[{}],"style":"{}","color":{}}})",
                out.empty() ? "" : ",", layer.rect[0], layer.rect[1], layer.rect[2], layer.rect[3], layer.uv[0],
                layer.uv[1], layer.uv[2], layer.uv[3], blend, textures, layer.style_variable, color);
        }
        return "[" + out + "]";
    }

    // A layer's color has no default of its own: the same variable's in the
    // body's shaders (a face's skin as its head's), else the palette's first.
    int default_index(const std::string& variable) const {
        for (const auto& [name, customization] : assets_.colors()) {
            for (const PaletteColor& color : customization.colors) {
                if (color.variable == variable) {
                    return color.default_index;
                }
            }
        }
        return 0;
    }

    static std::string names_json(const std::vector<std::string>& names) {
        std::string out;
        for (const std::string& name : names) {
            out += std::format(R"({}"{}")", out.empty() ? "" : ",", name);
        }
        return "[" + out + "]";
    }

    // <name>.occlusion.json: its meshes' occlusion zones (see
    // CharacterExport), for the client to hide what's worn over them.
    void write_occlusion(const std::filesystem::path& path) {
        if (occlusion_.empty()) {
            std::filesystem::remove(path);
            return;
        }
        std::ofstream out(path, std::ios::binary);
        out << "{\"meshes\": [";
        for (size_t i = 0; i < occlusion_.size(); ++i) {
            out << (i ? ",\n  " : "\n  ") << occlusion_[i];
        }
        out << "\n]}\n";
        if (!out) {
            throw std::runtime_error("can't write " + path.string());
        }
    }

    static std::string hex(std::array<uint8_t, 3> color) {
        return std::format("{:02x}{:02x}{:02x}", color[0], color[1], color[2]);
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
    CharacterBounds bounds_;
    std::map<std::string, std::string> renderers_;  // material -> its main texture's renderer (.trt)
    std::vector<std::string> occlusion_;            // each written mesh's occlusion (JSON)
};

} // namespace

Skeleton load_appearance_skeleton(TreLibrary& library, const std::string& sat_path) {
    Skeleton skeleton;
    for (const auto& reference : parse_skeletal_appearance(library.read(sat_path)).skeletons) {
        skeleton.attach(parse_skeleton(library.read(reference.path)), reference.attach_to);
    }
    return skeleton;
}

CharacterBounds export_character(TreLibrary& library, const CharacterExport& request,
                                 const std::filesystem::path& out_dir, std::ostream& log) {
    Exporter exporter(library, out_dir, log);
    exporter.run(request);
    return exporter.bounds();
}

} // namespace swm::swg
