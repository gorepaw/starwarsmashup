#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "swm/core/movement.h"
#include "swm/core/vec3.h"

namespace swm::core {

class World;

// A weapon and the bolt it fires, from a Battlefront weapon class and its
// ordnance (`bf2tool export-soldier`). Defaults are the Imperial E-11's.
struct WeaponDef {
    std::string name = "imp_weap_inf_rifle";
    double shot_delay = 0.166;       // ShotDelay, seconds between shots
    int rounds_per_clip = 50;        // RoundsPerClip; 0: no clip (heat only)
    double reload_time = 1.5;        // ReloadTime
    double heat_per_shot = 0;        // HeatPerShot; heat runs 0..1 and overheats at 1
    double heat_recover_rate = 0;    // HeatRecoverRate, per second
    double heat_threshold = 0;       // HeatThreshold: after overheating, fires again at or below this
    double yaw_spread = 0.225;       // YawSpread, degrees
    double pitch_spread = 0.225;     // PitchSpread, degrees
    double spread_per_shot = 0.75;   // SpreadPerShot, degrees of bloom per shot (ASSUMPTION: added to both)
    double spread_recover_rate = 2;  // SpreadRecoverRate, degrees per second
    double spread_limit = 1.25;      // SpreadLimit, most bloom (ASSUMPTION)
    int shots_per_salvo = 1;         // ShotsPerSalvo: pellets per shot, each spread on its own and using a round
    double damage = 60;              // ordnance MaxDamage times PersonScale
    double bolt_speed = 300;         // ordnance Velocity, m/s
    double bolt_gravity = 1;         // ordnance Gravity, a multiple of world gravity
    double bolt_life = 0.8;          // ordnance LifeSpan, seconds
    // Looks only (clients draw bolts however their settings allow).
    std::array<double, 4> bolt_color{253 / 255.0, 43 / 255.0, 43 / 255.0, 100 / 255.0};  // LaserGlowColor
    double bolt_length = 1.0;        // LaserLength, m
    double bolt_width = 0.1;         // LaserWidth, m
};

// A soldier class: health, movement and weapons. Defaults are Battlefront's
// com_inf_default with an E-11.
struct SoldierDef {
    std::string name = "imp_inf_rifleman";
    double max_health = 400;  // MaxHealth
    MoveParams move;
    std::vector<WeaponDef> weapons{WeaponDef{}};
};

// Reads a soldier file written by `bf2tool export-soldier`. Throws
// std::runtime_error if it can't be read.
SoldierDef load_soldier(const std::filesystem::path& path);

// Where shots start: the character's eyes, above its feet.
inline constexpr double eye_height = 1.6;
// The body bolts hit: an upright cylinder from the feet.
inline constexpr double hit_radius = 0.35;
inline constexpr double hit_height = 1.8;
// Weapon slots a character carries.
inline constexpr int max_weapons = 2;
// Seconds to bring up the other weapon.
inline constexpr double weapon_switch_time = 0.4;

// A shot leaving a weapon this tick.
struct Shot {
    Vec3 origin;
    std::vector<Vec3> directions;  // unit; one per pellet (ShotsPerSalvo)
    int weapon = 0;
};

// Advances a character's weapons by one tick: cooldown, reloading, heat,
// switching and firing. Like simulate_move, deterministic: spread comes from
// the input's sequence number, so the server and the client agree.
std::optional<Shot> simulate_weapons(MoveState& state, const MoveInput& input, const SoldierDef& soldier, double dt);

// A fresh character's weapons and energy: full clips, cold, rested.
void reset_loadout(MoveState& state, const SoldierDef& soldier);

// One tick of a character: movement, then weapons. A dead character stands
// still with its weapons idle.
std::optional<Shot> simulate_character(MoveState& state, const MoveInput& input, bool alive, const World& world,
                                       const SoldierDef& soldier, double dt);

// The unit aim direction for a yaw and pitch (radians; pitch up is positive).
Vec3 aim_direction(double yaw, double pitch);

// Where along the segment a..b (0..1) it first enters an upright cylinder
// standing at `feet`, if it does.
std::optional<double> segment_hits_cylinder(Vec3 a, Vec3 b, Vec3 feet, double radius, double height);

} // namespace swm::core
