#pragma once

#include <array>
#include <cstdint>

#include "swbfo/core/vec3.h"

namespace swbfo::core {

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
        // Pressed standing still: crouch or stand up. Pressed while moving:
        // roll that way (Battlefront's dive roll).
        crouch = 1 << 5,
        // Get into the vehicle at hand, or out of the one driven (a press).
        use = 1 << 6,
        // Melee block / timed parry.
        block = 1 << 7,
    };

    uint32_t sequence = 0;  // increases by one per tick
    int8_t forward = 0;     // -127..127, scaled to -1..1
    int8_t right = 0;
    float yaw = 0;          // radians; 0 faces -Z, positive turns left (Godot's rotation.y)
    float pitch = 0;        // radians; aim up is positive
    uint8_t buttons = 0;
    uint8_t weapon = 0;     // the weapon slot wanted
    bool aiming = false;    // aimed down its sights (zoomed: a perk's condition, Spotter)
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
    float reload = 0;      // holstered: seconds of reloading itself left (a perk's: WeaponDef::holster_reload)

    friend bool operator==(const WeaponState&, const WeaponState&) = default;
};

struct CombatState {
    uint8_t weapon = 0;   // held slot: primary or sidearm
    double cooldown = 0;  // seconds until it can fire
    double reload = 0;    // seconds of reloading left
    double spread = 0;    // degrees of bloom from recent shots
    float idle = 0;       // seconds since it last fired (a "paused" perk's trigger)
    std::array<WeaponState, 3> weapons{};  // primary, sidearm, secondary

    friend bool operator==(const CombatState&, const CombatState&) = default;
};

// The most triggered perks a loadout carries (docs/PERKS.md: a character
// knows ten at most), each with a timer in the character's state.
inline constexpr int max_perk_timers = 10;

// Everything about a character that its own inputs decide, so the client
// can predict all of it.
struct MoveState {
    Vec3 position;
    Vec3 velocity;
    float yaw = 0;
    float pitch = 0;      // the input's aim, for drawing (where the gun points)
    bool grounded = true;
    double energy = 100;  // spent by sprinting
    bool sprinting = false;
    bool crouched = false;
    bool crouch_held = false;  // the crouch button was down last tick (a press starts a crouch or roll)
    float roll = 0;            // seconds of rolling left
    float roll_yaw = 0;        // the way it rolls (as yaw); riding, where it aims its guns (their yaw)
    float slide = 0;           // seconds of sliding left (a perk's: MoveParams::slide_time)
    uint8_t rolls = 0;         // rolls in a row so far (a perk lets one follow another: roll_chain)
    bool blocking = false;
    float parry_window = 0;    // seconds of timed parry left
    float stagger = 0;         // seconds of melee stagger left
    // The vehicle it drives (an index into the battlefield's vehicle
    // classes), -1 on foot. Driving, the state is the vehicle's: its yaw is
    // its heading, its weapons and energy (boost) the vehicle's.
    int8_t vehicle = -1;
    // Riding in `vehicle` as its gunner: 1 for its first gunner's seat; 0
    // driving (or on foot). A gunner doesn't move itself: the server keeps
    // its place and yaw the vehicle's; its look turns the seat's guns.
    int8_t seat = 0;
    CombatState combat;
    // Triggered perks (docs/PERKS.md, Triggers): each of its loadout's
    // triggered perks' seconds left, active or cooling down (by its index
    // in Loadout::triggered); and how long it's been crouched without moving.
    std::array<float, max_perk_timers> perk_time{};
    float still = 0;

    bool rolling() const { return roll > 0; }

    friend bool operator==(const MoveState&, const MoveState&) = default;
};

// Movement tuning. Defaults are Battlefront II's com_inf_default soldier;
// gear changes them (see CharacterStats).
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
    // Crouched, speeds times these (ControlSpeed crouch: forward, sideways).
    double crouch_factor = 0.70;
    double crouch_strafe = 0.60;
    // Swimming (deep water: World::swim_floor_at), speeds times this, and no
    // sprinting, rolling, crouching or jumping. ASSUMPTION: about Galaxies'
    // swimming pace against its run.
    double swim_factor = 0.5;
    // A roll: it starts at run speed times RollSpeedFactor and slows to a
    // third of that over `roll_time`, the way it was going; steering is all
    // but gone (ControlSpeed roll 0.02), and it costs EnergyCostRoll.
    // ASSUMPTION: the roll's time and slowing (Battlefront's dive roll clip
    // is 35 frames).
    double roll_speed_factor = 1.5;
    double roll_time = 0.75;        // s
    double energy_cost_roll = 25;
    // Trainer perks' (docs/PERKS.md): crouching out of a sprint slides this
    // long instead of rolling (0: it rolls), keeping its speed, crouched, and
    // able to shoot; and this many rolls can follow a roll at once (in its
    // second half).
    double slide_time = 0;
    double roll_chain = 0;
    // A melee block: stamina a second held, times this; the timed parry
    // window, s (its held weapon's: WeaponDef::block_cost, parry_window).
    double block_cost = 1;
    double parry_window = 0.25;
    double max_slope_degrees = 55;  // steeper ground can't be walked up
    double snap_down = 0.6;         // m: grounded characters follow ground this far down per tick
    double step_up = 0.5;           // m: highest ledge walked up without jumping
    double radius = 0.3;            // m: body, for walls
    double height = 1.8;            // m
    // m: it walks under what's at least this high over its feet (a canopy
    // at head height); a floor lower than that and too high to step onto
    // (the top of a root lying on the ground) stops it, like a wall.
    double headroom = 1.5;
};

// Advances a character by one tick of `dt` seconds through the world
// (terrain, building floors and walls). Pure and deterministic: the server
// and every client run this same function.
MoveState simulate_move(const MoveState& state, const MoveInput& input, const World& world, const MoveParams& params,
                        double dt);

} // namespace swbfo::core
