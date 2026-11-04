#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "swm/core/conquest.h"
#include "swm/core/movement.h"
#include "swm/core/persistence.h"

namespace swm::core::net {

// Every packet starts with a magic number, the protocol version and a message
// type. Peers drop packets with another version. Bump it on any wire change.
inline constexpr uint16_t protocol_magic = 0x4D53;  // "SM"
inline constexpr uint16_t protocol_version = 6;

// Client -> server, repeated until a Welcome or Refused arrives. The name
// picks the saved character (there are no accounts yet). A match server
// launched by a hub only admits players with the ticket the hub gave them.
struct Hello {
    std::string name;
    uint64_t ticket = 0;
};

// A hub's war terminal: standing within `radius` of it, a player can queue
// for a battle.
struct Recruiter {
    std::string name;
    Vec3 position;  // game space
    double radius = 4;

    friend bool operator==(const Recruiter&, const Recruiter&) = default;
};

// Server -> client: the handshake answer.
struct Welcome {
    uint32_t entity_id = 0;  // the client's own character
    double tick_rate = 30;   // simulation ticks per second
    uint32_t server_tick = 0;
    PersistentId character_id = 0;  // the saved character; 0 if the server doesn't save
    // A hub's war terminals, and the sides its battles are fought between.
    std::vector<Recruiter> recruiters;
    std::array<std::string, 2> factions;
};

// Client -> hub, repeated until the hub's PlayerStatus shows it: queue for a
// battle on a side (0 or 1), near a war terminal; -1 leaves the queue.
struct QueueRequest {
    int8_t team = -1;
};

// Server -> client, repeated until the client leaves: go to another server
// (a hub sends players to a match, a match sends them back).
struct Transfer {
    uint32_t address = 0;  // IPv4; 0: the same address as this server
    uint16_t port = 0;
    uint64_t ticket = 0;      // to show the next server
    std::string battlefield;  // the match's battlefield (a file name without .ini); empty for a hub

    friend bool operator==(const Transfer&, const Transfer&) = default;
};

// What a server tells one client about itself (in every snapshot).
struct PlayerStatus {
    enum Queue : uint8_t { none, queued };
    Queue queue = none;
    int8_t team = -1;          // queued for
    uint16_t in_queue = 0;     // players queued for the next battle
    float starts_in = 0;       // seconds until it starts
    uint16_t notice_id = 0;    // changes with each new notice
    std::string notice;        // e.g. how the last battle went

    friend bool operator==(const PlayerStatus&, const PlayerStatus&) = default;
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

// Client -> server, repeated while waiting to spawn: the class and command
// post wanted (indexes into the battlefield's team classes and posts).
struct SpawnRequest {
    uint8_t soldier_class = 0;
    uint8_t post = 0;
};

struct EntityState {
    uint32_t id = 0;
    MoveState state;
    float health = 0;
    Life life = Life::alive;
    uint8_t spawn_count = 0;  // changes on every respawn: clients jump there instead of easing
    int8_t team = -1;         // -1: no teams (outside a match)
    uint8_t soldier_class = 0;
    uint16_t kills = 0;
    uint16_t deaths = 0;
    uint16_t captures = 0;    // command posts taken or neutralized while in them

    bool alive() const { return life == Life::alive; }
};

// A projectile in flight: a blaster bolt, a missile or a grenade.
struct BoltState {
    uint32_t id = 0;
    uint32_t owner = 0;  // entity that fired it
    uint8_t weapon = 0;  // the owner's weapon slot, for drawing it
    Projectile kind = Projectile::bolt;
    Vec3 position;
    Vec3 velocity;
};

// An explosion this tick, for drawing.
struct Explosion {
    uint32_t owner = 0;
    uint8_t weapon = 0;
    Vec3 position;
    float radius = 0;  // the outer damage radius
};

// Something that happened this tick. Hits and kills are between entities
// (attacker 0: the world, e.g. out of bounds); for captured and neutralized,
// `attacker` is the team and `victim` the command post.
struct CombatEvent {
    enum Kind : uint8_t { hit = 1, kill = 2, captured = 3, neutralized = 4 };
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
    std::vector<Explosion> explosions;  // this tick's
    std::optional<MatchState> match;  // in a conquest match
    PlayerStatus you;                 // differs per client
};

// Either direction: leaving.
struct Goodbye {};

using Message =
    std::variant<Hello, Welcome, Refused, InputBatch, Snapshot, Goodbye, SpawnRequest, QueueRequest, Transfer>;

std::vector<uint8_t> encode(const Message& message);
// Throws PacketError on malformed packets or another protocol version.
Message decode(std::span<const uint8_t> packet);

// Most inputs one InputBatch carries.
inline constexpr size_t max_batched_inputs = 16;

} // namespace swm::core::net
