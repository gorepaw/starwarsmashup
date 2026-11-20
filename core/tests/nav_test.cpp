// Bots' walkable grid (NavGrid): routes around walls and barriers, on
// synthetic ground, and a soldier actually walking them.

#include <cmath>
#include <cstdio>
#include <numbers>

#include "swm/core/conquest.h"
#include "swm/core/heightfield.h"
#include "swm/core/movement.h"
#include "swm/core/nav.h"
#include "swm/core/world.h"
#include "test.h"

using namespace swm::core;
using swm::test::near;

namespace {

// Flat ground: 101 x 101 samples 1 m apart; game x runs 0..-100, z 0..100.
Heightfield flat() {
    return Heightfield(101, 1.0, 0, 0, std::vector<float>(101 * 101, 0.0f));
}

void add_box(MapCollision& collision, Vec3 low, Vec3 high) {
    const auto corner = [&](int i) {
        return Vec3{i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z};
    };
    const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (const auto& f : faces) {
        collision.triangles.push_back({corner(f[0]), corner(f[2]), corner(f[1])});
        collision.triangles.push_back({corner(f[0]), corner(f[3]), corner(f[2])});
    }
}

Battlefield open_field() {
    Battlefield field;
    field.min_x = -100;
    field.max_x = 0;
    field.min_z = 0;
    field.max_z = 100;
    return field;
}

double length(const std::vector<Vec3>& path, Vec3 from) {
    double total = 0;
    for (const Vec3& p : path) {
        total += std::hypot(p.x - from.x, p.z - from.z);
        from = p;
    }
    return total;
}

// Every leg of the route is a clear line, and it ends where it should.
bool sound(const NavGrid& grid, const std::vector<Vec3>& path, Vec3 from) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (!grid.clear_line(path[i - 1], path[i])) {
            std::printf("leg %zu (%.2f %.2f) -> (%.2f %.2f) isn't clear\n", i, path[i - 1].x, path[i - 1].z, path[i].x,
                        path[i].z);
            return false;
        }
    }
    return !path.empty() && (grid.walkable(from) ? grid.clear_line(from, path[0]) : true);
}

// A soldier walks the route as a bot would: toward each corner in turn,
// moving on once within 0.6 m. Returns where it ends up after `seconds`.
Vec3 walk(const World& world, const std::vector<Vec3>& path, Vec3 from, double seconds) {
    MoveState state;
    state.position = from;
    MoveInput input;
    size_t corner = 0;
    for (int tick = 0; tick < int(seconds * 30); ++tick) {
        while (corner + 1 < path.size() &&
               std::hypot(path[corner].x - state.position.x, path[corner].z - state.position.z) < 0.6) {
            ++corner;
        }
        const Vec3 d{path[corner].x - state.position.x, 0, path[corner].z - state.position.z};
        input.sequence++;
        input.yaw = float(std::atan2(-d.x, -d.z));
        input.forward = d.length() > 0.2 ? 127 : 0;
        state = simulate_move(state, input, world, MoveParams{}, 1.0 / 30);
    }
    return state.position;
}

void test_open_ground() {
    const Heightfield ground = flat();
    const World world(ground);
    const NavGrid grid(world, open_field());
    const Vec3 from{-30, 0, 50}, to{-70, 0, 50.2};
    const auto path = grid.find_path(from, to);
    // Nothing in the way: one straight leg, to the exact spot.
    CHECK(path.size() == 1 && near(path[0].x, to.x) && near(path[0].z, to.z) && near(path[0].y, 0));
    CHECK(grid.walkable(from) && grid.clear_line(from, to));
    // Outside the bounds: nowhere.
    CHECK(!grid.walkable({-50, 0, 120}) && grid.find_path({-50, 0, 120}, to).empty());
    CHECK(grid.tiles_built() > 0 && grid.tiles_built() < 64);  // only where it looked
}

void test_around_a_wall() {
    // A wall 1 m thick and 3 m tall from z = 20 to 80, between from and to.
    const Heightfield ground = flat();
    MapCollision wall;
    add_box(wall, {-50.5, 0, 20}, {-49.5, 3, 80});
    World world(ground);
    world.add_map_collision(wall);
    const NavGrid grid(world, open_field());
    const Vec3 from{-30, 0, 50}, to{-70, 0, 50};
    CHECK(!grid.walkable({-50, 0, 50}) && !grid.walkable({-49.3, 0, 50}) && grid.walkable({-48.5, 0, 50}));
    CHECK(!grid.clear_line(from, to));
    const auto path = grid.find_path(from, to);
    CHECK(sound(grid, path, from));
    CHECK(!path.empty() && near(path.back().x, to.x) && near(path.back().z, to.z));
    // Around an end of the wall: a few corners, not much over the shortest
    // way (two legs of hypot(20, 30), past the end with some clearance).
    const double shortest = 2 * std::hypot(20.0, 30.0);
    std::printf("around a wall: %zu corners, %.1f m (shortest %.1f)\n", path.size(), length(path, from), shortest);
    CHECK(path.size() >= 2 && path.size() <= 4);
    CHECK(length(path, from) > shortest && length(path, from) < shortest + 2.5);
    // A soldier walking it gets there (running straight, it would stop at the wall).
    const Vec3 end = walk(world, path, from, 20);
    CHECK(std::hypot(end.x - to.x, end.z - to.z) < 0.5);
}

void test_rocks_and_logs() {
    const Heightfield ground = flat();
    MapCollision props;
    add_box(props, {-55, 0, 45}, {-45, 3, 55});    // a big rock
    add_box(props, {-55, 0, 70}, {-45, 0.4, 72});  // a log low enough to step on
    add_box(props, {-20, 1.2, 20}, {-10, 1.4, 30});  // a low ledge or branch, at chest height
    World world(ground);
    world.add_map_collision(props);
    const NavGrid grid(world, open_field());
    CHECK(!grid.walkable({-55.1, 0, 50}) && !grid.walkable({-44.9, 0, 50}));
    CHECK(!grid.walkable({-15, 0, 25}) && grid.walkable({-15, 0, 31}));
    CHECK(grid.walkable({-50, 0, 71}) && grid.clear_line({-50, 0, 65}, {-50, 0, 78}));
    // A destination inside the rock: the route ends at the nearest
    // reachable place, by its side.
    const auto path = grid.find_path({-30, 0, 50}, {-50, 0, 50});
    CHECK(sound(grid, path, {-30, 0, 50}));
    // (Cells within the body's widened radius, 0.45 m, of it aren't walkable.)
    const double side = !path.empty() ? std::max(std::abs(path.back().x + 50), std::abs(path.back().z - 50)) : 0;
    CHECK(side > 5.45 && side < 6);
}

void test_barriers() {
    const Heightfield ground = flat();
    const World world(ground);
    Battlefield field = open_field();
    // A 10 m square barrier on the straight line: worth going around.
    field.barriers.push_back({{Vec3{-45, 0, 45}, Vec3{-55, 0, 45}, Vec3{-55, 0, 55}, Vec3{-45, 0, 55}}});
    CHECK(field.barriers[0].contains({-50, 0, 50}) && !field.barriers[0].contains({-50, 0, 56}));
    const NavGrid grid(world, field);
    CHECK(grid.walkable({-50, 0, 50}));  // walkable, but dear
    const Vec3 from{-30, 0, 50};
    const auto path = grid.find_path(from, {-70, 0, 50});
    CHECK(sound(grid, path, from) && path.size() >= 2);
    // (A leg may clip a corner by part of a cell.)
    const Barrier inner{{Vec3{-45.5, 0, 45.5}, Vec3{-54.5, 0, 45.5}, Vec3{-54.5, 0, 54.5}, Vec3{-45.5, 0, 54.5}}};
    Vec3 at = from;
    bool entered = false;
    for (const Vec3& corner : path) {
        for (int i = 1; i <= 100; ++i) {
            const double t = i / 100.0;
            entered |= inner.contains({at.x + (corner.x - at.x) * t, 0, at.z + (corner.z - at.z) * t});
        }
        at = corner;
    }
    CHECK(!entered && length(path, from) < 45);
    // A barrier across the whole field is crossed anyway.
    field.barriers[0] = {{Vec3{-45, 0, -1}, Vec3{-55, 0, -1}, Vec3{-55, 0, 101}, Vec3{-45, 0, 101}}};
    const NavGrid fenced(world, field);
    const auto across = fenced.find_path(from, {-70, 0, 50});
    CHECK(!across.empty() && near(across.back().x, -70) && near(across.back().z, 50));
}

void test_slopes() {
    // A hill: flat to x = -40 (Galaxies 40), then rising 2 m per m: too
    // steep to walk.
    std::vector<float> heights;
    for (int row = 0; row < 101; ++row) {
        for (int column = 0; column < 101; ++column) {
            heights.push_back(column < 40 ? 0.0f : column < 45 ? float(2 * (column - 40)) : 10.0f);
        }
    }
    const Heightfield hill(101, 1.0, 0, 0, std::move(heights));
    const World world(hill);
    const NavGrid grid(world, open_field());
    CHECK(!grid.walkable({-42.5, 0, 50}));
    CHECK(!grid.clear_line({-30, 0, 50}, {-60, 10, 50}));
    const auto up = grid.find_path({-30, 0, 50}, {-60, 10, 50});
    CHECK(!up.empty() && up.back().x > -41);  // gets as near as it can, at the foot
}

void test_cost() {
    // Walls across the field, open at alternate ends: the route weaves.
    const Heightfield ground = flat();
    MapCollision walls;
    for (int i = 0; i < 9; ++i) {
        const double x = -10.0 - i * 9;
        const double z0 = i % 2 ? 15 : -1, z1 = i % 2 ? 101 : 85;
        add_box(walls, {x - 0.5, 0, z0}, {x + 0.5, 3, z1});
    }
    World world(ground);
    world.add_map_collision(walls);
    const NavGrid grid(world, open_field());
    const Vec3 from{-5, 0, 50}, to{-95, 0, 50};
    const auto path = grid.find_path(from, to);
    CHECK(sound(grid, path, from) && !path.empty() && near(path.back().x, to.x) && near(path.back().z, to.z));
    std::printf("maze: %zu corners, %.0f m, %zu tiles\n", path.size(), length(path, from), grid.tiles_built());
    CHECK(length(path, from) > 9 * 60);
}

} // namespace

int main() {
    test_open_ground();
    test_around_a_wall();
    test_rocks_and_logs();
    test_barriers();
    test_slopes();
    test_cost();
    return swm::test::finish("nav_test");
}
