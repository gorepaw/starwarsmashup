#pragma once

#include <array>
#include <cstdint>

#include "swm/core/vec3.h"

namespace swm::core {

class World;

// One fixed tick of player input, exactly as sent over the network. The
// client predicts with these same quantized values, so the server's result
// for an input matches the client's bit for bit.
struct MoveInput {
    enum Button : uint8_t {
        sprint = 1 << 0,
        jump = 1 << 1,
        fire = 1 << 2,
        reload = 1 << 3,
        secondary = 1 << 4,  // the secondary weapon: grenades
    };

    uint32_t sequence = 0;  // increases by one per tick
    int8_t forward = 0;     // -127..127, scaled to -1..1
    int8_t right = 0;
    float yaw = 0;          // radians; 0 faces -Z, positive turns left (Godot's rotation.y)
    float pitch = 0;        // radians; aim up is positive
    uint8_t buttons = 0;
    uint8_t weapon = 0;     // the weapon slot wanted
    // The server tick the client was showing other characters at when it
    // made this input (they're drawn in the past). The server rewinds them to
    // it to judge this input's shots: what the shooter saw is what it hits.
    double view_tick = 0;

    bool held(Button button) const { return (buttons & button) != 0; }
    friend bool operator==(const MoveInput&, const MoveInput&) = default;
};

// Quantizes stick axes (-1..1) for MoveInput.
int8_t quantize_axis(double value);

struct WeaponState {
    int16_t rounds = 0;    // left in the clip
    int16_t reserve = -1;  // rounds carried beyond the clip; -1: unlimited
    float heat = 0;        // 0..1
    bool overheated = false;

    friend bool operator==(const WeaponState&, const WeaponState&) = default;
};

struct CombatState {
    uint8_t weapon = 0;   // primary slot in hand
    double cooldown = 0;  // seconds until it can fire
    double reload = 0;    // seconds of reloading left
    double spread = 0;    // degrees of bloom from recent shots
    std::array<WeaponState, 3> weapons{};  // two primary slots, then the secondary

    friend bool operator==(const CombatState&, const CombatState&) = default;
};

// Everything about a character that its own inputs decide, so the client
// can predict all of it.
struct MoveState {
    Vec3 position;
    Vec3 velocity;
    float yaw = 0;
    bool grounded = true;
    double energy = 100;  // spent by sprinting
    bool sprinting = false;
    CombatState combat;

    friend bool operator==(const MoveState&, const MoveState&) = default;
};

// Movement tuning. Defaults are Battlefront II's com_inf_default soldier;
// load_soldier() reads them from the player's own files.
struct MoveParams {
    double run_speed = 7.0;         // MaxSpeed, m/s forward
    double strafe_speed = 5.25;     // MaxStrafeSpeed, m/s sideways and back
    double sprint_factor = 1.5;     // ControlSpeed sprint: forward speed times this
    double sprint_strafe = 0.5;     // ...and sideways speed times this
    double ground_accel = 70.0;     // Acceleration, m/s^2 toward the wished velocity
    double air_accel = 4.0;
    double gravity = 9.8;
    double jump_speed = 5.9;        // m/s upward: sqrt(2 g JumpHeight) for JumpHeight 1.78
    double max_energy = 100;
    double energy_restore = 10;     // EnergyRestore, per second while moving
    double energy_restore_idle = 15; // EnergyRestoreIdle, per second standing still
    double energy_drain_sprint = 20; // EnergyDrainSprint, per second
    double energy_min_sprint = 20;  // EnergyMinSprint: needed to start sprinting
    double max_slope_degrees = 50;  // steeper ground can't be walked up
    double snap_down = 0.6;         // m: grounded characters follow ground this far down per tick
    double step_up = 0.5;           // m: highest ledge walked up without jumping
    double radius = 0.3;            // m: body, for walls
    double height = 1.8;            // m
};

// Advances a character by one tick of `dt` seconds through the world
// (terrain, building floors and walls). Pure and deterministic: the server
// and every client run this same function.
MoveState simulate_move(const MoveState& state, const MoveInput& input, const World& world, const MoveParams& params,
                        double dt);

} // namespace swm::core
