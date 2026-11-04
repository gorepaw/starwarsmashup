// Hubs: hub files, the queue at war terminals, battles launched with bots
// filling the teams, transfers there and back, and admission by ticket.

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
    std::ofstream(dir / "trooper.ini") << "[soldier]\nName = trooper\n";
    std::ofstream(dir / "fields" / "dunes.ini") << "[battlefield]\nName = Dunes\n"
                                                   "[team]\nName = Empire\nClass = ../trooper.ini\n"
                                                   "[team]\nName = Rebellion\nClass = ../trooper.ini\n"
                                                   "[post]\nPosition = 0 0\nOwner = Empire\n"
                                                   "[post]\nPosition = 50 0\nOwner = Rebellion\n";
    std::ofstream(dir / "hub.ini") << "[hub]\nName = Mos Eisley\nBattlefield = fields/dunes.ini\nTeamSize = 4\n"
                                      "QueueTime = 7\n\n[recruiter]\nName = War Terminal\nPosition = 3460 -4835\n"
                                      "Radius = 3\n";
    const HubConfig hub = load_hub(dir / "hub.ini");
    CHECK(hub.name == "Mos Eisley" && hub.team_size == 4 && hub.queue_time == 7);
    CHECK(hub.battlefield.name == "Dunes" && hub.battlefield_id == "dunes");
    CHECK(hub.recruiters.size() == 1 && hub.recruiters[0].name == "War Terminal" && hub.recruiters[0].radius == 3);
    CHECK(hub.recruiters[0].position == Vec3(-3460, 0, -4835));
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

Battlefield small_field() {
    Battlefield field;
    field.name = "Test Field";
    field.teams[0] = {"Empire", {SoldierDef{}}};
    field.teams[1] = {"Rebellion", {SoldierDef{}}};
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
    config.battlefield = small_field();
    config.battlefield_id = "test_field";
    config.team_size = 2;
    config.queue_time = 2;
    config.return_delay = 2;
    Hub hub(hub_server, 1, world, config, network);
    int battles_started = 0;
    hub.log = [&](const std::string& line) { battles_started += line.find(": port ") != std::string::npos; };

    // Two players, driven by bots: one queues for each side.
    const auto find = [&](const std::string& name) -> std::optional<Battlefield> {
        return name == "test_field" ? std::optional(small_field()) : std::nullopt;
    };
    net::LoopbackLink alice_link{links, {10, 1}};
    net::LoopbackLink bob_link{links, {11, 1}};
    ClientSession alice(alice_link, world, ClientConfig{.server = {1, 1}, .name = "alice"}, find);
    ClientSession bob(bob_link, world, ClientConfig{.server = {1, 1}, .name = "bob"}, find);
    struct Player {
        ClientSession& session;
        int team;
        std::unique_ptr<Bot> bot;
        int generation = -1;
        bool queued = false, fought = false;
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
            p.bot->update(p.session.client(), world, now);
            p.session.update(now);
            const GameClient& client = p.session.client();
            p.queued |= client.player_status().queue == net::PlayerStatus::queued;
            p.fought |= client.match().has_value() && client.self().kills + client.self().deaths > 0;
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
    CHECK(players[0].queued && players[1].queued && battles_started == 1);
    CHECK(alice.generation() == 2 && bob.generation() == 2 && alice.error().empty());
    CHECK(players[0].fought && players[1].fought);
    CHECK(alice.client().status() == GameClient::Status::connected && !alice.client().match());
    CHECK(alice.client().recruiters().size() == 1 && alice.client().factions()[1] == "Rebellion");
    // Two players and two bots, on their chosen sides; each greeted with the outcome.
    CHECK(hub.last_result() && hub.last_result()->players.size() == 4);
    for (const PlayerResult& player : hub.last_result()->players) {
        if (player.name == "alice" || player.name == "bob") {
            CHECK(player.team == (player.name == "alice" ? 0 : 1));
        }
    }
    CHECK(players[0].notice.find("at Test Field") != std::string::npos);
    CHECK(players[1].notice.find("at Test Field") != std::string::npos);
    CHECK(hub.battle_count() == 0 && stranger && stranger->status() == GameClient::Status::refused);
}

} // namespace

int main() {
    test_hub_file();
    test_queue_battle_and_back();
    return swm::test::finish("hub_test");
}
