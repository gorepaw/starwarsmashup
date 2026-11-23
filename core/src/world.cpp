#include "swm/core/world.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "swm/core/net/bytes.h"

namespace swm::core {

namespace {

constexpr uint32_t collision_magic = 0x4C434D53;  // "SMCL"
constexpr uint32_t collision_version = 1;
constexpr uint32_t map_collision_magic = 0x434D4D53;  // "SMMC"
constexpr uint32_t map_collision_version = 2;  // 1: without parts
// Map triangles at most this steep (55 degrees, as MoveParams') are ground.
// (Felucia's ramp up to its cliff top has a stretch of 50.2.)
const double walkable_normal_y = std::cos(55 * std::numbers::pi / 180);

// Building space is small, so floats are plenty on disk. Everyone loads the
// same file, so everyone gets the same doubles.
void put_point(net::ByteWriter& out, Vec3 v) {
    out.put(float(v.x));
    out.put(float(v.y));
    out.put(float(v.z));
}

Vec3 get_point(net::ByteReader& in) {
    const float x = in.get<float>();
    const float y = in.get<float>();
    const float z = in.get<float>();
    return {x, y, z};
}

Vec3 transform(const Placement& p, Vec3 v) {
    const double c = std::cos(p.yaw);
    const double s = std::sin(p.yaw);
    return {p.position.x + c * v.x + s * v.z, p.position.y + v.y, p.position.z - s * v.x + c * v.z};
}

// 2D cross product of (b - a) and (p - a) in the XZ plane.
double cross_xz(Vec3 a, Vec3 b, Vec3 p) {
    return (b.x - a.x) * (p.z - a.z) - (b.z - a.z) * (p.x - a.x);
}

Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Where segment a..b crosses triangle (p0, p1, p2), 0..1 along it, from
// either side (Moller-Trumbore).
std::optional<double> segment_triangle(Vec3 a, Vec3 d, Vec3 p0, Vec3 p1, Vec3 p2) {
    const Vec3 e1 = p1 - p0, e2 = p2 - p0;
    const Vec3 p = cross(d, e2);
    const double det = dot(e1, p);
    if (std::abs(det) < 1e-12) {
        return std::nullopt;
    }
    const Vec3 s = a - p0;
    const double u = dot(s, p) / det;
    if (u < 0 || u > 1) {
        return std::nullopt;
    }
    const Vec3 q = cross(s, e1);
    const double v = dot(d, q) / det;
    if (v < 0 || u + v > 1) {
        return std::nullopt;
    }
    const double t = dot(e2, q) / det;
    if (t < 0 || t > 1) {
        return std::nullopt;
    }
    return t;
}

// The part of a polygon on one side of a height: keeps y >= height (above)
// or y <= height. Returns the new vertex count; `out` holds up to 8.
int clip_height(const Vec3* in, int count, double height, bool above, Vec3* out) {
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const Vec3 p = in[i], q = in[(i + 1) % count];
        const bool p_in = above ? p.y >= height : p.y <= height;
        const bool q_in = above ? q.y >= height : q.y <= height;
        if (p_in) {
            out[n++] = p;
        }
        if (p_in != q_in) {
            const double t = (height - p.y) / (q.y - p.y);
            out[n++] = p + (q - p) * t;
        }
    }
    return n;
}

// Closest point to (x, z) on the segment p..q, in XZ.
Vec3 closest_on_segment_xz(Vec3 p, Vec3 q, double x, double z) {
    const double ex = q.x - p.x, ez = q.z - p.z;
    const double length2 = ex * ex + ez * ez;
    const double t = length2 < 1e-12 ? 0 : std::clamp(((x - p.x) * ex + (z - p.z) * ez) / length2, 0.0, 1.0);
    return {p.x + ex * t, 0, p.z + ez * t};
}

// Where a body's center stands against a convex polygon seen from above:
// over it, or else how far from it and its nearest point.
struct Contact {
    bool over = false;
    double distance = 0;
    Vec3 nearest;
};

Contact contact_xz(const Vec3* polygon, int n, Vec3 center) {
    // Over the polygon: every edge turns the same way around the center.
    Contact out;
    double turn = 0;
    bool over = n >= 3;
    for (int i = 0; i < n && over; ++i) {
        const double c = cross_xz(polygon[i], polygon[(i + 1) % n], center);
        if (std::abs(c) < 1e-12) {
            continue;
        }
        over = turn == 0 || (c > 0) == (turn > 0);
        turn = c;
    }
    out.over = over && turn != 0;
    if (out.over) {
        return out;
    }
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; ++i) {
        const Vec3 q = closest_on_segment_xz(polygon[i], polygon[(i + 1) % n], center.x, center.z);
        const double d2 = (q.x - center.x) * (q.x - center.x) + (q.z - center.z) * (q.z - center.z);
        if (d2 < best) {
            best = d2;
            out.nearest = q;
        }
    }
    out.distance = std::sqrt(best);
    return out;
}

// The part of a triangle between two heights (a body's knees and head).
// Returns the vertex count; `slab` holds up to 8.
int slab_of(const Vec3 (&triangle)[3], double lo, double hi, Vec3* slab) {
    Vec3 above[8];
    return clip_height(above, clip_height(triangle, 3, lo, true, above), hi, false, slab);
}

} // namespace

void save_building_collision(const BuildingCollision& collision, const std::filesystem::path& path) {
    net::ByteWriter out;
    out.put(collision_magic);
    out.put(collision_version);
    out.put(uint32_t(collision.floors.size()));
    for (const auto& floor : collision.floors) {
        put_point(out, floor.a);
        put_point(out, floor.b);
        put_point(out, floor.c);
        out.put(int32_t(floor.cell));
    }
    out.put(uint32_t(collision.walls.size()));
    for (const auto& wall : collision.walls) {
        put_point(out, wall.a);
        put_point(out, wall.b);
        out.put(float(wall.height));
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(out.data().data()), std::streamsize(out.data().size()));
    if (!file) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

BuildingCollision load_building_collision(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + path.string());
    }
    const std::vector<uint8_t> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    BuildingCollision collision;
    try {
        net::ByteReader in(data);
        if (in.get<uint32_t>() != collision_magic || in.get<uint32_t>() != collision_version) {
            throw std::runtime_error(path.string() + " is not a version 1 building collision file");
        }
        const uint32_t floors = in.get<uint32_t>();
        for (uint32_t i = 0; i < floors; ++i) {
            BuildingCollision::Floor floor;
            floor.a = get_point(in);
            floor.b = get_point(in);
            floor.c = get_point(in);
            floor.cell = in.get<int32_t>();
            collision.floors.push_back(floor);
        }
        const uint32_t walls = in.get<uint32_t>();
        for (uint32_t i = 0; i < walls; ++i) {
            BuildingCollision::Wall wall;
            wall.a = get_point(in);
            wall.b = get_point(in);
            wall.height = in.get<float>();
            collision.walls.push_back(wall);
        }
        if (!in.at_end()) {
            throw std::runtime_error(path.string() + " has trailing bytes");
        }
    } catch (const net::PacketError&) {
        throw std::runtime_error(path.string() + " is truncated");
    }
    return collision;
}

void save_map_collision(const MapCollision& collision, const std::filesystem::path& path) {
    net::ByteWriter out;
    out.put(map_collision_magic);
    out.put(map_collision_version);
    out.put(uint32_t(collision.triangles.size()));
    for (const auto& triangle : collision.triangles) {
        put_point(out, triangle.a);
        put_point(out, triangle.b);
        put_point(out, triangle.c);
        out.put(triangle.blocks);
        out.put(triangle.part);
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(out.data().data()), std::streamsize(out.data().size()));
    if (!file) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

MapCollision load_map_collision(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + path.string());
    }
    const std::vector<uint8_t> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    MapCollision collision;
    try {
        net::ByteReader in(data);
        if (in.get<uint32_t>() != map_collision_magic) {
            throw std::runtime_error(path.string() + " is not a map collision file");
        }
        const uint32_t version = in.get<uint32_t>();
        if (version < 1 || version > map_collision_version) {
            throw std::runtime_error(path.string() + " is a map collision file of an unknown version");
        }
        const uint32_t count = in.get<uint32_t>();
        collision.triangles.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            MapCollision::Triangle triangle;
            triangle.a = get_point(in);
            triangle.b = get_point(in);
            triangle.c = get_point(in);
            triangle.blocks = in.get<uint8_t>();
            if (version >= 2) {
                triangle.part = in.get<int16_t>();
            }
            collision.triangles.push_back(triangle);
        }
        if (!in.at_end()) {
            throw std::runtime_error(path.string() + " has trailing bytes");
        }
    } catch (const net::PacketError&) {
        throw std::runtime_error(path.string() + " is truncated");
    }
    return collision;
}

std::filesystem::path map_collision_path(const std::filesystem::path& bake) {
    return std::filesystem::path(bake.string() + ".collision");
}

Placement placement_from_galaxies(double x, double y, double z, double yaw_degrees) {
    // Mirroring X turns a left-handed yaw into its negative.
    return {{-x, y, z}, -yaw_degrees * std::numbers::pi / 180};
}

void World::add_building(const BuildingCollision& collision, const Placement& placement) {
    constexpr double inf = std::numeric_limits<double>::infinity();
    Building building{{}, {}, inf, -inf, inf, -inf};
    const auto grow = [&](Vec3 v) {
        building.min_x = std::min(building.min_x, v.x);
        building.max_x = std::max(building.max_x, v.x);
        building.min_z = std::min(building.min_z, v.z);
        building.max_z = std::max(building.max_z, v.z);
        top_ = std::max(top_, v.y);
    };
    for (const auto& source : collision.floors) {
        Floor floor{transform(placement, source.a), transform(placement, source.b), transform(placement, source.c),
                    0, 0, 0, 0};
        if (std::abs(cross_xz(floor.a, floor.b, floor.c)) < 1e-9) {
            continue;  // vertical or degenerate: nothing to stand on
        }
        floor.min_x = std::min({floor.a.x, floor.b.x, floor.c.x});
        floor.max_x = std::max({floor.a.x, floor.b.x, floor.c.x});
        floor.min_z = std::min({floor.a.z, floor.b.z, floor.c.z});
        floor.max_z = std::max({floor.a.z, floor.b.z, floor.c.z});
        grow(floor.a);
        grow(floor.b);
        grow(floor.c);
        building.floors.push_back(floor);
    }
    for (const auto& source : collision.walls) {
        Wall wall{transform(placement, source.a), transform(placement, source.b), 0, 0};
        wall.bottom = std::min(wall.a.y, wall.b.y);
        wall.top = std::max(wall.a.y, wall.b.y) + source.height;
        top_ = std::max(top_, wall.top);
        grow(wall.a);
        grow(wall.b);
        building.walls.push_back(wall);
    }
    buildings_.push_back(std::move(building));
}

void World::add_map_collision(const MapCollision& collision) {
    for (const auto& t : collision.triangles) {
        const Vec3 n = cross(t.b - t.a, t.c - t.a);
        const double length = n.length();
        if (length < 1e-9 || t.blocks == 0) {
            continue;  // degenerate, or stops nothing
        }
        Solid s{t.a, t.b, t.c, n * (1 / length), 0, 0, 0, 0, 0, 0, t.blocks, false, false, t.part};
        s.min_x = std::min({t.a.x, t.b.x, t.c.x});
        s.max_x = std::max({t.a.x, t.b.x, t.c.x});
        s.min_y = std::min({t.a.y, t.b.y, t.c.y});
        s.max_y = std::max({t.a.y, t.b.y, t.c.y});
        s.min_z = std::min({t.a.z, t.b.z, t.c.z});
        s.max_z = std::max({t.a.z, t.b.z, t.c.z});
        top_ = std::max(top_, s.max_y);
        // Props' triangles face outward (Battlefront's do): a gentle one
        // facing down is an underside, not ground to be lifted onto.
        s.floor = s.normal.y >= walkable_normal_y;
        s.ceiling = s.normal.y <= -walkable_normal_y;
        solids_.push_back(s);
    }
    // Rebuild the grid over every solid.
    grid_.clear();
    if (solids_.empty()) {
        grid_w_ = grid_h_ = 0;
        return;
    }
    double min_x = solids_[0].min_x, max_x = solids_[0].max_x;
    double min_z = solids_[0].min_z, max_z = solids_[0].max_z;
    for (const Solid& s : solids_) {
        min_x = std::min(min_x, s.min_x);
        max_x = std::max(max_x, s.max_x);
        min_z = std::min(min_z, s.min_z);
        max_z = std::max(max_z, s.max_z);
    }
    grid_x0_ = min_x;
    grid_z0_ = min_z;
    grid_w_ = int((max_x - min_x) / grid_cell_) + 1;
    grid_h_ = int((max_z - min_z) / grid_cell_) + 1;
    grid_.assign(size_t(grid_w_) * size_t(grid_h_), {});
    for (uint32_t i = 0; i < solids_.size(); ++i) {
        const Solid& s = solids_[i];
        const int x0 = int((s.min_x - grid_x0_) / grid_cell_), x1 = int((s.max_x - grid_x0_) / grid_cell_);
        const int z0 = int((s.min_z - grid_z0_) / grid_cell_), z1 = int((s.max_z - grid_z0_) / grid_cell_);
        for (int gz = z0; gz <= z1; ++gz) {
            for (int gx = x0; gx <= x1; ++gx) {
                grid_[size_t(gz) * size_t(grid_w_) + size_t(gx)].push_back(i);
            }
        }
    }
}

void World::set_destroyed(size_t index, bool destroyed) const {
    if (index >= destroyed_.size()) {
        if (!destroyed) {
            return;
        }
        destroyed_.resize(index + 1, 0);
    }
    destroyed_[index] = destroyed ? 1 : 0;
}

size_t World::add_map_collision_of(const std::filesystem::path& bake) {
    const std::filesystem::path path = map_collision_path(bake);
    if (!std::filesystem::exists(path)) {
        return 0;
    }
    const MapCollision collision = load_map_collision(path);
    add_map_collision(collision);
    return collision.triangles.size();
}

template <class Visit>
void World::for_solids(double min_x, double max_x, double min_z, double max_z, Visit&& visit) const {
    if (grid_.empty()) {
        return;
    }
    const int x0 = std::max(0, int(std::floor((min_x - grid_x0_) / grid_cell_)));
    const int x1 = std::min(grid_w_ - 1, int(std::floor((max_x - grid_x0_) / grid_cell_)));
    const int z0 = std::max(0, int(std::floor((min_z - grid_z0_) / grid_cell_)));
    const int z1 = std::min(grid_h_ - 1, int(std::floor((max_z - grid_z0_) / grid_cell_)));
    for (int gz = z0; gz <= z1; ++gz) {
        for (int gx = x0; gx <= x1; ++gx) {
            for (const uint32_t i : grid_[size_t(gz) * size_t(grid_w_) + size_t(gx)]) {
                if (present(solids_[i])) {
                    visit(solids_[i]);
                }
            }
        }
    }
}

template <class Visit>
void World::for_solids_along(Vec3 a, Vec3 b, Visit&& visit) const {
    if (grid_.empty()) {
        return;
    }
    // Walk the cells the segment's XZ shadow crosses (Amanatides-Woo).
    const double fx = (a.x - grid_x0_) / grid_cell_, fz = (a.z - grid_z0_) / grid_cell_;
    const double dx = (b.x - a.x) / grid_cell_, dz = (b.z - a.z) / grid_cell_;
    int gx = int(std::floor(fx)), gz = int(std::floor(fz));
    const int end_x = int(std::floor(fx + dx)), end_z = int(std::floor(fz + dz));
    const int step_x = dx > 0 ? 1 : -1, step_z = dz > 0 ? 1 : -1;
    constexpr double never = std::numeric_limits<double>::infinity();
    const double delta_x = dx != 0 ? std::abs(1 / dx) : never, delta_z = dz != 0 ? std::abs(1 / dz) : never;
    double next_x = dx != 0 ? ((dx > 0 ? std::floor(fx) + 1 - fx : fx - std::floor(fx)) * delta_x) : never;
    double next_z = dz != 0 ? ((dz > 0 ? std::floor(fz) + 1 - fz : fz - std::floor(fz)) * delta_z) : never;
    const int steps = std::abs(end_x - gx) + std::abs(end_z - gz);
    for (int i = 0; i <= steps; ++i) {
        if (gx >= 0 && gx < grid_w_ && gz >= 0 && gz < grid_h_) {
            for (const uint32_t index : grid_[size_t(gz) * size_t(grid_w_) + size_t(gx)]) {
                if (present(solids_[index])) {
                    visit(solids_[index]);
                }
            }
        }
        if (next_x < next_z) {
            gx += step_x;
            next_x += delta_x;
        } else {
            gz += step_z;
            next_z += delta_z;
        }
    }
}

World::Ground World::ground_at(double x, double z, double y, double step_up) const {
    const double reach = y + step_up;
    Ground best;
    bool found = false;
    const double terrain = terrain_.height_at(x, z);
    const bool cut = terrain_.cut_at(x, z);
    if (terrain <= reach && !cut) {
        best = {terrain, terrain_.normal_at(x, z), false};
        found = true;
    }
    const Vec3 p{x, 0, z};
    for (const Building& building : buildings_) {
        if (x < building.min_x || x > building.max_x || z < building.min_z || z > building.max_z) {
            continue;
        }
        for (const Floor& f : building.floors) {
            if (x < f.min_x || x > f.max_x || z < f.min_z || z > f.max_z) {
                continue;
            }
            // Barycentric weights in XZ; inside when all share the triangle's sign.
            const double area = cross_xz(f.a, f.b, f.c);
            const double wa = cross_xz(f.b, f.c, p) / area;
            const double wb = cross_xz(f.c, f.a, p) / area;
            const double wc = 1 - wa - wb;
            constexpr double edge = -1e-9;  // shared edges count for both triangles
            if (wa < edge || wb < edge || wc < edge) {
                continue;
            }
            const double height = wa * f.a.y + wb * f.b.y + wc * f.c.y;
            if (height <= reach && (!found || height > best.height)) {
                best = {height, {0, 1, 0}, true};
                found = true;
            }
        }
    }
    // Gentle map triangles under the point.
    for_solids(x, x, z, z, [&](const Solid& s) {
        if (!s.floor || !(s.blocks & blocks_characters) || x < s.min_x || x > s.max_x || z < s.min_z ||
            z > s.max_z || s.min_y > reach) {
            return;
        }
        const double area = cross_xz(s.a, s.b, s.c);
        if (std::abs(area) < 1e-12) {
            return;
        }
        const double wa = cross_xz(s.b, s.c, p) / area;
        const double wb = cross_xz(s.c, s.a, p) / area;
        const double wc = 1 - wa - wb;
        constexpr double edge = -1e-9;
        if (wa < edge || wb < edge || wc < edge) {
            return;
        }
        const double height = wa * s.a.y + wb * s.b.y + wc * s.c.y;
        if (height <= reach && (!found || height > best.height)) {
            best = {height, s.normal, true};
            found = true;
        }
    });
    if (!found) {
        // A little under the terrain: back up onto it. Deep under it (in a
        // cave or a bunker, over a seam in its floor) or through a hole in
        // it with nothing below: falling.
        constexpr double resurface = 2;
        best = cut || terrain - y > resurface ? Ground{-1e9, {0, 1, 0}, false}
                                              : Ground{terrain, terrain_.normal_at(x, z), false};
    }
    return best;
}

World::Ground World::support_at(double x, double z, double y, double step_up, double seam) const {
    const Ground middle = ground_at(x, z, y, step_up);
    if (middle.height >= y - step_up) {
        return middle;  // standing (or nearly): no seam to look over
    }
    // Over a hole or a drop: the floor both sides of it, across any of four
    // ways, a step or more over what's under the middle. The higher side
    // may be more than a step up (a seam where a ramp meets a walkway, on
    // Utapau, the walkway a little over the ramp's top), the lower not.
    Ground best = middle;
    for (int k = 0; k < 4; ++k) {
        const double angle = k * std::numbers::pi / 4;
        const double dx = std::cos(angle) * seam, dz = std::sin(angle) * seam;
        const Ground one = ground_at(x + dx, z + dz, y + step_up, step_up);
        const Ground other = ground_at(x - dx, z - dz, y + step_up, step_up);
        const Ground& lower = one.height < other.height ? one : other;
        if (lower.height <= y + step_up && lower.height > best.height + step_up) {
            best = lower;
        }
    }
    return best;
}

std::optional<World::RayHit> World::raycast_hit(Vec3 a, Vec3 b) const { return cast(a, b, blocks_shots, false); }

std::optional<double> World::raycast(Vec3 a, Vec3 b) const {
    if (const auto hit = cast(a, b, blocks_shots, false)) {
        return hit->t;
    }
    return std::nullopt;
}

bool World::walk_blocked(Vec3 a, Vec3 b) const { return cast(a, b, blocks_characters, true).has_value(); }

bool World::inside_solid(Vec3 point, double reach) const {
    // Up first (cheap: in the open nothing's over it), then level all around.
    for (int k = -1; k < 8; ++k) {
        const double angle = k * std::numbers::pi / 4;
        const Vec3 d = k < 0 ? Vec3{0, reach, 0} : Vec3{reach * std::cos(angle), 0, reach * std::sin(angle)};
        double nearest = 2;
        bool behind = false;
        for_solids_along(point, point + d, [&](const Solid& s) {
            if (!(s.blocks & blocks_characters)) {
                return;
            }
            if (const auto t = segment_triangle(point, d, s.a, s.b, s.c); t && *t >= 0 && *t <= 1 && *t < nearest) {
                nearest = *t;
                behind = s.normal.dot(d) > 0;
            }
        });
        if (nearest > 1 || !behind) {
            return false;
        }
    }
    return true;
}

bool World::floor_between(double x, double z, double low, double high) const {
    const Vec3 p{x, 0, z};
    bool found = false;
    for_solids(x, x, z, z, [&](const Solid& s) {
        if (found || !s.floor || !(s.blocks & blocks_characters) || x < s.min_x || x > s.max_x || z < s.min_z ||
            z > s.max_z || s.min_y > high) {
            return;
        }
        const double area = cross_xz(s.a, s.b, s.c);
        if (std::abs(area) < 1e-12) {
            return;
        }
        const double wa = cross_xz(s.b, s.c, p) / area;
        const double wb = cross_xz(s.c, s.a, p) / area;
        const double wc = 1 - wa - wb;
        if (wa < 0 || wb < 0 || wc < 0) {
            return;
        }
        const double height = wa * s.a.y + wb * s.b.y + wc * s.c.y;
        found = height > low && height <= high;
    });
    return found;
}

std::optional<World::RayHit> World::cast(Vec3 a, Vec3 b, uint8_t blocks, bool front_only) const {
    std::optional<RayHit> best;
    const Vec3 d = b - a;
    const auto consider = [&](double t, Vec3 normal, int destructible = -1) {
        if (t >= 0 && t <= 1 && (!best || t < best->t)) {
            const double len = normal.length();
            if (len > 1e-9) {
                normal = normal * (1.0 / len);
            } else {
                normal = {0, 1, 0};
            }
            if (dot(normal, d) > 0) {
                normal = normal * -1.0;
            }
            best = RayHit{t, normal, destructible};
        }
    };

    // Terrain: march in steps of at most a meter, then bisect the crossing.
    const auto below = [&](double t) {
        const Vec3 p = a + d * t;
        return p.y < terrain_.height_at(p.x, p.z) && !terrain_.cut_at(p.x, p.z);
    };
    if (!below(0)) {
        const int steps = std::max(1, int(std::ceil(d.length())));
        for (int i = 1; i <= steps; ++i) {
            const double t = double(i) / steps;
            if (below(t)) {
                double lo = double(i - 1) / steps, hi = t;
                for (int k = 0; k < 12; ++k) {
                    const double mid = (lo + hi) / 2;
                    (below(mid) ? hi : lo) = mid;
                }
                const Vec3 at = a + d * hi;
                consider(hi, terrain_.normal_at(at.x, at.z));
                break;
            }
        }
    }

    for_solids_along(a, b, [&](const Solid& s) {
        if ((s.blocks & blocks) && (!front_only || s.normal.dot(d) < 0)) {
            if (const auto t = segment_triangle(a, d, s.a, s.b, s.c)) {
                consider(*t, s.normal, s.part > 0 ? s.part - 1 : -1);
            }
        }
    });

    const double min_x = std::min(a.x, b.x), max_x = std::max(a.x, b.x);
    const double min_z = std::min(a.z, b.z), max_z = std::max(a.z, b.z);
    for (const Building& building : buildings_) {
        if (max_x < building.min_x || min_x > building.max_x || max_z < building.min_z || min_z > building.max_z) {
            continue;
        }
        // Floors (Moller-Trumbore, both sides).
        for (const Floor& f : building.floors) {
            const Vec3 e1 = f.b - f.a, e2 = f.c - f.a;
            const Vec3 p{d.y * e2.z - d.z * e2.y, d.z * e2.x - d.x * e2.z, d.x * e2.y - d.y * e2.x};
            const double det = e1.x * p.x + e1.y * p.y + e1.z * p.z;
            if (std::abs(det) < 1e-12) {
                continue;
            }
            const Vec3 s = a - f.a;
            const double u = (s.x * p.x + s.y * p.y + s.z * p.z) / det;
            if (u < 0 || u > 1) {
                continue;
            }
            const Vec3 q{s.y * e1.z - s.z * e1.y, s.z * e1.x - s.x * e1.z, s.x * e1.y - s.y * e1.x};
            const double v = (d.x * q.x + d.y * q.y + d.z * q.z) / det;
            if (v < 0 || u + v > 1) {
                continue;
            }
            consider((e2.x * q.x + e2.y * q.y + e2.z * q.z) / det, cross(e1, e2));
        }
        // Walls: vertical quads above their floor edge.
        for (const Wall& w : building.walls) {
            const double ex = w.b.x - w.a.x, ez = w.b.z - w.a.z;
            const double denom = d.x * ez - d.z * ex;
            if (std::abs(denom) < 1e-12) {
                continue;
            }
            const double t = ((w.a.x - a.x) * ez - (w.a.z - a.z) * ex) / denom;
            const double u = ((w.a.x - a.x) * d.z - (w.a.z - a.z) * d.x) / denom;
            if (t < 0 || t > 1 || u < 0 || u > 1) {
                continue;
            }
            const double y = a.y + d.y * t;
            const double floor = w.a.y + (w.b.y - w.a.y) * u;
            if (y >= floor && y <= floor + (w.top - std::max(w.a.y, w.b.y))) {
                consider(t, {-ez, 0, ex});
            }
        }
    }
    return best;
}

Vec3 World::resolve_walls(Vec3 from, Vec3 position, double radius, double height, double step_up,
                          Vec3& velocity) const {
    return push_out(from, position, radius, height, step_up, velocity, true);
}

bool World::overlaps_walls(Vec3 feet, double radius, double height, double step_up) const {
    Vec3 velocity;
    const Vec3 pushed = push_out(feet, feet, radius, height, step_up, velocity, false);
    return std::abs(pushed.x - feet.x) + std::abs(pushed.z - feet.z) >= 1e-9;
}

Vec3 World::push_out(Vec3 from, Vec3 position, double radius, double height, double step_up, Vec3& velocity,
                     bool let_out) const {
    // A few passes settle corners where two walls push at once.
    for (int pass = 0; pass < 3; ++pass) {
        bool moved = false;
        for (const Building& building : buildings_) {
            if (position.x < building.min_x - radius || position.x > building.max_x + radius ||
                position.z < building.min_z - radius || position.z > building.max_z + radius) {
                continue;
            }
            for (const Wall& wall : building.walls) {
                // Walls a character can step over, or walks under, don't block.
                if (position.y + height <= wall.bottom + step_up || position.y >= wall.top) {
                    continue;
                }
                const double ex = wall.b.x - wall.a.x;
                const double ez = wall.b.z - wall.a.z;
                const double length2 = ex * ex + ez * ez;
                if (length2 < 1e-12) {
                    continue;
                }
                const double t = ((position.x - wall.a.x) * ex + (position.z - wall.a.z) * ez) / length2;
                double nx, nz, distance;
                if (t > 0 && t < 1) {
                    // Along the wall: push out on the side the move came from,
                    // so a fast move can't tunnel through.
                    const double length = std::sqrt(length2);
                    nx = -ez / length;
                    nz = ex / length;
                    if (cross_xz(wall.a, wall.b, from) < 0) {
                        nx = -nx;
                        nz = -nz;
                    }
                    distance = (position.x - wall.a.x) * nx + (position.z - wall.a.z) * nz;
                } else {
                    // Past an end: push away from the corner point.
                    const Vec3 corner = t <= 0 ? wall.a : wall.b;
                    nx = position.x - corner.x;
                    nz = position.z - corner.z;
                    distance = std::sqrt(nx * nx + nz * nz);
                    if (distance < 1e-9) {
                        continue;
                    }
                    nx /= distance;
                    nz /= distance;
                }
                if (distance >= radius) {
                    continue;
                }
                position.x += nx * (radius - distance);
                position.z += nz * (radius - distance);
                const double into = velocity.x * nx + velocity.z * nz;
                if (into < 0) {
                    velocity.x -= nx * into;
                    velocity.z -= nz * into;
                }
                moved = true;
            }
        }
        // Steep map triangles: the part of each between the character's
        // knees (step height) and head, seen from above, pushes the body's
        // circle out: away from its nearest point, or, if the center is over
        // it, across it toward the side the move came from.
        //
        // One the body started the move inside of (its center behind the
        // whole face, which faces outward) doesn't push: a body that
        // got inside a prop walks out, rather than being pinned by faces
        // pushing opposite ways. From outside, a body is always in front of
        // the faces it touches.
        const double lo = position.y + step_up, hi = position.y + height;
        const Vec3 start{from.x, 0, from.z};
        for_solids(position.x - radius, position.x + radius, position.z - radius, position.z + radius,
                   [&](const Solid& s) {
            if (s.floor || s.ceiling || !(s.blocks & blocks_characters) || s.max_y <= lo || s.min_y >= hi ||
                position.x + radius < s.min_x || position.x - radius > s.max_x || position.z + radius < s.min_z ||
                position.z - radius > s.max_z) {
                return;
            }
            const Vec3 triangle[3] = {s.a, s.b, s.c};
            Vec3 slab[8];
            const int n = slab_of(triangle, lo, hi, slab);
            if (n == 0) {
                return;
            }
            const Vec3 center{position.x, 0, position.z};
            const Contact contact = contact_xz(slab, n, center);
            if (!contact.over && contact.distance >= radius) {
                return;
            }
            Vec3 before[8];
            const int m = let_out ? slab_of(triangle, from.y + step_up, from.y + height, before) : 0;
            if (m > 0) {
                const double flat = std::hypot(s.normal.x, s.normal.z);
                bool behind = flat > 1e-9;
                for (int i = 0; i < m && behind; ++i) {
                    behind = ((start.x - before[i].x) * s.normal.x + (start.z - before[i].z) * s.normal.z) / flat < -1e-6;
                }
                if (behind) {
                    return;
                }
            }
            double nx = 0, nz = 0, push = 0;
            if (!contact.over && contact.distance > 1e-9) {
                nx = (center.x - contact.nearest.x) / contact.distance;
                nz = (center.z - contact.nearest.z) / contact.distance;
                push = radius - contact.distance;
            }
            if (push == 0) {
                // Over it (or on it): across, along the triangle's facing,
                // to the side the move came from: that of the part at the
                // body's height (a leaning face's corners can be well off
                // to one side of it).
                const double flat = std::hypot(s.normal.x, s.normal.z);
                if (flat < 1e-9) {
                    return;
                }
                nx = s.normal.x / flat;
                nz = s.normal.z / flat;
                double low = std::numeric_limits<double>::infinity(), high = -low;
                for (int i = 0; i < n; ++i) {
                    const double along = slab[i].x * nx + slab[i].z * nz;
                    low = std::min(low, along);
                    high = std::max(high, along);
                }
                if (from.x * nx + from.z * nz < (low + high) / 2) {
                    nx = -nx;
                    nz = -nz;
                }
                double support = 0;
                for (int i = 0; i < n; ++i) {
                    support = std::max(support, (slab[i].x - center.x) * nx + (slab[i].z - center.z) * nz);
                }
                push = support + radius;
            }
            position.x += nx * push;
            position.z += nz * push;
            const double into = velocity.x * nx + velocity.z * nz;
            if (into < 0) {
                velocity.x -= nx * into;
                velocity.z -= nz * into;
            }
            moved = true;
        });
        if (!moved) {
            break;
        }
    }
    return position;
}

std::shared_ptr<const World> load_world(const std::filesystem::path& bake) {
    struct Owned {
        Heightfield ground;
        World world;
        explicit Owned(Heightfield g) : ground(std::move(g)), world(ground) {}
    };
    auto owned = std::make_shared<Owned>(load_bake(bake));
    owned->world.add_map_collision_of(bake);
    return std::shared_ptr<const World>(owned, &owned->world);
}

} // namespace swm::core
