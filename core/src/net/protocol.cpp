#include "swm/core/net/protocol.h"

#include <algorithm>
#include <format>

#include "swm/core/net/bytes.h"

namespace swm::core::net {

namespace {

enum class Type : uint8_t { hello = 1, welcome, input_batch, snapshot, goodbye, refused, spawn_request };

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

// Full precision for now. Quantizing positions and velocities is a later
// bandwidth optimization; clients would then re-simulate from the quantized
// state, as the server must too.
void put_state(ByteWriter& out, const MoveState& state) {
    put_vec3(out, state.position);
    put_vec3(out, state.velocity);
    out.put(state.yaw);
    out.put(uint8_t(state.grounded));
    out.put(state.energy);
    out.put(uint8_t(state.sprinting));
    const CombatState& c = state.combat;
    out.put(c.weapon);
    out.put(c.cooldown);
    out.put(c.reload);
    out.put(c.spread);
    for (const WeaponState& w : c.weapons) {
        out.put(w.rounds);
        out.put(w.heat);
        out.put(uint8_t(w.overheated));
    }
}

MoveState get_state(ByteReader& in) {
    MoveState state;
    state.position = get_vec3(in);
    state.velocity = get_vec3(in);
    state.yaw = in.get<float>();
    state.grounded = in.get<uint8_t>() != 0;
    state.energy = in.get<double>();
    state.sprinting = in.get<uint8_t>() != 0;
    CombatState& c = state.combat;
    c.weapon = in.get<uint8_t>();
    c.cooldown = in.get<double>();
    c.reload = in.get<double>();
    c.spread = in.get<double>();
    for (WeaponState& w : c.weapons) {
        w.rounds = in.get<int16_t>();
        w.heat = in.get<float>();
        w.overheated = in.get<uint8_t>() != 0;
    }
    if (c.weapon >= c.weapons.size()) {
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
    return m;
}

struct Encoder {
    ByteWriter& out;

    void operator()(const Hello& m) {
        out.put(uint8_t(Type::hello));
        out.put_string(m.name);
    }
    void operator()(const Welcome& m) {
        out.put(uint8_t(Type::welcome));
        out.put(m.entity_id);
        out.put(m.tick_rate);
        out.put(m.server_tick);
        out.put(m.character_id);
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
            out.put(uint8_t(entity.life));
            out.put(entity.spawn_count);
            out.put(entity.team);
            out.put(entity.soldier_class);
            out.put(entity.kills);
            out.put(entity.deaths);
            out.put(entity.captures);
        }
        out.put(uint16_t(m.bolts.size()));
        for (const BoltState& bolt : m.bolts) {
            out.put(bolt.id);
            out.put(bolt.owner);
            out.put(bolt.weapon);
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
        out.put(uint8_t(m.match.has_value()));
        if (m.match) {
            put_match(out, *m.match);
        }
    }
    void operator()(const Goodbye&) { out.put(uint8_t(Type::goodbye)); }
    void operator()(const SpawnRequest& m) {
        out.put(uint8_t(Type::spawn_request));
        out.put(m.soldier_class);
        out.put(m.post);
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
    case Type::hello:
        message = Hello{in.get_string()};
        break;
    case Type::welcome: {
        Welcome m;
        m.entity_id = in.get<uint32_t>();
        m.tick_rate = in.get<double>();
        m.server_tick = in.get<uint32_t>();
        m.character_id = in.get<uint64_t>();
        if (!(m.tick_rate >= 1 && m.tick_rate <= 240)) {
            throw PacketError("bad tick rate");
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
            const uint8_t life = in.get<uint8_t>();
            if (life > uint8_t(Life::dead)) {
                throw PacketError("bad life state");
            }
            entity.life = Life(life);
            entity.spawn_count = in.get<uint8_t>();
            entity.team = get_team(in);
            entity.soldier_class = in.get<uint8_t>();
            entity.kills = in.get<uint16_t>();
            entity.deaths = in.get<uint16_t>();
            entity.captures = in.get<uint16_t>();
            m.entities.push_back(entity);
        }
        const size_t bolts = in.get<uint16_t>();
        for (size_t i = 0; i < bolts; ++i) {
            BoltState bolt;
            bolt.id = in.get<uint32_t>();
            bolt.owner = in.get<uint32_t>();
            bolt.weapon = in.get<uint8_t>();
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
        if (in.get<uint8_t>() != 0) {
            m.match = get_match(in);
        }
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
        m.soldier_class = in.get<uint8_t>();
        m.post = in.get<uint8_t>();
        message = m;
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
