#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "swm/core/world.h"
#include "swm/core/combat.h"
#include "swm/core/conquest.h"
#include "swm/core/movement.h"
#include "swm/core/net/protocol.h"
#include "swm/core/net/link.h"
#include "swm/core/persistence.h"

namespace swm::core {

struct ClientConfig {
    net::Endpoint server;
    std::string name = "player";
    double interpolation_delay = 0.1;  // seconds other players are shown in the past
    double hello_interval = 0.25;
    double timeout = 5;                // seconds without packets before giving up
    double smoothing = 10;             // 1/s: how fast a correction's visible jump fades
    SoldierDef soldier;                // outside a match; must match the server's
    std::optional<Battlefield> battlefield;  // in a match: the server's, for its classes
    double spawn_request_interval = 0.25;
    uint64_t ticket = 0;               // from a hub, for the match it sent us to
};

// Game client. Predicts its own character by running each input locally as
// soon as it's made, and reconciles when a snapshot arrives: it takes the
// server's state for the newest input the server has simulated and replays
// the inputs the server hasn't seen yet. Other characters are shown slightly
// in the past, interpolated between snapshots.
//
// In a match the character starts out waiting: request_spawn() picks a class
// and a command post, and the server spawns it there when it can.
class GameClient {
public:
    // transferring: the server sent us elsewhere (see transfer(); ClientSession follows it).
    enum class Status { connecting, connected, refused, timed_out, disconnected, transferring };

    // Another character as the newest snapshots describe it, its state
    // interpolated.
    using Remote = net::EntityState;

    struct Stats {
        uint32_t ticks = 0;          // inputs made
        uint32_t snapshots = 0;
        uint32_t corrections = 0;    // snapshots whose state differed from the prediction
        double max_correction = 0;   // meters
        double rtt = 0;              // seconds, smoothed
    };

    GameClient(net::Link& link, const World& world, ClientConfig config);

    // Input for the next ticks: axes -1..1, yaw and pitch in radians, the
    // weapon slot wanted (see MoveInput).
    struct Controls {
        double forward = 0;
        double right = 0;
        double yaw = 0;
        double pitch = 0;
        bool sprint = false;
        bool jump = false;
        bool fire = false;
        bool reload = false;
        bool secondary = false;  // throw a grenade
        int weapon = 0;
    };
    void set_input(const Controls& controls);
    // Receives packets, runs due ticks and sends inputs. `now` is in seconds.
    void update(double now);
    void disconnect();

    Status status() const { return status_; }
    const ClientConfig& config() const { return config_; }
    // True once the server has placed the character.
    bool spawned() const { return spawned_; }
    uint32_t entity_id() const { return entity_; }
    // The saved character this client plays (0 if the server doesn't save).
    PersistentId character_id() const { return character_; }
    // Why the server refused the connection, if it did.
    const std::string& refusal() const { return refusal_; }
    double tick_rate() const { return tick_rate_; }
    // The newest predicted state.
    const MoveState& predicted() const { return current_; }
    // What to draw: interpolated between the last two ticks, with any
    // correction eased in.
    MoveState presented() const;
    std::vector<Remote> remotes() const;
    const Stats& stats() const { return stats_; }

    // Own character as the newest snapshot describes it (health, life, team,
    // class, score); its state is the server's, not the prediction.
    const net::EntityState& self() const { return self_; }
    double health() const { return self_.health; }
    bool alive() const { return self_.life == Life::alive; }
    // The class being played: its movement and weapons.
    const SoldierDef& soldier() const;
    // In a match: asks to spawn as a class of our team at a command post,
    // until the character spawns. Spawning again after dying needs a new
    // request.
    void request_spawn(int soldier_class, int post);
    // The match as of the newest snapshot, if there is one.
    const std::optional<MatchState>& match() const { return match_; }

    // A hub's war terminals and the sides its battles are between.
    const std::vector<net::Recruiter>& recruiters() const { return recruiters_; }
    const std::array<std::string, 2>& factions() const { return factions_; }
    // Queue for a battle on a side (0 or 1) while at a war terminal, or
    // leave the queue (-1). Repeated until the server's status shows it.
    void queue(int team);
    // What the server says about us: queue, notices.
    const net::PlayerStatus& player_status() const { return player_status_; }
    // Where the server sent us (status transferring).
    const std::optional<net::Transfer>& transfer() const { return transfer_; }
    // Other characters' bolts in flight, moved on from the newest snapshot.
    // (Own bolts come from take_shots(), the moment they're fired.)
    std::vector<net::BoltState> bolts(double now) const;
    // Shots this client fired since the last call, predicted: draw them now.
    std::vector<Shot> take_shots();
    // Hits and kills since the last call.
    std::vector<net::CombatEvent> take_events();
    // Explosions since the last call.
    std::vector<net::Explosion> take_explosions();

private:
    struct Pending {
        MoveInput input;
        MoveState result;  // predicted state after this input
        double sent = 0;
    };
    struct Sample {
        uint32_t tick = 0;
        net::EntityState entity;
    };

    void receive(double now);
    void handle(const net::Datagram& datagram, double now);
    void reconcile(uint32_t last_input, const net::EntityState& server, double now);
    void tick(double now);
    void advance_clocks(double now);

    net::Link& link_;
    const World& world_;
    ClientConfig config_;
    Status status_ = Status::connecting;
    bool spawned_ = false;
    uint32_t entity_ = 0;
    PersistentId character_ = 0;
    std::string refusal_;
    double tick_rate_ = 30;
    MoveInput next_input_;
    uint32_t sequence_ = 0;
    std::deque<Pending> pending_;
    MoveState previous_;
    MoveState current_;
    Vec3 correction_;  // eased toward zero
    double next_tick_time_ = 0;
    double last_update_ = -1;
    double last_hello_ = -1e9;
    double last_heard_ = 0;
    bool started_ = false;
    // Other characters
    uint32_t latest_tick_ = 0;
    double latest_tick_time_ = 0;
    double render_tick_ = 0;
    std::map<uint32_t, std::deque<Sample>> history_;
    Stats stats_;
    // Combat and the match
    net::EntityState self_;
    std::optional<MatchState> match_;
    std::optional<net::SpawnRequest> spawn_request_;
    double last_spawn_request_ = -1e9;
    // Hubs
    std::vector<net::Recruiter> recruiters_;
    std::array<std::string, 2> factions_;
    net::PlayerStatus player_status_;
    int wanted_queue_ = -2;  // -2: no request
    double last_queue_request_ = -1e9;
    std::optional<net::Transfer> transfer_;
    std::vector<net::BoltState> bolts_;  // others', from the newest snapshot
    double bolts_time_ = 0;
    std::vector<Shot> shots_;
    std::vector<net::CombatEvent> events_;
    std::vector<net::Explosion> explosions_;
};

// A client that follows the server's transfers, from a hub to a match and
// back. Each server gets a new GameClient: entity ids and everything else
// start over, and generation() counts them. `battlefields` finds a match's
// battlefield by name, for its classes. Every server must use the same world
// for now.
class ClientSession {
public:
    using BattlefieldFinder = std::function<std::optional<Battlefield>(const std::string& name)>;

    ClientSession(net::Link& link, const World& world, ClientConfig config, BattlefieldFinder battlefields);

    // Follows a pending transfer, then updates the client.
    void update(double now);
    GameClient& client() { return *client_; }
    const GameClient& client() const { return *client_; }
    int generation() const { return generation_; }
    // Why the last transfer couldn't be followed, if it couldn't.
    const std::string& error() const { return error_; }

private:
    net::Link& link_;
    const World& world_;
    ClientConfig config_;
    BattlefieldFinder battlefields_;
    std::unique_ptr<GameClient> client_;
    int generation_ = 0;
    std::string error_;
};

} // namespace swm::core
