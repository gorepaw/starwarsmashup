#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "common/math.h"

namespace swm::swg {

// A portalized building (.pob, PRTO 0003/0004): cells (rooms) connected by
// portals (doorway polygons). Cell 0 is the exterior shell; the rest are
// interiors. All cells share the building's coordinate frame (SWG space).
struct PortalLayout {
    struct CellPortal {
        int32_t portal = 0;       // index into `portals`
        int32_t target_cell = 0;  // the cell on the other side
        bool passable = true;
        bool clockwise = false;   // which side of the shared polygon faces this cell
        std::string door_style;   // empty: open doorway
        bool has_door_hardpoint = false;
        Mat4 door_transform;      // where the door object sits
    };

    struct Light {
        enum class Kind { ambient = 0, parallel = 1, point = 2 };
        Kind kind = Kind::ambient;
        std::array<float, 4> diffuse{};   // a, r, g, b
        std::array<float, 4> specular{};  // a, r, g, b
        // Parallel lights shine along -Z: +Z points toward the light. Inferred
        // from the usual cell rig of two bright warm lights and one dim bluish
        // one: this way the bright ones shine down and the dim one up from the
        // floor (a bounce light). The other reading would light rooms from below.
        Mat4 transform;
        std::array<float, 3> attenuation{};  // constant, linear, quadratic
    };

    struct Cell {
        std::string name;
        bool can_see_parent = false;  // the exterior is visible from inside
        std::string appearance;       // .msh or .lod
        std::string floor;            // .flr, or empty
        std::vector<CellPortal> portals;
        std::vector<Light> lights;
    };

    std::vector<std::vector<Vec3>> portals;  // shared doorway polygons
    std::vector<Cell> cells;
};

// Throws std::runtime_error on unsupported or malformed data.
PortalLayout parse_portal_layout(std::span<const uint8_t> bytes);

// A walkable floor (.flr, FLOR 0003-0006): the triangles characters stand on.
// Characters can't leave a floor across an uncrossable edge, so a floor's
// uncrossable boundary edges are its walls. Furniture and walls are cut out
// of the floor, and stairs are ramps.
struct FloorMesh {
    struct Triangle {
        std::array<uint32_t, 3> corners{};
        // Per edge k (corner k to corner k+1):
        std::array<int32_t, 3> neighbors{};  // triangle across the edge, or -1 on the boundary
        std::array<bool, 3> crossable{};     // interior edges, doorways, and exits to open ground
        std::array<int32_t, 3> portal{};     // index into the cell's portals for doorway edges, else -1
    };
    std::vector<Vec3> vertices;
    std::vector<Triangle> triangles;
};

FloorMesh parse_floor(std::span<const uint8_t> bytes);

// The floor's walls: boundary edges that can't be crossed, as vertex pairs.
std::vector<std::array<Vec3, 2>> floor_walls(const FloorMesh& floor);

} // namespace swm::swg
