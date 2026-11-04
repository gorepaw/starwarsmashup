#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "swm/core/movement.h"
#include "swm/core/persistence.h"

namespace swm::core::net {

// Every packet starts with a magic number, the protocol version and a message
// type. Peers drop packets with another version. Bump it on any wire change.
inline constexpr uint16_t protocol_magic = 0x4D53;  // "SM"
inline constexpr uint16_t protocol_version = 3;

// Client -> server, repeated until a Welcome or Refused arrives. The name
// picks the saved character (there are no accounts yet).
struct Hello {
    std::string name;
};

// Server -> client: the handshake answer.
struct Welcome {
    uint32_t entity_id = 0;  // the client's own character
    double tick_rate = 30;   // simulation ticks per second
    uint32_t server_tick = 0;
    PersistentId character_id = 0;  // the saved character; 0 if the server doesn't save
};

// Server -> client: the handshake failed.
struct Refused {
    std::string reason;
};

// Client -> server: the newest inputs, oldest first. Each packet repeats the
// inputs the server hasn't acknowledged yet, so a lost packet costs nothing.
struct InputBatch {
    std::vector<MoveInput> inputs;
};

struct EntityState {
    uint32_t id = 0;
    MoveState state;
    float health = 0;
    bool alive = true;
    uint8_t spawn_count = 0;  // changes on every respawn: clients jump there instead of easing
};

// A blaster bolt in flight.
struct BoltState {
    uint32_t id = 0;
    uint32_t owner = 0;  // entity that fired it
    uint8_t weapon = 0;  // the owner's weapon slot, for drawing it
    Vec3 position;
    Vec3 velocity;
};

// Something that happened this tick.
struct CombatEvent {
    enum Kind : uint8_t { hit = 1, kill = 2 };
    Kind kind = hit;
    uint32_t attacker = 0;
    uint32_t victim = 0;
    float damage = 0;

    friend bool operator==(const CombatEvent&, const CombatEvent&) = default;
};

// Server -> client, every tick: the world as the server sees it.
struct Snapshot {
    uint32_t server_tick = 0;
    uint32_t last_input = 0;  // newest input of this client's that the server has simulated
    std::vector<EntityState> entities;
    std::vector<BoltState> bolts;
    // This tick's events. A lost snapshot loses them; the health in later
    // snapshots stays right.
    std::vector<CombatEvent> events;
};

// Either direction: leaving.
struct Goodbye {};

using Message = std::variant<Hello, Welcome, Refused, InputBatch, Snapshot, Goodbye>;

std::vector<uint8_t> encode(const Message& message);
// Throws PacketError on malformed packets or another protocol version.
Message decode(std::span<const uint8_t> packet);

// Most inputs one InputBatch carries.
inline constexpr size_t max_batched_inputs = 16;

} // namespace swm::core::net
