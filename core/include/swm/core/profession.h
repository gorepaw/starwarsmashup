#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "swm/core/ini.h"
#include "swm/core/items.h"
#include "swm/core/loadout.h"

namespace swm::core {

// Professions (docs/PROFESSIONS.md): Galaxies' professions as 1-100 lines a
// character levels by doing what they're about. There are no classes: a
// loadout's Active Specialization Slots say which professions' combat perks
// it fights with.

enum class ProfessionCategory : uint8_t { armor, weapon, gathering, crafting, specialist, companion };

std::optional<ProfessionCategory> profession_category_from_name(std::string_view name);
std::string_view profession_category_name(ProfessionCategory category);

inline constexpr int profession_max_level = 100;
// Active Specialization Slots in each loadout (ASSUMPTION: three, enough for
// a weapon, an armor and one more).
inline constexpr int specialization_slots = 3;

// A handling change a weapon profession makes to its own weapons, written
// "spread * 0.97": spread (the cone and its bloom per shot), heat (per shot),
// reload (its time), recovery (how fast bloom settles, larger is faster) or
// range (how far bolts carry before their damage falls off).
struct WeaponModifier {
    enum Field : uint8_t { spread, heat, reload, recovery, range, speed, lunge, disrupt };
    Field field = spread;
    double multiply = 1;

    friend bool operator==(const WeaponModifier&, const WeaponModifier&) = default;
};

// What a milestone or a rank gives: character statistics, and handling for
// the profession's own weapons.
struct ProfessionPerk {
    std::vector<StatModifier> stats;
    std::vector<WeaponModifier> weapons;

    bool empty() const { return stats.empty() && weapons.empty(); }
};

// A major unlock (at 1, 25, 50, 75 and 100).
struct ProfessionMilestone {
    int level = 1;
    std::string name;
    std::string description;
    ProfessionPerk perk;
};

struct ProfessionDef {
    std::string id;
    std::string name;
    std::string description;
    ProfessionCategory category = ProfessionCategory::weapon;
    std::string wave = "7A";
    // False while what it's about isn't in the game yet (gathering, crafting,
    // most specialists): shown in the planner, but it can't be slotted or levelled.
    bool available = true;
    // Its combat perks apply only from an Active Specialization Slot.
    bool slotted = false;
    std::vector<std::string> archetypes;  // a weapon profession's weapons (WeaponArchetypeInfo::id)
    std::string armor_class;              // an armor profession's: none, light, medium or heavy
    std::string earns;                    // what earns it experience, for the planner
    // Small steps between the milestones: `rank` every `rank_every` levels.
    int rank_every = 0;
    ProfessionPerk rank;
    std::vector<ProfessionMilestone> milestones;  // by level
};

// A character's experience in one profession.
struct ProfessionProgress {
    std::string id;
    int64_t xp = 0;

    friend bool operator==(const ProfessionProgress&, const ProfessionProgress&) = default;
};

// The roster, from game/professions/professions.ini: [curve], then each
// [profession] followed by its [milestone]s.
class ProfessionBook {
public:
    // Throws std::runtime_error if the file can't be read or makes no sense.
    void load(const std::filesystem::path& path);
    void parse(const Ini& ini, const std::string& where);

    const std::vector<ProfessionDef>& all() const { return professions_; }
    const ProfessionDef* find(std::string_view id) const;
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

// Whether a profession may sit in an Active Specialization Slot.
bool can_specialize(const ProfessionBook& book, std::string_view id);
// A loadout's slots made sound: known, slottable professions, each once, at most specialization_slots.
std::vector<std::string> clean_specializations(const ProfessionBook& book, std::span<const std::string> ids);

// The professions experience for something done in a loadout goes to when
// it isn't any one profession's (quests, missions, battles): the equipped
// weapon's (the primary's, else the sidearm's) and the armor's.
std::vector<std::string> routed_professions(const ProfessionBook& book, const Loadout& loadout);

// How strong a character is in a fight: the higher of the levels of the
// profession of the weapon it carries and of the armor it wears. Quests and
// missions have levels against it.
int combat_level(const ProfessionBook& book, const Loadout& loadout, std::span<const ProfessionProgress> progress);

// A quest this many levels below a character's combat level isn't worth
// their while: its giver is shown grey.
inline constexpr int quest_trivial_gap = 10;

// A slotted profession's perks at its character's level: its ranks and
// milestones reached. An armor profession's apply only to armor of its class.
ProfessionPerk profession_perks(const ProfessionDef& def, int level);

// Applies a loadout's slotted professions at the character's levels: their
// statistics as one more piece of gear (slot "specialization", so clients
// predict with them), their weapon handling to the weapons they cover.
void apply_specializations(Loadout& loadout, const ProfessionBook& book, std::span<const std::string> specializations,
                           std::span<const ProfessionProgress> progress);

// What an armor's pieces make it: the class most of them are, the heavier on
// a tie; "none" without armor.
std::string loadout_armor_class(std::span<const std::string> piece_classes);

} // namespace swm::core
