#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "swm/core/items.h"
#include "swm/core/species.h"

namespace swm::core {

struct BotProfileEntry {
    std::string name;
    std::string faction;  // normalized: rebellion | empire | republic | separatists
    Species species = Species::human;
    Gender gender = Gender::male;
    std::string appearance;
    std::vector<Loadout> loadouts;             // max_character_loadouts (5)
    std::vector<std::string> worn_per_loadout; // matching worn CSV or "override:<gltf>"
};

// Generates a deterministic stock roster (24 characters per playable faction)
// with faction-appropriate names, species/hair/gender diversity, quality tiers,
// and 5 complete clothed loadouts each (no naked bots).
std::vector<BotProfileEntry> build_stock_bot_roster(const ItemCatalog* catalog, uint32_t seed = 1);

// Filters `roster` down to characters matching normalized `faction`.
std::vector<const BotProfileEntry*> roster_for_faction(const std::vector<BotProfileEntry>& roster,
                                                       std::string_view faction);

} // namespace swm::core
