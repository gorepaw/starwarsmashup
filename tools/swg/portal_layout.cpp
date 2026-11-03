#include "swg/portal_layout.h"

#include <algorithm>
#include <stdexcept>

#include "swg/iff.h"

namespace swm::swg {
namespace {

Vec3 read_vec3(IffReader& r) {
    const float x = r.f32(), y = r.f32();
    return {x, y, r.f32()};
}

// 3x4 row-major rotation + translation.
Mat4 read_transform(IffReader& r) {
    Mat4 m;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            m.at(row, column) = r.f32();
        }
    }
    return m;
}

const IffNode& version_of(const IffNode& node) {
    for (const IffNode& child : node.children) {
        if (child.form) {
            return child;
        }
    }
    throw std::runtime_error("'" + node.tag + "' has no version form");
}

// A portal polygon: a PRTL chunk (count + vertices), or an IDTL form whose
// vertices are used in order.
std::vector<Vec3> parse_portal_polygon(const IffNode& node) {
    std::vector<Vec3> polygon;
    if (!node.form && node.tag == "PRTL") {
        IffReader r(node.data);
        const int32_t count = r.i32();
        for (int32_t i = 0; i < count; ++i) {
            polygon.push_back(read_vec3(r));
        }
    } else if (node.form && node.tag == "IDTL") {
        IffReader r(version_of(node).expect("VERT").data);
        while (r.remaining() >= 12) {
            polygon.push_back(read_vec3(r));
        }
    } else {
        throw std::runtime_error("unexpected portal chunk '" + node.tag + "'");
    }
    return polygon;
}

PortalLayout::CellPortal parse_cell_portal(const IffNode& prtl) {
    // PRTL -> a version chunk. 0005 adds a leading byte to 0004 (always 0 in
    // the reference set; ignored).
    const IffNode* data = prtl.child("0004");
    const IffNode* data5 = prtl.child("0005");
    if ((!data || data->form) && (!data5 || data5->form)) {
        throw std::runtime_error("unsupported cell portal version");
    }
    IffReader r(data ? data->data : data5->data);
    PortalLayout::CellPortal portal;
    if (!data) {
        r.u8();
    }
    portal.passable = r.u8() != 0;
    portal.portal = r.i32();
    portal.clockwise = r.u8() != 0;
    portal.target_cell = r.i32();
    portal.door_style = r.cstring();
    portal.has_door_hardpoint = r.u8() != 0;
    portal.door_transform = read_transform(r);
    return portal;
}

std::vector<PortalLayout::Light> parse_lights(const IffNode& lght) {
    IffReader r(lght.data);
    std::vector<PortalLayout::Light> lights(size_t(std::max(r.i32(), 0)));
    for (PortalLayout::Light& light : lights) {
        light.kind = PortalLayout::Light::Kind(r.u8());
        for (float& v : light.diffuse) {
            v = r.f32();
        }
        for (float& v : light.specular) {
            v = r.f32();
        }
        light.transform = read_transform(r);
        for (float& v : light.attenuation) {
            v = r.f32();
        }
    }
    return lights;
}

PortalLayout::Cell parse_cell(const IffNode& cell_form) {
    // 0004 and 0005 share the DATA layout; 0005 adds a collision form.
    const IffNode& version = version_of(cell_form);
    if (version.tag != "0004" && version.tag != "0005") {
        throw std::runtime_error("unsupported cell version " + version.tag);
    }
    IffReader r(version.expect("DATA").data);
    PortalLayout::Cell cell;
    const int32_t portal_count = r.i32();
    cell.can_see_parent = r.u8() != 0;
    cell.name = r.cstring();
    cell.appearance = r.cstring();
    // Without a floor, the floor path may be empty or missing entirely.
    const bool has_floor = r.u8() != 0;
    const std::string floor = has_floor || r.remaining() > 0 ? r.cstring() : std::string();
    if (has_floor) {
        cell.floor = floor;
    }
    for (const IffNode* prtl : version.children_with("PRTL")) {
        if (prtl->form) {
            cell.portals.push_back(parse_cell_portal(*prtl));
        }
    }
    if (int32_t(cell.portals.size()) != portal_count) {
        throw std::runtime_error("cell '" + cell.name + "': portal count mismatch");
    }
    if (const IffNode* lght = version.child("LGHT")) {
        cell.lights = parse_lights(*lght);
    }
    return cell;
}

} // namespace

PortalLayout parse_portal_layout(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "PRTO") {
        throw std::runtime_error("not a portal layout (" + root.tag + ")");
    }
    const IffNode& version = version_of(root);
    if (version.tag != "0003" && version.tag != "0004") {
        throw std::runtime_error("unsupported portal layout version " + version.tag);
    }
    IffReader counts(version.expect("DATA").data);
    const int32_t portal_count = counts.i32();
    const int32_t cell_count = counts.i32();

    PortalLayout layout;
    for (const IffNode& portal : version.expect("PRTS").children) {
        layout.portals.push_back(parse_portal_polygon(portal));
    }
    for (const IffNode* cell : version.expect("CELS").children_with("CELL")) {
        layout.cells.push_back(parse_cell(*cell));
    }
    if (int32_t(layout.portals.size()) != portal_count || int32_t(layout.cells.size()) != cell_count) {
        throw std::runtime_error("portal layout: portal or cell count mismatch");
    }
    for (const PortalLayout::Cell& cell : layout.cells) {
        for (const PortalLayout::CellPortal& portal : cell.portals) {
            if (portal.portal < 0 || portal.portal >= portal_count || portal.target_cell < 0 ||
                portal.target_cell >= cell_count) {
                throw std::runtime_error("cell '" + cell.name + "': portal reference out of range");
            }
        }
    }
    return layout;
}

FloorMesh parse_floor(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "FLOR") {
        throw std::runtime_error("not a floor (" + root.tag + ")");
    }
    const IffNode& version = version_of(root);
    if (version.tag != "0003" && version.tag != "0004" && version.tag != "0005" && version.tag != "0006") {
        throw std::runtime_error("unsupported floor version " + version.tag);
    }
    // 0006 prefixes VERT and TRIS with counts; older versions don't.
    const size_t prefix = version.tag == "0006" ? 4 : 0;
    FloorMesh floor;
    const std::span<const uint8_t> vert = version.expect("VERT").data;
    if (vert.size() < prefix || (vert.size() - prefix) % 12 != 0) {
        throw std::runtime_error("floor: unexpected vertex data size");
    }
    IffReader vertices(vert.subspan(prefix));
    floor.vertices.resize((vert.size() - prefix) / 12);
    for (Vec3& v : floor.vertices) {
        v = read_vec3(vertices);
    }

    // TRIS: 60-byte records: corners (3 x i32), the triangle's own index,
    // neighbors (3 x i32), normal (3 x f32), edge types (3 x u8; 0 uncrossable,
    // 1 crossable), a fallthrough flag (u8), a part tag (i32), and portal ids
    // (3 x i32). Edge semantics were checked on the Mos Eisley cantina: every
    // edge with a neighbor is crossable; on the boundary, exactly one edge per
    // cell portal is crossable and carries that portal's index.
    constexpr size_t record_size = 60;
    const std::span<const uint8_t> tris = version.expect("TRIS").data;
    if (tris.size() < prefix || (tris.size() - prefix) % record_size != 0) {
        throw std::runtime_error("floor: unexpected triangle record size");
    }
    const size_t count = (tris.size() - prefix) / record_size;
    for (size_t i = 0; i < count; ++i) {
        IffReader r(tris.subspan(prefix + i * record_size, record_size));
        FloorMesh::Triangle triangle;
        for (uint32_t& corner : triangle.corners) {
            const int32_t index = r.i32();
            if (index < 0 || size_t(index) >= floor.vertices.size()) {
                throw std::runtime_error("floor: vertex index out of range");
            }
            corner = uint32_t(index);
        }
        r.i32();  // own index
        for (int32_t& neighbor : triangle.neighbors) {
            neighbor = r.i32();
            if (neighbor >= int32_t(count)) {
                throw std::runtime_error("floor: neighbor index out of range");
            }
        }
        r.f32();
        r.f32();
        r.f32();  // normal
        for (bool& crossable : triangle.crossable) {
            crossable = r.u8() != 0;
        }
        r.u8();   // fallthrough
        r.i32();  // part tag
        for (int32_t& portal : triangle.portal) {
            portal = r.i32();
        }
        floor.triangles.push_back(triangle);
    }
    return floor;
}

std::vector<std::array<Vec3, 2>> floor_walls(const FloorMesh& floor) {
    std::vector<std::array<Vec3, 2>> walls;
    for (const FloorMesh::Triangle& t : floor.triangles) {
        for (size_t k = 0; k < 3; ++k) {
            if (t.neighbors[k] < 0 && !t.crossable[k]) {
                walls.push_back({floor.vertices[t.corners[k]], floor.vertices[t.corners[(k + 1) % 3]]});
            }
        }
    }
    return walls;
}

} // namespace swm::swg
