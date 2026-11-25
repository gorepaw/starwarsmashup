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

enum class ItemKind : uint8_t { weapon, armor, deflector_shield, clothing, junk, supply, quest, enhancement, vehicle };

enum class QueueMode : uint8_t { standard = 0, gear = 1 };

inline constexpr double loadout_switch_time = 3.0;
inline constexpr int max_character_loadouts = 3;

struct BattleReward {
    int64_t xp = 0;
    int64_t faction_points = 0;
    int64_t credits = 0;

    friend bool operator==(const BattleReward&, const BattleReward&) = default;
};

BattleReward compute_battle_reward(bool won, bool draw, uint16_t kills, uint16_t captures, QueueMode mode);

enum class WeaponArchetype : uint8_t {
    rifle_auto,
    carbine_burst,
    sniper,
    scatter,
    pistol,
    heavy_launcher,
    grenade,
    melee,
};

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

// Static item definition loaded from game/items/swg_catalog.ini.
struct ItemDef {
    std::string id;
    std::string display_name;
    std::string description;
    ItemKind kind = ItemKind::weapon;
    std::string slot = "primary";
    WeaponArchetype archetype = WeaponArchetype::rifle_auto;
    std::string anim_set = "rifle";
    std::string faction = "neutral";  // neutral | empire | rebellion
    int64_t cost_faction_points = 0;
    int64_t cost_credits = 0;
    int64_t sell_value = 0;
    double heal_amount = 0.0;
    std::string armor_class;  // armor: light, medium or heavy (its armor profession's)
    std::string vehicle;      // a vehicle: its class (a world vehicle class file's Name), called by using it
    std::string model_path;  // relative to game/imported_assets/, may contain {g} for m/f
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
    std::optional<DamageType> damage_type;
    std::string bolt_color_name;  // optional red/blue/green/yellow

    // Stat modifiers for armor, deflector shields, or weapon carry penalties.
    std::vector<StatModifier> modifiers;
};

// An item owned by a character (persisted in SQLite).
struct ItemInstance {
    uint64_t id = 0;
    uint64_t owner_id = 0;
    std::string def_id;
    double quality = 0.5;  // 0..1; 0.5 is standard-issue baseline (±8% in Gear queue)
    std::string custom_name;
    std::string color_json = "{}";

    friend bool operator==(const ItemInstance&, const ItemInstance&) = default;
};

// One of a character's up to 3 saved loadouts.
struct CharacterLoadout {
    int slot_index = 0;
    std::string name = "Loadout 1";
    uint64_t primary_id = 0;
    uint64_t sidearm_id = 0;
    uint64_t secondary_id = 0;
    std::map<std::string, uint64_t> gear_ids;        // armor slots + deflector_shield
    std::map<std::string, uint64_t> appearance_ids;  // separate cosmetic clothing/armor layer
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

    WeaponDef resolve_weapon(const ItemDef& def, double quality = 0.5, bool apply_quality = true) const;
    Gear resolve_gear(const ItemDef& def, double quality = 0.5, bool apply_quality = true) const;
    Loadout build_loadout(const CharacterLoadout& cl, std::span<const ItemInstance> inventory, Species species,
                          bool apply_quality = true) const;

private:
    std::vector<ItemDef> items_;
    std::map<std::string, size_t, std::less<>> index_;
    std::map<std::string, WeaponDef, std::less<>> bf2_bases_;
};

// Resolves worn armor & cosmetic clothing model paths (comma-separated,
// game/imported_assets/-relative) with appearance slots overriding armor visuals.
std::string resolve_worn_models(const ItemCatalog& catalog, const CharacterLoadout& cl,
                                std::span<const ItemInstance> inventory, Species species, Gender gender);

// Plain clothes (a tunic, pants and boots): what a soldier with no wardrobe of
// its own, a match bot, wears. Empty if the catalog has none of them.
std::string plain_clothes(const ItemCatalog& catalog, Gender gender);

} // namespace swm::core
