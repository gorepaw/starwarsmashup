#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace swm::core {

struct LootEntry {
    std::string item_id;
    double weight = 1.0;
};

struct LootTable {
    std::string id;
    int64_t credits_min = 0;
    int64_t credits_max = 0;
    int junk_rolls_min = 1;
    int junk_rolls_max = 1;
    std::vector<LootEntry> junk_pool;
    double gear_chance = 0.0;
    double gear_quality_min = 0.25;
    double gear_quality_max = 0.85;
    int gear_mark_min = 0;  // its gear's Mk tiers (items.h), rolled evenly between
    int gear_mark_max = 0;
    std::vector<LootEntry> gear_pool;
};

struct RolledLootItem {
    std::string def_id;
    double quality = 0.5;
    int mark = 0;
};

struct RolledLoot {
    int64_t credits = 0;
    std::vector<RolledLootItem> items;
};

class LootTableSet {
public:
    void load(const std::filesystem::path& ini_path);
    const LootTable* find(std::string_view id) const;
    size_t size() const { return tables_.size(); }

    RolledLoot roll(std::string_view table_id, uint64_t& rng_state) const;

private:
    std::vector<LootTable> tables_;
    std::map<std::string, size_t, std::less<>> index_;
};

} // namespace swm::core
