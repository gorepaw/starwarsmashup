#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "swm/core/conquest.h"
#include "swm/core/movement.h"
#include "swm/core/vec3.h"

namespace swm::core {

class World;

// Where a soldier can walk on a battlefield, for bots: a grid of small cells
// on the ground. A cell is walkable if a body (a little wider than a
// character's) fits there: the ground isn't too steep, no wall overlaps it
// between its knees and head, and nothing hangs over it below head height.
// One cell leads to the next if the climb is no more than a step (or a
// walkable slope); downhill always works. Barriers (Battlefront's AI
// barriers) are walkable but cost more, so paths go around them when that's
// not a long way. Cells outside the battlefield's bounds aren't walkable.
// Cells sealed inside a big solid (the middle of a boulder) look walkable but
// can't be reached; a search for one ends at the nearest place it can reach,
// after visiting everything else it may.
//
// The grid is worked out lazily, in tiles, where searches go, so a big map
// costs only where bots walk. Searches share the grid's scratch space: use
// one NavGrid from one thread.
class NavGrid {
public:
    struct Settings {
        double cell = 0.5;               // m
        double clearance = 0.15;         // m kept from walls, beyond the body's radius
        double barrier_cost = 4;         // a barrier's cells cost this many times their length
        size_t max_expansions = 120000;  // cells a search may visit
    };

    // `world` must outlive the grid. `move` gives the body's size, step and
    // steepest walkable slope.
    NavGrid(const World& world, const Battlefield& field, const MoveParams& move = {});
    NavGrid(const World& world, const Battlefield& field, const MoveParams& move, const Settings& settings);
    ~NavGrid();
    NavGrid(const NavGrid&) = delete;
    NavGrid& operator=(const NavGrid&) = delete;

    // A route for a soldier from `from` to `to`: the corners to walk to, in
    // order, on the ground (straight lines between them are walkable),
    // ending at `to`, or at the nearest place to it that can be reached.
    // Empty if `from` has nowhere to go.
    std::vector<Vec3> find_path(Vec3 from, Vec3 to) const;
    // Whether a soldier can walk straight from a to b without leaving the
    // walkable cells or entering a barrier.
    bool clear_line(Vec3 a, Vec3 b) const;
    bool walkable(Vec3 point) const;
    // Tiles worked out so far.
    size_t tiles_built() const { return built_; }

private:
    struct Tile;
    struct Cell {
        int x, z;
    };
    static constexpr int tile_bits = 5;  // 32 x 32 cells
    static constexpr int tile_size = 1 << tile_bits;
    static constexpr uint8_t walkable_bit = 1, barrier_bit = 2;

    bool inside(int x, int z) const { return x >= 0 && z >= 0 && x < width_ && z < depth_; }
    Cell cell_at(Vec3 p) const;
    Vec3 center(int x, int z) const;
    Tile& tile(int x, int z) const;  // the tile holding cell (x, z), worked out if it isn't yet
    void build(Tile& tile, int tile_x, int tile_z) const;
    uint8_t flags(int x, int z) const;
    float height(int x, int z) const;
    // Whether a soldier can go up from height `from` to `to` over `distance`.
    bool climbable(float from, float to, double distance) const;
    // The walkable cell nearest p, within `reach` meters.
    bool nearest_walkable(Vec3 p, double reach, Cell& out) const;

    const World& world_;
    MoveParams move_;
    Settings settings_;
    double max_rise_;  // m per m: the steepest walkable slope
    std::vector<Barrier> barriers_;
    double x0_ = 0, z0_ = 0;  // corner of cell (0, 0)
    int width_ = 0, depth_ = 0;
    int tiles_x_ = 0, tiles_z_ = 0;
    mutable std::vector<std::unique_ptr<Tile>> tiles_;
    mutable size_t built_ = 0;
    mutable uint32_t search_ = 0;
};

} // namespace swm::core
