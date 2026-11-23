#include "swm/core/items.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "swm/core/ini.h"

namespace swm::core {
namespace {

const std::array<WeaponArchetypeInfo, 7> archetype_table{{
    {WeaponArchetype::rifle_auto, "rifle_auto", "imp_weap_inf_rifle", "rifle", "primary"},
    {WeaponArchetype::carbine_burst, "carbine_burst", "all_weap_inf_rifle", "rifle", "primary"},
    {WeaponArchetype::sniper, "sniper", "imp_weap_inf_sniper_rifle", "rifle", "primary"},
    {WeaponArchetype::scatter, "scatter", "imp_weap_inf_shotgun", "rifle", "primary"},
    {WeaponArchetype::pistol, "pistol", "imp_weap_inf_pistol", "pistol", "sidearm"},
    {WeaponArchetype::heavy_launcher, "heavy_launcher", "imp_weap_inf_rocket_launcher", "bazooka", "primary"},
    {WeaponArchetype::grenade, "grenade", "imp_weap_inf_thermaldetonator", "grenade", "secondary"},
}};

WeaponDef default_archetype_weapon(WeaponArchetype a) {
    WeaponDef w;
    switch (a) {
    case WeaponArchetype::rifle_auto:
        w.name = "imp_weap_inf_rifle";
        w.shot_delay = 0.166;
        w.rounds_per_clip = 50;
        w.damage = 60;
        w.bolt_speed = 300;
        w.bolt_life = 0.8;
        w.zoom_min = 1.5;
        w.zoom_max = 2.5;
        break;
    case WeaponArchetype::carbine_burst:
        w.name = "all_weap_inf_rifle";
        w.shot_delay = 0.135;
        w.rounds_per_clip = 45;
        w.damage = 50;
        w.bolt_speed = 300;
        w.bolt_life = 0.7;
        w.zoom_min = 1.5;
        w.zoom_max = 2.0;
        break;
    case WeaponArchetype::sniper:
        w.name = "imp_weap_inf_sniper_rifle";
        w.shot_delay = 0.9;
        w.rounds_per_clip = 6;
        w.damage = 300;
        w.bolt_speed = 600;
        w.bolt_life = 1.2;
        w.yaw_spread = 0.05;
        w.pitch_spread = 0.05;
        w.zoom_min = 2.0;
        w.zoom_max = 8.0;
        w.zoom_first_person = true;
        break;
    case WeaponArchetype::scatter:
        w.name = "imp_weap_inf_shotgun";
        w.shot_delay = 0.75;
        w.rounds_per_clip = 8;
        w.shots_per_salvo = 6;
        w.damage = 36;
        w.bolt_speed = 240;
        w.bolt_life = 0.25;
        w.yaw_spread = 3.5;
        w.pitch_spread = 3.5;
        break;
    case WeaponArchetype::pistol:
        w.name = "imp_weap_inf_pistol";
        w.shot_delay = 0.22;
        w.rounds_per_clip = 0;
        w.heat_per_shot = 0.14;
        w.heat_recover_rate = 0.45;
        w.heat_threshold = 0.35;
        w.damage = 55;
        w.bolt_speed = 260;
        w.bolt_life = 0.55;
        break;
    case WeaponArchetype::heavy_launcher:
        w.name = "imp_weap_inf_rocket_launcher";
        w.projectile = Projectile::missile;
        w.damage_type = DamageType::explosive;
        w.shot_delay = 1.5;
        w.rounds_per_clip = 1;
        w.ammo = 6;
        w.reload_time = 2.2;
        w.damage = 300;
        w.explosion_damage = 280;
        w.explosion_inner = 2.0;
        w.explosion_outer = 6.0;
        w.explosion_push = 8.0;
        w.push_inner = 2.0;
        w.push_outer = 6.0;
        w.bolt_speed = 45;
        w.bolt_gravity = 0.0;
        w.bolt_life = 4.0;
        break;
    case WeaponArchetype::grenade:
        w.name = "imp_weap_inf_thermaldetonator";
        w.projectile = Projectile::grenade;
        w.damage_type = DamageType::explosive;
        w.shot_delay = 0.8;
        w.rounds_per_clip = 1;
        w.ammo = 3;
        w.explode_on_impact = false;
        w.fuse = 0.6;
        w.timer_on_contact = true;
        w.rebound = 0.45;
        w.friction = 0.5;
        w.bolt_speed = 22;
        w.bolt_gravity = 1.0;
        w.bolt_life = 5.0;
        w.explosion_damage = 350;
        w.explosion_inner = 2.5;
        w.explosion_outer = 6.5;
        w.explosion_push = 10.0;
        w.push_inner = 2.5;
        w.push_outer = 6.5;
        break;
    }
    return w;
}

StatModifier parse_catalog_modifier(const std::string& text, const std::string& where) {
    std::istringstream fields(text);
    std::string name, op;
    double value = 0;
    if (!(fields >> name >> op >> value)) {
        throw std::runtime_error(where + ": Modifier must be '<stat> +|-|* <n>', got '" + text + "'");
    }
    const auto stat = stat_from_name(name);
    if (!stat) {
        throw std::runtime_error(where + ": unknown stat '" + name + "'");
    }
    StatModifier m{*stat};
    if (op == "+") m.add = value;
    else if (op == "-") m.add = -value;
    else if (op == "*") m.multiply = value;
    else throw std::runtime_error(where + ": bad operator '" + op + "'");
    return m;
}

} // namespace

const WeaponArchetypeInfo& weapon_archetype_info(WeaponArchetype a) {
    const size_t i = size_t(a);
    return archetype_table[i < archetype_table.size() ? i : 0];
}

std::optional<WeaponArchetype> weapon_archetype_from_name(std::string_view name) {
    for (const auto& info : archetype_table) {
        if (info.id == name) {
            return info.archetype;
        }
    }
    return std::nullopt;
}

std::optional<ItemKind> item_kind_from_name(std::string_view name) {
    if (name == "weapon") return ItemKind::weapon;
    if (name == "armor") return ItemKind::armor;
    if (name == "deflector_shield") return ItemKind::deflector_shield;
    if (name == "clothing") return ItemKind::clothing;
    if (name == "junk") return ItemKind::junk;
    if (name == "supply") return ItemKind::supply;
    return std::nullopt;
}

std::string_view item_kind_name(ItemKind k) {
    switch (k) {
    case ItemKind::weapon: return "weapon";
    case ItemKind::armor: return "armor";
    case ItemKind::deflector_shield: return "deflector_shield";
    case ItemKind::clothing: return "clothing";
    case ItemKind::junk: return "junk";
    case ItemKind::supply: return "supply";
    }
    return "weapon";
}

void ItemCatalog::load(const std::filesystem::path& catalog_ini, const std::filesystem::path& bf2_items_dir) {
    items_.clear();
    index_.clear();
    bf2_bases_.clear();

    const Ini ini = Ini::load(catalog_ini);
    const std::string where = catalog_ini.string();

    for (const Ini::Section& s : ini.sections()) {
        if (s.name != "item") {
            continue;
        }
        ItemDef def;
        def.id = s.get("Id").value_or("");
        if (def.id.empty()) {
            throw std::runtime_error(where + ": [item] missing Id");
        }
        def.display_name = s.get("Name").value_or(def.id);
        def.description = s.get("Description").value_or("");
        const std::string kind_str = s.get("Kind").value_or("weapon");
        const auto kind = item_kind_from_name(kind_str);
        if (!kind) {
            throw std::runtime_error(where + ": unknown Kind '" + kind_str + "' on " + def.id);
        }
        def.kind = *kind;

        if (def.kind == ItemKind::weapon) {
            const std::string arch_str = s.get("Archetype").value_or("rifle_auto");
            const auto arch = weapon_archetype_from_name(arch_str);
            if (!arch) {
                throw std::runtime_error(where + ": unknown Archetype '" + arch_str + "' on " + def.id);
            }
            def.archetype = *arch;
            const auto& info = weapon_archetype_info(def.archetype);
            def.slot = s.get("Slot").value_or(info.default_slot);
            def.anim_set = s.get("AnimSet").value_or(info.anim_set);
            def.bf2_base = s.get("Bf2Base").value_or(info.bf2_weapon_base);
        } else {
            const char* default_slot = "chest";
            if (def.kind == ItemKind::deflector_shield) default_slot = "deflector_shield";
            else if (def.kind == ItemKind::junk) default_slot = "junk";
            else if (def.kind == ItemKind::supply) default_slot = "supply";
            def.slot = s.get("Slot").value_or(default_slot);
            def.anim_set.clear();
        }

        def.faction = s.get("Faction").value_or("neutral");
        def.cost_faction_points = int64_t(s.number("CostFactionPoints", 0));
        def.cost_credits = int64_t(s.number("CostCredits", 0));
        if (s.get("SellValue")) {
            def.sell_value = int64_t(s.number("SellValue", 0));
        } else {
            def.sell_value = def.cost_credits > 0 ? std::max<int64_t>(1, def.cost_credits / 4) : 0;
        }
        def.heal_amount = s.number("HealAmount", 0.0);
        def.model_path = s.get("Model").value_or("");
        def.damage_mult = s.number("DamageMult", 1.0);
        def.rate_mult = s.number("RateMult", 1.0);
        def.heat_mult = s.number("HeatMult", 1.0);
        def.spread_mult = s.number("SpreadMult", 1.0);
        def.range_mult = s.number("RangeMult", 1.0);
        def.clip_mult = s.number("ClipMult", 1.0);
        def.reload_mult = s.number("ReloadMult", 1.0);
        if (const auto dt = s.get("DamageType")) {
            def.damage_type = damage_type_from_name(*dt);
        }
        def.bolt_color_name = s.get("BoltColor").value_or("");
        for (const auto& [k, v] : s.values) {
            if (k == "Modifier") {
                def.modifiers.push_back(parse_catalog_modifier(v, where + " (" + def.id + ")"));
            }
        }

        if (def.kind == ItemKind::weapon && !def.bf2_base.empty() && !bf2_bases_.contains(def.bf2_base) &&
            !bf2_items_dir.empty()) {
            const auto p = bf2_items_dir / (def.bf2_base + ".ini");
            if (std::filesystem::exists(p)) {
                bf2_bases_[def.bf2_base] = load_weapon(p);
            }
        }

        index_[def.id] = items_.size();
        items_.push_back(std::move(def));
    }
}

const ItemDef* ItemCatalog::find(std::string_view id) const {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &items_[it->second];
}

WeaponDef ItemCatalog::resolve_weapon(const ItemDef& def, double quality, bool apply_quality) const {
    WeaponDef w = default_archetype_weapon(def.archetype);
    if (const auto it = bf2_bases_.find(def.bf2_base); it != bf2_bases_.end()) {
        w = it->second;
    }
    w.name = def.id;
    w.model = def.model_path;
    w.anim_set = def.anim_set;

    // Quality scales combat power within a narrow ±8% band in Gear queue;
    // Standard queue flattens quality to 0.5 (multiplier 1.0).
    const double q = std::clamp(apply_quality ? quality : 0.5, 0.0, 1.0);
    const double q_mult = 1.0 + (q - 0.5) * 0.16;

    w.damage *= def.damage_mult * q_mult;
    if (w.damage_final > 0) {
        w.damage_final *= def.damage_mult * q_mult;
    }
    w.explosion_damage *= def.damage_mult * q_mult;
    w.shot_delay /= std::max(0.1, def.rate_mult);
    w.heat_per_shot *= def.heat_mult / (1.0 + (q - 0.5) * 0.08);
    w.yaw_spread *= def.spread_mult;
    w.pitch_spread *= def.spread_mult;
    w.spread_per_shot *= def.spread_mult;
    w.bolt_life *= def.range_mult;
    w.damage_transition_delay *= def.range_mult;
    if (w.rounds_per_clip > 0) {
        w.rounds_per_clip = std::max(1, int(std::lround(w.rounds_per_clip * def.clip_mult)));
    }
    w.reload_time *= def.reload_mult;
    if (def.damage_type) {
        w.damage_type = *def.damage_type;
    }
    if (def.bolt_color_name == "blue") {
        w.bolt_color = {0.25, 0.55, 1.0, 0.85};
        w.bolt_texture = "com_sfx_laser_blue";
    } else if (def.bolt_color_name == "green") {
        w.bolt_color = {0.25, 0.95, 0.35, 0.85};
        w.bolt_texture = "com_sfx_laser_green";
    } else if (def.bolt_color_name == "yellow") {
        w.bolt_color = {1.0, 0.85, 0.2, 0.85};
        w.bolt_texture = "com_sfx_laser_yellow";
    } else if (def.bolt_color_name == "red") {
        w.bolt_color = {1.0, 0.25, 0.25, 0.85};
        w.bolt_texture = "com_sfx_laser_red";
    }
    w.modifiers = def.modifiers;
    return w;
}

Gear ItemCatalog::resolve_gear(const ItemDef& def, double quality, bool apply_quality) const {
    Gear g;
    g.name = def.id;
    g.slot = def.slot;
    const double q = std::clamp(apply_quality ? quality : 0.5, 0.0, 1.0);
    const double q_mult = 1.0 + (q - 0.5) * 0.16;
    for (StatModifier m : def.modifiers) {
        if (m.add > 0) {
            m.add *= q_mult;
        }
        g.modifiers.push_back(m);
    }
    return g;
}

Loadout ItemCatalog::build_loadout(const CharacterLoadout& cl, std::span<const ItemInstance> inventory, Species species,
                                   bool apply_quality) const {
    Loadout out;
    out.name = cl.name;
    out.weapons = {};

    const auto find_inst = [&](uint64_t id) -> const ItemInstance* {
        if (id == 0) return nullptr;
        for (const ItemInstance& inst : inventory) {
            if (inst.id == id) return &inst;
        }
        return nullptr;
    };

    const std::array<uint64_t, weapon_slots> w_ids{cl.primary_id, cl.sidearm_id, cl.secondary_id};
    for (size_t s = 0; s < weapon_slots; ++s) {
        if (const ItemInstance* inst = find_inst(w_ids[s])) {
            if (const ItemDef* def = find(inst->def_id)) {
                out.weapons[s] = resolve_weapon(*def, inst->quality, apply_quality);
            }
        }
    }

    for (const auto& [slot, item_id] : cl.gear_ids) {
        if (!species_can_wear_slot(species, slot)) {
            continue;
        }
        if (const ItemInstance* inst = find_inst(item_id)) {
            if (const ItemDef* def = find(inst->def_id)) {
                if (def->kind == ItemKind::armor || def->kind == ItemKind::deflector_shield) {
                    out.gear.push_back(resolve_gear(*def, inst->quality, apply_quality));
                }
            }
        }
    }

    if (!out.weapons[primary_slot] && !out.weapons[sidearm_slot]) {
        out.weapons[primary_slot] = WeaponDef{};
    }
    return out;
}

BattleReward compute_battle_reward(bool won, bool draw, uint16_t kills, uint16_t captures, QueueMode mode) {
    BattleReward r;
    if (won) {
        r = {250, 100, 200};
    } else if (draw) {
        r = {150, 60, 120};
    } else {
        r = {100, 40, 80};
    }
    r.xp += int64_t(kills) * 25 + int64_t(captures) * 60;
    r.faction_points += int64_t(kills) * 10 + int64_t(captures) * 25;
    r.credits += int64_t(kills) * 15 + int64_t(captures) * 40;
    if (mode == QueueMode::gear) {
        r.faction_points = int64_t(std::llround(double(r.faction_points) * 1.15));
        r.credits = int64_t(std::llround(double(r.credits) * 1.15));
    }
    return r;
}

namespace {

std::string substitute_gender(std::string path, Gender gender) {
    const std::string rep = gender == Gender::female ? "f" : "m";
    size_t pos = 0;
    while ((pos = path.find("{g}", pos)) != std::string::npos) {
        path.replace(pos, 3, rep);
        pos += rep.size();
    }
    return path;
}

std::string covered_armor_slot(std::string_view app_slot) {
    if (app_slot == "jacket" || app_slot == "shirt") return "chest";
    if (app_slot == "pants") return "leggings";
    if (app_slot == "boots_cosmetic" || app_slot == "boots") return "boots";
    return std::string(app_slot);
}

} // namespace

std::string resolve_worn_models(const ItemCatalog& catalog, const CharacterLoadout& cl,
                                std::span<const ItemInstance> inventory, Species species, Gender gender) {
    const auto find_inst = [&](uint64_t id) -> const ItemInstance* {
        if (id == 0) return nullptr;
        for (const ItemInstance& inst : inventory) {
            if (inst.id == id) return &inst;
        }
        return nullptr;
    };

    // Appearance slots visually override their matching armor slots.
    std::map<std::string, std::string> visual_by_slot;
    for (const auto& [slot, item_id] : cl.gear_ids) {
        if (!species_can_wear_slot(species, slot)) {
            continue;
        }
        if (const ItemInstance* inst = find_inst(item_id)) {
            if (const ItemDef* def = catalog.find(inst->def_id); def && !def->model_path.empty()) {
                visual_by_slot[slot] = substitute_gender(def->model_path, gender);
            }
        }
    }
    for (const auto& [app_slot, item_id] : cl.appearance_ids) {
        if (const ItemInstance* inst = find_inst(item_id)) {
            if (const ItemDef* def = catalog.find(inst->def_id); def && !def->model_path.empty()) {
                visual_by_slot[covered_armor_slot(app_slot)] = substitute_gender(def->model_path, gender);
            }
        }
    }

    std::string out;
    for (const auto& [slot, path] : visual_by_slot) {
        if (path.empty()) continue;
        if (!out.empty()) out += ',';
        out += path;
    }
    return out;
}

} // namespace swm::core
