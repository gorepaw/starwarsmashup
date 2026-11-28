#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "swbfo/core/loadout.h"

namespace swbfo::core {

enum class Species : uint8_t {
    human,
    twilek,
    zabrak,
    rodian,
    bothan,
    sullustan,
    moncal,
    trandoshan,
    wookiee,
    ithorian,
};
inline constexpr size_t species_count = 10;

enum class Gender : uint8_t { male, female };

struct SpeciesBody {
    double radius = 0.4;
    double height = 1.8;
    double eye_height = 1.6;
    double crouched_eye_height = 1.1;
    double scale_min = 0.88;
    double scale_max = 1.12;
};

struct SpeciesDef {
    Species species = Species::human;
    std::string id;
    std::string code;
    std::string display_name;
    SpeciesBody male_body;
    SpeciesBody female_body;
    // Worn slots whose armor and clothes this body doesn't show (a Wookiee's
    // chest, a Trandoshan's boots): they're worn and protect all the same.
    std::vector<std::string> hidden_slots;
    std::vector<StatModifier> modifiers;
    std::string traits_summary;
};

const SpeciesDef& species_def(Species s);
const std::array<SpeciesDef, species_count>& all_species();
std::optional<Species> species_from_name(std::string_view name);
std::optional<Gender> gender_from_name(std::string_view name);
// Whether worn gear in this slot shows on this species' body (the cosmetic
// layer only: every species wears every slot for its statistics).
bool species_shows_slot(Species s, std::string_view slot);
SpeciesBody species_body(Species s, Gender g, double height_slider = 0.5);

// Statistics with weapons, worn gear (every species wears every slot),
// and the species' built-in traits applied.
CharacterStats stats_for(const Loadout& loadout, Species species, const CharacterStats& base = {});

} // namespace swbfo::core
