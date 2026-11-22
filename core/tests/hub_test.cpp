// Hubs: hub files, the queue at war terminals, battles launched with bots
// filling the teams, transfers there and back, and admission by ticket.

#include <cmath>
#include <map>
#include <numbers>
#include <filesystem>
#include <fstream>
#include <memory>

#include "swm/core/bot.h"
#include "swm/core/client.h"
#include "swm/core/hub.h"
#include "swm/core/net/link.h"
#include "swm/core/net/protocol.h"
#include "swm/core/server.h"
#include "swm/core/world.h"
#include "test.h"

using namespace swm::core;

namespace {

void test_hub_file() {
    const auto dir = std::filesystem::temp_directory_path() / "swm_hub_test";
    std::filesystem::create_directories(dir / "fields");
    std::filesystem::create_directories(dir / "items");
    std::filesystem::create_directories(dir / "loadouts");
    std::ofstream(dir / "items" / "rifle.ini") << "[weapon]\nName = rifle\n";
    std::ofstream(dir / "loadouts" / "trooper.ini") << "[loadout]\nPrimary = rifle\n";
    std::ofstream(dir / "fields" / "dunes.ini") << "[battlefield]\nName = Dunes\n"
                                                   "[team]\nName = Empire\nLoadout = ../loadouts/trooper.ini\n"
                                                   "[team]\nName = Rebellion\nLoadout = ../loadouts/trooper.ini\n"
                                                   "[post]\nPosition = 0 0\nOwner = Empire\n"
                                                   "[post]\nPosition = 50 0\nOwner = Rebellion\n";
    // The Rebellion first here, as on most of Battlefront's maps.
    std::ofstream(dir / "fields" / "swamp.ini") << "[battlefield]\nName = Swamp\n"
                                                   "[team]\nName = Rebellion\nLoadout = ../loadouts/trooper.ini\n"
                                                   "[team]\nName = Empire\nLoadout = ../loadouts/trooper.ini\n"
                                                   "[post]\nPosition = 0 0\nOwner = Empire\n"
                                                   "[post]\nPosition = 50 0\nOwner = Rebellion\n";
    std::ofstream(dir / "fields" / "clones.ini") << "[battlefield]\nName = Arena\n"
                                                    "[team]\nName = Republic\nLoadout = ../loadouts/trooper.ini\n"
                                                    "[team]\nName = Separatists\nLoadout = ../loadouts/trooper.ini\n"
                                                    "[post]\nPosition = 0 0\nOwner = Republic\n"
                                                    "[post]\nPosition = 50 0\nOwner = Separatists\n";
    std::ofstream(dir / "hub.ini") << "[hub]\nName = Mos Eisley\nSides = rebellion , Empire\n"
                                      "Battlefield = fields/dunes.ini\nBattlefield = fields/swamp.ini\nTeamSize = 4\n"
                                      "QueueTime = 7\n\n[recruiter]\nName = War Terminal\nPosition = 3460 -4835\n"
                                      "Radius = 3\n";
    const HubConfig hub = load_hub(dir / "hub.ini");
    CHECK(hub.name == "Mos Eisley" && hub.team_size == 4 && hub.queue_time == 7);
    CHECK(hub.sides[0] == "rebellion" && hub.sides[1] == "Empire");
    CHECK(hub.battlefields.size() == 2 && hub.battlefields[0].field.name == "Dunes" && hub.battlefields[0].id == "dunes");
    // Each battlefield's teams for the sides, by name.
    CHECK(hub.battlefields[0].teams == (std::array<int, 2>{1, 0}));
    CHECK(hub.battlefields[1].id == "swamp" && hub.battlefields[1].teams == (std::array<int, 2>{0, 1}));
    CHECK(hub.recruiters.size() == 1 && hub.recruiters[0].name == "War Terminal" && hub.recruiters[0].radius == 3);
    CHECK(hub.recruiters[0].position == Vec3(-3460, 0, -4835));
    // Without Sides, the first battlefield's teams; a battlefield without a side's team is refused.
    std::ofstream(dir / "plain.ini") << "[hub]\nBattlefield = fields/swamp.ini\n[recruiter]\nPosition = 0 0\n";
    CHECK(load_hub(dir / "plain.ini").sides == (std::array<std::string, 2>{"Rebellion", "Empire"}));
    std::ofstream(dir / "mixed.ini") << "[hub]\nBattlefield = fields/swamp.ini\nBattlefield = fields/clones.ini\n"
                                        "[recruiter]\nPosition = 0 0\n";
    bool refused = false;
    try {
        load_hub(dir / "mixed.ini");
    } catch (const std::runtime_error&) {
        refused = true;
    }
    CHECK(refused);
    std::filesystem::remove_all(dir);
}

// In-memory links: battle servers on address 1 (the hub's), bots elsewhere.
class TestNetwork : public HubNetwork {
public:
    explicit TestNetwork(net::LoopbackHub& hub) : hub_(hub) {}
    std::unique_ptr<net::Link> open_server(uint16_t& port) override {
        port = next_port_++;
        return std::make_unique<net::LoopbackLink>(hub_, net::Endpoint{1, port});
    }
    std::unique_ptr<net::Link> open_client() override {
        return std::make_unique<net::LoopbackLink>(hub_, net::Endpoint{next_client_++, 1});
    }
    net::Endpoint local(uint16_t port) const override { return {1, port}; }

private:
    net::LoopbackHub& hub_;
    uint16_t next_port_ = 100;
    uint32_t next_client_ = 1000;
};

// Its teams the other way round from the hub's sides (Empire, Rebellion).
Battlefield small_field() {
    Battlefield field;
    field.name = "Test Field";
    field.teams[0] = {"Rebellion", {Loadout{}}};
    field.teams[1] = {"Empire", {Loadout{}}};
    field.min_x = -100;
    field.max_x = 100;
    field.min_z = -60;
    field.max_z = 60;
    field.posts = {{.name = "West", .position = {-40, 0, 0}, .owner = 0},
                   {.name = "Middle", .position = {0, 0, 0}},
                   {.name = "East", .position = {40, 0, 0}, .owner = 1}};
    field.tickets = 6;
    field.bleed = 0.5;
    field.warmup = 1;
    field.results = 2;
    return field;
}

void test_queue_battle_and_back() {
    Heightfield ground{401, 1.0, -200, -200, std::vector<float>(401 * 401, 0.0f)};
    World world{ground};
    net::LoopbackHub links;
    TestNetwork network(links);
    net::LoopbackLink hub_link{links, {1, 1}};
    ServerConfig hub_config;
    hub_config.spawn = {0, 0, 0};
    GameServer hub_server(hub_link, world, hub_config);
    HubConfig config;
    config.name = "Test Hub";
    config.recruiters = {{"Terminal", {0, 0, 20}, 4}};
    config.sides = {"Empire", "Rebellion"};
    config.battlefields = {{"test_field", small_field(), teams_for_sides(small_field(), config.sides)}};
    config.team_size = 2;
    config.queue_time = 2;
    config.return_delay = 2;
    Hub hub(hub_server, 1, world, config, network);
    // The battle is fought in a world of its own: ground 10 m up. The
    // server's and the clients' are loaded separately, as they would be.
    Heightfield battle_ground{401, 1.0, -200, -200, std::vector<float>(401 * 401, 10.0f)};
    int worlds_loaded = 0;
    hub.load_battle_world = [&](const Battlefield& field) -> std::shared_ptr<const World> {
        ++worlds_loaded;
        return field.name == "Test Field" ? std::make_shared<World>(battle_ground) : nullptr;
    };
    int battles_started = 0;
    std::string next_battle;
    hub.log = [&](const std::string& line) { battles_started += line.find(": port ") != std::string::npos; };

    // Two players, driven by bots: one queues for each side.
    const auto find = [&](const std::string& name) -> std::optional<Battlefield> {
        return name == "test_field" ? std::optional(small_field()) : std::nullopt;
    };
    int client_worlds = 0;
    const auto find_world = [&](const Battlefield&) -> std::shared_ptr<const World> {
        ++client_worlds;
        return std::make_shared<World>(battle_ground);
    };
    net::LoopbackLink alice_link{links, {10, 1}};
    net::LoopbackLink bob_link{links, {11, 1}};
    ClientSession alice(alice_link, world, ClientConfig{.server = {1, 1}, .name = "alice"}, find, find_world);
    ClientSession bob(bob_link, world, ClientConfig{.server = {1, 1}, .name = "bob"}, find, find_world);
    struct Player {
        ClientSession& session;
        int team;
        std::unique_ptr<Bot> bot;
        int generation = -1;
        bool queued = false, fought = false;
        double battle_height = -1;  // where it stood in the battle
        std::string notice;
    };
    Player players[] = {{alice, 0, nullptr}, {bob, 1, nullptr}};

    // A stranger without a ticket can't get into a battle.
    net::LoopbackLink stranger_link{links, {12, 1}};
    std::optional<GameClient> stranger;

    double now = 0;
    for (; now < 400; now += 1.0 / 60) {
        for (Player& p : players) {
            if (p.generation != p.session.generation()) {
                p.generation = p.session.generation();
                p.bot = std::make_unique<Bot>(uint64_t(p.team + 7));
                p.bot->set_queue_team(p.team);
            }
            p.bot->update(p.session.client(), p.session.world(), now);
            p.session.update(now);
            const GameClient& client = p.session.client();
            p.queued |= client.player_status().queue == net::PlayerStatus::queued;
            if (!client.player_status().battlefield.empty()) {
                next_battle = client.player_status().battlefield;
            }
            p.fought |= client.match().has_value() && client.self().kills + client.self().deaths > 0;
            if (client.match() && client.self().life == Life::alive && client.self().state.grounded) {
                p.battle_height = client.self().state.position.y;  // on the ground (bots hop when stuck)
            }
            if (!client.player_status().notice.empty()) {
                p.notice = client.player_status().notice;
            }
        }
        hub_server.update(now);
        hub.update(now);
        if (!stranger && hub.battle_count() == 1) {
            stranger.emplace(stranger_link, world, ClientConfig{.server = {1, 100}, .name = "stranger"});
        }
        if (stranger) {
            stranger->update(now);
        }
        if (!alice.client().match() && !bob.client().match() && !players[0].notice.empty() &&
            !players[1].notice.empty() && hub.battle_count() == 0) {
            break;
        }
    }
    std::printf("hub: back after %.0f s; alice: \"%s\", bob: \"%s\"\n", now, players[0].notice.c_str(),
                players[1].notice.c_str());
    // Both queued, went to the same battle, fought, and came back to the hub.
    CHECK(players[0].queued && players[1].queued && battles_started == 1 && next_battle == "Test Field");
    // In the battle's world, on the server and the clients; back at the hub, in its own.
    CHECK(worlds_loaded == 1 && client_worlds == 2);
    CHECK(std::abs(players[0].battle_height - 10) < 0.01 && std::abs(players[1].battle_height - 10) < 0.01);
    CHECK(&alice.world() == &world && std::abs(alice.client().self().state.position.y) < 0.01);
    CHECK(alice.generation() == 2 && bob.generation() == 2 && alice.error().empty());
    CHECK(players[0].fought && players[1].fought);
    CHECK(alice.client().status() == GameClient::Status::connected && !alice.client().match());
    CHECK(alice.client().recruiters().size() == 1 && alice.client().factions()[1] == "Rebellion");
    // Two players and two bots, on their chosen sides (alice the Empire, the
    // battlefield's second team); each greeted with the outcome.
    CHECK(hub.last_result() && hub.last_result()->players.size() == 4);
    for (const PlayerResult& player : hub.last_result()->players) {
        if (player.name == "alice" || player.name == "bob") {
            CHECK(player.team == (player.name == "alice" ? 1 : 0));
        }
    }
    CHECK(players[0].notice.find("at Test Field") != std::string::npos);
    CHECK(players[1].notice.find("at Test Field") != std::string::npos);
    CHECK(hub.battle_count() == 0 && stranger && stranger->status() == GameClient::Status::refused);
}

// A hub's people: parsed from its file, made by the hub's server, walking
// (the crowd wanders, a guard walks his route, a drinker sits), and seen by
// a player with their looks.
void test_people() {
    const auto dir = std::filesystem::temp_directory_path() / "swm_hub_people_test";
    std::filesystem::create_directories(dir / "fields");
    std::filesystem::create_directories(dir / "loadouts");
    std::filesystem::create_directories(dir / "items");
    std::ofstream(dir / "items" / "rifle.ini") << "[weapon]\nName = rifle\n";
    std::ofstream(dir / "loadouts" / "trooper.ini") << "[loadout]\nPrimary = rifle\n";
    std::ofstream(dir / "fields" / "dunes.ini") << "[battlefield]\nName = Dunes\n"
                                                   "[team]\nName = Empire\nLoadout = ../loadouts/trooper.ini\n"
                                                   "[team]\nName = Rebellion\nLoadout = ../loadouts/trooper.ini\n"
                                                   "[post]\nPosition = 0 0\nOwner = Empire\n"
                                                   "[post]\nPosition = 50 0\nOwner = Rebellion\n";
    std::ofstream(dir / "hub.ini") << "[hub]\nBattlefield = fields/dunes.ini\n[recruiter]\nPosition = 0 20\n"
                                      "[npc]\nName = Guard\nTemplate = object/mobile/shared_guard.iff\n"
                                      "Position = 10 10\nRoute = 10 10; -10 10\nRadius = 1.7\n"
                                      "[npc]\nName = Drinker\nTemplate = object/mobile/shared_commoner.iff\n"
                                      "Position = 0 -10\nYaw = 90\nPose = sit\n"
                                      "[crowd]\nCount = 6\nArea = 0 0 30\nTemplate = object/mobile/shared_commoner.iff\n"
                                      "Template = object/mobile/shared_twilek.iff\n";
    const HubConfig config = load_hub(dir / "hub.ini");
    std::filesystem::remove_all(dir);
    CHECK(config.looks.size() == 3 && config.looks[0] == "object/mobile/shared_guard.iff");
    CHECK(config.people.size() == 2 && config.people[0].route.size() == 2 && config.people[0].route[1] == Vec3(10, 0, 10));
    CHECK(config.people[1].pose == 1 && config.people[1].look == 1);
    CHECK(config.people[0].radius == 1.7 && config.people[1].radius == 0);  // a creature's body; a person's
    // Galaxies' yaw 90 faces its +X, the game's -X: a game yaw of pi/2.
    CHECK(std::abs(config.people[1].yaw - std::numbers::pi / 2) < 1e-9);
    CHECK(config.crowd.count == 6 && config.crowd.looks == (std::vector<int>{1, 2}) && config.crowd.radius == 30);

    Heightfield ground{201, 1.0, -100, -100, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    net::LoopbackHub links;
    TestNetwork network(links);
    net::LoopbackLink hub_link{links, {1, 1}};
    ServerConfig server_config;
    GameServer server(hub_link, world, server_config);
    Hub hub(server, 1, world, config, network);
    hub.update(0);
    CHECK(hub.people_count() == 8 && server.npcs().size() == 8);

    net::LoopbackLink player_link{links, {10, 1}};
    GameClient player(player_link, world, ClientConfig{.server = {1, 1}, .name = "visitor"});
    std::map<uint32_t, Vec3> first;
    double now = 0;
    for (; now < 40; now += 1.0 / 30) {
        player.update(now);
        server.update(now);
        hub.update(now);
        if (first.empty() && now > 0.5) {
            for (const uint32_t id : server.npcs()) {
                first[id] = server.state_of(id)->position;
            }
        }
    }
    // The crowd and the guard got somewhere; the drinker sat where he was.
    int moved = 0;
    for (const auto& [id, at] : first) {
        Vec3 d = server.state_of(id)->position - at;
        d.y = 0;
        moved += d.length() > 2;
    }
    std::printf("hub people: %d of %zu moved in 40 s\n", moved, first.size());
    CHECK(moved >= 5);
    // The player sees them, and how they look.
    CHECK(player.looks().size() == 3 && player.looks()[2] == "object/mobile/shared_twilek.iff");
    int seen = 0, sitting = 0, varied = 0;
    for (const auto& remote : player.remotes()) {
        seen += remote.look > 0;
        sitting += remote.pose == 1;
        varied += remote.colors != 0;
    }
    CHECK(seen == 8 && sitting == 1);
    CHECK(varied == 6);  // the crowd, each in colors of their own; the set pieces in their looks'
}

} // namespace

// A creature's body (add_npc's radius) keeps it further from a wall than a
// person's: both walk east into a wall at x = 10.
void test_creature_body() {
    Heightfield ground{201, 1.0, -100, -100, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    MapCollision wall;
    const Vec3 lo{10, 0, -20}, hi{12, 3, 20};
    const auto corner = [&](int i) { return Vec3{i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z}; };
    const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (const auto& f : faces) {
        wall.triangles.push_back({corner(f[0]), corner(f[2]), corner(f[1])});
        wall.triangles.push_back({corner(f[0]), corner(f[3]), corner(f[2])});
    }
    world.add_map_collision(wall);
    net::LoopbackHub links;
    net::LoopbackLink link{links, {1, 1}};
    GameServer server(link, world, ServerConfig{});
    const uint32_t person = server.add_npc("Citizen", {0, 0, -5}, 0, 1);
    const uint32_t bantha = server.add_npc("Bantha", {0, 0, 5}, 0, 2, 0, 0, 1.7);
    MoveInput east;
    east.forward = 127;
    east.yaw = float(-std::numbers::pi / 2);
    server.steer_npc(person, east);
    server.steer_npc(bantha, east);
    for (double now = 0; now < 10; now += 1.0 / 30) {
        server.update(now);
    }
    const double person_x = server.state_of(person)->position.x, bantha_x = server.state_of(bantha)->position.x;
    std::printf("against the wall: a person at x %.2f, a bantha at %.2f\n", person_x, bantha_x);
    CHECK(person_x > 9.5 && person_x < 10);
    CHECK(std::abs(bantha_x - (10 - 1.7)) < 0.01);
}

int main() {
    test_hub_file();
    test_creature_body();
    test_queue_battle_and_back();
    test_people();
    return swm::test::finish("hub_test");
}
