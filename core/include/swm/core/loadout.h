#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
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
    shield_capacity,
    shield_recharge,
    shield_delay,
    explosion_push_resist,
    healing_received,   // times every heal taken (1: as given)
    ability_cooldown,   // times every ability's cooldown (below 1: sooner)
    effect_resistance,  // share of a hostile effect's (a debuff's) time shrugged off
    roll_cost,          // the stamina a roll takes
    // Trainer perks' (docs/PERKS.md):
    ammo_capacity,      // times the ammunition carried
    self_blast,         // times the damage of its own blasts
    stagger_resist,     // share of a stagger (and a shoulder charge's) shrugged off; 1: immune
    slow_resist,        // share of a hostile slow shrugged off; 1: immune
    blast_shield,       // share of explosive damage taken that refills the shield
    shield_overcharge,  // a full recharge goes this share over capacity, and the extra drains away
    notice,             // times how far creatures and outlaws notice it from
    slide_time,         // s: crouching out of a sprint slides this long instead of rolling (0: rolls)
    roll_chain,         // rolls that can follow a roll at once
    charge,             // s: sprinting into an enemy staggers them this long (0: doesn't)
    radar_quiet,        // over 0: its shots don't show it on enemies' radar
    // An Entertainer's progression Inspirations' (docs/PROFESSIONS.md):
    xp_gain,            // times the profession experience earned
    faction_gain,       // times the faction points earned
};
inline constexpr Stat last_stat = Stat::faction_gain;

std::optional<Stat> stat_from_name(std::string_view name);
std::string_view stat_name(Stat stat);
std::optional<DamageType> damage_type_from_name(std::string_view name);
std::string_view damage_type_name(DamageType type);

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
    double shield_capacity = 0;
    double shield_recharge = 0;
    double shield_delay = 5.0;
    double explosion_push_resist = 0;
    double healing_received = 1;
    double ability_cooldown = 1;
    double effect_resistance = 0;  // 0..max_mitigation
    // Trainer perks' (Stat's comments).
    double ammo_capacity = 1;
    double self_blast = 1;
    double stagger_resist = 0;  // 0..1
    double slow_resist = 0;     // 0..1
    double blast_shield = 0;
    double shield_overcharge = 0;
    double notice = 1;
    double charge = 0;
    double radar_quiet = 0;
    double xp_gain = 1;
    double faction_gain = 1;
};

// The most of any damage mitigation can absorb.
inline constexpr double max_mitigation = 0.9;
// The most ammunition a character can carry, times the standard (ammo_capacity).
inline constexpr double max_ammo_capacity = 2.0;

// Damage after the target's mitigation, general and by type.
double mitigated_damage(const CharacterStats& stats, double amount, DamageType type);

// A weapon: how it fires, what it fires (a Battlefront weapon class, its
// ordnance and its explosion, via `bf2tool export-loadout`), and the stats it
// changes while carried. Defaults are the Imperial E-11's. A `heal` fires
// nothing: used from the tactical slot, it heals its user (a medpack).
enum class Projectile : uint8_t { bolt, missile, grenade, melee, heal };

// For a Battlefront weapon (by its class name), the Galaxies weapon of the
// same kind soldiers hold for it, game/imported_assets/-relative ("" none):
// pistols the DL-44 (the Empire's the D18), rifles a faction's carbine (the
// Empire's E-11, the Rebellion's DH-17, the Republic's EE-3, the
// Separatists' laser carbine), sniper rifles the DLT-20A, shotguns and
// blunderbusses the SG-82, launchers the T-21 (Galaxies has no launchers),
// bowcasters the bowcaster. And its animation set by its kind.
std::string default_weapon_model(std::string_view name);
std::string default_anim_set(std::string_view name, bool missile);

// What a hit leaves on its target, besides its damage: burning, chilled,
// ionized (an ammo's, mostly; game/items/swg_catalog.ini's [hit_effect]s).
// A character gets it as a debuff (a TimedEffect: the same id refreshes,
// never stacks), its time shortened by its effect_resistance; an NPC, as a
// snare (its speed: the run_speed multiplier) and damage over time.
struct HitEffect {
    std::string id;
    std::string name;
    double chance = 1;             // of each hit (each pellet)
    double duration = 0;           // seconds
    double damage_per_second = 0;
    DamageType damage_type = DamageType::laser;
    std::vector<StatModifier> modifiers;  // while it lasts
};

enum class AmmoClass : uint8_t { none, standard, heavy };
inline constexpr int max_standard_ammo = 999;
inline constexpr int max_heavy_ammo = 99;

inline constexpr int max_ammo_for(AmmoClass ac) {
    switch (ac) {
    case AmmoClass::standard: return max_standard_ammo;
    case AmmoClass::heavy: return max_heavy_ammo;
    case AmmoClass::none: return 0;
    }
    return 0;
}

inline constexpr std::string_view ammo_class_name(AmmoClass ac) {
    switch (ac) {
    case AmmoClass::none: return "none";
    case AmmoClass::standard: return "standard";
    case AmmoClass::heavy: return "heavy";
    }
    return "none";
}

inline std::optional<AmmoClass> ammo_class_from_name(std::string_view name) {
    if (name == "none") return AmmoClass::none;
    if (name == "standard") return AmmoClass::standard;
    if (name == "heavy") return AmmoClass::heavy;
    return std::nullopt;
}

struct WeaponDef {
    std::string name = "imp_weap_inf_rifle";
    // What soldiers hold: its model (game/imported_assets/-relative glTF:
    // a Galaxies weapon, as an item names it; for Battlefront's weapons, one
    // of Galaxies' of the same kind, default_weapon_model), and how they
    // hold it: Battlefront's animation set ("rifle", "pistol", "bazooka").
    std::string model;
    std::string anim_set = "rifle";
    // An item's weapon archetype (WeaponArchetypeInfo::id: "pistol",
    // "rifle_auto", ...), which says what profession it levels; empty for
    // Battlefront's own weapons.
    std::string archetype;
    DamageType damage_type = DamageType::laser;  // of a direct hit; explosions are explosive
    // What it fires: a bolt (blasters; bullets too), a missile (rockets:
    // accelerating), or a grenade (thrown or lobbed: bounces, and explodes on
    // impact or when its fuse runs out).
    Projectile projectile = Projectile::bolt;
    AmmoClass ammo_class = AmmoClass::none;
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
    // Against vehicles: ordnance MaxDamage times VehicleScale, as a factor on
    // `damage` (VehicleScale / PersonScale), and the explosion's likewise.
    // ASSUMPTION: a weapon that names no VehicleScale does full damage (1).
    double vehicle_factor = 1;
    double explosion_vehicle_factor = 1;
    double bolt_speed = 300;         // ordnance Velocity, m/s
    double bolt_gravity = 1;         // ordnance Gravity, a multiple of world gravity
    double bolt_life = 0.8;          // ordnance LifeSpan, seconds
    // Damage falling off with flight time: after DamageTransitionDelay
    // seconds it falls, over DamageTransitionPeriod, to DamageFinalDamage
    // (times PersonScale). The E-11's: 60 to 40 after 0.4 s (some 120 m);
    // a sniper rifle's doesn't (final < 0).
    double damage_transition_delay = 0;
    double damage_transition_period = 0;
    double damage_final = -1;
    double bolt_acceleration = 0;    // ordnance Acceleration (missiles), m/s^2 along its flight
    double rebound = 0;              // ordnance Rebound: the share of its speed into a surface a grenade bounces back with
    double friction = 0;             // ordnance Friction: how much of its speed along a surface it loses bouncing
    bool explode_on_impact = true;   // grenades: ExplosionImpact (else they explode when the fuse runs out)
    // Grenades: seconds to exploding, from the first thing they touch if
    // `timer_on_contact` (StartTimerOnContact: the thermal detonator's
    // LifeSpan, 0.6 s after it lands), else from the throw.
    double fuse = 0;
    bool timer_on_contact = false;
    // A placed mine or trap (the `placed` archetype; Battlefront's landmine):
    // once it comes to rest and `arm_time` has passed, it goes off when an
    // enemy comes within `trigger_radius` (its explosion), or, a trap with
    // `trap_shots`, fires that many bolts of `damage` at them,
    // `trap_interval` apart. `fuse` is how long it lies there; then it's
    // gone, harmlessly. 0: not placed.
    double trigger_radius = 0;
    double arm_time = 0;
    int trap_shots = 0;
    double trap_interval = 0;
    // A medpack (Projectile::heal): the health it gives its user, at once,
    // or spread over `heal_time` seconds if that's above 0.
    double heal = 0;
    double heal_time = 0;
    double shot_elevate = 0;         // ShotElevate: radians a throw is aimed above the crosshair
    double explosion_damage = 0;     // explosion Damage: full within the inner radius...
    double explosion_inner = 0;      // DamageRadiusInner, m
    double explosion_outer = 0;      // DamageRadiusOuter, m: ...falling to nothing here (ASSUMPTION: linearly)
    // Explosion Push: soldiers thrown away from it at this many m/s within
    // PushRadiusInner, falling to nothing at PushRadiusOuter. ASSUMPTION:
    // Push is a speed. The thermal detonator's: 10 within 6.5 m.
    double explosion_push = 0;
    double push_inner = 0;
    double push_outer = 0;
    double melee_range = 2.6;
    double melee_arc = 70.0;
    double melee_lunge = 5.0;
    double melee_stagger = 0.35;
    double shield_disrupt = 2.5;
    // A hit in the head does this many times the damage (combat.h's
    // headshot_multiplier unless the weapon says).
    double headshot_multiplier = 2.0;
    // A hit (each pellet) is critical with this chance, doing this many times
    // the damage (after a headshot's). 0: never.
    double crit_chance = 0;
    double crit_multiplier = 1.5;
    // Damage to a Deflector Shield (PSG) times this (vehicles: vehicle_factor).
    double shield_factor = 1;
    // Seconds to bring it up from the other held weapon (combat.h's weapon_switch_time).
    double swap_time = 0.4;
    // Degrees a shot kicks the shooter's aim up (a player's camera; 0: none).
    double recoil = 0;
    // How fast it's brought up to aim down (zoomed): times the standard.
    double aim_speed = 1;
    // Trainer perks' (docs/PERKS.md). A hit anywhere but the head does
    // `body_multiplier` times the damage, and from behind its target
    // `flank_multiplier` times. Holstered, its heat drains `holster_cooling`
    // times as fast, and with `holster_reload` its clip reloads itself (over
    // its reload time). `reload_sprinting`: a reload carries on while
    // sprinting (else it waits). `fire_rolling`: it fires (or swings) while
    // rolling. Its bolts bounce off `ricochets` walls and go through `pierces`
    // bodies; a missile bursts passing within `proximity_fuse` m of an enemy.
    double body_multiplier = 1;
    double flank_multiplier = 1;
    double holster_cooling = 1;
    bool holster_reload = false;
    bool reload_sprinting = false;
    bool fire_rolling = false;
    int ricochets = 0;
    int pierces = 0;
    double proximity_fuse = 0;
    // Melee: a block lets `block_leak` of a blow through and costs
    // `block_cost` times the stamina; with `deflect`, it turns aside bolts
    // from the front for that much stamina each. The parry window is
    // `parry_window` times the standard, and after a parry with `riposte` the
    // next hit crits. The third hit in a row on one target staggers
    // `combo_stagger` times as long. Hits throw their target back at
    // `knockback` m/s, and `unblockable` ones go through a block.
    double block_leak = 0.45;
    double block_cost = 1;
    double deflect = 0;
    double parry_window = 1;
    bool riposte = false;
    double combo_stagger = 1;
    double knockback = 0;
    bool unblockable = false;
    // What its hits leave on their target, if anything (its ammo's).
    std::optional<HitEffect> hit_effect;
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
    std::string fire_sound = "imp_weap_inf_rifle_fire";
    std::string fire_empty_sound = "com_weap_inf_ammo_empty";
    std::string reload_sound = "com_weap_inf_reload_med";
    std::string overheat_sound;
    std::string overheat_stop_sound;
    std::string weapon_change_sound = "com_weap_inf_equip_med";
    std::string ordnance_sound;
    std::string bounce_sound;
    std::string explosion_sound;
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

// A handling change to a weapon (a profession's perk's), written
// "spread * 0.97": spread (the cone and its bloom per shot), bloom (only its
// growth per shot), heat (per shot), reload (its time), recovery (how fast
// bloom settles, larger is faster), range (how far bolts carry before their
// damage falls off), body (a hit not in the head), blast (an explosion's
// radii), blast_damage (its damage), effect (how long its hit effect lasts),
// carry (its movement penalties), holster_cool (heat drained holstered),
// guard (what a block lets through), block (a block's stamina), parry (the
// window), combo (the third hit's stagger), and the rest by their names. From
// ricochet on they're added, "ricochet + 1": ricochet and pierce (counts),
// proximity (m), deflect (stamina a bolt), knockback (m/s), and the rest are
// switches (+ 1): sprint_reload, roll_fire, holster_reload, riposte,
// unblockable (WeaponDef's fields of those names).
struct WeaponModifier {
    enum Field : uint8_t { spread, heat, reload, recovery, range, speed, lunge, disrupt,
                           velocity, headshot, crit, vehicle, shield, swap, recoil, aim, reach, bloom,
                           body, blast, blast_damage, effect, carry, holster_cool, flank, guard, block, parry, combo,
                           ricochet, pierce, proximity, deflect, knockback,
                           sprint_reload, roll_fire, holster_reload, riposte, unblockable };
    static constexpr Field last_field = unblockable;
    static constexpr bool additive(Field f) { return f >= ricochet; }
    Field field = spread;
    double multiply = 1;  // the multiplier, or for an additive field the amount added

    friend bool operator==(const WeaponModifier&, const WeaponModifier&) = default;
};
void apply_weapon_modifier(WeaponDef& weapon, const WeaponModifier& modifier);

// Triggered perks (docs/PERKS.md, Triggers): a trainer perk that works only
// on something. Stances the shared simulation sees (crouched, crouched and
// still, a pause in firing, rolling) hold while they last; events (a roll
// begun, an overheat; and on the server a kill, a hit, a shield broken, low
// health, a while unhurt, a killing blow, a hit of some type) start its
// timer or do something at once.
enum class PerkTrigger : uint8_t {
    crouched, crouched_still, paused, rolling,  // stances
    roll, overheat,                             // events the simulation sees
    kill, headshot_kill, hit, target_low_health, shield_broken, unhit, low_health, lethal, hit_by,  // the server's
};
std::optional<PerkTrigger> perk_trigger_from_name(std::string_view name);
std::string_view perk_trigger_name(PerkTrigger trigger);
bool is_stance(PerkTrigger trigger);

struct TriggeredPerk {
    std::string id;
    std::string name;  // as the trainer teaches it (the HUD names it when it fires)
    PerkTrigger trigger = PerkTrigger::kill;
    double after = 0;     // seconds: crouched_still, paused, unhit
    double below = 0;     // a share of health: low_health, target_low_health
    double within = 0;    // m: a hit this near (0: any)
    double chain = 0;     // s: a kill only this soon after the last
    double chance = 1;    // of each time it would trigger (Stun Rounds)
    bool aimed = false;   // a hit: only from a shot fired aimed down the sights (Spotter)
    double duration = 0;  // s its modifiers last once triggered (its timer)
    double cooldown = 0;  // s before it can trigger again (its timer, when it has no duration)
    std::vector<StatModifier> stats;      // while active
    std::vector<WeaponModifier> weapons;  // while active, on its weapons
    // The weapon slots it's for (a bit each): its handling's, and those whose
    // hits and kills trigger it. 0: an armor perk.
    uint8_t slots = 0;
    // What it does at once.
    double stamina = 0;       // given back
    double shield = 0;        // a share of the Deflector Shield's capacity restored
    double cool = 0;          // a share of full heat taken off the weapon
    bool reload = false;      // the weapon's clip refilled
    bool recharge = false;    // the shield starts recharging now
    double cloak = 0;         // s cloaked
    double mark = 0;          // s the target is marked for its side (Spotter)
    bool crit = false;        // the hit is a critical hit
    std::vector<StatModifier> target;  // a debuff on what's hit...
    double target_duration = 0;        // ...for this long
    // While active, on the server: damage taken times this; resistance to
    // the type of the hit that triggered it; ended by the next hit taken.
    double damage_taken = 1;
    double resist = 0;
    bool one_hit = false;
    bool once_per_life = false;

    // Whether its timer counts a cooldown rather than an active spell.
    bool timer_is_cooldown() const { return duration <= 0 && cooldown > 0; }
};

// What a character brings into a fight: weapons and gear. There are no
// character classes: what a character can do, and how much damage it takes,
// comes from what it carries. The default is an E-11 alone.
struct Loadout {
    std::string name = "default";
    std::array<std::optional<WeaponDef>, weapon_slots> weapons{WeaponDef{}, std::nullopt, std::nullopt};
    // A match's, with an effect ammo equipped: its guns as they'd be with
    // plain ammo (its ammo slots empty), and which ammo classes' effect
    // stacks ran dry there (by AmmoClass). Out of it, at the next respawn a
    // class's guns go back to plain (fall_back_to_plain_ammo), and that
    // stack's left is 0. Not sent to clients.
    std::array<std::optional<WeaponDef>, weapon_slots> plain_weapons;
    std::array<bool, 3> ammo_spent{};
    std::vector<Gear> gear;
    // What its armor makes it: none, light, medium or heavy (empty: not
    // known, Battlefront's own). Says which armor profession it levels.
    std::string armor_class;
    // Stage 12 (specialist.h), filled from the character's professions so a
    // match a hub hands the loadout to knows them: the abilities it brings
    // (world and match ones), the levels of its specialist and companion
    // professions, and its Companion Slot's companion (format_companion; empty: none).
    std::vector<std::string> abilities;
    std::map<std::string, int> specialist_levels;
    // The switches its perks turn on, "<profession>:<grant>" (field_commander,
    // bloodhound ...): a match has no professions to look them up in.
    std::vector<std::string> grants;
    std::string companion;
    // An Entertainer's Inspiration it carries into a match (format_inspiration;
    // empty: none). The server's alone: clients see it as an effect.
    std::string inspiration;
    // Its triggered perks (from the character's trainer perks), at most
    // max_perk_timers: each one's timer is MoveState::perk_time[its index].
    std::vector<TriggeredPerk> triggered;
    // Unified ammo inventory reserves (-1 = legacy BF2 per-weapon reserves).
    int unified_standard_ammo = -1;
    int unified_heavy_ammo = -1;

    int unified_ammo(AmmoClass ac) const {
        if (ac == AmmoClass::standard) return unified_standard_ammo;
        if (ac == AmmoClass::heavy) return unified_heavy_ammo;
        return -1;
    }
    void set_unified_ammo(AmmoClass ac, int amount) {
        if (ac == AmmoClass::standard) unified_standard_ammo = amount;
        else if (ac == AmmoClass::heavy) unified_heavy_ammo = amount;
    }
};

// The weapon in a slot, if the loadout has one.
const WeaponDef* weapon_in_slot(const Loadout& loadout, int slot);

// The base statistics with every weapon's and gear's modifiers applied:
// each statistic plus all additions, times all multipliers.
CharacterStats stats_for(const Loadout& loadout, const CharacterStats& base = {});
// Statistics with more modifiers applied on top, the same way.
CharacterStats with_modifiers(const CharacterStats& stats, std::span<const StatModifier> modifiers);

// "run_speed * 0.9", "mitigation_laser + 0.15", "max_health - 50"; throws
// std::runtime_error naming `where` if it isn't one.
StatModifier parse_stat_modifier(const std::string& text, const std::string& where);

// Data files (INI). Each throws std::runtime_error if it can't be read or
// makes no sense.
//
// A weapon: a [weapon] section (written by `bf2tool export-loadout`).
WeaponDef load_weapon(const std::filesystem::path& path);
// A weapon from a [weapon] section (an item file's, or a vehicle's: see
// load_vehicle_class); `where` names it in errors.
WeaponDef weapon_from_section(const class Ini& ini, size_t section, const std::string& where,
                              const std::string& fallback_name);
// A piece of gear: a [gear] section with Name, Slot and Modifier lines.
Gear load_gear(const std::filesystem::path& path);
// A loadout: a [loadout] section naming its items: Primary, Sidearm,
// Secondary and Gear (repeatable), each "<name>" for an item file
// "<name>.ini" found in the Items folders (default: ../items and
// ../imported_assets/items, relative to the loadout file).
Loadout load_loadout(const std::filesystem::path& path);

} // namespace swm::core
