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
    next.pitch = input.pitch;

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

    // Stagger and melee block/parry timers.
    next.stagger = float(std::max(0.0, double(state.stagger) - dt));
    next.parry_window = float(std::max(0.0, double(state.parry_window) - dt));

    const bool wants_block = input.held(MoveInput::block) && !state.rolling() && next.stagger <= 0;
    const bool can_block = wants_block && (state.blocking ? state.energy > 0 : state.energy >= 8.0);

    // Sprinting (forward only) drains energy; blocking drains energy; otherwise
    // energy comes back, faster at rest.
    const bool wants_sprint = input.held(MoveInput::sprint) && f > 0 && next.stagger <= 0 && !can_block;
    next.sprinting = wants_sprint && state.energy > 0 &&
                     (state.sprinting || state.energy >= params.energy_min_sprint);
    if (can_block) {
        next.blocking = true;
        if (!state.blocking) {
            next.parry_window = 0.25f;
        }
        next.energy = std::max(0.0, state.energy - 8.0 * dt);
    } else {
        next.blocking = false;
        next.parry_window = 0;
        if (next.sprinting) {
            next.energy = std::max(0.0, state.energy - params.energy_drain_sprint * dt);
        } else {
            const double restore = (f == 0 && r == 0) ? params.energy_restore_idle : params.energy_restore;
            next.energy = std::min(params.max_energy, state.energy + restore * dt);
        }
    }
    // Crouch: a press standing still crouches or stands up; a press on the
    // move rolls that way, if there's the energy. Sprinting or jumping
    // stands up (a jump from a crouch only stands).
    const bool pressed = input.held(MoveInput::crouch) && !state.crouch_held;
    next.crouch_held = input.held(MoveInput::crouch);
    next.roll = float(std::max(0.0, double(state.roll) - dt));
    bool jump = input.held(MoveInput::jump) && !state.rolling() && next.stagger <= 0;
    if (pressed && state.grounded && !state.rolling() && next.stagger <= 0) {
        if (f * f + r * r > 0.09 && state.energy >= params.energy_cost_roll) {
            const Vec3 way = forward * f + right * r;
            next.roll = float(params.roll_time);
            next.roll_yaw = float(std::atan2(-way.x, -way.z));
            next.energy = std::max(0.0, next.energy - params.energy_cost_roll);
            next.crouched = false;
            next.blocking = false;
            next.parry_window = 0;
        } else {
            next.crouched = !state.crouched;
        }
    }
    if (next.crouched && (jump || next.sprinting)) {
        next.crouched = false;
        jump = false;
    }
    if (next.rolling()) {
        next.sprinting = false;
        next.blocking = false;
        next.parry_window = 0;
        jump = false;
    }

    double forward_speed = f > 0 ? params.run_speed : params.strafe_speed;
    double side_speed = params.strafe_speed;
    if (next.stagger > 0) {
        forward_speed *= 0.45;
        side_speed *= 0.45;
    } else if (next.blocking) {
        forward_speed *= 0.60;
        side_speed *= 0.60;
    } else if (next.sprinting) {
        forward_speed *= params.sprint_factor;
        side_speed *= params.sprint_strafe;
    } else if (next.crouched) {
        forward_speed *= f > 0 ? params.crouch_factor : params.crouch_strafe;
        side_speed *= params.crouch_strafe;
    }
    Vec3 target = forward * (f * forward_speed) + right * (r * side_speed);
    if (next.rolling()) {
        // Rolling: the way it started, slowing; no steering.
        const double left = next.roll / params.roll_time;
        const double speed = params.run_speed * params.roll_speed_factor * (1.0 / 3 + 2.0 / 3 * left);
        target = Vec3{-std::sin(double(next.roll_yaw)), 0, -std::cos(double(next.roll_yaw))} * speed;
    }

    if (state.grounded) {
        // A roll starts at full speed at once.
        next.velocity = next.rolling() && !state.rolling() ? target
                                                           : approach(state.velocity, target, params.ground_accel * dt);
        next.velocity.y = 0;
        if (jump) {
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
    // Under a floor too high to step onto and too low to duck under: not
    // into it (unless already under something about head height, inside a
    // root, say: then out).
    const double low = state.position.y + params.step_up, high = state.position.y + params.headroom;
    if ((position.x != state.position.x || position.z != state.position.z) &&
        world.floor_between(position.x, position.z, low, high) &&
        !world.floor_between(state.position.x, state.position.z, low,
                             state.position.y + params.height + params.step_up)) {
        position.x = state.position.x;
        position.z = state.position.z;
        next.velocity.x = next.velocity.z = 0;
    }
    // Over a seam narrower than a body (in Mygeeto's floor), on the floor
    // both sides of it.
    World::Ground ground = world.support_at(position.x, position.z, state.position.y, params.step_up);

    // Terrain too steep to walk up: stay put horizontally. (Building floors
    // are walkable by definition; their stairs are ramps.)
    const double min_normal_y = std::cos(params.max_slope_degrees * std::numbers::pi / 180);
    if (next.grounded && !ground.on_floor && ground.height > state.position.y && ground.normal.y < min_normal_y) {
        position.x = state.position.x;
        position.z = state.position.z;
        next.velocity.x = next.velocity.z = 0;
        ground = world.support_at(position.x, position.z, state.position.y, params.step_up);
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
