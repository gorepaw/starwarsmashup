#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "swm/core/world.h"
#include "swm/core/combat.h"
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
    SoldierDef soldier;                // must match the server's
};

// Game client. Predicts its own character by running each input locally as
// soon as it's made, and reconciles when a snapshot arrives: it takes the
// server's state for the newest input the server has simulated and replays
// the inputs the server hasn't seen yet. Other characters are shown slightly
// in the past, interpolated between snapshots.
class GameClient {
public:
    enum class Status { connecting, connected, refused, timed_out, disconnected };

    struct Remote {
        uint32_t id = 0;
        MoveState state;
        float health = 0;
        bool alive = true;
    };

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
        int weapon = 0;
    };
    void set_input(const Controls& controls);
    // Receives packets, runs due ticks and sends inputs. `now` is in seconds.
    void update(double now);
    void disconnect();

    Status status() const { return status_; }
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

    // Own health, as of the newest snapshot.
    double health() const { return health_; }
    bool alive() const { return alive_; }
    // Other characters' bolts in flight, moved on from the newest snapshot.
    // (Own bolts come from take_shots(), the moment they're fired.)
    std::vector<net::BoltState> bolts(double now) const;
    // Shots this client fired since the last call, predicted: draw them now.
    std::vector<Shot> take_shots();
    // Hits and kills since the last call.
    std::vector<net::CombatEvent> take_events();

private:
    struct Pending {
        MoveInput input;
        MoveState result;  // predicted state after this input
        double sent = 0;
    };
    struct Sample {
        uint32_t tick = 0;
        MoveState state;
        float health = 0;
        bool alive = true;
        uint8_t spawn_count = 0;
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
    // Combat
    double health_ = 0;
    bool alive_ = true;
    uint8_t spawn_count_ = 0;
    std::vector<net::BoltState> bolts_;  // others', from the newest snapshot
    double bolts_time_ = 0;
    std::vector<Shot> shots_;
    std::vector<net::CombatEvent> events_;
};

} // namespace swm::core
