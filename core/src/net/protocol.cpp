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
    out.put(uint8_t(uint8_t(state.crouched) | uint8_t(state.crouch_held) << 1));
    out.put(state.roll);
    out.put(state.roll_yaw);
    const CombatState& c = state.combat;
    out.put(c.weapon);
    out.put(c.cooldown);
    out.put(c.reload);
    out.put(c.spread);
    for (const WeaponState& w : c.weapons) {
        out.put(w.rounds);
        out.put(w.reserve);
        out.put(w.heat);
        out.put(uint8_t(w.overheated));
    }
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
    state.roll = in.get<float>();
    state.roll_yaw = in.get<float>();
    CombatState& c = state.combat;
    c.weapon = in.get<uint8_t>();
    c.cooldown = in.get<double>();
    c.reload = in.get<double>();
    c.spread = in.get<double>();
    for (WeaponState& w : c.weapons) {
        w.rounds = in.get<int16_t>();
        w.reserve = in.get<int16_t>();
        w.heat = in.get<float>();
        w.overheated = in.get<uint8_t>() != 0;
    }
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
    out.put(input.weapon);
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
    input.weapon = in.get<uint8_t>();
    input.view_tick = in.get<double>();
    return input;
}

void put_match(ByteWriter& out, const MatchState& m) {
    out.put(uint8_t(m.phase));
    out.put(float(m.time_left));
    out.put(float(m.tickets[0]));
    out.put(float(m.tickets[1]));
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
    return m;
}

void put_modifiers(ByteWriter& out, const std::vector<StatModifier>& mods) {
    out.put(uint8_t(std::min<size_t>(mods.size(), 64)));
    for (size_t i = 0; i < mods.size() && i < 64; ++i) {
        out.put(uint8_t(mods[i].stat));
        out.put(float(mods[i].add));
        out.put(float(mods[i].multiply));
    }
}

std::vector<StatModifier> get_modifiers(ByteReader& in) {
    const size_t n = in.get<uint8_t>();
    std::vector<StatModifier> mods;
    mods.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t s = in.get<uint8_t>();
        if (s > uint8_t(Stat::mitigation_melee)) {
            throw PacketError("bad stat");
        }
        StatModifier m;
        m.stat = Stat(s);
        m.add = in.get<float>();
        m.multiply = in.get<float>();
        mods.push_back(m);
    }
    return mods;
}

void put_weapon_def(ByteWriter& out, const WeaponDef& w) {
    out.put_string(w.name);
    out.put_string(w.model);
    out.put_string(w.anim_set);
    out.put(uint8_t(w.damage_type));
    out.put(uint8_t(w.projectile));
    out.put(int16_t(w.ammo));
    out.put(float(w.shot_delay));
    out.put(int16_t(w.rounds_per_clip));
    out.put(float(w.reload_time));
    out.put(float(w.heat_per_shot));
    out.put(float(w.heat_recover_rate));
    out.put(float(w.heat_threshold));
    out.put(float(w.yaw_spread));
    out.put(float(w.pitch_spread));
    out.put(float(w.spread_per_shot));
    out.put(float(w.spread_recover_rate));
    out.put(float(w.spread_limit));
    out.put(uint8_t(std::clamp(w.shots_per_salvo, 1, 255)));
    out.put(float(w.zoom_min));
    out.put(float(w.zoom_max));
    out.put(uint8_t(w.zoom_first_person));
    out.put(float(w.damage));
    out.put(float(w.bolt_speed));
    out.put(float(w.bolt_gravity));
    out.put(float(w.bolt_life));
    out.put(float(w.damage_transition_delay));
    out.put(float(w.damage_transition_period));
    out.put(float(w.damage_final));
    out.put(float(w.bolt_acceleration));
    out.put(float(w.rebound));
    out.put(float(w.friction));
    out.put(uint8_t(w.explode_on_impact));
    out.put(float(w.fuse));
    out.put(uint8_t(w.timer_on_contact));
    out.put(float(w.shot_elevate));
    out.put(float(w.explosion_damage));
    out.put(float(w.explosion_inner));
    out.put(float(w.explosion_outer));
    out.put(float(w.explosion_push));
    out.put(float(w.push_inner));
    out.put(float(w.push_outer));
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
    const uint8_t dt = in.get<uint8_t>();
    if (dt >= damage_type_count) throw PacketError("bad damage type");
    w.damage_type = DamageType(dt);
    const uint8_t proj = in.get<uint8_t>();
    if (proj > uint8_t(Projectile::grenade)) throw PacketError("bad projectile");
    w.projectile = Projectile(proj);
    w.ammo = in.get<int16_t>();
    w.shot_delay = in.get<float>();
    w.rounds_per_clip = in.get<int16_t>();
    w.reload_time = in.get<float>();
    w.heat_per_shot = in.get<float>();
    w.heat_recover_rate = in.get<float>();
    w.heat_threshold = in.get<float>();
    w.yaw_spread = in.get<float>();
    w.pitch_spread = in.get<float>();
    w.spread_per_shot = in.get<float>();
    w.spread_recover_rate = in.get<float>();
    w.spread_limit = in.get<float>();
    w.shots_per_salvo = in.get<uint8_t>();
    w.zoom_min = in.get<float>();
    w.zoom_max = in.get<float>();
    w.zoom_first_person = in.get<uint8_t>() != 0;
    w.damage = in.get<float>();
    w.bolt_speed = in.get<float>();
    w.bolt_gravity = in.get<float>();
    w.bolt_life = in.get<float>();
    w.damage_transition_delay = in.get<float>();
    w.damage_transition_period = in.get<float>();
    w.damage_final = in.get<float>();
    w.bolt_acceleration = in.get<float>();
    w.rebound = in.get<float>();
    w.friction = in.get<float>();
    w.explode_on_impact = in.get<uint8_t>() != 0;
    w.fuse = in.get<float>();
    w.timer_on_contact = in.get<uint8_t>() != 0;
    w.shot_elevate = in.get<float>();
    w.explosion_damage = in.get<float>();
    w.explosion_inner = in.get<float>();
    w.explosion_outer = in.get<float>();
    w.explosion_push = in.get<float>();
    w.push_inner = in.get<float>();
    w.push_outer = in.get<float>();
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
    return lo;
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
        }
        out.put_string(m.factions[0]);
        out.put_string(m.factions[1]);
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
        out.put(uint8_t(std::min<size_t>(m.vendors.size(), 255)));
        for (size_t i = 0; i < m.vendors.size() && i < 255; ++i) {
            const Vendor& v = m.vendors[i];
            out.put_string(v.name);
            out.put_string(v.kind);
            put_vec3(out, v.position);
            out.put(v.radius);
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
            put_vec3(out, t.position);
            out.put(t.radius);
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
            put_state(out, entity.state);
            out.put(entity.health);
            out.put(entity.max_health);
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
            out.put(entity.colors);
            out.put(entity.species);
            out.put(entity.gender);
            out.put_string(entity.name);
            out.put_long_string(entity.appearance);
            out.put_string(entity.weapon_model);
            out.put_string(entity.anim_set);
            out.put_long_string(entity.worn);
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
                out.put(c.items[ii].quality);
            }
        }
        out.put(uint8_t(m.match.has_value()));
        if (m.match) {
            put_match(out, *m.match);
        }
        out.put(uint8_t(m.you.queue));
        out.put(m.you.team);
        out.put(uint8_t(m.you.queue_mode));
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
        out.put(m.you.inventory_rev);
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
        out.put(uint8_t(m.mode));
    }
    void operator()(const Transfer& m) {
        out.put(uint8_t(Type::transfer));
        out.put(m.address);
        out.put(m.port);
        out.put(m.ticket);
        out.put_string(m.battlefield);
    }
    void operator()(const InventorySync& m) {
        out.put(uint8_t(Type::inventory_sync));
        out.put(m.revision);
        out.put(uint16_t(m.items.size()));
        for (const ItemInstance& inst : m.items) {
            out.put(inst.id);
            out.put(inst.owner_id);
            out.put_string(inst.def_id);
            out.put(float(inst.quality));
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
            m.recruiters.push_back(r);
        }
        m.factions[0] = in.get_string();
        m.factions[1] = in.get_string();
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
        const size_t vs = in.get<uint8_t>();
        for (size_t i = 0; i < vs; ++i) {
            Vendor v;
            v.name = in.get_string();
            v.kind = in.get_string();
            v.position = get_vec3(in);
            v.radius = in.get<double>();
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
            t.position = get_vec3(in);
            t.radius = in.get<double>();
            m.mission_terminals.push_back(std::move(t));
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
            entity.state = get_state(in);
            entity.health = in.get<float>();
            entity.max_health = in.get<float>();
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
            entity.colors = in.get<uint16_t>();
            entity.species = in.get<uint8_t>();
            if (entity.species >= species_count) {
                throw PacketError("bad species");
            }
            entity.gender = in.get<uint8_t>();
            if (entity.gender > 1) {
                throw PacketError("bad gender");
            }
            entity.name = in.get_string();
            entity.appearance = in.get_long_string();
            entity.weapon_model = in.get_string();
            entity.anim_set = in.get_string();
            entity.worn = in.get_long_string();
            m.entities.push_back(entity);
        }
        const size_t bolts = in.get<uint16_t>();
        for (size_t i = 0; i < bolts; ++i) {
            BoltState bolt;
            bolt.id = in.get<uint32_t>();
            bolt.owner = in.get<uint32_t>();
            bolt.weapon = in.get<uint8_t>();
            const uint8_t kind = in.get<uint8_t>();
            if (kind > uint8_t(Projectile::grenade)) {
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
            if (kind < CombatEvent::hit || kind > CombatEvent::neutralized) {
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
                entry.quality = in.get<float>();
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
        m.you.team = get_character_team(in);
        const uint8_t qmode = in.get<uint8_t>();
        if (qmode > uint8_t(QueueMode::gear)) {
            throw PacketError("bad queue mode");
        }
        m.you.queue_mode = QueueMode(qmode);
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
        m.you.inventory_rev = in.get<uint32_t>();
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
        m.team = get_team(in);
        const uint8_t mode = in.get<uint8_t>();
        if (mode > uint8_t(QueueMode::gear)) {
            throw PacketError("bad queue mode");
        }
        m.mode = QueueMode(mode);
        message = m;
        break;
    }
    case Type::transfer: {
        Transfer m;
        m.address = in.get<uint32_t>();
        m.port = in.get<uint16_t>();
        m.ticket = in.get<uint64_t>();
        m.battlefield = in.get_string();
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
            inst.quality = in.get<float>();
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
            m.resolved_loadouts.push_back(std::move(lo));
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
    default:
        throw PacketError("unknown message type");
    }
    if (!in.at_end()) {
        throw PacketError("trailing bytes");
    }
    return message;
}

} // namespace swm::core::net
