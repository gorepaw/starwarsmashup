#include "swm/core/net/protocol.h"

#include <algorithm>
#include <format>

#include "swm/core/net/bytes.h"

namespace swm::core::net {

namespace {

enum class Type : uint8_t { hello = 1, welcome, input_batch, snapshot, goodbye, refused };

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
}

MoveState get_state(ByteReader& in) {
    MoveState state;
    state.position = get_vec3(in);
    state.velocity = get_vec3(in);
    state.yaw = in.get<float>();
    state.grounded = in.get<uint8_t>() != 0;
    return state;
}

void put_input(ByteWriter& out, const MoveInput& input) {
    out.put(input.sequence);
    out.put(input.forward);
    out.put(input.right);
    out.put(input.yaw);
    out.put(input.buttons);
}

MoveInput get_input(ByteReader& in) {
    MoveInput input;
    input.sequence = in.get<uint32_t>();
    input.forward = in.get<int8_t>();
    input.right = in.get<int8_t>();
    input.yaw = in.get<float>();
    input.buttons = in.get<uint8_t>();
    return input;
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
        }
    }
    void operator()(const Goodbye&) { out.put(uint8_t(Type::goodbye)); }
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
            m.entities.push_back(entity);
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
    default:
        throw PacketError("unknown message type");
    }
    if (!in.at_end()) {
        throw PacketError("trailing bytes");
    }
    return message;
}

} // namespace swm::core::net
