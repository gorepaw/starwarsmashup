// Hubs: hub files, the queue at war terminals, battles launched with bots
// filling the teams, transfers there and back, and admission by ticket.

#include <cmath>
#include <chrono>
#include <cstdio>
#include <thread>
#include <map>
#include <numbers>
#include <filesystem>
#include <fstream>
#include <memory>

#include "swbfo/core/bot.h"
#include "swbfo/core/client.h"
#include "swbfo/core/hub.h"
#include "swbfo/core/net/link.h"
#include "swbfo/core/net/protocol.h"
#include "swbfo/core/server.h"
#include "swbfo/core/world.h"
#include "test.h"

using namespace swbfo::core;

namespace {

void test_hub_file() {
    const auto dir = std::filesystem::temp_directory_path() / "swbfo_hub_test";
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
    CHECK(load_hub(dir / "plain.ini").sides == (std::vector<std::string>{"Rebellion", "Empire"}));
    std::ofstream(dir / "mixed.ini") << "[hub]\nBattlefield = fields/swamp.ini\nBattlefield = fields/clones.ini\n"
                                        "[recruiter]\nPosition = 0 0\n";
    bool refused = false;
    try {
        load_hub(dir / "mixed.ini");
    } catch (const std::runtime_error&) {
        refused = true;
    }
    CHECK(refused);

    // Stage 13: a planet's other towns, each a file of its own (Town =), read
    // with the hub's: their [town], people and services the hub's.
    std::ofstream(dir / "far.ini") << "[town]\nName = Far Town\nCenter = 100 200\nRadius = 250\n"
                                      "CloningFacility = 110 190\nCloningFacility = 90 210\n"
                                      "[crowd]\nCount = 3\nArea = 100 200 40\nTemplate = object/mobile/shared_x.iff\n"
                                      "[service]\nName = Far Shuttleport\nType = travel\nPosition = 120 220\n"
                                      "[quartermaster]\nName = Far Town Quartermaster\nPosition = 105 205\n";
    std::ofstream(dir / "towns.ini") << "[hub]\nName = Home\nSpawn = 10 20\nRadius = 500\nCloningFacility = 5 5\n"
                                        "Town = far.ini\nBattlefield = fields/dunes.ini\n"
                                        "[recruiter]\nPosition = 0 0\n"
                                        "[quartermaster]\nName = Imperial Quartermaster\nFaction = empire\nPosition = 1 1\n"
                                        "[quartermaster]\nName = Home Quartermaster\nPosition = 2 2\n"
                                        "[crowd]\nCount = 5\nArea = 10 20 30\nTemplate = object/mobile/shared_y.iff\n";
    const HubConfig towns = load_hub(dir / "towns.ini");
    CHECK(towns.towns.size() == 2 && towns.towns[0].name == "Home" && towns.towns[0].radius == 500);
    CHECK(towns.towns[0].center == Vec3(-10, 0, 20) && towns.towns[0].cloning.size() == 1);
    CHECK(towns.towns[1].name == "Far Town" && towns.towns[1].center == Vec3(-100, 0, 200) &&
          towns.towns[1].cloning.size() == 2 && towns.towns[1].radius == 250);
    CHECK(towns.crowds.size() == 2 && towns.crowd.count == 5 && towns.crowds[1].count == 3);
    CHECK(towns.vendors.size() == 1 && towns.vendors[0].kind == "travel" && towns.vendors[0].name == "Far Shuttleport");
    // Ids are the files' stems; a quartermaster without a Faction is its town's.
    CHECK(towns.towns[0].id == "towns" && towns.towns[1].id == "far");
    CHECK(towns.quartermasters.size() == 3);
    if (towns.quartermasters.size() == 3) {
        CHECK(towns.quartermasters[0].town.empty() && towns.quartermasters[0].faction == "empire");
        CHECK(towns.quartermasters[1].town == "towns" && towns.quartermasters[1].town_name == "Home");
        CHECK(towns.quartermasters[2].town == "far" && towns.quartermasters[2].town_name == "Far Town");
    }
    std::filesystem::remove_all(dir);
}

// Every town of every planet has its city quartermaster, and every city
// blueprint is sold by one (docs/CRAFTING.md, City quartermasters).
void test_city_quartermasters() {
    auto root = std::filesystem::current_path();  // (the repo's, from below it too)
    while (!std::filesystem::exists(root / "game" / "hubs" / "galaxy.ini") && root.parent_path() != root) {
        root = root.parent_path();
    }
    const auto hubs = root / "game" / "hubs";
    std::map<std::string, int> sellers;  // town id -> its city quartermasters
    size_t town_count = 0;
    for (const char* file : {"mos_eisley.ini", "theed.ini", "coronet.ini", "dearic.ini", "narmle.ini",
                             "dantooine_imperial_outpost.ini", "lok_nyms_stronghold.ini", "yavin4_mining_outpost.ini",
                             "endor_research_outpost.ini", "dathomir_science_outpost.ini"}) {
        const HubConfig hub = load_hub(hubs / file);
        for (const HubTown& town : hub.towns) {
            CHECK(!town.id.empty() && !sellers.contains(town.id));
            sellers.try_emplace(town.id, 0);
            ++town_count;
        }
        for (const net::Quartermaster& qm : hub.quartermasters) {
            if (!qm.town.empty()) ++sellers[qm.town];
        }
    }
    CHECK(town_count == 44);
    for (const auto& [town, count] : sellers) {
        if (count != 1) std::printf("%s has %d city quartermasters\n", town.c_str(), count);
        CHECK(count == 1);
    }
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    std::map<std::string, int> stock;
    for (const ItemDef& def : catalog.all()) {
        if (def.town.empty()) continue;
        CHECK(sellers.contains(def.town));
        ++stock[def.town];
    }
    // Every town sells something.
    for (const auto& [town, count] : sellers) {
        if (!stock.contains(town)) std::printf("%s sells no blueprints\n", town.c_str());
        CHECK(stock.contains(town));
    }
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
    config.battlefields = {{"test_field", small_field(), teams_for_sides(small_field(), std::array<std::string, 2>{"Empire", "Rebellion"})}};
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
    CHECK(players[0].queued && players[1].queued && battles_started == 1 && next_battle == "Test Field (Conquest)");
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
    // Stage 11: the battle's telemetry: where and how it was fought, who won,
    // the two players (not bots) and the two bots.
    CHECK(hub.telemetry().size() == 1);
    if (!hub.telemetry().empty()) {
        const MatchTelemetry& t = hub.telemetry().back();
        CHECK(t.battlefield == "test_field" && t.map == "Test Field" && t.mode == "conquest" && t.era == "gcw");
        CHECK(!t.space && t.teams[0] == hub.last_result()->teams[0] && t.winner == hub.last_result()->winner);
        CHECK(t.player_count() == 2 && t.bot_count() == 2 && t.duration > 0);
    }
}

// A hub's people: parsed from its file, made by the hub's server, walking
// (the crowd wanders, a guard walks his route, a drinker sits), and seen by
// a player with their looks.
void test_people() {
    const auto dir = std::filesystem::temp_directory_path() / "swbfo_hub_people_test";
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
    ServerConfig config;
    config.npc_sim_radius = 0;  // simulated with no player near (Stage 13 freezes them otherwise)
    GameServer server(link, world, config);
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

void test_wookiee_profile_in_battle() {
    std::vector<float> flat(4, 0.0f);
    const Heightfield ground(2, 200.0, -100, -100, flat);
    const World world(ground);

    MemoryCharacterStore store;
    store.create_character(
        {0, "Chewbacca", {0, 0, 0}, 0, "wookiee", "male", "Mos Eisley", R"({"height":1.0})"});

    Battlefield field;
    field.name = "Dunes";
    field.min_x = field.min_z = -90;
    field.max_x = field.max_z = 90;
    Team reb{"Rebellion"}, imp{"Empire"};
    Loadout l;
    l.name = "Trooper";
    reb.loadouts = {l};
    imp.loadouts = {l};
    field.teams = {reb, imp};
    field.posts = {{"North", {0, 0, -20}, 5, 0, true}, {"South", {0, 0, 20}, 5, 1, true}};

    HubConfig hub_cfg;
    hub_cfg.name = "Mos Eisley";
    hub_cfg.sides = {"Rebellion", "Empire"};
    hub_cfg.battlefields = {{"dunes", field, {0, 1}}};
    hub_cfg.recruiters = {{"Terminal", {0, 0, 0}, 10}};
    hub_cfg.team_size = 1;
    hub_cfg.queue_time = 0.05;
    hub_cfg.return_delay = 0.1;

    struct LoopNet : HubNetwork {
        net::LoopbackHub hub;
        uint16_t next_port = 100;
        std::unique_ptr<net::Link> open_server(uint16_t& port) override {
            port = next_port++;
            return std::make_unique<net::LoopbackLink>(hub, net::Endpoint{1, port});
        }
        std::unique_ptr<net::Link> open_client() override {
            return std::make_unique<net::LoopbackLink>(hub, net::Endpoint{2, next_port++});
        }
        net::Endpoint local(uint16_t port) const override { return {1, port}; }
    } net;

    uint16_t hub_port = 0;
    auto hub_link = net.open_server(hub_port);
    GameServer hub_server(*hub_link, world, ServerConfig{}, &store);
    Hub hub(hub_server, hub_port, world, hub_cfg, net);
    hub.load_battle_world = [](const Battlefield&) { return std::shared_ptr<const World>{}; };

    auto client_link = net.open_client();
    ClientSession session(*client_link, world, ClientConfig{.server = {1, hub_port}, .name = "Chewbacca"},
                          [&](const std::string&) { return field; });
    bool queued = false;
    bool saw_wookiee_in_battle = false;
    double now = 0;
    // (A millisecond of real time a tick: the hub loads the battle's world on
    // another thread, and a busy machine shouldn't leave it unfinished.)
    for (; now < 5.0; now += 1.0 / 60) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        session.update(now);
        hub_server.update(now);
        hub.update(now);
        if (session.generation() == 0 && session.client().spawned() && !queued) {
            session.client().queue(0);
            queued = true;
        }
        if (session.generation() == 1 && session.client().match() && session.client().spawned()) {
            const auto& c = session.client();
            if (c.self().species == uint8_t(Species::wookiee) && c.self().appearance == R"({"height":1.0})" &&
                c.character_stats().max_health > 100.0) {
                saw_wookiee_in_battle = true;
                break;
            }
        }
    }
    CHECK(saw_wookiee_in_battle);
}

void test_stage4_hub_loop() {
    std::vector<float> flat(4, 0.0f);
    const Heightfield ground(2, 200.0, -100, -100, flat);
    const World world(ground);

    const auto cat_path = std::filesystem::temp_directory_path() / "swbfo_stage4_hub_catalog.ini";
    {
        std::ofstream out(cat_path);
        out << "[item]\nId = wp_rifle_e11\nName = E-11\nKind = weapon\nArchetype = rifle_auto\nSlot = primary\n"
            << "Faction = neutral\nCostFactionPoints = 0\nCostCredits = 100\nModel = weapons/wp_carbine_e11/wp_carbine_e11.gltf\n"
            << "[item]\nId = wp_pistol_dh17\nName = DH-17\nKind = weapon\nArchetype = pistol\nSlot = sidearm\n"
            << "Faction = neutral\nCostFactionPoints = 0\nCostCredits = 80\nModel = weapons/wp_pistol_d18/wp_pistol_d18.gltf\n"
            << "[item]\nId = wp_grenade_c22\nName = C-22\nKind = weapon\nArchetype = grenade\nSlot = secondary\n"
            << "Faction = neutral\nCostFactionPoints = 0\nCostCredits = 60\nModel = weapons/wp_grenade_c22/wp_grenade_c22.gltf\n"
            << "[item]\nId = wp_rifle_t21\nName = T-21\nKind = weapon\nArchetype = rifle_auto\nSlot = primary\n"
            << "Faction = empire\nCostFactionPoints = 0\nCostCredits = 200\nDamageMult = 1.25\n"
            << "Model = weapons/wp_rifle_t21/wp_rifle_t21.gltf\n"
            << "[item]\nId = psg_mark1\nName = PSG Mk I\nKind = deflector_shield\nSlot = deflector_shield\n"
            << "Faction = neutral\nCostFactionPoints = 0\nCostCredits = 100\nModifier = mitigation_laser + 0.08\n"
            << "[item]\nId = cloth_jacket_rebel\nName = Jacket\nKind = clothing\nSlot = chest\nFaction = neutral\n"
            << "[item]\nId = cloth_imp_uniform\nName = Imperial Officer Jacket\nKind = clothing\nSlot = chest\n"
            << "Faction = empire\nCostFactionPoints = 50\nCostCredits = 0\n"
            << "[item]\nId = cloth_pants_cargo\nName = Pants\nKind = clothing\nSlot = leggings\nFaction = neutral\n"
            << "[item]\nId = cloth_boots_leather\nName = Boots\nKind = clothing\nSlot = boots\nFaction = neutral\n"
            // What a city blueprint teaches is a crafter's, not a gear vendor's (docs/CRAFTING.md).
            << "[blueprint]\nTown = mos_eisley\nItems = wp_pistol_dh17\n";
    }
    ItemCatalog catalog;
    catalog.load(cat_path);
    std::filesystem::remove(cat_path);

    MemoryCharacterStore store;
    const auto hero = store.create_character({0, "Luke", {0, 0, 0}, 0, "human", "male"});
    ensure_starter_kit(store, hero.id);
    // Grant a Fine T-21 directly as well as testing quartermaster buy.
    const auto hq_t21 = store.grant_item({0, hero.id, "wp_rifle_t21", ItemGrade::fine, "", "{}"});

    Battlefield field;
    field.name = "Dunes";
    field.min_x = field.min_z = -90;
    field.max_x = field.max_z = 90;
    Team reb{"Rebellion"}, imp{"Empire"};
    Loadout stock;
    stock.name = "Stock";
    reb.loadouts = {stock};
    imp.loadouts = {stock};
    field.teams = {reb, imp};
    field.posts = {{"North", {0, 0, -20}, 5, 0, true}, {"South", {0, 0, 20}, 5, 1, true}};
    field.tickets = 4;
    field.warmup = 0.2;
    field.results = 0.2;

    HubConfig hub_cfg;
    hub_cfg.name = "Mos Eisley";
    hub_cfg.sides = {"Rebellion", "Empire"};
    hub_cfg.battlefields = {{"dunes", field, {0, 1}}};
    hub_cfg.recruiters = {{"Terminal", {0, 0, 0}, 10}};
    hub_cfg.quartermasters = {{"Imperial QM", "empire", {0, 0, 0}, 10}};
    hub_cfg.vendors = {{"Mos Eisley Gear", "gear_vendor", {0, 0, 0}, 10, ""}};
    hub_cfg.range_targets = {{"Range Dummy", {0, 0, -15}, {0, 0, -15}, 0, 100, 0.0, 0.5}};
    hub_cfg.team_size = 1;
    hub_cfg.queue_time = 0.05;
    hub_cfg.return_delay = 0.1;

    struct LoopNet : HubNetwork {
        net::LoopbackHub hub;
        uint16_t next_port = 100;
        std::unique_ptr<net::Link> open_server(uint16_t& port) override {
            port = next_port++;
            return std::make_unique<net::LoopbackLink>(hub, net::Endpoint{1, port});
        }
        std::unique_ptr<net::Link> open_client() override {
            return std::make_unique<net::LoopbackLink>(hub, net::Endpoint{2, next_port++});
        }
        net::Endpoint local(uint16_t port) const override { return {1, port}; }
    } net;

    uint16_t hub_port = 0;
    auto hub_link = net.open_server(hub_port);
    ServerConfig hub_scfg;
    hub_scfg.catalog = &catalog;
    GameServer hub_server(*hub_link, world, hub_scfg, &store);
    Hub hub(hub_server, hub_port, world, hub_cfg, net);
    hub.load_battle_world = [](const Battlefield&) { return std::shared_ptr<const World>{}; };

    auto client_link = net.open_client();
    ClientSession session(*client_link, world, ClientConfig{.server = {1, hub_port}, .name = "Luke"},
                          [&](const std::string&) { return field; });

    double now = 0;
    auto step_for = [&](double secs) {
        for (double end = now + secs; now < end; now += 1.0 / 60) {
            session.update(now);
            hub_server.update(now);
            hub.update(now);
        }
    };

    step_for(0.25);
    CHECK(session.client().spawned());
    CHECK(session.client().items().size() >= 8);
    CHECK(session.client().quartermasters().size() == 1);
    CHECK(session.client().targets().size() == 1);

    // Shoot the range target until it's knocked down, then verify it respawns.
    const double start_hp = session.client().targets()[0].health;
    session.client().set_input({.yaw = 0, .pitch = 0.0, .fire = true});
    step_for(0.6);
    session.client().set_input({});
    CHECK(session.client().targets()[0].health < start_hp || !session.client().targets()[0].alive);

    // The quartermaster sells its faction's equipment for Faction Points; the Gear Vendor, gear for Credits.
    const size_t before_buy = session.client().items().size();
    session.client().buy_item("cloth_imp_uniform", true);
    step_for(0.35);
    CHECK(session.client().items().size() == before_buy + 1);
    CHECK(session.client().player_status().faction_points == 50);
    // A weapon that isn't a basic is a crafter's (from a city blueprint), not the vendor's.
    session.client().buy_item("wp_pistol_dh17", false);
    step_for(0.35);
    CHECK(session.client().items().size() == before_buy + 1);
    session.client().buy_item("wp_rifle_t21", false);
    step_for(0.35);
    CHECK(session.client().items().size() == before_buy + 2);
    CHECK(session.client().player_status().credits == 300);

    // Save Loadout 1 with the Fine T-21 and switch to it in the hub (takes 3s).
    CharacterLoadout lo1;
    lo1.slot_index = 1;
    lo1.name = "Heavy";
    lo1.primary_id = hq_t21.id;
    session.client().save_loadout(lo1);
    step_for(0.35);
    CHECK(session.client().char_loadouts().size() == 2);
    session.client().switch_loadout(1);
    step_for(3.2);
    CHECK(session.client().player_status().active_loadout == 1);
    CHECK(session.client().loadout().name == "Heavy");

    // Queue -> the match counts its grade: Fine's +8%.
    session.client().queue(0);
    bool checked_gear = false;
    for (double limit = now + 4.0; now < limit; now += 1.0 / 60) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));  // (as above)
        session.update(now);
        hub_server.update(now);
        hub.update(now);
        if (session.generation() == 1 && session.client().spawned() && !checked_gear) {
            CHECK(session.client().custom_loadouts().size() == 2);
            const auto* w = weapon_in_slot(session.client().custom_loadouts()[1], primary_slot);
            CHECK(w != nullptr && w->damage > 60.0 * 1.25 * 1.05);
            checked_gear = true;
            break;
        }
    }
    CHECK(checked_gear);
}

void test_stock_bot_roster() {
    auto root = std::filesystem::current_path();
    while (!root.empty() && !std::filesystem::exists(root / "game" / "items" / "swg_catalog.ini")) {
        auto parent = root.parent_path();
        if (parent == root) break;
        root = parent;
    }
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    const auto roster = build_stock_bot_roster(&catalog, 7);
    CHECK(roster.size() == 96);
    for (std::string_view faction : playable_factions) {
        const auto pool = roster_for_faction(roster, faction);
        CHECK(pool.size() == 24);
        bool saw_non_empty_worn = true;
        for (const BotProfileEntry* b : pool) {
            CHECK(b->loadouts.size() == size_t(max_character_loadouts));
            CHECK(b->worn_per_loadout.size() == size_t(max_character_loadouts));
            for (size_t i = 0; i < b->worn_per_loadout.size(); ++i) {
                saw_non_empty_worn &= !b->worn_per_loadout[i].empty();
                CHECK(b->loadouts[i].unified_standard_ammo < 0);
                CHECK(b->loadouts[i].unified_heavy_ammo < 0);
            }
        }
        CHECK(saw_non_empty_worn);
    }
    const auto rep = roster_for_faction(roster, "republic");
    CHECK(!rep.empty() && rep[0]->worn_per_loadout[0].find("rep_inf_") != std::string::npos);
    const auto cis = roster_for_faction(roster, "separatists");
    CHECK(!cis.empty() && cis[0]->worn_per_loadout[0].find("cis_inf_") != std::string::npos);
}

int main() {
    test_hub_file();
    test_city_quartermasters();
    test_creature_body();
    test_queue_battle_and_back();
    test_people();
    test_wookiee_profile_in_battle();
    test_stage4_hub_loop();
    test_stock_bot_roster();
    return swbfo::test::finish("hub_test");
}
