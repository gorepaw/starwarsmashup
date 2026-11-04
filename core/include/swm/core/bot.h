#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "swm/core/client.h"
#include "swm/core/net/udp.h"
#include "swm/core/vec3.h"

namespace swm::core {

class World;

struct BotSkill {
    double aim_error = 2.5;    // degrees: how far its aim wanders
    double reaction = 0.4;     // seconds from spotting an enemy to shooting
    double fire_range = 60;    // m
};

// A simple soldier that plays through a GameClient, with the same controls
// and the same view of the world a player has. In a match it spawns at the
// post nearest the fighting, runs to the nearest post its team doesn't hold
// (or guards one), and shoots enemies it can see, leading them (and lobbing
// grenades and mortar shells on a ballistic arc). In a hub it
// walks to the nearest war terminal and queues for a battle. Elsewhere it
// hunts the nearest other character. No pathfinding yet: it runs straight,
// and jumps when stuck.
class Bot {
public:
    explicit Bot(uint64_t seed, BotSkill skill = {});

    // Sets the client's controls (and spawn requests) for the next ticks.
    // Call before client.update().
    void update(GameClient& client, const World& world, double now);

    // The command post it's heading for, or -1.
    int objective() const { return objective_; }
    // The side it queues for in a hub (default: picked from the seed).
    void set_queue_team(int team) { queue_team_ = team; }

private:
    double random();  // 0..1
    void go_to_recruiter(GameClient& client, double now);
    void choose_spawn(GameClient& client);
    void choose_objective(const GameClient& client, double now);

    uint64_t rng_;
    BotSkill skill_;
    bool requested_ = false;  // asked to spawn since it last died
    int objective_ = -1;
    Vec3 offset_;            // where in the post it stands
    double next_objective_ = 0;
    uint32_t target_ = 0;
    double target_since_ = 0;
    double strafe_ = 1;
    double next_strafe_ = 0;
    double aim_phase_ = 0;
    double slow_since_ = -1;  // moving too slowly since
    double unstick_until_ = 0;
    double next_grenade_ = 0;
    double throw_until_ = 0;
    double grenade_pitch_ = 0;
    int queue_team_ = 0;
};

// Bots connected to a server like players, each with its own UDP socket,
// e.g. to fill the teams of a match. They're named "Bot 1", "Bot 2", ...
class BotPlayers {
public:
    using LinkFactory = std::function<std::unique_ptr<net::Link>()>;

    // `base` is each bot's client config: the server, and the soldier or
    // battlefield the server uses. Each bot gets a link from `open_link`
    // (default: a UDP socket on a free port). Throws if one can't be opened.
    BotPlayers(int count, const World& world, const ClientConfig& base, LinkFactory open_link = {});
    void update(double now);
    void disconnect();
    size_t size() const { return players_.size(); }

private:
    struct Player {
        std::unique_ptr<net::Link> link;
        GameClient client;
        Bot bot;
        Player(std::unique_ptr<net::Link> l, const World& world, ClientConfig config, uint64_t seed)
            : link(std::move(l)), client(*link, world, std::move(config)), bot(seed) {}
    };
    const World& world_;
    std::vector<std::unique_ptr<Player>> players_;
};

} // namespace swm::core
