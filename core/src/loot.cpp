#include "swm/core/loot.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

#include "swm/core/ini.h"
#include "swm/core/items.h"

namespace swm::core {
namespace {

bool same_ci(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); });
}

double next_unit(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return double(z >> 11) / double(1ull << 53);
}

int64_t roll_int_range(int64_t lo, int64_t hi, uint64_t& rng) {
    if (hi <= lo) return lo;
    const double u = next_unit(rng);
    const int64_t span = hi - lo + 1;
    return std::clamp(lo + int64_t(u * double(span)), lo, hi);
}

LootEntry parse_entry(const std::string& text, const std::string& where) {
    std::istringstream in(text);
    LootEntry e;
    if (!(in >> e.item_id)) {
        throw std::runtime_error(where + ": loot entry missing item id");
    }
    if (!(in >> e.weight)) {
        e.weight = 1.0;
    }
    e.weight = std::max(0.01, e.weight);
    return e;
}

const LootEntry* pick_weighted(const std::vector<LootEntry>& pool, uint64_t& rng) {
    if (pool.empty()) return nullptr;
    double total = 0.0;
    for (const LootEntry& e : pool) total += e.weight;
    if (total <= 0.0) return &pool.front();
    double r = next_unit(rng) * total;
    for (const LootEntry& e : pool) {
        r -= e.weight;
        if (r <= 0.0) return &e;
    }
    return &pool.back();
}

} // namespace

void LootTableSet::load(const std::filesystem::path& ini_path) {
    tables_.clear();
    index_.clear();
    const Ini ini = Ini::load(ini_path);
    const std::string where = ini_path.string();
    for (const Ini::Section& s : ini.sections()) {
        if (s.name != "loot_table") continue;
        LootTable t;
        t.id = s.get("Id").value_or("");
        if (t.id.empty()) {
            throw std::runtime_error(where + ": [loot_table] missing Id");
        }
        t.credits_min = std::max<int64_t>(0, int64_t(s.number("CreditsMin", 0)));
        t.credits_max = std::max<int64_t>(t.credits_min, int64_t(s.number("CreditsMax", double(t.credits_min))));
        t.junk_rolls_min = std::max(0, int(s.number("JunkMin", 1)));
        t.junk_rolls_max = std::max(t.junk_rolls_min, int(s.number("JunkMax", double(t.junk_rolls_min))));
        t.gear_chance = std::clamp(s.number("GearChance", 0.0), 0.0, 1.0);
        if (const auto g = s.get("GearGrades")) {
            // "GearGrades = 35 50 15 0": Flawed, Standard, Fine, Exceptional weights.
            std::istringstream weights(*g);
            double total = 0.0;
            for (double& w : t.gear_grade_weights) {
                if (!(weights >> w) || w < 0.0) {
                    throw std::runtime_error(where + " (" + t.id + "): GearGrades takes 4 weights");
                }
                total += w;
            }
            if (total <= 0.0) throw std::runtime_error(where + " (" + t.id + "): GearGrades weighs nothing");
        }
        t.gear_mark_min = clamp_mark(int(s.number("GearMarkMin", 0)));
        t.gear_mark_max = std::max(t.gear_mark_min, clamp_mark(int(s.number("GearMarkMax", double(t.gear_mark_min)))));
        for (const auto& [k, v] : s.values) {
            if (same_ci(k, "Junk")) {
                t.junk_pool.push_back(parse_entry(v, where + " (" + t.id + ")"));
            } else if (same_ci(k, "Gear")) {
                t.gear_pool.push_back(parse_entry(v, where + " (" + t.id + ")"));
            }
        }
        index_[t.id] = tables_.size();
        tables_.push_back(std::move(t));
    }
}

const LootTable* LootTableSet::find(std::string_view id) const {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &tables_[it->second];
}

RolledLoot LootTableSet::roll(std::string_view table_id, uint64_t& rng_state) const {
    RolledLoot out;
    const LootTable* t = find(table_id);
    if (!t) return out;

    out.credits = roll_int_range(t->credits_min, t->credits_max, rng_state);

    if (!t->junk_pool.empty() && t->junk_rolls_max > 0) {
        const int count = int(roll_int_range(t->junk_rolls_min, t->junk_rolls_max, rng_state));
        for (int i = 0; i < count; ++i) {
            if (const LootEntry* e = pick_weighted(t->junk_pool, rng_state)) {
                out.items.push_back({e->item_id});
            }
        }
    }

    if (!t->gear_pool.empty() && t->gear_chance > 0.0 && next_unit(rng_state) < t->gear_chance) {
        if (const LootEntry* e = pick_weighted(t->gear_pool, rng_state)) {
            const auto& w = t->gear_grade_weights;
            double r = next_unit(rng_state) * (w[0] + w[1] + w[2] + w[3]);
            ItemGrade grade = ItemGrade::exceptional;
            for (size_t g = 0; g < w.size(); ++g) {
                if (r < w[g]) {
                    grade = ItemGrade(g);
                    break;
                }
                r -= w[g];
            }
            // A range of marks takes one more roll (none for a single mark).
            const int mark = t->gear_mark_max > t->gear_mark_min
                                 ? int(roll_int_range(t->gear_mark_min, t->gear_mark_max, rng_state))
                                 : t->gear_mark_min;
            out.items.push_back({e->item_id, grade, mark});
        }
    }
    return out;
}

} // namespace swm::core
