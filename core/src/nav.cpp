#include "swm/core/nav.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>
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
    max_rise_ = std::tan(move.max_slope_degrees * std::numbers::pi / 180);
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
    const double min_normal_y = std::cos(move_.max_slope_degrees * std::numbers::pi / 180);
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
        double probe = world_.terrain().height_at(c.x, c.z) + headroom;
        for (int layer = 0; layer < max_layers; ++layer) {
            const World::Ground ground = world_.ground_at(c.x, c.z, probe, move_.step_up);
            if (ground.height > probe + move_.step_up + 1e-6 || ground.height < -1e8) {
                break;  // nothing more below (the terrain's under us, or a hole)
            }
            bool open = on_field && (ground.on_floor || ground.normal.y >= min_normal_y) &&
                        !area_.deadly({c.x, ground.height, c.z});
            if (open) {
                // No wall within the (widened) body...
                open = !world_.overlaps_walls({c.x, ground.height, c.z}, radius, move_.height, move_.step_up);
            }
            if (open) {
                // ...and nothing over it, between knees and head (inside a
                // rock or a trunk, under a low branch).
                open = !world_.raycast({c.x, ground.height + move_.height, c.z},
                                       {c.x, ground.height + move_.step_up, c.z});
            }
            tile.height.push_back(float(ground.height));
            tile.flags.push_back(uint8_t((open ? walkable_bit : 0) | (in_barrier ? barrier_bit : 0)));
            tile.cell.push_back(uint16_t(i));
            probe = ground.height - move_.height - move_.step_up;
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
        }
        if (rise <= step + 1e-3) {
            return Node(((tz * tiles_x_ + tx) << node_bits) | n);  // the highest it can reach
        }
        if (rise > step + distance * max_rise_ || distance <= 0) {
            if (rise < move_.height) {
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
        if (rise <= step + distance * slope + 1e-3) {
            return Node(((tz * tiles_x_ + tx) << node_bits) | n);
        }
        return -1;  // a lip too high to step onto: in the way
    }
    return -1;
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

std::vector<Vec3> NavGrid::find_path(Vec3 from, Vec3 to) const {
    const Node start = nearest_walkable(from, 1.5);
    if (start < 0) {
        return {};
    }
    Node goal = nearest_walkable(to, 6);
    const bool goal_walkable = goal >= 0;
    if (++search_ == 0) {  // the stamps wrapped: forget every old search
        for (auto& t : tiles_) {
            if (t) {
                std::fill(t->visit.begin(), t->visit.end(), 0);
            }
        }
        search_ = 1;
    }
    const double cell = settings_.cell;
    const Cell goal_cell = goal_walkable ? cell_of(goal) : cell_at(to);
    const auto estimate = [&](Cell c) {
        // Octile distance, nudged up a hair so ties go toward the goal.
        const double dx = std::abs(c.x - goal_cell.x), dz = std::abs(c.z - goal_cell.z);
        return cell * (std::max(dx, dz) + (std::numbers::sqrt2 - 1) * std::min(dx, dz)) * 1.001;
    };
    const auto slot = [&](Node n) { return size_t(n & ((1 << node_bits) - 1)); };
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
        Tile& t = touch(start);
        t.g[slot(start)] = 0;
        t.parent[slot(start)] = -1;
        open.push({estimate(cell_of(start)), start});
    }
    Node best = start;
    double best_estimate = estimate(cell_of(start));
    bool reached = false;
    size_t expansions = 0;
    while (!open.empty()) {
        const Node current = open.top().second;
        open.pop();
        Tile& t = tile_of(current);
        if (t.closed[slot(current)]) {
            continue;
        }
        t.closed[slot(current)] = 1;
        if (current == goal) {
            reached = true;
            break;
        }
        const Cell here = cell_of(current);
        if (const double e = estimate(here); e < best_estimate) {
            best_estimate = e;
            best = current;
        }
        if (++expansions > settings_.max_expansions) {
            break;
        }
        const float g = t.g[slot(current)];
        const float level = t.height[slot(current)];
        for (int dz = -1; dz <= 1; ++dz) {
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
                Tile& nt = touch(next);
                if (nt.closed[slot(next)]) {
                    continue;
                }
                const double cost = g + step * ((f & barrier_bit) ? settings_.barrier_cost : 1.0);
                if (cost < nt.g[slot(next)]) {
                    nt.g[slot(next)] = float(cost);
                    nt.parent[slot(next)] = current;
                    open.push({cost + estimate(Cell{nx, nz}), next});
                }
            }
        }
    }

    // The surfaces back from the end, then forward from where we stand.
    const Node end = reached ? goal : best;
    std::vector<Vec3> points;
    for (Node n = end; n >= 0; n = tile_of(n).parent[slot(n)]) {
        const Cell c = cell_of(n);
        Vec3 p = center(c.x, c.z);
        p.y = height(n);
        points.push_back(p);
    }
    points.push_back(from);
    std::reverse(points.begin(), points.end());
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
