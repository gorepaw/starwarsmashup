#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "swm/core/heightfield.h"
#include "swm/core/vec3.h"

namespace swm::core {

// A building's walkable surfaces, in building space (game axes), as written
// by `swgtool export-building` (<name>.collision). Floors are Galaxies floor
// triangles; walls rise from the floors' uncrossable boundary edges, the
// same ones Galaxies stops characters at.
struct BuildingCollision {
    struct Floor {
        Vec3 a, b, c;
        int cell = 0;
    };
    struct Wall {
        Vec3 a, b;           // along the floor edge
        double height = 3;   // rises this far above the edge
    };
    std::vector<Floor> floors;
    std::vector<Wall> walls;
};

void save_building_collision(const BuildingCollision& collision, const std::filesystem::path& path);
// Throws std::runtime_error on a missing or malformed file.
BuildingCollision load_building_collision(const std::filesystem::path& path);

// Where a building stands: game-space position, and yaw in radians about +Y
// (Godot's rotation.y).
struct Placement {
    Vec3 position;
    double yaw = 0;
};

// A placement from Galaxies coordinates and yaw in degrees, as the world
// snapshot lists buildings (`swgtool snapshot`).
Placement placement_from_galaxies(double x, double y, double z, double yaw_degrees);

// Everything characters collide with: the terrain plus placed buildings.
// The server and every client build the same World, so movement predicts
// exactly.
class World {
public:
    explicit World(const Heightfield& terrain) : terrain_(terrain) {}

    void add_building(const BuildingCollision& collision, const Placement& placement);

    struct Ground {
        double height = 0;
        Vec3 normal{0, 1, 0};
        bool on_floor = false;  // a building floor rather than terrain
    };
    // The surface a character with feet at `y` stands on at (x, z): the
    // highest floor or terrain at most `step_up` above its feet. If nothing
    // is that low, the terrain (which then pushes it up, as on a hillside).
    Ground ground_at(double x, double z, double y, double step_up) const;

    // Pushes a character's body (an upright cylinder of `radius` and
    // `height`, feet at position.y) out of the walls it overlaps after moving
    // from `from`, and removes the velocity going into them, so it slides.
    // Only x and z change.
    Vec3 resolve_walls(Vec3 from, Vec3 position, double radius, double height, double step_up,
                       Vec3& velocity) const;

    // Where along the segment a..b (0..1) it first hits the terrain, a
    // building floor, or a wall, if it does. For shots.
    std::optional<double> raycast(Vec3 a, Vec3 b) const;

    Vec3 clamp(Vec3 position) const { return terrain_.clamp(position); }
    const Heightfield& terrain() const { return terrain_; }

private:
    struct Floor {
        Vec3 a, b, c;
        double min_x, max_x, min_z, max_z;
    };
    struct Wall {
        Vec3 a, b;
        double bottom, top;
    };
    struct Building {
        std::vector<Floor> floors;
        std::vector<Wall> walls;
        double min_x, max_x, min_z, max_z;  // of everything, for skipping far buildings
    };

    const Heightfield& terrain_;
    std::vector<Building> buildings_;
};

} // namespace swm::core
