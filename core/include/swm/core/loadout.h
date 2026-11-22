#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "swm/core/movement.h"

namespace swm::core {

// What a hit does, for mitigation: armor can resist each type differently.
enum class DamageType : uint8_t { ballistic, laser, electro, bio, explosive, melee };
inline constexpr size_t damage_type_count = 6;

// A character statistic that gear can change. Data files name them as below
// (e.g. "run_speed", "mitigation_laser").
enum class Stat : uint8_t {
    max_health,
    run_speed,
    strafe_speed,
    sprint_factor,
    sprint_strafe,
    jump_height,
    acceleration,
    max_energy,
    energy_restore,
    energy_restore_idle,
    energy_drain_sprint,
    mitigation,  // share of all damage absorbed
    mitigation_ballistic,
    mitigation_laser,
    mitigation_electro,
    mitigation_bio,
    mitigation_explosive,
    mitigation_melee,
};

std::optional<Stat> stat_from_name(std::string_view name);
std::optional<DamageType> damage_type_from_name(std::string_view name);

// A change gear makes to a statistic: added, then multiplied. Written in
// data files as "Modifier = run_speed * 0.9" or "Modifier = mitigation_laser + 0.15".
struct StatModifier {
    Stat stat = Stat::max_health;
    double add = 0;
    double multiply = 1;
};

// A character's statistics with everything carried applied. Defaults are
// Battlefront II's com_inf_default soldier, the base every character starts
// from.
struct CharacterStats {
    double max_health = 400;  // MaxHealth
    MoveParams move;
    double mitigation = 0;  // 0..max_mitigation: share of all damage absorbed
    std::array<double, damage_type_count> mitigation_by_type{};  // then this share of each type
};

// The most of any damage mitigation can absorb.
inline constexpr double max_mitigation = 0.9;

// Damage after the target's mitigation, general and by type.
double mitigated_damage(const CharacterStats& stats, double amount, DamageType type);

// A weapon: how it fires, what it fires (a Battlefront weapon class, its
// ordnance and its explosion, via `bf2tool export-loadout`), and the stats it
// changes while carried. Defaults are the Imperial E-11's.
enum class Projectile : uint8_t { bolt, missile, grenade };

struct WeaponDef {
    std::string name = "imp_weap_inf_rifle";
    DamageType damage_type = DamageType::laser;  // of a direct hit; explosions are explosive
    // What it fires: a bolt (blasters; bullets too), a missile (rockets:
    // accelerating), or a grenade (thrown or lobbed: bounces, and explodes on
    // impact or when its fuse runs out).
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
    // Aiming down it: magnification from ZoomMin to ZoomMax (the E-11's 2.5;
    // a sniper rifle's 2 to 8, stepped), through its scope in first person
    // if ZoomFirstPerson. 1: no zoom. The client's alone (how it's drawn).
    double zoom_min = 1;
    double zoom_max = 1;
    bool zoom_first_person = false;
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
    // Looks only (clients draw bolts, models and effects however their settings allow).
    std::array<double, 4> bolt_color{253 / 255.0, 43 / 255.0, 43 / 255.0, 100 / 255.0};  // LaserGlowColor
    double bolt_length = 1.0;        // LaserLength, m
    double bolt_width = 0.1;         // LaserWidth, m
    std::string bolt_texture = "com_sfx_laser_red";  // LaserTexture
    double glow_length = 3.0;        // GlowLength, m
    double blur_length = 5.0;        // BlurLength, m
    std::array<double, 4> light_color{253 / 255.0, 43 / 255.0, 43 / 255.0, 150 / 255.0};  // LightColor
    double light_radius = 4.0;       // LightRadius, m
    std::string ordnance_model;      // ordnance GeometryName (grenade, missile, mortar shell)
    std::string trail_effect;        // TrailEffect
    std::string impact_effect = "com_sfx_ord_exp";  // ImpactEffectSoft / Terrain
    std::string explosion_effect;    // explosion Effect
    std::string muzzle_flash = "small_muzzle_flash";  // MuzzleFlash
    std::array<double, 4> flash_color{1.0, 100 / 255.0, 100 / 255.0, 1.0};  // FlashColor
    double flash_length = 0.025;     // FlashLength, s
    std::array<double, 4> flash_light_color{1.0, 220 / 255.0, 220 / 255.0, 175 / 255.0};  // FlashLightColor
    double flash_light_radius = 2.0;    // FlashLightRadius, m
    double flash_light_duration = 0.25; // FlashLightDuration, s
    std::vector<StatModifier> modifiers;  // while carried
};

// A piece of gear that isn't a weapon: armor, mostly. `slot` says where it's
// worn (e.g. "chest"); a loadout carries one item per slot.
struct Gear {
    std::string name;
    std::string slot;
    std::vector<StatModifier> modifiers;
};

// Weapon slots. The primary and the sidearm are held (switched between);
// the secondary (throwables and gadgets) is used with its own button.
inline constexpr int primary_slot = 0;
inline constexpr int sidearm_slot = 1;
inline constexpr int secondary_slot = 2;
inline constexpr int hand_slots = 2;
inline constexpr int weapon_slots = 3;

// What a character brings into a fight: weapons and gear. There are no
// character classes: what a character can do, and how much damage it takes,
// comes from what it carries. The default is an E-11 alone.
struct Loadout {
    std::string name = "default";
    std::array<std::optional<WeaponDef>, weapon_slots> weapons{WeaponDef{}, std::nullopt, std::nullopt};
    std::vector<Gear> gear;
};

// The weapon in a slot, if the loadout has one.
const WeaponDef* weapon_in_slot(const Loadout& loadout, int slot);

// The base statistics with every weapon's and gear's modifiers applied:
// each statistic plus all additions, times all multipliers.
CharacterStats stats_for(const Loadout& loadout, const CharacterStats& base = {});

// Data files (INI). Each throws std::runtime_error if it can't be read or
// makes no sense.
//
// A weapon: a [weapon] section (written by `bf2tool export-loadout`).
WeaponDef load_weapon(const std::filesystem::path& path);
// A piece of gear: a [gear] section with Name, Slot and Modifier lines.
Gear load_gear(const std::filesystem::path& path);
// A loadout: a [loadout] section naming its items: Primary, Sidearm,
// Secondary and Gear (repeatable), each "<name>" for an item file
// "<name>.ini" found in the Items folders (default: ../items and
// ../imported_assets/items, relative to the loadout file).
Loadout load_loadout(const std::filesystem::path& path);

} // namespace swm::core
