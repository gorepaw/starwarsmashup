// Heightfield sampling and character movement on synthetic terrain.

#include <numbers>

#include "swm/core/heightfield.h"
#include "swm/core/movement.h"
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

MoveState run(MoveState state, MoveInput input, const Heightfield& ground, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        input.sequence++;
        state = simulate_move(state, input, ground, MoveParams{}, dt);
    }
    return state;
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
    CHECK(near(sprinted.velocity.x, params.sprint_speed, 1e-6) && sprinted.position.x > -50 + 7);

    // Strafing right while facing -Z moves toward +X at run speed.
    MoveInput strafe;
    strafe.right = 127;
    const MoveState strafed = run(start, strafe, flat, 30);
    CHECK(near(strafed.velocity.x, params.run_speed) && near(strafed.velocity.z, 0));

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
    CHECK(ticks > 20 && ticks < 35);  // about 0.94 s in the air

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
        a = simulate_move(a, input, hills, MoveParams{}, dt);
        b = simulate_move(b, input, hills, MoveParams{}, dt);
    }
    CHECK(a == b);
}

} // namespace

int main() {
    test_heightfield();
    test_running();
    test_slopes_and_jumps();
    test_determinism();
    return swm::test::finish("movement_test");
}
