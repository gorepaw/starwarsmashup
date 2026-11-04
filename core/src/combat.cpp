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
        if (s.name != "weapon" || int(soldier.weapons.size()) >= max_weapons) {
            continue;
        }
        WeaponDef w;
        w.name = s.get("Name").value_or(w.name);
        w.shot_delay = s.number("ShotDelay", w.shot_delay);
        w.rounds_per_clip = int(s.number("RoundsPerClip", w.rounds_per_clip));
        w.reload_time = s.number("ReloadTime", w.reload_time);
        w.heat_per_shot = s.number("HeatPerShot", w.heat_per_shot);
        w.heat_recover_rate = s.number("HeatRecoverRate", w.heat_recover_rate);
        w.heat_threshold = s.number("HeatThreshold", w.heat_threshold);
        w.yaw_spread = s.number("YawSpread", w.yaw_spread);
        w.pitch_spread = s.number("PitchSpread", w.pitch_spread);
        w.spread_per_shot = s.number("SpreadPerShot", w.spread_per_shot);
        w.spread_recover_rate = s.number("SpreadRecoverRate", w.spread_recover_rate);
        w.spread_limit = s.number("SpreadLimit", w.spread_limit);
        w.damage = s.number("MaxDamage", w.damage) * s.number("PersonScale", 1.0);
        w.bolt_speed = s.number("Velocity", w.bolt_speed);
        w.bolt_gravity = s.number("Gravity", w.bolt_gravity);
        w.bolt_life = s.number("LifeSpan", w.bolt_life);
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
        soldier.weapons.push_back(w);
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

void reset_loadout(MoveState& state, const SoldierDef& soldier) {
    state.combat = {};
    for (size_t i = 0; i < soldier.weapons.size() && i < state.combat.weapons.size(); ++i) {
        state.combat.weapons[i].rounds = int16_t(soldier.weapons[i].rounds_per_clip);
    }
    state.energy = soldier.move.max_energy;
    state.sprinting = false;
}

std::optional<Shot> simulate_weapons(MoveState& state, const MoveInput& input, const SoldierDef& soldier, double dt) {
    CombatState& c = state.combat;
    const int count = std::min<int>(int(soldier.weapons.size()), max_weapons);
    if (count == 0) {
        return std::nullopt;
    }
    c.cooldown = std::max(0.0, c.cooldown - dt);
    for (int i = 0; i < count; ++i) {
        const WeaponDef& def = soldier.weapons[size_t(i)];
        WeaponState& w = c.weapons[size_t(i)];
        w.heat = float(std::max(0.0, w.heat - def.heat_recover_rate * dt));
        if (w.overheated && w.heat <= def.heat_threshold) {
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
            weapon.rounds = int16_t(def.rounds_per_clip);
        }
    }
    const bool uses_clip = def.rounds_per_clip > 0;
    const bool clip_full = weapon.rounds >= def.rounds_per_clip;
    if (uses_clip && c.reload == 0 && input.held(MoveInput::reload) && !clip_full) {
        c.reload = def.reload_time;
    }
    if (!input.held(MoveInput::fire) || c.cooldown > 0 || c.reload > 0) {
        return std::nullopt;
    }
    if (uses_clip && weapon.rounds <= 0) {
        c.reload = def.reload_time;  // pulling the trigger on an empty clip reloads
        return std::nullopt;
    }
    if (weapon.overheated) {
        return std::nullopt;
    }

    // Fire.
    c.cooldown += def.shot_delay;
    if (uses_clip) {
        --weapon.rounds;
    }
    if (def.heat_per_shot > 0) {
        weapon.heat = float(std::min(1.0, weapon.heat + def.heat_per_shot));
        weapon.overheated = weapon.heat >= 1;
    }
    const auto [u, v] = random_pair(input.sequence);
    const double yaw = input.yaw + u * (def.yaw_spread + c.spread) * degrees;
    const double pitch = input.pitch + v * (def.pitch_spread + c.spread) * degrees;
    c.spread = std::min(def.spread_limit, c.spread + def.spread_per_shot);
    return Shot{state.position + Vec3{0, eye_height, 0}, aim_direction(yaw, pitch), c.weapon};
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
