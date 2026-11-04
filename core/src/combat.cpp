#include "swm/core/combat.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <string_view>

#include "swm/core/ini.h"
#include "swm/core/world.h"

namespace swm::core {

namespace {

constexpr double degrees = std::numbers::pi / 180;

// Two numbers in -1..1 from a 64-bit seed (splitmix64).
std::pair<double, double> random_pair(uint64_t seed) {
    const auto next = [&seed] {
        uint64_t z = (seed += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return double(z >> 11) / double(1ull << 53) * 2 - 1;
    };
    const double a = next();
    return {a, next()};
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

} // namespace

SoldierDef load_soldier(const std::filesystem::path& path) {
    const Ini ini = Ini::load(path);
    SoldierDef soldier;
    soldier.weapons.clear();
    if (const Ini::Section* s = ini.section("soldier")) {
        soldier.name = s->get("Name").value_or(soldier.name);
        soldier.max_health = s->number("MaxHealth", soldier.max_health);
        MoveParams& m = soldier.move;
        m.run_speed = s->number("MaxSpeed", m.run_speed);
        m.strafe_speed = s->number("MaxStrafeSpeed", m.strafe_speed);
        m.ground_accel = s->number("Acceleration", m.ground_accel);
        const double jump_height = s->number("JumpHeight", m.jump_speed * m.jump_speed / (2 * m.gravity));
        m.jump_speed = std::sqrt(2 * m.gravity * jump_height);
        m.energy_restore = s->number("EnergyRestore", m.energy_restore);
        m.energy_restore_idle = s->number("EnergyRestoreIdle", m.energy_restore_idle);
        m.energy_drain_sprint = s->number("EnergyDrainSprint", m.energy_drain_sprint);
        m.energy_min_sprint = s->number("EnergyMinSprint", m.energy_min_sprint);
        // ControlSpeed = <posture> <forward> <strafe> <turn>
        for (const auto& [key, value] : s->values) {
            if (key == "ControlSpeed" && starts_with(value, "sprint")) {
                std::istringstream fields(value.substr(6));
                fields >> m.sprint_factor >> m.sprint_strafe;
            }
        }
    }
    for (const Ini::Section& s : ini.sections()) {
        const bool primary = s.name == "weapon" && int(soldier.weapons.size()) < max_weapons;
        const bool secondary = s.name == "secondary" && !soldier.secondary;
        if (!primary && !secondary) {
            continue;
        }
        WeaponDef w;
        w.name = s.get("Name").value_or(w.name);
        const std::string projectile = s.get("Projectile").value_or("bolt");
        w.projectile = projectile == "missile" ? Projectile::missile
                       : projectile == "grenade" ? Projectile::grenade
                                                 : Projectile::bolt;
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
        if (const auto color = s.get("LaserGlowColor")) {
            std::istringstream fields(*color);
            for (double& channel : w.bolt_color) {
                double value = channel * 255;
                fields >> value;
                channel = value / 255;
            }
        }
        w.bolt_length = s.number("LaserLength", w.bolt_length);
        w.bolt_width = s.number("LaserWidth", w.bolt_width);
        if (primary) {
            soldier.weapons.push_back(w);
        } else {
            soldier.secondary = w;
        }
    }
    if (soldier.weapons.empty()) {
        soldier.weapons.push_back(WeaponDef{});
    }
    return soldier;
}

Vec3 aim_direction(double yaw, double pitch) {
    const double cp = std::cos(pitch);
    return {-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp};
}

std::optional<double> ballistic_pitch(double speed, double gravity, double dx, double dy) {
    if (gravity <= 0 || dx <= 0) {
        return std::atan2(dy, std::max(dx, 1e-9));
    }
    const double v2 = speed * speed;
    const double root = v2 * v2 - gravity * (gravity * dx * dx + 2 * dy * v2);
    if (root < 0) {
        return std::nullopt;
    }
    return std::atan((v2 - std::sqrt(root)) / (gravity * dx));
}

const WeaponDef* weapon_in_slot(const SoldierDef& soldier, int slot) {
    if (slot == secondary_slot) {
        return soldier.secondary ? &*soldier.secondary : nullptr;
    }
    return slot >= 0 && slot < int(soldier.weapons.size()) ? &soldier.weapons[size_t(slot)] : nullptr;
}

void reset_loadout(MoveState& state, const SoldierDef& soldier) {
    state.combat = {};
    for (int slot = 0; slot < int(state.combat.weapons.size()); ++slot) {
        if (const WeaponDef* def = weapon_in_slot(soldier, slot)) {
            WeaponState& w = state.combat.weapons[size_t(slot)];
            w.rounds = int16_t(def->rounds_per_clip);
            w.reserve = def->ammo > 0 ? int16_t((def->ammo - 1) * def->rounds_per_clip) : int16_t(-1);
        }
    }
    state.energy = soldier.move.max_energy;
    state.sprinting = false;
}

namespace {

// Refills a clip from what's carried.
void refill(WeaponState& w, const WeaponDef& def) {
    const int wanted = def.rounds_per_clip - w.rounds;
    const int taken = w.reserve < 0 ? wanted : std::min(wanted, int(w.reserve));
    w.rounds = int16_t(w.rounds + taken);
    if (w.reserve >= 0) {
        w.reserve = int16_t(w.reserve - taken);
    }
}

} // namespace

std::optional<Shot> simulate_weapons(MoveState& state, const MoveInput& input, const SoldierDef& soldier, double dt) {
    CombatState& c = state.combat;
    const int count = std::min<int>(int(soldier.weapons.size()), max_weapons);
    if (count == 0) {
        return std::nullopt;
    }
    c.cooldown = std::max(0.0, c.cooldown - dt);
    for (int i = 0; i < int(c.weapons.size()); ++i) {
        const WeaponDef* def = weapon_in_slot(soldier, i);
        WeaponState& w = c.weapons[size_t(i)];
        if (def == nullptr) {
            continue;
        }
        w.heat = float(std::max(0.0, w.heat - def->heat_recover_rate * dt));
        if (w.overheated && w.heat <= def->heat_threshold) {
            w.overheated = false;
        }
    }

    // Switching weapons cancels a reload and takes a moment.
    const uint8_t wanted = uint8_t(std::min<int>(input.weapon, count - 1));
    if (wanted != c.weapon) {
        c.weapon = wanted;
        c.reload = 0;
        c.cooldown = std::max(c.cooldown, weapon_switch_time);
    }
    const WeaponDef& def = soldier.weapons[c.weapon];
    WeaponState& weapon = c.weapons[c.weapon];
    c.spread = std::max(0.0, c.spread - def.spread_recover_rate * dt);

    if (c.reload > 0) {
        c.reload -= dt;
        if (c.reload <= 0) {
            c.reload = 0;
            refill(weapon, def);
        }
    }
    const bool uses_clip = def.rounds_per_clip > 0;
    const bool can_reload = uses_clip && weapon.rounds < def.rounds_per_clip && weapon.reserve != 0;
    if (can_reload && c.reload == 0 && input.held(MoveInput::reload)) {
        c.reload = def.reload_time;
    }

    // The secondary (a grenade) is thrown instead of firing, when ready.
    if (soldier.secondary && input.held(MoveInput::secondary) && c.cooldown <= 0 && c.reload <= 0) {
        const WeaponDef& thrown = *soldier.secondary;
        WeaponState& grenades = c.weapons[secondary_slot];
        if (grenades.rounds > 0) {
            c.cooldown += thrown.shot_delay;
            grenades.rounds = int16_t(grenades.rounds - 1);
            if (grenades.rounds == 0) {
                refill(grenades, thrown);  // the next grenade comes out of the pouch
            }
            const double pitch = std::min(1.5, double(input.pitch) + thrown.shot_elevate);
            return Shot{state.position + Vec3{0, eye_height, 0}, {aim_direction(input.yaw, pitch)}, secondary_slot};
        }
    }

    if (!input.held(MoveInput::fire) || c.cooldown > 0 || c.reload > 0) {
        return std::nullopt;
    }
    if (uses_clip && weapon.rounds <= 0) {
        if (can_reload) {
            c.reload = def.reload_time;  // pulling the trigger on an empty clip reloads
        }
        return std::nullopt;
    }
    if (weapon.overheated) {
        return std::nullopt;
    }

    // Fire: every pellet uses a round (ASSUMPTION: a 40-round shotgun clip
    // holding 8-pellet shots fires 5 times).
    c.cooldown += def.shot_delay;
    const int pellets = uses_clip ? std::min<int>(def.shots_per_salvo, weapon.rounds) : def.shots_per_salvo;
    if (uses_clip) {
        weapon.rounds = int16_t(weapon.rounds - pellets);
    }
    if (def.heat_per_shot > 0) {
        weapon.heat = float(std::min(1.0, weapon.heat + def.heat_per_shot));
        weapon.overheated = weapon.heat >= 1;
    }
    Shot shot{state.position + Vec3{0, eye_height, 0}, {}, c.weapon};
    for (int i = 0; i < pellets; ++i) {
        const auto [u, v] = random_pair(input.sequence ^ (uint64_t(i) << 32));
        const double yaw = input.yaw + u * (def.yaw_spread + c.spread) * degrees;
        const double pitch = input.pitch + def.shot_elevate + v * (def.pitch_spread + c.spread) * degrees;
        shot.directions.push_back(aim_direction(yaw, pitch));
    }
    c.spread = std::min(def.spread_limit, c.spread + def.spread_per_shot);
    return shot;
}

std::optional<Shot> simulate_character(MoveState& state, const MoveInput& input, bool alive, const World& world,
                                       const SoldierDef& soldier, double dt) {
    if (!alive) {
        MoveInput idle;
        idle.sequence = input.sequence;
        idle.yaw = state.yaw;
        state = simulate_move(state, idle, world, soldier.move, dt);
        return std::nullopt;
    }
    state = simulate_move(state, input, world, soldier.move, dt);
    return simulate_weapons(state, input, soldier, dt);
}

double explosion_damage(const WeaponDef& weapon, Vec3 center, Vec3 feet) {
    if (weapon.explosion_damage <= 0 || weapon.explosion_outer <= 0) {
        return 0;
    }
    // Distance to the body: its axis, less its radius.
    const double y = std::clamp(center.y, feet.y, feet.y + hit_height);
    const double distance =
        std::max(0.0, std::sqrt((center.x - feet.x) * (center.x - feet.x) + (center.y - y) * (center.y - y) +
                                (center.z - feet.z) * (center.z - feet.z)) -
                          hit_radius);
    if (distance <= weapon.explosion_inner) {
        return weapon.explosion_damage;
    }
    if (distance >= weapon.explosion_outer) {
        return 0;
    }
    return weapon.explosion_damage * (weapon.explosion_outer - distance) /
           (weapon.explosion_outer - weapon.explosion_inner);
}

std::optional<double> segment_hits_cylinder(Vec3 a, Vec3 b, Vec3 feet, double radius, double height) {
    // The part of the segment inside the infinite cylinder, in XZ...
    const double dx = b.x - a.x, dz = b.z - a.z;
    const double fx = a.x - feet.x, fz = a.z - feet.z;
    double t0 = 0, t1 = 1;
    const double qa = dx * dx + dz * dz;
    const double qc = fx * fx + fz * fz - radius * radius;
    if (qa < 1e-12) {
        if (qc > 0) {
            return std::nullopt;  // straight up or down, outside
        }
    } else {
        const double qb = 2 * (fx * dx + fz * dz);
        const double disc = qb * qb - 4 * qa * qc;
        if (disc < 0) {
            return std::nullopt;
        }
        const double root = std::sqrt(disc);
        t0 = std::max(t0, (-qb - root) / (2 * qa));
        t1 = std::min(t1, (-qb + root) / (2 * qa));
    }
    // ...and between its bottom and top.
    const double dy = b.y - a.y;
    if (std::abs(dy) < 1e-12) {
        if (a.y < feet.y || a.y > feet.y + height) {
            return std::nullopt;
        }
    } else {
        double y0 = (feet.y - a.y) / dy;
        double y1 = (feet.y + height - a.y) / dy;
        if (y0 > y1) {
            std::swap(y0, y1);
        }
        t0 = std::max(t0, y0);
        t1 = std::min(t1, y1);
    }
    if (t0 > t1) {
        return std::nullopt;
    }
    return t0;
}

} // namespace swm::core
