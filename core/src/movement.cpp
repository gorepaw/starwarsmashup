#include "swm/core/movement.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "swm/core/world.h"

namespace swm::core {

int8_t quantize_axis(double value) {
    return int8_t(std::lround(std::clamp(value, -1.0, 1.0) * 127));
}

namespace {

// Moves horizontal velocity toward a target by at most `step`.
Vec3 approach(Vec3 velocity, Vec3 target, double step) {
    const Vec3 delta{target.x - velocity.x, 0, target.z - velocity.z};
    const double distance = delta.length();
    if (distance <= step || distance == 0) {
        return {target.x, velocity.y, target.z};
    }
    return {velocity.x + delta.x * (step / distance), velocity.y, velocity.z + delta.z * (step / distance)};
}

} // namespace

MoveState simulate_move(const MoveState& state, const MoveInput& input, const World& world, const MoveParams& params,
                        double dt) {
    MoveState next = state;
    next.yaw = input.yaw;

    // Wished direction in the input's frame: forward is -Z rotated by yaw.
    const double s = std::sin(double(input.yaw));
    const double c = std::cos(double(input.yaw));
    const Vec3 forward{-s, 0, -c};
    const Vec3 right{c, 0, -s};
    double f = input.forward / 127.0;
    double r = input.right / 127.0;
    if (const double stick = std::hypot(f, r); stick > 1) {
        f /= stick;
        r /= stick;
    }

    // Sprinting (forward only) drains energy; it takes some energy to start,
    // and stops when it runs out. Otherwise energy comes back, faster at rest.
    const bool wants_sprint = input.held(MoveInput::sprint) && f > 0;
    next.sprinting = wants_sprint && state.energy > 0 &&
                     (state.sprinting || state.energy >= params.energy_min_sprint);
    if (next.sprinting) {
        next.energy = std::max(0.0, state.energy - params.energy_drain_sprint * dt);
    } else {
        const double restore = (f == 0 && r == 0) ? params.energy_restore_idle : params.energy_restore;
        next.energy = std::min(params.max_energy, state.energy + restore * dt);
    }
    double forward_speed = f > 0 ? params.run_speed : params.strafe_speed;
    double side_speed = params.strafe_speed;
    if (next.sprinting) {
        forward_speed *= params.sprint_factor;
        side_speed *= params.sprint_strafe;
    }
    const Vec3 target = forward * (f * forward_speed) + right * (r * side_speed);

    if (state.grounded) {
        next.velocity = approach(state.velocity, target, params.ground_accel * dt);
        next.velocity.y = 0;
        if (input.held(MoveInput::jump)) {
            next.velocity.y = params.jump_speed;
            next.grounded = false;
        }
    } else {
        next.velocity = approach(state.velocity, target, params.air_accel * dt);
        next.velocity.y -= params.gravity * dt;
    }

    Vec3 position = world.clamp(state.position + next.velocity * dt);
    position = world.resolve_walls(state.position, position, params.radius, params.height, params.step_up,
                                   next.velocity);
    World::Ground ground = world.ground_at(position.x, position.z, state.position.y, params.step_up);

    // Terrain too steep to walk up: stay put horizontally. (Building floors
    // are walkable by definition; their stairs are ramps.)
    const double min_normal_y = std::cos(params.max_slope_degrees * std::numbers::pi / 180);
    if (next.grounded && !ground.on_floor && ground.height > state.position.y && ground.normal.y < min_normal_y) {
        position.x = state.position.x;
        position.z = state.position.z;
        next.velocity.x = next.velocity.z = 0;
        ground = world.ground_at(position.x, position.z, state.position.y, params.step_up);
    }
    const double floor = ground.height;

    if (next.grounded) {
        if (position.y - floor <= params.snap_down) {
            position.y = floor;  // follow the ground down slopes
        } else {
            next.grounded = false;  // ran off a ledge
        }
    }
    if (position.y <= floor) {
        position.y = floor;
        if (next.velocity.y <= 0) {
            next.velocity.y = 0;
            next.grounded = true;
        }
    }
    next.position = position;
    return next;
}

} // namespace swm::core
