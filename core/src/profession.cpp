#include "swm/core/profession.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
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
    static constexpr std::array<std::pair<std::string_view, WeaponModifier::Field>, size_t(WeaponModifier::last_field) + 1>
        fields_by_name{{
            {"spread", WeaponModifier::spread},
            {"heat", WeaponModifier::heat},
            {"reload", WeaponModifier::reload},
            {"recovery", WeaponModifier::recovery},
            {"range", WeaponModifier::range},
            {"speed", WeaponModifier::speed},
            {"lunge", WeaponModifier::lunge},
            {"disrupt", WeaponModifier::disrupt},
            {"velocity", WeaponModifier::velocity},
            {"headshot", WeaponModifier::headshot},
            {"crit", WeaponModifier::crit},
            {"vehicle", WeaponModifier::vehicle},
            {"shield", WeaponModifier::shield},
            {"swap", WeaponModifier::swap},
            {"recoil", WeaponModifier::recoil},
            {"aim", WeaponModifier::aim},
            {"reach", WeaponModifier::reach},
            {"bloom", WeaponModifier::bloom},
            {"body", WeaponModifier::body},
            {"blast", WeaponModifier::blast},
            {"blast_damage", WeaponModifier::blast_damage},
            {"effect", WeaponModifier::effect},
            {"carry", WeaponModifier::carry},
            {"holster_cool", WeaponModifier::holster_cool},
            {"flank", WeaponModifier::flank},
            {"guard", WeaponModifier::guard},
            {"block", WeaponModifier::block},
            {"parry", WeaponModifier::parry},
            {"combo", WeaponModifier::combo},
            {"ricochet", WeaponModifier::ricochet},
            {"pierce", WeaponModifier::pierce},
            {"proximity", WeaponModifier::proximity},
            {"deflect", WeaponModifier::deflect},
            {"knockback", WeaponModifier::knockback},
            {"sprint_reload", WeaponModifier::sprint_reload},
            {"roll_fire", WeaponModifier::roll_fire},
            {"holster_reload", WeaponModifier::holster_reload},
            {"riposte", WeaponModifier::riposte},
            {"unblockable", WeaponModifier::unblockable},
        }};
    std::istringstream fields(text);
    std::string name, op;
    double value = 0;
    if (!(fields >> name >> op >> value) || (op != "*" && op != "+")) {
        throw std::runtime_error(where + ": Weapon is \"<field> * <n>\" (or \"<field> + <n>\" for ricochet, pierce, proximity, deflect, knockback and the switches), not \"" + text + "\"");
    }
    for (const auto& [n, f] : fields_by_name) {
        if (same(n, name)) {
            if (WeaponModifier::additive(f) != (op == "+")) {
                throw std::runtime_error(where + ": weapon handling " + name + " is " +
                                         (WeaponModifier::additive(f) ? "added (+)" : "multiplied (*)"));
            }
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

std::string_view perk_pool_name(PerkPool pool) {
    return pool == PerkPool::combat ? "combat" : "noncombat";
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
            // Its pool: weapons and armor fight; gathering and crafting don't;
            // a specialist or companion profession says (Pool = combat or noncombat).
            const bool fights = def.category == ProfessionCategory::weapon || def.category == ProfessionCategory::armor ||
                                def.category == ProfessionCategory::specialist ||
                                def.category == ProfessionCategory::companion;
            const std::string pool = s.get("Pool").value_or(fights ? "combat" : "noncombat");
            if (pool != "combat" && pool != "noncombat") {
                throw std::runtime_error(at + ": Pool is combat or noncombat");
            }
            def.pool = pool == "combat" ? PerkPool::combat : PerkPool::noncombat;
            def.first_perk_level = int(s.number("FirstPerk", perk_levels[0]));
            if (def.first_perk_level != 1 && def.first_perk_level != perk_levels[0]) {
                throw std::runtime_error(at + ": FirstPerk is 1 or 5");
            }
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
        } else if (same(s.name, "perk")) {
            if (professions_.empty()) {
                throw std::runtime_error(where + ": a [perk] before any [profession]");
            }
            ProfessionDef& def = professions_.back();
            const std::string at = where + " (" + def.id + ")";
            ProfessionPerkDef perk;
            perk.id = s.get("Id").value_or("");
            if (perk.id.empty()) {
                throw std::runtime_error(at + ": a [perk] without an Id");
            }
            if (find_perk(perk.id) != nullptr) {
                throw std::runtime_error(at + ": perk " + perk.id + " twice");
            }
            perk.level = int(s.number("Level", 0));
            const auto tiers = def.perk_tiers();
            if (std::find(tiers.begin(), tiers.end(), perk.level) == tiers.end()) {
                throw std::runtime_error(std::format("{}: perk {}'s Level is {}, 25, 45, 65 or 85", at, perk.id,
                                                     def.first_perk_level));
            }
            for (const auto& [key, value] : s.values) {
                if (same(key, "Ability")) perk.abilities.push_back(value);
                if (same(key, "Grants")) perk.grants.push_back(value);
                if (same(key, "Bonus")) {
                    const size_t sp = value.find_last_of(" \t");
                    char* end = nullptr;
                    const std::string num = sp == std::string::npos ? std::string() : value.substr(sp + 1);
                    const double v = num.empty() ? 0.0 : std::strtod(num.c_str(), &end);
                    if (sp == std::string::npos || end == nullptr || *end != '\0') {
                        throw std::runtime_error(at + ": perk " + perk.id + "'s Bonus is <key> <number>");
                    }
                    std::string k = value.substr(0, sp);
                    while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
                    perk.bonuses.emplace_back(std::move(k), v);
                }
            }
            const std::string choice = s.get("Choice").value_or("");
            if (choice != "A" && choice != "B") {
                throw std::runtime_error(at + ": perk " + perk.id + "'s Choice is A or B");
            }
            perk.choice = choice[0];
            perk.name = s.get("Name").value_or(perk.id);
            perk.description = s.get("Description").value_or("");
            perk.needs = s.get("Needs").value_or("");
            if (!perk.needs.empty() && perk.needs != "trigger" && perk.needs != "mechanic") {
                throw std::runtime_error(at + ": perk " + perk.id + "'s Needs is trigger or mechanic");
            }
            if (const auto a = s.get("Archetypes")) {
                perk.archetypes = split_list(*a);
                for (const std::string& id : perk.archetypes) {
                    if (std::find(def.archetypes.begin(), def.archetypes.end(), id) == def.archetypes.end()) {
                        throw std::runtime_error(at + ": perk " + perk.id + " is for " + id + ", not " + def.id + "'s");
                    }
                }
            }
            read_perk(s, "", perk.perk, at);
            if (const auto name = s.get("Trigger")) {
                const auto trigger = perk_trigger_from_name(*name);
                if (!trigger) {
                    throw std::runtime_error(at + ": perk " + perk.id + ": unknown Trigger " + *name);
                }
                if (!perk.needs.empty()) {
                    throw std::runtime_error(at + ": perk " + perk.id + " has a Trigger and Needs");
                }
                TriggeredPerk t;
                t.id = perk.id;
                t.name = perk.name;
                t.trigger = *trigger;
                t.after = s.number("After", 0);
                t.below = s.number("Below", 0);
                t.within = s.number("Within", 0);
                t.chain = s.number("Chain", 0);
                t.chance = std::clamp(s.number("Chance", 1), 0.0, 1.0);
                t.aimed = truthy(s.get("Aimed").value_or("false"));
                t.mark = s.number("Mark", 0);
                t.duration = s.number("Duration", 0);
                t.cooldown = s.number("Cooldown", 0);
                t.stamina = s.number("Stamina", 0);
                t.shield = s.number("Shield", 0);
                t.cool = s.number("Cool", 0);
                t.reload = truthy(s.get("Reload").value_or("false"));
                t.recharge = truthy(s.get("Recharge").value_or("false"));
                t.cloak = s.number("Cloak", 0);
                t.crit = truthy(s.get("Crit").value_or("false"));
                t.target_duration = s.number("TargetDuration", 0);
                t.damage_taken = s.number("DamageTaken", 1);
                t.resist = s.number("Resist", 0);
                t.one_hit = truthy(s.get("OneHit").value_or("false"));
                t.once_per_life = truthy(s.get("OncePerLife").value_or("false"));
                for (const auto& [key, value] : s.values) {
                    if (same(key, "TargetModifier")) t.target.push_back(parse_stat_modifier(value, at));
                }
                if (t.duration > 0 && t.cooldown > 0) {
                    throw std::runtime_error(at + ": perk " + perk.id + ": a Duration or a Cooldown, not both");
                }
                // Stances hold while they last; anything else needs a time
                // for its modifiers to last.
                const bool lasting = !perk.perk.empty() || t.damage_taken != 1 || t.resist != 0;
                if (lasting && !is_stance(t.trigger) && t.trigger != PerkTrigger::unhit &&
                    t.trigger != PerkTrigger::low_health && t.duration <= 0) {
                    throw std::runtime_error(at + ": perk " + perk.id + " needs a Duration for what it changes");
                }
                if (!lasting && t.stamina == 0 && t.shield == 0 && t.cool == 0 && !t.reload && !t.recharge &&
                    t.cloak == 0 && t.mark == 0 && !t.crit && t.target.empty()) {
                    throw std::runtime_error(at + ": perk " + perk.id + "'s Trigger does nothing");
                }
                if ((t.aimed || t.mark > 0) && t.trigger != PerkTrigger::hit) {
                    throw std::runtime_error(at + ": perk " + perk.id + ": Aimed and Mark are a hit's");
                }
                if (!t.target.empty() && t.target_duration <= 0) {
                    throw std::runtime_error(at + ": perk " + perk.id + "'s TargetModifier needs a TargetDuration");
                }
                perk.trigger = std::move(t);
            }
            def.perks.push_back(std::move(perk));
        } else {
            throw std::runtime_error(where + ": unknown section [" + s.name + "]");
        }
    }
    for (ProfessionDef& def : professions_) {
        std::stable_sort(def.milestones.begin(), def.milestones.end(),
                         [](const ProfessionMilestone& a, const ProfessionMilestone& b) { return a.level < b.level; });
        if (!def.chooses()) {
            continue;
        }
        if (!def.milestones.empty()) {
            throw std::runtime_error(where + " (" + def.id + "): perks or milestones, not both");
        }
        std::stable_sort(def.perks.begin(), def.perks.end(), [](const ProfessionPerkDef& a, const ProfessionPerkDef& b) {
            return a.level != b.level ? a.level < b.level : a.choice < b.choice;
        });
        // Two at each level: an A and a B.
        for (const int level : def.perk_tiers()) {
            int a = 0, b = 0;
            for (const ProfessionPerkDef& perk : def.perks) {
                if (perk.level == level) (perk.choice == 'A' ? a : b) += 1;
            }
            if (a != 1 || b != 1) {
                throw std::runtime_error(std::format("{} ({}): level {} needs one perk A and one perk B", where, def.id,
                                                     level));
            }
        }
    }
}

const ProfessionDef* ProfessionBook::find(std::string_view id) const {
    for (const ProfessionDef& def : professions_) {
        if (def.id == id) return &def;
    }
    return nullptr;
}

const ProfessionPerkDef* ProfessionBook::find_perk(std::string_view id, const ProfessionDef** owner) const {
    for (const ProfessionDef& def : professions_) {
        for (const ProfessionPerkDef& perk : def.perks) {
            if (perk.id == id) {
                if (owner != nullptr) *owner = &def;
                return &perk;
            }
        }
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

const ProfessionProgress* find_progress(std::span<const ProfessionProgress> progress, std::string_view id) {
    for (const ProfessionProgress& p : progress) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

ProfessionProgress& progress_for(std::vector<ProfessionProgress>& progress, std::string_view id) {
    for (ProfessionProgress& p : progress) {
        if (p.id == id) return p;
    }
    progress.push_back({std::string(id)});
    return progress.back();
}

int learned_count(const ProfessionDef& def, const ProfessionProgress& progress) {
    if (def.chooses()) {
        return int(progress.perks.size());
    }
    return int(std::count_if(def.milestones.begin(), def.milestones.end(),
                             [&](const ProfessionMilestone& m) { return m.level <= progress.trained; }));
}

int perks_learned(const ProfessionBook& book, std::span<const ProfessionProgress> progress, PerkPool pool) {
    int count = 0;
    for (const ProfessionProgress& p : progress) {
        if (const ProfessionDef* def = book.find(p.id); def != nullptr && def->pool == pool) {
            count += learned_count(*def, p);
        }
    }
    return count;
}

double perk_bonus(const ProfessionBook& book, std::span<const std::string> perks, std::string_view key) {
    double total = 0.0;
    for (const std::string& id : perks) {
        if (const ProfessionPerkDef* perk = book.find_perk(id)) {
            for (const auto& [k, v] : perk->bonuses) {
                if (k == key) total += v;
            }
        }
    }
    return total;
}

double perk_bonus(const ProfessionBook& book, std::span<const ProfessionProgress> progress, std::string_view profession,
                  std::string_view key) {
    const ProfessionProgress* p = find_progress(progress, profession);
    return p == nullptr ? 0.0 : perk_bonus(book, std::span<const std::string>(p->perks), key);
}

bool has_grant(const ProfessionBook& book, std::span<const ProfessionProgress> progress, std::string_view profession,
               std::string_view grant) {
    const ProfessionProgress* p = find_progress(progress, profession);
    if (p == nullptr) return false;
    for (const std::string& id : p->perks) {
        const ProfessionPerkDef* perk = book.find_perk(id);
        if (perk != nullptr && std::find(perk->grants.begin(), perk->grants.end(), grant) != perk->grants.end()) {
            return true;
        }
    }
    return false;
}

bool licensed(const ProfessionBook& book, std::span<const ProfessionProgress> progress, std::string_view id) {
    const ProfessionDef* def = book.find(id);
    if (def == nullptr || !def->available) return false;
    if (def->category == ProfessionCategory::weapon || def->category == ProfessionCategory::armor) return true;
    const ProfessionProgress* p = find_progress(progress, id);
    if (def->chooses()) {
        // A licence tier (FirstPerk = 1): one of its level-1 perks; else none needed.
        if (def->first_perk_level > 1) return true;
        if (p == nullptr) return false;
        return std::any_of(p->perks.begin(), p->perks.end(), [&](const std::string& known) {
            const ProfessionPerkDef* perk = book.find_perk(known);
            return perk != nullptr && perk->level == def->first_perk_level;
        });
    }
    return p != nullptr && p->trained >= 1;
}

namespace {

TrainResult refused(std::string message) {
    return {false, 0, std::move(message)};
}

std::string full_message(const ProfessionBook& book, std::span<const ProfessionProgress> progress, PerkPool pool) {
    return std::format("You know {} of {} {} perks already: give one up first.", perks_learned(book, progress, pool),
                       perk_cap, pool == PerkPool::combat ? "combat" : "non-combat");
}

} // namespace

TrainResult learn_perk(const ProfessionBook& book, std::vector<ProfessionProgress>& progress, std::string_view perk_id,
                       int64_t credits) {
    const ProfessionDef* def = nullptr;
    const ProfessionPerkDef* perk = book.find_perk(perk_id, &def);
    if (perk == nullptr || !def->available) {
        return refused("There's no such perk.");
    }
    const ProfessionProgress* mine = find_progress(progress, def->id);
    if (book.level_for(mine ? mine->xp : 0) < perk->level) {
        return refused(std::format("{} needs {} level {}.", perk->name, def->name, perk->level));
    }
    if (mine != nullptr) {
        for (const std::string& known : mine->perks) {
            const ProfessionPerkDef* other = book.find_perk(known);
            if (other == perk) {
                return refused(std::format("You know {} already.", perk->name));
            }
            if (other != nullptr && other->level == perk->level) {
                return refused(std::format("You know {} at level {}: give it up to learn {}.", other->name, perk->level,
                                           perk->name));
            }
        }
    }
    if (perks_learned(book, progress, def->pool) >= perk_cap) {
        return refused(full_message(book, progress, def->pool));
    }
    const bool paid_before = mine != nullptr && std::find(mine->paid_perks.begin(), mine->paid_perks.end(), perk->id) !=
                                                    mine->paid_perks.end();
    const int64_t cost = paid_before ? 0 : milestone_cost(perk->level);
    if (credits < cost) {
        return refused(std::format("{} Cr to learn {}.", cost, perk->name));
    }
    ProfessionProgress& p = progress_for(progress, def->id);
    p.perks.push_back(perk->id);
    if (!paid_before) p.paid_perks.push_back(perk->id);
    return {true, cost,
            cost > 0 ? std::format("Learned {} ({}) for {} Cr", perk->name, def->name, cost)
                     : std::format("Learned {} ({})", perk->name, def->name)};
}

TrainResult learn_milestone(const ProfessionBook& book, std::vector<ProfessionProgress>& progress,
                            std::string_view profession_id, int level, int64_t credits) {
    const ProfessionDef* def = book.find(profession_id);
    if (def == nullptr || !def->available || def->chooses()) {
        return refused("There's no one here to teach that.");
    }
    const ProfessionProgress* mine = find_progress(progress, def->id);
    const int trained = mine ? mine->trained : 0;
    const ProfessionMilestone* next = nullptr;  // the next one not yet learned
    for (const ProfessionMilestone& m : def->milestones) {
        if (m.level > trained && (next == nullptr || m.level < next->level)) next = &m;
    }
    if (next == nullptr || next->level != level) {
        return refused("You've learned that already, or must learn what comes before it.");
    }
    if (book.level_for(mine ? mine->xp : 0) < level) {
        return refused(std::format("You need {} level {}.", def->name, level));
    }
    if (perks_learned(book, progress, def->pool) >= perk_cap) {
        return refused(full_message(book, progress, def->pool));
    }
    const bool paid_before = mine != nullptr && mine->paid >= level;
    const int64_t cost = paid_before ? 0 : milestone_cost(level);
    if (credits < cost) {
        return refused(std::format("{} Cr to learn {}.", cost, next->name));
    }
    ProfessionProgress& p = progress_for(progress, def->id);
    p.trained = level;
    p.paid = std::max(p.paid, level);
    return {true, cost,
            cost > 0 ? std::format("Learned {} ({}) for {} Cr", next->name, def->name, cost)
                     : std::format("Learned {} ({})", next->name, def->name)};
}

TrainResult forget(const ProfessionBook& book, std::vector<ProfessionProgress>& progress, std::string_view id) {
    const ProfessionDef* def = nullptr;
    if (const ProfessionPerkDef* perk = book.find_perk(id, &def)) {
        for (ProfessionProgress& p : progress) {
            if (p.id != def->id) continue;
            const auto it = std::find(p.perks.begin(), p.perks.end(), perk->id);
            if (it == p.perks.end()) break;
            p.perks.erase(it);
            return {true, 0, std::format("Gave up {} ({})", perk->name, def->name)};
        }
        return refused(std::format("You don't know {}.", perk->name));
    }
    def = book.find(id);
    if (def == nullptr || def->chooses()) {
        return refused("There's nothing like that to give up.");
    }
    for (ProfessionProgress& p : progress) {
        if (p.id != def->id || p.trained < 1) continue;
        // Back to the milestone before the last learned.
        const ProfessionMilestone* last = nullptr;
        int before = 0;
        for (const ProfessionMilestone& m : def->milestones) {
            if (m.level <= p.trained) {
                if (last != nullptr) before = last->level;
                last = &m;
            }
        }
        p.trained = before;
        return {true, 0, std::format("Gave up {} ({})", last ? last->name : def->name, def->name)};
    }
    return refused(std::format("You've learned nothing of {}.", def->name));
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

const ProfessionDef* mark_profession(const ProfessionBook& book, const ItemDef& def) {
    if (def.kind == ItemKind::weapon) return book.for_archetype(weapon_archetype_info(def.archetype).id);
    if (def.kind == ItemKind::armor) return book.for_armor(def.armor_class);
    return nullptr;
}

int level_for_item(const ProfessionBook& book, const ItemDef& def, std::span<const ProfessionProgress> progress) {
    if (const ProfessionDef* p = mark_profession(book, def)) {
        return book.level_for(profession_xp(progress, p->id));
    }
    int level = 1;
    for (const ProfessionProgress& p : progress) {
        level = std::max(level, book.level_for(p.xp));
    }
    return level;
}

bool can_use_mark(const ProfessionBook& book, const ItemDef& def, int mark,
                  std::span<const ProfessionProgress> progress) {
    return clamp_mark(mark) == 0 || level_for_item(book, def, progress) >= mark_level(mark);
}

int64_t milestone_cost(int level) {
    if (level <= 1) return 0;
    if (level <= 25) return 500;
    if (level <= 50) return 2500;
    if (level <= 75) return 7500;
    return 20000;
}

ProfessionPerk profession_perks(const ProfessionDef& def, const ProfessionProgress& progress, int level) {
    ProfessionPerk perk;
    if (def.rank_every > 0) {
        add_perk(perk, def.rank, level / def.rank_every);
    }
    for (const ProfessionMilestone& m : def.milestones) {
        if (m.level <= level && m.level <= progress.trained) {
            add_perk(perk, m.perk);
        }
    }
    for (const ProfessionPerkDef& p : def.perks) {
        if (p.needs.empty() && !p.trigger && p.level <= level &&
            std::find(progress.perks.begin(), progress.perks.end(), p.id) != progress.perks.end()) {
            add_perk(perk, p.perk);
        }
    }
    return perk;
}

void apply_profession_perks(Loadout& loadout, const ProfessionBook& book, std::span<const ProfessionProgress> progress) {
    Gear perks{"specializations", "specialization", {}};
    const std::string armor = loadout.armor_class.empty() ? "none" : loadout.armor_class;
    loadout.triggered.clear();
    for (const ProfessionProgress& mine : progress) {
        const ProfessionDef* def = book.find(mine.id);
        if (def == nullptr || !def->available) {
            continue;
        }
        if (def->category == ProfessionCategory::armor && def->armor_class != armor) {
            continue;  // trained for other armor than this loadout's
        }
        if (!licensed(book, progress, def->id)) {
            continue;  // not licensed: none of it
        }
        if (def->category == ProfessionCategory::weapon &&
            std::none_of(loadout.weapons.begin(), loadout.weapons.end(), [&](const std::optional<WeaponDef>& w) {
                return w && std::find(def->archetypes.begin(), def->archetypes.end(), w->archetype) !=
                                def->archetypes.end();
            })) {
            continue;  // none of its weapons carried
        }
        const int level = book.level_for(mine.xp);
        const ProfessionPerk perk = profession_perks(*def, mine, level);
        perks.modifiers.insert(perks.modifiers.end(), perk.stats.begin(), perk.stats.end());
        // Handling, each to the weapons it's for: ranks and milestones to all
        // the profession's, a perk to its own (or all).
        ProfessionPerk shared = profession_perks(*def, ProfessionProgress{mine.id, mine.xp, mine.trained}, level);
        std::vector<std::pair<const std::vector<std::string>*, const std::vector<WeaponModifier>*>> handling{
            {&def->archetypes, &shared.weapons}};
        for (const ProfessionPerkDef& p : def->perks) {
            if (p.needs.empty() && !p.trigger && p.level <= level && !p.perk.weapons.empty() &&
                std::find(mine.perks.begin(), mine.perks.end(), p.id) != mine.perks.end()) {
                handling.push_back({p.archetypes.empty() ? &def->archetypes : &p.archetypes, &p.perk.weapons});
            }
        }
        // (An armor profession's perks handle every weapon: Tactical Swap.)
        const bool every_weapon = def->category == ProfessionCategory::armor;
        for (const auto& [archetypes, mods] : handling) {
            for (std::optional<WeaponDef>& w : loadout.weapons) {
                if (!w || (!every_weapon &&
                           std::find(archetypes->begin(), archetypes->end(), w->archetype) == archetypes->end())) {
                    continue;
                }
                for (const WeaponModifier& m : *mods) {
                    apply_weapon_modifier(*w, m);
                }
            }
        }
        // Triggered perks: carried for the simulation and the server to fire,
        // each with the weapon slots it's for.
        for (const ProfessionPerkDef& p : def->perks) {
            if (!p.trigger || p.level > level || int(loadout.triggered.size()) >= max_perk_timers ||
                std::find(mine.perks.begin(), mine.perks.end(), p.id) == mine.perks.end()) {
                continue;
            }
            TriggeredPerk t = *p.trigger;
            t.stats = p.perk.stats;
            t.weapons = p.perk.weapons;
            if (def->category == ProfessionCategory::weapon) {
                const std::vector<std::string>& archetypes = p.archetypes.empty() ? def->archetypes : p.archetypes;
                for (int slot = 0; slot < weapon_slots; ++slot) {
                    const auto& w = loadout.weapons[size_t(slot)];
                    if (w && std::find(archetypes.begin(), archetypes.end(), w->archetype) != archetypes.end()) {
                        t.slots = uint8_t(t.slots | (1u << slot));
                    }
                }
                if (t.slots == 0) continue;  // none of its weapons carried
            }
            loadout.triggered.push_back(std::move(t));
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
    // The most pieces, clothing ("none") counting too; on a tie the heavier
    // (the loop keeps the later, heavier one).
    int best = 0, most = counts[0];
    for (int w = 1; w <= 3; ++w) {
        if (counts[size_t(w)] > 0 && counts[size_t(w)] >= most) {
            most = counts[size_t(w)];
            best = w;
        }
    }
    return best == 0 ? "none" : best == 1 ? "light" : best == 2 ? "medium" : "heavy";
}

} // namespace swm::core
