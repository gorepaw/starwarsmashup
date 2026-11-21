#include "swm/core/nav.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>
#include <unordered_map>
#include <utility>

#include "swm/core/world.h"

namespace swm::core {

// A tile's cells, rows along +Z, and their surfaces: cell i's are
// [first[i], first[i + 1]), top down. Then the search scratch space for the
// surfaces: `visit` says which search last touched one; g, parent and closed
// are only meaningful when it's the current one.
struct NavGrid::Tile {
    static constexpr int count = tile_size * tile_size;
    std::array<uint16_t, count + 1> first{};
    std::vector<float> height;
    std::vector<uint8_t> flags;
    std::vector<uint16_t> cell;  // each surface's cell (its index in the tile)
    std::vector<float> g;
    std::vector<int32_t> parent;
    std::vector<uint32_t> visit;
    std::vector<uint8_t> closed;
};

namespace {

// The part of a battlefield the grid needs: where soldiers may be.
Battlefield area_of(const Battlefield& field) {
    Battlefield area;
    area.min_x = field.min_x;
    area.min_z = field.min_z;
    area.max_x = field.max_x;
    area.max_z = field.max_z;
    area.boundaries = field.boundaries;
    area.death_regions = field.death_regions;
    area.damage_regions = field.damage_regions;
    return area;
}

int local(int x, int z, int size_bits) {
    const int mask = (1 << size_bits) - 1;
    return ((z & mask) << size_bits) | (x & mask);
}

// How far above the terrain the grid looks for surfaces (an Ewok village's
// walkways, a bridge).
constexpr double headroom = 80;
// How far off the end's height a straight line may arrive.
constexpr double feet_slack = 0.3;
// What the grid leaves off a step: cells are coarse, so a lip it takes for
// a step might be a little higher where a soldier crosses it.
constexpr double step_margin = 0.1;

} // namespace

NavGrid::NavGrid(const World& world, const Battlefield& field, const MoveParams& move)
    : NavGrid(world, field, move, Settings{}) {}

NavGrid::NavGrid(const World& world, const Battlefield& field, const MoveParams& move, const Settings& settings)
    : world_(world), move_(move), settings_(settings), barriers_(field.barriers), area_(area_of(field)) {
    max_rise_ = std::tan(slope_degrees() * std::numbers::pi / 180);
    // The battlefield's bounds, within the terrain (and within reason, for
    // a battlefield without bounds on a world without terrain).
    constexpr double far = 4096;
    const Vec3 low = world.clamp({-far, 0, -far}), high = world.clamp({far, 0, far});
    const double min_x = std::max(field.min_x, low.x), max_x = std::min(field.max_x, high.x);
    const double min_z = std::max(field.min_z, low.z), max_z = std::min(field.max_z, high.z);
    x0_ = min_x;
    z0_ = min_z;
    width_ = std::max(0, int(std::ceil((max_x - min_x) / settings_.cell)));
    depth_ = std::max(0, int(std::ceil((max_z - min_z) / settings_.cell)));
    tiles_x_ = (width_ + tile_size - 1) >> tile_bits;
    tiles_z_ = (depth_ + tile_size - 1) >> tile_bits;
    tiles_.resize(size_t(tiles_x_) * size_t(tiles_z_));
}

NavGrid::~NavGrid() = default;

NavGrid::Cell NavGrid::cell_at(Vec3 p) const {
    return {int(std::floor((p.x - x0_) / settings_.cell)), int(std::floor((p.z - z0_) / settings_.cell))};
}

Vec3 NavGrid::center(int x, int z) const {
    return {x0_ + (x + 0.5) * settings_.cell, 0, z0_ + (z + 0.5) * settings_.cell};
}

NavGrid::Tile& NavGrid::tile(int x, int z) const {
    const int tx = x >> tile_bits, tz = z >> tile_bits;
    auto& slot = tiles_[size_t(tz) * size_t(tiles_x_) + size_t(tx)];
    if (!slot) {
        slot = std::make_unique<Tile>();
        build(*slot, tx, tz);
        ++built_;
    }
    return *slot;
}

NavGrid::Tile& NavGrid::tile_of(Node node) const {
    return *tiles_[size_t(node >> node_bits)];  // built: its node was handed out
}

float NavGrid::height(Node node) const {
    return tile_of(node).height[size_t(node & ((1 << node_bits) - 1))];
}

uint8_t NavGrid::flags(Node node) const {
    return node < 0 ? 0 : tile_of(node).flags[size_t(node & ((1 << node_bits) - 1))];
}

NavGrid::Cell NavGrid::cell_of(Node node) const {
    const int index = node >> node_bits;
    const int i = tile_of(node).cell[size_t(node & ((1 << node_bits) - 1))];
    return {((index % tiles_x_) << tile_bits) + (i & (tile_size - 1)), ((index / tiles_x_) << tile_bits) + (i >> tile_bits)};
}

void NavGrid::build(Tile& tile, int tile_x, int tile_z) const {
    const double cell = settings_.cell;
    const double radius = move_.radius + settings_.clearance;
    const double min_normal_y = std::cos(slope_degrees() * std::numbers::pi / 180);
    const double steepest_y = std::cos(move_.max_slope_degrees * std::numbers::pi / 180);  // movement's
    // The barriers reaching into this tile.
    const double low_x = x0_ + tile_x * tile_size * cell, high_x = low_x + tile_size * cell;
    const double low_z = z0_ + tile_z * tile_size * cell, high_z = low_z + tile_size * cell;
    std::vector<const Barrier*> barriers;
    for (const Barrier& b : barriers_) {
        double bx0 = b.corners[0].x, bx1 = bx0, bz0 = b.corners[0].z, bz1 = bz0;
        for (const Vec3& c : b.corners) {
            bx0 = std::min(bx0, c.x);
            bx1 = std::max(bx1, c.x);
            bz0 = std::min(bz0, c.z);
            bz1 = std::max(bz1, c.z);
        }
        if (bx1 >= low_x && bx0 <= high_x && bz1 >= low_z && bz0 <= high_z) {
            barriers.push_back(&b);
        }
    }
    for (int i = 0; i < Tile::count; ++i) {
        tile.first[size_t(i)] = uint16_t(tile.height.size());
        const int x = (tile_x << tile_bits) + (i & (tile_size - 1)), z = (tile_z << tile_bits) + (i >> tile_bits);
        if (!inside(x, z)) {
            continue;
        }
        const Vec3 c = center(x, z);
        const bool on_field = area_.contains(c);
        bool in_barrier = false;
        for (const Barrier* b : barriers) {
            in_barrier = in_barrier || b->contains(c);
        }
        // The surfaces from the top down: each the highest below the last
        // with room for a body over it.
        // From well over the terrain, or over everything built (a map without
        // terrain, its ground out of sight far below: the Death Star).
        double probe = std::max(world_.terrain().height_at(c.x, c.z) + headroom, world_.top() + move_.step_up);
        for (int layer = 0; layer < max_layers; ++layer) {
            const World::Ground ground = world_.ground_at(c.x, c.z, probe, move_.step_up);
            if (ground.height > probe + move_.step_up + 1e-6 || ground.height < -1e8) {
                break;  // nothing more below (the terrain's under us, or a hole)
            }
            bool open = on_field && ground.normal.y >= min_normal_y &&
                        !area_.harmful({c.x, ground.height, c.z});
            if (open) {
                // No wall within the (widened) body...
                open = !world_.overlaps_walls({c.x, ground.height, c.z}, radius, move_.height, move_.step_up);
            }
            if (open) {
                // ...and nothing over it, between knees and head (inside a
                // rock or a trunk, under a low branch).
                open = !world_.raycast({c.x, ground.height + move_.headroom, c.z},
                                       {c.x, ground.height + move_.step_up, c.z});
            }
            // ...nor anywhere a soldier's middle passes going on to the next
            // cells: movement stops one stepping under a floor too high to
            // step onto and too low to duck under (a rail's coping along
            // Naboo's terraces, narrower than a cell).
            for (int k = 0; open && k < 8; ++k) {
                const double angle = k * std::numbers::pi / 4, r = cell * 0.6;
                open = !world_.floor_between(c.x + r * std::cos(angle), c.z + r * std::sin(angle),
                                             ground.height + move_.step_up, ground.height + move_.headroom);
            }
            if (open) {
                // ...and not shut inside a model (the ground under a stepped
                // pyramid): no one gets in, and a way that drops in from its
                // top lands on it instead.
                open = !world_.inside_solid({c.x, ground.height + 1, c.z}, settings_.inside_reach);
            }
            // At an edge: more than a step down within the (widened) body's
            // reach, all around. And no ground there rising steeper than a
            // soldier walks up: movement stops one stepping onto it, as one
            // straying off a route at the foot of a hillside does (by
            // Geonosis's hangar door).
            bool edge = false;
            for (int k = 0; open && k < 8; ++k) {
                const double angle = k * std::numbers::pi / 4;
                const World::Ground beside = world_.ground_at(c.x + radius * std::cos(angle),
                                                              c.z + radius * std::sin(angle), ground.height,
                                                              move_.step_up);
                edge = edge || beside.height < ground.height - move_.step_up;
                open = beside.on_floor || beside.height <= ground.height || beside.normal.y >= steepest_y;
            }
            tile.height.push_back(float(ground.height));
            tile.flags.push_back(uint8_t((open ? walkable_bit : 0) | (in_barrier ? barrier_bit : 0) |
                                         (edge ? edge_bit : 0)));
            tile.cell.push_back(uint16_t(i));
            probe = ground.height - move_.headroom - move_.step_up;
        }
    }
    tile.first[Tile::count] = uint16_t(tile.height.size());
    const size_t nodes = tile.height.size();
    tile.g.resize(nodes);
    tile.parent.resize(nodes);
    tile.visit.assign(nodes, 0);
    tile.closed.resize(nodes);
}

NavGrid::Node NavGrid::land(int x, int z, float from, Cell source) const {
    if (!inside(x, z)) {
        return -1;
    }
    const int tx = x >> tile_bits, tz = z >> tile_bits;
    const Tile& t = tile(x, z);
    const int i = local(x, z, tile_bits);
    const double distance = settings_.cell * std::hypot(x - source.x, z - source.z);
    for (int n = t.first[size_t(i)]; n < t.first[size_t(i) + 1]; ++n) {
        const double h = double(t.height[size_t(n)]);
        const double rise = h - double(from);
        const double step = move_.step_up - step_margin;
        if (rise < -move_.step_up && distance > 0) {
            // A drop: over the edge at the height it's leaving (past no
            // railing or wall), then down.
            const Vec3 c = center(x, z);
            if (world_.overlaps_walls({c.x, double(from), c.z}, move_.radius + settings_.clearance, move_.height,
                                      move_.step_up)) {
                return -1;
            }
            // Through a gap a body falls through: on past the cell's middle,
            // and to either side, nothing to stand on at the height it
            // leaves (a crack between Kamino's walkway and a pod's floor,
            // narrower than a soldier, is no way down).
            const Vec3 s = center(source.x, source.z);
            const double run = std::hypot(c.x - s.x, c.z - s.z);
            const Vec3 on{(c.x - s.x) / run, 0, (c.z - s.z) / run};
            for (const Vec3 off : {on, Vec3{-on.z, 0, on.x}, Vec3{on.z, 0, -on.x}}) {
                const Vec3 p = c + off * move_.radius;
                if (world_.ground_at(p.x, p.z, double(from), move_.step_up).height >= double(from) - move_.step_up) {
                    return -1;
                }
            }
            // Nor through a death region on the way down (the Death Star's
            // thin one over a chasm's floor, a lower floor beside it), nor a
            // damage region (Mustafar's lava): it falls through anything
            // lower too.
            for (double y = h; y < double(from); y += 0.5) {
                if (area_.harmful({c.x, y, c.z})) {
                    return -1;
                }
            }
        }
        if (rise <= step + 1e-3) {
            return Node(((tz * tiles_x_ + tx) << node_bits) | n);  // the highest it can reach
        }
        if (rise > step + distance * max_rise_ || distance <= 0) {
            if (rise < move_.headroom) {
                return -1;  // too high to climb, too low to pass under: in the way
            }
            continue;  // overhead: it passes under this one
        }
        // More than a step: only up a slope, rising toward it (not onto
        // the side of a ramp or the lip of a ledge). Its height a little
        // back toward the source says how steeply.
        constexpr double back = 0.25;
        const Vec3 c = center(x, z), s = center(source.x, source.z);
        const double along = back / distance;
        const World::Ground behind =
            world_.ground_at(c.x + (s.x - c.x) * along, c.z + (s.z - c.z) * along, h, move_.step_up);
        const double slope = std::clamp((h - behind.height) / back, 0.0, max_rise_);
        // And a soldier really walks up it: between the cells' middles a
        // coarse mesh (a Wookiee tree's trunk) can be steeper than either.
        if (rise <= step + distance * slope + 1e-3 && walks_up(source, double(from), x, z, h)) {
            return Node(((tz * tiles_x_ + tx) << node_bits) | n);
        }
        return -1;  // a lip too high to step onto: in the way
    }
    return -1;
}

bool NavGrid::walks_up(Cell source, double from, int x, int z, double to_height) const {
    // Remembered by the cells, the way and the starting height (1/16 m).
    const uint64_t key = (uint64_t(uint16_t(source.x)) << 48) | (uint64_t(uint16_t(source.z)) << 32) |
                         (uint64_t((x - source.x + 1) * 3 + (z - source.z + 1)) << 28) |
                         uint64_t(uint32_t(std::lround((from + 4096) * 16)) & 0x0fffffff);
    if (const auto known = climbs_.find(key); known != climbs_.end()) {
        return known->second;
    }
    Vec3 start = center(source.x, source.z), end = center(x, z);
    start.y = from;
    end.y = to_height;
    MoveState state;
    state.position = start;
    state.grounded = true;
    MoveInput input;
    input.forward = 127;
    bool made_it = false;
    for (int tick = 0; tick < 20 && !made_it; ++tick) {
        const Vec3 d{end.x - state.position.x, 0, end.z - state.position.z};
        input.sequence++;
        input.yaw = float(std::atan2(-d.x, -d.z));
        state = simulate_move(state, input, world_, move_, 1.0 / 30);
        made_it = std::hypot(end.x - state.position.x, end.z - state.position.z) < 0.25 &&
                  std::abs(state.position.y - to_height) < 0.3;
    }
    climbs_.emplace(key, made_it);
    return made_it;
}

NavGrid::Node NavGrid::under(Vec3 p) const {
    // As World::ground_at: at most a step above the feet.
    const Cell c = cell_at(p);
    if (!inside(c.x, c.z)) {
        return -1;
    }
    const Tile& t = tile(c.x, c.z);
    const int i = local(c.x, c.z, tile_bits);
    const int index = (c.z >> tile_bits) * tiles_x_ + (c.x >> tile_bits);
    for (int n = t.first[size_t(i)]; n < t.first[size_t(i) + 1]; ++n) {
        if (double(t.height[size_t(n)]) <= p.y + move_.step_up) {
            return Node((index << node_bits) | n);
        }
    }
    return -1;
}

bool NavGrid::walkable(Vec3 point) const {
    return (flags(under(point)) & walkable_bit) != 0;
}

bool NavGrid::walkable_between(double x, double z, double low, double high) const {
    const Node node = under({x, high, z});
    return (flags(node) & walkable_bit) != 0 && double(height(node)) >= low;
}

NavGrid::Node NavGrid::nearest_walkable(Vec3 p, double reach) const {
    if (const Node here = under(p); flags(here) & walkable_bit) {
        return here;
    }
    const Cell c = cell_at(p);
    const int r = int(std::ceil(reach / settings_.cell));
    // Horizontal distance, and height off by more than a step counting
    // double: a surface overhead or far below is a last resort.
    double best = std::numeric_limits<double>::infinity();
    Node found = -1;
    for (int z = c.z - r; z <= c.z + r; ++z) {
        for (int x = c.x - r; x <= c.x + r; ++x) {
            if (!inside(x, z)) {
                continue;
            }
            const Vec3 q = center(x, z);
            const double d2 = (q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z);
            if (d2 > reach * reach) {
                continue;
            }
            const Tile& t = tile(x, z);
            const int i = local(x, z, tile_bits);
            const int index = (z >> tile_bits) * tiles_x_ + (x >> tile_bits);
            for (int n = t.first[size_t(i)]; n < t.first[size_t(i) + 1]; ++n) {
                if (!(t.flags[size_t(n)] & walkable_bit)) {
                    continue;
                }
                const double off = std::max(0.0, std::abs(double(t.height[size_t(n)]) - p.y) - move_.step_up);
                const double score = d2 + 4 * off * off;
                if (score < best) {
                    best = score;
                    found = Node((index << node_bits) | n);
                }
            }
        }
    }
    return found;
}

bool NavGrid::clear_line(Vec3 a, Vec3 b) const {
    // From the surface a is on: not one far below (a stands on a ledge's
    // lip, past its cell's middle, or is in the air).
    Node previous = under(a);
    if (!(flags(previous) & walkable_bit) || a.y - double(height(previous)) > move_.step_up + feet_slack) {
        return false;
    }
    Cell at = cell_at(a);
    const double length = std::hypot(b.x - a.x, b.z - a.z);
    const int steps = std::max(1, int(std::ceil(length / (settings_.cell * 0.5))));
    for (int i = 1; i <= steps; ++i) {
        const double t = double(i) / steps;
        const Cell c = cell_at({a.x + (b.x - a.x) * t, 0, a.z + (b.z - a.z) * t});
        if (c.x == at.x && c.z == at.z) {
            continue;
        }
        const float from = height(previous);
        const Node next = land(c.x, c.z, from, at);
        const uint8_t f = flags(next);
        if (!(f & walkable_bit) || ((f & barrier_bit) && !(flags(previous) & barrier_bit))) {
            return false;
        }
        // Not along an edge (but onto one, at b).
        if ((f & edge_bit) && i < steps && !(c.x == cell_at(b).x && c.z == cell_at(b).z)) {
            return false;
        }
        const bool diagonal = c.x != at.x && c.z != at.z;
        if (diagonal && (!(flags(land(c.x, at.z, from, at)) & walkable_bit) ||
                         !(flags(land(at.x, c.z, from, at)) & walkable_bit))) {
            return false;
        }
        previous = next;
        at = c;
    }
    // On b's surface, not over or under it.
    return std::abs(double(height(previous)) - b.y) <= move_.step_up + feet_slack;
}

std::vector<NavGrid::Node> NavGrid::ways_from(Vec3 p, double reach, uint32_t except) const {
    const Cell c = cell_at(p);
    const int r = int(std::ceil(reach / settings_.cell));
    std::vector<std::pair<double, Node>> found;
    for (int z = c.z - r; z <= c.z + r; ++z) {
        for (int x = c.x - r; x <= c.x + r; ++x) {
            if (!inside(x, z)) {
                continue;
            }
            const Vec3 q = center(x, z);
            const double d2 = (q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z);
            if (d2 > reach * reach) {
                continue;
            }
            const Tile& t = tile(x, z);
            const int i = local(x, z, tile_bits);
            const int index = (z >> tile_bits) * tiles_x_ + (x >> tile_bits);
            for (int n = t.first[size_t(i)]; n < t.first[size_t(i) + 1]; ++n) {
                const double h = double(t.height[size_t(n)]);
                // Not one overhead (the top of the root it's under).
                if ((t.flags[size_t(n)] & walkable_bit) && (except == 0 || t.visit[size_t(n)] != except) &&
                    h <= p.y + move_.step_up) {
                    found.push_back({d2 + 4 * (h - p.y) * (h - p.y), Node((index << node_bits) | n)});
                }
            }
        }
    }
    std::sort(found.begin(), found.end());
    std::vector<Node> out;
    for (const auto& [score, node] : found) {
        const Cell nc = cell_of(node);
        Vec3 q = center(nc.x, nc.z);
        q.y = height(node);
        // Nothing in the way: a root's flank, a rock (but out through one
        // it's inside of, as movement lets it).
        const auto clear = [&](double up) { return !world_.walk_blocked(p + Vec3{0, up, 0}, q + Vec3{0, up, 0}); };
        if (clear(0.6) && clear(1.2)) {
            out.push_back(node);
        }
    }
    return out;
}

bool NavGrid::leads_back(Vec3 from, Vec3 landing) const {
    const Node up = under(from), down = under(landing);
    if (up < 0 || down < 0 || (flags(up) & pocket_bit)) {
        return true;  // nothing to go by; or in a pocket already: anywhere in it
    }
    if (flags(down) & pocket_bit) {
        return false;
    }
    const Cell c = cell_of(down);
    const uint64_t key = (uint64_t(uint32_t(c.x >> 2)) << 40) | (uint64_t(uint32_t(c.z >> 2)) << 20) |
                         uint64_t(uint32_t(int(std::floor(height(down))) + 100000) & 0xfffff);
    if (leads_back_.count(key) != 0) {
        return true;
    }
    bool shut_in = false;
    route(landing, from, &shut_in);
    if (!shut_in) {
        leads_back_.emplace(key, true);
    }
    return !shut_in;
}

std::vector<Vec3> NavGrid::find_path(Vec3 from, Vec3 to) const {
    return route(from, to, nullptr);
}

std::vector<Vec3> NavGrid::route(Vec3 from, Vec3 to, bool* shut_in) const {
    // Where it stands on the grid: the surface under it, or, off the grid
    // (pushed under a root, say), the nearest it can walk straight onto.
    Node start = under(from);
    if (!(flags(start) & walkable_bit)) {
        const std::vector<Node> ways = ways_from(from, 1.5, 0);
        start = !ways.empty() ? ways.front() : nearest_walkable(from, 1.5);
    }
    if (start < 0) {
        const std::vector<Node> ways = ways_from(from, settings_.pocket_reach, 0);
        start = !ways.empty() ? ways.front() : -1;
    }
    if (start < 0) {
        return {};
    }
    const Node goal = nearest_walkable(to, 6);
    const bool goal_walkable = goal >= 0;
    const double cell = settings_.cell;
    const Cell goal_cell = goal_walkable ? cell_of(goal) : cell_at(to);
    // Into or out of a barrier (from or to a post inside one: Hoth's CP3),
    // its cells are the way, not a way to keep off: a search costing them
    // more spreads wide first, and runs out before it gets in or out.
    const double barrier_cost =
        (flags(start) & barrier_bit) || (flags(goal) & barrier_bit) ? 1.0 : settings_.barrier_cost;
    const auto slot = [&](Node n) { return size_t(n & ((1 << node_bits) - 1)); };
    // Where a search is headed.
    struct Target {
        Node node;
        Cell cell;
        double height;
    };
    const Target to_goal{goal, goal_cell, goal_walkable ? double(height(goal)) : to.y};
    const auto estimate = [&](const Target& target, Cell c, double h) {
        // Octile distance, nudged up a hair so ties go toward the target; at
        // least the climb or drop to it (a search that can't get there ends
        // beside it, not on the hill over it).
        const double dx = std::abs(c.x - target.cell.x), dz = std::abs(c.z - target.cell.z);
        const double across = cell * (std::max(dx, dz) + (std::numbers::sqrt2 - 1) * std::min(dx, dz));
        return std::max(across, std::abs(h - target.height)) * 1.3;
    };

    // A* from `origin` to the target: it if reached, else the surface nearest
    // it. Backward, it goes against the way soldiers walk (from a surface to
    // those that step onto it), and each surface's parent is the next one on
    // toward the origin.
    struct Search {
        Node end = -1;
        bool reached = false;
        bool exhausted = false;  // it saw everything it could reach
        double left = 0;         // estimate from the end to the goal
        uint32_t stamp = 0;      // the visit stamp of what it saw
        size_t expansions = 0;   // surfaces it went on from
        Node origin = -1;
    };
    const auto search = [&](Node origin, const Target& target, bool backward, size_t limit) {
        if (++search_ == 0) {  // the stamps wrapped: forget every old search
            for (auto& t : tiles_) {
                if (t) {
                    std::fill(t->visit.begin(), t->visit.end(), 0);
                }
            }
            search_ = 1;
        }
        const auto touch = [&](Node n) -> Tile& {
            Tile& t = tile_of(n);
            if (t.visit[slot(n)] != search_) {
                t.visit[slot(n)] = search_;
                t.g[slot(n)] = std::numeric_limits<float>::infinity();
                t.closed[slot(n)] = 0;
            }
            return t;
        };
        using Entry = std::pair<double, Node>;  // f, surface
        std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
        {
            Tile& t = touch(origin);
            t.g[slot(origin)] = 0;
            t.parent[slot(origin)] = -1;
            open.push({estimate(target, cell_of(origin), height(origin)), origin});
        }
        Search result{origin, false, false, estimate(target, cell_of(origin), height(origin)), search_};
        result.origin = origin;
        size_t& expansions = result.expansions;
        while (!open.empty()) {
            const Node current = open.top().second;
            open.pop();
            Tile& t = tile_of(current);
            if (t.closed[slot(current)]) {
                continue;
            }
            t.closed[slot(current)] = 1;
            if (current == target.node) {
                result.end = current;
                result.reached = true;
                result.left = 0;
                return result;
            }
            const Cell here = cell_of(current);
            if (const double e = estimate(target, here, t.height[slot(current)]); e < result.left) {
                result.left = e;
                result.end = current;
            }
            if (++expansions > limit) {
                return result;
            }
            const float g = t.g[slot(current)];
            const float level = t.height[slot(current)];
            const auto reach = [&](Node next, Node parent, Cell at, double step, uint8_t entered) {
                Tile& nt = touch(next);
                if (nt.closed[slot(next)]) {
                    return;
                }
                const double cost = g + step * ((entered & barrier_bit) ? barrier_cost : 1.0) *
                                            ((entered & edge_bit) ? settings_.edge_cost : 1.0);
                if (cost < nt.g[slot(next)]) {
                    nt.g[slot(next)] = float(cost);
                    nt.parent[slot(next)] = parent;
                    open.push({cost + estimate(target, at, nt.height[slot(next)]), next});
                }
            };
            for (int dz = -1; dz <= 1 && backward; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    // The surfaces beside it that step onto it.
                    const int px = here.x + dx, pz = here.z + dz;
                    if ((dx == 0 && dz == 0) || !inside(px, pz)) {
                        continue;
                    }
                    const bool diagonal = dx != 0 && dz != 0;
                    const double step = diagonal ? cell * std::numbers::sqrt2 : cell;
                    const Tile& pt = tile(px, pz);
                    const int i = local(px, pz, tile_bits);
                    const int index = (pz >> tile_bits) * tiles_x_ + (px >> tile_bits);
                    for (int n = pt.first[size_t(i)]; n < pt.first[size_t(i) + 1]; ++n) {
                        if (!(pt.flags[size_t(n)] & walkable_bit)) {
                            continue;
                        }
                        const Node previous = Node((index << node_bits) | n);
                        const float there = pt.height[size_t(n)];
                        const Cell source{px, pz};
                        if (land(here.x, here.z, there, source) != current) {
                            continue;
                        }
                        if (diagonal && (!(flags(land(here.x, pz, there, source)) & walkable_bit) ||
                                         !(flags(land(px, here.z, there, source)) & walkable_bit))) {
                            continue;
                        }
                        reach(previous, current, source, step, flags(current));
                    }
                }
            }
            for (int dz = -1; dz <= 1 && !backward; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0) {
                        continue;
                    }
                    const int nx = here.x + dx, nz = here.z + dz;
                    const bool diagonal = dx != 0 && dz != 0;
                    const double step = diagonal ? cell * std::numbers::sqrt2 : cell;
                    const Node next = land(nx, nz, level, here);
                    const uint8_t f = flags(next);
                    if (!(f & walkable_bit)) {
                        continue;
                    }
                    // No cutting corners past a blocked cell.
                    if (diagonal && (!(flags(land(nx, here.z, level, here)) & walkable_bit) ||
                                     !(flags(land(here.x, nz, level, here)) & walkable_bit))) {
                        continue;
                    }
                    reach(next, current, Cell{nx, nz}, step, f);
                }
            }
        }
        result.exhausted = true;
        return result;
    };

    Search found = search(start, to_goal, false, settings_.max_expansions);
    if (shut_in != nullptr) {
        // All it saw, closed under walking on, without the goal: a pocket
        // (any way out of it leads into it).
        *shut_in = !found.reached && found.exhausted;
        for (size_t index = 0; index < tiles_.size() && *shut_in; ++index) {
            Tile* t = tiles_[index].get();
            for (size_t n = 0; t != nullptr && n < t->visit.size(); ++n) {
                if (t->visit[n] == found.stamp && t->closed[n]) {
                    t->flags[n] |= pocket_bit;
                }
            }
        }
        return {};
    }
    // Shut in a small pocket the grid sees no way out of, far from the goal
    // (a soldier can get where the grid says it can't: under a root it would
    // brush with its head, say): out to the nearest places nearby that
    // aren't in the pocket, the first that leads on, straight there first.
    constexpr size_t pocket_surfaces = 4000;  // 1000 m2 of half-meter cells
    if (!found.reached && found.exhausted && found.expansions < pocket_surfaces &&
        found.left > settings_.pocket_reach) {
        const std::vector<Node> outside = ways_from(from, settings_.pocket_reach, found.stamp);
        int tried = 0;
        std::vector<Cell> seen;
        for (const Node node : outside) {
            const Cell oc = cell_of(node);
            // A few, apart (not the same way out twice).
            if (std::any_of(seen.begin(), seen.end(),
                            [&](Cell s) { return std::abs(s.x - oc.x) + std::abs(s.z - oc.z) < 6; })) {
                continue;
            }
            seen.push_back(oc);
            const Search way = search(node, to_goal, false, settings_.max_expansions);
            if (way.reached || way.left < found.left) {
                found = way;
            }
            if (found.reached || ++tried >= 4) {
                break;
            }
        }
    }
    // The surfaces back from the end (read before another search writes
    // over their parents).
    const auto chain = [&](Node end) {
        std::vector<Node> nodes;
        for (Node n = end; n >= 0; n = tile_of(n).parent[slot(n)]) {
            nodes.push_back(n);
        }
        return nodes;
    };
    std::vector<Node> back_from_end = chain(found.end);
    // Not to the bottom of a pit with no way out (the ring round the Death
    // Star's reactor's column, a drop down from its catwalks, inside CP4):
    // dropping on the way, if all a search from where it ends sees is a
    // small pocket without the way back, it ends at the surface nearest the
    // goal the first search saw outside it (their parents untouched: the
    // pocket leads nowhere else).
    bool pocketed = false;
    const bool drops = std::adjacent_find(back_from_end.begin(), back_from_end.end(), [&](Node child, Node parent) {
                           return double(height(parent)) - double(height(child)) > move_.step_up;
                       }) != back_from_end.end();
    if (drops && found.end != found.origin) {
        const Node origin = found.origin;
        const Search back =
            search(found.end, Target{origin, cell_of(origin), double(height(origin))}, false, pocket_surfaces);
        if (!back.reached && back.exhausted) {
            Node best = -1;
            double nearest = std::numeric_limits<double>::infinity();
            for (size_t index = 0; index < tiles_.size(); ++index) {
                const Tile* t = tiles_[index].get();
                for (size_t n = 0; t != nullptr && n < t->visit.size(); ++n) {
                    if (t->visit[n] == found.stamp && t->closed[n]) {
                        const Node node = Node((int(index) << node_bits) | int(n));
                        const double e = estimate(to_goal, cell_of(node), double(t->height[n]));
                        if (e < nearest) {
                            nearest = e;
                            best = node;
                        }
                    }
                }
            }
            if (best >= 0) {
                found.end = best;
                found.left = nearest;
                found.reached = false;
                pocketed = true;
                back_from_end = chain(best);
            }
        }
    }

    // Those surfaces, then forward from where we stand.
    const auto point = [&](Node n) {
        const Cell c = cell_of(n);
        Vec3 p = center(c.x, c.z);
        p.y = height(n);
        return p;
    };
    bool reached = found.reached;
    std::vector<Vec3> points;
    for (const Node n : back_from_end) {
        points.push_back(point(n));
    }
    points.push_back(from);
    std::reverse(points.begin(), points.end());
    // Out of time on the way (not shut in): back from the goal to where it
    // set out. Searching toward a goal under a hill (Hoth's hangar) covers
    // the hill first, all of it near the goal as the crow flies; back from
    // the goal, the search keeps to the tunnels it's in, out to the open.
    if (!found.reached && !found.exhausted && goal_walkable && !pocketed) {
        const Node origin = found.origin;
        const Target back_to{origin, cell_of(origin), double(height(origin))};
        if (search(goal, back_to, true, settings_.max_expansions).reached) {
            reached = true;
            points.assign(1, from);
            for (Node n = origin; n >= 0; n = tile_of(n).parent[slot(n)]) {
                points.push_back(point(n));
            }
        }
    }
    if (reached && goal_walkable && cell_at(to).x == goal_cell.x && cell_at(to).z == goal_cell.z) {
        points.back().x = to.x;  // the exact spot, within its cell
        points.back().z = to.z;
    }

    // Corners only: from each one, on to the farthest point in a straight
    // walkable line (always at least the next).
    std::vector<Vec3> path;
    size_t anchor = 0;
    while (anchor + 1 < points.size()) {
        size_t next = anchor + 1;
        for (size_t j = anchor + 2; j < points.size() && clear_line(points[anchor], points[j]); ++j) {
            next = j;
        }
        path.push_back(points[next]);
        anchor = next;
    }
    // Then any corner whose neighbours see each other goes.
    for (size_t i = 0; i + 1 < path.size();) {
        if (clear_line(i == 0 ? from : path[i - 1], path[i + 1])) {
            path.erase(path.begin() + std::ptrdiff_t(i));
        } else {
            ++i;
        }
    }
    return path;
}

} // namespace swm::core
