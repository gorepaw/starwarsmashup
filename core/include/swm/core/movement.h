#pragma once

#include <cstdint>

#include "swm/core/vec3.h"

namespace swm::core {

class Heightfield;

// One fixed tick of player input, exactly as sent over the network. The
// client predicts with these same quantized values, so the server's result
// for an input matches the client's bit for bit.
struct MoveInput {
    enum Button : uint8_t {
        sprint = 1 << 0,
        jump = 1 << 1,
    };

    uint32_t sequence = 0;  // increases by one per tick
    int8_t forward = 0;     // -127..127, scaled to -1..1
    int8_t right = 0;
    float yaw = 0;          // radians; 0 faces -Z, positive turns left (Godot's rotation.y)
    uint8_t buttons = 0;

    bool held(Button button) const { return (buttons & button) != 0; }
    friend bool operator==(const MoveInput&, const MoveInput&) = default;
};

// Quantizes stick axes (-1..1) for MoveInput.
int8_t quantize_axis(double value);

struct MoveState {
    Vec3 position;
    Vec3 velocity;
    float yaw = 0;
    bool grounded = true;

    friend bool operator==(const MoveState&, const MoveState&) = default;
};

// Provisional Battlefront-style tuning. Will come from data files.
struct MoveParams {
    double run_speed = 5.5;        // m/s
    double sprint_speed = 8.0;     // m/s, forward only
    double ground_accel = 40.0;    // m/s^2 toward the wished velocity
    double air_accel = 4.0;
    double gravity = 9.8;
    double jump_speed = 4.6;       // m/s upward, about a 1.1 m jump
    double max_slope_degrees = 50; // steeper ground can't be walked up
    double snap_down = 0.6;        // m: grounded characters follow ground this far down per tick
};

// Advances a character by one tick of `dt` seconds on the terrain. Pure and
// deterministic: the server and every client run this same function.
MoveState simulate_move(const MoveState& state, const MoveInput& input, const Heightfield& ground,
                        const MoveParams& params, double dt);

} // namespace swm::core
