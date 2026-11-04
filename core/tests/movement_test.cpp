// Heightfield sampling and character movement on synthetic terrain.

#include <filesystem>
#include <numbers>

#include "swm/core/heightfield.h"
#include "swm/core/movement.h"
#include "swm/core/world.h"
#include "test.h"

using namespace swm::core;
using swm::test::near;

namespace {

// 101 x 101 samples, 1 m apart, Galaxies origin (0, 0): game x runs 0..-100,
// z 0..100. Height from a function of the Galaxies position.
template <typename F>
Heightfield make_ground(F height) {
    std::vector<float> heights;
    for (int row = 0; row < 101; ++row) {
        for (int column = 0; column < 101; ++column) {
            heights.push_back(float(height(double(column), double(row))));
        }
    }
    return Heightfield(101, 1.0, 0, 0, std::move(heights));
}

constexpr double dt = 1.0 / 30;

MoveState run(MoveState state, MoveInput input, const World& world, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        input.sequence++;
        state = simulate_move(state, input, world, MoveParams{}, dt);
    }
    return state;
}

MoveState run(MoveState state, MoveInput input, const Heightfield& ground, int ticks) {
    return run(state, input, World(ground), ticks);
}

void test_heightfield() {
    // A plane rising 0.5 m per Galaxies meter in x: exact on both triangles.
    const Heightfield ramp = make_ground([](double gx, double) { return gx * 0.5; });
    CHECK(near(ramp.height_at(-10, 10), 5));
    CHECK(near(ramp.height_at(-10.25, 10.75), 5.125));  // second triangle of the cell
    CHECK(near(ramp.height_at(-10.75, 10.25), 5.375));  // first triangle
    CHECK(near(ramp.height_at(50, 10), 0));             // outside: nearest edge
    const Vec3 n = ramp.normal_at(-10.5, 10.5);
    CHECK(n.y > 0 && near(n.length(), 1) && n.x > 0);  // uphill is game -x, so it leans toward +x
    // A single raised sample shows which diagonal splits the cells.
    const Heightfield bump = make_ground([](double gx, double gz) { return gx == 11 && gz == 10 ? 1.0 : 0.0; });
    // Cell (10, 10) splits along the line from (11, 10) to (10, 11): its
    // midpoint is halfway up the bump.
    CHECK(near(bump.height_at(-10.5, 10.5), 0.5));
    const Vec3 inside = bump.clamp({5, 0, 150});
    CHECK(inside.x == 0 && inside.z == 100);
}

void test_running() {
    const Heightfield flat = make_ground([](double, double) { return 2.0; });
    MoveState start;
    start.position = {-50, 2, 50};
    MoveInput forward;
    forward.forward = 127;
    forward.yaw = 0;  // facing -Z
    const MoveState after = run(start, forward, flat, 60);  // 2 s
    const MoveParams params;
    // Reaches run speed within 0.14 s, so covers just under 2 s worth.
    CHECK(after.position.z < 50 - 2 * params.run_speed + 0.5 && after.position.z > 50 - 2 * params.run_speed);
    CHECK(near(after.position.x, -50) && near(after.position.y, 2) && after.grounded);

    // Facing +X (yaw -90 degrees) and sprinting.
    MoveInput sprint = forward;
    sprint.yaw = float(-std::numbers::pi / 2);
    sprint.buttons = MoveInput::sprint;
    const MoveState sprinted = run(start, sprint, flat, 30);
    CHECK(near(sprinted.velocity.x, params.run_speed * params.sprint_factor, 1e-6) && sprinted.position.x > -50 + 7);
    CHECK(sprinted.sprinting && sprinted.energy < params.max_energy);

    // Strafing right while facing -Z moves toward +X at strafe speed.
    MoveInput strafe;
    strafe.right = 127;
    const MoveState strafed = run(start, strafe, flat, 30);
    CHECK(near(strafed.velocity.x, params.strafe_speed) && near(strafed.velocity.z, 0));

    // Letting go stops quickly.
    const MoveState stopped = run(after, MoveInput{}, flat, 10);
    CHECK(near(stopped.velocity.length(), 0));
}

void test_slopes_and_jumps() {
    // Gentle slope up toward game -x (0.3 m per meter): walked, staying on the ground.
    const Heightfield gentle = make_ground([](double gx, double) { return gx * 0.3; });
    MoveState start;
    start.position = {-20, gentle.height_at(-20, 50), 50};
    MoveInput uphill;
    uphill.forward = 127;
    uphill.yaw = float(std::numbers::pi / 2);  // facing -X
    const MoveState climbed = run(start, uphill, gentle, 30);
    CHECK(climbed.position.x < -24 && climbed.grounded);
    CHECK(near(climbed.position.y, gentle.height_at(climbed.position.x, climbed.position.z)));

    // Steep cliff (2 m per meter, 63 degrees): blocked going up.
    const Heightfield cliff = make_ground([](double gx, double) { return gx < 30 ? 0.0 : (gx - 30) * 2; });
    start.position = {-25, 0, 50};
    const MoveState blocked = run(start, uphill, cliff, 60);
    CHECK(blocked.position.x > -31 && blocked.position.y < 1);

    // Jump: leaves the ground, peaks near v^2 / 2g, lands again.
    const Heightfield flat = make_ground([](double, double) { return 0.0; });
    start.position = {-50, 0, 50};
    MoveInput jump;
    jump.buttons = MoveInput::jump;
    MoveState state = run(start, jump, flat, 1);
    CHECK(!state.grounded && state.position.y > 0);
    double peak = 0;
    int ticks = 1;
    while (!state.grounded && ticks < 100) {
        state = run(state, MoveInput{}, flat, 1);
        peak = std::max(peak, state.position.y);
        ++ticks;
    }
    const MoveParams params;
    CHECK(state.grounded && near(state.position.y, 0));
    // v^2 / 2g, plus the first tick's rise before gravity applies (about v dt / 2).
    CHECK(near(peak, params.jump_speed * params.jump_speed / (2 * params.gravity), 0.12));
    CHECK(ticks > 30 && ticks < 40);  // about 1.2 s in the air

    // Running off a ledge falls instead of snapping down.
    const Heightfield ledge = make_ground([](double, double gz) { return gz < 50 ? 5.0 : 0.0; });
    start.position = {-50, 5, 48};
    MoveInput off;
    off.forward = 127;
    off.yaw = float(std::numbers::pi);  // facing +Z
    state = run(start, off, ledge, 8);
    CHECK(!state.grounded && state.position.y > 0.5);
    state = run(state, off, ledge, 40);
    CHECK(state.grounded && near(state.position.y, 0));
}

// A square floor of half-size `half` centered at (cx, cz), at `height`.
void add_floor(BuildingCollision& b, double cx, double cz, double half, double height) {
    const Vec3 p0{cx - half, height, cz - half}, p1{cx + half, height, cz - half};
    const Vec3 p2{cx + half, height, cz + half}, p3{cx - half, height, cz + half};
    b.floors.push_back({p0, p1, p2, 0});
    b.floors.push_back({p0, p2, p3, 0});
}

void test_buildings() {
    const Heightfield flat = make_ground([](double, double) { return 0.0; });
    const MoveParams params;
    MoveInput east;  // facing +X
    east.forward = 127;
    east.yaw = float(-std::numbers::pi / 2);

    // A low platform is stepped onto.
    BuildingCollision low;
    add_floor(low, -40, 50, 5, 0.4);
    World low_world(flat);
    low_world.add_building(low, {});
    MoveState start;
    start.position = {-50, 0, 50};
    MoveState state = run(start, east, low_world, 30);  // ~5 m east
    CHECK(state.position.x > -46 && near(state.position.y, 0.4) && state.grounded);
    // Running off its far side drops back to the terrain.
    state = run(state, east, low_world, 60);
    CHECK(state.position.x > -34 && near(state.position.y, 0) && state.grounded);

    // A wall stops the body `radius` short of it, and running at it at an
    // angle slides along it.
    BuildingCollision walled;
    walled.walls.push_back({{-45, 0, 40}, {-45, 0, 60}, 3});
    World walled_world(flat);
    walled_world.add_building(walled, {});
    state = run(start, east, walled_world, 60);
    CHECK(near(state.position.x, -45 - params.radius, 1e-6) && near(state.position.z, 50));
    MoveInput northeast = east;
    northeast.yaw = float(-std::numbers::pi / 4);  // between +X and -Z
    state = run(start, northeast, walled_world, 60);
    CHECK(state.position.x <= -45 - params.radius + 1e-9 && state.position.z < 50 - 5);
    // Sprinting at it for a long time never tunnels through.
    MoveInput sprint = east;
    sprint.buttons = MoveInput::sprint;
    state = run(start, sprint, walled_world, 120);
    CHECK(state.position.x < -45);
    // A wall rising from a floor higher than the body can reach doesn't block.
    BuildingCollision overhead;
    overhead.walls.push_back({{-45, 1.5, 40}, {-45, 1.5, 60}, 3});
    World overhead_world(flat);
    overhead_world.add_building(overhead, {});
    CHECK(run(start, east, overhead_world, 60).position.x > -45);

    // Jumping up onto a 1 m platform.
    BuildingCollision high;
    add_floor(high, -40, 50, 6, 1.0);  // x -46..-34
    World high_world(flat);
    high_world.add_building(high, {});
    state = run(start, east, high_world, 20);  // to x ~ -45.6: under its edge, too high to step up
    CHECK(near(state.position.y, 0));
    MoveInput jump = east;
    jump.buttons = MoveInput::jump;
    state = run(state, jump, high_world, 1);
    state = run(state, east, high_world, 35);
    CHECK(state.grounded && near(state.position.y, 1.0) && state.position.x > -46);

    // Placement: Galaxies position and yaw, as in the world snapshot. A floor
    // 1 m along building +X, turned 90 degrees, lands 1 m toward game +Z.
    const Placement placed = placement_from_galaxies(10, 5, 20, 90);
    CHECK(placed.position == (Vec3{-10, 5, 20}) && near(placed.yaw, -std::numbers::pi / 2));
    BuildingCollision pad;
    add_floor(pad, 1, 0, 0.25, 0);
    const Heightfield deep = make_ground([](double, double) { return -100.0; });
    World placed_world(deep);
    placed_world.add_building(pad, placed);
    const World::Ground on = placed_world.ground_at(-10, 21, 5, 0.5);
    CHECK(on.on_floor && near(on.height, 5));
    CHECK(!placed_world.ground_at(-9, 20, 5, 0.5).on_floor);

    // The collision file round-trips.
    walled.floors = high.floors;
    const auto path = std::filesystem::temp_directory_path() / "swm_movement_test.collision";
    save_building_collision(walled, path);
    const BuildingCollision loaded = load_building_collision(path);
    std::filesystem::remove(path);
    CHECK(loaded.floors.size() == 2 && loaded.walls.size() == 1 && loaded.floors[1].c == walled.floors[1].c &&
          loaded.walls[0].b == walled.walls[0].b && near(loaded.walls[0].height, 3));
}

void test_determinism() {
    const Heightfield hills = make_ground([](double gx, double gz) { return 3 * std::sin(gx * 0.2) * std::cos(gz * 0.15); });
    MoveState a, b;
    a.position = b.position = {-50, hills.height_at(-50, 50), 50};
    for (uint32_t i = 1; i <= 300; ++i) {
        MoveInput input;
        input.sequence = i;
        input.forward = int8_t(i * 37 % 255 - 127);
        input.right = int8_t(i * 91 % 255 - 127);
        input.yaw = float(i * 0.05);
        input.buttons = uint8_t(i % 23 == 0 ? MoveInput::jump : 0);
        a = simulate_move(a, input, World(hills), MoveParams{}, dt);
        b = simulate_move(b, input, World(hills), MoveParams{}, dt);
    }
    CHECK(a == b);
}

} // namespace

int main() {
    test_heightfield();
    test_running();
    test_slopes_and_jumps();
    test_buildings();
    test_determinism();
    return swm::test::finish("movement_test");
}
