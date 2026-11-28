#include "swm/core/crafting.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <set>
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
    const Ini ini = Ini::load(path);
    for (const Ini::Section& s : ini.sections()) {
        if (!same(s.name, "catalog")) continue;
        const auto rel = s.get("Path");
        if (!rel) throw std::runtime_error(path.string() + ": [catalog] needs a Path");
        ItemCatalog catalog;
        catalog.load(path.parent_path() / *rel);
        parse_schematics(ini, path.string(), &catalog);
        return;
    }
    parse_schematics(ini, path.string());
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

namespace {

// A comma-separated list's names, trimmed.
std::vector<std::string> names_of(std::string_view list) {
    std::vector<std::string> out;
    std::stringstream parts{std::string(list)};
    for (std::string part; std::getline(parts, part, ',');) {
        part.erase(0, part.find_first_not_of(" \t"));
        part.erase(part.find_last_not_of(" \t") + 1);
        if (!part.empty()) out.push_back(part);
    }
    return out;
}

// A glob with '*' for any run of characters ("armor_ubese_*").
bool glob_match(std::string_view pattern, std::string_view text) {
    if (pattern.empty()) return text.empty();
    if (pattern.front() == '*') {
        for (size_t i = 0; i <= text.size(); ++i) {
            if (glob_match(pattern.substr(1), text.substr(i))) return true;
        }
        return false;
    }
    return !text.empty() && pattern.front() == text.front() && glob_match(pattern.substr(1), text.substr(1));
}

bool any_glob(const std::vector<std::string>& patterns, std::string_view text) {
    return std::any_of(patterns.begin(), patterns.end(), [&](const std::string& p) { return glob_match(p, text); });
}

} // namespace

bool item_takes_marks(const ItemDef& def) {
    return def.kind == ItemKind::weapon || def.kind == ItemKind::armor || def.kind == ItemKind::deflector_shield;
}

int craft_max_mark(int crafter_level) { return std::clamp(crafter_level / 10, 0, max_mark); }

int mark_material_amount(int amount, int mark) {
    return int(std::ceil(double(amount) * (1.0 + 0.25 * double(clamp_mark(mark))) - 1e-9));
}

double focus_material_mult(int focus_pct) { return 1.0 + 9.0 * double(std::clamp(focus_pct, 0, 100)) / 100.0; }

int craft_material_amount(int amount, int mark, int focus_pct) {
    return int(std::ceil(double(mark_material_amount(amount, mark)) * focus_material_mult(focus_pct) - 1e-9));
}

int64_t mark_craft_xp(int64_t base_xp, int mark) {
    return int64_t(std::llround(double(base_xp) * (1.0 + 0.15 * double(clamp_mark(mark)))));
}

bool dye_values_ok(std::string_view dye) {
    if (dye.empty()) return true;
    size_t count = 0;
    for (size_t at = 0; at <= dye.size();) {
        const size_t amp = std::min(dye.find('&', at), dye.size());
        const std::string_view pair = dye.substr(at, amp - at);
        const size_t eq = pair.find('=');
        if (eq == std::string_view::npos || eq == 0 || eq + 1 == pair.size() || ++count > max_dye_values) return false;
        for (const char c : pair.substr(0, eq)) {
            if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_' ||
                  c == '/')) {
                return false;
            }
        }
        const std::string_view index = pair.substr(eq + 1);
        if (index.size() > 3 || !std::all_of(index.begin(), index.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }) ||
            std::stoi(std::string(index)) > 255) {
            return false;
        }
        at = amp + 1;
    }
    return true;
}

void CraftingBook::parse_schematics(const Ini& ini, const std::string& where, const ItemCatalog* catalog) {
    schematics_.clear();
    // A schematic's common keys: [schematic]'s and [family]'s.
    const auto read_recipe = [&](const Ini::Section& s, SchematicDef& def) {
        def.group = s.get("Group").value_or("");
        def.profession = s.get("Profession").value_or("weaponsmith");
        def.required_level = std::clamp(int(s.number("RequiredLevel", 1)), 1, 100);
        def.blueprint = same(s.get("Blueprint").value_or("no"), "yes");
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
    };
    std::vector<const Ini::Section*> families;
    for (const Ini::Section& s : ini.sections()) {
        if (s.name.empty() && s.values.empty()) continue;
        if (same(s.name, "catalog")) continue;  // (load_schematics')
        if (same(s.name, "family")) {
            if (catalog == nullptr) throw std::runtime_error(where + ": a [family] needs a [catalog]");
            families.push_back(&s);
        } else if (same(s.name, "schematic")) {
            SchematicDef def;
            def.id = s.get("Id").value_or("");
            if (def.id.empty()) throw std::runtime_error(where + ": [schematic] missing Id");
            if (find_schematic(def.id) != nullptr) throw std::runtime_error(where + ": duplicate schematic " + def.id);
            def.name = s.get("Name").value_or(def.id);
            def.output_item = s.get("Output").value_or("");
            if (def.output_item.empty()) throw std::runtime_error(where + " (" + def.id + "): missing Output");
            read_recipe(s, def);
            schematics_.push_back(std::move(def));
        } else {
            throw std::runtime_error(where + ": unknown section [" + s.name + "]");
        }
    }
    if (catalog == nullptr) return;

    // Each family's items, in catalog order; an item made already (by a
    // [schematic], or an earlier family) keeps that.
    std::set<std::string> made;
    for (const SchematicDef& def : schematics_) made.insert(def.output_item);
    for (const Ini::Section* f : families) {
        const std::string what = where + " ([family] " + f->get("Group").value_or("?") + ")";
        const auto kind = f->get("Kind");
        if (!kind) throw std::runtime_error(what + ": needs a Kind");
        const std::vector<std::string> archetypes = names_of(f->get("Archetype").value_or(""));
        const std::vector<std::string> classes = names_of(f->get("ArmorClass").value_or(""));
        const std::vector<std::string> item_slots = names_of(f->get("ItemSlot").value_or(""));
        const std::vector<std::string> match = names_of(f->get("Match").value_or(""));
        const std::vector<std::string> exclude = names_of(f->get("Exclude").value_or(""));
        const std::vector<std::string> factions = names_of(f->get("Faction").value_or("neutral"));
        const bool by_slot = same(f->get("ScaleBySlot").value_or("no"), "yes");
        // Some of a family behind blueprints (the exotic ones), by id glob.
        const std::vector<std::string> blueprint_match = names_of(f->get("BlueprintMatch").value_or(""));
        SchematicDef recipe;
        recipe.id = what;
        read_recipe(*f, recipe);
        size_t matched = 0;
        for (const ItemDef& item : catalog->all()) {
            if (item_kind_name(item.kind) != *kind || made.contains(item.id)) continue;
            if (!archetypes.empty() &&
                std::find(archetypes.begin(), archetypes.end(), weapon_archetype_info(item.archetype).id) ==
                    archetypes.end()) {
                continue;
            }
            if (!classes.empty() && std::find(classes.begin(), classes.end(), item.armor_class) == classes.end()) continue;
            if (!item_slots.empty() && std::find(item_slots.begin(), item_slots.end(), item.slot) == item_slots.end()) {
                continue;
            }
            if (!match.empty() && !any_glob(match, item.id)) continue;
            if (any_glob(exclude, item.id)) continue;
            if (std::find(factions.begin(), factions.end(), "any") == factions.end() &&
                std::find(factions.begin(), factions.end(), item.faction) == factions.end()) {
                continue;
            }
            SchematicDef def = recipe;
            def.id = "sch_" + item.id;
            if (any_glob(blueprint_match, item.id)) def.blueprint = true;
            if (find_schematic(def.id) != nullptr) throw std::runtime_error(what + ": duplicate schematic " + def.id);
            def.name = item.display_name.empty() ? item.id : item.display_name;
            def.output_item = item.id;
            if (by_slot && item.kind == ItemKind::armor) {
                // A piece costs its slot's share of a chest's (as its stats).
                const double scale = armor_slot_share(item.slot) / armor_slot_share("chest");
                for (SchematicSlot& sl : def.slots) sl.amount = std::max(1, int(std::lround(sl.amount * scale)));
                def.base_xp = std::max<int64_t>(10, int64_t(std::llround(double(def.base_xp) * std::max(0.4, scale))));
            }
            made.insert(item.id);
            schematics_.push_back(std::move(def));
            ++matched;
        }
        if (matched == 0) throw std::runtime_error(what + ": matches no catalog item");
    }
    for (SchematicDef& def : schematics_) {
        const ItemDef* out = catalog->find(def.output_item);
        if (out == nullptr) throw std::runtime_error(where + " (" + def.id + "): no catalog item " + def.output_item);
        def.marks = item_takes_marks(*out);
        if (out->basic) def.blueprint = false;  // anyone makes the basics (catalog [basic])
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
                                                 AutoFillMode mode, int mark, int focus_pct) const {
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
        const int amount = craft_material_amount(slot.amount, mark, focus_pct);
        const ResourceDef* best = nullptr;
        double best_score = -1e18;
        for (const ResourceDef& r : resources_) {
            if (r.category != slot.category) continue;
            const int64_t stock = avail_of(r.id);
            if (stock < amount) continue;
            const double sc = score_of(r, stock);
            if (sc > best_score) {
                best_score = sc;
                best = &r;
            }
        }
        if (best != nullptr) {
            chosen[i] = best->id;
            consume(best->id, amount);
        }
    }
    return chosen;
}

ExperimentOdds CraftingBook::grade_odds(double material_score, int crafter_level, int enhancement_count,
                                        int focus_pct, double grade_bonus, double exceptional_mult) {
    // Where the roll centers, from Flawed (0) to Exceptional (3), spread
    // around it as a bell: a fresh crafter on average materials makes mostly
    // Standard; a master on great ones, two modules and full focus mostly
    // Exceptional.
    const double t = std::clamp(double(crafter_level - 1) / 99.0, 0.0, 1.0);
    const double center = 0.3 + 1.2 * std::clamp(material_score, 0.0, 1.0) + 0.6 * t +
                          0.2 * std::clamp(enhancement_count, 0, 2) + 0.8 * double(std::clamp(focus_pct, 0, 100)) / 100.0 +
                          std::max(0.0, grade_bonus);  // crafting perks' Experimentation
    constexpr double spread = 0.75;
    std::array<double, 4> w{};
    double total = 0.0;
    for (size_t k = 0; k < w.size(); ++k) {
        const double d = double(k) - center;
        w[k] = std::exp(-d * d / (2.0 * spread * spread));
        total += w[k];
    }
    ExperimentOdds odds{w[0] / total, w[1] / total, w[2] / total, w[3] / total};
    if (exceptional_mult > 1.0 && odds.exceptional < 1.0) {
        // More Exceptional, the rest in proportion.
        const double ex = std::min(1.0, odds.exceptional * exceptional_mult);
        const double rest = (1.0 - ex) / (1.0 - odds.exceptional);
        odds = {odds.flawed * rest, odds.standard * rest, odds.fine * rest, ex};
    }
    return odds;
}

CraftPreview CraftingBook::preview_craft(const SchematicDef& schematic, std::span<const std::string> resource_ids,
                                         int enhancement_count, int crafter_level, uint8_t focus_pct,
                                         double grade_bonus, double exceptional_mult) const {
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
    CraftPreview out;
    out.material_score = weight_total > 0.0 ? weighted_sum / weight_total : 0.5;
    out.material_mult = focus_material_mult(focus_pct);
    out.odds = grade_odds(out.material_score, crafter_level, enhancement_count, focus_pct, grade_bonus, exceptional_mult);
    return out;
}

ItemGrade CraftingBook::roll_craft_grade(const CraftPreview& preview, double unit_roll) {
    // The best grade on the lowest rolls.
    const double r = std::clamp(unit_roll, 0.0, 0.999999);
    const ExperimentOdds& o = preview.odds;
    if (r < o.exceptional) return ItemGrade::exceptional;
    if (r < o.exceptional + o.fine) return ItemGrade::fine;
    if (r < o.exceptional + o.fine + o.standard) return ItemGrade::standard;
    return ItemGrade::flawed;
}

CraftingBook::SalvageResult CraftingBook::salvage_yield(const ItemDef& def, int salvaging_level, double yield_bonus) const {
    SalvageResult res;
    const double mult = (1.0 + std::clamp(double(salvaging_level - 1) / 99.0, 0.0, 1.0) * 0.75) * (1.0 + std::max(0.0, yield_bonus));
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

std::string_view station_for(std::string_view profession) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 8> stations{{
        {"weaponsmith", "weapon"},  {"droid_engineer", "weapon"}, {"armorsmith", "clothing"},
        {"tailor", "clothing"},     {"chef", "food"},             {"doctor", "food"},
        {"bio_engineer", "food"},   {"architect", "structure"},
    }};
    for (const auto& [prof, kind] : stations) {
        if (prof == profession) return kind;
    }
    return "general";
}

bool station_makes(std::string_view station_kind, std::string_view profession) {
    if (station_kind == "general") return true;
    const std::string_view wanted = station_for(profession);
    return wanted == "general" || wanted == station_kind;
}

std::string_view station_name(std::string_view station_kind) {
    if (station_kind == "weapon") return "Weapon & Droid Crafting Station";
    if (station_kind == "clothing") return "Armor & Clothing Crafting Station";
    if (station_kind == "food") return "Food & Chemical Crafting Station";
    if (station_kind == "structure") return "Structure & Machinery Crafting Station";
    return "crafting station";
}

} // namespace swm::core
