#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "swm/core/loadout.h"
#include "swm/core/movement.h"
#include "swm/core/vec3.h"

namespace swm::core {

class World;

// Where shots start: the character's eyes, above its feet.
inline constexpr double eye_height = 1.6;
inline constexpr double crouched_eye_height = 1.1;  // Battlefront's crouch camera is half a meter lower
// Where shots leave from: the eyes, lower crouched.
inline double eye_height_of(const MoveState& state) {
    return state.crouched ? crouched_eye_height : eye_height;
}
// The body bolts hit: an upright cylinder from the feet.
inline constexpr double hit_radius = 0.35;
inline constexpr double hit_height = 1.8;
inline constexpr double crouched_hit_height = 1.3;  // ASSUMPTION: a crouched soldier's height
// A hit within this much of the top of a soldier is in the head, and does
// this many times the damage. ASSUMPTION: Battlefront's ODFs give no
// multiplier (its soldiers have a head collision box); doubled, a sniper
// rifle's 300 kills a 400-health soldier with a headshot and takes two to
// the body, as its own tip has it ("a well aimed shot from a sniper unit can
// prove to be quite deadly").
inline constexpr double head_size = 0.3;
inline constexpr double headshot_multiplier = 2.0;

// A bolt's damage after `flown` seconds in flight (its falloff, if any).
double bolt_damage(const WeaponDef& weapon, double flown);
// Crouched, a soldier's spread is divided by this (AimFactorPostureCrouch
// 140 against AimFactorPostureStand 100). ASSUMPTION: that's what the factor does.
inline constexpr double crouch_aim_factor = 1.4;
// Seconds to bring up the other held weapon.
inline constexpr double weapon_switch_time = 0.4;

// A shot leaving a weapon this tick.
struct Shot {
    Vec3 origin;
    std::vector<Vec3> directions;  // unit; one per pellet (ShotsPerSalvo)
    int weapon = 0;                // the slot
};

// Advances a character's weapons by one tick: cooldown, reloading, heat,
// switching between the held weapons, firing, and throwing the secondary.
// Like simulate_move, deterministic: spread comes from the input's sequence
// number, so the server and the client agree.
std::optional<Shot> simulate_weapons(MoveState& state, const MoveInput& input, const Loadout& loadout, double dt);

// A fresh character's weapons and energy: full clips and carried ammo, cold,
// rested, holding its primary (or its sidearm, without one).
void reset_loadout(MoveState& state, const Loadout& loadout, const CharacterStats& stats);

// One tick of a character: movement (with its statistics), then weapons. A
// dead character stands still with its weapons idle.
std::optional<Shot> simulate_character(MoveState& state, const MoveInput& input, bool alive, const World& world,
                                       const Loadout& loadout, const CharacterStats& stats, double dt);

// The unit aim direction for a yaw and pitch (radians; pitch up is positive).
Vec3 aim_direction(double yaw, double pitch);

// The lower pitch that lands a projectile (initial speed, gravity in m/s^2)
// `dx` away horizontally and `dy` higher; nullopt if it can't reach.
std::optional<double> ballistic_pitch(double speed, double gravity, double dx, double dy);

// Where along the segment a..b (0..1) it first enters an upright cylinder
// standing at `feet`, if it does.
std::optional<double> segment_hits_cylinder(Vec3 a, Vec3 b, Vec3 feet, double radius, double height);

// A droid's resupply: each weapon that carries limited ammo gets `fraction`
// of its full ammunition back (clip and spare clips together), up to full:
// a gun's as spare rounds, a secondary's (grenades) into its clip first.
// Returns whether anything changed.
bool resupply(MoveState& state, const Loadout& loadout, double fraction);

// A dropped ammo or dual pack's resupply: each weapon that carries limited
// ammo gets `clips` clips back (Battlefront's SoldierAmmo on a powerupitem),
// up to full. Returns whether anything changed.
bool resupply_clips(MoveState& state, const Loadout& loadout, int clips);

// An explosion's damage to a body standing at `feet`, before mitigation: full
// within the inner radius of the body, falling linearly to nothing at the
// outer radius.
double explosion_damage(const WeaponDef& weapon, Vec3 center, Vec3 feet);

} // namespace swm::core
