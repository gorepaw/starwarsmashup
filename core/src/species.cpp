#include "swm/core/species.h"

#include <algorithm>
#include <cctype>

namespace swm::core {

namespace {

std::string lower_ascii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

SpeciesBody scaled_female(const SpeciesBody& male, double factor = 0.95) {
    SpeciesBody f = male;
    f.height *= factor;
    f.eye_height *= factor;
    f.crouched_eye_height *= factor;
    f.radius *= std::max(0.92, factor);
    return f;
}

const std::array<SpeciesDef, species_count> species_table = [] {
    std::array<SpeciesDef, species_count> table{};

    const std::vector<std::string> no_standard_armor = {
        "helmet", "chest", "biceps", "bracers", "gloves", "belt", "leggings", "boots",
    };

    {
        SpeciesDef& d = table[size_t(Species::human)];
        d.species = Species::human;
        d.id = "human";
        d.code = "hum";
        d.display_name = "Human";
        d.male_body = {0.40, 1.78, 1.60, 1.10, 0.88, 1.12};
        d.female_body = scaled_female(d.male_body, 0.95);
        d.modifiers = {};
        d.traits_summary = "Baseline stats; wears all standard armor";
    }
    {
        SpeciesDef& d = table[size_t(Species::twilek)];
        d.species = Species::twilek;
        d.id = "twilek";
        d.code = "twk";
        d.display_name = "Twi'lek";
        d.male_body = {0.40, 1.80, 1.62, 1.11, 0.88, 1.12};
        d.female_body = scaled_female(d.male_body, 0.95);
        d.modifiers = {{Stat::run_speed, 0, 1.03}, {Stat::max_energy, 10, 1}};
        d.traits_summary = "+3% Run Speed, +10 Energy; wears all standard armor";
    }
    {
        SpeciesDef& d = table[size_t(Species::zabrak)];
        d.species = Species::zabrak;
        d.id = "zabrak";
        d.code = "zab";
        d.display_name = "Zabrak";
        d.male_body = {0.40, 1.80, 1.62, 1.11, 0.88, 1.12};
        d.female_body = scaled_female(d.male_body, 0.95);
        d.modifiers = {{Stat::max_health, 30, 1}, {Stat::energy_restore, 0, 1.08}};
        d.traits_summary = "+30 Health, +8% Energy Regen; wears all standard armor";
    }
    {
        SpeciesDef& d = table[size_t(Species::rodian)];
        d.species = Species::rodian;
        d.id = "rodian";
        d.code = "rod";
        d.display_name = "Rodian";
        d.male_body = {0.39, 1.74, 1.56, 1.07, 0.88, 1.12};
        d.female_body = scaled_female(d.male_body, 0.95);
        d.modifiers = {{Stat::sprint_factor, 0, 1.04}, {Stat::max_energy, 15, 1}};
        d.traits_summary = "+4% Sprint Speed, +15 Energy; wears all standard armor";
    }
    {
        SpeciesDef& d = table[size_t(Species::bothan)];
        d.species = Species::bothan;
        d.id = "bothan";
        d.code = "bth";
        d.display_name = "Bothan";
        d.male_body = {0.38, 1.68, 1.51, 1.04, 0.88, 1.12};
        d.female_body = scaled_female(d.male_body, 0.95);
        d.modifiers = {{Stat::sprint_factor, 0, 1.04}, {Stat::energy_restore, 0, 1.10}};
        d.traits_summary = "+4% Sprint Speed, +10% Energy Regen; wears all standard armor";
    }
    {
        SpeciesDef& d = table[size_t(Species::sullustan)];
        d.species = Species::sullustan;
        d.id = "sullustan";
        d.code = "sul";
        d.display_name = "Sullustan";
        d.male_body = {0.36, 1.58, 1.42, 0.98, 0.88, 1.12};
        d.female_body = scaled_female(d.male_body, 0.96);
        d.modifiers = {{Stat::max_health, 15, 1}, {Stat::energy_restore, 0, 1.08}};
        d.traits_summary = "+15 Health, +8% Energy Regen; wears all standard armor";
    }
    {
        SpeciesDef& d = table[size_t(Species::moncal)];
        d.species = Species::moncal;
        d.id = "moncal";
        d.code = "mon";
        d.display_name = "Mon Calamari";
        d.male_body = {0.40, 1.76, 1.58, 1.09, 0.88, 1.12};
        d.female_body = scaled_female(d.male_body, 0.95);
        d.modifiers = {{Stat::max_energy, 20, 1}, {Stat::mitigation_bio, 0.10, 1}};
        d.traits_summary = "+20 Energy, +10% Bio Resistance; wears all standard armor";
    }
    {
        SpeciesDef& d = table[size_t(Species::trandoshan)];
        d.species = Species::trandoshan;
        d.id = "trandoshan";
        d.code = "trn";
        d.display_name = "Trandoshan";
        d.male_body = {0.45, 1.96, 1.78, 1.22, 0.90, 1.12};
        d.female_body = scaled_female(d.male_body, 0.95);
        d.blocked_slots = {"gloves", "boots"};
        d.modifiers = {{Stat::max_health, 60, 1},
                       {Stat::mitigation, 0.10, 1},
                       {Stat::mitigation_melee, 0.10, 1},
                       {Stat::energy_restore, 0, 1.05}};
        d.traits_summary = "+60 Health, +10% Armor, +10% Melee Resist; cannot wear gloves or boots";
    }
    {
        SpeciesDef& d = table[size_t(Species::wookiee)];
        d.species = Species::wookiee;
        d.id = "wookiee";
        d.code = "wke";
        d.display_name = "Wookiee";
        d.male_body = {0.50, 2.20, 1.98, 1.36, 0.92, 1.12};
        d.female_body = scaled_female(d.male_body, 0.95);
        d.blocked_slots = no_standard_armor;
        d.modifiers = {{Stat::max_health, 120, 1},
                       {Stat::mitigation, 0.20, 1},
                       {Stat::mitigation_melee, 0.15, 1}};
        d.traits_summary = "+120 Health, +20% Armor, +15% Melee Resist; no standard armor or footwear";
    }
    {
        SpeciesDef& d = table[size_t(Species::ithorian)];
        d.species = Species::ithorian;
        d.id = "ithorian";
        d.code = "ith";
        d.display_name = "Ithorian";
        d.male_body = {0.46, 2.08, 1.88, 1.28, 0.90, 1.12};
        d.female_body = scaled_female(d.male_body, 0.95);
        d.blocked_slots = no_standard_armor;
        d.modifiers = {{Stat::max_health, 100, 1},
                       {Stat::mitigation, 0.18, 1},
                       {Stat::mitigation_bio, 0.15, 1},
                       {Stat::mitigation_electro, 0.10, 1}};
        d.traits_summary = "+100 Health, +18% Armor, +15% Bio, +10% Electro; no standard armor";
    }

    return table;
}();

} // namespace

const SpeciesDef& species_def(Species s) {
    const size_t index = size_t(s);
    return species_table[index < species_count ? index : 0];
}

const std::array<SpeciesDef, species_count>& all_species() {
    return species_table;
}

std::optional<Species> species_from_name(std::string_view name) {
    const std::string key = lower_ascii(name);
    for (const SpeciesDef& d : species_table) {
        if (d.id == key || d.code == key) {
            return d.species;
        }
    }
    return std::nullopt;
}

std::optional<Gender> gender_from_name(std::string_view name) {
    const std::string key = lower_ascii(name);
    if (key == "male" || key == "m") {
        return Gender::male;
    }
    if (key == "female" || key == "f") {
        return Gender::female;
    }
    return std::nullopt;
}

bool species_can_wear_slot(Species s, std::string_view slot) {
    const std::string key = lower_ascii(slot);
    const auto& blocked = species_def(s).blocked_slots;
    return std::find(blocked.begin(), blocked.end(), key) == blocked.end();
}

SpeciesBody species_body(Species s, Gender g, double height_slider) {
    const SpeciesDef& d = species_def(s);
    SpeciesBody body = g == Gender::female ? d.female_body : d.male_body;
    const double t = std::clamp(height_slider, 0.0, 1.0);
    const double scale = body.scale_min + (body.scale_max - body.scale_min) * t;
    body.height *= scale;
    body.eye_height *= scale;
    body.crouched_eye_height *= scale;
    return body;
}

CharacterStats stats_for(const Loadout& loadout, Species species, const CharacterStats& base) {
    Loadout filtered;
    filtered.name = loadout.name;
    filtered.weapons = loadout.weapons;
    for (const Gear& g : loadout.gear) {
        if (g.slot.empty() || species_can_wear_slot(species, g.slot)) {
            filtered.gear.push_back(g);
        }
    }
    const auto& mods = species_def(species).modifiers;
    if (!mods.empty()) {
        filtered.gear.push_back(Gear{.name = species_def(species).id, .slot = "", .modifiers = mods});
    }
    return stats_for(filtered, base);
}

} // namespace swm::core
