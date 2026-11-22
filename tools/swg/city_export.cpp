#include "swg/city_export.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <map>
#include <numbers>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "swg/appearance.h"
#include "swg/appearance_collision.h"
#include "swg/building_export.h"
#include "swg/character_export.h"
#include "swg/keyframe_animation.h"
#include "swg/datatable.h"
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
    std::vector<std::array<Vec3, 3>> rooms;  // a building's rooms' walked floors (every cell but the outside)
    std::vector<std::array<Vec3, 3>> drawn_rooms;  // and their drawn floors
    // A building's doors: one per doorway that names a door style.
    struct Door {
        std::string style;
        int cell = 0;    // the cell whose link names it (lit by its lights)
        Mat4 transform;  // SWG building space
    };
    std::vector<Door> doors;
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

// The same from a rotation + translation matrix in SWG space.
Placed placed(const Mat4& m) {
    const float sign[3] = {-1, 1, 1};
    Placed p;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            p.basis[row * 3 + column] = m.at(row, column) * sign[row] * sign[column];
        }
    }
    p.position = to_gltf(Vec3{m.at(0, 3), m.at(1, 3), m.at(2, 3)});
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

// The hub's people ([npc] and [crowd] templates): each a dressed body
// (its template's appearance and what its client data file says it wears)
// with Galaxies' own clips, named walk, idle and sit, in <out_dir>/people/,
// named by the template's file without "shared_". A creature (a bantha, a
// dewback: its own skeleton, whose name its clips are named by) walks,
// stands breathing, and kneels for sit (else lies down: a bantha lies on
// its side, which reads as dead).
void export_people(TreLibrary& library, const CityExport& city, const std::filesystem::path& out_dir,
                   std::ostream& log) {
    const core::Ini hub = core::Ini::load(city.hub);
    std::vector<std::string> looks;
    for (const core::Ini::Section& section : hub.sections()) {
        if (section.name != "npc" && section.name != "crowd") {
            continue;
        }
        for (const auto& [key, value] : section.values) {
            if (key == "Template" && std::find(looks.begin(), looks.end(), value) == looks.end()) {
                looks.push_back(value);
            }
        }
    }
    ObjectTemplates templates(library);
    for (const std::string& look : looks) {
        try {
            const SharedObjectTemplate& shared = templates.shared(look);
            if (!shared.appearance.ends_with(".sat")) {
                log << std::format("  person {}: no skeletal appearance ({})\n", look, shared.appearance);
                continue;
            }
            CharacterExport request;
            request.name = std::filesystem::path(look).stem().string();
            if (request.name.starts_with("shared_")) {
                request.name = request.name.substr(7);
            }
            request.appearances = {shared.appearance};
            if (!shared.client_data.empty() && library.contains(shared.client_data)) {
                request.meshes = client_data_wearables(library.read(shared.client_data));
            }
            // A woman walks as one (hum_f.sat, twk_f.sat, ...).
            const std::string body = std::filesystem::path(shared.appearance).stem().string();
            const bool female = body.ends_with("_f");
            const Skeleton skeleton = load_appearance_skeleton(library, shared.appearance);
            const SkeletalAppearance appearance = parse_skeletal_appearance(library.read(shared.appearance));
            const std::string kind =
                appearance.skeletons.empty() ? "all_b" : std::filesystem::path(appearance.skeletons[0].path).stem().string();
            const std::string clip_prefix = "appearance/animation/" + kind;
            std::vector<std::pair<std::string, std::string>> clips = {
                {"walk", clip_prefix + (female ? "_loc_walk_female.ans" : "_loc_walk_male.ans")},
                {"idle", clip_prefix + "_idl_standing_idle1.ans"},
                {"sit", clip_prefix + "_idl_sitting_chair_idle.ans"},
            };
            if (kind != "all_b") {  // a creature
                const std::string kneel = clip_prefix + "_idl_kneel_breathe.ans";
                clips = {{"walk", clip_prefix + "_loc_walk.ans"},
                         {"idle", clip_prefix + "_idl_stand_breathe.ans"},
                         {"sit", library.contains(kneel) ? kneel : clip_prefix + "_idl_lie_breathe.ans"}};
            }
            for (const auto& [name, path] : clips) {
                if (library.contains(path)) {
                    request.clips.push_back(sample_animation(parse_keyframe_animation(library.read(path)), skeleton, name));
                }
            }
            std::ostringstream quiet;
            export_character(library, request, out_dir / "people", quiet);
            // A creature's body is its own (the hub file's [npc] Radius).
            const std::string radius = kind != "all_b" && shared.collision_radius > 0
                                           ? std::format(", radius {:.2f} m", shared.collision_radius)
                                           : "";
            log << std::format("  person {}: {} ({} worn{})\n", request.name, shared.appearance, request.meshes.size(),
                               radius);
        } catch (const std::exception& error) {
            log << std::format("  person {}: {}\n", look, error.what());
        }
    }
}

// A room's drawn floors, in glTF space: its render mesh's triangles facing
// up (by their vertices' normals). They reach its walls, where its walked
// floor (.flr) stops short of them.
void add_drawn_floors(const StaticMesh& mesh, std::vector<std::array<Vec3, 3>>& out) {
    for (const StaticMesh::Part& part : mesh.parts) {
        if (part.normals.size() != part.positions.size()) {
            continue;
        }
        for (size_t i = 0; i + 2 < part.indices.size(); i += 3) {
            const uint32_t a = part.indices[i], b = part.indices[i + 1], c = part.indices[i + 2];
            if (part.normals[a].y + part.normals[b].y + part.normals[c].y < 3 * 0.7f) {
                continue;
            }
            out.push_back({to_gltf(part.positions[a]), to_gltf(part.positions[b]), to_gltf(part.positions[c])});
        }
    }
}

// Whether the square [x, x + size] by [z, z + size] and a triangle, seen from
// above, overlap by more than a touch (separating axes: the square's and the
// triangle's edges' normals).
bool square_meets_triangle(double x, double z, double size, const std::array<Vec3, 3>& t) {
    const auto separated = [&](double ax, double az) {
        double s_lo = 1e300, s_hi = -1e300, t_lo = 1e300, t_hi = -1e300;
        for (const double cx : {x, x + size}) {
            for (const double cz : {z, z + size}) {
                s_lo = std::min(s_lo, cx * ax + cz * az), s_hi = std::max(s_hi, cx * ax + cz * az);
            }
        }
        for (const Vec3& v : t) {
            t_lo = std::min(t_lo, v.x * ax + v.z * az), t_hi = std::max(t_hi, v.x * ax + v.z * az);
        }
        const double touch = 1e-6 * (std::abs(ax) + std::abs(az));
        return s_hi <= t_lo + touch || t_hi <= s_lo + touch;
    };
    if (separated(1, 0) || separated(0, 1)) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        const Vec3& a = t[size_t(i)];
        const Vec3& b = t[size_t((i + 1) % 3)];
        if (separated(-(double(b.z) - a.z), double(b.x) - a.x)) {
            return false;
        }
    }
    return true;
}

// The height of a triangle's plane over (x, z) (its highest corner if it
// stands on edge).
double plane_height(const std::array<Vec3, 3>& t, double x, double z) {
    const double e1x = t[1].x - t[0].x, e1y = t[1].y - t[0].y, e1z = t[1].z - t[0].z;
    const double e2x = t[2].x - t[0].x, e2y = t[2].y - t[0].y, e2z = t[2].z - t[0].z;
    const double nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
    if (std::abs(ny) < 1e-9) {
        return std::max({t[0].y, t[1].y, t[2].y});
    }
    return t[0].y - (nx * (x - t[0].x) + nz * (z - t[0].z)) / ny;
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
    // What's inside what: a building holds its cells, a cell its furniture.
    std::map<int32_t, std::vector<const SnapshotNode*>> children;
    for (const SnapshotNode& node : nodes) {
        if (node.parent_id > 0) {
            children[node.parent_id].push_back(&node);
        }
    }

    std::ostringstream objects;
    objects << "{\n  \"city\": " << std::format("\"{}\"", out_name) << ",\n  \"objects\": [";
    core::MapCollision collision;
    std::vector<std::array<Vec3, 3>> rooms;  // every building's rooms' walked floors, game space
    std::vector<std::array<Vec3, 3>> drawn_rooms;  // and their drawn floors
    size_t placed_count = 0, furniture = 0, doors = 0;
    // Galaxies' door styles: each one's door, how far it slides and how fast.
    struct DoorStyle {
        std::string appearance;
        Vec3 move;
        float open_time = 0.5f, close_time = 0.5f, radius = 3;
    };
    std::map<std::string, DoorStyle> door_styles;
    if (library.contains("datatables/appearance/door_style.iff")) {
        const DataTable table = parse_datatable(library.read("datatables/appearance/door_style.iff"));
        for (size_t row = 0; row < table.rows.size(); ++row) {
            DoorStyle style;
            style.appearance = normalize_path(table.text(row, table.column("doorAppearance")));
            style.move = {table.number(row, table.column("doorMoveX")), table.number(row, table.column("doorMoveY")),
                          table.number(row, table.column("doorMoveZ"))};
            style.open_time = table.number(row, table.column("openTime"));
            style.close_time = table.number(row, table.column("closeTime"));
            style.radius = table.number(row, table.column("triggerRadius"));
            door_styles[table.text(row, table.column("doorStyleName"))] = style;
        }
    }
    // Places one object; `inside` is the index of the building it's in
    // (-1 if none) and `cell` its cell there. Returns whether it was placed.
    // A model, exported the first time it's asked for; null if it couldn't be.
    const auto model_for = [&](const std::string& source, bool building) -> const Model* {
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
                    std::set<int32_t> doorways;
                    for (size_t c = 0; c < layout.cells.size(); ++c) {
                        const PortalLayout::Cell& cell_layout = layout.cells[c];
                        if (c == 0) {
                            model.collision = collisions.load(cell_layout.appearance);  // the outside
                            model.collision.floors.clear();  // its own floors come with the cells below
                            model.collision.walls.clear();
                        }
                        if (!cell_layout.floor.empty()) {
                            const FloorMesh floor = parse_floor(library.read(cell_layout.floor));
                            add_floor_collision(floor, Mat4{}, model.collision);
                            if (c > 0) {
                                AppearanceCollision room;
                                add_floor_collision(floor, Mat4{}, room);
                                model.rooms.insert(model.rooms.end(), room.floors.begin(), room.floors.end());
                            }
                        }
                        if (c > 0) {
                            add_drawn_floors(load_static_appearance(library, cell_layout.appearance), model.drawn_rooms);
                        }
                        // A doorway's door, named on either side: once.
                        for (const PortalLayout::CellPortal& link : cell_layout.portals) {
                            if (!link.door_style.empty() && link.has_door_hardpoint &&
                                doorways.insert(link.portal).second) {
                                model.doors.push_back({link.door_style, int(c), link.door_transform});
                            } else if (!link.door_style.empty() && !link.has_door_hardpoint) {
                                ++skipped["door without a hardpoint"];
                            }
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
                log << std::format("  {} {}: {} solid ({:.1f} m3), {} floor, {} wall triangles{}\n",
                                   building ? "building" : "model", source, model.collision.solids.size(), volume,
                                   model.collision.floors.size(), model.collision.walls.size(),
                                   building ? std::format(", {} doors", model.doors.size()) : "");
            } catch (const std::exception& error) {
                log << std::format("  {}: {}\n", source, error.what());
            }
            found = models.emplace(source, std::move(model)).first;
        }
        return found->second.file.empty() ? nullptr : &found->second;
    };

    const auto place = [&](const SnapshotNode& node, bool recruiter, int inside, int cell) -> bool {
        SharedObjectTemplate look;
        try {
            look = templates.shared(node.template_name);
        } catch (const std::exception& error) {
            ++skipped["template unreadable"];
            log << std::format("  {}: {}\n", node.template_name, error.what());
            return false;
        }
        const bool building = !look.portal_layout.empty();
        const std::string source = building ? look.portal_layout : look.appearance;
        if (source.empty()) {
            ++skipped["no appearance"];
            return false;
        }
        const std::string extension = std::filesystem::path(source).extension().string();
        if (!building && extension != ".apt" && extension != ".msh" && extension != ".lod" && extension != ".cmp") {
            ++skipped["appearance " + extension];
            return false;
        }
        const Model* found = model_for(source, building);
        if (found == nullptr) {
            ++skipped["model failed"];
            return false;
        }
        const Model& model = *found;
        const Placed p = placed(node);
        std::string marks = model.building ? "\"building\": true, " : recruiter ? "\"recruiter\": true, " : "";
        if (inside >= 0) {
            marks += std::format("\"inside\": {}, \"cell\": {}, ", inside, cell);
        }
        objects << (placed_count == 0 ? "\n" : ",\n")
                << std::format("    {{\"template\": \"{}\", \"model\": \"{}\", {}\"position\": [{}, {}, {}], \"basis\": [{}, "
                               "{}, {}, {}, {}, {}, {}, {}, {}]}}",
                               node.template_name, model.file, marks, p.position.x, p.position.y, p.position.z,
                               p.basis[0], p.basis[1], p.basis[2], p.basis[3], p.basis[4], p.basis[5], p.basis[6],
                               p.basis[7], p.basis[8]);
        ++placed_count;
        for (const auto* list : {&model.collision.solids, &model.collision.floors, &model.collision.walls}) {
            for (const auto& t : *list) {
                collision.triangles.push_back({to_core(p.apply(t[0])), to_core(p.apply(t[1])), to_core(p.apply(t[2]))});
            }
        }
        for (const auto& t : model.rooms) {
            rooms.push_back({p.apply(t[0]), p.apply(t[1]), p.apply(t[2])});
        }
        for (const auto& t : model.drawn_rooms) {
            drawn_rooms.push_back({p.apply(t[0]), p.apply(t[1]), p.apply(t[2])});
        }
        return true;
    };

    for (const SnapshotNode& node : nodes) {
        const bool recruiter = node.parent_id == -1;
        if (node.parent_id != 0 && !recruiter) {
            continue;
        }
        if (std::hypot(node.position.x - city.center_x, node.position.z - city.center_z) > city.radius) {
            continue;
        }
        const int index = int(placed_count);
        if (!place(node, recruiter, -1, 0)) {
            continue;
        }
        // A building's furniture: in its cells, placed in building space
        // (its cells sit at its origin), moved with it.
        for (const SnapshotNode* cell : children[node.id]) {
            for (const SnapshotNode* item : children[cell->id]) {
                SnapshotNode world = *item;
                world.rotation = node.rotation * item->rotation;
                world.position = rotate(node.rotation, item->position) + node.position;
                furniture += place(world, false, index, cell->cell_index) ? 1 : 0;
            }
        }
        // Its doors: in their doorways, sliding open as Galaxies' door style
        // says when someone comes near (drawn only: doorways don't block).
        const SharedObjectTemplate& look = templates.shared(node.template_name);
        const Model* building = look.portal_layout.empty() ? nullptr : model_for(look.portal_layout, true);
        if (building == nullptr) {
            continue;
        }
        const Mat4 at = Mat4::from_rotation_translation(normalized(node.rotation), node.position);
        for (const Model::Door& door : building->doors) {
            const auto style = door_styles.find(door.style);
            if (style == door_styles.end() || style->second.appearance.empty()) {
                ++skipped["door style unknown"];
                continue;
            }
            const Model* model = model_for(style->second.appearance, false);
            if (model == nullptr) {
                ++skipped["door model failed"];
                continue;
            }
            const Mat4 world = at * door.transform;
            const Placed p = placed(world);
            // The slide, from the door's own space to the world's (game).
            const Vec3 m = style->second.move;
            const Vec3 slide = to_gltf(transform_point(world, m) - transform_point(world, Vec3{}));
            objects << ",\n"
                    << std::format("    {{\"template\": \"door:{}\", \"model\": \"{}\", \"inside\": {}, \"cell\": {}, "
                                   "\"door\": {{\"slide\": [{}, {}, {}], \"open\": {}, \"close\": {}, \"radius\": {}}}, "
                                   "\"position\": [{}, {}, {}], \"basis\": [{}, {}, {}, {}, {}, {}, {}, {}, {}]}}",
                                   door.style, model->file, index, door.cell, slide.x, slide.y, slide.z,
                                   style->second.open_time, style->second.close_time, style->second.radius,
                                   p.position.x, p.position.y, p.position.z, p.basis[0], p.basis[1], p.basis[2],
                                   p.basis[3], p.basis[4], p.basis[5], p.basis[6], p.basis[7], p.basis[8]);
            ++placed_count;
            ++doors;
        }
    }
    objects << "\n  ]\n}\n";

    const std::filesystem::path objects_path = city.bake.string() + ".objects.json";
    std::ofstream(objects_path, std::ios::binary) << objects.str();
    core::save_map_collision(collision, core::map_collision_path(city.bake));
    core::TerrainHoles holes;
    if (!rooms.empty()) {
        const core::Heightfield ground = core::load_bake(city.bake);
        holes = room_terrain_holes(rooms, drawn_rooms, [&](double x, double z) { return ground.height_at(x, z); });
        core::save_terrain_holes(holes, city.bake.string() + ".holes");
    }
    log << std::format("placed {} objects ({} inside buildings, {} doors, {} models) in {}; {} collision triangles; "
                       "terrain holes {} x {} cells over the rooms\n",
                       placed_count, furniture, doors, models.size(), out_dir.string(), collision.triangles.size(),
                       holes.width, holes.height);
    for (const auto& [why, count] : skipped) {
        log << std::format("  not placed: {} ({})\n", why, count);
    }
    for (const auto& [kind, count] : collisions.skipped()) {
        log << std::format("  collision not read: {} ({})\n", kind, count);
    }
    if (!city.hub.empty()) {
        export_people(library, city, out_dir, log);
    }
}

core::TerrainHoles room_terrain_holes(const std::vector<std::array<Vec3, 3>>& walked,
                                      const std::vector<std::array<Vec3, 3>>& drawn,
                                      const std::function<double(double, double)>& terrain_height) {
    core::TerrainHoles holes;
    if (walked.empty() && drawn.empty()) {
        return holes;
    }
    double lo_x = 1e30, lo_z = 1e30, hi_x = -1e30, hi_z = -1e30;
    for (const auto* rooms : {&walked, &drawn}) {
        for (const auto& t : *rooms) {
            for (const Vec3& v : t) {
                lo_x = std::min(lo_x, double(v.x)), hi_x = std::max(hi_x, double(v.x));
                lo_z = std::min(lo_z, double(v.z)), hi_z = std::max(hi_z, double(v.z));
            }
        }
    }
    holes.x0 = std::floor(lo_x) - 1;
    holes.z0 = std::floor(lo_z) - 1;
    holes.width = int(std::ceil((hi_x + 1 - holes.x0) / holes.cell));
    holes.height = int(std::ceil((hi_z + 1 - holes.z0) / holes.cell));
    holes.cells.assign(size_t(holes.width) * size_t(holes.height), 0);
    const auto side = [](double ax, double az, double bx, double bz, double px, double pz) {
        return (bx - ax) * (pz - az) - (bz - az) * (px - ax);
    };
    const auto mark = [&](const std::vector<std::array<Vec3, 3>>& rooms, bool walked_on) {
        for (const auto& t : rooms) {
            const double x_lo = std::min({t[0].x, t[1].x, t[2].x}), x_hi = std::max({t[0].x, t[1].x, t[2].x});
            const double z_lo = std::min({t[0].z, t[1].z, t[2].z}), z_hi = std::max({t[0].z, t[1].z, t[2].z});
            for (int row = int((z_lo - holes.z0) / holes.cell); row <= int((z_hi - holes.z0) / holes.cell); ++row) {
                for (int column = int((x_lo - holes.x0) / holes.cell); column <= int((x_hi - holes.x0) / holes.cell);
                     ++column) {
                    const double px = holes.x0 + (column + 0.5) * holes.cell;
                    const double pz = holes.z0 + (row + 0.5) * holes.cell;
                    const double a = side(t[0].x, t[0].z, t[1].x, t[1].z, px, pz);
                    const double b = side(t[1].x, t[1].z, t[2].x, t[2].z, px, pz);
                    const double c = side(t[2].x, t[2].z, t[0].x, t[0].z, px, pz);
                    const bool center = (a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0);
                    if (center && walked_on) {
                        holes.set(column, row);
                    } else if ((center || square_meets_triangle(holes.x0 + column * holes.cell,
                                                                holes.z0 + row * holes.cell, holes.cell, t)) &&
                               terrain_height(px, pz) > plane_height(t, px, pz) + 0.02) {
                        holes.hide(column, row);  // the floor covers terrain under it
                    }
                }
            }
        }
    };
    mark(walked, true);
    mark(drawn, false);
    return holes;
}

} // namespace swm::swg
