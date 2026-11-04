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

// What a weapon fires: a bolt (blasters; bullets too), a missile (rockets:
// accelerating), or a grenade (thrown or lobbed: bounces, and explodes on
// impact or when its fuse runs out).
enum class Projectile : uint8_t { bolt, missile, grenade };

// A weapon and what it fires, from a Battlefront weapon class, its ordnance
// and its explosion (`bf2tool export-soldier`). Defaults are the Imperial
// E-11's.
struct WeaponDef {
    std::string name = "imp_weap_inf_rifle";
    Projectile projectile = Projectile::bolt;
    int ammo = 0;                    // WeaponAmmo: clips carried, the first loaded; 0: unlimited
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
    double bolt_acceleration = 0;    // ordnance Acceleration (missiles), m/s^2 along its flight
    double rebound = 0;              // ordnance Rebound: the share of its speed a grenade keeps bouncing
    bool explode_on_impact = true;   // grenades: ExplosionImpact (else they explode when the fuse runs out)
    double fuse = 0;                 // grenades: seconds from the throw to exploding (ASSUMPTION, see bf2tool)
    double shot_elevate = 0;         // ShotElevate: radians a throw is aimed above the crosshair
    double explosion_damage = 0;     // explosion Damage: full within the inner radius...
    double explosion_inner = 0;      // DamageRadiusInner, m
    double explosion_outer = 0;      // DamageRadiusOuter, m: ...falling to nothing here (ASSUMPTION: linearly)
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
    std::vector<WeaponDef> weapons{WeaponDef{}};  // primary slots
    std::optional<WeaponDef> secondary;           // grenades (Battlefront's weapon channel 1)
};

// Reads a soldier file written by `bf2tool export-soldier`. Throws
// std::runtime_error if it can't be read.
SoldierDef load_soldier(const std::filesystem::path& path);

// Where shots start: the character's eyes, above its feet.
inline constexpr double eye_height = 1.6;
// The body bolts hit: an upright cylinder from the feet.
inline constexpr double hit_radius = 0.35;
inline constexpr double hit_height = 1.8;
// Primary weapon slots a character carries; the secondary is slot 2.
inline constexpr int max_weapons = 2;
inline constexpr int secondary_slot = 2;

// The weapon in a slot (0, 1: primary; secondary_slot), if the soldier has one.
const WeaponDef* weapon_in_slot(const SoldierDef& soldier, int slot);
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

// The lower pitch that lands a projectile (initial speed, gravity in m/s^2)
// `dx` away horizontally and `dy` higher; nullopt if it can't reach.
std::optional<double> ballistic_pitch(double speed, double gravity, double dx, double dy);

// Where along the segment a..b (0..1) it first enters an upright cylinder
// standing at `feet`, if it does.
std::optional<double> segment_hits_cylinder(Vec3 a, Vec3 b, Vec3 feet, double radius, double height);

// An explosion's damage to a body standing at `feet`: full within the inner
// radius of the body, falling linearly to nothing at the outer radius.
double explosion_damage(const WeaponDef& weapon, Vec3 center, Vec3 feet);

} // namespace swm::core
