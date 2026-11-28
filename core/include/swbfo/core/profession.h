#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "swbfo/core/ini.h"
#include "swbfo/core/items.h"
#include "swbfo/core/loadout.h"

namespace swbfo::core {

// Professions (docs/PROFESSIONS.md): Galaxies' professions as 1-100 lines a
// character levels by doing what they're about. There are no classes: what a
// character has learned from trainers (docs/PERKS.md), at most perk_cap
// perks in each pool, is what it fights and works with.

enum class ProfessionCategory : uint8_t { armor, weapon, gathering, crafting, specialist, companion };

std::optional<ProfessionCategory> profession_category_from_name(std::string_view name);
std::string_view profession_category_name(ProfessionCategory category);

inline constexpr int profession_max_level = 100;
// Perks fall in two pools (docs/PERKS.md): combat (weapons,
// armor, the fighting specialists) and non-combat (gathering, crafting, the
// social and building ones). A character may know `perk_cap` in each.
enum class PerkPool : uint8_t { combat, noncombat };
std::string_view perk_pool_name(PerkPool pool);
inline constexpr int perk_cap = 10;
// The levels a profession offers its perks at, two at each. A profession you
// can't do at all without its first perk (its licence: gathering, performing,
// rallying) offers that tier at level 1 instead of 5 (FirstPerk = 1).
inline constexpr std::array<int, 5> perk_levels{5, 25, 45, 65, 85};

// (WeaponModifier, a handling change, is in loadout.h.)

// What a perk gives: character statistics, and handling for the
// profession's own weapons.
struct ProfessionPerk {
    std::vector<StatModifier> stats;
    std::vector<WeaponModifier> weapons;

    bool empty() const { return stats.empty() && weapons.empty(); }
};

// A profession's perk: one of the two (A or B) at its level.
struct ProfessionPerkDef {
    std::string id;
    int level = 5;
    char choice = 'A';
    std::string name;
    std::string description;
    // What it waits for ("trigger", "mechanic"): it can be learned, but does
    // nothing yet. Empty: it works.
    std::string needs;
    // The profession's weapons its handling is for; empty: all of them.
    std::vector<std::string> archetypes;
    // Its Modifier and Weapon lines: always, or while its trigger holds.
    ProfessionPerk perk;
    // An advanced profession's: the abilities it teaches (Ability = <id>,
    // specialists.ini), and what it switches on that the server looks for by
    // name (Grants = <flag>: band_leader, field_commander ...).
    std::vector<std::string> abilities;
    std::vector<std::string> grants;
    // Numbers the server sums by name across a profession's known perks
    // (Bonus = <key> <value>): gathering's yield, double_haul; crafting's
    // grade, exceptional, materials, extra_item, order_pay; anyone's xp.
    std::vector<std::pair<std::string, double>> bonuses;
    // Working only on something (Trigger = ...): what, and what it does.
    // Its slots and modifiers are filled in for a loadout.
    std::optional<TriggeredPerk> trigger;
};

struct ProfessionDef {
    std::string id;
    std::string name;
    std::string description;
    ProfessionCategory category = ProfessionCategory::weapon;
    std::string wave = "7A";
    // False while what it's about isn't in the game yet: shown in the
    // planner, but it can't be learned or levelled.
    bool available = true;
    std::vector<std::string> archetypes;  // a weapon profession's weapons (WeaponArchetypeInfo::id)
    std::string armor_class;              // an armor profession's: none, light, medium or heavy
    std::string earns;                    // what earns it experience, for the planner
    std::vector<ProfessionPerkDef> perks;  // by level, A before B
    PerkPool pool = PerkPool::combat;  // whose cap its perks count against
    int first_perk_level = 5;          // 1 for a licence tier (FirstPerk = 1)

    // Whether it teaches perks (one without any has nothing to learn).
    bool chooses() const { return !perks.empty(); }
    // The levels its perks are at: its first tier's, then 25, 45, 65, 85.
    std::array<int, 5> perk_tiers() const {
        return {first_perk_level, perk_levels[1], perk_levels[2], perk_levels[3], perk_levels[4]};
    }
};

// A character's experience in one profession.
struct ProfessionProgress {
    std::string id;
    int64_t xp = 0;
    // An old save's milestones, from before perks (the highest level learned
    // and paid for): refunded and cleared on loading (docs/PERKS.md).
    int trained = 0;
    int paid = 0;
    std::vector<std::string> perks;       // the perks learned (a profession that chooses)
    std::vector<std::string> paid_perks;  // every perk ever paid for

    friend bool operator==(const ProfessionProgress&, const ProfessionProgress&) = default;
};

// The roster, from game/professions/professions.ini: [curve], then each
// [profession] followed by its [perk]s.
class ProfessionBook {
public:
    // Throws std::runtime_error if the file can't be read or makes no sense.
    void load(const std::filesystem::path& path);
    void parse(const Ini& ini, const std::string& where);

    const std::vector<ProfessionDef>& all() const { return professions_; }
    const ProfessionDef* find(std::string_view id) const;
    // A perk by its id, and the profession it's in (`owner`); nullptr if none.
    const ProfessionPerkDef* find_perk(std::string_view id, const ProfessionDef** owner = nullptr) const;
    // The profession a weapon of this archetype levels (by its id); nullptr if none.
    const ProfessionDef* for_archetype(std::string_view archetype) const;
    // The armor profession for an armor class ("none" is Unarmored).
    const ProfessionDef* for_armor(std::string_view armor_class) const;

    // The level curve: experience from `level` to the next, and in all to reach `level`.
    int64_t xp_to_next(int level) const;
    int64_t xp_for_level(int level) const;
    int level_for(int64_t xp) const;

private:
    std::vector<ProfessionDef> professions_;
    int64_t curve_base_ = 100;     // level 1 to 2
    int64_t curve_per_level_ = 15;  // more for each level after
};

int64_t profession_xp(std::span<const ProfessionProgress> progress, std::string_view id);
// Adds experience; returns the new total.
int64_t add_profession_xp(std::vector<ProfessionProgress>& progress, std::string_view id, int64_t xp);

const ProfessionProgress* find_progress(std::span<const ProfessionProgress> progress, std::string_view id);
ProfessionProgress& progress_for(std::vector<ProfessionProgress>& progress, std::string_view id);

// How many of perk_cap one profession's learning takes: its perks.
int learned_count(const ProfessionDef& def, const ProfessionProgress& progress);
// How many a character has learned in one pool.
int perks_learned(const ProfessionBook& book, std::span<const ProfessionProgress> progress, PerkPool pool);
// Whether a character may do what a profession is about (gather, craft,
// heal, tame, perform): a level-1 perk (FirstPerk = 1), learned. Weapons, armor and a profession whose perks start at 5
// (Doctor: medicine is crafted from level 1) need no licence.
bool licensed(const ProfessionBook& book, std::span<const ProfessionProgress> progress, std::string_view id);
// Whether a perk the character knows in a profession Grants this flag.
// The sum of a Bonus key across the perks known in a profession (or in a
// list of perk ids); 0 with none.
double perk_bonus(const ProfessionBook& book, std::span<const ProfessionProgress> progress, std::string_view profession,
                  std::string_view key);
double perk_bonus(const ProfessionBook& book, std::span<const std::string> perks, std::string_view key);
bool has_grant(const ProfessionBook& book, std::span<const ProfessionProgress> progress, std::string_view profession,
               std::string_view grant);

// A trainer's answer: whether it happened, what it cost and what to tell them.
struct TrainResult {
    bool ok = false;
    int64_t cost = 0;
    std::string message;
};
// Learns a perk (by id): its profession's level reached, the other perk at
// its level not known, room under perk_cap and the credits (perk_cost at
// its level, nothing if it was paid for before).
TrainResult learn_perk(const ProfessionBook& book, std::vector<ProfessionProgress>& progress, std::string_view perk_id,
                       int64_t credits);
// Gives up a perk (by its id). Always free.
TrainResult forget(const ProfessionBook& book, std::vector<ProfessionProgress>& progress, std::string_view id);

// The professions experience for something done in a loadout goes to when
// it isn't any one profession's (quests, missions, battles): the equipped
// weapon's (the primary's, else the sidearm's) and the armor's.
std::vector<std::string> routed_professions(const ProfessionBook& book, const Loadout& loadout);

// How strong a character is in a fight: the higher of the levels of the
// profession of the weapon it carries and of the armor it wears. Quests and
// missions have levels against it.
int combat_level(const ProfessionBook& book, const Loadout& loadout, std::span<const ProfessionProgress> progress);

// The profession whose level a marked item asks for: a weapon's archetype's,
// armor's class's; nullptr for anything else (a Deflector Shield, ammo),
// which asks for the character's highest profession level.
const ProfessionDef* mark_profession(const ProfessionBook& book, const ItemDef& def);
// A character's level for an item's mark: mark_profession's, else their highest.
int level_for_item(const ProfessionBook& book, const ItemDef& def, std::span<const ProfessionProgress> progress);
// Whether a character may equip an item at this mark (mark_level).
bool can_use_mark(const ProfessionBook& book, const ItemDef& def, int mark,
                  std::span<const ProfessionProgress> progress);

// A quest this many levels below a character's combat level isn't worth
// their while: its giver is shown grey.
inline constexpr int quest_trivial_gap = 10;

// What a character has of a profession at its level: its perks learned
// (perks that wait for something left out). An armor profession's apply only
// to armor of its class.
ProfessionPerk profession_perks(const ProfessionDef& def, const ProfessionProgress& progress, int level);

// What a trainer asks for learning a perk at `level`: a licence (level 1)
// is free.
int64_t perk_cost(int level);

// Applies what a character has learned at its levels: statistics as one more
// piece of gear (slot "specialization", so clients predict with them), weapon
// handling to the weapons each profession covers. An armor profession's only
// while the loadout's armor is its class.
void apply_profession_perks(Loadout& loadout, const ProfessionBook& book, std::span<const ProfessionProgress> progress);

// What an armor's pieces make it: the class most of them are, the heavier on
// a tie; "none" without armor.
std::string loadout_armor_class(std::span<const std::string> piece_classes);

} // namespace swbfo::core
