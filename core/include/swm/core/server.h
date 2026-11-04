#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "swm/core/heightfield.h"
#include "swm/core/movement.h"
#include "swm/core/net/link.h"
#include "swm/core/persistence.h"

namespace swm::core {

struct ServerConfig {
    double tick_rate = 30;
    Vec3 spawn;                  // game space; y comes from the ground
    double spawn_spacing = 3;    // meters between players' spawn points, along x
    double client_timeout = 5;   // seconds of silence before a client is dropped
    size_t max_inputs_per_tick = 3;   // lets a client catch up after jitter, but not speed up for long
    size_t max_queued_inputs = 30;
    double save_interval = 10;   // seconds between saves of every connected character
    MoveParams move;             // must match the clients' (sent in the handshake later)
};

// Authoritative game server. Clients send inputs; the server simulates every
// character from those inputs alone and sends each client a snapshot of the
// world every tick. A character only moves when its inputs arrive, so the
// server's result for an input is exactly what the client predicted.
//
// With a CharacterStore, a player's name picks their saved character: it
// spawns where it was last saved. Characters are saved when they leave or
// time out, every `save_interval`, and on save_all() (call it at shutdown).
class GameServer {
public:
    GameServer(net::Link& link, const Heightfield& ground, ServerConfig config, CharacterStore* store = nullptr);

    // Receives packets and runs any ticks that are due. `now` is in seconds,
    // from any steady clock.
    void update(double now);

    uint32_t tick() const { return tick_; }
    size_t client_count() const { return clients_.size(); }
    std::vector<uint32_t> entities() const;
    const MoveState* state_of(uint32_t entity) const;
    // Moves a character, e.g. a teleport. Its client gets corrected.
    void set_state(uint32_t entity, const MoveState& state);
    // Saves every connected character now. Storage errors are logged.
    void save_all();

    std::function<void(const std::string&)> log;

private:
    struct Client {
        net::Endpoint endpoint;
        std::string name;
        uint32_t entity = 0;
        PersistentId character = 0;  // 0 without a store
        MoveState state;
        uint32_t last_input = 0;  // newest simulated input
        std::deque<MoveInput> queue;
        double last_heard = 0;
    };

    void receive(double now);
    void handle(const net::Endpoint& from, const net::Datagram& datagram, double now);
    Client* join(const net::Endpoint& from, const std::string& name);
    void save(std::span<const Client> clients);
    void step();
    void drop_silent(double now);
    void send(const net::Endpoint& to, const auto& message);
    Client* find(const net::Endpoint& endpoint);
    Client* find(uint32_t entity);
    void say(const std::string& text) const;

    net::Link& link_;
    const Heightfield& ground_;
    ServerConfig config_;
    CharacterStore* store_;
    std::vector<Client> clients_;
    uint32_t tick_ = 0;
    uint32_t next_entity_ = 1;
    uint32_t spawned_ = 0;
    double next_tick_time_ = 0;
    double next_save_time_ = 0;
    bool started_ = false;
};

} // namespace swm::core
