#include "swg/city_export.h"

#include <cmath>
#include <format>
#include <fstream>
#include <map>
#include <numbers>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "swg/appearance_collision.h"
#include "swg/building_export.h"
#include "swg/gltf.h"
#include "swg/gltf_assets.h"
#include "swg/manifest.h"
#include "swg/object_template.h"
#include "swg/portal_layout.h"
#include "swg/world_snapshot.h"
#include "swm/core/heightfield.h"
#include "swm/core/ini.h"
#include "swm/core/world.h"

namespace swm::swg {

namespace {

// A model, exported once and placed wherever its objects stand.
struct Model {
    std::string file;  // relative to the bake's directory, "" if it couldn't be exported
    bool building = false;
    AppearanceCollision collision;  // glTF space
};

std::string stem_of(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

// The object's rotation and position in game space: SWG's mirrored in X.
struct Placed {
    float basis[9];  // row-major
    Vec3 position;
    Vec3 apply(Vec3 v) const {
        return {basis[0] * v.x + basis[1] * v.y + basis[2] * v.z + position.x,
                basis[3] * v.x + basis[4] * v.y + basis[5] * v.z + position.y,
                basis[6] * v.x + basis[7] * v.y + basis[8] * v.z + position.z};
    }
};

Placed placed(const SnapshotNode& node) {
    const Mat4 r = Mat4::from_rotation_translation(normalized(node.rotation), {});
    // Mirroring X on both sides: M R M, with M = diag(-1, 1, 1).
    const float sign[3] = {-1, 1, 1};
    Placed p;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            p.basis[row * 3 + column] = r.at(row, column) * sign[row] * sign[column];
        }
    }
    p.position = to_gltf(node.position);
    return p;
}

core::Vec3 to_core(Vec3 v) {
    return {v.x, v.y, v.z};
}

// The hub's war terminals, as snapshot objects on the terrain (parent id -1
// marks them).
std::vector<SnapshotNode> recruiters(const CityExport& city) {
    std::vector<SnapshotNode> out;
    if (city.hub.empty()) {
        return out;
    }
    const core::Heightfield ground = core::load_bake(city.bake);
    const core::Ini hub = core::Ini::load(city.hub);
    for (const core::Ini::Section& section : hub.sections()) {
        const auto template_name = section.get("Template");
        if (section.name != "recruiter" || !template_name) {
            continue;
        }
        std::istringstream fields(section.get("Position").value_or(""));
        double x = 0, z = 0;
        if (!(fields >> x >> z)) {
            throw std::runtime_error(city.hub.string() + ": a recruiter needs Position = x z");
        }
        const double yaw = section.number("Yaw", 0) * std::numbers::pi / 180;
        SnapshotNode node;
        node.parent_id = -1;
        node.template_name = *template_name;
        node.position = {float(x), float(ground.height_at(-x, z)), float(z)};
        node.rotation = {float(std::cos(yaw / 2)), 0, float(std::sin(yaw / 2)), 0};
        out.push_back(node);
    }
    return out;
}

} // namespace

void export_city(TreLibrary& library, const CityExport& city, std::ostream& log) {
    std::vector<SnapshotNode> nodes = parse_world_snapshot(library.read(normalize_path(city.snapshot)));
    const std::vector<SnapshotNode> terminals = recruiters(city);
    nodes.insert(nodes.end(), terminals.begin(), terminals.end());
    if (!city.hub.empty()) {
        log << std::format("{} war terminal(s) from {}\n", terminals.size(), city.hub.string());
    }
    const std::filesystem::path out_dir = city.bake.parent_path() / (city.bake.filename().string() + "_city");
    const std::string out_name = out_dir.filename().string();
    std::filesystem::create_directories(out_dir);

    ObjectTemplates templates(library);
    AppearanceCollisions collisions(library);
    std::map<std::string, Model> models;  // appearance or portal layout path -> model
    std::map<std::string, int> skipped;   // why objects weren't placed
    std::map<int32_t, int> contents;      // objects inside each building
    for (const SnapshotNode& node : nodes) {
        if (node.parent_id > 0) {
            ++contents[node.parent_id];
        }
    }

    std::ostringstream objects;
    objects << "{\n  \"city\": " << std::format("\"{}\"", out_name) << ",\n  \"objects\": [";
    core::MapCollision collision;
    size_t placed_count = 0, inside = 0;
    for (const SnapshotNode& node : nodes) {
        const bool recruiter = node.parent_id == -1;
        if (node.parent_id != 0 && !recruiter) {
            continue;
        }
        if (std::hypot(node.position.x - city.center_x, node.position.z - city.center_z) > city.radius) {
            continue;
        }
        SharedObjectTemplate look;
        try {
            look = templates.shared(node.template_name);
        } catch (const std::exception& error) {
            ++skipped["template unreadable"];
            log << std::format("  {}: {}\n", node.template_name, error.what());
            continue;
        }
        const bool building = !look.portal_layout.empty();
        const std::string source = building ? look.portal_layout : look.appearance;
        if (source.empty()) {
            ++skipped["no appearance"];
            continue;
        }
        const std::string extension = std::filesystem::path(source).extension().string();
        if (!building && extension != ".apt" && extension != ".msh" && extension != ".lod" && extension != ".cmp") {
            ++skipped["appearance " + extension];
            continue;
        }
        auto found = models.find(source);
        if (found == models.end()) {
            Model model;
            try {
                if (building) {
                    // Every cell, with its lights (and its .collision for
                    // `--building`); the city's collision is gathered here.
                    std::ostringstream quiet;
                    export_building(library, source, stem_of(source), out_dir, quiet);
                    const PortalLayout layout = parse_portal_layout(library.read(source));
                    for (size_t c = 0; c < layout.cells.size(); ++c) {
                        const PortalLayout::Cell& cell = layout.cells[c];
                        if (c == 0) {
                            model.collision = collisions.load(cell.appearance);  // the outside
                            model.collision.floors.clear();  // its own floors come with the cells below
                            model.collision.walls.clear();
                        }
                        if (!cell.floor.empty()) {
                            add_floor_collision(parse_floor(library.read(cell.floor)), Mat4{}, model.collision);
                        }
                    }
                    model.building = true;
                } else {
                    Gltf gltf;
                    GltfAssets assets(library, gltf, out_dir, log);
                    const std::string name = stem_of(source);
                    gltf.nodes.push_back({.name = name, .mesh = assets.add_static_mesh(load_static_appearance(library, source), name)});
                    gltf.scene_nodes = {0};
                    gltf.write(out_dir / (name + ".gltf"));
                    model.collision = collisions.load(source);
                }
                model.file = out_name + "/" + stem_of(source) + ".gltf";
                // Signed volume of the solids: positive when they face
                // outward, as a closed skin should.
                double volume = 0;
                for (const auto& t : model.collision.solids) {
                    volume += dot(t[0], cross(t[1], t[2])) / 6.0;
                }
                log << std::format("  {} {}: {} solid ({:.1f} m3), {} floor, {} wall triangles\n",
                                   building ? "building" : "model", source, model.collision.solids.size(), volume,
                                   model.collision.floors.size(), model.collision.walls.size());
            } catch (const std::exception& error) {
                log << std::format("  {}: {}\n", source, error.what());
            }
            found = models.emplace(source, std::move(model)).first;
        }
        const Model& model = found->second;
        if (model.file.empty()) {
            ++skipped["model failed"];
            continue;
        }
        const Placed p = placed(node);
        objects << (placed_count == 0 ? "\n" : ",\n")
                << std::format("    {{\"template\": \"{}\", \"model\": \"{}\", {}\"position\": [{}, {}, {}], \"basis\": [{}, "
                               "{}, {}, {}, {}, {}, {}, {}, {}]}}",
                               node.template_name, model.file,
                               model.building ? "\"building\": true, " : recruiter ? "\"recruiter\": true, " : "",
                               p.position.x, p.position.y, p.position.z, p.basis[0], p.basis[1], p.basis[2], p.basis[3],
                               p.basis[4], p.basis[5], p.basis[6], p.basis[7], p.basis[8]);
        ++placed_count;
        inside += size_t(contents[node.id]);
        for (const auto* list : {&model.collision.solids, &model.collision.floors, &model.collision.walls}) {
            for (const auto& t : *list) {
                collision.triangles.push_back({to_core(p.apply(t[0])), to_core(p.apply(t[1])), to_core(p.apply(t[2]))});
            }
        }
    }
    objects << "\n  ]\n}\n";

    const std::filesystem::path objects_path = city.bake.string() + ".objects.json";
    std::ofstream(objects_path, std::ios::binary) << objects.str();
    core::save_map_collision(collision, core::map_collision_path(city.bake));
    log << std::format("placed {} objects ({} models) in {}; {} collision triangles; {} objects inside buildings "
                       "not placed yet\n",
                       placed_count, models.size(), out_dir.string(), collision.triangles.size(), inside);
    for (const auto& [why, count] : skipped) {
        log << std::format("  not placed: {} ({})\n", why, count);
    }
    for (const auto& [kind, count] : collisions.skipped()) {
        log << std::format("  collision not read: {} ({})\n", kind, count);
    }
}

} // namespace swm::swg
