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

bool perk_active(const Loadout& loadout, const MoveState& state, size_t index) {
    if (index >= loadout.triggered.size() || index >= state.perk_time.size()) return false;
    const TriggeredPerk& p = loadout.triggered[index];
    switch (p.trigger) {
    case PerkTrigger::crouched: return state.crouched && !state.rolling();
    case PerkTrigger::crouched_still: return state.crouched && state.still >= p.after;
    case PerkTrigger::paused: return state.combat.idle >= p.after;
    case PerkTrigger::rolling: return state.rolling();
    default: return !p.timer_is_cooldown() && state.perk_time[index] > 0;
    }
}

bool start_perk(const Loadout& loadout, MoveState& state, size_t index) {
    if (index >= loadout.triggered.size() || index >= state.perk_time.size()) return false;
    const TriggeredPerk& p = loadout.triggered[index];
    float& timer = state.perk_time[index];
    if (p.timer_is_cooldown()) {
        if (timer > 0) return false;
        timer = float(p.cooldown);
    } else if (p.duration > 0) {
        timer = float(p.duration);
    }
    return true;
}

CharacterStats perk_stats(const CharacterStats& stats, const Loadout& loadout, const MoveState& state) {
    std::vector<StatModifier> mods;
    for (size_t i = 0; i < loadout.triggered.size(); ++i) {
        if (!loadout.triggered[i].stats.empty() && perk_active(loadout, state, i)) {
            mods.insert(mods.end(), loadout.triggered[i].stats.begin(), loadout.triggered[i].stats.end());
        }
    }
    return mods.empty() ? stats : with_modifiers(stats, mods);
}

PerkHandling perk_handling(const Loadout& loadout, const MoveState& state, int slot) {
    PerkHandling h;
    for (size_t i = 0; i < loadout.triggered.size(); ++i) {
        const TriggeredPerk& p = loadout.triggered[i];
        // (An armor perk, without slots, is for every weapon.)
        if (p.weapons.empty() || slot < 0 || (p.slots != 0 && (p.slots & (1u << slot)) == 0) ||
            !perk_active(loadout, state, i)) {
            continue;
        }
        for (const WeaponModifier& m : p.weapons) {
            switch (m.field) {
            case WeaponModifier::spread: h.spread *= m.multiply; break;
            case WeaponModifier::bloom: h.bloom *= m.multiply; break;
            case WeaponModifier::heat: h.heat *= m.multiply; break;
            case WeaponModifier::reload: h.reload *= m.multiply; break;
            case WeaponModifier::recovery: h.recovery *= m.multiply; break;
            case WeaponModifier::speed: h.speed *= m.multiply; break;
            case WeaponModifier::swap: h.swap *= m.multiply; break;
            default: break;  // (what a bolt carries isn't triggered)
            }
        }
    }
    return h;
}

void reset_loadout(MoveState& state, const Loadout& loadout, const CharacterStats& stats) {
    state.combat = {};
    state.perk_time = {};
    state.still = 0;
    state.slide = 0;
    state.rolls = 0;
    state.combat.weapon = uint8_t(weapon_in_slot(loadout, primary_slot) ? primary_slot : sidearm_slot);
    // (A stack carried may be bigger than the standard: ammo_capacity lets it
    // grow so; it's never cut back here, which would lose rounds.)
    int pools[3] = {-1, loadout.unified_standard_ammo, loadout.unified_heavy_ammo};
    for (int i = 1; i <= 2; ++i) {
        if (pools[i] >= 0) {
            pools[i] = std::min(pools[i], int(max_ammo_for(AmmoClass(i)) * max_ammo_capacity));
        }
    }
    for (int slot = 0; slot < weapon_slots; ++slot) {
        if (const WeaponDef* def = weapon_in_slot(loadout, slot)) {
            WeaponState& w = state.combat.weapons[size_t(slot)];
            const AmmoClass ac = def->ammo_class;
            if (ac != AmmoClass::none && loadout.unified_ammo(ac) >= 0) {
                int& pool = pools[size_t(ac)];
                if (def->rounds_per_clip > 0) {
                    w.rounds = int16_t(std::min(def->rounds_per_clip, std::max(0, pool)));
                    pool -= w.rounds;
                } else {
                    w.rounds = 0;
                }
            } else {
                w.rounds = int16_t(def->rounds_per_clip);
                // Its clips, more with a perk's capacity (Ammo Webbing).
                const int carried = int(std::lround(def->ammo * def->rounds_per_clip * stats.ammo_capacity));
                w.reserve = def->ammo > 0 ? int16_t(std::clamp(carried - def->rounds_per_clip, 0, 32000))
                                          : int16_t(-1);
            }
        }
    }
    for (int slot = 0; slot < weapon_slots; ++slot) {
        if (const WeaponDef* def = weapon_in_slot(loadout, slot)) {
            if (def->ammo_class != AmmoClass::none && loadout.unified_ammo(def->ammo_class) >= 0) {
                state.combat.weapons[size_t(slot)].reserve = int16_t(std::max(0, pools[size_t(def->ammo_class)]));
            }
        }
    }
    state.energy = stats.move.max_energy;
    state.sprinting = false;
}

bool fall_back_to_plain_ammo(Loadout& loadout, AmmoClass ac) {
    if (ac == AmmoClass::none || loadout.unified_ammo(ac) < 0) {
        return false;  // (plain already, or fallen back)
    }
    loadout.set_unified_ammo(ac, -1);
    loadout.ammo_spent[size_t(ac)] = true;
    for (int s = 0; s < weapon_slots; ++s) {
        std::optional<WeaponDef>& w = loadout.weapons[size_t(s)];
        if (w && w->ammo_class == ac && loadout.plain_weapons[size_t(s)]) {
            w = loadout.plain_weapons[size_t(s)];
        }
    }
    return true;
}

void unlimited_world_ammo(Loadout& loadout) {
    for (std::optional<WeaponDef>& w : loadout.weapons) {
        if (w && w->ammo_class != AmmoClass::none && loadout.unified_ammo(w->ammo_class) < 0) {
            w->ammo = 0;
        }
    }
}

namespace {

void sync_unified_pool(CombatState& combat, const Loadout& loadout, AmmoClass ac, int new_reserve) {
    if (ac == AmmoClass::none || loadout.unified_ammo(ac) < 0) return;
    for (int slot = 0; slot < weapon_slots; ++slot) {
        if (const WeaponDef* def = weapon_in_slot(loadout, slot)) {
            if (def->ammo_class == ac) {
                combat.weapons[size_t(slot)].reserve = int16_t(std::max(0, new_reserve));
            }
        }
    }
}

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

void reload_now(MoveState& state, const Loadout& loadout, int slot) {
    const WeaponDef* def = weapon_in_slot(loadout, slot);
    if (def == nullptr || def->rounds_per_clip <= 0) return;
    WeaponState& w = state.combat.weapons[size_t(slot)];
    refill(w, *def);
    sync_unified_pool(state.combat, loadout, def->ammo_class, w.reserve);
    if (state.combat.weapon == slot) state.combat.reload = 0;
}

namespace {

bool add_ammo(MoveState& state, const Loadout& loadout, auto amount_for) {
    bool changed = false;
    for (int slot = 0; slot < weapon_slots; ++slot) {
        const WeaponDef* def = weapon_in_slot(loadout, slot);
        WeaponState& w = state.combat.weapons[size_t(slot)];
        if (def == nullptr || def->ammo <= 0 || w.reserve < 0) {
            continue;
        }
        // Unified-ammo slots consume carried player inventory stacks rather than regenerating from droids.
        if (def->ammo_class != AmmoClass::none && loadout.unified_ammo(def->ammo_class) >= 0) {
            continue;
        }
        const int given = amount_for(*def);
        if (given <= 0) {
            continue;
        }
        // Clip and spare together, up to the full load.
        const int full = def->ammo * def->rounds_per_clip;
        if (w.rounds + w.reserve >= full) {
            continue;  // full (or more: a perk's capacity), never cut back
        }
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
    const auto amount_for = [&](const WeaponDef& def) {
        return std::max(1, int(std::ceil(fraction * def.ammo * def.rounds_per_clip)));
    };
    bool changed = add_ammo(state, loadout, amount_for);
    // Standard rounds carried (the unified pool, from the inventory's
    // stacks) come from a droid too while under droid_standard_ammo_cap:
    // `fraction` of that, as a weapon's own would of its full load; heavy
    // rounds only from what's carried.
    if (loadout.unified_ammo(AmmoClass::standard) >= 0) {
        for (int slot = 0; slot < weapon_slots; ++slot) {
            const WeaponDef* def = weapon_in_slot(loadout, slot);
            if (def == nullptr || def->ammo_class != AmmoClass::standard) {
                continue;
            }
            const int total = remaining_unified_ammo(state.combat, loadout, AmmoClass::standard);
            if (total < droid_standard_ammo_cap) {
                const int given = std::max(1, int(std::ceil(fraction * droid_standard_ammo_cap)));
                const int added = std::min(given, droid_standard_ammo_cap - total);
                const int reserve = std::max<int>(0, state.combat.weapons[size_t(slot)].reserve);
                sync_unified_pool(state.combat, loadout, AmmoClass::standard, reserve + added);
                changed = true;
            }
            break;  // (one pool, shared by them all)
        }
    }
    return changed;
}

bool resupply_clips(MoveState& state, const Loadout& loadout, int clips) {
    if (clips <= 0) {
        return false;
    }
    return add_ammo(state, loadout, [&](const WeaponDef& def) { return clips * def.rounds_per_clip; });
}

int remaining_unified_ammo(const CombatState& combat, const Loadout& loadout, AmmoClass ac) {
    if (ac != AmmoClass::none && loadout.ammo_spent[size_t(ac)]) {
        return 0;  // its effect stack ran dry: firing plain ammo now
    }
    if (ac == AmmoClass::none || loadout.unified_ammo(ac) < 0) {
        return -1;
    }
    int reserve = -1;
    int clip_sum = 0;
    for (int slot = 0; slot < weapon_slots; ++slot) {
        if (const WeaponDef* def = weapon_in_slot(loadout, slot); def && def->ammo_class == ac) {
            const WeaponState& w = combat.weapons[size_t(slot)];
            if (reserve < 0) reserve = std::max<int>(0, w.reserve);
            clip_sum += std::max<int>(0, w.rounds);
        }
    }
    // If no weapon of this AmmoClass is equipped in `loadout`, carry over the loadout's initial pool unchanged.
    return reserve < 0 ? loadout.unified_ammo(ac) : (reserve + clip_sum);
}

std::optional<Shot> simulate_weapons(MoveState& state, const MoveInput& pressed, const Loadout& loadout, double dt) {
    // Rolling, staggered, or blocking: cannot fire, throw or reload (a
    // perk's weapon fires while rolling: Roll Shot, Teras Kasi Master).
    MoveInput input = pressed;
    CombatState& c = state.combat;
    if (state.stagger > 0 || state.blocking) {
        input.buttons &= uint8_t(~(MoveInput::fire | MoveInput::secondary | MoveInput::reload));
    } else if (state.rolling()) {
        const WeaponDef* in_hand = weapon_in_slot(loadout, c.weapon);
        const bool fires = in_hand != nullptr && in_hand->fire_rolling;
        input.buttons &= uint8_t(~(MoveInput::secondary | MoveInput::reload | (fires ? 0 : MoveInput::fire)));
    }
    c.cooldown = std::max(0.0, c.cooldown - dt);
    c.idle = std::min(60.0f, c.idle + float(dt));
    for (int i = 0; i < weapon_slots; ++i) {
        const WeaponDef* def = weapon_in_slot(loadout, i);
        WeaponState& w = c.weapons[size_t(i)];
        if (def == nullptr) {
            continue;
        }
        // Holstered: a perk's weapon cools faster (Cool Holster), or reloads
        // itself (Tactical Swap).
        const bool holstered = i < hand_slots && i != c.weapon;
        const double cooling = holstered ? def->holster_cooling : 1.0;
        w.heat = float(std::max(0.0, w.heat - def->heat_recover_rate * cooling * dt));
        if (w.overheated && w.heat <= def->heat_threshold) {
            w.overheated = false;
        }
        if (holstered && w.reload > 0) {
            w.reload = float(std::max(0.0, double(w.reload) - dt));
            if (w.reload <= 0) {
                refill(w, *def);
                sync_unified_pool(c, loadout, def->ammo_class, w.reserve);
            }
        }
    }

    // Switching between the held weapons (to one the loadout has) cancels a
    // reload and takes a moment: the weapon brought up's swap time. (One
    // that reloads itself holstered goes on reloading there, and finishes
    // in the hand if it's brought back first.)
    const uint8_t wanted = uint8_t(input.weapon);
    if (wanted != c.weapon && wanted < hand_slots && weapon_in_slot(loadout, wanted)) {
        if (const WeaponDef* old = weapon_in_slot(loadout, c.weapon); old != nullptr && old->holster_reload) {
            WeaponState& was = c.weapons[c.weapon];
            if (old->rounds_per_clip > 0 && was.rounds < old->rounds_per_clip && was.reserve != 0) {
                was.reload = float(c.reload > 0 ? c.reload : old->reload_time);
            }
        }
        c.weapon = wanted;
        WeaponState& brought = c.weapons[wanted];
        c.reload = brought.reload > 0 ? double(brought.reload) : 0.0;
        brought.reload = 0;
        c.cooldown = std::max(c.cooldown, weapon_in_slot(loadout, wanted)->swap_time *
                                              perk_handling(loadout, state, wanted).swap);
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
    const PerkHandling perk = perk_handling(loadout, state, c.weapon);
    c.spread = std::max(0.0, c.spread - def.spread_recover_rate * perk.recovery * dt);

    // Sprinting holds a reload where it is (a perk's weapon carries on: Combat Reload).
    if (c.reload > 0 && !(state.sprinting && !def.reload_sprinting)) {
        c.reload -= dt;
        if (c.reload <= 0) {
            c.reload = 0;
            refill(weapon, def);
            sync_unified_pool(c, loadout, def.ammo_class, weapon.reserve);
        }
    }
    const bool uses_clip = def.rounds_per_clip > 0;
    const bool uses_unified = def.ammo_class != AmmoClass::none && loadout.unified_ammo(def.ammo_class) >= 0;
    const bool can_reload = uses_clip && weapon.rounds < def.rounds_per_clip && weapon.reserve != 0;
    if (can_reload && c.reload == 0 && input.held(MoveInput::reload)) {
        c.reload = def.reload_time * perk.reload;
    }

    if (!input.held(MoveInput::fire) || c.cooldown > 0 || c.reload > 0) {
        return std::nullopt;
    }
    if (uses_clip && weapon.rounds <= 0) {
        if (can_reload) {
            c.reload = def.reload_time * perk.reload;  // pulling the trigger on an empty clip reloads
        }
        return std::nullopt;
    }
    if (!uses_clip && uses_unified && def.projectile != Projectile::melee && weapon.reserve <= 0) {
        return std::nullopt;
    }
    if (weapon.overheated) {
        return std::nullopt;
    }

    // Fire: melee swings lunge forward and sweep; ranged shots fire pellets.
    // (A "paused" perk saw the pause first: its handling is this shot's.)
    c.cooldown += def.shot_delay * perk.speed;
    c.idle = 0;
    if (def.projectile == Projectile::melee) {
        if (def.melee_lunge > 0) {
            const Vec3 fwd{-std::sin(double(input.yaw)), 0, -std::cos(double(input.yaw))};
            state.velocity.x += fwd.x * def.melee_lunge;
            state.velocity.z += fwd.z * def.melee_lunge;
            const double horiz = std::hypot(state.velocity.x, state.velocity.z);
            const double cap = std::max(12.0, def.melee_lunge * 1.4);
            if (horiz > cap) {
                state.velocity.x *= cap / horiz;
                state.velocity.z *= cap / horiz;
            }
        }
        return Shot{state.position + Vec3{0, eye_height_of(state), 0}, {aim_direction(input.yaw, input.pitch)}, c.weapon,
                    input.aiming};
    }
    const int pellets = uses_clip
        ? std::min<int>(def.shots_per_salvo, weapon.rounds)
        : (uses_unified ? std::min<int>(def.shots_per_salvo, std::max<int>(0, weapon.reserve)) : def.shots_per_salvo);
    if (uses_clip) {
        weapon.rounds = int16_t(weapon.rounds - pellets);
    } else if (uses_unified) {
        weapon.reserve = int16_t(std::max<int>(0, weapon.reserve - pellets));
        sync_unified_pool(c, loadout, def.ammo_class, weapon.reserve);
    }
    if (def.heat_per_shot > 0) {
        weapon.heat = float(std::min(1.0, weapon.heat + def.heat_per_shot * perk.heat));
        weapon.overheated = weapon.heat >= 1;
        // An overheat triggers perks (Overclock: it vents at once).
        for (size_t i = 0; weapon.overheated && i < loadout.triggered.size(); ++i) {
            const TriggeredPerk& p = loadout.triggered[i];
            if (p.trigger == PerkTrigger::overheat && (p.slots == 0 || (p.slots & (1u << c.weapon)) != 0) &&
                start_perk(loadout, state, i)) {
                weapon.heat = float(std::max(0.0, weapon.heat - p.cool));
                weapon.overheated = weapon.heat >= 1;
            }
        }
    }
    Shot shot{state.position + Vec3{0, eye_height_of(state), 0}, {}, c.weapon, input.aiming};
    for (int i = 0; i < pellets; ++i) {
        const auto [u, v] = random_pair(input.sequence ^ (uint64_t(i) << 32));
        const double aim = state.crouched ? 1 / crouch_aim_factor : 1.0;  // steadier crouched
        const double yaw = input.yaw + u * (def.yaw_spread * perk.spread + c.spread) * aim * degrees;
        const double pitch =
            input.pitch + def.shot_elevate + v * (def.pitch_spread * perk.spread + c.spread) * aim * degrees;
        shot.directions.push_back(aim_direction(yaw, pitch));
    }
    c.spread = std::min(def.spread_limit, c.spread + def.spread_per_shot * perk.spread * perk.bloom);
    return shot;
}

std::optional<Shot> simulate_character(MoveState& state, const MoveInput& input, bool alive, const World& world,
                                       const Loadout& loadout, const CharacterStats& stats, double dt,
                                       std::span<const VehicleClass> vehicles, const Battlefield* field) {
    for (float& t : state.perk_time) {
        t = std::max(0.0f, t - float(dt));
    }
    if (state.vehicle >= 0 && size_t(state.vehicle) < vehicles.size() && state.seat > 0) {
        // A gunner: carried, its guns turned within the seat's limits.
        const VehicleSeat* seat = vehicle_seat(vehicles[size_t(state.vehicle)], state.seat);
        if (!alive || seat == nullptr) {
            return std::nullopt;
        }
        state.pitch = input.pitch;
        MoveInput aimed = input;
        const auto [yaw, pitch] = seat_aim(state, input, *seat);
        aimed.yaw = float(yaw);
        aimed.pitch = float(pitch);
        state.roll_yaw = float(yaw);  // the guns' way, for drawing them
        state.pitch = float(pitch);
        aimed.buttons &= uint8_t(~(MoveInput::secondary | MoveInput::crouch | MoveInput::jump | MoveInput::sprint));
        std::optional<Shot> shot = simulate_weapons(state, aimed, seat->loadout, dt);
        if (shot) {
            shot->origin = seat_muzzle(state, *seat);
        }
        return shot;
    }
    if (state.vehicle >= 0 && size_t(state.vehicle) < vehicles.size()) {
        const VehicleClass& vehicle = vehicles[size_t(state.vehicle)];
        MoveInput steer = input;
        if (!alive) {
            steer.forward = steer.right = 0;
            steer.buttons = 0;
            steer.yaw = state.yaw;
        }
        simulate_vehicle(state, steer, world, vehicle, dt, field, stats.vehicle_speed, stats.vehicle_boost_drain);
        if (!alive) {
            return std::nullopt;
        }
        MoveInput aimed = input;
        const auto [yaw, pitch] = vehicle_aim(state, input, vehicle);
        aimed.yaw = float(yaw);
        aimed.pitch = float(pitch);
        state.roll_yaw = float(yaw);  // the guns' way, for drawing them
        // Its second weapon (a starfighter's torpedoes, a gunship's
        // missiles) on the secondary button, its first on fire: each fires
        // its own, switching to it first.
        if (input.held(MoveInput::secondary) && weapon_in_slot(vehicle.loadout, 1) != nullptr) {
            aimed.weapon = 1;
            aimed.buttons |= MoveInput::fire;
        } else if (input.held(MoveInput::fire)) {
            aimed.weapon = 0;
        }
        aimed.buttons &= uint8_t(~(MoveInput::secondary | MoveInput::crouch | MoveInput::jump));
        std::optional<Shot> shot = simulate_weapons(state, aimed, vehicle.loadout, dt);
        if (shot) {
            shot->origin = vehicle_muzzle(state, vehicle);
        }
        return shot;
    }
    if (!alive) {
        MoveInput idle;
        idle.sequence = input.sequence;
        idle.yaw = state.yaw;
        state = simulate_move(state, idle, world, stats.move, dt);
        return std::nullopt;
    }
    MoveInput char_input = input;
    const WeaponDef* active_w = weapon_in_slot(loadout, state.combat.weapon);
    if (active_w == nullptr || active_w->projectile != Projectile::melee) {
        char_input.buttons &= uint8_t(~MoveInput::block);
    }
    // Active perks change how it moves (Pathfinder, Ghost Step...); a melee
    // weapon how it blocks and parries (Iron Guard, Riposte).
    const bool was_rolling = state.rolling();
    MoveParams params = loadout.triggered.empty() ? stats.move : perk_stats(stats, loadout, state).move;
    if (active_w != nullptr && active_w->projectile == Projectile::melee) {
        params.block_cost = active_w->block_cost;
        params.parry_window *= active_w->parry_window;
    }
    state = simulate_move(state, char_input, world, params, dt);
    if (!loadout.triggered.empty()) {
        const bool still = state.crouched && state.grounded && std::hypot(state.velocity.x, state.velocity.z) < 0.25;
        state.still = still ? std::min(60.0f, state.still + float(dt)) : 0.0f;
        if (!was_rolling && state.rolling()) {
            for (size_t i = 0; i < loadout.triggered.size(); ++i) {
                if (loadout.triggered[i].trigger == PerkTrigger::roll) start_perk(loadout, state, i);
            }
        }
    }
    return simulate_weapons(state, char_input, loadout, dt);
}

double explosion_push(const WeaponDef& weapon, Vec3 center, Vec3 feet) {
    if (weapon.explosion_push <= 0 || weapon.push_outer <= 0) {
        return 0;
    }
    const double distance = (feet + Vec3{0, 1.0, 0} - center).length();
    if (distance >= weapon.push_outer) {
        return distance <= weapon.push_inner ? weapon.explosion_push : 0;
    }
    if (distance <= weapon.push_inner) {
        return weapon.explosion_push;
    }
    return weapon.explosion_push * (weapon.push_outer - distance) / (weapon.push_outer - weapon.push_inner);
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

ShieldHitResult apply_shield_damage(double& shield, double mitigated_amount, DamageType type,
                                    double extra_shield_disrupt, double shield_factor) {
    ShieldHitResult out;
    if (mitigated_amount <= 0) {
        return out;
    }
    // Shield points a point of damage takes (a weapon's shield factor).
    const double f = std::max(0.05, shield_factor);
    if (type == DamageType::electro) {
        const double drain = mitigated_amount * 1.75 * f;
        if (shield <= 0) {
            out.health_damage = mitigated_amount;
        } else if (shield >= drain) {
            shield -= drain;
            out.shield_damage = drain;
        } else {
            out.overloaded = true;
            out.shield_damage = shield;
            out.disrupt_seconds = 3.0;
            out.health_damage = mitigated_amount * (1.0 - shield / drain);
            shield = 0;
        }
        return out;
    }
    if (type == DamageType::melee) {
        const double pierce = mitigated_amount * 0.5;
        const double shield_portion = mitigated_amount - pierce;
        const double absorbed = std::min(shield, shield_portion * f);
        shield -= absorbed;
        double extra = 0;
        if (extra_shield_disrupt > 2.5 && shield > 0) {
            extra = std::min(shield, shield_portion * 0.5 * f);
            shield -= extra;
        }
        out.shield_damage = absorbed + extra;
        out.health_damage = pierce + (shield_portion - absorbed / f);
        out.disrupt_seconds = std::max(2.5, extra_shield_disrupt);
        return out;
    }
    const double absorbed = std::min(shield, mitigated_amount * f);
    shield -= absorbed;
    out.shield_damage = absorbed;
    out.health_damage = mitigated_amount - absorbed / f;
    return out;
}

double hit_damage(const WeaponDef& weapon, double base, bool headshot, double roll) {
    double amount = base * (headshot ? weapon.headshot_multiplier : weapon.body_multiplier);
    if (weapon.crit_chance > 0 && roll < weapon.crit_chance) {
        amount *= weapon.crit_multiplier;
    }
    return amount;
}

bool hit_effect_lands(const WeaponDef& weapon, double roll) {
    return weapon.hit_effect && weapon.hit_effect->duration > 0 && roll < weapon.hit_effect->chance;
}

} // namespace swm::core
