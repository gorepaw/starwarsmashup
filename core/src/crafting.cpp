#include "swm/core/crafting.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace swm::core {

namespace {

constexpr std::array<std::string_view, resource_category_count> category_names{
    "mineral", "chemical", "gas", "flora", "water", "organic", "salvage"};
constexpr std::array<std::string_view, resource_category_count> category_labels{
    "Mineral", "Chemical", "Gas", "Flora", "Water", "Organic", "Salvage"};

constexpr std::array<std::string_view, resource_stat_count> stat_names{
    "quality", "conductivity", "toughness", "malleability", "purity"};
constexpr std::array<std::string_view, resource_stat_count> stat_labels{
    "Quality", "Conductivity", "Toughness", "Malleability", "Purity"};

bool same(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

std::optional<ResourceCategory> resource_category_from_name(std::string_view name) {
    for (size_t i = 0; i < category_names.size(); ++i) {
        if (same(category_names[i], name)) return ResourceCategory(i);
    }
    return std::nullopt;
}

std::string_view resource_category_name(ResourceCategory category) {
    const size_t i = size_t(category);
    return i < category_names.size() ? category_names[i] : "mineral";
}

std::string_view resource_category_label(ResourceCategory category) {
    const size_t i = size_t(category);
    return i < category_labels.size() ? category_labels[i] : "Mineral";
}

std::optional<ResourceStat> resource_stat_from_name(std::string_view name) {
    for (size_t i = 0; i < stat_names.size(); ++i) {
        if (same(stat_names[i], name)) return ResourceStat(i);
    }
    return std::nullopt;
}

std::string_view resource_stat_name(ResourceStat stat) {
    const size_t i = size_t(stat);
    return i < stat_names.size() ? stat_names[i] : "quality";
}

std::string_view resource_stat_label(ResourceStat stat) {
    const size_t i = size_t(stat);
    return i < stat_labels.size() ? stat_labels[i] : "Quality";
}

void CraftingBook::load_resources(const std::filesystem::path& path) {
    parse_resources(Ini::load(path), path.string());
}

void CraftingBook::load_schematics(const std::filesystem::path& path) {
    parse_schematics(Ini::load(path), path.string());
}

void CraftingBook::parse_resources(const Ini& ini, const std::string& where) {
    resources_.clear();
    for (const Ini::Section& s : ini.sections()) {
        if (s.name.empty() && s.values.empty()) continue;
        if (!same(s.name, "resource")) {
            throw std::runtime_error(where + ": unknown section [" + s.name + "]");
        }
        ResourceDef def;
        def.id = s.get("Id").value_or("");
        if (def.id.empty()) throw std::runtime_error(where + ": [resource] missing Id");
        if (find_resource(def.id) != nullptr) throw std::runtime_error(where + ": duplicate resource " + def.id);
        def.name = s.get("Name").value_or(def.id);
        const std::string cat_str = s.get("Category").value_or("mineral");
        const auto cat = resource_category_from_name(cat_str);
        if (!cat) throw std::runtime_error(where + " (" + def.id + "): unknown Category " + cat_str);
        def.category = *cat;
        def.profession = s.get("Profession").value_or(
            (def.category == ResourceCategory::organic || def.category == ResourceCategory::flora) ? "foraging"
            : def.category == ResourceCategory::salvage                                          ? "salvaging"
                                                                                                 : "surveying");
        def.stats[size_t(ResourceStat::quality)] = std::clamp(int(s.number("Quality", 500)), 0, 1000);
        def.stats[size_t(ResourceStat::conductivity)] = std::clamp(int(s.number("Conductivity", 500)), 0, 1000);
        def.stats[size_t(ResourceStat::toughness)] = std::clamp(int(s.number("Toughness", 500)), 0, 1000);
        def.stats[size_t(ResourceStat::malleability)] = std::clamp(int(s.number("Malleability", 500)), 0, 1000);
        def.stats[size_t(ResourceStat::purity)] = std::clamp(int(s.number("Purity", 500)), 0, 1000);
        def.seed = uint32_t(std::max(1.0, s.number("Seed", double(resources_.size() + 1))));
        def.peak_x = s.number("PeakX", -3500.0);
        def.peak_z = s.number("PeakZ", -4800.0);
        def.radius = std::max(25.0, s.number("Radius", 350.0));
        resources_.push_back(std::move(def));
    }
}

void CraftingBook::parse_schematics(const Ini& ini, const std::string& where) {
    schematics_.clear();
    work_orders_.clear();
    for (const Ini::Section& s : ini.sections()) {
        if (s.name.empty() && s.values.empty()) continue;
        if (same(s.name, "schematic")) {
            SchematicDef def;
            def.id = s.get("Id").value_or("");
            if (def.id.empty()) throw std::runtime_error(where + ": [schematic] missing Id");
            if (find_schematic(def.id) != nullptr) throw std::runtime_error(where + ": duplicate schematic " + def.id);
            def.name = s.get("Name").value_or(def.id);
            def.profession = s.get("Profession").value_or("weaponsmith");
            def.required_level = std::clamp(int(s.number("RequiredLevel", 1)), 1, 100);
            def.output_item = s.get("Output").value_or("");
            if (def.output_item.empty()) throw std::runtime_error(where + " (" + def.id + "): missing Output");
            def.output_count = std::max(1, int(s.number("OutputCount", 1)));
            def.base_xp = std::max<int64_t>(10, int64_t(s.number("BaseXP", 60)));
            def.enhancement_slots = std::clamp(int(s.number("Enhancements", 1)), 0, 2);
            for (const auto& [k, v] : s.values) {
                if (same(k, "Slot")) {
                    std::istringstream fields(v);
                    std::string cat_str;
                    int amount = 0;
                    if (!(fields >> cat_str >> amount) || amount <= 0) {
                        throw std::runtime_error(where + " (" + def.id + "): bad Slot '" + v + "'");
                    }
                    const auto cat = resource_category_from_name(cat_str);
                    if (!cat) throw std::runtime_error(where + " (" + def.id + "): unknown Slot category " + cat_str);
                    def.slots.push_back({*cat, amount});
                } else if (same(k, "Weight")) {
                    std::istringstream fields(v);
                    std::string stat_str;
                    double w = 0;
                    if (!(fields >> stat_str >> w) || w < 0) {
                        throw std::runtime_error(where + " (" + def.id + "): bad Weight '" + v + "'");
                    }
                    const auto st = resource_stat_from_name(stat_str);
                    if (!st) throw std::runtime_error(where + " (" + def.id + "): unknown Weight stat " + stat_str);
                    def.weights.push_back({*st, w});
                }
            }
            if (def.slots.empty()) throw std::runtime_error(where + " (" + def.id + "): needs at least one Slot");
            if (def.weights.empty()) def.weights.push_back({ResourceStat::quality, 1.0});
            schematics_.push_back(std::move(def));
        } else if (same(s.name, "work_order")) {
            WorkOrder wo;
            wo.id = s.get("Id").value_or("");
            if (wo.id.empty()) throw std::runtime_error(where + ": [work_order] missing Id");
            wo.title = s.get("Title").value_or(wo.id);
            wo.profession = s.get("Profession").value_or("weaponsmith");
            wo.output_item = s.get("Output").value_or("");
            wo.count = std::max(1, int(s.number("Count", 1)));
            wo.reward_credits = std::max<int64_t>(0, int64_t(s.number("Credits", 100)));
            wo.reward_fp = std::max<int64_t>(0, int64_t(s.number("FactionPoints", 25)));
            wo.reward_xp = std::max<int64_t>(0, int64_t(s.number("XP", 120)));
            work_orders_.push_back(std::move(wo));
        } else {
            throw std::runtime_error(where + ": unknown section [" + s.name + "]");
        }
    }
}

const ResourceDef* CraftingBook::find_resource(std::string_view id) const {
    for (const ResourceDef& r : resources_) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

const SchematicDef* CraftingBook::find_schematic(std::string_view id) const {
    for (const SchematicDef& s : schematics_) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

const WorkOrder* CraftingBook::find_work_order(std::string_view id) const {
    for (const WorkOrder& wo : work_orders_) {
        if (wo.id == id) return &wo;
    }
    return nullptr;
}

double CraftingBook::concentration_at(const ResourceDef& def, double x, double z) const {
    const double dx = x - def.peak_x;
    const double dz = z - def.peak_z;
    const double d2 = (dx * dx + dz * dz) / (def.radius * def.radius);
    const double primary = std::exp(-d2);
    const double p1 = double(def.seed % 17) * 0.37;
    const double p2 = double(def.seed % 31) * 0.23;
    const double wave = 0.5 + 0.5 * std::sin(x * 0.022 + p1) * std::cos(z * 0.022 + p2);
    const double ridge = 0.5 + 0.5 * std::sin((x + z) * 0.011 + p2);
    return std::clamp(0.10 + 0.65 * primary + 0.17 * wave + 0.08 * ridge, 0.05, 0.99);
}

std::pair<const ResourceDef*, double> CraftingBook::best_resource_at(ResourceCategory category, double x, double z) const {
    const ResourceDef* best = nullptr;
    double best_c = -1.0;
    for (const ResourceDef& r : resources_) {
        if (r.category != category) continue;
        const double c = concentration_at(r, x, z);
        if (c > best_c) {
            best_c = c;
            best = &r;
        }
    }
    return {best, std::max(0.0, best_c)};
}

ScanResult CraftingBook::scan_terrain(ResourceCategory category, double x, double z, double radius, int grid_n) const {
    ScanResult out;
    grid_n = std::clamp(grid_n, 5, 33);
    out.grid_n = grid_n;
    out.step = (2.0 * radius) / double(grid_n - 1);
    const auto [best, conc] = best_resource_at(category, x, z);
    if (best == nullptr) {
        out.grid.assign(size_t(grid_n * grid_n), 0.0f);
        return out;
    }
    out.best_resource_id = best->id;
    out.best_resource_name = best->name;
    out.concentration = conc;
    out.peak_x = best->peak_x;
    out.peak_z = best->peak_z;
    out.grid.reserve(size_t(grid_n * grid_n));
    for (int gz = 0; gz < grid_n; ++gz) {
        const double wz = z - radius + double(gz) * out.step;
        for (int gx = 0; gx < grid_n; ++gx) {
            const double wx = x - radius + double(gx) * out.step;
            out.grid.push_back(float(concentration_at(*best, wx, wz)));
        }
    }
    return out;
}

std::vector<std::string> CraftingBook::auto_fill(const SchematicDef& schematic, std::span<const MaterialStack> vault,
                                                 AutoFillMode mode) const {
    std::vector<std::string> chosen(schematic.slots.size());
    std::vector<std::pair<std::string, int64_t>> available;
    available.reserve(vault.size());
    for (const MaterialStack& st : vault) {
        if (st.amount > 0) available.emplace_back(st.resource_id, st.amount);
    }
    const auto avail_of = [&](std::string_view id) -> int64_t {
        for (const auto& [rid, amt] : available) {
            if (rid == id) return amt;
        }
        return 0;
    };
    const auto consume = [&](std::string_view id, int64_t amt) {
        for (auto& [rid, cur] : available) {
            if (rid == id) {
                cur = std::max<int64_t>(0, cur - amt);
                return;
            }
        }
    };
    const auto score_of = [&](const ResourceDef& r, int64_t stock) {
        if (mode == AutoFillMode::cheapest) {
            // Prefer abundant lower-quality materials to save high-grade stock.
            return double(stock) * 0.01 - double(r.stats[size_t(ResourceStat::quality)]) * 0.001;
        }
        if (mode == AutoFillMode::best_quality) {
            return double(r.stats[size_t(ResourceStat::quality)]);
        }
        double total = 0.0, wsum = 0.0;
        for (const StatWeight& w : schematic.weights) {
            total += double(r.stats[size_t(w.stat)]) * w.weight;
            wsum += w.weight;
        }
        return wsum > 0.0 ? total / wsum : double(r.stats[size_t(ResourceStat::quality)]);
    };

    for (size_t i = 0; i < schematic.slots.size(); ++i) {
        const SchematicSlot& slot = schematic.slots[i];
        const ResourceDef* best = nullptr;
        double best_score = -1e18;
        for (const ResourceDef& r : resources_) {
            if (r.category != slot.category) continue;
            const int64_t stock = avail_of(r.id);
            if (stock < slot.amount) continue;
            const double sc = score_of(r, stock);
            if (sc > best_score) {
                best_score = sc;
                best = &r;
            }
        }
        if (best != nullptr) {
            chosen[i] = best->id;
            consume(best->id, slot.amount);
        }
    }
    return chosen;
}

ExperimentOdds CraftingBook::experiment_odds(int crafter_level) {
    const double t = std::clamp(double(crafter_level - 1) / 99.0, 0.0, 1.0);
    const double exp_bonus = crafter_level >= 75 ? 0.05 : 0.0;  // Lv 75 Experimentation milestone
    ExperimentOdds o;
    o.great_success = std::clamp(0.10 + 0.25 * t + exp_bonus, 0.05, 0.45);
    o.minor_flaw = std::clamp(0.15 - 0.12 * t - exp_bonus * 0.4, 0.02, 0.20);
    o.neutral = std::clamp(0.25 - 0.17 * t, 0.06, 0.30);
    o.success = std::max(0.10, 1.0 - o.great_success - o.neutral - o.minor_flaw);
    return o;
}

CraftPreview CraftingBook::preview_craft(const SchematicDef& schematic, std::span<const std::string> resource_ids,
                                         int enhancement_count, int crafter_level, uint8_t exp_quality_pct) const {
    double weighted_sum = 0.0;
    double weight_total = 0.0;
    for (size_t i = 0; i < schematic.slots.size(); ++i) {
        const std::string& rid = i < resource_ids.size() ? resource_ids[i] : std::string();
        const ResourceDef* r = rid.empty() ? nullptr : find_resource(rid);
        double slot_score = 500.0;
        if (r != nullptr) {
            double num = 0.0, den = 0.0;
            for (const StatWeight& w : schematic.weights) {
                num += double(r->stats[size_t(w.stat)]) * w.weight;
                den += w.weight;
            }
            slot_score = den > 0.0 ? num / den : double(r->stats[size_t(ResourceStat::quality)]);
        }
        const double amt_w = double(std::max(1, schematic.slots[i].amount));
        weighted_sum += (slot_score / 1000.0) * amt_w;
        weight_total += amt_w;
    }
    const double mat_score = weight_total > 0.0 ? weighted_sum / weight_total : 0.5;
    const double level_bonus = std::clamp(double(crafter_level - 1) / 99.0, 0.0, 1.0) * 0.12;
    const double enh_bonus = std::clamp(double(enhancement_count), 0.0, 2.0) * 0.08;
    const double focus = double(std::clamp<int>(exp_quality_pct, 0, 100)) / 100.0;

    CraftPreview out;
    out.base_quality = std::clamp(0.22 + 0.58 * mat_score + level_bonus + enh_bonus, 0.05, 0.95);
    out.min_quality = std::clamp(out.base_quality - 0.06, 0.02, 0.95);
    out.max_quality = std::clamp(out.base_quality + 0.08 + 0.10 * focus, out.base_quality, 1.0);
    out.odds = experiment_odds(crafter_level);
    return out;
}

std::pair<double, std::string> CraftingBook::roll_craft_quality(const CraftPreview& preview, double unit_roll) {
    const double r = std::clamp(unit_roll, 0.0, 0.999999);
    const ExperimentOdds& o = preview.odds;
    if (r < o.great_success) {
        return {preview.max_quality, "Great Success"};
    }
    if (r < o.great_success + o.success) {
        return {std::clamp((preview.base_quality + preview.max_quality) * 0.5, 0.0, 1.0), "Success"};
    }
    if (r < o.great_success + o.success + o.neutral) {
        return {preview.base_quality, "Neutral"};
    }
    return {preview.min_quality, "Minor Flaw"};
}

CraftingBook::SalvageResult CraftingBook::salvage_yield(const ItemDef& def, int salvaging_level) const {
    SalvageResult res;
    const double mult = 1.0 + std::clamp(double(salvaging_level - 1) / 99.0, 0.0, 1.0) * 0.75 +
                        (salvaging_level >= 50 ? 0.25 : 0.0);
    const int64_t base_qty = def.kind == ItemKind::junk ? 3 : 6;
    const int64_t qty = std::max<int64_t>(2, int64_t(std::llround(double(base_qty) * mult)));

    const ResourceCategory wanted =
        (def.kind == ItemKind::junk && def.id.find("hide") != std::string::npos) ||
                def.id.find("fur") != std::string::npos || def.id.find("bone") != std::string::npos ||
                def.id.find("shell") != std::string::npos || def.id.find("scale") != std::string::npos ||
                def.id.find("skull") != std::string::npos || def.id.find("meat") != std::string::npos
            ? ResourceCategory::organic
            : ResourceCategory::salvage;

    const ResourceDef* chosen = nullptr;
    for (const ResourceDef& r : resources_) {
        if (r.category == wanted) {
            chosen = &r;
            break;
        }
    }
    if (chosen == nullptr && !resources_.empty()) {
        chosen = &resources_.front();
    }
    if (chosen != nullptr) {
        res.materials.push_back({chosen->id, qty});
    }
    res.xp = def.kind == ItemKind::junk ? 18 : 35;
    return res;
}

int64_t material_amount(std::span<const MaterialStack> vault, std::string_view resource_id) {
    for (const MaterialStack& m : vault) {
        if (m.resource_id == resource_id) return m.amount;
    }
    return 0;
}

void add_material(std::vector<MaterialStack>& vault, std::string_view resource_id, int64_t delta) {
    if (resource_id.empty() || delta == 0) return;
    for (size_t i = 0; i < vault.size(); ++i) {
        if (vault[i].resource_id == resource_id) {
            vault[i].amount += delta;
            if (vault[i].amount <= 0) {
                vault.erase(vault.begin() + ptrdiff_t(i));
            }
            return;
        }
    }
    if (delta > 0) {
        vault.push_back({std::string(resource_id), delta});
    }
}

} // namespace swm::core
