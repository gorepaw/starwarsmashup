#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "swm/core/conquest.h"
#include "swm/core/movement.h"
#include "swm/core/vec3.h"

namespace swm::core {

class World;

// Where a soldier can walk on a battlefield, for bots: a grid of small cells,
// each with the surfaces stacked in it, top down (the ground; a walkway or
// a bunker's floor under the hill; a bridge over a river), with room for a
// body between them. A surface is walkable if a body (a little wider than a
// character's) fits there: it isn't too steep (with a margin), no wall overlaps it between
// its knees and head, and nothing hangs over it much below head height. From a
// surface, a soldier stepping into the next cell lands on the highest
// surface there it can reach: one at most a step up (or a walkable slope),
// or any lower one, dropping onto it; it must be walkable. Barriers
// (Battlefront's AI barriers) are walkable but cost more, so paths go around
// them when that's not a long way (unless a route starts or ends in one: a
// post inside it). So do surfaces at an edge (a drop within
// a body's reach: the side of a ramp, a ledge's lip), and straight lines
// between corners don't run along them, so a bot that strays a little
// doesn't fall off: routes keep to the middle of a ramp or a walkway. Cells outside the battlefield (its bounds
// and boundaries) aren't walkable, nor are death regions. Surfaces sealed
// inside a big solid (the middle of a boulder, a bunker's roof under its
// hill) look walkable but can't be reached; a search for one ends at the
// nearest place it can reach, after visiting everything else it may.
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
        double edge_cost = 3;            // an edge's cells, likewise
        // Degrees under the steepest slope a soldier walks up that a route
        // may take: bots keep off slopes they'd barely climb.
        double slope_margin = 5;
        size_t max_expansions = 120000;  // cells a search may visit
        // Shut in where the grid sees no way on (somewhere a soldier got to
        // that the grid says it can't), a route starts straight out to the
        // nearest place within this far that leads on.
        double pocket_reach = 8;  // m
        // How far around a cell to look for the model it may be shut in.
        double inside_reach = 40;  // m
    };

    // `world` must outlive the grid. `move` gives the body's size, step and
    // steepest walkable slope.
    NavGrid(const World& world, const Battlefield& field, const MoveParams& move = {});
    NavGrid(const World& world, const Battlefield& field, const MoveParams& move, const Settings& settings);
    ~NavGrid();
    NavGrid(const NavGrid&) = delete;
    NavGrid& operator=(const NavGrid&) = delete;

    // A route for a soldier with feet at `from` to `to` (on the surface
    // nearest its height): the corners to walk to, in order, on the ground
    // (straight lines between them are walkable), ending at `to`, or at the
    // nearest place to it that can be reached. Empty if `from` has nowhere
    // to go.
    std::vector<Vec3> find_path(Vec3 from, Vec3 to) const;
    // Whether a soldier with feet at a can walk straight to b (and end up at
    // its height) without leaving the walkable surfaces or entering a
    // barrier.
    bool clear_line(Vec3 a, Vec3 b) const;
    // Whether the surface under feet at `point` is walkable.
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
    static constexpr int max_layers = 6;  // surfaces kept per cell, from the top
    static constexpr int node_bits = 13;  // a tile's surfaces: up to 32 x 32 x 6
    static constexpr uint8_t walkable_bit = 1, barrier_bit = 2, edge_bit = 4;
    // A surface: its tile's index, then its own within the tile.
    using Node = int32_t;

    bool inside(int x, int z) const { return x >= 0 && z >= 0 && x < width_ && z < depth_; }
    // The steepest slope a route takes.
    double slope_degrees() const { return move_.max_slope_degrees - settings_.slope_margin; }
    Cell cell_at(Vec3 p) const;
    Vec3 center(int x, int z) const;
    Tile& tile(int x, int z) const;  // the tile holding cell (x, z), worked out if it isn't yet
    Tile& tile_of(Node node) const;
    void build(Tile& tile, int tile_x, int tile_z) const;
    float height(Node node) const;
    uint8_t flags(Node node) const;
    Cell cell_of(Node node) const;
    // The surface a soldier at height `from` lands on stepping into cell
    // (x, z) from cell `source`: the highest it can reach (a step up, or up
    // a slope rising toward it, or any way down). -1 if none.
    Node land(int x, int z, float from, Cell source) const;
    // Whether a soldier at height `from` in cell `source`, walking toward
    // the middle of cell (x, z), gets there at `to_height` (remembered).
    bool walks_up(Cell source, double from, int x, int z, double to_height) const;
    // The surface under feet at p: the highest at most a step above them.
    Node under(Vec3 p) const;
    // The walkable surface nearest p (horizontally, then in height), within
    // `reach` meters.
    Node nearest_walkable(Vec3 p, double reach) const;
    // Walkable surfaces within `reach` of feet at p that a soldier there
    // could walk straight onto (at most a step up, nothing in the way at
    // knee or chest height), nearest first; none a search with visit stamp
    // `except` saw (0: any).
    std::vector<Node> ways_from(Vec3 p, double reach, uint32_t except) const;

    const World& world_;
    MoveParams move_;
    Settings settings_;
    double max_rise_;  // m per m: the steepest walkable slope
    std::vector<Barrier> barriers_;
    Battlefield area_;  // the battlefield's bounds, boundaries and death regions (no teams or posts)
    double x0_ = 0, z0_ = 0;  // corner of cell (0, 0)
    int width_ = 0, depth_ = 0;
    int tiles_x_ = 0, tiles_z_ = 0;
    mutable std::vector<std::unique_ptr<Tile>> tiles_;
    mutable std::unordered_map<uint64_t, bool> climbs_;  // walks_up's answers
    mutable size_t built_ = 0;
    mutable uint32_t search_ = 0;
};

} // namespace swm::core
