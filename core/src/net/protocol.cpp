#include "swm/core/net/protocol.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "swm/core/net/bytes.h"

namespace swm::core::net {

namespace {

enum class Type : uint8_t {
    hello = 1,
    welcome,
    input_batch,
    snapshot,
    goodbye,
    refused,
    spawn_request,
    queue_request,
    transfer,
    inventory_sync,
    inventory_ack,
    save_loadout,
    switch_loadout,
    shop_buy,
    destroy_item,
    loot_take,
    vendor_sell,
    use_item,
    quest_sync,
    quest_ack,
    quest_request,
    profession_sync,
    profession_ack,
    crafting_sync,
    crafting_ack,
    gather_request,
    craft_request,
    service_request,
    specialist_request,
    specialist_sync,
    specialist_ack,
};

void put_vec3(ByteWriter& out, Vec3 v) {
    out.put(v.x);
    out.put(v.y);
    out.put(v.z);
}

Vec3 get_vec3(ByteReader& in) {
    Vec3 v;
    v.x = in.get<double>();
    v.y = in.get<double>();
    v.z = in.get<double>();
    return v;
}

void put_quest(ByteWriter& out, const QuestInfo& q) {
    out.put_string(q.id);
    out.put_string(q.title);
    out.put_string(q.giver);
    out.put_long_string(q.summary);
    out.put(uint8_t(std::min<size_t>(q.steps.size(), 32)));
    for (size_t i = 0; i < q.steps.size() && i < 32; ++i) {
        out.put_string(q.steps[i]);
    }
    out.put(q.step);
    out.put(q.count);
    out.put(q.needed);
    out.put(uint8_t(q.has_waypoint));
    if (q.has_waypoint) {
        put_vec3(out, q.waypoint);
    }
    out.put(q.credits);
    out.put(q.experience);
    out.put(q.faction_points);
    out.put(uint8_t(std::min<size_t>(q.items.size(), 16)));
    for (size_t i = 0; i < q.items.size() && i < 16; ++i) {
        out.put_string(q.items[i]);
    }
    out.put(uint8_t(q.mission));
    out.put(q.level);
    out.put_string(q.step_npc);
    out.put(uint8_t(std::min<size_t>(q.targets.size(), 32)));
    for (size_t i = 0; i < q.targets.size() && i < 32; ++i) {
        out.put_string(q.targets[i]);
    }
    out.put(q.area_radius);
}

QuestInfo get_quest(ByteReader& in) {
    QuestInfo q;
    q.id = in.get_string();
    q.title = in.get_string();
    q.giver = in.get_string();
    q.summary = in.get_long_string();
    const size_t steps = in.get<uint8_t>();
    if (steps > 32) throw PacketError("too many quest steps");
    for (size_t i = 0; i < steps; ++i) {
        q.steps.push_back(in.get_string());
    }
    q.step = in.get<uint8_t>();
    q.count = in.get<uint16_t>();
    q.needed = in.get<uint16_t>();
    q.has_waypoint = in.get<uint8_t>() != 0;
    if (q.has_waypoint) {
        q.waypoint = get_vec3(in);
    }
    q.credits = in.get<int64_t>();
    q.experience = in.get<int64_t>();
    q.faction_points = in.get<int64_t>();
    const size_t items = in.get<uint8_t>();
    if (items > 16) throw PacketError("too many quest reward items");
    for (size_t i = 0; i < items; ++i) {
        q.items.push_back(in.get_string());
    }
    q.mission = in.get<uint8_t>() != 0;
    q.level = in.get<uint8_t>();
    q.step_npc = in.get_string();
    const size_t targets = in.get<uint8_t>();
    if (targets > 32) throw PacketError("too many quest targets");
    for (size_t i = 0; i < targets; ++i) {
        q.targets.push_back(in.get_string());
    }
    q.area_radius = in.get<float>();
    return q;
}

// Full precision for now. Quantizing positions and velocities is a later
// bandwidth optimization; clients would then re-simulate from the quantized
// state, as the server must too.
void put_state(ByteWriter& out, const MoveState& state) {
    put_vec3(out, state.position);
    put_vec3(out, state.velocity);
    out.put(state.yaw);
    out.put(state.pitch);
    out.put(uint8_t(state.grounded));
    out.put(state.energy);
    out.put(uint8_t(state.sprinting));
    out.put(uint8_t(uint8_t(state.crouched) | (uint8_t(state.crouch_held) << 1) | (uint8_t(state.blocking) << 2)));
    out.put(state.roll);
    out.put(state.roll_yaw);
    out.put(state.slide);
    out.put(state.rolls);
    out.put(state.parry_window);
    out.put(state.stagger);
    out.put(state.vehicle);
    out.put(state.seat);
    const CombatState& c = state.combat;
    out.put(c.weapon);
    out.put(c.cooldown);
    out.put(c.reload);
    out.put(c.spread);
    out.put(c.idle);
    for (const WeaponState& w : c.weapons) {
        out.put(w.rounds);
        out.put(w.reserve);
        out.put(w.heat);
        out.put(uint8_t(w.overheated));
        out.put(w.reload);
    }
    // Triggered perks' timers (the trailing zeros left off) and stillness.
    size_t timers = state.perk_time.size();
    while (timers > 0 && state.perk_time[timers - 1] <= 0) --timers;
    out.put(uint8_t(timers));
    for (size_t i = 0; i < timers; ++i) out.put(state.perk_time[i]);
    out.put(state.still);
}

// What's drawn of someone else's state (Stage 13): floats (a millimeter at
// a planet's edge), posture as bits, the held weapon and whether it's
// reloading; not its ammunition, heat, energy or timers.
void put_remote_state(ByteWriter& out, const MoveState& state) {
    out.put(float(state.position.x));
    out.put(float(state.position.y));
    out.put(float(state.position.z));
    out.put(float(state.velocity.x));
    out.put(float(state.velocity.y));
    out.put(float(state.velocity.z));
    out.put(state.yaw);
    out.put(state.pitch);
    out.put(uint8_t(uint8_t(state.grounded) | (uint8_t(state.sprinting) << 1) | (uint8_t(state.crouched) << 2) |
                    (uint8_t(state.crouch_held) << 3) | (uint8_t(state.blocking) << 4) |
                    (uint8_t(state.combat.reload > 0) << 5) | (uint8_t(state.slide > 0) << 6)));
    out.put(state.roll);
    out.put(state.roll_yaw);
    out.put(state.stagger);
    out.put(state.vehicle);
    out.put(state.seat);
    out.put(state.combat.weapon);
}

MoveState get_remote_state(ByteReader& in) {
    MoveState state;
    state.position.x = in.get<float>();
    state.position.y = in.get<float>();
    state.position.z = in.get<float>();
    state.velocity.x = in.get<float>();
    state.velocity.y = in.get<float>();
    state.velocity.z = in.get<float>();
    state.yaw = in.get<float>();
    state.pitch = in.get<float>();
    const uint8_t bits = in.get<uint8_t>();
    state.grounded = (bits & 1) != 0;
    state.sprinting = (bits & 2) != 0;
    state.crouched = (bits & 4) != 0;
    state.crouch_held = (bits & 8) != 0;
    state.blocking = (bits & 16) != 0;
    state.combat.reload = (bits & 32) != 0 ? 1.0 : 0.0;
    state.slide = (bits & 64) != 0 ? 1.0f : 0.0f;  // (sliding; how long, not drawn)
    state.roll = in.get<float>();
    state.roll_yaw = in.get<float>();
    state.stagger = in.get<float>();
    state.vehicle = in.get<int8_t>();
    state.seat = in.get<int8_t>();
    state.combat.weapon = in.get<uint8_t>();
    if (state.combat.weapon >= hand_slots) {
        throw PacketError("bad weapon slot");
    }
    return state;
}

MoveState get_state(ByteReader& in) {
    MoveState state;
    state.position = get_vec3(in);
    state.velocity = get_vec3(in);
    state.yaw = in.get<float>();
    state.pitch = in.get<float>();
    state.grounded = in.get<uint8_t>() != 0;
    state.energy = in.get<double>();
    state.sprinting = in.get<uint8_t>() != 0;
    const uint8_t posture = in.get<uint8_t>();
    state.crouched = (posture & 1) != 0;
    state.crouch_held = (posture & 2) != 0;
    state.blocking = (posture & 4) != 0;
    state.roll = in.get<float>();
    state.roll_yaw = in.get<float>();
    state.slide = in.get<float>();
    state.rolls = in.get<uint8_t>();
    state.parry_window = in.get<float>();
    state.stagger = in.get<float>();
    state.vehicle = in.get<int8_t>();
    state.seat = in.get<int8_t>();
    CombatState& c = state.combat;
    c.weapon = in.get<uint8_t>();
    c.cooldown = in.get<double>();
    c.reload = in.get<double>();
    c.spread = in.get<double>();
    c.idle = in.get<float>();
    for (WeaponState& w : c.weapons) {
        w.rounds = in.get<int16_t>();
        w.reserve = in.get<int16_t>();
        w.heat = in.get<float>();
        w.overheated = in.get<uint8_t>() != 0;
        w.reload = in.get<float>();
    }
    const size_t timers = in.get<uint8_t>();
    if (timers > state.perk_time.size()) throw PacketError("too many perk timers");
    for (size_t i = 0; i < timers; ++i) state.perk_time[i] = in.get<float>();
    state.still = in.get<float>();
    if (c.weapon >= hand_slots) {
        throw PacketError("bad weapon slot");
    }
    return state;
}

void put_input(ByteWriter& out, const MoveInput& input) {
    out.put(input.sequence);
    out.put(input.forward);
    out.put(input.right);
    out.put(input.yaw);
    out.put(input.pitch);
    out.put(input.buttons);
    out.put(uint8_t((input.weapon & 0x7f) | (input.aiming ? 0x80 : 0)));  // aiming in the top bit
    out.put(input.view_tick);
}

MoveInput get_input(ByteReader& in) {
    MoveInput input;
    input.sequence = in.get<uint32_t>();
    input.forward = in.get<int8_t>();
    input.right = in.get<int8_t>();
    input.yaw = in.get<float>();
    input.pitch = in.get<float>();
    input.buttons = in.get<uint8_t>();
    const uint8_t weapon = in.get<uint8_t>();
    input.weapon = uint8_t(weapon & 0x7f);
    input.aiming = (weapon & 0x80) != 0;
    input.view_tick = in.get<double>();
    return input;
}

void put_match(ByteWriter& out, const MatchState& m) {
    out.put(uint8_t(m.phase));
    out.put(float(m.time_left));
    out.put(float(m.tickets[0]));
    out.put(float(m.tickets[1]));
    out.put(float(m.scores[0]));
    out.put(float(m.scores[1]));
    out.put(m.score_limit);
    out.put(m.dominant);
    out.put(float(m.victory_left));
    out.put(m.winner);
    out.put(uint8_t(m.posts.size()));
    for (const PostState& post : m.posts) {
        out.put(post.owner);
        out.put(post.holder);
        out.put(post.charge);
        out.put(uint8_t(post.contested));
    }
    out.put(uint16_t(m.powerup_health.size()));
    for (const float health : m.powerup_health) {
        out.put(health);
    }
    out.put(uint16_t(m.doors.size()));
    for (const float open : m.doors) {
        out.put(uint8_t(std::lround(std::clamp(open, 0.0f, 1.0f) * 255)));
    }
    out.put(uint16_t(m.destructibles.size()));
    for (const float health : m.destructibles) {
        out.put(health);
    }
    out.put(m.clock);
    out.put(uint8_t(m.mode));
    out.put(uint8_t(std::min<size_t>(m.flags.size(), 8)));
    for (size_t i = 0; i < m.flags.size() && i < 8; ++i) {
        put_vec3(out, m.flags[i].position);
        out.put(m.flags[i].carrier);
        out.put(uint8_t(m.flags[i].home));
        out.put(m.flags[i].dropped_for);
    }
}

// A character's team: a side's, or the natives' (2).
int8_t get_character_team(ByteReader& in) {
    const int8_t team = in.get<int8_t>();
    if (team < -1 || team > 2) {
        throw PacketError("bad team");
    }
    return team;
}

int8_t get_team(ByteReader& in) {
    const int8_t team = in.get<int8_t>();
    if (team < -1 || team > 1) {
        throw PacketError("bad team");
    }
    return team;
}

MatchState get_match(ByteReader& in) {
    MatchState m;
    const uint8_t phase = in.get<uint8_t>();
    if (phase > uint8_t(Phase::ended)) {
        throw PacketError("bad match phase");
    }
    m.phase = Phase(phase);
    m.time_left = in.get<float>();
    m.tickets[0] = in.get<float>();
    m.tickets[1] = in.get<float>();
    m.scores[0] = in.get<float>();
    m.scores[1] = in.get<float>();
    m.score_limit = in.get<uint16_t>();
    m.dominant = get_team(in);
    m.victory_left = in.get<float>();
    m.winner = get_team(in);
    const size_t count = in.get<uint8_t>();
    for (size_t i = 0; i < count; ++i) {
        PostState post;
        post.owner = get_team(in);
        post.holder = get_team(in);
        post.charge = in.get<float>();
        post.contested = in.get<uint8_t>() != 0;
        m.posts.push_back(post);
    }
    const size_t powerups = in.get<uint16_t>();
    m.powerup_health.reserve(powerups);
    for (size_t i = 0; i < powerups; ++i) {
        m.powerup_health.push_back(in.get<float>());
    }
    const size_t doors = in.get<uint16_t>();
    m.doors.reserve(doors);
    for (size_t i = 0; i < doors; ++i) {
        m.doors.push_back(in.get<uint8_t>() / 255.0f);
    }
    const size_t destructibles = in.get<uint16_t>();
    m.destructibles.reserve(destructibles);
    for (size_t i = 0; i < destructibles; ++i) {
        m.destructibles.push_back(in.get<float>());
    }
    m.clock = in.get<double>();
    const uint8_t mode = in.get<uint8_t>();
    if (mode >= game_mode_count) {
        throw PacketError("bad game mode");
    }
    m.mode = GameMode(mode);
    const size_t flags = in.get<uint8_t>();
    if (flags > 8) {
        throw PacketError("too many flags");
    }
    for (size_t i = 0; i < flags; ++i) {
        FlagState flag;
        flag.position = get_vec3(in);
        flag.carrier = in.get<uint32_t>();
        flag.home = in.get<uint8_t>() != 0;
        flag.dropped_for = in.get<float>();
        m.flags.push_back(flag);
    }
    return m;
}

// Stat modifiers in full precision: gear, weapons and triggered perks all feed
// the client's predicted simulation, which must match the server's exactly.
void put_modifiers(ByteWriter& out, const std::vector<StatModifier>& mods) {
    out.put(uint8_t(std::min<size_t>(mods.size(), 64)));
    for (size_t i = 0; i < mods.size() && i < 64; ++i) {
        out.put(uint8_t(mods[i].stat));
        out.put(mods[i].add);
        out.put(mods[i].multiply);
    }
}

std::vector<StatModifier> get_modifiers(ByteReader& in) {
    const size_t n = in.get<uint8_t>();
    if (n > 64) throw PacketError("too many modifiers");
    std::vector<StatModifier> mods;
    mods.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t s = in.get<uint8_t>();
        if (s > uint8_t(last_stat)) {
            throw PacketError("bad stat");
        }
        StatModifier m;
        m.stat = Stat(s);
        m.add = in.get<double>();
        m.multiply = in.get<double>();
        mods.push_back(m);
    }
    return mods;
}

// A loadout's triggered perks (docs/PERKS.md), for its own client to predict with.
void put_triggered(ByteWriter& out, const std::vector<TriggeredPerk>& perks) {
    out.put(uint8_t(std::min<size_t>(perks.size(), max_perk_timers)));
    for (size_t i = 0; i < perks.size() && i < size_t(max_perk_timers); ++i) {
        const TriggeredPerk& p = perks[i];
        out.put_string(p.id);
        out.put_string(p.name);
        out.put(uint8_t(p.trigger));
        for (const double v : {p.after, p.below, p.within, p.chain, p.chance, p.duration, p.cooldown, p.stamina,
                               p.shield, p.cool, p.cloak, p.mark, p.target_duration, p.damage_taken, p.resist}) {
            out.put(v);
        }
        out.put(p.slots);
        out.put(uint8_t(uint8_t(p.reload) | (uint8_t(p.recharge) << 1) | (uint8_t(p.crit) << 2) |
                        (uint8_t(p.one_hit) << 3) | (uint8_t(p.once_per_life) << 4) | (uint8_t(p.aimed) << 5)));
        put_modifiers(out, p.stats);
        put_modifiers(out, p.target);
        out.put(uint8_t(std::min<size_t>(p.weapons.size(), 16)));
        for (size_t k = 0; k < p.weapons.size() && k < 16; ++k) {
            out.put(uint8_t(p.weapons[k].field));
            out.put(p.weapons[k].multiply);
        }
    }
}

std::vector<TriggeredPerk> get_triggered(ByteReader& in) {
    std::vector<TriggeredPerk> perks;
    const size_t n = in.get<uint8_t>();
    if (n > size_t(max_perk_timers)) throw PacketError("too many triggered perks");
    for (size_t i = 0; i < n; ++i) {
        TriggeredPerk p;
        p.id = in.get_string();
        p.name = in.get_string();
        const uint8_t trigger = in.get<uint8_t>();
        if (trigger > uint8_t(PerkTrigger::hit_by)) throw PacketError("bad perk trigger");
        p.trigger = PerkTrigger(trigger);
        for (double* v : {&p.after, &p.below, &p.within, &p.chain, &p.chance, &p.duration, &p.cooldown, &p.stamina,
                          &p.shield, &p.cool, &p.cloak, &p.mark, &p.target_duration, &p.damage_taken, &p.resist}) {
            *v = in.get<double>();
        }
        p.slots = in.get<uint8_t>();
        const uint8_t flags = in.get<uint8_t>();
        p.reload = (flags & 1) != 0;
        p.recharge = (flags & 2) != 0;
        p.crit = (flags & 4) != 0;
        p.one_hit = (flags & 8) != 0;
        p.once_per_life = (flags & 16) != 0;
        p.aimed = (flags & 32) != 0;
        p.stats = get_modifiers(in);
        p.target = get_modifiers(in);
        const size_t w = in.get<uint8_t>();
        if (w > 16) throw PacketError("too many perk handling changes");
        for (size_t k = 0; k < w; ++k) {
            const uint8_t field = in.get<uint8_t>();
            if (field > uint8_t(WeaponModifier::last_field)) throw PacketError("bad weapon handling");
            p.weapons.push_back({WeaponModifier::Field(field), in.get<double>()});
        }
        perks.push_back(std::move(p));
    }
    return perks;
}

void put_weapon_def(ByteWriter& out, const WeaponDef& w) {
    out.put_string(w.name);
    out.put_string(w.model);
    out.put_string(w.anim_set);
    out.put_string(w.archetype);
    out.put(uint8_t(w.damage_type));
    out.put(uint8_t(w.projectile));
    out.put(int16_t(w.ammo));
    out.put(double(w.shot_delay));
    out.put(int16_t(w.rounds_per_clip));
    out.put(double(w.reload_time));
    out.put(double(w.heat_per_shot));
    out.put(double(w.heat_recover_rate));
    out.put(double(w.heat_threshold));
    out.put(double(w.yaw_spread));
    out.put(double(w.pitch_spread));
    out.put(double(w.spread_per_shot));
    out.put(double(w.spread_recover_rate));
    out.put(double(w.spread_limit));
    out.put(uint8_t(std::clamp(w.shots_per_salvo, 1, 255)));
    out.put(double(w.zoom_min));
    out.put(double(w.zoom_max));
    out.put(uint8_t(w.zoom_first_person));
    out.put(double(w.damage));
    out.put(double(w.bolt_speed));
    out.put(double(w.bolt_gravity));
    out.put(double(w.bolt_life));
    out.put(double(w.damage_transition_delay));
    out.put(double(w.damage_transition_period));
    out.put(double(w.damage_final));
    out.put(double(w.bolt_acceleration));
    out.put(double(w.rebound));
    out.put(double(w.friction));
    out.put(uint8_t(w.explode_on_impact));
    out.put(double(w.fuse));
    out.put(uint8_t(w.timer_on_contact));
    out.put(double(w.trigger_radius));
    out.put(double(w.arm_time));
    out.put(uint8_t(std::clamp(w.trap_shots, 0, 255)));
    out.put(double(w.trap_interval));
    out.put(double(w.shot_elevate));
    out.put(double(w.explosion_damage));
    out.put(double(w.explosion_inner));
    out.put(double(w.explosion_outer));
    out.put(double(w.explosion_push));
    out.put(double(w.push_inner));
    out.put(double(w.push_outer));
    out.put(double(w.melee_range));
    out.put(double(w.melee_arc));
    out.put(double(w.melee_lunge));
    out.put(double(w.melee_stagger));
    out.put(double(w.shield_disrupt));
    out.put(double(w.headshot_multiplier));
    out.put(double(w.crit_chance));
    out.put(double(w.crit_multiplier));
    out.put(double(w.shield_factor));
    out.put(double(w.vehicle_factor));
    out.put(double(w.explosion_vehicle_factor));
    out.put(double(w.swap_time));
    out.put(double(w.recoil));
    out.put(double(w.aim_speed));
    for (const double v : {w.body_multiplier, w.flank_multiplier, w.holster_cooling, w.proximity_fuse, w.block_leak,
                           w.block_cost, w.deflect, w.parry_window, w.combo_stagger, w.knockback}) {
        out.put(v);
    }
    out.put(uint8_t(std::clamp(w.ricochets, 0, 255)));
    out.put(uint8_t(std::clamp(w.pierces, 0, 255)));
    out.put(uint8_t(uint8_t(w.holster_reload) | (uint8_t(w.reload_sprinting) << 1) | (uint8_t(w.fire_rolling) << 2) |
                    (uint8_t(w.riposte) << 3) | (uint8_t(w.unblockable) << 4)));
    out.put(uint8_t(w.hit_effect.has_value()));
    if (w.hit_effect) {
        const HitEffect& e = *w.hit_effect;
        out.put_string(e.id);
        out.put_string(e.name);
        out.put(double(e.chance));
        out.put(double(e.duration));
        out.put(double(e.damage_per_second));
        out.put(uint8_t(e.damage_type));
        put_modifiers(out, e.modifiers);
    }
    for (double c : w.bolt_color) out.put(float(c));
    out.put(float(w.bolt_length));
    out.put(float(w.bolt_width));
    out.put_string(w.bolt_texture);
    out.put(float(w.glow_length));
    out.put(float(w.blur_length));
    for (double c : w.light_color) out.put(float(c));
    out.put(float(w.light_radius));
    out.put_string(w.ordnance_model);
    out.put_string(w.trail_effect);
    out.put_string(w.impact_effect);
    out.put_string(w.explosion_effect);
    out.put_string(w.muzzle_flash);
    for (double c : w.flash_color) out.put(float(c));
    out.put(float(w.flash_length));
    for (double c : w.flash_light_color) out.put(float(c));
    out.put(float(w.flash_light_radius));
    out.put(float(w.flash_light_duration));
    out.put_string(w.fire_sound);
    out.put_string(w.fire_empty_sound);
    out.put_string(w.reload_sound);
    out.put_string(w.overheat_sound);
    out.put_string(w.overheat_stop_sound);
    out.put_string(w.weapon_change_sound);
    out.put_string(w.ordnance_sound);
    out.put_string(w.bounce_sound);
    out.put_string(w.explosion_sound);
    put_modifiers(out, w.modifiers);
}

WeaponDef get_weapon_def(ByteReader& in) {
    WeaponDef w;
    w.name = in.get_string();
    w.model = in.get_string();
    w.anim_set = in.get_string();
    w.archetype = in.get_string();
    const uint8_t dt = in.get<uint8_t>();
    if (dt >= damage_type_count) throw PacketError("bad damage type");
    w.damage_type = DamageType(dt);
    const uint8_t proj = in.get<uint8_t>();
    if (proj > uint8_t(Projectile::melee)) throw PacketError("bad projectile");
    w.projectile = Projectile(proj);
    w.ammo = in.get<int16_t>();
    w.shot_delay = in.get<double>();
    w.rounds_per_clip = in.get<int16_t>();
    w.reload_time = in.get<double>();
    w.heat_per_shot = in.get<double>();
    w.heat_recover_rate = in.get<double>();
    w.heat_threshold = in.get<double>();
    w.yaw_spread = in.get<double>();
    w.pitch_spread = in.get<double>();
    w.spread_per_shot = in.get<double>();
    w.spread_recover_rate = in.get<double>();
    w.spread_limit = in.get<double>();
    w.shots_per_salvo = in.get<uint8_t>();
    w.zoom_min = in.get<double>();
    w.zoom_max = in.get<double>();
    w.zoom_first_person = in.get<uint8_t>() != 0;
    w.damage = in.get<double>();
    w.bolt_speed = in.get<double>();
    w.bolt_gravity = in.get<double>();
    w.bolt_life = in.get<double>();
    w.damage_transition_delay = in.get<double>();
    w.damage_transition_period = in.get<double>();
    w.damage_final = in.get<double>();
    w.bolt_acceleration = in.get<double>();
    w.rebound = in.get<double>();
    w.friction = in.get<double>();
    w.explode_on_impact = in.get<uint8_t>() != 0;
    w.fuse = in.get<double>();
    w.timer_on_contact = in.get<uint8_t>() != 0;
    w.trigger_radius = in.get<double>();
    w.arm_time = in.get<double>();
    w.trap_shots = in.get<uint8_t>();
    w.trap_interval = in.get<double>();
    w.shot_elevate = in.get<double>();
    w.explosion_damage = in.get<double>();
    w.explosion_inner = in.get<double>();
    w.explosion_outer = in.get<double>();
    w.explosion_push = in.get<double>();
    w.push_inner = in.get<double>();
    w.push_outer = in.get<double>();
    w.melee_range = in.get<double>();
    w.melee_arc = in.get<double>();
    w.melee_lunge = in.get<double>();
    w.melee_stagger = in.get<double>();
    w.shield_disrupt = in.get<double>();
    w.headshot_multiplier = in.get<double>();
    w.crit_chance = in.get<double>();
    w.crit_multiplier = in.get<double>();
    w.shield_factor = in.get<double>();
    w.vehicle_factor = in.get<double>();
    w.explosion_vehicle_factor = in.get<double>();
    w.swap_time = in.get<double>();
    w.recoil = in.get<double>();
    w.aim_speed = in.get<double>();
    for (double* v : {&w.body_multiplier, &w.flank_multiplier, &w.holster_cooling, &w.proximity_fuse, &w.block_leak,
                      &w.block_cost, &w.deflect, &w.parry_window, &w.combo_stagger, &w.knockback}) {
        *v = in.get<double>();
    }
    w.ricochets = in.get<uint8_t>();
    w.pierces = in.get<uint8_t>();
    {
        const uint8_t flags = in.get<uint8_t>();
        w.holster_reload = (flags & 1) != 0;
        w.reload_sprinting = (flags & 2) != 0;
        w.fire_rolling = (flags & 4) != 0;
        w.riposte = (flags & 8) != 0;
        w.unblockable = (flags & 16) != 0;
    }
    if (in.get<uint8_t>() != 0) {
        HitEffect e;
        e.id = in.get_string();
        e.name = in.get_string();
        e.chance = in.get<double>();
        e.duration = in.get<double>();
        e.damage_per_second = in.get<double>();
        const uint8_t et = in.get<uint8_t>();
        if (et >= damage_type_count) throw PacketError("bad effect damage type");
        e.damage_type = DamageType(et);
        e.modifiers = get_modifiers(in);
        w.hit_effect = std::move(e);
    }
    for (double& c : w.bolt_color) c = in.get<float>();
    w.bolt_length = in.get<float>();
    w.bolt_width = in.get<float>();
    w.bolt_texture = in.get_string();
    w.glow_length = in.get<float>();
    w.blur_length = in.get<float>();
    for (double& c : w.light_color) c = in.get<float>();
    w.light_radius = in.get<float>();
    w.ordnance_model = in.get_string();
    w.trail_effect = in.get_string();
    w.impact_effect = in.get_string();
    w.explosion_effect = in.get_string();
    w.muzzle_flash = in.get_string();
    for (double& c : w.flash_color) c = in.get<float>();
    w.flash_length = in.get<float>();
    for (double& c : w.flash_light_color) c = in.get<float>();
    w.flash_light_radius = in.get<float>();
    w.flash_light_duration = in.get<float>();
    w.fire_sound = in.get_string();
    w.fire_empty_sound = in.get_string();
    w.reload_sound = in.get_string();
    w.overheat_sound = in.get_string();
    w.overheat_stop_sound = in.get_string();
    w.weapon_change_sound = in.get_string();
    w.ordnance_sound = in.get_string();
    w.bounce_sound = in.get_string();
    w.explosion_sound = in.get_string();
    w.modifiers = get_modifiers(in);
    return w;
}

void put_char_loadout(ByteWriter& out, const CharacterLoadout& lo) {
    out.put(uint8_t(lo.slot_index));
    out.put_string(lo.name);
    out.put(lo.primary_id);
    out.put(lo.sidearm_id);
    out.put(lo.secondary_id);
    out.put(uint8_t(std::min<size_t>(lo.gear_ids.size(), 32)));
    for (const auto& [k, v] : lo.gear_ids) {
        out.put_string(k);
        out.put(v);
    }
    out.put(uint8_t(std::min<size_t>(lo.appearance_ids.size(), 32)));
    for (const auto& [k, v] : lo.appearance_ids) {
        out.put_string(k);
        out.put(v);
    }
    out.put_string(lo.companion);
}

CharacterLoadout get_char_loadout(ByteReader& in) {
    CharacterLoadout lo;
    lo.slot_index = in.get<uint8_t>();
    if (lo.slot_index >= max_character_loadouts) {
        throw PacketError("bad loadout slot");
    }
    lo.name = in.get_string();
    lo.primary_id = in.get<uint64_t>();
    lo.sidearm_id = in.get<uint64_t>();
    lo.secondary_id = in.get<uint64_t>();
    const size_t gn = in.get<uint8_t>();
    for (size_t i = 0; i < gn; ++i) {
        const std::string k = in.get_string();
        lo.gear_ids[k] = in.get<uint64_t>();
    }
    const size_t an = in.get<uint8_t>();
    for (size_t i = 0; i < an; ++i) {
        const std::string k = in.get_string();
        lo.appearance_ids[k] = in.get<uint64_t>();
    }
    lo.companion = in.get_string();
    return lo;
}

// Stage 12 (specialist.h).
void put_companion(ByteWriter& out, const CompanionRecord& r) {
    out.put(r.id);
    out.put_string(r.template_id);
    out.put_string(r.name);
    out.put_string(r.origin);
    out.put(float(r.quality));
    out.put(float(r.health));
    out.put(float(r.damage));
    out.put(float(r.speed));
    out.put(r.colors);
    out.put(uint8_t(std::min<size_t>(r.modules.size(), 8)));
    for (size_t i = 0; i < r.modules.size() && i < 8; ++i) out.put_string(r.modules[i]);
    out.put(uint8_t(r.mount));
}

CompanionRecord get_companion(ByteReader& in) {
    CompanionRecord r;
    r.id = in.get<uint64_t>();
    r.template_id = in.get_string();
    r.name = in.get_string();
    r.origin = in.get_string();
    r.quality = in.get<float>();
    r.health = in.get<float>();
    r.damage = in.get<float>();
    r.speed = in.get<float>();
    r.colors = in.get<uint16_t>();
    const size_t n = in.get<uint8_t>();
    if (n > 8) throw PacketError("too many droid modules");
    for (size_t i = 0; i < n; ++i) r.modules.push_back(in.get_string());
    r.mount = in.get<uint8_t>() != 0;
    return r;
}

void put_contract(ByteWriter& out, const Contract& c) {
    out.put(c.id);
    out.put(uint8_t(c.kind));
    out.put_string(c.title);
    out.put_string(c.target_name);
    out.put(c.target_entity);
    put_vec3(out, c.waypoint);
    out.put(float(c.radius));
    out.put(c.reward);
    out.put(c.xp);
    out.put(float(c.expires));
    out.put(float(c.progress));
    out.put(float(c.needed));
    out.put_string(c.profession);
}

Contract get_contract(ByteReader& in) {
    Contract c;
    c.id = in.get<uint64_t>();
    const uint8_t kind = in.get<uint8_t>();
    if (kind > uint8_t(ContractKind::gig)) throw PacketError("bad contract kind");
    c.kind = ContractKind(kind);
    c.title = in.get_string();
    c.target_name = in.get_string();
    c.target_entity = in.get<uint32_t>();
    c.waypoint = get_vec3(in);
    c.radius = in.get<float>();
    c.reward = in.get<int64_t>();
    c.xp = in.get<int64_t>();
    c.expires = in.get<float>();
    c.progress = in.get<float>();
    c.needed = in.get<float>();
    c.profession = in.get_string();
    return c;
}

void put_listing(ByteWriter& out, const BazaarListing& l) {
    out.put(l.id);
    out.put(l.seller);
    out.put_string(l.seller_name);
    out.put_string(l.shop);
    out.put(l.item.id);
    out.put_string(l.item.def_id);
    out.put(uint8_t(l.item.grade));
    out.put(uint8_t(clamp_mark(l.item.mark)));
    out.put(uint16_t(std::clamp(l.item.quantity, 1, 65535)));
    out.put_string(l.item.custom_name);
    out.put_long_string(l.item.color_json);
    out.put(l.price);
    out.put(l.listed_at);
}

BazaarListing get_listing(ByteReader& in) {
    BazaarListing l;
    l.id = in.get<uint64_t>();
    l.seller = in.get<uint64_t>();
    l.seller_name = in.get_string();
    l.shop = in.get_string();
    l.item.id = in.get<uint64_t>();
    l.item.def_id = in.get_string();
    l.item.grade = clamp_grade(in.get<uint8_t>());
    l.item.mark = clamp_mark(in.get<uint8_t>());
    l.item.quantity = std::max<int>(1, in.get<uint16_t>());
    l.item.custom_name = in.get_string();
    l.item.color_json = in.get_long_string();
    l.price = in.get<int64_t>();
    l.listed_at = in.get<int64_t>();
    return l;
}

void put_field(ByteWriter& out, const FieldState& f) {
    out.put(uint16_t(std::min<size_t>(f.marks.size(), 512)));
    for (size_t i = 0; i < f.marks.size() && i < 512; ++i) {
        out.put(f.marks[i].entity);
        out.put(f.marks[i].remaining);
        out.put(f.marks[i].kind);
    }
    out.put(uint16_t(std::min<size_t>(f.cloaked.size(), 512)));
    for (size_t i = 0; i < f.cloaked.size() && i < 512; ++i) out.put(f.cloaked[i]);
    out.put(uint16_t(std::min<size_t>(f.performers.size(), 512)));
    for (size_t i = 0; i < f.performers.size() && i < 512; ++i) out.put(f.performers[i]);
    out.put(uint16_t(std::min<size_t>(f.companions.size(), 512)));
    for (size_t i = 0; i < f.companions.size() && i < 512; ++i) {
        out.put(f.companions[i].first);
        out.put(f.companions[i].second);
    }
    out.put(uint16_t(std::min<size_t>(f.deployables.size(), 512)));
    for (size_t i = 0; i < f.deployables.size() && i < 512; ++i) {
        const FieldDeployable& d = f.deployables[i];
        out.put(d.id);
        out.put(d.kind);
        out.put(d.owner);
        out.put(uint8_t(d.friendly));
        put_vec3(out, d.position);
        out.put(d.radius);
        out.put(d.remaining);
    }
    out.put(uint16_t(std::min<size_t>(f.hazards.size(), 512)));
    for (size_t i = 0; i < f.hazards.size() && i < 512; ++i) {
        const FieldHazard& h = f.hazards[i];
        out.put(h.id);
        out.put_string(h.kind);
        put_vec3(out, h.position);
        out.put(h.radius);
        out.put(h.remaining);
    }
    out.put(uint16_t(std::min<size_t>(f.flair.size(), 512)));
    for (size_t i = 0; i < f.flair.size() && i < 512; ++i) {
        out.put(f.flair[i].first);
        out.put(f.flair[i].second);
    }
}

FieldState get_field(ByteReader& in) {
    FieldState f;
    const auto count = [&](const char* what) {
        const size_t n = in.get<uint16_t>();
        if (n > 512) throw PacketError(std::string("too many field ") + what);
        return n;
    };
    for (size_t i = 0, n = count("marks"); i < n; ++i) {
        FieldMark m;
        m.entity = in.get<uint32_t>();
        m.remaining = in.get<float>();
        m.kind = in.get<uint8_t>();
        f.marks.push_back(m);
    }
    for (size_t i = 0, n = count("cloaks"); i < n; ++i) f.cloaked.push_back(in.get<uint32_t>());
    for (size_t i = 0, n = count("performers"); i < n; ++i) f.performers.push_back(in.get<uint32_t>());
    for (size_t i = 0, n = count("companions"); i < n; ++i) {
        const uint32_t entity = in.get<uint32_t>();
        f.companions.emplace_back(entity, in.get<uint32_t>());
    }
    for (size_t i = 0, n = count("deployables"); i < n; ++i) {
        FieldDeployable d;
        d.id = in.get<uint32_t>();
        d.kind = in.get<uint8_t>();
        d.owner = in.get<uint32_t>();
        d.friendly = in.get<uint8_t>() != 0;
        d.position = get_vec3(in);
        d.radius = in.get<float>();
        d.remaining = in.get<float>();
        f.deployables.push_back(d);
    }
    for (size_t i = 0, n = count("hazards"); i < n; ++i) {
        FieldHazard h;
        h.id = in.get<uint32_t>();
        h.kind = in.get_string();
        h.position = get_vec3(in);
        h.radius = in.get<float>();
        h.remaining = in.get<float>();
        f.hazards.push_back(std::move(h));
    }
    for (size_t i = 0, n = count("flair"); i < n; ++i) {
        const uint32_t entity = in.get<uint32_t>();
        f.flair.emplace_back(entity, in.get<uint8_t>());
    }
    return f;
}

struct Encoder {
    ByteWriter& out;

    void operator()(const Hello& m) {
        out.put(uint8_t(Type::hello));
        out.put_string(m.name);
        out.put(m.ticket);
        out.put(m.team);
    }
    void operator()(const Welcome& m) {
        out.put(uint8_t(Type::welcome));
        out.put(m.entity_id);
        out.put(m.tick_rate);
        out.put(m.server_tick);
        out.put(m.character_id);
        out.put(uint8_t(m.recruiters.size()));
        for (const Recruiter& r : m.recruiters) {
            out.put_string(r.name);
            put_vec3(out, r.position);
            out.put(r.radius);
            out.put(uint8_t(r.kind));
        }
        out.put(uint8_t(std::min<size_t>(m.factions.size(), 16)));
        for (size_t i = 0; i < m.factions.size() && i < 16; ++i) {
            out.put_string(m.factions[i]);
        }
        out.put(uint8_t(std::min<size_t>(m.looks.size(), 255)));
        for (size_t i = 0; i < m.looks.size() && i < 255; ++i) {
            out.put_string(m.looks[i]);
        }
        out.put(uint8_t(std::min<size_t>(m.quartermasters.size(), 255)));
        for (size_t i = 0; i < m.quartermasters.size() && i < 255; ++i) {
            const Quartermaster& q = m.quartermasters[i];
            out.put_string(q.name);
            out.put_string(q.faction);
            put_vec3(out, q.position);
            out.put(q.radius);
        }
        // (Two bytes: a planet's towns have more than 255 trainers, shops and terminals.)
        out.put(uint16_t(std::min<size_t>(m.vendors.size(), 65535)));
        for (size_t i = 0; i < m.vendors.size() && i < 65535; ++i) {
            const Vendor& v = m.vendors[i];
            out.put_string(v.name);
            out.put_string(v.kind);
            put_vec3(out, v.position);
            out.put(v.radius);
            out.put_string(v.detail);
        }
        out.put(uint8_t(std::min<size_t>(m.quest_npcs.size(), 255)));
        for (size_t i = 0; i < m.quest_npcs.size() && i < 255; ++i) {
            const QuestNpc& n = m.quest_npcs[i];
            out.put(n.entity);
            out.put_string(n.name);
            put_vec3(out, n.position);
            out.put(uint8_t(n.giver));
        }
        out.put(uint8_t(std::min<size_t>(m.mission_terminals.size(), 255)));
        for (size_t i = 0; i < m.mission_terminals.size() && i < 255; ++i) {
            const MissionTerminal& t = m.mission_terminals[i];
            out.put_string(t.name);
            out.put_string(t.kind);
            put_vec3(out, t.position);
            out.put(t.radius);
        }
        out.put(uint8_t(std::min<size_t>(m.workbenches.size(), 255)));
        for (size_t i = 0; i < m.workbenches.size() && i < 255; ++i) {
            const Workbench& wb = m.workbenches[i];
            out.put_string(wb.name);
            out.put_string(wb.kind);
            put_vec3(out, wb.position);
            out.put(wb.radius);
        }
    }
    void operator()(const Refused& m) {
        out.put(uint8_t(Type::refused));
        out.put_string(m.reason);
    }
    void operator()(const InputBatch& m) {
        out.put(uint8_t(Type::input_batch));
        const size_t count = std::min(m.inputs.size(), max_batched_inputs);
        out.put(uint8_t(count));
        for (size_t i = m.inputs.size() - count; i < m.inputs.size(); ++i) {
            put_input(out, m.inputs[i]);
        }
    }
    void operator()(const Snapshot& m) {
        out.put(uint8_t(Type::snapshot));
        out.put(m.server_tick);
        out.put(m.last_input);
        out.put(uint16_t(m.entities.size()));
        for (const EntityState& entity : m.entities) {
            out.put(entity.id);
            out.put(uint8_t(entity.full_state));
            if (entity.full_state) {
                put_state(out, entity.state);
            } else {
                put_remote_state(out, entity.state);
            }
            out.put(entity.health);
            out.put(entity.max_health);
            out.put(entity.shield);
            out.put(entity.max_shield);
            out.put(uint8_t(entity.life));
            out.put(entity.spawn_count);
            out.put(entity.team);
            out.put(entity.loadout);
            out.put(entity.kills);
            out.put(entity.deaths);
            out.put(entity.captures);
            out.put(entity.look);
            out.put(entity.pose);
            out.put(entity.actor_kind);
            out.put(uint8_t(uint8_t(entity.passive) | (uint8_t(entity.quiet) << 1)));
            out.put(entity.colors);
            out.put(entity.species);
            out.put(entity.gender);
            out.put(entity.static_key);
            out.put(uint8_t(entity.has_static));
            if (entity.has_static) {
                out.put_string(entity.name);
                out.put_long_string(entity.appearance);
                out.put_string(entity.weapon_model);
                out.put_string(entity.anim_set);
                out.put_long_string(entity.worn);
            }
        }
        out.put(uint16_t(m.bolts.size()));
        for (const BoltState& bolt : m.bolts) {
            out.put(bolt.id);
            out.put(bolt.owner);
            out.put(bolt.weapon);
            out.put(uint8_t(bolt.kind));
            put_vec3(out, bolt.position);
            put_vec3(out, bolt.velocity);
        }
        out.put(uint16_t(m.events.size()));
        for (const CombatEvent& event : m.events) {
            out.put(uint8_t(event.kind));
            out.put(event.attacker);
            out.put(event.victim);
            out.put(event.damage);
        }
        out.put(uint16_t(m.explosions.size()));
        for (const Explosion& e : m.explosions) {
            out.put(e.owner);
            out.put(e.weapon);
            put_vec3(out, e.position);
            put_vec3(out, e.normal);
            out.put(e.radius);
        }
        out.put(uint16_t(m.impacts.size()));
        for (const Impact& imp : m.impacts) {
            out.put(imp.owner);
            out.put(imp.weapon);
            put_vec3(out, imp.position);
            put_vec3(out, imp.normal);
            out.put(uint8_t(imp.hit_character));
        }
        out.put(uint16_t(m.pickups.size()));
        for (const PickupState& p : m.pickups) {
            out.put(p.id);
            out.put(uint8_t(p.kind));
            put_vec3(out, p.position);
            out.put(p.time_left);
        }
        out.put(uint16_t(m.targets.size()));
        for (const TargetState& t : m.targets) {
            out.put(t.id);
            out.put_string(t.name);
            put_vec3(out, t.position);
            out.put(t.health);
            out.put(t.max_health);
            out.put(uint8_t(t.alive));
        }
        out.put(uint16_t(m.vehicles.size()));
        for (const VehicleState& v : m.vehicles) {
            out.put(v.id);
            out.put(v.vehicle);
            put_vec3(out, v.position);
            put_vec3(out, v.velocity);
            out.put(v.yaw);
            out.put(v.pitch);
            out.put(v.bank);
            out.put(v.health);
            out.put(v.max_health);
            out.put(v.driver);
            out.put(v.team);
            out.put(uint8_t(v.alive));
            out.put(v.flags);
            out.put(uint8_t(std::min<size_t>(v.gunners.size(), 8)));
            for (size_t g = 0; g < v.gunners.size() && g < 8; ++g) {
                out.put(v.gunners[g]);
            }
            out.put(v.owner);
            out.put(v.towing);
            out.put(v.tow_wraps);
        }
        const uint8_t ccount = uint8_t(std::min<size_t>(m.corpses.size(), 255));
        out.put(ccount);
        for (size_t ci = 0; ci < ccount; ++ci) {
            const CorpseLootState& c = m.corpses[ci];
            out.put(c.entity_id);
            out.put_string(c.name);
            put_vec3(out, c.position);
            const uint8_t icount = uint8_t(std::min<size_t>(c.items.size(), 255));
            out.put(icount);
            for (size_t ii = 0; ii < icount; ++ii) {
                out.put(c.items[ii].slot);
                out.put_string(c.items[ii].def_id);
                out.put(uint8_t(c.items[ii].grade));
                out.put(c.items[ii].mark);
            }
        }
        out.put(uint8_t(m.match.has_value()));
        if (m.match) {
            put_match(out, *m.match);
        }
        out.put(uint8_t(m.you.queue));
        out.put(m.you.team);
        out.put(m.you.in_queue);
        out.put(m.you.starts_in);
        out.put_string(m.you.battlefield);
        out.put(m.you.notice_id);
        out.put_string(m.you.notice);
        out.put(m.you.credits);
        out.put(m.you.faction_points);
        out.put(m.you.experience);
        out.put(m.you.active_loadout);
        out.put(m.you.switching_loadout);
        out.put(m.you.switch_progress);
        out.put(m.you.combat_level);
        out.put(m.you.extract_progress);
        out.put(m.you.inventory_rev);
        put_field(out, m.field);
    }
    void operator()(const Goodbye&) { out.put(uint8_t(Type::goodbye)); }
    void operator()(const SpawnRequest& m) {
        out.put(uint8_t(Type::spawn_request));
        out.put(m.loadout);
        out.put(m.post);
    }
    void operator()(const QueueRequest& m) {
        out.put(uint8_t(Type::queue_request));
        out.put(m.team);
        out.put(m.mode);
    }
    void operator()(const Transfer& m) {
        out.put(uint8_t(Type::transfer));
        out.put(m.address);
        out.put(m.port);
        out.put(m.ticket);
        out.put_string(m.battlefield);
        out.put_string(m.planet);
    }
    void operator()(const InventorySync& m) {
        out.put(uint8_t(Type::inventory_sync));
        out.put(m.revision);
        out.put(uint16_t(m.items.size()));
        for (const ItemInstance& inst : m.items) {
            out.put(inst.id);
            out.put(inst.owner_id);
            out.put_string(inst.def_id);
            out.put(uint8_t(inst.grade));
            out.put(uint8_t(clamp_mark(inst.mark)));
            out.put(uint16_t(std::clamp(inst.quantity, 1, 65535)));
            out.put_string(inst.custom_name);
            out.put_string(inst.color_json);
        }
        out.put(uint8_t(std::min<size_t>(m.loadouts.size(), max_character_loadouts)));
        for (size_t i = 0; i < m.loadouts.size() && i < size_t(max_character_loadouts); ++i) {
            put_char_loadout(out, m.loadouts[i]);
        }
        out.put(uint8_t(std::min<size_t>(m.resolved_loadouts.size(), max_character_loadouts)));
        for (size_t i = 0; i < m.resolved_loadouts.size() && i < size_t(max_character_loadouts); ++i) {
            const Loadout& lo = m.resolved_loadouts[i];
            out.put_string(lo.name);
            for (int s = 0; s < weapon_slots; ++s) {
                const bool has_w = lo.weapons[size_t(s)].has_value();
                out.put(uint8_t(has_w));
                if (has_w) {
                    put_weapon_def(out, *lo.weapons[size_t(s)]);
                }
            }
            out.put(uint8_t(std::min<size_t>(lo.gear.size(), 32)));
            for (size_t gi = 0; gi < lo.gear.size() && gi < 32; ++gi) {
                out.put_string(lo.gear[gi].name);
                out.put_string(lo.gear[gi].slot);
                put_modifiers(out, lo.gear[gi].modifiers);
            }
            out.put_string(lo.armor_class);
            put_triggered(out, lo.triggered);
        }
            out.put(m.bank_credits);
        out.put(uint16_t(std::min<size_t>(m.bank_items.size(), 512)));
        for (size_t i = 0; i < m.bank_items.size() && i < 512; ++i) {
            const ItemInstance& inst = m.bank_items[i];
            out.put(inst.id);
            out.put_string(inst.def_id);
            out.put(uint8_t(inst.grade));
            out.put(uint8_t(clamp_mark(inst.mark)));
            out.put(uint16_t(std::clamp(inst.quantity, 1, 65535)));
            out.put_string(inst.custom_name);
            out.put_string(inst.color_json);
        }
        out.put(uint8_t(std::min<size_t>(m.reputation.size(), 32)));
        for (const auto& [faction, rep] : m.reputation) {
            out.put_string(faction);
            out.put(rep);
        }
    }

    void operator()(const InventoryAck& m) {
        out.put(uint8_t(Type::inventory_ack));
        out.put(m.revision);
    }
    void operator()(const SaveLoadoutRequest& m) {
        out.put(uint8_t(Type::save_loadout));
        put_char_loadout(out, m.loadout);
    }
    void operator()(const SwitchLoadoutRequest& m) {
        out.put(uint8_t(Type::switch_loadout));
        out.put(m.slot);
    }
    void operator()(const ShopBuyRequest& m) {
        out.put(uint8_t(Type::shop_buy));
        out.put_string(m.def_id);
        out.put(uint8_t(m.use_faction_points));
    }
    void operator()(const DestroyItemRequest& m) {
        out.put(uint8_t(Type::destroy_item));
        out.put(m.item_id);
    }
    void operator()(const LootTakeRequest& m) {
        out.put(uint8_t(Type::loot_take));
        out.put(m.corpse_entity);
        out.put(m.slot);
    }
    void operator()(const VendorSellRequest& m) {
        out.put(uint8_t(Type::vendor_sell));
        out.put(m.item_id);
        out.put(uint8_t(m.sell_all_junk));
    }
    void operator()(const UseItemRequest& m) {
        out.put(uint8_t(Type::use_item));
        out.put(m.item_id);
    }
    void operator()(const QuestSync& m) {
        out.put(uint8_t(Type::quest_sync));
        out.put(m.revision);
        out.put(uint8_t(std::min<size_t>(m.active.size(), 64)));
        for (size_t i = 0; i < m.active.size() && i < 64; ++i) {
            put_quest(out, m.active[i]);
        }
        out.put(uint16_t(std::min<size_t>(m.completed.size(), 1024)));
        for (size_t i = 0; i < m.completed.size() && i < 1024; ++i) {
            out.put_string(m.completed[i]);
        }
        out.put(uint8_t(std::min<size_t>(m.offers.size(), 64)));
        for (size_t i = 0; i < m.offers.size() && i < 64; ++i) {
            put_quest(out, m.offers[i]);
        }
        out.put_string(m.offer_source);
        out.put(m.offer_serial);
        out.put_string(m.offer_note);
        out.put(uint8_t(std::min<size_t>(m.ready_givers.size(), 255)));
        for (size_t i = 0; i < m.ready_givers.size() && i < 255; ++i) {
            out.put_string(m.ready_givers[i]);
        }
        out.put(uint8_t(std::min<size_t>(m.waiting_givers.size(), 255)));
        for (size_t i = 0; i < m.waiting_givers.size() && i < 255; ++i) {
            out.put_string(m.waiting_givers[i]);
        }
    }
    void operator()(const QuestAck& m) {
        out.put(uint8_t(Type::quest_ack));
        out.put(m.revision);
    }
    void operator()(const QuestRequest& m) {
        out.put(uint8_t(Type::quest_request));
        out.put(uint8_t(m.action));
        out.put(m.target);
        out.put_string(m.quest_id);
    }
    void operator()(const ProfessionSync& m) {
        out.put(uint8_t(Type::profession_sync));
        out.put(m.revision);
        out.put(uint8_t(std::min<size_t>(m.professions.size(), 255)));
        for (size_t i = 0; i < m.professions.size() && i < 255; ++i) {
            const ProfessionProgress& p = m.professions[i];
            out.put_string(p.id);
            out.put(p.xp);
            out.put(uint8_t(p.trained));
            out.put(uint8_t(p.paid));
            for (const std::vector<std::string>* list : {&p.perks, &p.paid_perks}) {
                out.put(uint8_t(std::min<size_t>(list->size(), 16)));
                for (size_t k = 0; k < list->size() && k < 16; ++k) {
                    out.put_string((*list)[k]);
                }
            }
        }
    }
    void operator()(const ProfessionAck& m) {
        out.put(uint8_t(Type::profession_ack));
        out.put(m.revision);
    }
    void operator()(const CraftingSync& m) {
        out.put(uint8_t(Type::crafting_sync));
        out.put(m.revision);
        out.put(uint16_t(std::min<size_t>(m.materials.size(), 65535)));
        for (size_t i = 0; i < m.materials.size() && i < 65535; ++i) {
            out.put_string(m.materials[i].resource_id);
            out.put(m.materials[i].amount);
        }
        out.put(uint16_t(std::min<size_t>(m.presets.size(), 65535)));
        for (size_t i = 0; i < m.presets.size() && i < 65535; ++i) {
            const RecipePreset& p = m.presets[i];
            out.put_string(p.name);
            out.put_string(p.schematic_id);
            out.put(uint8_t(std::min<size_t>(p.resource_ids.size(), 16)));
            for (size_t j = 0; j < p.resource_ids.size() && j < 16; ++j) {
                out.put_string(p.resource_ids[j]);
            }
            out.put(uint8_t(std::min<size_t>(p.enhancement_defs.size(), 8)));
            for (size_t j = 0; j < p.enhancement_defs.size() && j < 8; ++j) {
                out.put_string(p.enhancement_defs[j]);
            }
            out.put(p.exp_quality_pct);
        }
        out.put(uint16_t(std::min<size_t>(m.blueprints.size(), 65535)));
        for (size_t i = 0; i < m.blueprints.size() && i < 65535; ++i) {
            out.put_string(m.blueprints[i]);
        }
    }
    void operator()(const CraftingAck& m) {
        out.put(uint8_t(Type::crafting_ack));
        out.put(m.revision);
    }
    void operator()(const GatherRequest& m) {
        out.put(uint8_t(Type::gather_request));
        out.put(uint8_t(m.action));
        out.put(m.category);
        out.put(m.target_id);
    }
    void operator()(const ServiceRequest& m) {
        out.put(uint8_t(Type::service_request));
        out.put(m.action);
        out.put_string(m.id);
        out.put(m.amount);
        out.put(m.item_id);
    }
    void operator()(const CraftRequest& m) {
        out.put(uint8_t(Type::craft_request));
        out.put(uint8_t(m.action));
        out.put_string(m.schematic_id);
        out.put(uint8_t(std::min<size_t>(m.resource_ids.size(), 16)));
        for (size_t i = 0; i < m.resource_ids.size() && i < 16; ++i) {
            out.put_string(m.resource_ids[i]);
        }
        out.put(uint8_t(std::min<size_t>(m.enhancement_item_ids.size(), 8)));
        for (size_t i = 0; i < m.enhancement_item_ids.size() && i < 8; ++i) {
            out.put(m.enhancement_item_ids[i]);
        }
        out.put(uint8_t(std::min<size_t>(m.enhancement_defs.size(), 8)));
        for (size_t i = 0; i < m.enhancement_defs.size() && i < 8; ++i) {
            out.put_string(m.enhancement_defs[i]);
        }
        out.put(m.exp_quality_pct);
        out.put(m.batch_count);
        out.put_string(m.preset_name);
        out.put_string(m.order_id);
        out.put(m.mark);
        out.put(m.item_id);
        out.put_string(m.dye_values);
    }
    void operator()(const SpecialistRequest& m) {
        out.put(uint8_t(Type::specialist_request));
        out.put(m.action);
        out.put_string(m.id);
        out.put(m.target);
        put_vec3(out, m.position);
        out.put(m.amount);
        out.put(m.item_id);
        out.put_long_string(m.text);
    }
    void operator()(const SpecialistSync& m) {
        out.put(uint8_t(Type::specialist_sync));
        out.put(m.revision);
        out.put(uint8_t(std::min<size_t>(m.abilities.size(), 64)));
        for (size_t i = 0; i < m.abilities.size() && i < 64; ++i) {
            out.put_string(m.abilities[i].id);
            out.put(m.abilities[i].cooldown);
        }
        out.put(uint8_t(std::min<size_t>(m.effects.size(), 64)));
        for (size_t i = 0; i < m.effects.size() && i < 64; ++i) {
            out.put_string(m.effects[i].id);
            out.put_string(m.effects[i].name);
            out.put(m.effects[i].remaining);
            out.put(m.effects[i].duration);
            out.put(m.effects[i].flags);
        }
        out.put(uint8_t(std::min<size_t>(m.companions.size(), 64)));
        for (size_t i = 0; i < m.companions.size() && i < 64; ++i) put_companion(out, m.companions[i]);
        out.put(m.active_companion);
        out.put(m.companion_entity);
        out.put(m.companion_cooldown);
        out.put(m.companion_mode);
        out.put(m.companion_slot);
        out.put(uint8_t(std::min<size_t>(m.samples.size(), 64)));
        for (size_t i = 0; i < m.samples.size() && i < 64; ++i) {
            out.put(m.samples[i].id);
            out.put_string(m.samples[i].template_id);
            out.put(float(m.samples[i].quality));
        }
        out.put(uint8_t(std::min<size_t>(m.contracts.size(), 16)));
        for (size_t i = 0; i < m.contracts.size() && i < 16; ++i) put_contract(out, m.contracts[i]);
        out.put(uint8_t(std::min<size_t>(m.offers.size(), 16)));
        for (size_t i = 0; i < m.offers.size() && i < 16; ++i) put_contract(out, m.offers[i]);
        out.put_string(m.offer_source);
        out.put(uint8_t(std::min<size_t>(m.bazaar.size(), 40)));
        for (size_t i = 0; i < m.bazaar.size() && i < 40; ++i) put_listing(out, m.bazaar[i]);
        out.put(uint8_t(std::min<size_t>(m.my_listings.size(), 40)));
        for (size_t i = 0; i < m.my_listings.size() && i < 40; ++i) put_listing(out, m.my_listings[i]);
        out.put(uint8_t(std::min<size_t>(m.prices.size(), 40)));
        for (size_t i = 0; i < m.prices.size() && i < 40; ++i) {
            out.put_string(m.prices[i].first);
            const PriceStats& p = m.prices[i].second;
            out.put(int32_t(p.count));
            out.put(p.low);
            out.put(p.high);
            out.put(p.average);
            out.put(p.last);
        }
        out.put(uint8_t(m.performing));
        out.put(uint8_t(m.bounty_flag));
        out.put_string(m.channel);
        out.put(m.channel_remaining);
    }
    void operator()(const SpecialistAck& m) {
        out.put(uint8_t(Type::specialist_ack));
        out.put(m.revision);
    }
};

} // namespace

std::vector<uint8_t> encode(const Message& message) {
    ByteWriter out;
    out.put(protocol_magic);
    out.put(protocol_version);
    std::visit(Encoder{out}, message);
    return out.data();
}

Message decode(std::span<const uint8_t> packet) {
    ByteReader in(packet);
    if (in.get<uint16_t>() != protocol_magic) {
        throw PacketError("not a game packet");
    }
    if (const uint16_t version = in.get<uint16_t>(); version != protocol_version) {
        throw PacketError(std::format("protocol version {} (expected {})", version, protocol_version));
    }
    Message message;
    switch (Type(in.get<uint8_t>())) {
    case Type::hello: {
        Hello m;
        m.name = in.get_string();
        m.ticket = in.get<uint64_t>();
        m.team = in.get<int8_t>();
        message = m;
        break;
    }
    case Type::welcome: {
        Welcome m;
        m.entity_id = in.get<uint32_t>();
        m.tick_rate = in.get<double>();
        m.server_tick = in.get<uint32_t>();
        m.character_id = in.get<uint64_t>();
        if (!(m.tick_rate >= 1 && m.tick_rate <= 240)) {
            throw PacketError("bad tick rate");
        }
        const size_t recruiters = in.get<uint8_t>();
        for (size_t i = 0; i < recruiters; ++i) {
            Recruiter r;
            r.name = in.get_string();
            r.position = get_vec3(in);
            r.radius = in.get<double>();
            const uint8_t kind = in.get<uint8_t>();
            if (kind > Recruiter::both) {
                throw PacketError("bad terminal kind");
            }
            r.kind = Recruiter::Kind(kind);
            m.recruiters.push_back(r);
        }
        const size_t fcount = in.get<uint8_t>();
        if (fcount > 16) throw PacketError("too many factions");
        for (size_t i = 0; i < fcount; ++i) {
            m.factions.push_back(in.get_string());
        }
        const size_t looks = in.get<uint8_t>();
        for (size_t i = 0; i < looks; ++i) {
            m.looks.push_back(in.get_string());
        }
        const size_t qms = in.get<uint8_t>();
        for (size_t i = 0; i < qms; ++i) {
            Quartermaster q;
            q.name = in.get_string();
            q.faction = in.get_string();
            q.position = get_vec3(in);
            q.radius = in.get<double>();
            m.quartermasters.push_back(std::move(q));
        }
        const size_t vs = in.get<uint16_t>();
        for (size_t i = 0; i < vs; ++i) {
            Vendor v;
            v.name = in.get_string();
            v.kind = in.get_string();
            v.position = get_vec3(in);
            v.radius = in.get<double>();
            v.detail = in.get_string();
            m.vendors.push_back(std::move(v));
        }
        const size_t npcs = in.get<uint8_t>();
        for (size_t i = 0; i < npcs; ++i) {
            QuestNpc n;
            n.entity = in.get<uint32_t>();
            n.name = in.get_string();
            n.position = get_vec3(in);
            n.giver = in.get<uint8_t>() != 0;
            m.quest_npcs.push_back(std::move(n));
        }
        const size_t terminals = in.get<uint8_t>();
        for (size_t i = 0; i < terminals; ++i) {
            MissionTerminal t;
            t.name = in.get_string();
            t.kind = in.get_string();
            t.position = get_vec3(in);
            t.radius = in.get<double>();
            m.mission_terminals.push_back(std::move(t));
        }
        const size_t workbenches = in.get<uint8_t>();
        for (size_t i = 0; i < workbenches; ++i) {
            Workbench wb;
            wb.name = in.get_string();
            wb.kind = in.get_string();
            wb.position = get_vec3(in);
            wb.radius = in.get<double>();
            m.workbenches.push_back(std::move(wb));
        }
        message = m;
        break;
    }
    case Type::input_batch: {
        InputBatch m;
        const size_t count = in.get<uint8_t>();
        if (count > max_batched_inputs) {
            throw PacketError("too many inputs");
        }
        for (size_t i = 0; i < count; ++i) {
            m.inputs.push_back(get_input(in));
        }
        message = std::move(m);
        break;
    }
    case Type::snapshot: {
        Snapshot m;
        m.server_tick = in.get<uint32_t>();
        m.last_input = in.get<uint32_t>();
        const size_t count = in.get<uint16_t>();
        for (size_t i = 0; i < count; ++i) {
            EntityState entity;
            entity.id = in.get<uint32_t>();
            entity.full_state = in.get<uint8_t>() != 0;
            entity.state = entity.full_state ? get_state(in) : get_remote_state(in);
            entity.health = in.get<float>();
            entity.max_health = in.get<float>();
            entity.shield = in.get<float>();
            entity.max_shield = in.get<float>();
            const uint8_t life = in.get<uint8_t>();
            if (life > uint8_t(Life::dead)) {
                throw PacketError("bad life state");
            }
            entity.life = Life(life);
            entity.spawn_count = in.get<uint8_t>();
            entity.team = get_character_team(in);
            entity.loadout = in.get<uint8_t>();
            entity.kills = in.get<uint16_t>();
            entity.deaths = in.get<uint16_t>();
            entity.captures = in.get<uint16_t>();
            entity.look = in.get<uint8_t>();
            entity.pose = in.get<uint8_t>();
            entity.actor_kind = in.get<uint8_t>();
            {
                const uint8_t flags = in.get<uint8_t>();
                entity.passive = (flags & 1) != 0;
                entity.quiet = (flags & 2) != 0;
            }
            entity.colors = in.get<uint16_t>();
            entity.species = in.get<uint8_t>();
            if (entity.species >= species_count) {
                throw PacketError("bad species");
            }
            entity.gender = in.get<uint8_t>();
            if (entity.gender > 1) {
                throw PacketError("bad gender");
            }
            entity.static_key = in.get<uint32_t>();
            entity.has_static = in.get<uint8_t>() != 0;
            if (entity.has_static) {
                entity.name = in.get_string();
                entity.appearance = in.get_long_string();
                entity.weapon_model = in.get_string();
                entity.anim_set = in.get_string();
                entity.worn = in.get_long_string();
            }
            m.entities.push_back(std::move(entity));
        }
        const size_t bolts = in.get<uint16_t>();
        for (size_t i = 0; i < bolts; ++i) {
            BoltState bolt;
            bolt.id = in.get<uint32_t>();
            bolt.owner = in.get<uint32_t>();
            bolt.weapon = in.get<uint8_t>();
            const uint8_t kind = in.get<uint8_t>();
            if (kind > uint8_t(Projectile::melee)) {
                throw PacketError("bad projectile");
            }
            bolt.kind = Projectile(kind);
            bolt.position = get_vec3(in);
            bolt.velocity = get_vec3(in);
            m.bolts.push_back(bolt);
        }
        const size_t events = in.get<uint16_t>();
        for (size_t i = 0; i < events; ++i) {
            CombatEvent event;
            const uint8_t kind = in.get<uint8_t>();
            if (kind < CombatEvent::hit || kind > CombatEvent::perk) {
                throw PacketError("unknown combat event");
            }
            event.kind = CombatEvent::Kind(kind);
            event.attacker = in.get<uint32_t>();
            event.victim = in.get<uint32_t>();
            event.damage = in.get<float>();
            m.events.push_back(event);
        }
        const size_t explosions = in.get<uint16_t>();
        for (size_t i = 0; i < explosions; ++i) {
            Explosion e;
            e.owner = in.get<uint32_t>();
            e.weapon = in.get<uint8_t>();
            e.position = get_vec3(in);
            e.normal = get_vec3(in);
            e.radius = in.get<float>();
            m.explosions.push_back(e);
        }
        const size_t impacts = in.get<uint16_t>();
        for (size_t i = 0; i < impacts; ++i) {
            Impact imp;
            imp.owner = in.get<uint32_t>();
            imp.weapon = in.get<uint8_t>();
            imp.position = get_vec3(in);
            imp.normal = get_vec3(in);
            imp.hit_character = in.get<uint8_t>() != 0;
            m.impacts.push_back(imp);
        }
        const size_t pickups = in.get<uint16_t>();
        for (size_t i = 0; i < pickups; ++i) {
            PickupState p;
            p.id = in.get<uint32_t>();
            const uint8_t kind = in.get<uint8_t>();
            if (kind >= pickup_kind_count) {
                throw PacketError("bad pickup kind");
            }
            p.kind = PickupKind(kind);
            p.position = get_vec3(in);
            p.time_left = in.get<float>();
            m.pickups.push_back(p);
        }
        const size_t targets = in.get<uint16_t>();
        for (size_t i = 0; i < targets; ++i) {
            TargetState t;
            t.id = in.get<uint32_t>();
            t.name = in.get_string();
            t.position = get_vec3(in);
            t.health = in.get<float>();
            t.max_health = in.get<float>();
            t.alive = in.get<uint8_t>() != 0;
            m.targets.push_back(std::move(t));
        }
        const size_t vehicles = in.get<uint16_t>();
        for (size_t i = 0; i < vehicles; ++i) {
            VehicleState v;
            v.id = in.get<uint32_t>();
            v.vehicle = in.get<uint8_t>();
            v.position = get_vec3(in);
            v.velocity = get_vec3(in);
            v.yaw = in.get<float>();
            v.pitch = in.get<float>();
            v.bank = in.get<float>();
            v.health = in.get<float>();
            v.max_health = in.get<float>();
            v.driver = in.get<uint32_t>();
            v.team = in.get<int8_t>();
            v.alive = in.get<uint8_t>() != 0;
            v.flags = in.get<uint8_t>();
            const size_t gunners = in.get<uint8_t>();
            if (gunners > 8) throw PacketError("too many gunners");
            for (size_t g = 0; g < gunners; ++g) {
                v.gunners.push_back(in.get<uint32_t>());
            }
            v.owner = in.get<uint32_t>();
            v.towing = in.get<uint32_t>();
            v.tow_wraps = in.get<float>();
            m.vehicles.push_back(v);
        }
        const size_t corpses = in.get<uint8_t>();
        for (size_t ci = 0; ci < corpses; ++ci) {
            CorpseLootState c;
            c.entity_id = in.get<uint32_t>();
            c.name = in.get_string();
            c.position = get_vec3(in);
            const size_t icount = in.get<uint8_t>();
            for (size_t ii = 0; ii < icount; ++ii) {
                LootItemEntry entry;
                entry.slot = in.get<uint8_t>();
                entry.def_id = in.get_string();
                entry.grade = clamp_grade(in.get<uint8_t>());
                entry.mark = uint8_t(clamp_mark(in.get<uint8_t>()));
                c.items.push_back(std::move(entry));
            }
            m.corpses.push_back(std::move(c));
        }
        if (in.get<uint8_t>() != 0) {
            m.match = get_match(in);
        }
        const uint8_t queue = in.get<uint8_t>();
        if (queue > PlayerStatus::queued) {
            throw PacketError("bad queue state");
        }
        m.you.queue = PlayerStatus::Queue(queue);
        // The side queued for: one of a hub's (up to four: the Separatists are 3).
        m.you.team = in.get<int8_t>();
        if (m.you.team < -1 || m.you.team > 3) {
            throw PacketError("bad queued side");
        }
        m.you.in_queue = in.get<uint16_t>();
        m.you.starts_in = in.get<float>();
        m.you.battlefield = in.get_string();
        m.you.notice_id = in.get<uint16_t>();
        m.you.notice = in.get_string();
        m.you.credits = in.get<int64_t>();
        m.you.faction_points = in.get<int64_t>();
        m.you.experience = in.get<int64_t>();
        m.you.active_loadout = in.get<uint8_t>();
        m.you.switching_loadout = in.get<int8_t>();
        m.you.switch_progress = in.get<float>();
        m.you.combat_level = in.get<uint8_t>();
        m.you.extract_progress = in.get<float>();
        m.you.inventory_rev = in.get<uint32_t>();
        m.field = get_field(in);
        message = std::move(m);
        break;
    }
    case Type::goodbye:
        message = Goodbye{};
        break;
    case Type::refused:
        message = Refused{in.get_string()};
        break;
    case Type::spawn_request: {
        SpawnRequest m;
        m.loadout = in.get<uint8_t>();
        m.post = in.get<uint8_t>();
        message = m;
        break;
    }
    case Type::queue_request: {
        QueueRequest m;
        m.team = in.get<int8_t>();
        if (m.team < -1 || m.team > 3) {
            throw PacketError("bad team");
        }
        m.mode = in.get<uint8_t>();
        if (m.mode >= game_mode_count && m.mode != QueueRequest::any_mode) {
            throw PacketError("bad game mode");
        }
        message = m;
        break;
    }
    case Type::transfer: {
        Transfer m;
        m.address = in.get<uint32_t>();
        m.port = in.get<uint16_t>();
        m.ticket = in.get<uint64_t>();
        m.battlefield = in.get_string();
        m.planet = in.get_string();
        message = m;
        break;
    }
    case Type::inventory_sync: {
        InventorySync m;
        m.revision = in.get<uint32_t>();
        const size_t icount = in.get<uint16_t>();
        m.items.reserve(icount);
        for (size_t i = 0; i < icount; ++i) {
            ItemInstance inst;
            inst.id = in.get<uint64_t>();
            inst.owner_id = in.get<uint64_t>();
            inst.def_id = in.get_string();
            inst.grade = clamp_grade(in.get<uint8_t>());
            inst.mark = clamp_mark(in.get<uint8_t>());
            inst.quantity = std::max<int>(1, in.get<uint16_t>());
            inst.custom_name = in.get_string();
            inst.color_json = in.get_string();
            m.items.push_back(std::move(inst));
        }
        const size_t lcount = in.get<uint8_t>();
        if (lcount > size_t(max_character_loadouts)) throw PacketError("too many loadouts");
        for (size_t i = 0; i < lcount; ++i) {
            m.loadouts.push_back(get_char_loadout(in));
        }
        const size_t rcount = in.get<uint8_t>();
        if (rcount > size_t(max_character_loadouts)) throw PacketError("too many resolved loadouts");
        for (size_t i = 0; i < rcount; ++i) {
            Loadout lo;
            lo.name = in.get_string();
            for (int s = 0; s < weapon_slots; ++s) {
                if (in.get<uint8_t>() != 0) {
                    lo.weapons[size_t(s)] = get_weapon_def(in);
                }
            }
            const size_t gcount = in.get<uint8_t>();
            for (size_t gi = 0; gi < gcount; ++gi) {
                Gear g;
                g.name = in.get_string();
                g.slot = in.get_string();
                g.modifiers = get_modifiers(in);
                lo.gear.push_back(std::move(g));
            }
            lo.armor_class = in.get_string();
            lo.triggered = get_triggered(in);
            m.resolved_loadouts.push_back(std::move(lo));
        }
        m.bank_credits = in.get<int64_t>();
        const size_t bcount = in.get<uint16_t>();
        if (bcount > 512) throw PacketError("too many banked items");
        for (size_t i = 0; i < bcount; ++i) {
            ItemInstance inst;
            inst.id = in.get<uint64_t>();
            inst.def_id = in.get_string();
            inst.grade = clamp_grade(in.get<uint8_t>());
            inst.mark = clamp_mark(in.get<uint8_t>());
            inst.quantity = std::max<int>(1, in.get<uint16_t>());
            inst.custom_name = in.get_string();
            inst.color_json = in.get_string();
            m.bank_items.push_back(std::move(inst));
        }
        const size_t rep_count = in.get<uint8_t>();
        if (rep_count > 32) throw PacketError("too many reputation entries");
        for (size_t i = 0; i < rep_count; ++i) {
            const std::string faction = in.get_string();
            m.reputation[faction] = in.get<int64_t>();
        }
        message = std::move(m);
        break;
    }
    case Type::inventory_ack:
        message = InventoryAck{in.get<uint32_t>()};
        break;
    case Type::save_loadout:
        message = SaveLoadoutRequest{get_char_loadout(in)};
        break;
    case Type::switch_loadout: {
        SwitchLoadoutRequest m;
        m.slot = in.get<uint8_t>();
        if (m.slot >= max_character_loadouts) throw PacketError("bad loadout slot");
        message = m;
        break;
    }
    case Type::shop_buy: {
        ShopBuyRequest m;
        m.def_id = in.get_string();
        m.use_faction_points = in.get<uint8_t>() != 0;
        message = std::move(m);
        break;
    }
    case Type::destroy_item:
        message = DestroyItemRequest{in.get<uint64_t>()};
        break;
    case Type::loot_take: {
        LootTakeRequest m;
        m.corpse_entity = in.get<uint32_t>();
        m.slot = in.get<int8_t>();
        message = m;
        break;
    }
    case Type::vendor_sell: {
        VendorSellRequest m;
        m.item_id = in.get<uint64_t>();
        m.sell_all_junk = in.get<uint8_t>() != 0;
        message = m;
        break;
    }
    case Type::use_item:
        message = UseItemRequest{in.get<uint64_t>()};
        break;
    case Type::quest_sync: {
        QuestSync m;
        m.revision = in.get<uint32_t>();
        const size_t active = in.get<uint8_t>();
        for (size_t i = 0; i < active; ++i) {
            m.active.push_back(get_quest(in));
        }
        const size_t completed = in.get<uint16_t>();
        if (completed > 1024) throw PacketError("too many completed quests");
        for (size_t i = 0; i < completed; ++i) {
            m.completed.push_back(in.get_string());
        }
        const size_t offers = in.get<uint8_t>();
        for (size_t i = 0; i < offers; ++i) {
            m.offers.push_back(get_quest(in));
        }
        m.offer_source = in.get_string();
        m.offer_serial = in.get<uint16_t>();
        m.offer_note = in.get_string();
        const size_t ready = in.get<uint8_t>();
        for (size_t i = 0; i < ready; ++i) {
            m.ready_givers.push_back(in.get_string());
        }
        const size_t waiting = in.get<uint8_t>();
        for (size_t i = 0; i < waiting; ++i) {
            m.waiting_givers.push_back(in.get_string());
        }
        message = std::move(m);
        break;
    }
    case Type::quest_ack:
        message = QuestAck{in.get<uint32_t>()};
        break;
    case Type::quest_request: {
        QuestRequest m;
        const uint8_t action = in.get<uint8_t>();
        if (action > QuestRequest::abandon) throw PacketError("bad quest action");
        m.action = QuestRequest::Action(action);
        m.target = in.get<uint32_t>();
        m.quest_id = in.get_string();
        message = std::move(m);
        break;
    }
    case Type::profession_sync: {
        ProfessionSync m;
        m.revision = in.get<uint32_t>();
        const size_t count = in.get<uint8_t>();
        for (size_t i = 0; i < count; ++i) {
            ProfessionProgress p;
            p.id = in.get_string();
            p.xp = in.get<int64_t>();
            p.trained = in.get<uint8_t>();
            p.paid = in.get<uint8_t>();
            for (std::vector<std::string>* list : {&p.perks, &p.paid_perks}) {
                const size_t n = in.get<uint8_t>();
                if (n > 16) throw PacketError("too many perks");
                for (size_t k = 0; k < n; ++k) {
                    list->push_back(in.get_string());
                }
            }
            m.professions.push_back(std::move(p));
        }
        message = std::move(m);
        break;
    }
    case Type::profession_ack:
        message = ProfessionAck{in.get<uint32_t>()};
        break;
    case Type::crafting_sync: {
        CraftingSync m;
        m.revision = in.get<uint32_t>();
        const size_t mats = in.get<uint16_t>();
        for (size_t i = 0; i < mats; ++i) {
            MaterialStack st;
            st.resource_id = in.get_string();
            st.amount = in.get<int64_t>();
            m.materials.push_back(std::move(st));
        }
        const size_t presets = in.get<uint16_t>();
        for (size_t i = 0; i < presets; ++i) {
            RecipePreset p;
            p.name = in.get_string();
            p.schematic_id = in.get_string();
            const size_t rcount = in.get<uint8_t>();
            if (rcount > 16) throw PacketError("too many preset resources");
            for (size_t j = 0; j < rcount; ++j) p.resource_ids.push_back(in.get_string());
            const size_t ecount = in.get<uint8_t>();
            if (ecount > 8) throw PacketError("too many preset enhancements");
            for (size_t j = 0; j < ecount; ++j) p.enhancement_defs.push_back(in.get_string());
            p.exp_quality_pct = in.get<uint8_t>();
            m.presets.push_back(std::move(p));
        }
        const size_t blueprints = in.get<uint16_t>();
        for (size_t i = 0; i < blueprints; ++i) m.blueprints.push_back(in.get_string());
        message = std::move(m);
        break;
    }
    case Type::crafting_ack:
        message = CraftingAck{in.get<uint32_t>()};
        break;
    case Type::gather_request: {
        GatherRequest m;
        const uint8_t action = in.get<uint8_t>();
        if (action > uint8_t(GatherRequest::salvage_item)) throw PacketError("bad gather action");
        m.action = GatherRequest::Action(action);
        m.category = in.get<uint8_t>();
        if (m.category >= resource_category_count) throw PacketError("bad resource category");
        m.target_id = in.get<uint64_t>();
        message = m;
        break;
    }
    case Type::service_request: {
        ServiceRequest m;
        m.action = in.get<uint8_t>();
        m.id = in.get_string();
        m.amount = in.get<int64_t>();
        m.item_id = in.get<uint64_t>();
        message = std::move(m);
        break;
    }
    case Type::craft_request: {
        CraftRequest m;
        const uint8_t action = in.get<uint8_t>();
        if (action > uint8_t(CraftRequest::dye)) throw PacketError("bad craft action");
        m.action = CraftRequest::Action(action);
        m.schematic_id = in.get_string();
        const size_t rcount = in.get<uint8_t>();
        if (rcount > 16) throw PacketError("too many craft resources");
        for (size_t i = 0; i < rcount; ++i) m.resource_ids.push_back(in.get_string());
        const size_t ecount = in.get<uint8_t>();
        if (ecount > 8) throw PacketError("too many craft enhancements");
        for (size_t i = 0; i < ecount; ++i) m.enhancement_item_ids.push_back(in.get<uint64_t>());
        const size_t edcount = in.get<uint8_t>();
        if (edcount > 8) throw PacketError("too many preset enhancements");
        for (size_t i = 0; i < edcount; ++i) m.enhancement_defs.push_back(in.get_string());
        m.exp_quality_pct = in.get<uint8_t>();
        m.batch_count = in.get<uint8_t>();
        m.preset_name = in.get_string();
        m.order_id = in.get_string();
        m.mark = in.get<uint8_t>();
        m.item_id = in.get<uint64_t>();
        m.dye_values = in.get_string();
        message = std::move(m);
        break;
    }
    case Type::specialist_request: {
        SpecialistRequest m;
        m.action = in.get<uint8_t>();
        if (m.action > uint8_t(SpecialistRequest::cancel_channel)) throw PacketError("bad specialist action");
        m.id = in.get_string();
        m.target = in.get<uint32_t>();
        m.position = get_vec3(in);
        m.amount = in.get<int64_t>();
        m.item_id = in.get<uint64_t>();
        m.text = in.get_long_string();
        message = std::move(m);
        break;
    }
    case Type::specialist_sync: {
        SpecialistSync m;
        m.revision = in.get<uint32_t>();
        const size_t an = in.get<uint8_t>();
        if (an > 64) throw PacketError("too many abilities");
        for (size_t i = 0; i < an; ++i) {
            AbilityStatus a;
            a.id = in.get_string();
            a.cooldown = in.get<float>();
            m.abilities.push_back(std::move(a));
        }
        const size_t en = in.get<uint8_t>();
        if (en > 64) throw PacketError("too many effects");
        for (size_t i = 0; i < en; ++i) {
            EffectStatus e;
            e.id = in.get_string();
            e.name = in.get_string();
            e.remaining = in.get<float>();
            e.duration = in.get<float>();
            e.flags = in.get<uint16_t>();
            m.effects.push_back(std::move(e));
        }
        const size_t cn = in.get<uint8_t>();
        if (cn > 64) throw PacketError("too many companions");
        for (size_t i = 0; i < cn; ++i) m.companions.push_back(get_companion(in));
        m.active_companion = in.get<uint64_t>();
        m.companion_entity = in.get<uint32_t>();
        m.companion_cooldown = in.get<float>();
        m.companion_mode = in.get<uint8_t>();
        m.companion_slot = in.get<uint64_t>();
        const size_t sn = in.get<uint8_t>();
        if (sn > 64) throw PacketError("too many samples");
        for (size_t i = 0; i < sn; ++i) {
            DnaSample d;
            d.id = in.get<uint64_t>();
            d.template_id = in.get_string();
            d.quality = in.get<float>();
            m.samples.push_back(std::move(d));
        }
        const size_t kn = in.get<uint8_t>();
        if (kn > 16) throw PacketError("too many contracts");
        for (size_t i = 0; i < kn; ++i) m.contracts.push_back(get_contract(in));
        const size_t on = in.get<uint8_t>();
        if (on > 16) throw PacketError("too many contract offers");
        for (size_t i = 0; i < on; ++i) m.offers.push_back(get_contract(in));
        m.offer_source = in.get_string();
        const size_t bn = in.get<uint8_t>();
        if (bn > 40) throw PacketError("too many listings");
        for (size_t i = 0; i < bn; ++i) m.bazaar.push_back(get_listing(in));
        const size_t mn = in.get<uint8_t>();
        if (mn > 40) throw PacketError("too many own listings");
        for (size_t i = 0; i < mn; ++i) m.my_listings.push_back(get_listing(in));
        const size_t pn = in.get<uint8_t>();
        if (pn > 40) throw PacketError("too many prices");
        for (size_t i = 0; i < pn; ++i) {
            std::string def = in.get_string();
            PriceStats p;
            p.count = in.get<int32_t>();
            p.low = in.get<int64_t>();
            p.high = in.get<int64_t>();
            p.average = in.get<int64_t>();
            p.last = in.get<int64_t>();
            m.prices.emplace_back(std::move(def), p);
        }
        m.performing = in.get<uint8_t>() != 0;
        m.bounty_flag = in.get<uint8_t>() != 0;
        m.channel = in.get_string();
        m.channel_remaining = in.get<float>();
        message = std::move(m);
        break;
    }
    case Type::specialist_ack:
        message = SpecialistAck{in.get<uint32_t>()};
        break;
    default:
        throw PacketError("unknown message type");
    }
    if (!in.at_end()) {
        throw PacketError("trailing bytes");
    }
    return message;
}

uint32_t static_key(const EntityState& entity) {
    uint32_t hash = 2166136261u;  // FNV-1a
    const auto mix = [&](std::string_view text) {
        for (const char c : text) {
            hash = (hash ^ uint8_t(c)) * 16777619u;
        }
        hash = (hash ^ 0x1Fu) * 16777619u;  // a separator, so "ab"+"c" differs from "a"+"bc"
    };
    mix(entity.name);
    mix(entity.appearance);
    mix(entity.weapon_model);
    mix(entity.anim_set);
    mix(entity.worn);
    return hash == 0 ? 1 : hash;
}

} // namespace swm::core::net
