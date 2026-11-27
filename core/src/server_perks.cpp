// Triggered trainer perks on the server (docs/PERKS.md, Triggers). The
// shared simulation (combat.cpp) runs the stances and what it sees itself (a
// roll begun, an overheat); here are the events only the server knows: hits
// and kills, a Deflector Shield broken, low health, a while unhurt, a killing
// blow, a hit of some type. They start a perk's timer in the character's
// state (sent back to its client like the rest of it) or act at once. And
// some of the perks' own mechanics: staggers and knockback (less what the
// target resists), the shoulder charge, how far outlaws notice you from.
#include <algorithm>
#include <cmath>
#include <format>

#include "swm/core/server.h"

namespace swm::core {

namespace {

// How long a held condition (low health, unhurt) keeps its perk going after
// it last held: it's renewed every tick while it does.
constexpr float held_for = 0.25f;

// A weapon perk is for its weapons; an armor perk (no slots) for any.
bool for_slot(const TriggeredPerk& p, int slot) {
    return slot >= 0 && slot < weapon_slots && (p.slots == 0 || (p.slots & (1u << slot)) != 0);
}

double multiply_of(std::span<const StatModifier> mods, Stat stat) {
    double m = 1;
    for (const StatModifier& mod : mods) {
        if (mod.stat == stat) m *= mod.multiply;
    }
    return m;
}

} // namespace

double GameServer::perk_clock() const {
    return double(tick_) / config_.tick_rate;
}

bool GameServer::in_combat(const Client& client) const {
    const double now = perk_clock();
    return now - client.last_hurt_at < out_of_combat_delay || now - client.perks.last_hit < out_of_combat_delay;
}

CharacterStats GameServer::active_stats(const Client& client) const {
    const Loadout& lo = loadout_of(client);
    CharacterStats stats = perk_stats(client.stats, lo, client.state);
    for (size_t i = 0; i < lo.triggered.size(); ++i) {
        const TriggeredPerk& p = lo.triggered[i];
        if (p.resist > 0 && perk_active(lo, client.state, i)) {  // Adaptive Plating: the type that struck
            double& r = stats.mitigation_by_type[size_t(client.perks.resist_type)];
            r = std::min(max_mitigation, r + p.resist);
        }
    }
    return stats;
}

void GameServer::perk_act(Client& client, const TriggeredPerk& p, int slot) {
    MoveState& state = client.state;
    const Loadout& lo = loadout_of(client);
    const int weapon = slot >= 0 && slot < weapon_slots ? slot : int(state.combat.weapon);
    if (p.stamina > 0) {
        state.energy = std::min(active_stats(client).move.max_energy, state.energy + p.stamina);
    }
    if (p.shield > 0 && client.stats.shield_capacity > 0) {
        client.shield = std::min(client.stats.shield_capacity, client.shield + p.shield * client.stats.shield_capacity);
    }
    if (p.cool > 0) {
        WeaponState& w = state.combat.weapons[size_t(weapon)];
        w.heat = float(std::max(0.0, w.heat - p.cool));
        if (w.heat < 1) w.overheated = false;
    }
    if (p.reload) {
        reload_now(state, lo, weapon);
    }
    if (p.recharge) {
        client.shield_recharge_timer = 0;
    }
    if (p.cloak > 0) {
        TimedEffect e;
        e.id = "perk:" + p.id;
        e.name = "Cloaked";
        e.source = client.entity;
        e.remaining = e.duration = p.cloak;
        e.flags = effect_flag::cloaked;
        give_effect(client, std::move(e));
    }
}

double GameServer::perk_shot_damage(const Client* shooter, int slot, const WeaponDef& weapon, double base, bool headshot,
                                    double target_share, bool behind, bool crit) {
    const double roll = random_unit();
    const double flank = behind ? weapon.flank_multiplier : 1.0;  // Flanker
    if (crit) {  // a riposte
        return hit_damage(weapon, base, headshot, 1.0) * weapon.crit_multiplier * flank;
    }
    if (shooter != nullptr && shooter->state.vehicle < 0) {
        for (const TriggeredPerk& p : loadout_of(*shooter).triggered) {
            // Execution: a hit on the badly hurt is a critical hit.
            if (p.trigger == PerkTrigger::target_low_health && p.crit && for_slot(p, slot) && target_share < p.below) {
                return hit_damage(weapon, base, headshot, 1.0) * weapon.crit_multiplier * flank;
            }
        }
    }
    return hit_damage(weapon, base, headshot, roll) * flank;
}

void GameServer::perk_hit(Client& shooter, int slot, const PerkTarget& target) {
    if (shooter.life != Life::alive || shooter.state.vehicle >= 0 || target.entity == shooter.entity) return;
    const Loadout& lo = loadout_of(shooter);
    const double now = perk_clock();
    PerkState& mine = shooter.perks;
    const bool same_target = target.entity == mine.last_target;
    for (size_t i = 0; i < lo.triggered.size(); ++i) {
        const TriggeredPerk& p = lo.triggered[i];
        if (p.trigger != PerkTrigger::hit || !for_slot(p, slot)) continue;
        if (p.within > 0 && target.distance > p.within) continue;
        if (p.aimed && !target.aimed) continue;  // Spotter: from a shot aimed down the sights
        // Chain: only a hit on the same target soon after the last (Rhythm).
        if (p.chain > 0 && !(same_target && now - mine.last_hit <= p.chain)) continue;
        if (p.chance < 1 && random_unit() >= p.chance) continue;  // Stun Rounds
        if (!start_perk(lo, shooter.state, i)) continue;
        perk_fired(shooter, i);
        perk_act(shooter, p, slot);
        if (p.mark > 0 && !target.killed) {  // Spotter: marked for the shooter's side
            mark_entity(target.entity, conquest_ ? shooter.team : -1, shooter.entity, p.mark, net::FieldMark::marked);
        }
        if (!p.target.empty() && !target.killed) {
            if (target.client != nullptr) {
                TimedEffect e;
                e.id = "perk:" + p.id;
                e.name = "Suppressed";
                e.source = shooter.entity;
                e.remaining = e.duration = p.target_duration;
                e.modifiers = p.target;
                e.flags = effect_flag::debuff;
                give_effect(*target.client, std::move(e));
            } else if (target.npc != nullptr && target.npc->life == Life::alive) {
                const double speed = multiply_of(p.target, Stat::run_speed) * multiply_of(p.target, Stat::sprint_factor);
                if (speed < 1) {
                    npc_base_speed_.try_emplace(target.npc->entity, target.npc->walk_speed, target.npc->run_speed);
                    const std::string id = "perk:" + p.id;
                    bool refreshed = false;
                    for (NpcEffect& e : npc_effects_) {
                        if (e.entity == target.npc->entity && e.id == id) {
                            e.remaining = std::max(e.remaining, p.target_duration);
                            refreshed = true;
                        }
                    }
                    if (!refreshed) {
                        npc_effects_.push_back({target.npc->entity, id, p.target_duration, speed, 0, DamageType::bio,
                                                shooter.entity, false});
                    }
                }
            }
        }
    }
    mine.last_target = target.entity;
    mine.last_hit = now;
    if (target.killed) perk_kill(shooter, slot, target.headshot);
}

void GameServer::perk_fired(const Client& client, size_t index) {
    events_.push_back({net::CombatEvent::perk, client.entity, uint32_t(index), 0});
}

void GameServer::perk_kill(Client& killer, int slot, bool headshot) {
    const Loadout& lo = loadout_of(killer);
    const double now = perk_clock();
    for (size_t i = 0; i < lo.triggered.size(); ++i) {
        const TriggeredPerk& p = lo.triggered[i];
        const bool fits = p.trigger == PerkTrigger::kill || (p.trigger == PerkTrigger::headshot_kill && headshot);
        if (!fits || !for_slot(p, slot)) continue;
        // Chain: only a kill soon after the last (Chain Breach).
        if (p.chain > 0 && now - killer.perks.last_kill > p.chain) continue;
        if (start_perk(lo, killer.state, i)) {
            perk_fired(killer, i);
            perk_act(killer, p, slot);
        }
    }
    killer.perks.last_kill = now;
}

double GameServer::perk_incoming(Client& victim, double amount, DamageType type) {
    const Loadout& lo = loadout_of(victim);
    if (lo.triggered.empty()) return amount;
    // Taken less while active (Evasion, Ablative, Second Chance); a one-hit
    // perk is spent by it.
    for (size_t i = 0; i < lo.triggered.size(); ++i) {
        const TriggeredPerk& p = lo.triggered[i];
        if (p.damage_taken != 1 && perk_active(lo, victim.state, i)) {
            amount *= p.damage_taken;
            if (p.one_hit) victim.state.perk_time[i] = 0;
        }
    }
    // A hit: unhurt no longer; a hit of a type (Adaptive Plating).
    victim.perks.last_hurt = perk_clock();
    for (size_t i = 0; i < lo.triggered.size(); ++i) {
        const TriggeredPerk& p = lo.triggered[i];
        if (p.trigger == PerkTrigger::unhit) {
            victim.state.perk_time[i] = 0;
        } else if (p.trigger == PerkTrigger::hit_by && start_perk(lo, victim.state, i)) {
            perk_fired(victim, i);
            victim.perks.resist_type = type;
            perk_act(victim, p, -1);
        }
    }
    return amount;
}

void GameServer::perk_shield_broken(Client& victim) {
    const Loadout& lo = loadout_of(victim);
    for (size_t i = 0; i < lo.triggered.size(); ++i) {
        const TriggeredPerk& p = lo.triggered[i];
        if (p.trigger == PerkTrigger::shield_broken && start_perk(lo, victim.state, i)) {
            perk_fired(victim, i);
            perk_act(victim, p, -1);
        }
    }
}

bool GameServer::perk_lethal(Client& victim) {
    const Loadout& lo = loadout_of(victim);
    for (size_t i = 0; i < lo.triggered.size(); ++i) {
        const TriggeredPerk& p = lo.triggered[i];
        if (p.trigger != PerkTrigger::lethal || (p.once_per_life && victim.perks.second_chance_used)) continue;
        if (!start_perk(lo, victim.state, i)) continue;
        victim.perks.second_chance_used = victim.perks.second_chance_used || p.once_per_life;
        victim.health = 1;
        perk_fired(victim, i);
        perk_act(victim, p, -1);
        return true;
    }
    return false;
}

void GameServer::update_perks(Client& client) {
    if (client.life != Life::alive) return;
    const Loadout& lo = loadout_of(client);
    if (lo.triggered.empty()) return;
    const double now = perk_clock();
    const double share = client.stats.max_health > 0 ? client.health / client.stats.max_health : 1;
    for (size_t i = 0; i < lo.triggered.size(); ++i) {
        const TriggeredPerk& p = lo.triggered[i];
        // Held conditions: renewed while they hold (Last Stand, Untouchable).
        const bool holds = (p.trigger == PerkTrigger::low_health && share < p.below) ||
                           (p.trigger == PerkTrigger::unhit && now - client.perks.last_hurt >= p.after);
        if (holds) client.state.perk_time[i] = std::max(client.state.perk_time[i], held_for);
    }
}

void GameServer::perk_life_ended(Client& client) {
    client.perks.second_chance_used = false;
    client.perks.last_hurt = perk_clock();
    client.perks.last_target = 0;
    client.perks.riposte_until = -1e9;
    client.perks.combo_count = 0;
}

void GameServer::stagger(Client& client, double seconds) {
    const double kept = seconds * (1.0 - std::clamp(client.stats.stagger_resist, 0.0, 1.0));
    if (kept > 0) client.state.stagger = std::max(client.state.stagger, float(kept));
}

void GameServer::knock_back(Client& client, Vec3 from, double speed) {
    const double resist = loadout_of(client).triggered.empty() ? client.stats.explosion_push_resist
                                                               : active_stats(client).explosion_push_resist;
    speed *= 1.0 - std::clamp(resist, 0.0, max_mitigation);
    if (speed <= 0) return;
    Vec3 away{client.state.position.x - from.x, 0, client.state.position.z - from.z};
    const double length = std::hypot(away.x, away.z);
    away = length > 1e-4 ? away * (1 / length) : Vec3{0, 0, 1};
    away.y = 0.35;  // and off its feet
    client.state.velocity += away * speed;
    client.state.grounded = false;
}

void GameServer::update_charge(Client& client) {
    // Shoulder Charge: sprinting into an enemy (within a body's length ahead)
    // staggers them, and shoves them a little; then it rests a while.
    constexpr double reach = 1.4, rest = 4.0, shove = 4.0;
    if (client.life != Life::alive || client.stats.charge <= 0 || !client.state.sprinting ||
        client.state.vehicle >= 0 || perk_clock() < client.perks.charge_ready) {
        return;
    }
    const Vec3 at = client.state.position;
    const Vec3 fwd{-std::sin(double(client.state.yaw)), 0, -std::cos(double(client.state.yaw))};
    const auto ahead = [&](Vec3 other, double radius) {
        const Vec3 d{other.x - at.x, 0, other.z - at.z};
        const double horiz = std::hypot(d.x, d.z);
        return std::abs(other.y - at.y) < 1.5 && horiz <= reach + radius && horiz > 1e-4 &&
               (d.x * fwd.x + d.z * fwd.z) / horiz >= 0.6;
    };
    const bool pvp_blocked = !conquest_ && !config_.pvp_enabled;
    for (Client& target : clients_) {
        if (target.entity == client.entity || target.life != Life::alive || target.state.vehicle >= 0 ||
            (allied(client.team, target.team) && !config_.friendly_fire) ||
            (pvp_blocked && !bounty_pvp_allowed(client.entity, target.entity)) || !ahead(target.state.position, hit_radius)) {
            continue;
        }
        stagger(target, client.stats.charge);
        knock_back(target, at, shove);
        client.perks.charge_ready = perk_clock() + rest;
        return;
    }
    for (Npc& npc : npcs_) {
        if (npc.hidden || npc.actor_kind < 2 || npc.life != Life::alive || !ahead(npc.state.position, npc.hit_radius)) {
            continue;
        }
        npc.state.stagger = std::max(npc.state.stagger, float(client.stats.charge));
        npc.last_attacker = client.entity;
        client.perks.charge_ready = perk_clock() + rest;
        return;
    }
}

double GameServer::notice_of(uint32_t entity) const {
    const Client* client = find(entity);
    return client != nullptr ? std::max(0.0, client->stats.notice) : 1.0;
}

} // namespace swm::core
