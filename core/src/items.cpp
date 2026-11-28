#include "swm/core/items.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>

#include "swm/core/ini.h"
#include "swm/core/profession.h"

namespace swm::core {
namespace {

const std::array<WeaponArchetypeInfo, 17> archetype_table{{
    {WeaponArchetype::rifle_auto, "rifle_auto", "imp_weap_inf_rifle", "rifle", "primary"},
    {WeaponArchetype::carbine_burst, "carbine_burst", "all_weap_inf_rifle", "rifle", "primary"},
    {WeaponArchetype::sniper, "sniper", "imp_weap_inf_sniper_rifle", "rifle", "primary"},
    {WeaponArchetype::scatter, "scatter", "imp_weap_inf_shotgun", "rifle", "primary"},
    {WeaponArchetype::pistol, "pistol", "imp_weap_inf_pistol", "pistol", "sidearm"},
    {WeaponArchetype::heavy_launcher, "heavy_launcher", "imp_weap_inf_rocket_launcher", "bazooka", "primary"},
    {WeaponArchetype::grenade, "grenade", "imp_weap_inf_thermaldetonator", "grenade", "secondary"},
    {WeaponArchetype::melee, "melee", "", "melee", "sidearm"},
    // Beams: Battlefront II's chaingun, the closest thing it has to a stream.
    {WeaponArchetype::beam, "beam", "rep_weap_inf_chaingun", "rifle", "primary"},
    {WeaponArchetype::beam_cannon, "beam_cannon", "rep_weap_inf_chaingun", "rifle", "primary"},
    {WeaponArchetype::flamer, "flamer", "", "rifle", "primary"},
    {WeaponArchetype::scatter_pistol, "scatter_pistol", "", "pistol", "sidearm"},
    {WeaponArchetype::melee_2h, "melee_2h", "", "melee_2h", "primary"},
    // Tactical: Battlefront II's landmine (com_weap_inf_landmine) has no
    // exported definition, and our numbers are our own (see below).
    {WeaponArchetype::placed, "placed", "", "grenade", "secondary"},
    {WeaponArchetype::utility, "utility", "", "grenade", "secondary"},
    {WeaponArchetype::medpack, "medpack", "", "grenade", "secondary"},
}};

WeaponDef default_archetype_weapon(WeaponArchetype a) {
    {WeaponArchetype::jump_pack, "jump_pack", "", "grenade", "secondary"},
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
    case WeaponArchetype::beam:
    case WeaponArchetype::beam_cannon:
        // rep_weap_inf_chaingun's, where it isn't imported.
        w.name = "rep_weap_inf_chaingun";
        w.shot_delay = 0.05;
        w.rounds_per_clip = 0;
        w.reload_time = 0;
        w.heat_per_shot = 0.02;
        w.heat_recover_rate = 0.25;
        w.heat_threshold = 0.2;
        w.yaw_spread = 0.25;
        w.pitch_spread = 0.25;
        w.spread_per_shot = 0.125;
        w.spread_recover_rate = 1.0;
        w.spread_limit = 2.5;
        w.zoom_min = 2.5;
        w.zoom_max = 2.5;
        w.damage = 25;
        w.bolt_speed = 20000;
        w.bolt_gravity = 0.1;
        w.bolt_life = 0.05;
        break;
    case WeaponArchetype::flamer:
        // Battlefront II has none: a cone of slow, short-lived pellets that
        // drop (some 11 m), on heat. ASSUMPTION: by eye, about the
        // chaingun's damage a second before its effect.
        w.name = "swg_flamer";
        w.shot_delay = 0.1;
        w.rounds_per_clip = 0;
        w.reload_time = 0;
        w.heat_per_shot = 0.04;
        w.heat_recover_rate = 0.3;
        w.heat_threshold = 0.3;
        w.shots_per_salvo = 3;
        w.yaw_spread = 6;
        w.pitch_spread = 4;
        w.spread_per_shot = 0;
        w.damage = 9;
        w.bolt_speed = 18;
        w.bolt_gravity = 0.2;
        w.bolt_life = 0.6;
        break;
    case WeaponArchetype::scatter_pistol:
        // Battlefront II has none: the shotgun's spread on the pistol's heat,
        // about a pistol's damage a second up close; its pellets spread wide
        // and die out at 24 m.
        w.name = "swg_scatter_pistol";
        w.shot_delay = 0.6;
        w.rounds_per_clip = 0;
        w.heat_per_shot = 0.3;
        w.heat_recover_rate = 0.45;
        w.heat_threshold = 0.35;
        w.shots_per_salvo = 5;
        w.damage = 28;
        w.bolt_speed = 240;
        w.bolt_life = 0.1;
        w.yaw_spread = 5;
        w.pitch_spread = 5;
        break;
    case WeaponArchetype::placed:
        // Battlefront II's landmine lies 60 s and kills outright (8000
        // damage); ours lies as long but does about a thermal detonator's
        // damage, so a mine is a warning, not a sentence. Tossed a few meters,
        // it arms 1.5 s after it comes to rest, and goes off at 2 m.
        w.name = "swg_placed";
        w.projectile = Projectile::grenade;
        w.damage_type = DamageType::explosive;
        w.shot_delay = 1.0;
        w.rounds_per_clip = 1;
        w.ammo = 2;
        w.reload_time = 0;
        w.yaw_spread = 0;
        w.pitch_spread = 0;
        w.spread_per_shot = 0;
        w.explode_on_impact = false;
        w.fuse = 60;
        w.trigger_radius = 2.0;
        w.arm_time = 1.5;
        w.rebound = 0;
        w.friction = 1.0;
        w.bolt_speed = 6;
        w.bolt_gravity = 1.0;
        w.bolt_life = 60;
        w.damage = 40;  // a trap's bolt
        w.explosion_damage = 400;
        w.explosion_inner = 2.0;
        w.explosion_outer = 5.0;
        w.explosion_push = 8.0;
        w.push_inner = 2.0;
        w.push_outer = 5.0;
        w.fire_sound = "com_weap_throw";
        w.muzzle_flash.clear();
        break;
    case WeaponArchetype::utility:
        // A shot from the tactical slot, a few carried: a slow, lobbed bolt
        // whose effect is the point (a tangle's snare, a flare's fire).
        w.name = "swg_utility";
        w.shot_delay = 0.8;
        w.rounds_per_clip = 1;
        w.ammo = 3;
        w.reload_time = 0;
        w.yaw_spread = 0;
        w.pitch_spread = 0;
        w.spread_per_shot = 0;
        w.damage = 20;
        w.bolt_speed = 60;
        w.bolt_gravity = 0.3;
        w.bolt_life = 0.8;
        break;
    case WeaponArchetype::medpack:
        // No shot: a heal (its item's HealAmount, HealTime), two a life (`ammo`,
        // in hand and in reserve), a second to use.
        w.name = "swg_medpack";
        w.projectile = Projectile::heal;
        w.shot_delay = 1.0;
        w.rounds_per_clip = 1;
        w.ammo = 2;
        w.reload_time = 0;
        w.yaw_spread = 0;
        w.pitch_spread = 0;
        w.spread_per_shot = 0;
        w.damage = 0;
        w.fire_sound = "com_weap_throw";
        w.muzzle_flash.clear();
        break;
    case WeaponArchetype::melee:
    case WeaponArchetype::melee_2h:
        // One-handed: a sidearm. Two-handed is a primary, so it outweighs
    case WeaponArchetype::jump_pack:
        // No shot: a hop (its item's Jump), three a life, a second and a half apart.
        w.name = "swg_jump_pack";
        w.projectile = Projectile::jump;
        w.jump = 11;
        w.shot_delay = 1.5;
        w.rounds_per_clip = 1;
        w.ammo = 3;
        w.reload_time = 0;
        w.yaw_spread = 0;
        w.pitch_spread = 0;
        w.spread_per_shot = 0;
        w.damage = 0;
        w.fire_sound.clear();
        w.muzzle_flash.clear();
        break;
        // it: slower swings that hit harder, about a fifth more damage a
        // second, with more reach, a wider arc and more stagger, and a
        // shorter lunge.
        w.name = a == WeaponArchetype::melee ? "swg_melee" : "swg_melee_2h";
        w.projectile = Projectile::melee;
        w.damage_type = DamageType::melee;
        w.anim_set = a == WeaponArchetype::melee ? "melee" : "melee_2h";
        w.rounds_per_clip = 0;
        w.fire_sound = "com_weap_throw";
        w.impact_effect = "com_sfx_ord_exp";
        w.muzzle_flash.clear();
        if (a == WeaponArchetype::melee) {
            w.shot_delay = 0.55;
            w.damage = 115;
            w.melee_range = 2.6;
            w.melee_arc = 70.0;
            w.melee_lunge = 5.0;
            w.melee_stagger = 0.35;
            w.shield_disrupt = 2.5;
        } else {
            w.shot_delay = 0.65;
            w.damage = 165;
            w.melee_range = 3.2;
            w.melee_arc = 90.0;
            w.melee_lunge = 4.5;
            w.melee_stagger = 0.5;
            w.shield_disrupt = 3.0;
        }
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
    if (name == "quest") return ItemKind::quest;
    if (name == "vehicle") return ItemKind::vehicle;
    if (name == "enhancement") return ItemKind::enhancement;
    if (name == "ammo") return ItemKind::ammo;
    if (name == "bodysuit") return ItemKind::bodysuit;
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
    case ItemKind::quest: return "quest";
    case ItemKind::vehicle: return "vehicle";
    case ItemKind::enhancement: return "enhancement";
    case ItemKind::ammo: return "ammo";
    case ItemKind::bodysuit: return "bodysuit";
    }
    return "weapon";
}

namespace {

std::string ascii_lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return out;
}

} // namespace

std::string normalize_faction(std::string_view faction) {
    const std::string f = ascii_lower(faction);
    if (f == "rebel" || f == "rebellion" || f == "alliance") return "rebellion";
    if (f == "imp" || f == "empire" || f == "imperial") return "empire";
    if (f == "rep" || f == "republic") return "republic";
    if (f == "cis" || f == "sep" || f == "separatist" || f == "separatists") return "separatists";
    if (f == "neutral" || f.empty()) return "neutral";
    return f;
}

std::string opposing_faction(std::string_view faction) {
    const std::string f = normalize_faction(faction);
    if (f == "rebellion") return "empire";
    if (f == "empire") return "rebellion";
    if (f == "republic") return "separatists";
    if (f == "separatists") return "republic";
    return {};
}

int64_t clamp_reputation(int64_t reputation) {
    return std::clamp<int64_t>(reputation, -42000, 42999);
}

RepTier reputation_tier(int64_t reputation) {
    if (reputation < -3000) return RepTier::hated;
    if (reputation < 0) return RepTier::unfriendly;
    if (reputation < 3000) return RepTier::neutral;
    if (reputation < 9000) return RepTier::friendly;
    if (reputation < 21000) return RepTier::honored;
    if (reputation < 42000) return RepTier::revered;
    return RepTier::exalted;
}

std::string_view reputation_tier_name(RepTier tier) {
    switch (tier) {
    case RepTier::hated: return "Hated";
    case RepTier::unfriendly: return "Unfriendly";
    case RepTier::neutral: return "Neutral";
    case RepTier::friendly: return "Friendly";
    case RepTier::honored: return "Honored";
    case RepTier::revered: return "Revered";
    case RepTier::exalted: return "Exalted";
    }
    return "Neutral";
}

std::optional<RepTier> reputation_tier_from_name(std::string_view name) {
    const std::string low = ascii_lower(name);
    if (low == "hated") return RepTier::hated;
    if (low == "unfriendly") return RepTier::unfriendly;
    if (low == "neutral") return RepTier::neutral;
    if (low == "friendly") return RepTier::friendly;
    if (low == "honored") return RepTier::honored;
    if (low == "revered") return RepTier::revered;
    if (low == "exalted") return RepTier::exalted;
    return std::nullopt;
}

int64_t reputation_threshold(RepTier tier) {
    switch (tier) {
    case RepTier::hated: return -42000;
    case RepTier::unfriendly: return -3000;
    case RepTier::neutral: return 0;
    case RepTier::friendly: return 3000;
    case RepTier::honored: return 9000;
    case RepTier::revered: return 21000;
    case RepTier::exalted: return 42000;
    }
    return 0;
}

namespace {

void apply_bolt_color(WeaponDef& target, std::string_view color_name) {
    if (color_name == "blue") {
        target.bolt_color = {0.25, 0.55, 1.0, 0.85};
        target.bolt_texture = "com_sfx_laser_blue";
    } else if (color_name == "green") {
        target.bolt_color = {0.25, 0.95, 0.35, 0.85};
        target.bolt_texture = "com_sfx_laser_green";
    } else if (color_name == "yellow") {
        target.bolt_color = {1.0, 0.85, 0.2, 0.85};
        target.bolt_texture = "com_sfx_laser_yellow";
    } else if (color_name == "red") {
        target.bolt_color = {1.0, 0.25, 0.25, 0.85};
        target.bolt_texture = "com_sfx_laser_red";
    }
}

// What an item (a weapon, or the ammo it fires) adds to a weapon beyond its
// damage, rate, heat and spread: bolt speed, headshots and crits, damage to
// vehicles and shields, handling, and its hits' effect.
void apply_item_extras(WeaponDef& w, const ItemDef& def, const HitEffect* effect) {
    if (const double v = std::max(0.1, def.velocity_mult); v != 1.0 && w.projectile != Projectile::grenade &&
                                                         w.projectile != Projectile::melee) {
        w.bolt_speed *= v;
        w.bolt_life /= v;  // as far as before, sooner
        w.damage_transition_delay /= v;
        w.damage_transition_period /= v;
    }
    w.headshot_multiplier *= std::max(0.0, def.headshot_mult);
    w.crit_chance = std::clamp(w.crit_chance + def.crit_chance, 0.0, 1.0);
    if (def.crit_mult > 0) w.crit_multiplier = def.crit_mult;
    w.vehicle_factor *= std::max(0.0, def.vehicle_mult);
    w.explosion_vehicle_factor *= std::max(0.0, def.vehicle_mult);
    w.shield_factor *= std::max(0.0, def.shield_mult);
    w.swap_time *= std::max(0.0, def.swap_mult);
    if (def.recoil >= 0) w.recoil = def.recoil;
    w.aim_speed *= std::max(0.1, def.aim_speed_mult);
    if (effect != nullptr) w.hit_effect = *effect;
}

} // namespace

// A piece's modifiers: its perks, then its slot's share of its class's
// budget, the typed part spread as its Resist says.
void apply_armor_budget(ItemDef& def);

void ItemCatalog::load(const std::filesystem::path& catalog_ini, const std::filesystem::path& bf2_items_dir) {
    items_.clear();
    index_.clear();
    bf2_bases_.clear();
    hit_effects_.clear();
    species_looks_.clear();

    const Ini ini = Ini::load(catalog_ini);
    const std::string where = catalog_ini.string();

    for (const Ini::Section& s : ini.sections()) {
        if (s.name == "species_armor") {
            SpeciesArmorLook look;
            const std::string sp = s.get("Species").value_or("");
            const auto species = species_from_name(sp);
            look.armor_class = s.get("ArmorClass").value_or("");
            look.slot = s.get("Slot").value_or("");
            look.set_name = s.get("Name").value_or("");
            look.model_path = s.get("Model").value_or("");
            look.swg_template = s.get("Template").value_or("");
            if (!species ||
                (look.armor_class != "light" && look.armor_class != "medium" && look.armor_class != "heavy") ||
                !is_worn_slot(look.slot) ||
                look.model_path.empty()) {
                throw std::runtime_error(where + ": [species_armor] " + look.set_name + " " + look.slot +
                                         " needs a Species, an ArmorClass (light, medium or heavy), a worn Slot "
                                         "and a Model");
            }
            look.species = *species;
            species_looks_.push_back(std::move(look));
            continue;
        }
        if (s.name == "hit_effect") {
            HitEffect e;
            e.id = s.get("Id").value_or("");
            if (e.id.empty()) {
                throw std::runtime_error(where + ": [hit_effect] missing Id");
            }
            e.name = s.get("Name").value_or(e.id);
            e.chance = std::clamp(s.number("Chance", 1.0), 0.0, 1.0);
            e.duration = std::max(0.0, s.number("Duration", 0.0));
            e.damage_per_second = std::max(0.0, s.number("DamagePerSecond", 0.0));
            if (const auto dt = s.get("DamageType")) {
                const auto type = damage_type_from_name(*dt);
                if (!type) throw std::runtime_error(where + ": unknown DamageType '" + *dt + "' on " + e.id);
                e.damage_type = *type;
            }
            for (const auto& [k, v] : s.values) {
                if (k == "Modifier") e.modifiers.push_back(parse_catalog_modifier(v, where + " (" + e.id + ")"));
            }
            hit_effects_.push_back(std::move(e));
            continue;
        }
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

        if (const auto ac_str = s.get("AmmoClass")) {
            const auto ac = ammo_class_from_name(*ac_str);
            if (!ac) throw std::runtime_error(where + ": unknown AmmoClass '" + *ac_str + "' on " + def.id);
            def.ammo_class = *ac;
        }
        def.ammo_batch = std::max(1, int(s.number("AmmoBatch", def.ammo_class == AmmoClass::heavy ? 12 : 100)));
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
            else if (def.kind == ItemKind::quest) default_slot = "quest";
            else if (def.kind == ItemKind::enhancement) default_slot = "enhancement";
            else if (def.kind == ItemKind::vehicle) default_slot = "vehicle";
            else if (def.kind == ItemKind::ammo) {
                if (def.ammo_class == AmmoClass::none) def.ammo_class = AmmoClass::standard;
                default_slot = def.ammo_class == AmmoClass::heavy ? "ammo_heavy" : "ammo_standard";
            }
            def.slot = s.get("Slot").value_or(default_slot);
            def.anim_set.clear();
            // A bodysuit has the override's one slot of its own; armor and
            // clothing, a worn slot.
            if (def.kind == ItemKind::bodysuit) {
                def.slot = std::string(full_body_slot);
            } else if ((def.kind == ItemKind::armor || def.kind == ItemKind::clothing) && !is_worn_slot(def.slot)) {
                throw std::runtime_error(where + " (" + def.id + "): Slot " + def.slot +
                                         " isn't a worn slot (a whole-body look is a bodysuit)");
            }
        }

        def.faction = normalize_faction(s.get("Faction").value_or("neutral"));
        if (const auto rank_str = s.get("RequiredRank")) {
            const auto parsed_rank = reputation_tier_from_name(*rank_str);
            if (!parsed_rank) {
                throw std::runtime_error(where + ": unknown RequiredRank '" + *rank_str + "' on " + def.id);
            }
            def.required_rank = *parsed_rank;
        }
        def.cost_faction_points = int64_t(s.number("CostFactionPoints", 0));
        def.cost_credits = int64_t(s.number("CostCredits", 0));
        if (s.get("SellValue")) {
            def.sell_value = int64_t(s.number("SellValue", 0));
        } else {
            def.sell_value = def.cost_credits > 0 ? std::max<int64_t>(1, def.cost_credits / 4) : 0;
        }
        def.heal_amount = s.number("HealAmount", 0.0);
        def.heal_time = std::max(0.0, s.number("HealTime", 0.0));
        def.carried = std::max(0, int(s.number("Carried", 0)));
        def.teaches = s.get("Teaches").value_or("");
        def.vehicle = s.get("Vehicle").value_or("");
        if (def.kind == ItemKind::vehicle && def.vehicle.empty()) {
            throw std::runtime_error(where + " (" + def.id + "): a vehicle needs Vehicle = its class");
        }
        if (def.kind == ItemKind::armor) {
            def.armor_class = s.get("ArmorClass").value_or("medium");  // a soldier's, unless it says
            if (def.armor_class != "light" && def.armor_class != "medium" && def.armor_class != "heavy") {
                throw std::runtime_error(where + " (" + def.id + "): ArmorClass is light, medium or heavy");
            }
        }
        def.swg_template = s.get("Template").value_or("");
        def.model_path = s.get("Model").value_or(def.swg_template.empty() ? "" : "weapons/" + def.id + "/" + def.id + ".gltf");
        if (const auto f = s.get("ModelForward")) {
            if (*f == "+y") def.model_forward = 1;
            else if (*f == "-y") def.model_forward = -1;
            else throw std::runtime_error(where + " (" + def.id + "): ModelForward is +y or -y");
        }
        if (const auto m = s.get("ModelMuzzle")) {
            if (*m != "front") throw std::runtime_error(where + " (" + def.id + "): ModelMuzzle is front");
            def.model_add_muzzle = true;
        }
        def.damage_mult = s.number("DamageMult", 1.0);
        def.rate_mult = s.number("RateMult", 1.0);
        def.heat_mult = s.number("HeatMult", 1.0);
        def.spread_mult = s.number("SpreadMult", 1.0);
        def.range_mult = s.number("RangeMult", 1.0);
        def.clip_mult = s.number("ClipMult", 1.0);
        def.reload_mult = s.number("ReloadMult", 1.0);
        def.melee_range = s.number("MeleeRange", 0.0);
        def.melee_arc = s.number("MeleeArc", 0.0);
        def.melee_lunge = s.number("MeleeLunge", 0.0);
        def.melee_stagger = s.number("MeleeStagger", 0.0);
        def.shield_disrupt = s.number("ShieldDisrupt", 0.0);
        def.trap_shots = std::max(0, int(s.number("TrapShots", 0)));
        def.velocity_mult = s.number("VelocityMult", 1.0);
        def.headshot_mult = s.number("HeadshotMult", 1.0);
        def.crit_chance = s.number("CritChance", 0.0);
        def.crit_mult = s.number("CritMult", 0.0);
        def.vehicle_mult = s.number("VehicleMult", 1.0);
        def.shield_mult = s.number("ShieldMult", 1.0);
        def.swap_mult = s.number("SwapMult", 1.0);
        def.recoil = s.number("Recoil", -1.0);
        def.aim_speed_mult = s.number("AimSpeedMult", 1.0);
        def.hit_effect = s.get("OnHit").value_or("");
        if (const auto sp = s.get("Species")) {
            std::stringstream names(*sp);
            std::string name;
            while (std::getline(names, name, ',')) {
                name.erase(0, name.find_first_not_of(" \t"));
                name.erase(name.find_last_not_of(" \t") + 1);
                if (name.empty()) continue;
                const auto found = species_from_name(name);
                if (!found) throw std::runtime_error(where + " (" + def.id + "): unknown Species " + name);
                def.species.push_back(*found);
            }
        }
        if (const auto dt = s.get("DamageType")) {
            def.damage_type = damage_type_from_name(*dt);
        }
        def.bolt_color_name = s.get("BoltColor").value_or("");
        if (const auto g = s.get("Grade")) {
            def.grade = item_grade_from_name(*g);
            if (!def.grade) throw std::runtime_error(where + " (" + def.id + "): unknown Grade " + *g);
        }
        for (const auto& [k, v] : s.values) {
            if (k == "Modifier") {
                def.modifiers.push_back(parse_catalog_modifier(v, where + " (" + def.id + ")"));
            }
        }
        for (const auto& [k, v] : s.values) {
            if (k != "Paint") continue;
            // "<scheme>: <variable>=<index>, ...", kept as a query string.
            const size_t colon = v.find(':');
            std::string scheme = v.substr(0, colon == std::string::npos ? 0 : colon);
            scheme.erase(0, scheme.find_first_not_of(" \t"));
            scheme.erase(scheme.find_last_not_of(" \t") + 1);
            std::string query;
            std::stringstream parts(colon == std::string::npos ? "" : v.substr(colon + 1));
            std::string part;
            while (std::getline(parts, part, ',')) {
                part.erase(0, part.find_first_not_of(" \t"));
                part.erase(part.find_last_not_of(" \t") + 1);
                if (part.empty()) continue;
                if (part.find('=') == std::string::npos) {
                    throw std::runtime_error(where + " (" + def.id + "): Paint is '<scheme>: <variable>=<index>, ...'");
                }
                query += (query.empty() ? "" : "&") + part;
        def.jump = std::max(0.0, s.number("Jump", 0.0));
            }
            if (scheme.empty() || query.empty()) {
                throw std::runtime_error(where + " (" + def.id + "): Paint is '<scheme>: <variable>=<index>, ...'");
            }
            def.paints[normalize_faction(scheme)] = query;  // a faction's, or "neutral"
        }
        for (const auto& [k, v] : s.values) {
            if (k != "Covers") continue;
            std::stringstream names(v);
            for (std::string name; std::getline(names, name, ',');) {
                name.erase(0, name.find_first_not_of(" \t"));
                name.erase(name.find_last_not_of(" \t") + 1);
                if (!is_worn_slot(name)) {
                    throw std::runtime_error(where + " (" + def.id + "): Covers names worn slots, got " + name);
                }
                def.covers.push_back(name);
            }
        }
        def.module_for = s.get("ModuleFor").value_or("");
        if (!def.module_for.empty() && def.module_for != "ranged" && def.module_for != "melee" &&
            def.module_for != "armor" && def.module_for != "shield") {
            throw std::runtime_error(where + " (" + def.id + "): ModuleFor is ranged, melee, armor or shield");
        }
        if (def.kind == ItemKind::armor || def.kind == ItemKind::enhancement) {
            // Its perk is what it lists; its slot's share of its class's
            // budget comes on top, the typed part spread as Resist says (an
            // armor module's Resist: where it moves a piece's).
            def.perks = def.modifiers;
            if (const auto r = s.get("Resist")) {
                std::stringstream parts(*r);
                std::string part;
                double total = 0;
                while (std::getline(parts, part, ',')) {
                    const size_t colon = part.find(':');
                    std::string name = part.substr(0, colon);
                    name.erase(0, name.find_first_not_of(" \t"));
                    name.erase(name.find_last_not_of(" \t") + 1);
                    const auto type = damage_type_from_name(name);
                    if (!type || colon == std::string::npos) {
                        throw std::runtime_error(where + " (" + def.id + "): Resist is '<type>:<share>, ...', got '" +
                                                 *r + "'");
                    }
                    const double share = std::stod(part.substr(colon + 1));
                    def.resist.emplace_back(*type, share);
                    total += share;
                }
                if (std::abs(total - 1.0) > 0.01) {
                    throw std::runtime_error(where + " (" + def.id + "): Resist's shares add up to 1");
                }
            }
            if (def.kind == ItemKind::armor) apply_armor_budget(def);
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
    for (const ItemDef& def : items_) {
        if (!def.hit_effect.empty() && hit_effect(def.hit_effect) == nullptr) {
            throw std::runtime_error(where + ": " + def.id + " has OnHit = " + def.hit_effect + ", no [hit_effect] of that Id");
        }
    }
}

void apply_armor_budget(ItemDef& def) {
    def.modifiers = def.perks;
    const ArmorBudget& b = armor_budget(def.armor_class);
    const double share = armor_slot_share(def.slot);
    if (share <= 0) return;
    def.modifiers.push_back({Stat::mitigation, b.mitigation * share});
    for (const auto& [type, part] : def.resist) {
        const Stat stat = Stat(size_t(Stat::mitigation_ballistic) + size_t(type));
        def.modifiers.push_back({stat, b.typed * share * part});
    }
    def.modifiers.push_back({Stat::max_health, b.health * share});
    const auto mult = [&](Stat stat, double full) {
        if (full != 1.0) def.modifiers.push_back({stat, 0, std::pow(full, share)});
    };
    mult(Stat::run_speed, b.run_speed);
    mult(Stat::sprint_factor, b.sprint_factor);
    mult(Stat::energy_restore, b.energy_restore);
}

ItemGrade clamp_grade(int grade) { return ItemGrade(std::clamp(grade, 0, item_grade_count - 1)); }

std::string_view item_grade_name(ItemGrade grade) {
    static constexpr std::array<std::string_view, item_grade_count> names{"Flawed", "Standard", "Fine", "Exceptional",
                                                                          "Legendary"};
    return names[size_t(clamp_grade(int(grade)))];
}

std::optional<ItemGrade> item_grade_from_name(std::string_view name) {
    for (int g = 0; g < item_grade_count; ++g) {
        const std::string_view n = item_grade_name(ItemGrade(g));
        if (n.size() == name.size() &&
            std::equal(n.begin(), n.end(), name.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            })) {
            return ItemGrade(g);
        }
    }
    return std::nullopt;
}

double grade_stat_mult(ItemGrade grade) {
    static constexpr std::array<double, item_grade_count> mults{0.92, 1.0, 1.08, 1.16, 1.16};
    return mults[size_t(clamp_grade(int(grade)))];
}

ItemGrade grade_from_quality(double quality) {
    // Quality moved stats by (q - 0.5) x 16%: 0 was -8% (Flawed), 1 +8% (Fine).
    return quality < 0.25 ? ItemGrade::flawed : quality >= 0.75 ? ItemGrade::fine : ItemGrade::standard;
}

ItemGrade saved_grade(std::string_view text) {
    const std::string s(text);
    if (s.find('.') != std::string::npos) return grade_from_quality(std::atof(s.c_str()));
    return s.empty() ? ItemGrade::standard : clamp_grade(std::atoi(s.c_str()));
}

bool item_takes_grade(const ItemDef& def) {
    return def.kind == ItemKind::weapon || def.kind == ItemKind::armor || def.kind == ItemKind::deflector_shield;
}

std::optional<ItemGrade> shown_grade(const ItemDef& def, ItemGrade instance_grade) {
    if (def.grade) return def.grade;
    if (item_takes_grade(def)) return instance_grade;
    return std::nullopt;
}

int clamp_mark(int mark) { return std::clamp(mark, 0, max_mark); }

int mark_level(int mark) { return std::max(1, clamp_mark(mark) * 10); }

double mark_scale(int mark) { return 1.0 + 0.1 * clamp_mark(mark); }

std::string mark_name(int mark) {
    static constexpr std::array<const char*, max_mark + 1> numerals{"",   "I",   "II", "III", "IV", "V",
                                                                    "VI", "VII", "VIII", "IX", "X"};
    const int m = clamp_mark(mark);
    return m == 0 ? std::string() : std::string("Mk ") + numerals[size_t(m)];
}

std::string item_display_name(const ItemDef& def, int mark) {
    const std::string name = def.display_name.empty() ? def.id : def.display_name;
    const std::string mk = mark_name(mark);
    return mk.empty() ? name : name + " " + mk;
}

double defense_mark_weight(std::string_view slot) {
    if (slot == "deflector_shield") return 0.30;
    if (slot == "chest") return 0.20;
    if (slot == "leggings") return 0.14;
    if (slot == "helmet") return 0.10;
    if (slot == "boots") return 0.06;
    if (slot == "gloves" || slot == "biceps" || slot == "bracers" || slot == "belt") return 0.05;
    return 0.0;
}

bool is_worn_slot(std::string_view slot) {
    return std::find(worn_slots.begin(), worn_slots.end(), slot) != worn_slots.end();
}

const ArmorBudget& armor_budget(std::string_view armor_class) {
    // docs/ARMOR.md, Balance. ASSUMPTION: a starting point, to tune in play.
    static const ArmorBudget none{};
    static const ArmorBudget light{0.12, 0.10, 60, 1.02, 1.0, 1.05};
    static const ArmorBudget medium{0.20, 0.12, 100, 0.96, 1.0, 1.0};
    static const ArmorBudget heavy{0.28, 0.15, 140, 0.92, 0.95, 1.0};
    if (armor_class == "light") return light;
    if (armor_class == "medium") return medium;
    if (armor_class == "heavy") return heavy;
    return none;
}

double armor_slot_share(std::string_view slot) {
    if (!is_worn_slot(slot)) return 0.0;
    double all = 0;
    for (const std::string_view s : worn_slots) all += defense_mark_weight(s);
    return defense_mark_weight(slot) / all;
}

std::string item_json_field(std::string_view json, std::string_view key) {
    const std::string quoted = "\"" + std::string(key) + "\"";
    const size_t at = json.find(quoted);
    if (at == std::string::npos) return {};
    const size_t colon = json.find(':', at + quoted.size());
    const size_t open = colon == std::string::npos ? std::string::npos : json.find('"', colon + 1);
    const size_t close = open == std::string::npos ? std::string::npos : json.find('"', open + 1);
    return close == std::string::npos ? std::string() : std::string(json.substr(open + 1, close - open - 1));
}

std::string with_item_json_field(std::string_view json, std::string_view key, std::string_view value) {
    // The fields kept, in order, without this one; then it, if set.
    std::vector<std::pair<std::string, std::string>> fields;
    for (size_t at = json.find('"'); at != std::string::npos;) {
        const size_t name_end = json.find('"', at + 1);
        if (name_end == std::string::npos) break;
        const std::string name(json.substr(at + 1, name_end - at - 1));
        const size_t open = json.find('"', json.find(':', name_end) + 1);
        const size_t close = open == std::string::npos ? std::string::npos : json.find('"', open + 1);
        if (close == std::string::npos) break;
        if (name != key) fields.emplace_back(name, std::string(json.substr(open + 1, close - open - 1)));
        at = json.find('"', close + 1);
    }
    if (!value.empty()) fields.emplace_back(std::string(key), std::string(value));
    std::string out = "{";
    for (const auto& [name, v] : fields) {
        if (out.size() > 1) out += ',';
        out += "\"" + name + "\":\"" + v + "\"";
    }
    return out + "}";
}

std::string item_paint(const ItemInstance& inst) { return item_json_field(inst.color_json, "paint"); }

std::vector<std::string> item_modules(const ItemInstance& inst) {
    std::vector<std::string> out;
    std::stringstream ids(item_json_field(inst.color_json, "mods"));
    for (std::string id; std::getline(ids, id, ',');) {
        if (!id.empty()) out.push_back(id);
    }
    return out;
}

bool module_fits(const ItemDef& module, const ItemDef& item) {
    if (module.kind != ItemKind::enhancement) return false;
    if (module.module_for == "armor") return item.kind == ItemKind::armor;
    if (module.module_for == "shield") return item.kind == ItemKind::deflector_shield;
    if (item.kind != ItemKind::weapon) return false;
    const bool melee = is_melee_archetype(item.archetype);
    return module.module_for == (melee ? "melee" : "ranged");
}

ItemDef ItemCatalog::tuned(const ItemDef& def, const ItemInstance& inst) const {
    ItemDef out = def;
    std::vector<const ItemDef*> mods;
    for (const std::string& id : item_modules(inst)) {
        if (const ItemDef* m = find(id); m && module_fits(*m, def)) mods.push_back(m);
    }
    if (mods.empty()) return out;
    if (def.kind == ItemKind::weapon) {
        for (const ItemDef* m : mods) {
            out.damage_mult *= m->damage_mult;
            out.rate_mult *= m->rate_mult;
            out.heat_mult *= m->heat_mult;
            out.spread_mult *= m->spread_mult;
            out.range_mult *= m->range_mult;
            out.clip_mult *= m->clip_mult;
            out.reload_mult *= m->reload_mult;
            out.velocity_mult *= m->velocity_mult;
            out.headshot_mult *= m->headshot_mult;
            out.vehicle_mult *= m->vehicle_mult;
            out.shield_mult *= m->shield_mult;
            out.swap_mult *= m->swap_mult;
            out.aim_speed_mult *= m->aim_speed_mult;
        }
    } else if (def.kind == ItemKind::armor) {
        // Each module moves a share of the piece's typed resistance to its
        // own spread: the class budget stays whole.
        std::map<DamageType, double> spread;
        const double kept = std::max(0.0, 1.0 - module_resist_share * double(mods.size()));
        for (const auto& [type, part] : def.resist) spread[type] += part * kept;
        for (const ItemDef* m : mods) {
            for (const auto& [type, part] : m->resist) spread[type] += part * module_resist_share;
        }
        out.resist.assign(spread.begin(), spread.end());
        apply_armor_budget(out);
    } else if (def.kind == ItemKind::deflector_shield) {
        for (const ItemDef* m : mods) {
            out.modifiers.insert(out.modifiers.end(), m->modifiers.begin(), m->modifiers.end());
        }
    }
    return out;
}

std::string painted_color_json(const ItemDef& def, std::string_view faction) {
    const std::string scheme = normalize_faction(faction);
    return def.paints.contains(scheme) ? "{\"paint\":\"" + scheme + "\"}" : "{}";
}

bool item_fits_slot(const ItemDef& def, std::string_view slot, bool appearance) {
    switch (def.kind) {
    case ItemKind::armor:
    case ItemKind::clothing: return is_worn_slot(slot) && def.slot == slot;
    case ItemKind::weapon:
    case ItemKind::deflector_shield:
    case ItemKind::ammo: return !appearance && def.slot == slot;
    case ItemKind::vehicle: return !appearance && slot == "mount";
    case ItemKind::bodysuit: return appearance && slot == full_body_slot;
    default: return false;
    }
}

bool is_melee_archetype(WeaponArchetype a) { return a == WeaponArchetype::melee || a == WeaponArchetype::melee_2h; }

bool species_can_use(const ItemDef& def, Species species) {
    return def.species.empty() || std::find(def.species.begin(), def.species.end(), species) != def.species.end();
}

bool is_generic_ammo(const ItemDef& def) {
    return def.kind == ItemKind::ammo && def.hit_effect.empty() && !def.damage_type && def.bolt_color_name.empty() &&
           def.damage_mult == 1.0 && def.rate_mult == 1.0 && def.heat_mult == 1.0 && def.spread_mult == 1.0 &&
           def.velocity_mult == 1.0 && def.headshot_mult == 1.0 && def.vehicle_mult == 1.0 &&
           def.shield_mult == 1.0 && def.crit_chance == 0.0;
}

double sidegrade_score(const ItemDef& d) {
    const auto pw = [](double v, double e) { return std::pow(std::max(v, 0.01), e); };
    const double crit_mult = d.crit_mult > 0 ? d.crit_mult : WeaponDef{}.crit_multiplier;
    double s = d.damage_mult * (1 + d.crit_chance * (crit_mult - 1)) * d.rate_mult;
    s *= pw(d.spread_mult, -0.35) * pw(d.range_mult, 0.35) * pw(d.heat_mult, -0.3);
    s *= pw(d.clip_mult, 0.15) * pw(d.reload_mult, -0.15) * pw(d.velocity_mult, 0.1);
    s *= pw(d.headshot_mult, 0.25) * pw(d.vehicle_mult, 0.1) * pw(d.shield_mult, 0.15);
    s *= pw(d.swap_mult, -0.05) * pw(d.aim_speed_mult, 0.05);
    if (d.recoil > 0) s /= 1 + 0.1 * d.recoil;
    if (!d.hit_effect.empty()) s *= 1.1;
    if (is_melee_archetype(d.archetype)) {  // its overrides, against the archetype's
        const WeaponDef base = default_archetype_weapon(d.archetype);
        const auto vs = [](double v, double b) { return v > 0 && b > 0 ? v / b : 1.0; };
        s *= pw(vs(d.melee_range, base.melee_range), 0.5) * pw(vs(d.melee_arc, base.melee_arc), 0.15);
        s *= pw(vs(d.melee_lunge, base.melee_lunge), 0.15) * pw(vs(d.melee_stagger, base.melee_stagger), 0.25);
        s *= pw(vs(d.shield_disrupt, base.shield_disrupt), 0.15);
    }
    for (const StatModifier& m : d.modifiers) {  // a carry penalty (or a boon)
        if (m.stat == Stat::run_speed || m.stat == Stat::sprint_factor) s *= pw(m.multiply, 1.5);
    }
    return s;
}

double sidegrade_budget(const ItemDef& def) { return def.species.empty() ? 1.0 : 1.0 + species_weapon_bonus; }

const HitEffect* ItemCatalog::hit_effect(std::string_view id) const {
    for (const HitEffect& e : hit_effects_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

const ItemDef* ItemCatalog::find(std::string_view id) const {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &items_[it->second];
}

WeaponDef ItemCatalog::resolve_weapon(const ItemDef& def, ItemGrade grade, int mark) const {
    WeaponDef w = default_archetype_weapon(def.archetype);
    if (const auto it = bf2_bases_.find(def.bf2_base); it != bf2_bases_.end()) {
        w = it->second;
    }
    w.name = def.id;
    w.model = def.model_path;
    w.anim_set = def.anim_set;
    w.archetype = weapon_archetype_info(def.archetype).id;

    // Its grade moves damage by -8% to +16% within its level; its mark scales
    // it along the level curve (x1 at Mk 0 to x2 at Mk X).
    const double g_mult = grade_stat_mult(def.grade.value_or(grade));
    const double power = def.damage_mult * g_mult * mark_scale(mark);

    w.damage *= power;
    if (w.damage_final > 0) {
        w.damage_final *= power;
    }
    w.explosion_damage *= power;
    w.shot_delay /= std::max(0.1, def.rate_mult);
    w.heat_per_shot *= def.heat_mult / (1.0 + (g_mult - 1.0) * 0.5);
    w.yaw_spread *= def.spread_mult;
    w.pitch_spread *= def.spread_mult;
    w.spread_per_shot *= def.spread_mult;
    if (w.projectile == Projectile::grenade) {
        // A thrown or placed explosive goes off by its fuse or trigger: its
        // range is its blast's.
        w.explosion_inner *= def.range_mult;
        w.explosion_outer *= def.range_mult;
        w.push_inner *= def.range_mult;
        w.push_outer *= def.range_mult;
    } else {
        w.bolt_life *= def.range_mult;
    }
    w.damage_transition_delay *= def.range_mult;
    if (w.rounds_per_clip > 0) {
        w.rounds_per_clip = std::max(1, int(std::lround(w.rounds_per_clip * def.clip_mult)));
    }
    w.reload_time *= def.reload_mult;
    if (def.melee_range > 0.0) w.melee_range = def.melee_range;
    if (def.melee_arc > 0.0) w.melee_arc = def.melee_arc;
    if (def.melee_lunge > 0.0) w.melee_lunge = def.melee_lunge;
    if (def.melee_stagger > 0.0) w.melee_stagger = def.melee_stagger;
    if (def.shield_disrupt > 0.0) w.shield_disrupt = def.shield_disrupt;
    if (def.trap_shots > 0 && w.trigger_radius > 0) {
        // A trap: it covers more ground than a mine (some 8 m), firing a
        // bolt every 0.3 s instead of exploding.
        w.trap_shots = def.trap_shots;
        w.trap_interval = 0.3;
        w.trigger_radius = 8.0 * def.range_mult;
    }
    if (w.projectile == Projectile::heal) {
        // A medpack's heal moves with its grade and mark as a weapon's damage does.
        w.heal = std::max(0.0, def.heal_amount) * g_mult * mark_scale(mark);
        w.heal_time = def.heal_time;
        if (def.carried > 0) w.ammo = def.carried;
    }
    if (w.projectile == Projectile::melee) {
        w.melee_range *= def.range_mult;
    }
    if (def.ammo_class != AmmoClass::none) {
        w.ammo_class = def.ammo_class;
    } else if (def.archetype == WeaponArchetype::heavy_launcher) {
        w.ammo_class = AmmoClass::heavy;
    } else if (w.projectile == Projectile::melee || w.projectile == Projectile::heal || w.projectile == Projectile::jump ||
               def.archetype == WeaponArchetype::grenade || def.archetype == WeaponArchetype::placed ||
               def.archetype == WeaponArchetype::utility) {
        w.ammo_class = AmmoClass::none;
    } else {
        w.ammo_class = AmmoClass::standard;
    }
    if (def.damage_type) {
        w.damage_type = *def.damage_type;
    }
    if (w.projectile == Projectile::grenade && !def.model_path.empty()) {
        w.ordnance_model = def.model_path;  // thrown or placed, it's drawn as its own model
    }
    apply_bolt_color(w, def.bolt_color_name);
    apply_item_extras(w, def, def.hit_effect.empty() ? nullptr : hit_effect(def.hit_effect));
    w.modifiers = def.modifiers;
    return w;
}

Gear ItemCatalog::resolve_gear(const ItemDef& def, ItemGrade grade, int mark) const {
    Gear g;
    g.name = def.id;
    g.slot = def.slot;
    const double g_mult = grade_stat_mult(def.grade.value_or(grade));
    for (StatModifier m : def.modifiers) {
        if (m.add > 0) {
            m.add *= g_mult;
            // A Deflector Shield's mark grows its capacity and recharge; worn
            // gear's marks go to health as a whole (build_loadout).
            if (def.kind == ItemKind::deflector_shield &&
                (m.stat == Stat::shield_capacity || m.stat == Stat::shield_recharge)) {
                m.add *= mark_scale(mark);
            }
        }
        g.modifiers.push_back(m);
    }
    return g;
}

Loadout ItemCatalog::build_loadout(const CharacterLoadout& cl, std::span<const ItemInstance> inventory,
                                   Species species) const {
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
            if (const ItemDef* def = find(inst->def_id); def && species_can_use(*def, species)) {
                out.weapons[s] = resolve_weapon(tuned(*def, *inst), inst->grade, inst->mark);
            }
        }
    }

    std::vector<std::string> armor_classes;
    double marked_weight = 0;  // the slots' weights times their pieces' marks
    for (const auto& [slot, item_id] : cl.gear_ids) {
        if (const ItemInstance* inst = find_inst(item_id)) {
            if (const ItemDef* def = find(inst->def_id); def && item_fits_slot(*def, slot, false)) {
                if (def->kind == ItemKind::armor || def->kind == ItemKind::deflector_shield) {
                    out.gear.push_back(resolve_gear(tuned(*def, *inst), inst->grade, inst->mark));
                    marked_weight += defense_mark_weight(slot) * clamp_mark(inst->mark);
                }
                // Clothing is worn too: it counts toward being unarmored.
                if (def->kind == ItemKind::armor) {
                    armor_classes.push_back(def->armor_class);
                } else if (def->kind == ItemKind::clothing) {
                    armor_classes.emplace_back("none");
                }
            }
        }
    }
    out.armor_class = loadout_armor_class(armor_classes);

    // Health along the level curve: the weighted mark of what's worn (the
    // weights sum to 1 over every slot).
    if (marked_weight > 0) {
        out.gear.push_back(Gear{"marks", "", {{Stat::max_health, 0, 1.0 + 0.1 * marked_weight}}});
    }

    if (!out.weapons[primary_slot] && !out.weapons[sidearm_slot]) {
        out.weapons[primary_slot] = WeaponDef{};
        out.weapons[primary_slot]->ammo_class = AmmoClass::standard;
    }

    // Ammo stacks (standard & heavy): an effect or sidegrade ammo is spent
    // from its stack (the pool); plain ammo, or none, isn't counted (-1).
    out.unified_standard_ammo = -1;
    out.unified_heavy_ammo = -1;
    const auto apply_ammo_slot = [&](std::string_view slot_key, AmmoClass expected_ac) {
        const auto git = cl.gear_ids.find(std::string(slot_key));
        if (git == cl.gear_ids.end() || git->second == 0) return;
        const ItemInstance* inst = find_inst(git->second);
        if (inst == nullptr) return;
        const ItemDef* adef = find(inst->def_id);
        if (adef == nullptr || adef->kind != ItemKind::ammo || adef->ammo_class != expected_ac ||
            is_generic_ammo(*adef)) {
            return;
        }
        out.set_unified_ammo(expected_ac,
                             std::clamp(inst->quantity, 0, int(max_ammo_for(expected_ac) * max_ammo_capacity)));
        for (auto& opt_w : out.weapons) {
            if (!opt_w || opt_w->ammo_class != expected_ac) continue;
            opt_w->damage *= adef->damage_mult;
            opt_w->damage_final *= adef->damage_mult;
            opt_w->explosion_damage *= adef->damage_mult;
            opt_w->shot_delay /= std::max(0.1, adef->rate_mult);
            opt_w->heat_per_shot *= adef->heat_mult;
            opt_w->yaw_spread *= adef->spread_mult;
            opt_w->pitch_spread *= adef->spread_mult;
            opt_w->spread_per_shot *= adef->spread_mult;
            if (adef->damage_type) opt_w->damage_type = *adef->damage_type;
            apply_bolt_color(*opt_w, adef->bolt_color_name);
            // Burning, cryo, ion: the ammo's effect is every gun's firing it.
            apply_item_extras(*opt_w, *adef, adef->hit_effect.empty() ? nullptr : hit_effect(adef->hit_effect));
        }
    };
    apply_ammo_slot("ammo_standard", AmmoClass::standard);
    apply_ammo_slot("ammo_heavy", AmmoClass::heavy);
    return out;
}

namespace {

BattleReward battle_reward(BattleReward r, uint16_t kills, uint16_t captures) {
    r.xp += int64_t(kills) * 25 + int64_t(captures) * 60;
    r.faction_points += int64_t(kills) * 10 + int64_t(captures) * 25;
    r.credits += int64_t(kills) * 15 + int64_t(captures) * 40;
    // Gear-Matters rules always award +15% faction points & credits.
    r.faction_points = int64_t(std::llround(double(r.faction_points) * 1.15));
    r.credits = int64_t(std::llround(double(r.credits) * 1.15));
    return r;
}

} // namespace

BattleReward compute_battle_reward(bool won, bool draw, uint16_t kills, uint16_t captures) {
    if (won) {
        return battle_reward({250, 100, 200}, kills, captures);
    }
    if (draw) {
    if (w.projectile == Projectile::jump) {
        if (def.jump > 0) w.jump = def.jump;
        if (def.carried > 0) w.ammo = def.carried;
    }
        return battle_reward({150, 60, 120}, kills, captures);
    }
    return battle_reward({100, 40, 80}, kills, captures);
}

BattleReward compute_left_battle_reward(uint16_t kills, uint16_t captures) {
    return battle_reward({}, kills, captures);
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

} // namespace

std::string resolve_worn_models(const ItemCatalog& catalog, const CharacterLoadout& cl,
                                std::span<const ItemInstance> inventory, Species species, Gender gender) {
    // A layer's worn things by slot: only what fits there (a mount's model is
    // the vehicle it summons; it isn't worn).
    struct Worn {
        const ItemDef* def;
        const ItemInstance* inst;
    };
    const auto layer = [&](const std::map<std::string, uint64_t>& ids, bool appearance) {
        std::map<std::string, Worn> by_slot;
        for (const auto& [slot, item_id] : ids) {
            if (item_id == 0 || (!is_worn_slot(slot) && slot != full_body_slot)) continue;
            for (const ItemInstance& inst : inventory) {
                if (inst.id != item_id) continue;
                if (const ItemDef* def = catalog.find(inst.def_id); def && item_fits_slot(*def, slot, appearance)) {
                    by_slot[slot] = {def, &inst};
                }
                break;
            }
        }
        return by_slot;
    };
    const auto gear = layer(cl.gear_ids, false);
    const auto look = layer(cl.appearance_ids, true);

    // A bodysuit shows in place of everything.
    if (const auto it = look.find(std::string(full_body_slot));
        it != look.end() && !it->second.def->model_path.empty()) {
        return "override:" + substitute_gender(it->second.def->model_path, gender);
    }

    std::map<std::string, Worn> shown = gear;
    for (const auto& [slot, worn] : look) shown[slot] = worn;
    std::set<std::string> covered;  // under a robe or a dress
    for (const auto& [slot, worn] : shown) {
        if (species_shows_slot(species, slot)) covered.insert(worn.def->covers.begin(), worn.def->covers.end());
    }
    std::string out;
    for (const auto& [slot, worn] : shown) {
        if (covered.contains(slot)) continue;
        const ItemDef* def = worn.def;
        std::string path = def->model_path;
        // Its paint: its own scheme, else its neutral one, on each model.
        std::string paint;
        if (const auto own = def->paints.find(item_paint(*worn.inst)); own != def->paints.end()) {
            paint = own->second;
        } else if (const auto neutral = def->paints.find("neutral"); neutral != def->paints.end()) {
            paint = neutral->second;
        }
        if (const std::string dye = item_json_field(worn.inst->color_json, "dye"); !dye.empty()) {
            paint += (paint.empty() ? "" : "&") + dye;  // (its values after the paint's: they win)
        }
        // A body that doesn't show the piece: its species' own of its class.
        if (!species_shows_slot(species, slot)) {
            const SpeciesArmorLook* own =
                def->kind == ItemKind::armor ? catalog.species_look(species, def->armor_class, slot) : nullptr;
            path = own ? own->model_path : "";
            paint.clear();
        }
        if (path.empty()) continue;
        std::stringstream models(substitute_gender(path, gender));
        for (std::string model; std::getline(models, model, ',');) {
            if (!out.empty()) out += ',';
            out += paint.empty() ? model : model + "?" + paint;
        }
    }
    return out;
}

const SpeciesArmorLook* ItemCatalog::species_look(Species species, std::string_view armor_class,
                                                  std::string_view slot) const {
    for (const SpeciesArmorLook& look : species_looks_) {
        if (look.species == species && look.armor_class == armor_class && look.slot == slot) return &look;
    }
    return nullptr;
}

std::string plain_clothes(const ItemCatalog& catalog, Gender gender) {
    std::string out;
    for (const char* id : {"cloth_shirt_tunic", "cloth_pants_cargo", "cloth_boots_leather"}) {
        const ItemDef* def = catalog.find(id);
        if (def == nullptr || def->model_path.empty()) continue;
        if (!out.empty()) out += ',';
        out += substitute_gender(def->model_path, gender);
    }
    return out;
}

} // namespace swm::core
