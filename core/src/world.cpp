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
        grow(wall.a);
        grow(wall.b);
        building.walls.push_back(wall);
    }
    buildings_.push_back(std::move(building));
}

World::Ground World::ground_at(double x, double z, double y, double step_up) const {
    const double reach = y + step_up;
    Ground best;
    bool found = false;
    const double terrain = terrain_.height_at(x, z);
    if (terrain <= reach) {
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
    if (!found) {
        best = {terrain, terrain_.normal_at(x, z), false};
    }
    return best;
}

Vec3 World::resolve_walls(Vec3 from, Vec3 position, double radius, double height, double step_up,
                          Vec3& velocity) const {
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
        if (!moved) {
            break;
        }
    }
    return position;
}

} // namespace swm::core
