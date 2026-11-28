// Stage 13: planets, each a hub on a world server of its own, and starships
// between their starports.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "swm/core/client.h"
#include "swm/core/galaxy.h"
#include "swm/core/hub.h"
#include "swm/core/items.h"
#include "swm/core/net/link.h"
#include "swm/core/server.h"
#include "swm/core/sqlite_store.h"
#include "swm/core/world.h"
#include "test.h"

using namespace swm::core;

namespace {

std::filesystem::path repo_root() {
    auto dir = std::filesystem::current_path();
    for (int i = 0; i < 6 && !std::filesystem::exists(dir / "game" / "items" / "swg_catalog.ini"); ++i) {
        dir = dir.parent_path();
    }
    return dir;
}

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

// A flat square bake, 400 m across around (0, 0), at a height.
void write_bake(const std::filesystem::path& prefix, float height) {
    std::ofstream(prefix.string() + ".json") << "{\"samples\": 101, \"spacing\": 4, \"origin_x\": -200, \"origin_z\": -200}";
    std::vector<float> heights(101 * 101, height);
    std::ofstream(prefix.string() + ".r32", std::ios::binary)
        .write(reinterpret_cast<const char*>(heights.data()), std::streamsize(heights.size() * sizeof(float)));
}

void test_starships_between_planets() {
    const auto root = repo_root();
    const auto dir = std::filesystem::temp_directory_path() / "swm_galaxy_test";
    std::filesystem::remove_all(dir);
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
    write_bake(dir / "home", 0);
    write_bake(dir / "far", 10);
    std::ofstream(dir / "home.ini") << "[hub]\nName = Home Port\nSpawn = 0 0\nSides = Empire, Rebellion\n"
                                       "Battlefield = fields/dunes.ini\n"
                                       "[recruiter]\nName = Terminal\nPosition = 100 100\n"
                                       "[service]\nName = Home Starport\nType = starport\nPosition = 0 0\nRadius = 6\n";
    // Its battles borrowed from the home hub's file.
    std::ofstream(dir / "far.ini") << "[hub]\nName = Far Town\nSpawn = 50 50\nBattlefieldsFrom = home.ini\n"
                                      "[recruiter]\nName = Terminal\nPosition = 100 100\n"
                                      "[service]\nName = Far Starport\nType = starport\nPosition = 60 60\nRadius = 6\n";
    std::ofstream(dir / "galaxy.ini") << "[planet]\nName = Home\nBake = home\nHub = home.ini\n"
                                         "[planet]\nName = Far\nBake = far\nHub = far.ini\n"
                                         "[fare]\nFrom = Home\nTo = Far\nCredits = 150\n";

    const GalaxyConfig config = load_galaxy(dir / "galaxy.ini");
    CHECK(config.planets.size() == 2 && config.planets[1].stem() == "far" && config.fare("far", "HOME") == 150);
    const HubConfig far_hub = load_hub(dir / "far.ini");
    CHECK(far_hub.battlefields.size() == 1 && far_hub.sides.size() == 2);  // BattlefieldsFrom

    {  // (everything holding the database closed before it's removed)
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    const auto db = dir / "galaxy.sqlite";
    SqliteCharacterStore store(db);
    net::LoopbackHub links;
    TestNetwork network(links);
    const uint16_t home_port = 1;
    net::LoopbackLink home_link(links, {1, home_port});
    const auto home_world = load_world(dir / "home");
    ServerConfig base;
    base.catalog = &catalog;
    base.pvp_enabled = false;
    ServerConfig home_config = base;
    home_config.planet = "Home";
    home_config.spawn = {0, 0, 0};
    GameServer home(home_link, *home_world, home_config, &store);
    HubConfig home_hub_config = load_hub(dir / "home.ini");
    home_hub_config.planet = "home";
    Hub home_hub(home, home_port, *home_world, home_hub_config, network);
    Galaxy galaxy(home, home_port, config, base, &store, network);
    CHECK(galaxy.planet_count() == 2 && galaxy.port("far") != 0 && galaxy.server("Far") != nullptr);

    double now = 0;
    const auto session_for = [&](const char* name) {
        auto link = network.open_client();
        auto session = std::make_unique<ClientSession>(
            *link, *home_world, ClientConfig{.server = {1, home_port}, .name = name},
            [](const std::string&) { return std::optional<Battlefield>{}; }, ClientSession::WorldFinder{},
            [&](const std::string& planet) { return load_world(dir / planet); }, "home");
        return std::make_pair(std::move(link), std::move(session));
    };
    const auto run = [&](ClientSession& session, double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 60.0) {
            session.update(now);
            home.update(now);
            home_hub.update(now);
            galaxy.update(now);
        }
    };

    {
        auto [link, session] = session_for("Pilot");
        run(*session, 0.6);
        CHECK(session->client().status() == GameClient::Status::connected && session->planet().empty());
        // The far planet's starport is on offer here, with its fare.
        bool offered = false;
        for (const net::Vendor& v : session->client().vendors()) {
            offered = offered || (v.kind == "travel_offworld" && v.name == "Far Starport" && v.detail == "Far|150");
        }
        CHECK(offered);
        const int64_t credits = session->client().player_status().credits;
        CHECK(credits >= 150);

        // Passage there: the fare paid, then the far planet's world and server, at its starport.
        session->client().service(net::ServiceRequest::travel, "Far Starport", 0, 0);
        run(*session, 1.5);
        CHECK(session->planet() == "far" && session->generation() == 1);
        CHECK(session->client().status() == GameClient::Status::connected);
        GameServer* far = galaxy.server("Far");
        CHECK(far != nullptr && far->client_count() == 1 && home.client_count() == 0);
        if (far != nullptr && far->client_count() == 1) {
            const Vec3 at = far->state_of(far->entities()[0])->position;
            CHECK(std::hypot(at.x - -60, at.z - 60) < 3 && std::abs(at.y - 10) < 0.5);
        }
        CHECK(session->client().player_status().credits == credits - 150);
        session->client().disconnect();
        run(*session, 0.3);
    }
    {
        // Back another day, at the home server: sent on to the planet they were on.
        auto [link, session] = session_for("Pilot");
        run(*session, 1.5);
        CHECK(session->planet() == "far" && galaxy.server("Far")->client_count() == 1);
        session->client().disconnect();
        run(*session, 0.3);
    }
    }
    std::filesystem::remove_all(dir);
}

// --ports: planets and battles take the first free port of the range, a
// released one is taken again, and a full range says so.
void test_port_range() {
    swm::core::UdpHubNetwork network(47811, 47813);
    uint16_t a = 0, b = 0, c = 0;
    auto first = network.open_server(a);
    auto second = network.open_server(b);
    auto third = network.open_server(c);
    CHECK(a == 47811);
    CHECK(b == 47812);
    CHECK(c == 47813);
    uint16_t d = 0;
    bool full = false;
    try {
        network.open_server(d);
    } catch (const std::runtime_error&) {
        full = true;
    }
    CHECK(full);
    second.reset();
    uint16_t again = 0;
    auto reopened = network.open_server(again);
    CHECK(again == 47812);
    // Without a range: any port.
    swm::core::UdpHubNetwork any;
    uint16_t free_port = 0;
    auto link = any.open_server(free_port);
    CHECK(free_port != 0);
}

} // namespace

int main() {
    try {
        test_port_range();
        test_starships_between_planets();
    } catch (const std::exception& error) {
        std::printf("FAIL: %s\n", error.what());
        return 1;
    }
    return swm::test::finish("galaxy_test");
}
