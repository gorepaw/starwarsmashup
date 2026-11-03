#include "swg/building_export.h"

#include <format>
#include <fstream>
#include <ostream>
#include <stdexcept>

#include "swg/gltf_assets.h"
#include "swg/portal_layout.h"

namespace swm::swg {
namespace {

std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

std::string json_vec3(Vec3 v) {
    return std::format("[{:.4f},{:.4f},{:.4f}]", v.x, v.y, v.z);
}

const char* light_kind(PortalLayout::Light::Kind kind) {
    switch (kind) {
    case PortalLayout::Light::Kind::ambient: return "ambient";
    case PortalLayout::Light::Kind::parallel: return "parallel";
    case PortalLayout::Light::Kind::point: return "point";
    }
    return "unknown";
}

int add_floor_mesh(Gltf& gltf, const FloorMesh& floor, const std::string& name) {
    std::vector<Vec3> positions;
    for (const Vec3& v : floor.vertices) {
        positions.push_back(to_gltf(v));
    }
    std::vector<uint32_t> indices;
    for (const FloorMesh::Triangle& t : floor.triangles) {
        append_triangle(indices, t.corners[0], t.corners[1], t.corners[2]);
    }
    Gltf::Primitive primitive;
    primitive.attributes["POSITION"] = gltf.add_vec3(positions, true);
    primitive.indices = gltf.add_indices(indices);
    gltf.meshes.push_back({.name = name, .primitives = {primitive}});
    return int(gltf.meshes.size() - 1);
}

// Collision walls: a vertical quad rising from each uncrossable floor edge.
// Each quad is emitted with both windings so it blocks from either side.
int add_wall_mesh(Gltf& gltf, const FloorMesh& floor, const std::string& name) {
    constexpr float wall_height = 3.0f;
    std::vector<Vec3> positions;
    std::vector<uint32_t> indices;
    for (const auto& [a, b] : floor_walls(floor)) {
        const uint32_t base = uint32_t(positions.size());
        positions.insert(positions.end(), {to_gltf(a), to_gltf(b), to_gltf(b + Vec3{0, wall_height, 0}),
                                           to_gltf(a + Vec3{0, wall_height, 0})});
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        indices.insert(indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }
    if (positions.empty()) {
        return -1;
    }
    Gltf::Primitive primitive;
    primitive.attributes["POSITION"] = gltf.add_vec3(positions, true);
    primitive.indices = gltf.add_indices(indices);
    gltf.meshes.push_back({.name = name, .primitives = {primitive}});
    return int(gltf.meshes.size() - 1);
}

std::string sidecar_json(const PortalLayout& layout, const std::string& pob_path) {
    std::string json = std::format("{{\n  \"source\": {},\n  \"portals\": [", json_string(pob_path));
    for (size_t p = 0; p < layout.portals.size(); ++p) {
        json += p ? ",\n    [" : "\n    [";
        for (size_t v = 0; v < layout.portals[p].size(); ++v) {
            json += (v ? "," : "") + json_vec3(to_gltf(layout.portals[p][v]));
        }
        json += "]";
    }
    json += "\n  ],\n  \"cells\": [";
    for (size_t c = 0; c < layout.cells.size(); ++c) {
        const PortalLayout::Cell& cell = layout.cells[c];
        json += std::format("{}\n    {{\"name\": {}, \"appearance\": {}, \"floor\": {}, \"can_see_parent\": {},\n"
                            "     \"portals\": [",
                            c ? "," : "", json_string(cell.name), json_string(cell.appearance), json_string(cell.floor),
                            cell.can_see_parent);
        for (size_t p = 0; p < cell.portals.size(); ++p) {
            const PortalLayout::CellPortal& portal = cell.portals[p];
            json += std::format("{}{{\"portal\": {}, \"target_cell\": {}, \"passable\": {}, \"door\": {}}}",
                                p ? ", " : "", portal.portal, portal.target_cell, portal.passable,
                                json_string(portal.door_style));
        }
        json += "],\n     \"lights\": [";
        for (size_t l = 0; l < cell.lights.size(); ++l) {
            const PortalLayout::Light& light = cell.lights[l];
            const Mat4 m = to_gltf(light.transform);
            const Vec3 position{m.at(0, 3), m.at(1, 3), m.at(2, 3)};
            // The direction light travels: the transform's -Z (see PortalLayout::Light).
            const Vec3 direction = normalized(Vec3{-m.at(0, 2), -m.at(1, 2), -m.at(2, 2)});
            json += std::format(
                "{}\n       {{\"kind\": \"{}\", \"color\": [{:.3f},{:.3f},{:.3f}], \"position\": {}, \"direction\": {}, "
                "\"attenuation\": [{:.4f},{:.4f},{:.4f}]}}",
                l ? "," : "", light_kind(light.kind), light.diffuse[1], light.diffuse[2], light.diffuse[3],
                json_vec3(position), json_vec3(direction), light.attenuation[0], light.attenuation[1],
                light.attenuation[2]);
        }
        json += "]}";
    }
    return json + "\n  ]\n}\n";
}

} // namespace

void export_building(TreLibrary& library, const std::string& pob_path, const std::string& name,
                     const std::filesystem::path& out_dir, std::ostream& log) {
    const PortalLayout layout = parse_portal_layout(library.read(pob_path));
    log << std::format("  {}: {} cells, {} portals\n", pob_path, layout.cells.size(), layout.portals.size());
    std::filesystem::create_directories(out_dir);

    Gltf gltf;
    GltfAssets assets(library, gltf, out_dir, log);
    gltf.nodes.push_back({.name = name});
    for (const PortalLayout::Cell& cell : layout.cells) {
        const int cell_node = int(gltf.nodes.size());
        gltf.nodes.push_back({.name = cell.name});
        gltf.nodes[0].children.push_back(cell_node);

        const int mesh = assets.add_static_mesh(load_static_appearance(library, cell.appearance), cell.name);
        gltf.nodes.push_back({.name = cell.name + "_mesh", .mesh = mesh});
        gltf.nodes[size_t(cell_node)].children.push_back(int(gltf.nodes.size() - 1));

        if (!cell.floor.empty()) {
            const FloorMesh floor = parse_floor(library.read(cell.floor));
            gltf.nodes.push_back({.name = cell.name + "_floor", .mesh = add_floor_mesh(gltf, floor, cell.name + "_floor")});
            gltf.nodes[size_t(cell_node)].children.push_back(int(gltf.nodes.size() - 1));
            if (const int walls = add_wall_mesh(gltf, floor, cell.name + "_walls"); walls >= 0) {
                gltf.nodes.push_back({.name = cell.name + "_walls", .mesh = walls});
                gltf.nodes[size_t(cell_node)].children.push_back(int(gltf.nodes.size() - 1));
            }
        }
        log << std::format("    {}: {} ({} lights, {} portals)\n", cell.name, cell.appearance, cell.lights.size(),
                           cell.portals.size());
    }
    gltf.scene_nodes = {0};
    gltf.write(out_dir / (name + ".gltf"));

    std::ofstream sidecar(out_dir / (name + ".json"), std::ios::binary);
    sidecar << sidecar_json(layout, pob_path);
    if (!sidecar) {
        throw std::runtime_error("cannot write " + (out_dir / (name + ".json")).string());
    }
}

} // namespace swm::swg
