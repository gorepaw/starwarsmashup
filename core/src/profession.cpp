#include "swm/core/profession.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace swm::core {

namespace {

constexpr std::array<std::string_view, 6> category_names{"armor", "weapon", "gathering",
                                                         "crafting", "specialist", "companion"};

bool same(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

bool truthy(std::string_view v) {
    return same(v, "true") || same(v, "yes") || v == "1";
}

std::vector<std::string> split_list(std::string_view text) {
    std::vector<std::string> out;
    std::string item;
    for (const char c : text) {
        if (c == ',' || c == ' ' || c == '\t') {
            if (!item.empty()) out.push_back(std::move(item));
            item.clear();
        } else {
            item += c;
        }
    }
    if (!item.empty()) out.push_back(std::move(item));
    return out;
}

WeaponModifier parse_weapon_modifier(const std::string& text, const std::string& where) {
    std::istringstream fields(text);
    std::string name, op;
    double value = 0;
    if (!(fields >> name >> op >> value) || op != "*") {
        throw std::runtime_error(where + ": Weapon is \"<spread|heat|reload|recovery|range|speed|lunge|disrupt> * <n>\", not \"" + text + "\"");
    }
    static constexpr std::array<std::pair<std::string_view, WeaponModifier::Field>, 8> fields_by_name{{
        {"spread", WeaponModifier::spread},
        {"heat", WeaponModifier::heat},
        {"reload", WeaponModifier::reload},
        {"recovery", WeaponModifier::recovery},
        {"range", WeaponModifier::range},
        {"speed", WeaponModifier::speed},
        {"lunge", WeaponModifier::lunge},
        {"disrupt", WeaponModifier::disrupt},
    }};
    for (const auto& [n, f] : fields_by_name) {
        if (same(n, name)) {
            return {f, value};
        }
    }
    throw std::runtime_error(where + ": unknown weapon handling " + name);
}

// "Modifier = <stat> ..." and "Weapon = <field> * <n>" lines with a prefix
// ("" for a milestone's, "Rank" for the rank's).
void read_perk(const Ini::Section& section, std::string_view prefix, ProfessionPerk& perk, const std::string& where) {
    for (const auto& [key, value] : section.values) {
        if (!(key.size() > prefix.size() && same(std::string_view(key).substr(0, prefix.size()), prefix))) {
            continue;
        }
        const std::string_view rest = std::string_view(key).substr(prefix.size());
        if (same(rest, "Modifier")) {
            perk.stats.push_back(parse_stat_modifier(value, where));
        } else if (same(rest, "Weapon")) {
            perk.weapons.push_back(parse_weapon_modifier(value, where));
        }
    }
}

void add_perk(ProfessionPerk& to, const ProfessionPerk& from, int times = 1) {
    for (int i = 0; i < times; ++i) {
        to.stats.insert(to.stats.end(), from.stats.begin(), from.stats.end());
        to.weapons.insert(to.weapons.end(), from.weapons.begin(), from.weapons.end());
    }
}

void apply_weapon(WeaponDef& w, const WeaponModifier& m) {
    switch (m.field) {
    case WeaponModifier::spread:
        w.yaw_spread *= m.multiply;
        w.pitch_spread *= m.multiply;
        w.spread_per_shot *= m.multiply;
        break;
    case WeaponModifier::heat:
        w.heat_per_shot *= m.multiply;
        break;
    case WeaponModifier::reload:
        w.reload_time *= m.multiply;
        break;
    case WeaponModifier::recovery:
        w.spread_recover_rate *= m.multiply;
        break;
    case WeaponModifier::range:
        w.damage_transition_delay *= m.multiply;
        break;
    case WeaponModifier::speed:
        w.shot_delay *= m.multiply;
        break;
    case WeaponModifier::lunge:
        w.melee_lunge *= m.multiply;
        break;
    case WeaponModifier::disrupt:
        w.shield_disrupt *= m.multiply;
        break;
    }
}

int armor_weight(std::string_view armor_class) {
    if (armor_class == "light") return 1;
    if (armor_class == "medium") return 2;
    if (armor_class == "heavy") return 3;
    return 0;
}

} // namespace

std::optional<ProfessionCategory> profession_category_from_name(std::string_view name) {
    for (size_t i = 0; i < category_names.size(); ++i) {
        if (same(category_names[i], name)) {
            return ProfessionCategory(i);
        }
    }
    return std::nullopt;
}

std::string_view profession_category_name(ProfessionCategory category) {
    return category_names[size_t(category)];
}

void ProfessionBook::load(const std::filesystem::path& path) {
    parse(Ini::load(path), path.string());
}

void ProfessionBook::parse(const Ini& ini, const std::string& where) {
    professions_.clear();
    for (const Ini::Section& s : ini.sections()) {
        if (s.name.empty() && s.values.empty()) {
            continue;  // before the first section
        }
        if (same(s.name, "curve")) {
            curve_base_ = int64_t(s.number("Base", double(curve_base_)));
            curve_per_level_ = int64_t(s.number("PerLevel", double(curve_per_level_)));
            if (curve_base_ <= 0 || curve_per_level_ < 0) {
                throw std::runtime_error(where + ": [curve] needs Base > 0 and PerLevel >= 0");
            }
        } else if (same(s.name, "profession")) {
            ProfessionDef def;
            def.id = s.get("Id").value_or("");
            if (def.id.empty()) {
                throw std::runtime_error(where + ": a [profession] without an Id");
            }
            if (find(def.id) != nullptr) {
                throw std::runtime_error(where + ": profession " + def.id + " twice");
            }
            const std::string at = where + " (" + def.id + ")";
            def.name = s.get("Name").value_or(def.id);
            def.description = s.get("Description").value_or("");
            const std::string category = s.get("Category").value_or("");
            const auto cat = profession_category_from_name(category);
            if (!cat) {
                throw std::runtime_error(at + ": unknown Category " + category);
            }
            def.category = *cat;
            def.wave = s.get("Wave").value_or("7A");
            def.available = truthy(s.get("Available").value_or("true"));
            def.slotted = truthy(s.get("Slotted").value_or("false"));
            if (const auto a = s.get("Archetypes")) {
                def.archetypes = split_list(*a);
                for (const std::string& id : def.archetypes) {
                    if (!weapon_archetype_from_name(id)) {
                        throw std::runtime_error(at + ": unknown weapon archetype " + id);
                    }
                }
            }
            def.armor_class = s.get("ArmorClass").value_or("");
            if (def.category == ProfessionCategory::armor && def.armor_class != "none" &&
                armor_weight(def.armor_class) == 0) {
                throw std::runtime_error(at + ": an armor profession's ArmorClass is none, light, medium or heavy");
            }
            def.earns = s.get("Earns").value_or("");
            def.rank_every = int(s.number("RankEvery", 0));
            read_perk(s, "Rank", def.rank, at);
            if (def.rank_every <= 0 && !def.rank.empty()) {
                throw std::runtime_error(at + ": rank perks need RankEvery");
            }
            professions_.push_back(std::move(def));
        } else if (same(s.name, "milestone")) {
            if (professions_.empty()) {
                throw std::runtime_error(where + ": a [milestone] before any [profession]");
            }
            ProfessionDef& def = professions_.back();
            ProfessionMilestone m;
            m.level = int(s.number("Level", 0));
            if (m.level < 1 || m.level > profession_max_level) {
                throw std::runtime_error(where + " (" + def.id + "): a milestone's Level is 1 to 100");
            }
            m.name = s.get("Name").value_or("");
            m.description = s.get("Description").value_or("");
            read_perk(s, "", m.perk, where + " (" + def.id + ")");
            def.milestones.push_back(std::move(m));
        } else {
            throw std::runtime_error(where + ": unknown section [" + s.name + "]");
        }
    }
    for (ProfessionDef& def : professions_) {
        std::stable_sort(def.milestones.begin(), def.milestones.end(),
                         [](const ProfessionMilestone& a, const ProfessionMilestone& b) { return a.level < b.level; });
    }
}

const ProfessionDef* ProfessionBook::find(std::string_view id) const {
    for (const ProfessionDef& def : professions_) {
        if (def.id == id) return &def;
    }
    return nullptr;
}

const ProfessionDef* ProfessionBook::for_archetype(std::string_view archetype) const {
    if (archetype.empty()) return nullptr;
    for (const ProfessionDef& def : professions_) {
        if (std::find(def.archetypes.begin(), def.archetypes.end(), archetype) != def.archetypes.end()) {
            return &def;
        }
    }
    return nullptr;
}

const ProfessionDef* ProfessionBook::for_armor(std::string_view armor_class) const {
    for (const ProfessionDef& def : professions_) {
        if (def.category == ProfessionCategory::armor && def.armor_class == armor_class) {
            return &def;
        }
    }
    return nullptr;
}

int64_t ProfessionBook::xp_to_next(int level) const {
    if (level >= profession_max_level) return 0;
    return curve_base_ + curve_per_level_ * int64_t(std::max(0, level - 1));
}

int64_t ProfessionBook::xp_for_level(int level) const {
    level = std::clamp(level, 1, profession_max_level);
    const int64_t steps = level - 1;  // levels gained from 1
    return curve_base_ * steps + curve_per_level_ * steps * (steps - 1) / 2;
}

int ProfessionBook::level_for(int64_t xp) const {
    int level = 1;
    while (level < profession_max_level && xp >= xp_for_level(level + 1)) {
        ++level;
    }
    return level;
}

int64_t profession_xp(std::span<const ProfessionProgress> progress, std::string_view id) {
    for (const ProfessionProgress& p : progress) {
        if (p.id == id) return p.xp;
    }
    return 0;
}

int64_t add_profession_xp(std::vector<ProfessionProgress>& progress, std::string_view id, int64_t xp) {
    for (ProfessionProgress& p : progress) {
        if (p.id == id) {
            p.xp = std::max<int64_t>(0, p.xp + xp);
            return p.xp;
        }
    }
    progress.push_back({std::string(id), std::max<int64_t>(0, xp)});
    return progress.back().xp;
}

bool can_specialize(const ProfessionBook& book, std::string_view id) {
    const ProfessionDef* def = book.find(id);
    return def != nullptr && def->available && def->slotted;
}

std::vector<std::string> clean_specializations(const ProfessionBook& book, std::span<const std::string> ids) {
    std::vector<std::string> out;
    for (const std::string& id : ids) {
        if (int(out.size()) >= specialization_slots) break;
        if (can_specialize(book, id) && std::find(out.begin(), out.end(), id) == out.end()) {
            out.push_back(id);
        }
    }
    return out;
}

std::vector<std::string> routed_professions(const ProfessionBook& book, const Loadout& loadout) {
    std::vector<std::string> out;
    for (const int slot : {primary_slot, sidearm_slot}) {
        if (const WeaponDef* w = weapon_in_slot(loadout, slot)) {
            if (const ProfessionDef* def = book.for_archetype(w->archetype)) {
                out.push_back(def->id);
                break;
            }
        }
    }
    if (const ProfessionDef* def = book.for_armor(loadout.armor_class.empty() ? "none" : loadout.armor_class)) {
        out.push_back(def->id);
    }
    return out;
}

int combat_level(const ProfessionBook& book, const Loadout& loadout, std::span<const ProfessionProgress> progress) {
    int level = 1;
    for (const std::string& id : routed_professions(book, loadout)) {
        level = std::max(level, book.level_for(profession_xp(progress, id)));
    }
    return level;
}

int64_t milestone_cost(int level) {
    if (level <= 1) return 0;
    if (level <= 25) return 500;
    if (level <= 50) return 2500;
    if (level <= 75) return 7500;
    return 20000;
}

ProfessionPerk profession_perks(const ProfessionDef& def, int level, int trained) {
    ProfessionPerk perk;
    if (def.rank_every > 0) {
        add_perk(perk, def.rank, level / def.rank_every);
    }
    for (const ProfessionMilestone& m : def.milestones) {
        if (m.level <= level && m.level <= trained) {
            add_perk(perk, m.perk);
        }
    }
    return perk;
}

void apply_specializations(Loadout& loadout, const ProfessionBook& book, std::span<const std::string> specializations,
                           std::span<const ProfessionProgress> progress) {
    Gear perks{"specializations", "specialization", {}};
    const std::string armor = loadout.armor_class.empty() ? "none" : loadout.armor_class;
    for (const std::string& id : clean_specializations(book, specializations)) {
        const ProfessionDef* def = book.find(id);
        if (def->category == ProfessionCategory::armor && def->armor_class != armor) {
            continue;  // trained for other armor than this loadout's
        }
        int trained = 0;
        for (const ProfessionProgress& p : progress) {
            if (p.id == id) trained = p.trained;
        }
        const ProfessionPerk perk = profession_perks(*def, book.level_for(profession_xp(progress, id)), trained);
        perks.modifiers.insert(perks.modifiers.end(), perk.stats.begin(), perk.stats.end());
        if (perk.weapons.empty()) continue;
        for (std::optional<WeaponDef>& w : loadout.weapons) {
            if (!w || std::find(def->archetypes.begin(), def->archetypes.end(), w->archetype) == def->archetypes.end()) {
                continue;
            }
            for (const WeaponModifier& m : perk.weapons) {
                apply_weapon(*w, m);
            }
        }
    }
    if (!perks.modifiers.empty()) {
        loadout.gear.push_back(std::move(perks));
    }
}

std::string loadout_armor_class(std::span<const std::string> piece_classes) {
    std::array<int, 4> counts{};
    for (const std::string& c : piece_classes) {
        ++counts[size_t(armor_weight(c))];
    }
    // The most pieces; on a tie the heavier (the loop keeps the later, heavier one).
    int best = 0, most = 0;
    for (int w = 1; w <= 3; ++w) {
        if (counts[size_t(w)] > 0 && counts[size_t(w)] >= most) {
            most = counts[size_t(w)];
            best = w;
        }
    }
    return best == 0 ? "none" : best == 1 ? "light" : best == 2 ? "medium" : "heavy";
}

} // namespace swm::core
