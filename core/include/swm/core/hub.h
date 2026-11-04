#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "swm/core/bot.h"
#include "swm/core/conquest.h"
#include "swm/core/net/link.h"
#include "swm/core/net/protocol.h"
#include "swm/core/server.h"

namespace swm::core {

class World;

struct HubConfig {
    std::string name = "Hub";
    std::vector<net::Recruiter> recruiters;
    Battlefield battlefield;     // where its battles are fought
    std::string battlefield_id;  // the battlefield file's name without .ini: clients load the same file
    int team_size = 8;           // soldiers per team; bots fill the places players don't
    double queue_time = 10;      // seconds from the first player queueing to the battle starting
    double return_delay = 5;     // seconds after a battle's results before it closes
};

// Reads a hub file (see game/hubs/): its war terminals (Galaxies x z) and
// the battlefield file it launches battles on, relative to it. Throws
// std::runtime_error if it, or the battlefield, can't be read.
HubConfig load_hub(const std::filesystem::path& path);

// How a hub opens sockets for the battle servers it launches and their
// bots: UDP on this machine, or in-memory links in tests.
class HubNetwork {
public:
    virtual ~HubNetwork() = default;
    // A server socket on a free port; sets `port`.
    virtual std::unique_ptr<net::Link> open_server(uint16_t& port) = 0;
    virtual std::unique_ptr<net::Link> open_client() = 0;
    // How a client on this machine reaches a local port.
    virtual net::Endpoint local(uint16_t port) const = 0;
};

class UdpHubNetwork : public HubNetwork {
public:
    std::unique_ptr<net::Link> open_server(uint16_t& port) override;
    std::unique_ptr<net::Link> open_client() override;
    net::Endpoint local(uint16_t port) const override;
};

// A hub: an open-world server (Mos Eisley) whose war terminals queue players
// for battles. When the first player in the queue has waited `queue_time`,
// the hub launches a battle server (a single-match GameServer, in this
// process for now) with bots filling the teams, and transfers the queued
// players to it with tickets; later players join it while it's on. When the
// match's results have shown, everyone is transferred back and greeted with
// how it went.
class Hub {
public:
    // `server` is the hub's own game server and `port` where clients reach
    // it. Both must outlive the hub.
    Hub(GameServer& server, uint16_t port, const World& world, HubConfig config, HubNetwork& network);
    ~Hub();
    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;

    // Runs the queue and the battles. Call after server.update(now).
    void update(double now);
    size_t battle_count() const { return battles_.size(); }
    // The last battle's result, if one has ended.
    const std::optional<MatchResult>& last_result() const { return last_result_; }

    std::function<void(const std::string&)> log;

private:
    struct Queued {
        uint32_t entity = 0;
        std::string name;
        int team = 0;
        double since = 0;
    };
    struct Battle {
        int id = 0;
        uint16_t port = 0;
        std::unique_ptr<net::Link> link;
        std::unique_ptr<GameServer> server;
        std::unique_ptr<BotPlayers> bots;
        std::map<uint64_t, std::pair<std::string, int>> tickets;  // -> player and team
        uint64_t bot_ticket = 0;
        std::array<int, 2> soldiers{};  // sent or admitted per team
        std::array<int, 2> players{};   // players (not bots) sent per team
        double over_since = -1;
        double last_player = 0;  // when a player was last connected
    };

    void queue(uint32_t entity, int team);
    void start_battle();
    void send(Battle& battle, const Queued& player);
    bool has_room(const Battle& battle, int team) const;
    bool players_connected(const Battle& battle) const;
    void say(const std::string& text) const;

    GameServer& server_;
    uint16_t port_;
    const World& world_;
    HubConfig config_;
    HubNetwork& network_;
    std::vector<Queued> queue_;
    std::vector<std::unique_ptr<Battle>> battles_;
    std::map<std::string, std::string> notices_;  // player -> how their battle went, for when they're back
    std::optional<MatchResult> last_result_;
    std::mt19937_64 random_{std::random_device{}()};
    int next_battle_ = 1;
    double now_ = 0;
};

} // namespace swm::core
