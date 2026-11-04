#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "swm/core/combat.h"
#include "swm/core/conquest.h"
#include "swm/core/movement.h"
#include "swm/core/net/link.h"
#include "swm/core/net/protocol.h"
#include "swm/core/persistence.h"
#include "swm/core/world.h"

namespace swm::core {

struct ServerConfig {
    double tick_rate = 30;
    Vec3 spawn;                  // game space; y comes from the ground
    double spawn_spacing = 3;    // meters between players' spawn points, along x (8 points)
    double client_timeout = 5;   // seconds of silence before a client is dropped
    size_t max_inputs_per_tick = 3;   // lets a client catch up after jitter, but not speed up for long
    size_t max_queued_inputs = 30;
    double save_interval = 10;   // seconds between saves of every connected character
    double respawn_delay = 5;    // seconds dead before respawning (in a match: before choosing a spawn)
    double max_rewind = 0.5;     // seconds: the most the server rewinds targets for a shooter
    SoldierDef soldier;          // everyone's class outside a match; must match the clients'
    // With a battlefield the server runs a conquest match on it: players
    // join a team, choose a class and a command post to spawn at, and the
    // match's rules decide who wins. Clients must load the same battlefield.
    // Characters aren't saved in a match.
    std::optional<Battlefield> battlefield;
    bool friendly_fire = false;  // in a match
};

// Authoritative game server. Clients send inputs; the server simulates every
// character from those inputs alone and sends each client a snapshot of the
// world every tick. A character only moves when its inputs arrive, so the
// server's result for an input is exactly what the client predicted.
//
// Combat: shots become bolts that fly through the world. Bolts are tested
// against other characters where the shooter saw them: each input says which
// server tick its client was showing others at, and the server rewinds them
// that far (up to `max_rewind`).
//
// Outside a match everyone is hostile and respawns where they first spawned.
// In a match (ServerConfig::battlefield) players are put on the smaller team
// and wait until their client asks to spawn at a command post their team
// holds; see Conquest for the rules.
//
// With a CharacterStore (and no match), a player's name picks their saved
// character: it spawns where it was last saved. Characters are saved when
// they leave or time out, every `save_interval`, and on save_all() (call it
// at shutdown).
class GameServer {
public:
    GameServer(net::Link& link, const World& world, ServerConfig config, CharacterStore* store = nullptr);

    // Receives packets and runs any ticks that are due. `now` is in seconds,
    // from any steady clock.
    void update(double now);

    uint32_t tick() const { return tick_; }
    size_t client_count() const { return clients_.size(); }
    std::vector<uint32_t> entities() const;
    const MoveState* state_of(uint32_t entity) const;
    // Moves a character, e.g. a teleport. Its client gets corrected.
    void set_state(uint32_t entity, const MoveState& state);
    double health_of(uint32_t entity) const;
    bool alive(uint32_t entity) const;
    // The entity as snapshots describe it, if it's connected.
    std::optional<net::EntityState> describe(uint32_t entity) const;
    size_t bolt_count() const { return bolts_.size(); }
    // The match, if there is one.
    const Conquest* match() const { return conquest_ ? &*conquest_ : nullptr; }
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
        double health = 0;
        Life life = Life::alive;
        uint32_t respawn_tick = 0;
        uint8_t spawn_count = 0;
        int team = -1;
        size_t soldier_class = 0;
        std::optional<net::SpawnRequest> wants_spawn;
        double outside = 0;  // seconds out of bounds
        uint16_t kills = 0, deaths = 0, captures = 0;
        std::deque<std::pair<uint32_t, Vec3>> history;  // recent positions, for rewinding
        uint32_t last_input = 0;  // newest simulated input
        std::deque<MoveInput> queue;
        double last_heard = 0;
    };
    struct Bolt {
        uint32_t id = 0;
        uint32_t owner = 0;
        int team = -1;
        uint8_t weapon = 0;
        Vec3 position;
        Vec3 velocity;
        double life = 0;
        double damage = 0;
        double gravity = 0;  // m/s^2
        double rewind = 0;   // ticks: targets are tested this far in the past
    };

    void receive(double now);
    void handle(const net::Endpoint& from, const net::Datagram& datagram, double now);
    Client* join(const net::Endpoint& from, const std::string& name);
    void save(std::span<const Client> clients);
    void step();
    void run_match();
    void fire(const Client& shooter, const Shot& shot, double view_tick);
    void advance_bolts();
    void kill(Client& victim, uint32_t attacker);
    void respawn(Client& client);
    void spawn_at_post(Client& client, size_t soldier_class, size_t post);
    void return_to_waiting(Client& client);
    const SoldierDef& soldier_of(const Client& client) const;
    net::EntityState describe(const Client& client) const;
    Vec3 spawn_point(uint32_t index) const;
    Vec3 position_at(const Client& client, double tick) const;
    void drop_silent(double now);
    void send(const net::Endpoint& to, const auto& message);
    Client* find(const net::Endpoint& endpoint);
    Client* find(uint32_t entity);
    const Client* find(uint32_t entity) const;
    void say(const std::string& text) const;

    net::Link& link_;
    const World& world_;
    ServerConfig config_;
    CharacterStore* store_;
    std::optional<Conquest> conquest_;
    std::vector<Client> clients_;
    std::vector<Bolt> bolts_;
    std::vector<net::CombatEvent> events_;  // this tick's
    uint32_t tick_ = 0;
    uint32_t next_entity_ = 1;
    uint32_t next_bolt_ = 1;
    uint32_t next_spawn_slot_ = 0;  // spreads spawns around a post
    double next_tick_time_ = 0;
    double next_save_time_ = 0;
    bool started_ = false;
};

} // namespace swm::core
