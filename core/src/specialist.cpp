#include "swm/core/specialist.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <sstream>
#include <stdexcept>

namespace swm::core {

namespace {

bool same(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

bool truthy(std::string_view v) {
    return same(v, "true") || same(v, "yes") || v == "1";
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> out;
    std::string item;
    for (const char c : text) {
        if (c == separator) {
            out.push_back(std::move(item));
            item.clear();
        } else {
            item += c;
        }
    }
    out.push_back(std::move(item));
    return out;
}

std::vector<std::string> split_words(std::string_view text) {
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

// Fields of saved records: '%' escapes the characters that separate them.
std::string escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '%' || c == '|' || c == '\t' || c == '\n' || c == '\r' || c == ',' || c == ';') {
            out += std::format("%{:02X}", static_cast<unsigned char>(c));
        } else {
            out += c;
        }
    }
    return out;
}

std::string unescape(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            const int hi = hex(text[i + 1]), lo = hex(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += char(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

double to_double(const std::string& text, double fallback = 0) {
    try {
        size_t used = 0;
        const double v = std::stod(text, &used);
        return std::isfinite(v) ? v : fallback;
    } catch (const std::exception&) {
        return fallback;
    }
}

int64_t to_int(const std::string& text, int64_t fallback = 0) {
    try {
        return std::stoll(text);
    } catch (const std::exception&) {
        return fallback;
    }
}

uint64_t to_uint(const std::string& text) {
    try {
        return std::stoull(text);
    } catch (const std::exception&) {
        return 0;
    }
}

constexpr std::array<std::string_view, 28> effect_names{
    "buff",          "heal",        "heal_area", "damage_area",      "hazard",
    "cleanse",       "revive",      "cloak",     "mark",             "snare",
    "jump",          "deploy",      "strike",    "supply",           "vehicle_repair",
    "vehicle_guard", "companion_repair", "companion_overcharge", "perform", "track",
    "seeker",        "hvt",         "tranquilize", "tame",           "sample_dna",
    "blind",         "vehicle_boost", "vehicle_flare",
};
constexpr std::array<std::string_view, 5> target_names{"self", "ally", "enemy", "ground", "creature"};
constexpr std::array<std::string_view, 7> deploy_names{"none",         "heal_aura", "buff_aura", "sensor",
                                                       "trap",         "spawn_beacon", "camp"};
constexpr std::array<std::string_view, 6> contract_names{"npc_mark", "pvp_mark", "contraband", "track", "gig",
                                                         "craft_order"};

template <typename E, size_t N>
std::optional<E> from_names(const std::array<std::string_view, N>& names, std::string_view name) {
    for (size_t i = 0; i < N; ++i) {
        if (same(names[i], name)) return E(i);
    }
    return std::nullopt;
}

std::vector<std::pair<std::string, int>> read_materials(const Ini::Section& s, const std::string& where) {
    std::vector<std::pair<std::string, int>> out;
    for (const auto& [key, value] : s.values) {
        if (!same(key, "Material")) continue;
        std::istringstream fields(value);
        std::string category;
        int units = 0;
        if (!(fields >> category >> units) || units <= 0) {
            throw std::runtime_error(where + ": Material is \"<category> <units>\", not \"" + value + "\"");
        }
        out.emplace_back(category, units);
    }
    return out;
}

DamageType read_damage_type(const Ini::Section& s, std::string_view key, DamageType fallback, const std::string& where) {
    const auto text = s.get(key);
    if (!text) return fallback;
    const auto type = damage_type_from_name(*text);
    if (!type) throw std::runtime_error(where + ": unknown damage type " + *text);
    return *type;
}

}  // namespace

std::optional<AbilityEffect> ability_effect_from_name(std::string_view name) {
    return from_names<AbilityEffect>(effect_names, name);
}
std::string_view ability_effect_name(AbilityEffect effect) {
    return size_t(effect) < effect_names.size() ? effect_names[size_t(effect)] : "buff";
}
std::optional<AbilityTarget> ability_target_from_name(std::string_view name) {
    return from_names<AbilityTarget>(target_names, name);
}
std::string_view ability_target_name(AbilityTarget target) {
    return size_t(target) < target_names.size() ? target_names[size_t(target)] : "self";
}
std::optional<DeployKind> deploy_kind_from_name(std::string_view name) {
    return from_names<DeployKind>(deploy_names, name);
}
std::string_view deploy_kind_name(DeployKind kind) {
    return size_t(kind) < deploy_names.size() ? deploy_names[size_t(kind)] : "none";
}
std::string_view companion_kind_name(CompanionKind kind) {
    return kind == CompanionKind::droid ? "droid" : "creature";
}
std::string_view companion_role_name(CompanionRole role) {
    return role == CompanionRole::recon ? "recon" : role == CompanionRole::support ? "support" : "combat";
}
std::string_view contract_kind_name(ContractKind kind) {
    return size_t(kind) < contract_names.size() ? contract_names[size_t(kind)] : "npc_mark";
}
std::optional<ContractKind> contract_kind_from_name(std::string_view name) {
    return from_names<ContractKind>(contract_names, name);
}

// ---------------------------------------------------------------------------
// The book

void SpecialistBook::load(const std::filesystem::path& path) {
    parse(Ini::load(path), path.string());
}

void SpecialistBook::parse(const Ini& ini, const std::string& where) {
    abilities_.clear();
    companions_.clear();
    modules_.clear();
    slices_.clear();
    for (const Ini::Section& s : ini.sections()) {
        const std::string here = where + " [" + s.name + "] " + s.get("Id").value_or("?");
        if (same(s.name, "ability")) {
            AbilityDef a;
            a.id = s.get("Id").value_or("");
            a.name = s.get("Name").value_or(a.id);
            a.description = s.get("Description").value_or("");
            a.profession = s.get("Profession").value_or("");
            if (a.id.empty() || a.profession.empty()) throw std::runtime_error(here + ": an ability needs Id and Profession");
            a.level = std::clamp(int(s.number("Level", 1)), 1, profession_max_level);
            const std::string effect = s.get("Effect").value_or("buff");
            const auto e = ability_effect_from_name(effect);
            if (!e) throw std::runtime_error(here + ": unknown Effect " + effect);
            a.effect = *e;
            const std::string target = s.get("Target").value_or("self");
            const auto t = ability_target_from_name(target);
            if (!t) throw std::runtime_error(here + ": unknown Target " + target);
            a.target = *t;
            const std::string deploy = s.get("Deploy").value_or("none");
            const auto d = deploy_kind_from_name(deploy);
            if (!d) throw std::runtime_error(here + ": unknown Deploy " + deploy);
            a.deploy = *d;
            if (a.effect == AbilityEffect::deploy && a.deploy == DeployKind::none) {
                throw std::runtime_error(here + ": a deploy ability needs Deploy");
            }
            a.cooldown = std::max(0.0, s.number("Cooldown", 10));
            a.range = std::max(0.0, s.number("Range", 0));
            a.radius = std::max(0.0, s.number("Radius", 0));
            a.duration = std::max(0.0, s.number("Duration", 0));
            a.amount = s.number("Amount", 0);
            a.delay = std::max(0.0, s.number("Delay", 0));
            a.channel = std::max(0.0, s.number("Channel", 0));
            a.damage_type = read_damage_type(s, "DamageType", a.damage_type, here);
            for (const auto& [key, value] : s.values) {
                if (same(key, "Modifier")) a.modifiers.push_back(parse_stat_modifier(value, here));
            }
            const std::string where_used = s.get("Where").value_or("both");
            a.match_only = same(where_used, "match");
            a.world_only = same(where_used, "world");
            a.xp = int64_t(std::max(0.0, s.number("XP", 20)));
            a.flair = truthy(s.get("Flair").value_or("false"));
            a.unmark = truthy(s.get("Unmark").value_or("false"));
            if (ability(a.id)) throw std::runtime_error(here + ": a second ability " + a.id);
            abilities_.push_back(std::move(a));
        } else if (same(s.name, "companion")) {
            CompanionTemplate c;
            c.id = s.get("Id").value_or("");
            c.name = s.get("Name").value_or(c.id);
            if (c.id.empty()) throw std::runtime_error(here + ": a companion needs Id");
            c.kind = same(s.get("Kind").value_or("creature"), "droid") ? CompanionKind::droid : CompanionKind::creature;
            const std::string role = s.get("Role").value_or("combat");
            c.role = same(role, "recon") ? CompanionRole::recon : same(role, "support") ? CompanionRole::support
                                                                                         : CompanionRole::combat;
            c.look = s.get("Look").value_or("");
            c.tier = std::clamp(int(s.number("Tier", 1)), 1, 5);
            c.level = std::clamp(int(s.number("Level", 1)), 1, profession_max_level);
            c.build_level = std::clamp(int(s.number("BuildLevel", 1)), 1, profession_max_level);
            c.health = std::max(1.0, s.number("Health", c.health));
            c.damage = std::max(0.0, s.number("Damage", c.damage));
            c.damage_type = read_damage_type(s, "DamageType", c.kind == CompanionKind::droid ? DamageType::laser
                                                                                              : DamageType::melee, here);
            c.attack_range = std::max(1.0, s.number("AttackRange", c.kind == CompanionKind::droid ? 18.0 : 2.5));
            c.attack_cooldown = std::max(0.2, s.number("AttackCooldown", c.attack_cooldown));
            c.speed = std::max(0.5, s.number("Speed", c.speed));
            c.radius = std::max(0.1, s.number("Radius", c.radius));
            c.height = std::max(0.1, s.number("Height", c.height));
            c.mitigation = std::clamp(s.number("Mitigation", 0), 0.0, 0.8);
            c.bays = std::clamp(int(s.number("Bays", 0)), 0, 6);
            c.mountable = truthy(s.get("Mountable").value_or("false"));
            c.mount_item = s.get("MountItem").value_or("");
            c.materials = read_materials(s, here);
            if (companion(c.id)) throw std::runtime_error(here + ": a second companion " + c.id);
            companions_.push_back(std::move(c));
        } else if (same(s.name, "droid_module")) {
            DroidModule m;
            m.id = s.get("Id").value_or("");
            m.name = s.get("Name").value_or(m.id);
            m.description = s.get("Description").value_or("");
            if (m.id.empty()) throw std::runtime_error(here + ": a module needs Id");
            m.bay = lower(s.get("Bay").value_or("core"));
            if (m.bay != "core" && m.bay != "primary" && m.bay != "utility") {
                throw std::runtime_error(here + ": Bay is core, primary or utility");
            }
            m.level = std::clamp(int(s.number("Level", 25)), 1, profession_max_level);
            m.health = std::max(0.1, s.number("Health", 1));
            m.damage = std::max(0.0, s.number("Damage", 1));
            m.speed = std::max(0.1, s.number("Speed", 1));
            m.mitigation = std::clamp(s.number("Mitigation", 0), -0.5, 0.5);
            if (s.get("DamageType")) m.damage_type = read_damage_type(s, "DamageType", DamageType::laser, here);
            if (const auto role = s.get("Role")) {
                m.role = same(*role, "recon") ? CompanionRole::recon : same(*role, "support") ? CompanionRole::support
                                                                                               : CompanionRole::combat;
            }
            m.materials = read_materials(s, here);
            modules_.push_back(std::move(m));
        } else if (same(s.name, "slice")) {
            SliceDef sl;
            sl.id = s.get("Id").value_or("");
            sl.name = s.get("Name").value_or(sl.id);
            sl.description = s.get("Description").value_or("");
            if (sl.id.empty()) throw std::runtime_error(here + ": a slice needs Id");
            sl.applies = lower(s.get("Applies").value_or("weapon"));
            if (sl.applies != "weapon" && sl.applies != "armor") {
                throw std::runtime_error(here + ": Applies is weapon or armor");
            }
            sl.level = std::clamp(int(s.number("Level", 1)), 1, profession_max_level);
            sl.damage = s.number("Damage", 1);
            sl.rate = s.number("Rate", 1);
            sl.heat = s.number("Heat", 1);
            sl.spread = s.number("Spread", 1);
            sl.range = s.number("Range", 1);
            sl.reload = s.number("Reload", 1);
            for (const auto& [key, value] : s.values) {
                if (same(key, "Modifier")) sl.modifiers.push_back(parse_stat_modifier(value, here));
            }
            slices_.push_back(std::move(sl));
        } else if (same(s.name, "inspiration")) {
            InspirationDef in;
            in.id = s.get("Id").value_or("");
            in.name = s.get("Name").value_or(in.id);
            in.description = s.get("Description").value_or("");
            if (in.id.empty()) throw std::runtime_error(here + ": an inspiration needs Id");
            in.kind = lower(s.get("Kind").value_or("combat"));
            if (in.kind != "combat" && in.kind != "progression") {
                throw std::runtime_error(here + ": Kind is combat or progression");
            }
            in.level = std::clamp(int(s.number("Level", 1)), 1, profession_max_level);
            for (const auto& [key, value] : s.values) {
                if (same(key, "Modifier")) in.modifiers.push_back(parse_stat_modifier(value, here));
            }
            if (in.modifiers.empty()) throw std::runtime_error(here + ": an inspiration needs a Modifier");
            inspirations_.push_back(std::move(in));
        }
    }
    for (const CompanionTemplate& c : companions_) {
        if (c.mountable && c.mount_item.empty()) {
            throw std::runtime_error(where + ": mountable companion " + c.id + " needs MountItem");
        }
    }
}

const AbilityDef* SpecialistBook::ability(std::string_view id) const {
    for (const AbilityDef& a : abilities_) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

const CompanionTemplate* SpecialistBook::companion(std::string_view id) const {
    for (const CompanionTemplate& c : companions_) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

const CompanionTemplate* SpecialistBook::companion_for_look(std::string_view look) const {
    for (const CompanionTemplate& c : companions_) {
        if (c.kind == CompanionKind::creature && same(c.look, look)) return &c;
    }
    return nullptr;
}

const DroidModule* SpecialistBook::module(std::string_view id) const {
    for (const DroidModule& m : modules_) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

const SliceDef* SpecialistBook::slice(std::string_view id) const {
    for (const SliceDef& s : slices_) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

const InspirationDef* SpecialistBook::inspiration(std::string_view id) const {
    for (const InspirationDef& i : inspirations_) {
        if (i.id == id) return &i;
    }
    return nullptr;
}

ProfessionStanding profession_standing(const ProfessionBook& professions, std::span<const ProfessionProgress> progress,
                                       std::string_view id) {
    ProfessionStanding out;
    for (const ProfessionProgress& p : progress) {
        if (p.id == id) {
            out.level = professions.level_for(p.xp);
            out.trained = p.trained;
            // A profession of perks: licensed, and the highest tier known.
            if (const ProfessionDef* def = professions.find(id); def != nullptr && def->chooses()) {
                out.trained = licensed(professions, progress, id) ? 1 : 0;
                for (const std::string& known : p.perks) {
                    if (const ProfessionPerkDef* perk = professions.find_perk(known); perk && out.trained > 0) {
                        out.trained = std::max(out.trained, perk->level);
                    }
                }
            }
            return out;
        }
    }
    out.level = professions.level_for(0);
    return out;
}

std::vector<std::string> unlocked_abilities(const SpecialistBook& book, const ProfessionBook& professions,
                                            std::span<const ProfessionProgress> progress, bool in_match) {
    std::vector<std::string> out;
    for (const AbilityDef& a : book.abilities()) {
        if ((in_match && a.world_only) || (!in_match && a.match_only)) continue;
        const ProfessionDef* def = professions.find(a.profession);
        if (def == nullptr || !def->available) continue;
        if (def->chooses()) {
            // A profession of perks: the abilities its known perks teach.
            const ProfessionProgress* p = find_progress(progress, def->id);
            const bool taught = p != nullptr && std::any_of(p->perks.begin(), p->perks.end(), [&](const std::string& id) {
                const ProfessionPerkDef* perk = professions.find_perk(id);
                return perk != nullptr && std::find(perk->abilities.begin(), perk->abilities.end(), a.id) != perk->abilities.end();
            });
            if (taught) out.push_back(a.id);
            continue;
        }
        const ProfessionStanding st = profession_standing(professions, progress, def->id);
        if (st.level >= a.level && st.trained >= a.level) out.push_back(a.id);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Timed effects

bool add_effect(std::vector<TimedEffect>& effects, TimedEffect effect) {
    effect.duration = std::max(effect.duration, effect.remaining);
    for (TimedEffect& e : effects) {
        if (e.id == effect.id) {
            const bool changed = e.modifiers.size() != effect.modifiers.size() ||
                                 !std::equal(e.modifiers.begin(), e.modifiers.end(), effect.modifiers.begin(),
                                             [](const StatModifier& x, const StatModifier& y) {
                                                 return x.stat == y.stat && x.add == y.add && x.multiply == y.multiply;
                                             });
            effect.remaining = std::max(effect.remaining, e.remaining);
            e = std::move(effect);
            return changed;
        }
    }
    const bool stats = !effect.modifiers.empty();
    effects.push_back(std::move(effect));
    return stats;
}

bool tick_effects(std::vector<TimedEffect>& effects, double dt) {
    bool changed = false;
    for (TimedEffect& e : effects) {
        e.remaining -= dt;
    }
    std::erase_if(effects, [&](const TimedEffect& e) {
        if (e.remaining > 0) return false;
        changed = changed || !e.modifiers.empty();
        return true;
    });
    return changed;
}

bool remove_effects(std::vector<TimedEffect>& effects, uint16_t flags) {
    bool changed = false;
    std::erase_if(effects, [&](const TimedEffect& e) {
        if ((e.flags & flags) == 0) return false;
        changed = changed || !e.modifiers.empty();
        return true;
    });
    return changed;
}

bool has_effect_flag(std::span<const TimedEffect> effects, uint16_t flag) {
    return std::any_of(effects.begin(), effects.end(), [&](const TimedEffect& e) { return (e.flags & flag) != 0; });
}

std::optional<Gear> effects_gear(std::span<const TimedEffect> effects) {
    Gear gear{"effects", "effects", {}};
    for (const TimedEffect& e : effects) {
        gear.modifiers.insert(gear.modifiers.end(), e.modifiers.begin(), e.modifiers.end());
    }
    if (gear.modifiers.empty()) return std::nullopt;
    return gear;
}

// ---------------------------------------------------------------------------
// Companions

CompanionStats companion_stats(const SpecialistBook& book, const CompanionTemplate& tpl, const CompanionRecord& record,
                               int handler_level) {
    CompanionStats out;
    const double level = std::clamp(handler_level, 0, profession_max_level);
    // ASSUMPTION: a master's companion is half again as tough and a quarter
    // again as fierce as a novice's.
    out.max_health = tpl.health * record.health * (1 + level / 200);
    out.damage = tpl.damage * record.damage * (1 + level / 400);
    out.damage_type = tpl.damage_type;
    out.attack_range = tpl.attack_range;
    out.attack_cooldown = tpl.attack_cooldown;
    out.speed = tpl.speed * record.speed;
    out.mitigation = tpl.mitigation;
    out.role = tpl.role;
    for (const std::string& id : record.modules) {
        const DroidModule* m = book.module(id);
        if (m == nullptr) continue;
        out.max_health *= m->health;
        out.damage *= m->damage;
        out.speed *= m->speed;
        out.mitigation += m->mitigation;
        if (m->damage_type) out.damage_type = *m->damage_type;
        if (m->role) out.role = *m->role;
    }
    out.mitigation = std::clamp(out.mitigation, 0.0, 0.8);
    return out;
}

std::string check_droid_modules(const SpecialistBook& book, const CompanionTemplate& chassis,
                                std::span<const std::string> modules, int engineer_level, int extra_bays) {
    if (chassis.kind != CompanionKind::droid) return chassis.name + " isn't a droid chassis";
    const int bays = chassis.bays + std::max(0, extra_bays);
    if (int(modules.size()) > bays) {
        return std::format("{} has {} bay(s), not {}", chassis.name, bays, modules.size());
    }
    int primaries = 0;
    for (size_t i = 0; i < modules.size(); ++i) {
        const DroidModule* m = book.module(modules[i]);
        if (m == nullptr) return "unknown module " + modules[i];
        if (m->level > engineer_level) return std::format("{} needs Droid Engineer {}", m->name, m->level);
        if (m->bay == "primary" && ++primaries > 1) return "one primary function per droid";
        if (std::find(modules.begin(), modules.begin() + std::ptrdiff_t(i), modules[i]) != modules.begin() + std::ptrdiff_t(i)) {
            return "the same module twice";
        }
    }
    return {};
}

int tameable_tier(int level) {
    return std::clamp(1 + std::max(0, level) / 25, 1, 5);
}

double tame_chance(const CompanionTemplate& creature, int handler_level, bool tranquilized, double health_share) {
    const int reach = tameable_tier(handler_level);
    if (creature.kind != CompanionKind::creature || creature.tier > reach || handler_level < creature.level) {
        return 0;
    }
    double chance = 0.35 + 0.1 * (reach - creature.tier);
    if (tranquilized) chance += 0.25;
    chance += (1 - std::clamp(health_share, 0.0, 1.0)) * 0.3;
    return std::clamp(chance, 0.05, 0.95);
}

double dna_quality(int bio_level, double luck) {
    const double floor = 0.2 + std::clamp(bio_level, 0, profession_max_level) * 0.004;
    return std::clamp(floor + std::clamp(luck, 0.0, 1.0) * 0.35, 0.0, 1.0);
}

CompanionRecord engineer_egg(const DnaSample& a, const DnaSample* b, int bio_level, uint16_t colors,
                             double quality_bonus, double hybrid_bonus) {
    CompanionRecord r;
    r.template_id = a.template_id;
    r.origin = "hatched";
    double q = b ? a.quality * 0.6 + b->quality * 0.4 : a.quality;
    q = std::clamp(q + std::clamp(bio_level, 0, profession_max_level) * 0.001 + std::max(0.0, quality_bonus), 0.0, 1.0);
    r.quality = q;
    r.health = 0.85 + q * 0.35;
    r.damage = 0.85 + q * 0.3;
    r.speed = 0.95 + q * 0.1;
    if (b && b->template_id != a.template_id) {
        // Another creature's DNA spliced in: hardier, a little slower.
        r.health += 0.1 + std::max(0.0, hybrid_bonus);
        r.damage += std::max(0.0, hybrid_bonus);
        r.speed -= 0.03;
    }
    r.colors = colors == 0 ? 1 : colors;
    return r;
}

std::string format_companion(const CompanionRecord& r) {
    std::string modules;
    for (size_t i = 0; i < r.modules.size(); ++i) {
        if (i) modules += ',';
        modules += escape(r.modules[i]);
    }
    return std::format("{}|{}|{}|{}|{:.4f}|{:.4f}|{:.4f}|{:.4f}|{}|{}|{}|{}|{}|{}", r.id, escape(r.template_id),
                       escape(r.name), escape(r.origin), r.quality, r.health, r.damage, r.speed, r.colors, modules,
                       r.mount ? 1 : 0, r.tactical ? 1 : 0, r.extra_bay ? 1 : 0, r.hardened ? 1 : 0);
}

std::optional<CompanionRecord> parse_companion(std::string_view line) {
    const std::vector<std::string> f = split(line, '|');
    if (f.size() < 11) return std::nullopt;
    CompanionRecord r;
    r.id = to_uint(f[0]);
    r.template_id = unescape(f[1]);
    r.name = unescape(f[2]);
    r.origin = unescape(f[3]);
    r.quality = std::clamp(to_double(f[4], 0.5), 0.0, 1.0);
    r.health = std::clamp(to_double(f[5], 1), 0.1, 3.0);
    r.damage = std::clamp(to_double(f[6], 1), 0.1, 3.0);
    r.speed = std::clamp(to_double(f[7], 1), 0.1, 3.0);
    r.colors = uint16_t(std::clamp<int64_t>(to_int(f[8]), 0, 65535));
    if (!f[9].empty()) {
        for (const std::string& m : split(f[9], ',')) {
            if (!m.empty()) r.modules.push_back(unescape(m));
        }
    }
    r.mount = f[10] == "1";
    r.tactical = f.size() > 11 && f[11] == "1";  // (saved before Tactical Strain: not)
    r.extra_bay = f.size() > 12 && f[12] == "1";  // (and before the Droid Engineer's perks)
    r.hardened = f.size() > 13 && f[13] == "1";
    if (r.template_id.empty()) return std::nullopt;
    return r;
}

std::string format_samples(std::span<const DnaSample> samples) {
    std::string out;
    for (const DnaSample& s : samples) {
        out += std::format("{}|{}|{:.4f}\n", s.id, escape(s.template_id), s.quality);
    }
    return out;
}

std::vector<DnaSample> parse_samples(std::string_view text) {
    std::vector<DnaSample> out;
    for (const std::string& line : split(text, '\n')) {
        const std::vector<std::string> f = split(line, '|');
        if (f.size() < 3 || f[1].empty()) continue;
        out.push_back({to_uint(f[0]), unescape(f[1]), std::clamp(to_double(f[2], 0.5), 0.0, 1.0)});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Contracts

int64_t contract_reward(ContractKind kind, int tier, double distance) {
    const double d = std::max(0.0, distance);
    switch (kind) {
    case ContractKind::npc_mark: return int64_t(150 * std::max(1, tier) + d * 0.5);
    case ContractKind::pvp_mark: return pvp_bounty_base;
    case ContractKind::contraband: return int64_t(100 + d * 0.8);
    case ContractKind::track: return int64_t(80 + d * 0.4);
    case ContractKind::gig: return 300;
    case ContractKind::craft_order: return int64_t(60 * std::max(1, tier));
    }
    return 0;
}

// ---------------------------------------------------------------------------
// The bazaar

int bazaar_listing_limit(int merchant_level) {
    // Anyone's 10, and more with Merchant levels (a Merchant's perks add more).
    return 10 + std::clamp(merchant_level, 0, profession_max_level) / 5;
}

double bazaar_fee_rate(int merchant_level) {
    return 0.06 - 0.04 * std::clamp(merchant_level, 0, profession_max_level) / profession_max_level;
}

int64_t bazaar_fee(int64_t price, int merchant_level, double cut, bool halved) {
    const double rate = std::max(0.0, bazaar_fee_rate(merchant_level) - std::max(0.0, cut)) * (halved ? 0.5 : 1.0);
    return std::max<int64_t>(price > 0 ? 1 : 0, int64_t(std::llround(double(price) * rate)));
}

PriceStats price_stats(std::span<const BazaarSale> sales, std::string_view def_id) {
    PriceStats out;
    int64_t total = 0;
    for (const BazaarSale& s : sales) {
        if (s.def_id != def_id) continue;
        if (out.count == 0) {
            out.low = out.high = s.price;
        }
        out.low = std::min(out.low, s.price);
        out.high = std::max(out.high, s.price);
        out.last = s.price;
        total += s.price;
        ++out.count;
    }
    if (out.count > 0) out.average = total / out.count;
    return out;
}

std::string format_listings(std::span<const BazaarListing> listings) {
    std::string out;
    for (const BazaarListing& l : listings) {
        out += std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}\n", l.id, l.seller, escape(l.seller_name),
                           escape(l.shop), l.item.id, escape(l.item.def_id), int(l.item.grade), l.item.quantity,
                           escape(l.item.custom_name), escape(l.item.color_json), l.price, l.listed_at, l.item.mark);
    }
    return out;
}

std::vector<BazaarListing> parse_listings(std::string_view text) {
    std::vector<BazaarListing> out;
    for (const std::string& line : split(text, '\n')) {
        const std::vector<std::string> f = split(line, '|');
        if (f.size() < 12) continue;
        BazaarListing l;
        l.id = to_uint(f[0]);
        l.seller = to_uint(f[1]);
        l.seller_name = unescape(f[2]);
        l.shop = unescape(f[3]);
        l.item.id = to_uint(f[4]);
        l.item.owner_id = l.seller;
        l.item.def_id = unescape(f[5]);
        l.item.grade = saved_grade(f[6]);
        l.item.quantity = int(std::clamp<int64_t>(to_int(f[7], 1), 1, 65535));
        l.item.custom_name = unescape(f[8]);
        l.item.color_json = unescape(f[9]);
        l.price = std::clamp<int64_t>(to_int(f[10]), 1, bazaar_max_price);
        l.listed_at = to_int(f[11]);
        if (f.size() > 12) l.item.mark = clamp_mark(int(to_int(f[12])));
        if (l.id == 0 || l.item.def_id.empty()) continue;
        out.push_back(std::move(l));
    }
    return out;
}

std::string format_sales(std::span<const BazaarSale> sales) {
    std::string out;
    for (const BazaarSale& s : sales) {
        out += std::format("{}|{}|{}\n", escape(s.def_id), s.price, s.at);
    }
    return out;
}

std::vector<BazaarSale> parse_sales(std::string_view text) {
    std::vector<BazaarSale> out;
    for (const std::string& line : split(text, '\n')) {
        const std::vector<std::string> f = split(line, '|');
        if (f.size() < 3 || f[0].empty()) continue;
        out.push_back({unescape(f[0]), to_int(f[1]), to_int(f[2])});
    }
    return out;
}

std::vector<BazaarListing> general_store_listings(const ItemCatalog& catalog) {
    std::vector<BazaarListing> out;
    for (const ItemDef& def : catalog.all()) {
        if (!def.basic || def.cost_credits <= 0) continue;
        BazaarListing l;
        l.id = general_store_listing_base + out.size();
        l.seller_name = std::string(general_store_seller);
        l.shop = "General Store";
        l.item.def_id = def.id;
        l.price = def.cost_credits;
        out.push_back(std::move(l));
    }
    return out;
}

bool listing_matches(const BazaarListing& listing, std::string_view item_name, std::string_view query) {
    const std::string haystack = lower(item_name) + " " + lower(listing.item.def_id) + " " + lower(listing.item.custom_name) +
                                 " " + lower(listing.shop) + " " + lower(listing.seller_name);
    for (const std::string& word : split_words(lower(query))) {
        if (haystack.find(word) == std::string::npos) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Slicing

namespace {

// An item's slice as saved: its id, and ":<power>" after it if not 1.
std::string saved_slice(const ItemInstance& item) {
    const std::string& json = item.color_json;
    const size_t key = json.find("\"slice\"");
    if (key == std::string::npos) return {};
    const size_t open = json.find('"', json.find(':', key + 7) + 1);
    if (open == std::string::npos) return {};
    const size_t close = json.find('"', open + 1);
    if (close == std::string::npos) return {};
    return json.substr(open + 1, close - open - 1);
}

} // namespace

std::string item_slice(const ItemInstance& item) {
    const std::string saved = saved_slice(item);
    return saved.substr(0, saved.find(':'));
}

double item_slice_power(const ItemInstance& item) {
    const std::string saved = saved_slice(item);
    const size_t colon = saved.find(':');
    if (colon == std::string::npos) return 1.0;
    char* end = nullptr;
    const double p = std::strtod(saved.c_str() + colon + 1, &end);
    return p > 0 && p < 10 ? p : 1.0;
}

SliceDef scaled_slice(const SliceDef& slice, double power) {
    SliceDef s = slice;
    const auto more = [&](double m) { return 1.0 + (m - 1.0) * power; };
    s.damage = more(s.damage);
    s.rate = more(s.rate);
    s.heat = more(s.heat);
    s.spread = more(s.spread);
    s.range = more(s.range);
    s.reload = more(s.reload);
    for (StatModifier& m : s.modifiers) {
        m.add *= power;
        m.multiply = more(m.multiply);
    }
    return s;
}

void set_item_slice(ItemInstance& item, std::string_view slice_id, double power) {
    std::string json = item.color_json;
    if (json.empty() || json.front() != '{' || json.back() != '}') json = "{}";
    // Drop any slice already there, with the comma that joined it.
    if (const size_t key = json.find("\"slice\""); key != std::string::npos) {
        const size_t open = json.find('"', json.find(':', key + 7) + 1);
        const size_t close = open == std::string::npos ? std::string::npos : json.find('"', open + 1);
        if (close != std::string::npos) {
            size_t from = key, to = close + 1;
            if (to < json.size() && json[to] == ',') {
                ++to;
            } else if (from > 0) {
                size_t back = from - 1;
                while (back > 0 && std::isspace(static_cast<unsigned char>(json[back]))) --back;
                if (json[back] == ',') from = back;
            }
            json.erase(from, to - from);
        }
    }
    if (!slice_id.empty()) {
        const bool empty = json.find_first_not_of(" {}") == std::string::npos;
        const std::string saved = std::abs(power - 1.0) < 1e-6 ? std::string(slice_id) : std::format("{}:{:.2f}", slice_id, power);
        json.insert(json.size() - 1, std::format("{}\"slice\":\"{}\"", empty ? "" : ",", saved));
    }
    item.color_json = json;
}

void apply_slice(WeaponDef& w, const SliceDef& s) {
    w.damage *= s.damage;
    if (w.damage_final > 0) w.damage_final *= s.damage;
    w.shot_delay /= std::max(0.1, s.rate);
    w.heat_per_shot *= s.heat;
    w.yaw_spread *= s.spread;
    w.pitch_spread *= s.spread;
    w.spread_per_shot *= s.spread;
    w.damage_transition_delay *= s.range;
    w.bolt_life *= s.range;
    w.reload_time *= s.reload;
}

void apply_slice(Gear& gear, const SliceDef& s) {
    gear.modifiers.insert(gear.modifiers.end(), s.modifiers.begin(), s.modifiers.end());
}

// ---------------------------------------------------------------------------
// Entertainer

double inspiration_strength(int entertainer_level) {
    return 0.6 + 0.4 * double(std::clamp(entertainer_level, 1, profession_max_level)) / profession_max_level;
}

int64_t performance_tips(int entertainer_level, int audience) {
    return 1 + std::clamp(entertainer_level, 0, profession_max_level) / 50 + std::clamp(audience, 0, 8);
}

double inspiration_gain(double seconds, int band_size, bool band_leader, bool flourish) {
    // Each performer past the first a quarter more (a Band Leader's a half), up to a band of four.
    const double band = double(std::clamp(band_size, 1, 4) - 1) * (band_leader ? 0.5 : 0.25);
    return std::max(0.0, seconds) * inspiration_per_second * (1 + band) * (flourish ? 2 : 1);
}

std::string format_inspiration(const Inspiration& insp) {
    if (insp.id.empty() || insp.remaining <= 0) return {};
    return std::format("{} {:.0f} {:.3f}", insp.id, insp.remaining, insp.strength);
}

std::optional<Inspiration> parse_inspiration(std::string_view text) {
    std::istringstream in{std::string(text)};
    Inspiration insp;
    if (!(in >> insp.id >> insp.remaining >> insp.strength) || insp.id.empty() || insp.remaining <= 0) {
        return std::nullopt;
    }
    insp.strength = std::clamp(insp.strength, 0.0, 1.0);
    return insp;
}

TimedEffect inspiration_effect(const InspirationDef& def, const Inspiration& insp) {
    TimedEffect e;
    e.id = "inspiration";
    e.name = "Inspired: " + def.name;
    e.remaining = e.duration = std::max(0.0, insp.remaining);
    e.flags = effect_flag::inspired;
    for (StatModifier m : def.modifiers) {
        m.add *= insp.strength;
        m.multiply = 1 + (m.multiply - 1) * insp.strength;
        e.modifiers.push_back(m);
    }
    return e;
}

std::string bounty_mark_name(double first_roll, double last_roll) {
    static constexpr std::array<std::string_view, 16> firsts{"Dorn", "Vesk", "Talla", "Krin", "Mavo", "Shera", "Brask", "Oro",
                                                             "Jhess", "Tyber", "Nalla", "Corvin", "Rusk", "Ilsa", "Garro", "Pell"};
    static constexpr std::array<std::string_view, 14> lasts{"Rask", "Vantor", "Oskane", "Drell", "Mokk", "Sarn", "Tevari",
                                                            "Hoult", "Brennick", "Zayne", "Quell", "Marrow", "Kesh", "Dunmore"};
    const auto pick = [](const auto& list, double roll) {
        return list[std::min(list.size() - 1, size_t(std::clamp(roll, 0.0, 1.0) * double(list.size())))];
    };
    return std::format("{} {}", pick(firsts, first_roll), pick(lasts, last_roll));
}

std::string quarry_name(double roll) {
    static constexpr std::array<std::string_view, 12> names{"Old Scar",  "Ironhide",  "Red Fang",   "Bone-Eater",
                                                            "Greymane",  "Nightstalker", "Broken Horn", "Ashback",
                                                            "Longtooth", "Dune Ghost", "Splitclaw",  "the Widowmaker"};
    return std::string(names[std::min(names.size() - 1, size_t(std::clamp(roll, 0.0, 1.0) * double(names.size())))]);
}

std::string format_posted_bounties(std::span<const PostedBounty> bounties) {
    std::string out;
    for (const PostedBounty& b : bounties) {
        out += std::format("{}\t{}\t{}\t{}\t{}\n", b.poster, b.poster_name, b.target, b.amount, b.expires);
    }
    return out;
}

std::vector<PostedBounty> parse_posted_bounties(std::string_view text) {
    std::vector<PostedBounty> out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        std::vector<std::string> f;
        size_t a = 0;
        while (true) {
            const size_t b = line.find('\t', a);
            f.emplace_back(line.substr(a, b == std::string_view::npos ? std::string_view::npos : b - a));
            if (b == std::string_view::npos) break;
            a = b + 1;
        }
        if (f.size() != 5) continue;
        try {
            out.push_back({std::stoull(f[0]), f[1], f[2], std::stoll(f[3]), std::stoll(f[4])});
        } catch (const std::exception&) {
            continue;  // (a damaged line)
        }
    }
    return out;
}

}  // namespace swm::core
