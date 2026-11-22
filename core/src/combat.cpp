#include "swm/core/combat.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string_view>

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

} // namespace

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

void reset_loadout(MoveState& state, const Loadout& loadout, const CharacterStats& stats) {
    state.combat = {};
    state.combat.weapon = uint8_t(weapon_in_slot(loadout, primary_slot) ? primary_slot : sidearm_slot);
    for (int slot = 0; slot < weapon_slots; ++slot) {
        if (const WeaponDef* def = weapon_in_slot(loadout, slot)) {
            WeaponState& w = state.combat.weapons[size_t(slot)];
            w.rounds = int16_t(def->rounds_per_clip);
            w.reserve = def->ammo > 0 ? int16_t((def->ammo - 1) * def->rounds_per_clip) : int16_t(-1);
        }
    }
    state.energy = stats.move.max_energy;
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

bool add_ammo(MoveState& state, const Loadout& loadout, auto amount_for) {
    bool changed = false;
    for (int slot = 0; slot < weapon_slots; ++slot) {
        const WeaponDef* def = weapon_in_slot(loadout, slot);
        WeaponState& w = state.combat.weapons[size_t(slot)];
        if (def == nullptr || def->ammo <= 0 || w.reserve < 0) {
            continue;
        }
        const int given = amount_for(*def);
        if (given <= 0) {
            continue;
        }
        // Clip and spare together, up to the full load.
        const int full = def->ammo * def->rounds_per_clip;
        const int total = std::min(full, w.rounds + w.reserve + given);
        // Grenades go straight into the hand; a gun's come as spare rounds.
        const int rounds = slot == secondary_slot ? std::min(def->rounds_per_clip, total) : int(w.rounds);
        const int reserve = total - rounds;
        if (rounds != w.rounds || reserve != w.reserve) {
            w.rounds = int16_t(rounds);
            w.reserve = int16_t(reserve);
            changed = true;
        }
    }
    return changed;
}

} // namespace

double bolt_damage(const WeaponDef& weapon, double flown) {
    if (weapon.damage_final < 0 || flown <= weapon.damage_transition_delay) {
        return weapon.damage;
    }
    const double period = weapon.damage_transition_period;
    const double f = period > 0 ? std::min(1.0, (flown - weapon.damage_transition_delay) / period) : 1.0;
    return weapon.damage + (weapon.damage_final - weapon.damage) * f;
}

bool resupply(MoveState& state, const Loadout& loadout, double fraction) {
    if (fraction <= 0) {
        return false;
    }
    return add_ammo(state, loadout, [&](const WeaponDef& def) {
        return std::max(1, int(std::ceil(fraction * def.ammo * def.rounds_per_clip)));
    });
}

bool resupply_clips(MoveState& state, const Loadout& loadout, int clips) {
    if (clips <= 0) {
        return false;
    }
    return add_ammo(state, loadout, [&](const WeaponDef& def) { return clips * def.rounds_per_clip; });
}

std::optional<Shot> simulate_weapons(MoveState& state, const MoveInput& pressed, const Loadout& loadout, double dt) {
    // Rolling, it can't fire, throw or reload.
    MoveInput input = pressed;
    if (state.rolling()) {
        input.buttons &= uint8_t(~(MoveInput::fire | MoveInput::secondary | MoveInput::reload));
    }
    CombatState& c = state.combat;
    c.cooldown = std::max(0.0, c.cooldown - dt);
    for (int i = 0; i < weapon_slots; ++i) {
        const WeaponDef* def = weapon_in_slot(loadout, i);
        WeaponState& w = c.weapons[size_t(i)];
        if (def == nullptr) {
            continue;
        }
        w.heat = float(std::max(0.0, w.heat - def->heat_recover_rate * dt));
        if (w.overheated && w.heat <= def->heat_threshold) {
            w.overheated = false;
        }
    }

    // Switching between the held weapons (to one the loadout has) cancels a
    // reload and takes a moment.
    const uint8_t wanted = uint8_t(input.weapon);
    if (wanted != c.weapon && wanted < hand_slots && weapon_in_slot(loadout, wanted)) {
        c.weapon = wanted;
        c.reload = 0;
        c.cooldown = std::max(c.cooldown, weapon_switch_time);
    }

    // The secondary (a throwable) is used instead of firing, when ready.
    const WeaponDef* thrown = weapon_in_slot(loadout, secondary_slot);
    if (thrown != nullptr && input.held(MoveInput::secondary) && c.cooldown <= 0 && c.reload <= 0) {
        WeaponState& throwables = c.weapons[secondary_slot];
        if (throwables.rounds > 0) {
            c.cooldown += thrown->shot_delay;
            throwables.rounds = int16_t(throwables.rounds - 1);
            if (throwables.rounds == 0) {
                refill(throwables, *thrown);  // the next one comes out of the pouch
            }
            const double pitch = std::min(1.5, double(input.pitch) + thrown->shot_elevate);
            return Shot{state.position + Vec3{0, eye_height_of(state), 0}, {aim_direction(input.yaw, pitch)},
                        secondary_slot};
        }
    }

    const WeaponDef* held = weapon_in_slot(loadout, c.weapon);
    if (held == nullptr) {
        return std::nullopt;
    }
    const WeaponDef& def = *held;
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
    Shot shot{state.position + Vec3{0, eye_height_of(state), 0}, {}, c.weapon};
    for (int i = 0; i < pellets; ++i) {
        const auto [u, v] = random_pair(input.sequence ^ (uint64_t(i) << 32));
        const double aim = state.crouched ? 1 / crouch_aim_factor : 1.0;  // steadier crouched
        const double yaw = input.yaw + u * (def.yaw_spread + c.spread) * aim * degrees;
        const double pitch = input.pitch + def.shot_elevate + v * (def.pitch_spread + c.spread) * aim * degrees;
        shot.directions.push_back(aim_direction(yaw, pitch));
    }
    c.spread = std::min(def.spread_limit, c.spread + def.spread_per_shot);
    return shot;
}

std::optional<Shot> simulate_character(MoveState& state, const MoveInput& input, bool alive, const World& world,
                                       const Loadout& loadout, const CharacterStats& stats, double dt) {
    if (!alive) {
        MoveInput idle;
        idle.sequence = input.sequence;
        idle.yaw = state.yaw;
        state = simulate_move(state, idle, world, stats.move, dt);
        return std::nullopt;
    }
    state = simulate_move(state, input, world, stats.move, dt);
    return simulate_weapons(state, input, loadout, dt);
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
