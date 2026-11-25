#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "swm/core/bot.h"
#include "swm/core/bot_roster.h"
#include "swm/core/crafting.h"
#include "swm/core/loot.h"
#include "swm/core/nav.h"
#include "swm/core/conquest.h"
#include "swm/core/net/link.h"
#include "swm/core/net/protocol.h"
#include "swm/core/server.h"
#include "swm/core/telemetry.h"
#include "swm/core/wildlife.h"

namespace swm::core {

class World;

// A battlefield a hub's battles can be fought on.
struct HubBattlefield {
    std::string id;                  // its battle id (battlefield_id): clients load the same file, in the same mode
    Battlefield field;               // set up for its own mode (conquest or space assault); a battle's for its mode
    std::array<int, 2> teams{0, 1};  // the battlefield's team indices matching `sides`
    std::array<int, 2> sides{0, 1};  // indices into HubConfig::sides for this battlefield's two sides
    std::filesystem::path path;      // its file, for loading it in its other modes (Stage 11)
};

// One of a hub's people (an [npc]): standing or sitting somewhere, or
// walking a route back and forth.
struct HubPerson {
    std::string name;
    int look = 0;              // index into HubConfig::looks
    Vec3 position;             // game space
    double yaw = 0;            // game yaw (Godot's rotation.y: 0 faces -Z)
    uint8_t pose = 0;          // 0 standing, 1 sitting
    std::vector<Vec3> route;   // if walking: the points walked to in turn, game space
    double radius = 0;         // its body's, meters (a creature's); 0 for a person's
};

// A hub's crowd (its [crowd]): people who wander its streets, walking from
// one place to another in an area and lingering there.
struct HubCrowd {
    int count = 0;
    std::vector<int> looks;  // indices into HubConfig::looks, taken in turn
    Vec3 center;             // game space
    double radius = 0;       // meters
};

struct HubRangeTarget {
    std::string name = "Target";
    Vec3 position;
    Vec3 patrol_end;
    double speed = 0;
    double health = 400;
    double mitigation = 0;
    double respawn = 2.5;
};

// One of a planet's towns (Stage 13). The hub file is its first (Name,
// Spawn, CloningFacility in [hub]); each `Town = <file>` names another, whose
// [town] section gives its Name, Center x z, Radius in meters and
// CloningFacility x z (repeatable), and whose other sections (people, crowd,
// terminals, services) are the hub's like the hub file's own.
struct HubTown {
    std::string name;
    Vec3 center;                // game space
    double radius = 400;        // meters
    std::vector<Vec3> cloning;  // its cloning facilities, game space
};

struct HubConfig {
    std::string name = "Hub";
    std::vector<HubTown> towns;  // the hub's own town first
    // Its planet's terrain bake's name (e.g. "naboo"): players coming back
    // from a battle are sent to this planet's world (Stage 13). Empty: the home one.
    std::string planet;
    std::optional<Vec3> spawn;
    std::optional<Vec3> cloning_facility;
    std::vector<net::Recruiter> recruiters;
    std::vector<net::Quartermaster> quartermasters;
    std::vector<net::Vendor> vendors;
    std::vector<HubRangeTarget> range_targets;
    // The sides players queue for (2 factions, or 4 covering both GCW and Clone Wars).
    std::vector<std::string> sides;
    std::vector<HubBattlefield> battlefields;  // each battle is on one, at random
    int team_size = 8;           // soldiers per team; bots fill the places players don't (XL battles: theirs)
    BotSkill bot_skill;          // theirs (the players' difficulty setting, when they host)
    double queue_time = 10;      // seconds from the first player queueing to the battle starting
    double return_delay = 5;     // seconds after a battle's results before it closes
    // Its people: how they look (Galaxies object templates), the set pieces
    // and the crowd.
    std::vector<std::string> looks;
    std::vector<HubPerson> people;
    HubCrowd crowd;                // the first [crowd]
    std::vector<HubCrowd> crowds;  // every [crowd] (a town's each), the first included
    std::optional<WorldSpawns> spawns;
    LootTableSet loot_tables;
    // Its quests (Quests = a file of them) and mission terminals
    // ([mission_terminal]: Name, Position x z, Radius).
    QuestBook quests;
    std::vector<net::MissionTerminal> mission_terminals;
    CraftingBook crafting;
    std::vector<net::Workbench> workbenches;
};

// Reads a hub file (see game/hubs/): its war terminals (Galaxies x z), the
// sides, the battlefield files it launches battles on (Battlefield =,
// repeated), relative to it, and its people ([npc]: Template, Position x z,
// Yaw in Galaxies degrees, Pose stand or sit, Route "x z; x z; ...", Radius
// in meters (a creature's body: its template's collisionRadius); [crowd]:
// Count, Template repeated, Area x z radius). Throws std::runtime_error if it, or a
// battlefield, can't be read, or a battlefield has no team for a side.
HubConfig load_hub(const std::filesystem::path& path);

// Which of a battlefield's teams is each side (by name, ignoring case).
// Throws std::runtime_error if one isn't there.
std::array<int, 2> teams_for_sides(const Battlefield& field, const std::array<std::string, 2>& sides);

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
// for battles. When the first player queues, the hub picks the next battle's
// battlefield (at random, not the last one again) and loads its world in the
// background. Once that player has waited `queue_time` and the world is
// loaded, it launches a battle server (a single-match GameServer, in this
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

    // Runs the queue and the battles, and walks the hub's people. Call after
    // server.update(now).
    void update(double now);
    // Its people (made on the first update).
    size_t people_count() const { return walkers_.size() + standing_; }
    size_t battle_count() const { return battles_.size(); }
    // Its wildlife and bandits (null: none, or not yet made).
    const WorldPopulation* population() const { return population_.get(); }
    // The bots' skill in battles from now on, and in those being fought.
    void set_bot_skill(BotSkill skill);
    // The last battle's result, if one has ended.
    const std::optional<MatchResult>& last_result() const { return last_result_; }
    // The last battles' telemetry, oldest first (also recorded in the
    // server's store, if it has one).
    const std::vector<MatchTelemetry>& telemetry() const { return telemetry_; }

    std::function<void(const std::string&)> log;
    // Loads the world a battle on a battlefield is fought in, on another
    // thread; null for the hub's own. By default: the battlefield's terrain
    // bake and the map collision next to it (load_world), or the hub's own
    // world when it names none.
    std::function<std::shared_ptr<const World>(const Battlefield&)> load_battle_world;

private:
    struct Queued {
        uint32_t entity = 0;
        std::string name;
        int team = 0;
        double since = 0;
    };
    // A queue (Stage 11): the two sides it's between (an era: Galactic
    // Civil War or Clone Wars), ground or space battles, and the game mode
    // wanted (net::QueueRequest::any_mode: any the battlefield offers).
    struct QueueKey {
        std::pair<int, int> sides{0, 1};
        bool space = false;
        uint8_t mode = 0;
        auto operator<=>(const QueueKey&) const = default;
    };
    struct Battle {
        std::shared_ptr<const World> world;  // null: the hub's; outlives the server and bots below
        HubBattlefield field;
        int id = 0;
        uint16_t port = 0;
        std::unique_ptr<net::Link> link;
        std::unique_ptr<GameServer> server;
        std::unique_ptr<BotPlayers> bots;
        std::map<uint64_t, std::pair<std::string, int>> tickets;  // -> player and team
        std::map<std::string, PlayerProfile> profiles;            // player name -> species & appearance
        std::map<std::string, std::vector<Loadout>> player_loadouts;
        std::map<std::string, std::vector<std::string>> player_worn;
        uint64_t bot_ticket = 0;
        std::array<int, 2> soldiers{};  // sent or admitted per team
        std::array<int, 2> players{};   // players (not bots) sent per team
        int team_size = 8;              // the hub's, or the mode's (XL)
        QueueKey key;                   // the queue it was launched for: later players of it join
        double started = 0;             // when it was launched (telemetry)
        double over_since = -1;
        double last_player = 0;  // when a player was last connected
    };
    // The next battle's battlefield, and its world loading.
    struct Next {
        size_t battlefield = 0;  // index into config_.battlefields
        HubBattlefield chosen;   // it, set up for the queue's mode
        std::shared_future<std::shared_ptr<const World>> world;
    };

    void queue(uint32_t entity, int team, uint8_t mode, uint8_t terminal);
    // Someone who walks: the crowd wandering, or a set piece's route.
    struct Walker {
        uint32_t entity = 0;
        std::vector<Vec3> route;      // a set piece's points; empty for the crowd
        size_t crowd = 0;             // which crowd it wanders with (HubConfig::crowds)
        size_t route_index = 0;
        std::vector<Vec3> path;       // corners to the current goal
        size_t next = 0;
        double pause_until = 0;
        Vec3 checked;                 // where it was when progress was last checked
        double checked_at = 0;
    };
    void add_people();
    void walk_people();
    std::optional<Vec3> wander_point(size_t crowd);
    static bool offers(const HubBattlefield& bf, const QueueKey& key);
    void record_telemetry(const Battle& battle, const MatchResult& result);
    void choose_next(const QueueKey& key);
    bool next_ready(const QueueKey& key);
    void start_battle(const QueueKey& key);
    void send(Battle& battle, const Queued& player);
    bool has_room(const Battle& battle, int side) const;
    bool players_connected(const Battle& battle) const;
    std::pair<int, int> queue_pair_for_side(int side) const;
    void say(const std::string& text) const;

    GameServer& server_;
    uint16_t port_;
    const World& world_;
    HubConfig config_;
    HubNetwork& network_;
    std::map<QueueKey, std::vector<Queued>> queues_;
    std::vector<std::unique_ptr<Battle>> battles_;
    std::map<QueueKey, Next> next_;  // after config_: its loading refers to a battlefield there
    std::string last_battlefield_;
    std::map<std::string, std::weak_ptr<const World>> worlds_;  // battlefield id -> loaded world
    std::unique_ptr<NavGrid> streets_;  // where the hub's people walk
    std::unique_ptr<WorldPopulation> population_;
    std::vector<Walker> walkers_;
    size_t standing_ = 0;
    bool people_added_ = false;
    uint32_t steered_tick_ = UINT32_MAX;  // the server tick people were last steered on
    std::map<std::string, std::string> notices_;  // player -> how their battle went, for when they're back
    std::optional<MatchResult> last_result_;
    std::vector<MatchTelemetry> telemetry_;  // the last battles' (Stage 11), oldest first
    std::vector<BotProfileEntry> bot_roster_;
    std::mt19937_64 random_{std::random_device{}()};
    uint64_t loot_rng_ = 0x4C4F4F5448554231ull;
    int next_battle_ = 1;
    double now_ = 0;
};

} // namespace swm::core
