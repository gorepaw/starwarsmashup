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

// A tile's cells, rows along +Z, and the search scratch space for them.
// `visit` says which search last touched a cell; g, parent and closed are
// only meaningful when it's the current one.
struct NavGrid::Tile {
    static constexpr int count = tile_size * tile_size;
    std::array<float, count> height{};
    std::array<uint8_t, count> flags{};
    std::array<float, count> g{};
    std::array<int32_t, count> parent{};
    std::array<uint32_t, count> visit{};
    std::array<uint8_t, count> closed{};
};

namespace {

int local(int x, int z, int size_bits) {
    const int mask = (1 << size_bits) - 1;
    return ((z & mask) << size_bits) | (x & mask);
}

} // namespace

NavGrid::NavGrid(const World& world, const Battlefield& field, const MoveParams& move)
    : NavGrid(world, field, move, Settings{}) {}

NavGrid::NavGrid(const World& world, const Battlefield& field, const MoveParams& move, const Settings& settings)
    : world_(world), move_(move), settings_(settings), barriers_(field.barriers) {
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
    for (int lz = 0; lz < tile_size; ++lz) {
        for (int lx = 0; lx < tile_size; ++lx) {
            const int x = (tile_x << tile_bits) + lx, z = (tile_z << tile_bits) + lz;
            const int i = (lz << tile_bits) | lx;
            if (!inside(x, z)) {
                continue;
            }
            const Vec3 c = center(x, z);
            // The ground at terrain level (a log or a low rock on it counts).
            const World::Ground ground =
                world_.ground_at(c.x, c.z, world_.terrain().height_at(c.x, c.z), move_.step_up);
            tile.height[size_t(i)] = float(ground.height);
            bool open = ground.on_floor || ground.normal.y >= min_normal_y;
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
            uint8_t flags = open ? walkable_bit : 0;
            for (const Barrier* b : barriers) {
                if (b->contains(c)) {
                    flags |= barrier_bit;
                    break;
                }
            }
            tile.flags[size_t(i)] = flags;
        }
    }
}

uint8_t NavGrid::flags(int x, int z) const {
    return inside(x, z) ? tile(x, z).flags[size_t(local(x, z, tile_bits))] : 0;
}

float NavGrid::height(int x, int z) const {
    return tile(x, z).height[size_t(local(x, z, tile_bits))];
}

bool NavGrid::climbable(float from, float to, double distance) const {
    return double(to) - double(from) <= move_.step_up + distance * max_rise_;
}

bool NavGrid::walkable(Vec3 point) const {
    const Cell c = cell_at(point);
    return (flags(c.x, c.z) & walkable_bit) != 0;
}

bool NavGrid::nearest_walkable(Vec3 p, double reach, Cell& out) const {
    const Cell c = cell_at(p);
    if (flags(c.x, c.z) & walkable_bit) {
        out = c;
        return true;
    }
    const int r = int(std::ceil(reach / settings_.cell));
    double best = reach * reach;
    bool found = false;
    for (int z = c.z - r; z <= c.z + r; ++z) {
        for (int x = c.x - r; x <= c.x + r; ++x) {
            if (!(flags(x, z) & walkable_bit)) {
                continue;
            }
            const Vec3 q = center(x, z);
            const double d2 = (q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z);
            if (d2 <= best) {
                best = d2;
                out = {x, z};
                found = true;
            }
        }
    }
    return found;
}

bool NavGrid::clear_line(Vec3 a, Vec3 b) const {
    Cell previous = cell_at(a);
    uint8_t previous_flags = flags(previous.x, previous.z);
    if (!(previous_flags & walkable_bit)) {
        return false;
    }
    const double length = std::hypot(b.x - a.x, b.z - a.z);
    const int steps = std::max(1, int(std::ceil(length / (settings_.cell * 0.5))));
    for (int i = 1; i <= steps; ++i) {
        const double t = double(i) / steps;
        const Cell c = cell_at({a.x + (b.x - a.x) * t, 0, a.z + (b.z - a.z) * t});
        if (c.x == previous.x && c.z == previous.z) {
            continue;
        }
        const uint8_t f = flags(c.x, c.z);
        if (!(f & walkable_bit) || ((f & barrier_bit) && !(previous_flags & barrier_bit))) {
            return false;
        }
        const bool diagonal = c.x != previous.x && c.z != previous.z;
        if (diagonal && (!(flags(c.x, previous.z) & walkable_bit) || !(flags(previous.x, c.z) & walkable_bit))) {
            return false;
        }
        const double step = settings_.cell * std::hypot(c.x - previous.x, c.z - previous.z);
        if (!climbable(height(previous.x, previous.z), height(c.x, c.z), step)) {
            return false;
        }
        previous = c;
        previous_flags = f;
    }
    return true;
}

std::vector<Vec3> NavGrid::find_path(Vec3 from, Vec3 to) const {
    Cell start, goal;
    if (!nearest_walkable(from, 1.5, start)) {
        return {};
    }
    const bool goal_walkable = nearest_walkable(to, 6, goal);
    if (!goal_walkable) {
        goal = cell_at(to);
    }
    if (++search_ == 0) {  // the stamps wrapped: forget every old search
        for (auto& t : tiles_) {
            if (t) {
                t->visit.fill(0);
            }
        }
        search_ = 1;
    }
    const double cell = settings_.cell;
    const auto id = [&](int x, int z) { return int32_t(z) * int32_t(width_) + int32_t(x); };
    const auto estimate = [&](int x, int z) {
        // Octile distance, nudged up a hair so ties go toward the goal.
        const double dx = std::abs(x - goal.x), dz = std::abs(z - goal.z);
        return cell * (std::max(dx, dz) + (std::numbers::sqrt2 - 1) * std::min(dx, dz)) * 1.001;
    };
    const auto touch = [&](Tile& t, int i) {
        if (t.visit[size_t(i)] != search_) {
            t.visit[size_t(i)] = search_;
            t.g[size_t(i)] = std::numeric_limits<float>::infinity();
            t.closed[size_t(i)] = 0;
        }
    };

    using Entry = std::pair<double, int32_t>;  // f, cell id
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    {
        Tile& t = tile(start.x, start.z);
        const int i = local(start.x, start.z, tile_bits);
        touch(t, i);
        t.g[size_t(i)] = 0;
        t.parent[size_t(i)] = -1;
        open.push({estimate(start.x, start.z), id(start.x, start.z)});
    }
    const int32_t goal_id = id(goal.x, goal.z);
    Cell best = start;
    double best_estimate = estimate(start.x, start.z);
    bool reached = false;
    size_t expansions = 0;
    while (!open.empty()) {
        const int32_t current = open.top().second;
        open.pop();
        const int x = current % width_, z = current / width_;
        Tile& t = tile(x, z);
        const int i = local(x, z, tile_bits);
        if (t.closed[size_t(i)]) {
            continue;
        }
        t.closed[size_t(i)] = 1;
        if (current == goal_id) {
            reached = true;
            break;
        }
        if (const double e = estimate(x, z); e < best_estimate) {
            best_estimate = e;
            best = {x, z};
        }
        if (++expansions > settings_.max_expansions) {
            break;
        }
        const float g = t.g[size_t(i)];
        const float here = t.height[size_t(i)];
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) {
                    continue;
                }
                const int nx = x + dx, nz = z + dz;
                const uint8_t f = flags(nx, nz);
                if (!(f & walkable_bit)) {
                    continue;
                }
                const bool diagonal = dx != 0 && dz != 0;
                // No cutting corners past a blocked cell.
                if (diagonal && (!(flags(nx, z) & walkable_bit) || !(flags(x, nz) & walkable_bit))) {
                    continue;
                }
                const double step = diagonal ? cell * std::numbers::sqrt2 : cell;
                Tile& nt = tile(nx, nz);
                const int ni = local(nx, nz, tile_bits);
                if (!climbable(here, nt.height[size_t(ni)], step)) {
                    continue;
                }
                touch(nt, ni);
                if (nt.closed[size_t(ni)]) {
                    continue;
                }
                const double cost = g + step * ((f & barrier_bit) ? settings_.barrier_cost : 1.0);
                if (cost < nt.g[size_t(ni)]) {
                    nt.g[size_t(ni)] = float(cost);
                    nt.parent[size_t(ni)] = current;
                    open.push({cost + estimate(nx, nz), id(nx, nz)});
                }
            }
        }
    }

    // The cells back from the end, then forward from where we stand.
    const Cell end = reached ? goal : best;
    std::vector<Vec3> points;
    for (int32_t c = id(end.x, end.z); c >= 0;) {
        const int x = c % width_, z = c / width_;
        Vec3 p = center(x, z);
        p.y = height(x, z);
        points.push_back(p);
        c = tile(x, z).parent[size_t(local(x, z, tile_bits))];
    }
    points.push_back(from);
    std::reverse(points.begin(), points.end());
    if (reached && goal_walkable && cell_at(to).x == goal.x && cell_at(to).z == goal.z) {
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
