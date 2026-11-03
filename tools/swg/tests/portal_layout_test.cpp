// Builds a small synthetic building and floor (no game data) and checks parsing.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "swg/portal_layout.h"

using namespace swm;
using namespace swm::swg;

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

using Bytes = std::vector<uint8_t>;

Bytes chunk(const char* tag, const Bytes& payload) {
    Bytes out(tag, tag + 4);
    const auto size = static_cast<uint32_t>(payload.size());
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>(size >> shift));
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

Bytes form(const char* name, const std::vector<Bytes>& children) {
    Bytes payload(name, name + 4);
    for (const Bytes& child : children) {
        payload.insert(payload.end(), child.begin(), child.end());
    }
    return chunk("FORM", payload);
}

template <class T>
void put(Bytes& out, T value) {
    uint8_t raw[sizeof(T)];
    std::memcpy(raw, &value, sizeof(T));
    out.insert(out.end(), raw, raw + sizeof(T));
}

void put_string(Bytes& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
}

void put_transform(Bytes& out, Vec3 t) {
    const float rows[3][4] = {{1, 0, 0, t.x}, {0, 1, 0, t.y}, {0, 0, 1, t.z}};
    for (const auto& row : rows) {
        for (const float v : row) {
            put(out, v);
        }
    }
}

// A cell portal (PRTL 0004).
Bytes cell_portal(int32_t portal, int32_t target, const std::string& door) {
    Bytes data;
    put<uint8_t>(data, 1);  // passable
    put(data, portal);
    put<uint8_t>(data, 0);  // clockwise
    put(data, target);
    put_string(data, door);
    put<uint8_t>(data, door.empty() ? 0 : 1);
    put_transform(data, {1, 0, 2});
    return form("PRTL", {chunk("0004", data)});
}

Bytes cell(const std::string& name, const std::string& floor, const std::vector<Bytes>& portals, const Bytes& lights) {
    Bytes data;
    put(data, int32_t(portals.size()));
    put<uint8_t>(data, 1);  // can see parent
    put_string(data, name);
    put_string(data, "appearance/mesh/" + name + ".msh");
    put<uint8_t>(data, floor.empty() ? 0 : 1);
    if (!floor.empty()) {
        put_string(data, floor);
    }
    std::vector<Bytes> children{chunk("DATA", data), form("NULL", {})};
    children.insert(children.end(), portals.begin(), portals.end());
    if (!lights.empty()) {
        children.push_back(chunk("LGHT", lights));
    }
    return form("CELL", {form("0005", children)});
}

Bytes two_cell_building() {
    Bytes counts, quad;
    put<int32_t>(counts, 1);  // portals
    put<int32_t>(counts, 2);  // cells
    put<int32_t>(quad, 4);
    for (const Vec3 v : {Vec3{0, 0, 0}, Vec3{0, 3, 0}, Vec3{2, 3, 0}, Vec3{2, 0, 0}}) {
        put(quad, v.x);
        put(quad, v.y);
        put(quad, v.z);
    }
    Bytes lights;
    put<int32_t>(lights, 1);
    put<uint8_t>(lights, 2);  // point
    for (const float v : {1.0f, 0.5f, 0.25f, 0.125f, 1.0f, 0.0f, 0.0f, 0.0f}) {
        put(lights, v);  // diffuse, specular (argb)
    }
    put_transform(lights, {4, 2, 6});
    for (const float v : {0.0f, 1.0f, 0.0f}) {
        put(lights, v);
    }
    // The exterior has no floor and ends right after its "no floor" flag.
    return form("PRTO", {form("0003", {chunk("DATA", counts), form("PRTS", {chunk("PRTL", quad)}),
                                       form("CELS", {cell("r0", "", {cell_portal(0, 1, "door")}, {}),
                                                     cell("room", "floor.flr", {cell_portal(0, 0, "")}, lights)})})});
}

void test_portal_layout() {
    const PortalLayout layout = parse_portal_layout(two_cell_building());
    CHECK(layout.portals.size() == 1 && layout.portals[0].size() == 4);
    CHECK(layout.portals[0][2].x == 2 && layout.portals[0][2].y == 3);
    CHECK(layout.cells.size() == 2);
    CHECK(layout.cells[0].name == "r0" && layout.cells[0].floor.empty());
    CHECK(layout.cells[1].name == "room" && layout.cells[1].floor == "floor.flr");
    CHECK(layout.cells[1].appearance == "appearance/mesh/room.msh");
    CHECK(layout.cells[0].portals.size() == 1 && layout.cells[0].portals[0].target_cell == 1);
    CHECK(layout.cells[0].portals[0].door_style == "door" && layout.cells[0].portals[0].has_door_hardpoint);
    CHECK(layout.cells[0].portals[0].door_transform.at(2, 3) == 2);
    CHECK(layout.cells[1].lights.size() == 1);
    const PortalLayout::Light& light = layout.cells[1].lights[0];
    CHECK(light.kind == PortalLayout::Light::Kind::point);
    CHECK(light.diffuse[1] == 0.5f && light.transform.at(1, 3) == 2 && light.attenuation[1] == 1);
}

// Two triangles sharing an edge; one boundary edge is a doorway.
Bytes two_triangle_floor() {
    Bytes vert, tris;
    put<int32_t>(vert, 4);
    for (const Vec3 v : {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 0, 1}, Vec3{0, 0, 1}}) {
        put(vert, v.x);
        put(vert, v.y);
        put(vert, v.z);
    }
    put<int32_t>(tris, 2);
    auto triangle = [&](std::array<int32_t, 3> corners, int32_t index, std::array<int32_t, 3> neighbors,
                        std::array<uint8_t, 3> crossable, std::array<int32_t, 3> portals) {
        for (const int32_t c : corners) {
            put(tris, c);
        }
        put(tris, index);
        for (const int32_t n : neighbors) {
            put(tris, n);
        }
        for (const float n : {0.0f, -1.0f, 0.0f}) {
            put(tris, n);
        }
        for (const uint8_t c : crossable) {
            put(tris, c);
        }
        put<uint8_t>(tris, 0);
        put<int32_t>(tris, 0);
        for (const int32_t p : portals) {
            put(tris, p);
        }
    };
    // Triangle 0: 0-1-2; edge 0-1 is a doorway (portal 0), edge 1-2 a wall, edge 2-0 shared.
    triangle({0, 1, 2}, 0, {-1, -1, 1}, {1, 0, 1}, {0, -1, -1});
    // Triangle 1: 0-2-3; edges 2-3 and 3-0 are walls.
    triangle({0, 2, 3}, 1, {0, -1, -1}, {1, 0, 0}, {-1, -1, -1});
    return form("FLOR", {form("0006", {chunk("VERT", vert), chunk("TRIS", tris)})});
}

void test_floor() {
    const FloorMesh floor = parse_floor(two_triangle_floor());
    CHECK(floor.vertices.size() == 4 && floor.triangles.size() == 2);
    CHECK(floor.triangles[1].corners[2] == 3);
    CHECK(floor.triangles[0].neighbors[2] == 1 && floor.triangles[0].portal[0] == 0);
    CHECK(floor.triangles[0].crossable[0] && !floor.triangles[0].crossable[1]);

    const auto walls = floor_walls(floor);
    CHECK(walls.size() == 3);  // the doorway and the shared edge stay open
    for (const auto& wall : walls) {
        const bool doorway = (wall[0].x == 0 && wall[0].z == 0 && wall[1].x == 1 && wall[1].z == 0);
        CHECK(!doorway);
    }
}

void test_rejects_bad_references() {
    Bytes bytes = two_cell_building();
    // Point r0's portal at cell 7 (the target field follows passable, portal, clockwise).
    const std::string door = "door";
    const auto it = std::search(bytes.begin(), bytes.end(), door.begin(), door.end());
    CHECK(it != bytes.end());
    *(it - 4) = 7;
    bool threw = false;
    try {
        parse_portal_layout(bytes);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main() {
    test_portal_layout();
    test_floor();
    test_rejects_bad_references();

    if (failures == 0) {
        std::printf("portal_layout_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
