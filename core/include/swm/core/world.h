#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
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

// What a map triangle stops (bits).
enum Blocks : uint8_t { blocks_characters = 1, blocks_shots = 2 };

// The collision of a map's props, in game space: triangles, each stopping
// characters, shots or both. bf2tool export-map writes it next to the
// terrain bake (map_collision_path).
// A triangle may belong to a destructible object (Battlefield::destructibles):
// `part` n > 0 while destructible n - 1 stands, -n once it's destroyed (its
// wreck's), 0 always. Plus moved_part: something that destroying it moves
// (Kashyyyk's gate, its panel's doing), there or not as its own part would
// be but not the destructible itself to shots.
inline constexpr int16_t moved_part = 16384;
struct MapCollision {
    struct Triangle {
        Vec3 a, b, c;
        uint8_t blocks = blocks_characters | blocks_shots;
        int16_t part = 0;
    };
    std::vector<Triangle> triangles;
};

void save_map_collision(const MapCollision& collision, const std::filesystem::path& path);
// Throws std::runtime_error on a missing or malformed file.
MapCollision load_map_collision(const std::filesystem::path& path);
// Where a terrain bake's map collision is: <bake prefix>.collision.
std::filesystem::path map_collision_path(const std::filesystem::path& bake);

// Where a building stands: game-space position, and yaw in radians about +Y
// (Godot's rotation.y).
struct Placement {
    Vec3 position;
    double yaw = 0;
};

// A placement from Galaxies coordinates and yaw in degrees, as the world
// snapshot lists buildings (`swgtool snapshot`).
Placement placement_from_galaxies(double x, double y, double z, double yaw_degrees);

// Everything characters collide with: the terrain, placed buildings and a
// map's props.
// The server and every client build the same World, so movement predicts
// exactly.
class World {
public:
    explicit World(const Heightfield& terrain) : terrain_(terrain) {}

    void add_building(const BuildingCollision& collision, const Placement& placement);

    // A map's props, their triangles facing outward. Gentle triangles (up to
    // 55 degrees) facing up that stop characters are ground they can stand
    // on; facing down, undersides, which characters pass; steeper ones push
    // them out, like walls (except one a character is already deep inside,
    // which it can walk out of). Shots stop at triangles that stop shots.
    void add_map_collision(const MapCollision& collision);
    // Adds the map collision next to a terrain bake, if there is one; returns
    // its triangle count (0 if none). Throws on a malformed file.
    size_t add_map_collision_of(const std::filesystem::path& bake);

    struct Ground {
        double height = 0;
        Vec3 normal{0, 1, 0};   // a building's floor's: straight up
        bool on_floor = false;  // a building floor rather than terrain
    };
    // The surface a character with feet at `y` stands on at (x, z): the
    // highest floor or terrain at most `step_up` above its feet. The terrain
    // doesn't count where it's cut away. If nothing is that low: the terrain
    // if it's a little above the feet (which then pushes it up, as on a
    // hillside), else (deep under it, or through a hole) a height of -1e9:
    // falling.
    Ground ground_at(double x, double z, double y, double step_up) const;
    // What a body stands on there: the ground under its middle, or, over a
    // seam narrower than a body (one in Mygeeto's floor, under a spawn
    // point), the floor on both sides of it, `seam` off either way. Past a
    // ledge's edge, with floor on one side only, the ground under its middle.
    Ground support_at(double x, double z, double y, double step_up, double seam = 0.25) const;

    // Pushes a character's body (an upright cylinder of `radius` and
    // `height`, feet at position.y) out of the walls it overlaps after moving
    // from `from`, and removes the velocity going into them, so it slides.
    // Only x and z change.
    Vec3 resolve_walls(Vec3 from, Vec3 position, double radius, double height, double step_up,
                       Vec3& velocity) const;
    // Whether any wall overlaps a body standing at `feet` (including props it
    // would be inside of, which resolve_walls lets it walk out of).
    bool overlaps_walls(Vec3 feet, double radius, double height, double step_up) const;

    // Where along the segment a..b (0..1) it first hits the terrain, a
    // building floor, or a wall, if it does. For shots.
    struct RayHit {
        double t = 0;
        Vec3 normal{0, 1, 0};  // unit, facing the incoming ray
        int destructible = -1;  // the standing destructible it hit (index), or -1
    };
    std::optional<RayHit> raycast_hit(Vec3 a, Vec3 b) const;
    std::optional<double> raycast(Vec3 a, Vec3 b) const;
    // Whether something stops a character going straight from a to b: the
    // terrain, a building, or a map triangle that stops characters, met
    // from the front (one it's behind, inside a prop, it walks out of).
    bool walk_blocked(Vec3 a, Vec3 b) const;
    // Whether a map floor (a gentle triangle facing up that stops
    // characters) is over (x, z) at a height in (low, high].
    bool floor_between(double x, double z, double low, double high) const;
    // Whether `point` is shut inside a map model: looking up and level all
    // around (within `reach`), the first triangle stopping characters is
    // met from behind every way. Models are skins, solid only from outside;
    // the ground inside one (Yavin's minitemple, a stepped pyramid) is no
    // place to stand.
    bool inside_solid(Vec3 point, double reach) const;

    // Destructible `index` destroyed (its triangles gone, its wreck's
    // there) or standing (as at first). The server sets this from its match
    // and each client from the server's snapshots, so both see the same
    // world. It's state on a const World, deliberately: everyone sharing a
    // world (a battle's server and its bots) shares the battle.
    void set_destroyed(size_t index, bool destroyed) const;
    bool destroyed(size_t index) const { return index < destroyed_.size() && destroyed_[index]; }

    Vec3 clamp(Vec3 position) const { return terrain_.clamp(position); }
    // The highest point of any building or map triangle (-inf with none).
    double top() const { return top_; }
    const Heightfield& terrain() const { return terrain_; }

private:
    std::optional<RayHit> cast(Vec3 a, Vec3 b, uint8_t blocks, bool front_only) const;
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

    // A map triangle, with what it's for and its bounds.
    struct Solid {
        Vec3 a, b, c;
        Vec3 normal;  // unit
        double min_x, max_x, min_y, max_y, min_z, max_z;
        uint8_t blocks;
        bool floor;    // facing up, gentle enough to stand on
        bool ceiling;  // facing down as gently: an underside, neither ground nor wall
        int16_t part = 0;  // as MapCollision::Triangle's
    };
    // Whether a solid is there now (its destructible's state).
    bool present(const Solid& s) const {
        const int part = s.part > 0 ? s.part % moved_part : -(-s.part % moved_part);
        return part == 0 || (part > 0 ? !destroyed(size_t(part - 1)) : destroyed(size_t(-part - 1)));
    }
    // resolve_walls; with let_out false, props the body started inside push too.
    Vec3 push_out(Vec3 from, Vec3 position, double radius, double height, double step_up, Vec3& velocity,
                  bool let_out) const;

    // Calls visit(solid) for the solids in grid cells overlapping the XZ box
    // (a solid in several cells comes up once per cell).
    template <class Visit>
    void for_solids(double min_x, double max_x, double min_z, double max_z, Visit&& visit) const;
    // ...in the cells the segment a..b passes through.
    template <class Visit>
    void for_solids_along(Vec3 a, Vec3 b, Visit&& visit) const;

    const Heightfield& terrain_;
    std::vector<Building> buildings_;
    std::vector<Solid> solids_;
    mutable std::vector<uint8_t> destroyed_;  // per destructible
    double top_ = -std::numeric_limits<double>::infinity();
    static constexpr double grid_cell_ = 4;  // meters
    double grid_x0_ = 0, grid_z0_ = 0;
    int grid_w_ = 0, grid_h_ = 0;
    std::vector<std::vector<uint32_t>> grid_;  // solid indices per cell, rows along +Z
};

// A world on a bake (swgtool bake-terrain, or bf2tool export-map) with the
// map collision next to it, owning its terrain. Throws std::runtime_error if
// the bake can't be read.
std::shared_ptr<const World> load_world(const std::filesystem::path& bake);

} // namespace swm::core
