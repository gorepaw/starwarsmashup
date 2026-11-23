// Bots' walkable grid (NavGrid): routes around walls and barriers, on
// synthetic ground, and a soldier actually walking them.

#include <cmath>
#include <cstdio>
#include <filesystem>
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
    // Ground in a height range: there between -1 and 1, not between 2 and 3.
    CHECK(grid.walkable_between(-50, 50, -1, 1) && !grid.walkable_between(-50, 50, 2, 3));
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

    // A post deep inside a barrier (Hoth's CP3): the way in, and out, is
    // straight through it, not round to the side nearest the post.
    field.barriers[0] = {{Vec3{-40, 0, 30}, Vec3{-80, 0, 30}, Vec3{-80, 0, 70}, Vec3{-40, 0, 70}}};
    const NavGrid deep(world, field);
    const Vec3 post{-75, 0, 50};
    const auto in = deep.find_path(from, post);
    CHECK(!in.empty() && near(in.back().x, post.x) && near(in.back().z, post.z) && length(in, from) < 50);
    const auto out = deep.find_path(post, from);
    CHECK(!out.empty() && near(out.back().x, from.x) && near(out.back().z, from.z) && length(out, post) < 50);
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
    // Flat ground at the hill's foot, but the hill within a body's reach: a
    // soldier straying there stops against it (Geonosis's hangar door).
    CHECK(grid.walkable({-39.25, 0, 50}) && !grid.walkable({-39.75, 0, 50}));
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

// A goal in a room whose door faces away, and a search too short to cover
// everything outside the room on the near side (Hoth's hangar, under a
// hill): back from the goal, out of the door and round, it gets there.
void test_backward() {
    const Heightfield ground = flat();
    MapCollision walls;
    add_box(walls, {-60.5, 0, 40}, {-60, 3, 60});    // facing the start
    add_box(walls, {-80, 0, 39.5}, {-60, 3, 40});    // sides
    add_box(walls, {-80, 0, 60}, {-60, 3, 60.5});
    add_box(walls, {-80.5, 0, 40}, {-80, 3, 48});    // the far wall, its door
    add_box(walls, {-80.5, 0, 52}, {-80, 3, 60});    // at z 48..52
    World world(ground);
    world.add_map_collision(walls);
    NavGrid::Settings settings;
    settings.max_expansions = 6000;
    const NavGrid grid(world, open_field(), MoveParams{}, settings);
    const Vec3 from{-10, 0, 50}, goal{-70, 0, 50};
    const auto path = grid.find_path(from, goal);
    CHECK(sound(grid, path, from) && !path.empty() && near(path.back().x, goal.x) && near(path.back().z, goal.z));
    std::printf("into the room: %zu corners, %.0f m\n", path.size(), length(path, from));
    const Vec3 reached = walk(world, path, from, 30);
    CHECK(std::hypot(reached.x - goal.x, reached.z - goal.z) < 0.6);
}

// A knee-high rail's coping (Naboo's terraces): a flat top 0.8 m up on two
// sloping undersides, narrower than a cell. Movement won't take a soldier
// under it or over it (it's no step), so routes go round its end.
void test_coping() {
    const Heightfield ground = flat();
    MapCollision rail;
    const Vec3 a0{-50.1, 0.8, 30}, a1{-49.8, 0.8, 30}, b0{-50.1, 0.8, 70}, b1{-49.8, 0.8, 70};
    const Vec3 r0{-49.95, 0.6, 30}, r1{-49.95, 0.6, 70};
    rail.triangles.push_back({a0, b1, a1});  // the top, facing up
    rail.triangles.push_back({a0, b0, b1});
    rail.triangles.push_back({a0, a1, r0});  // ends
    rail.triangles.push_back({b0, r1, b1});
    rail.triangles.push_back({a0, r0, r1});  // undersides, facing down
    rail.triangles.push_back({a0, r1, b0});
    rail.triangles.push_back({a1, r1, r0});
    rail.triangles.push_back({a1, b1, r1});
    World world(ground);
    world.add_map_collision(rail);
    CHECK(world.floor_between(-49.95, 50, 0.5, 1.5) && !world.floor_between(-49.6, 50, 0.5, 1.5));
    const NavGrid grid(world, open_field());
    CHECK(!grid.walkable({-49.75, 0, 50}) && !grid.walkable({-50.25, 0, 50}) && grid.walkable({-48.75, 0, 50}));
    const Vec3 from{-45, 0, 50}, to{-55, 0, 50};
    const auto path = grid.find_path(from, to);
    CHECK(sound(grid, path, from) && !path.empty() && near(path.back().x, to.x) && length(path, from) > 40);
    const Vec3 reached = walk(world, path, from, 20);
    CHECK(std::hypot(reached.x - to.x, reached.z - to.z) < 0.6);
}

// Surfaces stacked in a cell: a deck 3 m up, reached by a ramp, with open
// ground under it; and a cellar under the terrain, reached through a hole
// cut in it (like Endor's bunker).
void test_stacked() {
    Heightfield ground = flat();
    MapCollision props;
    add_box(props, {-60, 3, 45}, {-40, 3.3, 55});  // the deck
    // The ramp: from the ground at x -30 up to the deck's edge at x -40.
    props.triangles.push_back({{-30, 0, 45}, {-40, 3.3, 45}, {-40, 3.3, 55}});
    props.triangles.push_back({{-30, 0, 45}, {-40, 3.3, 55}, {-30, 0, 55}});
    World world(ground);
    world.add_map_collision(props);
    const NavGrid grid(world, open_field());
    // Under the deck and on it.
    CHECK(grid.walkable({-50, 0, 50}) && grid.walkable({-50, 3.3, 50}));
    // Under it is a straight line; onto it isn't.
    const Vec3 south{-50, 0, 35}, north{-50, 0, 65}, deck{-50, 3.3, 50};
    CHECK(grid.clear_line(south, north) && !grid.clear_line(south, deck));
    const auto under = grid.find_path(south, north);
    CHECK(sound(grid, under, south) && under.size() == 1 && near(length(under, south), 30));
    // Onto it: up the ramp.
    const auto up = grid.find_path(south, deck);
    CHECK(sound(grid, up, south) && !up.empty() && near(up.back().y, 3.3, 0.01) && length(up, south) > 25);
    const Vec3 end = walk(world, up, south, 15);
    CHECK(std::hypot(end.x - deck.x, end.z - deck.z) < 0.5 && near(end.y, 3.3, 0.05));
    // And off it the short way: dropping off its edge.
    const auto down = grid.find_path(deck, north);
    CHECK(sound(grid, down, deck) && length(down, deck) < 16);

    // The cellar: a floor 4 m under the terrain from x -70 to -90, its
    // ceiling 1 m under the terrain, reached by a ramp down a hole cut from
    // x -60 to -70.
    MapCollision cellar;
    add_box(cellar, {-90, -5, 10}, {-70, -4, 20});    // the floor
    add_box(cellar, {-90, -1, 10}, {-70, -0.5, 20});  // the ceiling
    cellar.triangles.push_back({{-60, 0, 10}, {-70, -4, 10}, {-70, -4, 20}});
    cellar.triangles.push_back({{-60, 0, 10}, {-70, -4, 20}, {-60, 0, 20}});
    ground.set_cuts({TerrainCut{{-65, -2, 15}, 8,
                                {{1, 0, 0, 60}, {-1, 0, 0, -70}, {0, 0, 1, -20}, {0, 0, -1, 10}, {0, 1, 0, -1},
                                 {0, -1, 0, -6}}}});
    World below(ground);
    below.add_map_collision(cellar);
    const NavGrid cellar_grid(below, open_field());
    const Vec3 outside{-50, 0, 15}, inside{-80, -4, 15};
    // Above it on the terrain, and in it.
    CHECK(cellar_grid.walkable({-80, 0, 15}) && cellar_grid.walkable(inside));
    const auto in = cellar_grid.find_path(outside, inside);
    CHECK(sound(cellar_grid, in, outside) && !in.empty() && near(in.back().y, -4, 0.01));
    const Vec3 reached = walk(below, in, outside, 12);
    CHECK(std::hypot(reached.x - inside.x, reached.z - inside.z) < 0.5 && near(reached.y, -4, 0.05));
}

void test_edges() {
    // A deck 4 m wide and 3 m up, from z 20 to 80: a route along it from one
    // end to the other, starting and ending by its east edge, keeps a body's
    // reach in from the edges on the way.
    const Heightfield ground = flat();
    MapCollision props;
    add_box(props, {-52, 2.7, 20}, {-48, 3, 80});
    World world(ground);
    world.add_map_collision(props);
    const NavGrid grid(world, open_field());
    const Vec3 start{-48.3, 3, 25}, end{-48.3, 3, 75};
    const auto along = grid.find_path(start, end);
    CHECK(sound(grid, along, start) && !along.empty() && near(along.back().y, 3, 0.01));
    Vec3 from = start, middle{};
    double longest = 0;
    for (const Vec3& p : along) {
        if (const double leg = std::hypot(p.x - from.x, p.z - from.z); leg > longest) {
            longest = leg;
            middle = (from + p) * 0.5;
        }
        from = p;
    }
    CHECK(longest > 30 && middle.x < -48.4 && middle.x > -51.6);
    const Vec3 reached = walk(world, along, start, 12);
    CHECK(std::hypot(reached.x - end.x, reached.z - end.z) < 0.6 && near(reached.y, 3, 0.05));

    // A ramp at 52 degrees, onto a ledge: soldiers walk up it (they manage
    // 55), but routes keep off it (they take 50).
    MapCollision steep;
    steep.triangles.push_back({{-70, 0, 20}, {-75, 6.4, 20}, {-75, 6.4, 30}});
    steep.triangles.push_back({{-70, 0, 20}, {-75, 6.4, 30}, {-70, 0, 30}});
    add_box(steep, {-85, 6.0, 20}, {-75, 6.4, 30});
    World ramp_world(ground);
    ramp_world.add_map_collision(steep);
    const NavGrid ramp_grid(ramp_world, open_field());
    CHECK(!ramp_grid.walkable({-72.5, 3.2, 25}) && ramp_grid.walkable({-80, 6.4, 25}));
    const Vec3 top = walk(ramp_world, {{-80, 6.4, 25}}, {-65, 0, 25}, 5);
    CHECK(std::hypot(top.x + 80, top.z - 25) < 0.6 && near(top.y, 6.4, 0.05));
}

void test_inside_a_prop() {
    // A soldier inside a hollow prop (a root lying on the ground, its faces
    // all outward): the grid sees no way out, but movement lets it walk out
    // through a face it's behind, so its route starts straight out.
    const Heightfield ground = flat();
    MapCollision props;
    add_box(props, {-54, 0, 46}, {-46, 2.5, 54});
    World world(ground);
    world.add_map_collision(props);
    const NavGrid grid(world, open_field());
    const Vec3 inside{-50, 0, 50}, away{-20, 0, 50};
    const auto out = grid.find_path(inside, away);
    CHECK(!out.empty() && std::hypot(out.back().x - away.x, out.back().z - away.z) < 0.5);
    const Vec3 reached = walk(world, out, inside, 10);
    CHECK(std::hypot(reached.x - away.x, reached.z - away.z) < 0.6);

    // The ground inside is no place to stand (Yavin's minitemple): a route
    // there ends outside it, beside it.
    CHECK(world.inside_solid({-50, 1, 50}, 40));
    CHECK(!world.inside_solid({-44, 1, 50}, 40));
    CHECK(!world.inside_solid({-50, 3, 50}, 40));  // on top
    CHECK(!grid.walkable(inside));
    const auto in = grid.find_path(away, inside);
    CHECK(!in.empty());
    if (!in.empty()) {
        const Vec3 end = in.back();
        CHECK(std::abs(end.x + 50) > 4 || std::abs(end.z - 50) > 4);
        CHECK(std::hypot(end.x + 50, end.z - 50) < 6);
    }
}

void test_high_and_deadly() {
    // A deck 100 m over the terrain (a map without terrain: the Death Star,
    // its floors far over the ground no one sees): on the grid.
    const Heightfield ground = flat();
    MapCollision high;
    add_box(high, {-60, 99.7, 40}, {-40, 100, 60});
    World high_world(ground);
    high_world.add_map_collision(high);
    const NavGrid high_grid(high_world, open_field());
    CHECK(high_grid.walkable_between(-50, 50, 99, 101) && high_grid.walkable_between(-50, 50, -1, 1));

    // A deck 10 m up with no way down but dropping off it, through a thin
    // death region all around (the Death Star's over a chasm's floor): the
    // ground beside it is safe to stand on, but no route falls to it.
    MapCollision deck;
    add_box(deck, {-60, 9.7, 40}, {-40, 10, 60});
    World world(ground);
    world.add_map_collision(deck);
    Battlefield field = open_field();
    field.death_regions.push_back({DeathRegion::Shape::box, {-50, 4.5, 50}, {20, 0.5, 20}});
    const NavGrid grid(world, field);
    CHECK(grid.walkable({-35, 0, 50}) && grid.walkable({-50, 10, 50}));
    const auto down = grid.find_path({-50, 10, 50}, {-35, 0, 50});
    CHECK(!down.empty() && down.back().y > 9);
    // Without it, off the edge.
    const NavGrid open(world, open_field());
    const auto off = open.find_path({-50, 10, 50}, {-35, 0, 50});
    CHECK(!off.empty() && near(off.back().y, 0, 0.01) && std::hypot(off.back().x + 35, off.back().z - 50) < 0.5);
    // A damage region there instead (Mustafar's lava): no way either; and
    // no standing on the ground in it.
    Battlefield hot = open_field();
    hot.damage_regions.push_back({{DeathRegion::Shape::box, {-50, 0.5, 50}, {20, 5, 20}}, 1000});
    const NavGrid hot_grid(world, hot);
    CHECK(!hot_grid.walkable({-35, 0, 50}) && hot_grid.walkable({-50, 10, 50}) && hot_grid.walkable({-25, 0, 50}));
    const auto into = hot_grid.find_path({-50, 10, 50}, {-25, 0, 50});
    CHECK(!into.empty() && into.back().y > 9);
}

// Where a route drops from a deck 5 m up to the ground under it: the x of
// the first corner on the ground.
double drop_x(const std::vector<Vec3>& path) {
    for (const Vec3& p : path) {
        if (p.y < 1) {
            return p.x;
        }
    }
    return 1e9;
}

void test_crack() {
    // A deck 5 m up, split down the middle by a slit narrower than a body
    // (a crack between Kamino's walkway and a pod's floor): no way down
    // there; off the deck's edge instead. Through a hole 2 m wide, yes.
    // (Each around a cell's middle, x -50.25: the grid sees the ground
    // there.)
    const Heightfield ground = flat();
    const auto route = [&](double gap) {
        MapCollision deck;
        add_box(deck, {-60, 4.7, 40}, {-50.25 - gap / 2, 5, 60});
        add_box(deck, {-50.25 + gap / 2, 4.7, 40}, {-40, 5, 60});
        World world(ground);
        world.add_map_collision(deck);
        const NavGrid grid(world, open_field());
        return grid.find_path({-55, 5, 50}, {-50.25, 0, 50.25});
    };
    const auto slit = route(0.3);
    const auto hole = route(2);
    std::printf("down through a slit: at x %.2f; a hole: at x %.2f\n", drop_x(slit), drop_x(hole));
    CHECK(!slit.empty() && std::abs(drop_x(slit) + 50.25) > 5);
    CHECK(!hole.empty() && std::abs(drop_x(hole) + 50.25) < 1.5);
}

void test_pit() {
    // A pen walled in 6 m high, a deck 4 m up over its east side (up a ramp
    // from the east): a soldier drops into it off the deck's edge, and can't
    // climb back out (the ring round the Death Star's reactor's column,
    // under its catwalks). A route to a spot inside it ends up on the deck,
    // as near as it gets without going in.
    const Heightfield ground = flat();
    MapCollision props;
    add_box(props, {-70.5, 0, 44.5}, {-70, 6, 55.5});   // west wall
    add_box(props, {-70, 0, 44.5}, {-50, 6, 45});       // south
    add_box(props, {-70, 0, 55}, {-50, 6, 55.5});       // north
    add_box(props, {-50.5, 0, 44.5}, {-50, 3.6, 55.5});  // east, under the deck
    add_box(props, {-58, 3.7, 45}, {-40, 4, 55});        // the deck, over the pen's east side
    props.triangles.push_back({{-30, 0, 45}, {-40, 4, 45}, {-40, 4, 55}});  // its ramp
    props.triangles.push_back({{-30, 0, 45}, {-40, 4, 55}, {-30, 0, 55}});
    World world(ground);
    world.add_map_collision(props);
    const NavGrid grid(world, open_field());
    const Vec3 from{-20, 0, 50}, inside{-65, 0, 50};
    CHECK(grid.walkable(inside) && grid.walkable_between(-45, 50, 3.5, 4.5));
    const auto path = grid.find_path(from, inside);
    CHECK(!path.empty());
    if (!path.empty()) {
        std::printf("to the pen: ends (%.2f %.2f %.2f)\n", path.back().x, path.back().y, path.back().z);
        CHECK(near(path.back().y, 4, 0.01) && path.back().x > -58.5 && std::hypot(path.back().x + 58, path.back().z - 50) < 2);
    }
    // From inside, it gets nowhere.
    const auto out = grid.find_path(inside, from);
    CHECK(out.empty() || std::hypot(out.back().x - from.x, out.back().z - from.z) > 20);
    // Off the deck into the pen: no way back (twice: the second known from
    // the first); off its north side outside the pen: back up the ramp.
    // Inside the pen already, anywhere in it.
    const Vec3 deck{-55, 4, 50};
    CHECK(!grid.leads_back(deck, {-60, 0, 50}));
    CHECK(!grid.leads_back(deck, {-66, 0, 47}));
    CHECK(grid.leads_back({-45, 4, 54.5}, {-45, 0, 57}));
    CHECK(grid.leads_back(inside, {-60, 0, 48}));
}

void test_planet_scale_open_terrain() {
    // Full 16 x 16 km planet dimensions (4097 x 4097 at 4 m spacing, origin -8192)
    // with rolling dunes, a steep canyon wall with a 12 m walkable pass, and scattered rocks.
    constexpr int samples = 4097;
    constexpr double spacing = 4.0;
    constexpr double origin = -8192.0;
    std::vector<float> heights(size_t(samples) * size_t(samples), 0.0f);

    const auto set_patch = [&](double gx0, double gx1, double gz0, double gz1, auto&& fn) {
        const int c0 = std::max(0, int(std::floor((-gx1 - origin) / spacing)));
        const int c1 = std::min(samples - 1, int(std::ceil((-gx0 - origin) / spacing)));
        const int r0 = std::max(0, int(std::floor((gz0 - origin) / spacing)));
        const int r1 = std::min(samples - 1, int(std::ceil((gz1 - origin) / spacing)));
        for (int r = r0; r <= r1; ++r) {
            const double z = origin + r * spacing;
            for (int c = c0; c <= c1; ++c) {
                const double x = -(origin + c * spacing);
                heights[size_t(r) * samples + size_t(c)] = float(fn(x, z));
            }
        }
    };

    // Region A: near Mos Eisley outskirts (-3900..-3700, -4900..-4700): dunes + canyon wall at x=-3800 with pass at z in [-4806, -4794]
    set_patch(-3950, -3650, -4950, -4650, [](double x, double z) {
        double h = 6.0 * std::sin(x * 0.025) * std::cos(z * 0.022);
        const double ridge_dx = std::abs(x + 3800.0);
        const double pass_dz = std::abs(z + 4800.0);
        if (ridge_dx < 14.0 && pass_dz > 6.0) {
            const double wall = std::max(0.0, 1.0 - ridge_dx / 14.0) * std::min(1.0, (pass_dz - 6.0) / 4.0);
            h += 35.0 * wall;
        }
        return h;
    });

    // Region B: far quadrant (5900..6100, 5900..6100) where 32-bit Node would overflow (tile_index > 2^19)
    set_patch(5900, 6100, 5900, 6100, [](double x, double z) {
        return 4.0 * std::sin(x * 0.03) + 3.0 * std::cos(z * 0.03);
    });

    Heightfield terrain(samples, spacing, origin, origin, std::move(heights));
    MapCollision rocks;
    const auto add_rock = [&](double x, double z, double half_w, double h) {
        const double y = terrain.height_at(x, z);
        add_box(rocks, {x - half_w, y - 2.0, z - half_w}, {x + half_w, y + h, z + half_w});
    };
    add_rock(-3765.0, -4800.0, 3.0, 4.0);
    add_rock(-3835.0, -4802.0, 3.0, 4.0);
    add_rock(6000.0, 6000.0, 3.5, 4.0);

    World world(terrain);
    world.add_map_collision(rocks);
    Battlefield planet;
    planet.min_x = -8192.0;
    planet.max_x = 8192.0;
    planet.min_z = -8192.0;
    planet.max_z = 8192.0;
    const NavGrid grid(world, planet);

    // 1. Navigate dunes, boulders, and through the canyon pass
    const Vec3 from{-3740.0, terrain.height_at(-3740.0, -4820.0), -4820.0};
    const Vec3 to{-3860.0, terrain.height_at(-3860.0, -4780.0), -4780.0};
    const auto path = grid.find_path(from, to);
    CHECK(sound(grid, path, from));
    CHECK(!path.empty() && std::hypot(path.back().x - to.x, path.back().z - to.z) < 1.5);
    const Vec3 arrived = walk(world, path, from, 40.0);
    CHECK(std::hypot(arrived.x - to.x, arrived.z - to.z) < 2.0);

    // 2. Navigate far quadrant (tests 64-bit Node indexing at high tile indices)
    const Vec3 far_from{5970.0, terrain.height_at(5970.0, 5970.0), 5970.0};
    const Vec3 far_to{6030.0, terrain.height_at(6030.0, 6030.0), 6030.0};
    const auto far_path = grid.find_path(far_from, far_to);
    CHECK(sound(grid, far_path, far_from));
    CHECK(!far_path.empty() && std::hypot(far_path.back().x - far_to.x, far_path.back().z - far_to.z) < 1.5);
    const Vec3 far_arrived = walk(world, far_path, far_from, 30.0);
    CHECK(std::hypot(far_arrived.x - far_to.x, far_arrived.z - far_to.z) < 2.0);
    CHECK(grid.tiles_built() < 400);
}

void test_tatooine_real_bake_if_present() {
    std::filesystem::path bake;
    for (const auto& candidate : {
             std::filesystem::path("game/imported_assets/terrain/tatooine"),
             std::filesystem::path("../game/imported_assets/terrain/tatooine"),
             std::filesystem::path("../../game/imported_assets/terrain/tatooine"),
             std::filesystem::path("../../../game/imported_assets/terrain/tatooine"),
         }) {
        if (std::filesystem::exists(candidate.string() + ".json")) {
            bake = candidate;
            break;
        }
    }
    if (bake.empty()) {
        std::printf("tatooine real bake not present, skipping\n");
        return;
    }
    const auto world = load_world(bake);
    CHECK(world->terrain().samples() == 4097);
    CHECK(world->terrain().spacing() == 4.0);
    // Cantina / city building interiors in Mos Eisley have terrain holes cut out
    const auto& holes = world->terrain().holes();
    CHECK(!holes.empty());
    bool found_hole = false;
    for (int r = 0; r < holes.height && !found_hole; ++r) {
        for (int c = 0; c < holes.width && !found_hole; ++c) {
            if (holes.cells[size_t(r) * size_t(holes.width) + size_t(c)] == 1) {
                CHECK(world->terrain().cut_at(holes.x0 + (c + 0.5) * holes.cell, holes.z0 + (r + 0.5) * holes.cell));
                found_hole = true;
            }
        }
    }
    CHECK(found_hole);

    Battlefield planet;
    planet.min_x = -8192.0;
    planet.max_x = 8192.0;
    planet.min_z = -8192.0;
    planet.max_z = 8192.0;
    const NavGrid grid(*world, planet);
    const Vec3 from{-3950.0, world->terrain().height_at(-3950.0, -4800.0), -4800.0};
    const Vec3 to{-4050.0, world->terrain().height_at(-4050.0, -4760.0), -4760.0};
    const auto path = grid.find_path(from, to);
    CHECK(sound(grid, path, from));
    CHECK(!path.empty() && std::hypot(path.back().x - to.x, path.back().z - to.z) < 2.0);
    const Vec3 arrived = walk(*world, path, from, 40.0);
    CHECK(std::hypot(arrived.x - to.x, arrived.z - to.z) < 2.5);
}

} // namespace

int main() {
    test_open_ground();
    test_around_a_wall();
    test_rocks_and_logs();
    test_barriers();
    test_slopes();
    test_cost();
    test_stacked();
    test_edges();
    test_inside_a_prop();
    test_backward();
    test_coping();
    test_high_and_deadly();
    test_crack();
    test_pit();
    test_planet_scale_open_terrain();
    test_tatooine_real_bake_if_present();
    return swm::test::finish("nav_test");
}
