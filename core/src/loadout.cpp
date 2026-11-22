#include "swm/core/loadout.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "swm/core/ini.h"

namespace swm::core {

namespace {

constexpr std::array<std::pair<std::string_view, Stat>, 18> stat_names{{
    {"max_health", Stat::max_health},
    {"run_speed", Stat::run_speed},
    {"strafe_speed", Stat::strafe_speed},
    {"sprint_factor", Stat::sprint_factor},
    {"sprint_strafe", Stat::sprint_strafe},
    {"jump_height", Stat::jump_height},
    {"acceleration", Stat::acceleration},
    {"max_energy", Stat::max_energy},
    {"energy_restore", Stat::energy_restore},
    {"energy_restore_idle", Stat::energy_restore_idle},
    {"energy_drain_sprint", Stat::energy_drain_sprint},
    {"mitigation", Stat::mitigation},
    {"mitigation_ballistic", Stat::mitigation_ballistic},
    {"mitigation_laser", Stat::mitigation_laser},
    {"mitigation_electro", Stat::mitigation_electro},
    {"mitigation_bio", Stat::mitigation_bio},
    {"mitigation_explosive", Stat::mitigation_explosive},
    {"mitigation_melee", Stat::mitigation_melee},
}};

constexpr std::array<std::string_view, damage_type_count> damage_type_names{"ballistic", "laser",     "electro",
                                                                            "bio",       "explosive", "melee"};

double& stat_value(CharacterStats& stats, Stat stat, double& jump_height) {
    MoveParams& m = stats.move;
    switch (stat) {
    case Stat::max_health: return stats.max_health;
    case Stat::run_speed: return m.run_speed;
    case Stat::strafe_speed: return m.strafe_speed;
    case Stat::sprint_factor: return m.sprint_factor;
    case Stat::sprint_strafe: return m.sprint_strafe;
    case Stat::jump_height: return jump_height;
    case Stat::acceleration: return m.ground_accel;
    case Stat::max_energy: return m.max_energy;
    case Stat::energy_restore: return m.energy_restore;
    case Stat::energy_restore_idle: return m.energy_restore_idle;
    case Stat::energy_drain_sprint: return m.energy_drain_sprint;
    case Stat::mitigation: return stats.mitigation;
    default: return stats.mitigation_by_type[size_t(stat) - size_t(Stat::mitigation_ballistic)];
    }
}

// "run_speed * 0.9", "mitigation_laser + 0.15", "max_health - 50"
StatModifier parse_modifier(const std::string& text, const std::string& where) {
    std::istringstream fields(text);
    std::string name, op;
    double value = 0;
    if (!(fields >> name >> op >> value)) {
        throw std::runtime_error(where + ": Modifier is \"<stat> + <n>\" or \"<stat> * <n>\", not \"" + text + "\"");
    }
    const auto stat = stat_from_name(name);
    if (!stat) {
        throw std::runtime_error(where + ": unknown statistic " + name);
    }
    StatModifier modifier{*stat};
    if (op == "+") {
        modifier.add = value;
    } else if (op == "-") {
        modifier.add = -value;
    } else if (op == "*") {
        modifier.multiply = value;
    } else {
        throw std::runtime_error(where + ": Modifier operator is +, - or *, not " + op);
    }
    return modifier;
}

std::vector<StatModifier> read_modifiers(const Ini::Section& section, const std::string& where) {
    std::vector<StatModifier> modifiers;
    for (const auto& [key, value] : section.values) {
        if (key == "Modifier") {
            modifiers.push_back(parse_modifier(value, where));
        }
    }
    return modifiers;
}

} // namespace

std::optional<Stat> stat_from_name(std::string_view name) {
    for (const auto& [text, stat] : stat_names) {
        if (text == name) {
            return stat;
        }
    }
    return std::nullopt;
}

std::optional<DamageType> damage_type_from_name(std::string_view name) {
    for (size_t i = 0; i < damage_type_names.size(); ++i) {
        if (damage_type_names[i] == name) {
            return DamageType(i);
        }
    }
    return std::nullopt;
}

double mitigated_damage(const CharacterStats& stats, double amount, DamageType type) {
    const double general = std::clamp(stats.mitigation, 0.0, max_mitigation);
    const double specific = std::clamp(stats.mitigation_by_type[size_t(type)], 0.0, max_mitigation);
    return amount * (1 - general) * (1 - specific);
}

const WeaponDef* weapon_in_slot(const Loadout& loadout, int slot) {
    if (slot < 0 || slot >= weapon_slots || !loadout.weapons[size_t(slot)]) {
        return nullptr;
    }
    return &*loadout.weapons[size_t(slot)];
}

CharacterStats stats_for(const Loadout& loadout, const CharacterStats& base) {
    std::vector<const StatModifier*> modifiers;
    for (const auto& weapon : loadout.weapons) {
        if (weapon) {
            for (const StatModifier& m : weapon->modifiers) {
                modifiers.push_back(&m);
            }
        }
    }
    for (const Gear& gear : loadout.gear) {
        for (const StatModifier& m : gear.modifiers) {
            modifiers.push_back(&m);
        }
    }
    CharacterStats stats = base;
    MoveParams& move = stats.move;
    double jump_height = move.jump_speed * move.jump_speed / (2 * move.gravity);
    for (const auto& [name, stat] : stat_names) {
        double add = 0, multiply = 1;
        for (const StatModifier* m : modifiers) {
            if (m->stat == stat) {
                add += m->add;
                multiply *= m->multiply;
            }
        }
        double& value = stat_value(stats, stat, jump_height);
        value = std::max(0.0, (value + add) * multiply);
    }
    move.jump_speed = std::sqrt(2 * move.gravity * jump_height);
    stats.max_health = std::max(1.0, stats.max_health);
    return stats;
}

WeaponDef load_weapon(const std::filesystem::path& path) {
    const Ini ini = Ini::load(path);
    const std::string where = path.string();
    const Ini::Section* section = ini.section("weapon");
    if (section == nullptr) {
        throw std::runtime_error(where + ": no [weapon] section");
    }
    const Ini::Section& s = *section;
    WeaponDef w;
    w.name = s.get("Name").value_or(path.stem().string());
    const std::string projectile = s.get("Projectile").value_or("bolt");
    w.projectile = projectile == "missile" ? Projectile::missile
                   : projectile == "grenade" ? Projectile::grenade
                                             : Projectile::bolt;
    // Explosives hit explosive; blasters are lasers, unless the file says.
    const auto damage_type = s.get("DamageType");
    if (damage_type) {
        const auto type = damage_type_from_name(*damage_type);
        if (!type) {
            throw std::runtime_error(where + ": unknown DamageType " + *damage_type);
        }
        w.damage_type = *type;
    } else {
        w.damage_type = w.projectile == Projectile::bolt ? DamageType::laser : DamageType::explosive;
    }
    w.ammo = std::max(0, int(s.number("WeaponAmmo", 0)));
    w.shot_delay = s.number("ShotDelay", w.shot_delay);
    w.rounds_per_clip = int(s.number("RoundsPerClip", w.rounds_per_clip));
    w.reload_time = s.number("ReloadTime", w.reload_time);
    w.heat_per_shot = s.number("HeatPerShot", w.heat_per_shot);
    w.heat_recover_rate = s.number("HeatRecoverRate", w.heat_recover_rate);
    w.heat_threshold = s.number("HeatThreshold", w.heat_threshold);
    // A weapon without spread values (grenades) has none.
    w.yaw_spread = s.number("YawSpread", 0);
    w.pitch_spread = s.number("PitchSpread", 0);
    w.spread_per_shot = s.number("SpreadPerShot", 0);
    w.spread_recover_rate = s.number("SpreadRecoverRate", w.spread_recover_rate);
    w.spread_limit = s.number("SpreadLimit", 0);
    w.shots_per_salvo = std::max(1, int(s.number("ShotsPerSalvo", w.shots_per_salvo)));
    w.damage = s.number("MaxDamage", w.damage) * s.number("PersonScale", 1.0);
    w.bolt_speed = s.number("Velocity", w.bolt_speed);
    w.bolt_gravity = s.number("Gravity", w.bolt_gravity);
    w.bolt_life = s.number("LifeSpan", w.bolt_life);
    w.bolt_acceleration = s.number("Acceleration", 0);
    w.rebound = s.number("Rebound", 0);
    w.explode_on_impact = s.number("ExplosionImpact", w.projectile == Projectile::grenade ? 0 : 1) != 0;
    w.fuse = s.number("Fuse", w.bolt_life);
    w.shot_elevate = s.number("ShotElevate", 0);
    w.explosion_damage = s.number("ExplosionDamage", 0) * s.number("ExplosionPersonScale", 1.0);
    w.explosion_inner = s.number("ExplosionRadiusInner", 0);
    w.explosion_outer = std::max(w.explosion_inner, s.number("ExplosionRadiusOuter", 0));
    const auto read_color = [&](const char* key, std::array<double, 4>& out) {
        if (const auto color = s.get(key)) {
            std::istringstream fields(*color);
            for (double& channel : out) {
                double value = channel * 255;
                if (fields >> value) {
                    channel = value / 255;
                }
            }
        }
    };
    read_color("LaserGlowColor", w.bolt_color);
    w.bolt_length = s.number("LaserLength", w.bolt_length);
    w.bolt_width = s.number("LaserWidth", w.bolt_width);
    if (const auto tex = s.get("LaserTexture")) {
        w.bolt_texture = *tex;
    } else if (w.projectile != Projectile::bolt) {
        w.bolt_texture.clear();
    }
    w.glow_length = s.number("GlowLength", w.bolt_length * 3.0);
    w.blur_length = s.number("BlurLength", w.glow_length);
    read_color("LightColor", w.light_color);
    w.light_radius = s.number("LightRadius", w.light_radius);
    w.ordnance_model = s.get("GeometryName").value_or("");
    w.trail_effect = s.get("TrailEffect").value_or("");
    w.impact_effect = s.get("ImpactEffect").value_or(w.impact_effect);
    w.explosion_effect = s.get("ExplosionEffect").value_or("");
    w.muzzle_flash = s.get("MuzzleFlash").value_or(w.projectile == Projectile::grenade ? "" : w.muzzle_flash);
    read_color("FlashColor", w.flash_color);
    w.flash_length = s.number("FlashLength", w.flash_length);
    read_color("FlashLightColor", w.flash_light_color);
    w.flash_light_radius = s.number("FlashLightRadius", w.flash_light_radius);
    w.flash_light_duration = s.number("FlashLightDuration", w.flash_light_duration);
    w.modifiers = read_modifiers(s, where);
    return w;
}

Gear load_gear(const std::filesystem::path& path) {
    const Ini ini = Ini::load(path);
    const std::string where = path.string();
    const Ini::Section* s = ini.section("gear");
    if (s == nullptr) {
        throw std::runtime_error(where + ": no [gear] section");
    }
    Gear gear;
    gear.name = s->get("Name").value_or(path.stem().string());
    gear.slot = s->get("Slot").value_or("");
    gear.modifiers = read_modifiers(*s, where);
    return gear;
}

Loadout load_loadout(const std::filesystem::path& path) {
    const Ini ini = Ini::load(path);
    const std::string where = path.string();
    const Ini::Section* s = ini.section("loadout");
    if (s == nullptr) {
        throw std::runtime_error(where + ": no [loadout] section");
    }
    std::vector<std::filesystem::path> folders;
    std::istringstream items(s->get("Items").value_or("../items ../imported_assets/items"));
    for (std::string folder; items >> folder;) {
        folders.push_back(path.parent_path() / folder);
    }
    const auto find = [&](const std::string& name) {
        if (name.find_first_of("/\\") != std::string::npos) {
            throw std::runtime_error(where + ": item names are file names without .ini, not paths: " + name);
        }
        for (const auto& folder : folders) {
            if (const auto file = folder / (name + ".ini"); std::filesystem::exists(file)) {
                return file;
            }
        }
        throw std::runtime_error(where + ": no item file " + name +
                                 ".ini in its Items folders (Battlefront weapons come from bf2tool export-loadout)");
    };

    Loadout loadout;
    loadout.name = s->get("Name").value_or(path.stem().string());
    loadout.weapons = {};
    const std::array<const char*, weapon_slots> keys{"Primary", "Sidearm", "Secondary"};
    for (int slot = 0; slot < weapon_slots; ++slot) {
        if (const auto name = s->get(keys[size_t(slot)]); name && !name->empty()) {
            loadout.weapons[size_t(slot)] = load_weapon(find(*name));
        }
    }
    for (const auto& [key, value] : s->values) {
        if (key == "Gear") {
            Gear gear = load_gear(find(value));
            const bool taken = !gear.slot.empty() && std::any_of(loadout.gear.begin(), loadout.gear.end(),
                                                                 [&](const Gear& g) { return g.slot == gear.slot; });
            if (taken) {
                throw std::runtime_error(where + ": two pieces of gear for the " + gear.slot + " slot");
            }
            loadout.gear.push_back(std::move(gear));
        }
    }
    if (!loadout.weapons[primary_slot] && !loadout.weapons[sidearm_slot]) {
        throw std::runtime_error(where + ": needs a Primary or a Sidearm");
    }
    return loadout;
}

} // namespace swm::core
