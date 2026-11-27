#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "swm/core/loadout.h"
#include "swm/core/species.h"

namespace swm::core {

// A bodysuit is the one look-only item (docs/ARMOR.md): a whole-body model
// (a Battlefront II clone or droid), worn only in the cosmetic override.
enum class ItemKind : uint8_t {
    weapon, armor, deflector_shield, clothing, junk, supply, quest, enhancement, vehicle, ammo, bodysuit
};

inline constexpr std::array<std::string_view, 4> playable_factions{"rebellion", "empire", "republic", "separatists"};

enum class RepTier : uint8_t { hated = 0, unfriendly, neutral, friendly, honored, revered, exalted };

std::string normalize_faction(std::string_view faction);
std::string opposing_faction(std::string_view faction);
RepTier reputation_tier(int64_t reputation);
std::string_view reputation_tier_name(RepTier tier);
std::optional<RepTier> reputation_tier_from_name(std::string_view name);
int64_t reputation_threshold(RepTier tier);
int64_t clamp_reputation(int64_t reputation);

inline constexpr double loadout_switch_time = 3.0;
inline constexpr int max_character_loadouts = 5;

struct BattleReward {
    int64_t xp = 0;
    int64_t faction_points = 0;
    int64_t credits = 0;

    friend bool operator==(const BattleReward&, const BattleReward&) = default;
};

BattleReward compute_battle_reward(bool won, bool draw, uint16_t kills, uint16_t captures);

enum class WeaponArchetype : uint8_t {
    rifle_auto,
    carbine_burst,
    sniper,
    scatter,
    pistol,
    heavy_launcher,
    grenade,
    melee,
    beam,         // a rifle's continuous stream: near-instant, heat only (lightning, beam rifles)
    beam_cannon,  // a heavy weapon's (the particle and lightning beam cannons)
    flamer,       // a short cone of fire or acid: heat only, sets its effect (flamethrower, acid launcher)
    scatter_pistol,  // a spread of bolts at sidearm size, on heat (the scatter pistol)
    melee_2h,        // a two-handed sword, axe, hammer or polearm: a primary, heavier than `melee`
    placed,          // a mine or trap: tossed down, it goes off when an enemy comes near (tactical)
    utility,         // a tactical one-shot: a bolt that leaves its effect (the flare, tangle pistols)
};

// A melee archetype: one-handed (`melee`, a sidearm) or two-handed (`melee_2h`).
bool is_melee_archetype(WeaponArchetype a);

struct WeaponArchetypeInfo {
    WeaponArchetype archetype = WeaponArchetype::rifle_auto;
    std::string id;
    std::string bf2_weapon_base;
    std::string anim_set;
    std::string default_slot;
};

const WeaponArchetypeInfo& weapon_archetype_info(WeaponArchetype a);
std::optional<WeaponArchetype> weapon_archetype_from_name(std::string_view name);
std::optional<ItemKind> item_kind_from_name(std::string_view name);
std::string_view item_kind_name(ItemKind k);

// Static item definition loaded from game/items/swg_catalog.ini. Weapon
// keys (multipliers over the archetype's Battlefront weapon): DamageMult,
// RateMult, HeatMult, SpreadMult, RangeMult, ClipMult, ReloadMult,
// VelocityMult, HeadshotMult, VehicleMult, ShieldMult, SwapMult,
// AimSpeedMult; and CritChance, CritMult, Recoil (degrees), OnHit (a
// [hit_effect]'s Id). Ammo takes the same (its OnHit goes to every gun
// firing it).
struct ItemDef {
    std::string id;
    std::string display_name;
    std::string description;
    ItemKind kind = ItemKind::weapon;
    std::string slot = "primary";
    WeaponArchetype archetype = WeaponArchetype::rifle_auto;
    std::string anim_set = "rifle";
    std::string faction = "neutral";  // neutral | empire | rebellion | republic | separatists
    RepTier required_rank = RepTier::neutral;
    int64_t cost_faction_points = 0;
    int64_t cost_credits = 0;
    int64_t sell_value = 0;
    double heal_amount = 0.0;
    AmmoClass ammo_class = AmmoClass::none;
    int ammo_batch = 100;
    std::string armor_class;  // armor: light, medium or heavy (its armor profession's)
    std::string vehicle;      // a vehicle: its class (a world vehicle class file's Name), called by using it
    std::string model_path;  // relative to game/imported_assets/, may contain {g} for m/f; worn pieces may list several, comma-separated
    // A weapon's Galaxies object (Template = object/weapon/.../shared_*.iff):
    // `swgtool export-items` exports its model, as Galaxies shows it, to
    // weapons/<id>/<id>.gltf, which is its Model unless it names another.
    std::string swg_template;
    // How the exporter turns the model to lie along +Z with the guns
    // (ModelForward = +y or -y: a melee weapon modeled standing along Y; 0
    // as modeled), and whether it makes a muzzle at the model's front
    // (ModelMuzzle = front: for a model with no muzzle hardpoint).
    int model_forward = 0;
    bool model_add_muzzle = false;
    std::string bf2_base;    // overrides archetype's default BF2 weapon .ini if non-empty

    // Weapon sidegrade multipliers over the BF2 archetype base.
    double damage_mult = 1.0;
    double rate_mult = 1.0;    // >1 fires faster (divides shot_delay)
    double heat_mult = 1.0;    // >1 builds more heat per shot
    double spread_mult = 1.0;  // >1 wider spread
    double range_mult = 1.0;   // >1 longer bolt_life / transition delay
    double clip_mult = 1.0;    // >1 more rounds_per_clip
    double reload_mult = 1.0;  // >1 longer reload_time
    double melee_range = 0.0;
    double melee_arc = 0.0;
    double melee_lunge = 0.0;
    double melee_stagger = 0.0;
    double shield_disrupt = 0.0;
    // A placed trap (the auto laser trap): bolts it fires at an enemy who
    // comes near, instead of exploding. 0: a mine.
    int trap_shots = 0;
    double velocity_mult = 1.0;  // >1 faster bolts (over the same reach)
    double headshot_mult = 1.0;  // times the base headshot multiplier
    double crit_chance = 0.0;    // added to the weapon's (0: none)
    double crit_mult = 0.0;      // the crit multiplier, if > 0
    double vehicle_mult = 1.0;   // damage to vehicles
    double shield_mult = 1.0;    // damage to Deflector Shields
    double swap_mult = 1.0;      // >1 slower to bring up
    double recoil = -1;          // degrees of kick a shot (-1: the archetype's)
    double aim_speed_mult = 1.0; // >1 faster to aim down
    std::string hit_effect;      // a [hit_effect]'s Id ("" none): ammo's, mostly
    // The species that may use it ("Species = wookiee", comma-separated; empty:
    // anyone). A species weapon has a bigger sidegrade budget in return.
    std::vector<Species> species;
    std::optional<DamageType> damage_type;
    std::string bolt_color_name;  // optional red/blue/green/yellow

    // Armor: how its class's typed resistance is spread ("Resist = laser:0.6,
    // ballistic:0.4", shares summing to 1), and its own small perk (its
    // Modifier lines), beyond the class budget. The loader adds its slot's
    // share of the class budget (armor_budget) to `modifiers`.
    std::vector<std::pair<DamageType, double>> resist;
    std::vector<StatModifier> perks;
    // Paint schemes ("Paint = empire: index_color_1=1, index_texture_1=0"):
    // the customization values its model is drawn with, by scheme, a
    // faction's or "neutral" (the default). An owned piece's scheme is its
    // color_json's "paint" (painted_color_json). Kept as a query string,
    // "index_color_1=1&index_texture_1=0".
    std::map<std::string, std::string> paints;

    // Stat modifiers for armor, deflector shields, or weapon carry penalties.
    std::vector<StatModifier> modifiers;
};

// The worn slots (docs/ARMOR.md): armor and clothing each go in one, in a
// loadout's gear or its cosmetic override (appearance). The override also
// has "full_body", for a bodysuit, which shows in place of all of them.
inline constexpr std::array<std::string_view, 8> worn_slots{"helmet", "chest",  "biceps",   "bracers",
                                                            "gloves", "belt",   "leggings", "boots"};
inline constexpr std::string_view full_body_slot = "full_body";
bool is_worn_slot(std::string_view slot);  // one of worn_slots

// A full kit's statistics for an armor class (docs/ARMOR.md, Balance): every
// set of the class has the same, and each piece its slot's share. Clothing
// ("none") has nothing.
struct ArmorBudget {
    double mitigation = 0;      // general
    double typed = 0;           // by type, spread as the set's Resist says
    double health = 0;
    double run_speed = 1;       // multipliers: a piece gets them to the power of its share
    double sprint_factor = 1;
    double energy_restore = 1;
};
const ArmorBudget& armor_budget(std::string_view armor_class);
// A worn slot's share of a full kit: its defense_mark_weight over the worn
// slots' (the chest's 2/7 ...); 0 for anything else.
double armor_slot_share(std::string_view slot);

// Whether an item goes in a loadout slot: in its gear (`appearance` false),
// or in its cosmetic override, which only shows (`appearance` true). Any
// armor or clothing goes in either; weapons, shields, ammo and mounts only
// in the gear; a bodysuit only in the override's full_body.
bool item_fits_slot(const ItemDef& def, std::string_view slot, bool appearance);

// An owned item's paint scheme: its color_json's "paint" ("" if none).
struct ItemInstance;
std::string item_paint(const ItemInstance& inst);
// A new item's color_json, painted in a faction's scheme if it has one
// (a Katarn from the Imperial quartermaster), else "{}".
std::string painted_color_json(const ItemDef& def, std::string_view faction);

// An item owned by a character (persisted in SQLite).
struct ItemInstance {
    uint64_t id = 0;
    uint64_t owner_id = 0;
    std::string def_id;
    double quality = 0.5;  // 0..1; 0.5 is standard-issue baseline (±8% in Gear queue)
    std::string custom_name;
    std::string color_json = "{}";
    int quantity = 1;
    int mark = 0;  // its Mk, 0..max_mark (0: unmarked, level 1); see mark_scale

    friend bool operator==(const ItemInstance&, const ItemInstance&) = default;
};

// Mk tiers (docs/DESIGN.md, Power curve): any item at levels 1 (Mk 0, the
// plain item), 10 (Mk I), 20 (Mk II) ... 100 (Mk X). An item's mark is its
// owned copy's, so every item has every tier without a catalog entry each;
// which tiers drop or sell is up to loot tables and vendors.
inline constexpr int max_mark = 10;
int clamp_mark(int mark);
// The level a mark asks for: 1 for Mk 0, else 10 a mark.
int mark_level(int mark);
// What a mark multiplies an item's power by: 1 at Mk 0, 2 at Mk X (a
// weapon's damage; worn gear's share of the character's health, see
// defense_mark_weight; a Deflector Shield's capacity and recharge).
double mark_scale(int mark);
// "Mk IV" ("" for Mk 0).
std::string mark_name(int mark);
// "E-11 Blaster Rifle Mk IV".
std::string item_display_name(const ItemDef& def, int mark);
// A worn slot's share of the health a loadout's marks give: the Deflector
// Shield's 0.3, the chest's 0.2 and so on, summing to 1 over every slot. A
// character's health is times mark_scale of the weighted mark (an empty slot
// is Mk 0); 0 for any other slot.
double defense_mark_weight(std::string_view slot);

// Whether a species may use an item: a species weapon only its own.
bool species_can_use(const ItemDef& def, Species species);

// The sidegrade budget (docs/WEAPONS.md, Balance): what a weapon's or ammo's
// multipliers over its archetype add up to, as one number. Damage and rate
// of fire count in full; accuracy, range, heat, clip, reload and the rest by
// weights; crits by their expected damage; an on-hit effect by its chance; a
// carry penalty (slower running) gives budget back. The archetype itself
// scores 1. Every weapon in the catalog scores within sidegrade_tolerance of
// its budget: 1, or 1 + species_weapon_bonus for a species weapon.
double sidegrade_score(const ItemDef& def);
double sidegrade_budget(const ItemDef& def);
inline constexpr double sidegrade_tolerance = 0.06;
inline constexpr double species_weapon_bonus = 0.10;

// How a species whose body doesn't show the standard armor (a Wookiee, an
// Ithorian) looks armored (docs/ARMOR.md, Species sets): its own set's piece
// for a worn slot and an armor class, shown in place of a piece of that class
// worn there. Not an item: a catalog [species_armor] section.
struct SpeciesArmorLook {
    Species species = Species::human;
    std::string armor_class;   // light, medium or heavy
    std::string slot;          // a worn slot
    std::string set_name;      // "Kashyyykian Hunting"
    std::string model_path;    // like an item's: {g}, comma-separated (a pair of bracers)
    std::string swg_template;  // its Galaxies objects, one a model, for the exporter
};

// One of a character's up to 5 saved loadouts.
struct CharacterLoadout {
    int slot_index = 0;
    std::string name = "Loadout 1";
    uint64_t primary_id = 0;
    uint64_t sidearm_id = 0;
    uint64_t secondary_id = 0;
    std::map<std::string, uint64_t> gear_ids;        // worn slots, deflector_shield, ammo, mount
    std::map<std::string, uint64_t> appearance_ids;  // the cosmetic override: worn slots, shown over the gear
    // Its Active Specialization Slots: profession ids, at most
    // specialization_slots (profession.h), and its Companion Slot (none yet).
    std::vector<std::string> specializations;
    std::string companion;

    friend bool operator==(const CharacterLoadout&, const CharacterLoadout&) = default;
};

class ItemCatalog {
public:
    void load(const std::filesystem::path& catalog_ini, const std::filesystem::path& bf2_items_dir = {});

    const ItemDef* find(std::string_view id) const;
    const std::vector<ItemDef>& all() const { return items_; }
    // A [hit_effect] section's effect by its Id; nullptr if there's none.
    const HitEffect* hit_effect(std::string_view id) const;
    const std::vector<HitEffect>& hit_effects() const { return hit_effects_; }
    const std::vector<SpeciesArmorLook>& species_looks() const { return species_looks_; }
    // A species' own piece for a slot and an armor class; nullptr if it has none.
    const SpeciesArmorLook* species_look(Species species, std::string_view armor_class, std::string_view slot) const;

    WeaponDef resolve_weapon(const ItemDef& def, double quality = 0.5, bool apply_quality = true, int mark = 0) const;
    Gear resolve_gear(const ItemDef& def, double quality = 0.5, bool apply_quality = true, int mark = 0) const;
    Loadout build_loadout(const CharacterLoadout& cl, std::span<const ItemInstance> inventory, Species species,
                          bool apply_quality = true) const;

private:
    std::vector<ItemDef> items_;
    std::map<std::string, size_t, std::less<>> index_;
    std::vector<HitEffect> hit_effects_;
    std::vector<SpeciesArmorLook> species_looks_;
    std::map<std::string, WeaponDef, std::less<>> bf2_bases_;
};

// The models a loadout shows (comma-separated, game/imported_assets/-relative;
// "override:<path>" for a bodysuit): its gear's worn slots, each shown as
// what its cosmetic override has there instead, if anything; or the
// override's bodysuit, in place of all of them. In a slot this species
// doesn't show, its own piece of the worn piece's armor class
// (species_look), if it has one; else nothing. A painted piece's models carry
// its paint as a query ("armor/katarn/chest_m.gltf?index_color_1=1&..."): its
// own scheme, else its "neutral" one.
std::string resolve_worn_models(const ItemCatalog& catalog, const CharacterLoadout& cl,
                                std::span<const ItemInstance> inventory, Species species, Gender gender);

// Plain clothes (a tunic, pants and boots): what a soldier with no wardrobe of
// its own, a match bot, wears. Empty if the catalog has none of them.
std::string plain_clothes(const ItemCatalog& catalog, Gender gender);

} // namespace swm::core
