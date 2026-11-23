// Standalone game server: core/ only, no Godot.
//
//   swm_server [--bake <prefix>] [--db <file>] [--port 7777] [--spawn <swg_x> <swg_z>] [--seconds <n>]
//              [--building <file.collision> <swg_x> <swg_y> <swg_z> <yaw_degrees>]... [--loadout <file.ini>]
//              [--battlefield <file.ini>] [--bots <n>] [--hub <file.ini>]
//
// <prefix> names a terrain bake (swgtool bake-terrain, or bf2tool export-map;
// a battlefield on a Battlefront map brings its own). --db is the SQLite
// file characters are saved in (created if missing; default: none, nothing is
// saved). --spawn is where new characters start, in Galaxies coordinates as
// the snapshot tools print them (default: the bake's center). --building
// places a building's floors and walls (from swgtool export-building) where
// the world snapshot has it; clients must place the same buildings. Runs
// until Ctrl+C or for --seconds, then saves every connected character.
// --loadout is everyone's weapons and gear outside a match (a loadout file,
// see game/loadouts/; default: an E-11 alone); clients must use the same file.
//
// --battlefield runs a conquest match on a battlefield file (see
// game/battlefields/) instead: teams, their loadouts, command posts and
// reinforcements, warmup, live, results, and again. Clients must load the
// same file. Characters aren't saved in a match. --bots adds that many bots,
// connected like players (over UDP, from this process), to fill the teams.
//
// --hub makes this an open-world hub (see game/hubs/): players queue for
// battles at its war terminals, and it launches battle servers in this
// process (on free UDP ports), fills them with bots, sends the players there
// and takes them back afterwards. Use it with --db to keep characters.

#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#endif

#include "swm/core/bot.h"
#include "swm/core/build_info.h"
#include "swm/core/client.h"
#include "swm/core/combat.h"
#include "swm/core/conquest.h"
#include "swm/core/heightfield.h"
#include "swm/core/hub.h"
#include "swm/core/net/udp.h"
#include "swm/core/server.h"
#include "swm/core/sqlite_store.h"
#include "swm/core/world.h"

namespace {

std::atomic<bool> stop_requested = false;

int usage() {
    std::cerr << "usage: swm_server [--bake <prefix>] [--db <file>] [--port 7777] [--spawn <swg_x> <swg_z>] "
                 "[--seconds <n>] [--building <file.collision> <swg_x> <swg_y> <swg_z> <yaw_degrees>]... "
                 "[--loadout <file.ini>] [--battlefield <file.ini>] [--bots <n>] [--difficulty easy|normal|hard] "
                 "[--hub <file.ini>]\n";
    return 2;
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

int main(int argc, char** argv) {
    std::string bake;
    std::string db;
    std::string loadout;
    std::string catalog_path;
    std::string bf2_items_dir;
    std::string battlefield;
    std::string hub_file;
    int bot_count = 0;
    std::string difficulty = "normal";
    int port = 7777;
    bool has_spawn = false;
    double spawn_x = 0, spawn_z = 0, run_for = 0;
    struct BuildingArg {
        std::string path;
        double x, y, z, yaw;
    };
    std::vector<BuildingArg> buildings;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--bake" && i + 1 < argc) {
            bake = argv[++i];
        } else if (arg == "--loadout" && i + 1 < argc) {
            loadout = argv[++i];
        } else if (arg == "--catalog" && i + 1 < argc) {
            catalog_path = argv[++i];
        } else if (arg == "--bf2-items" && i + 1 < argc) {
            bf2_items_dir = argv[++i];
        } else if (arg == "--battlefield" && i + 1 < argc) {
            battlefield = argv[++i];
        } else if (arg == "--hub" && i + 1 < argc) {
            hub_file = argv[++i];
        } else if (arg == "--bots" && i + 1 < argc) {
            bot_count = std::atoi(argv[++i]);
        } else if (arg == "--difficulty" && i + 1 < argc) {
            difficulty = argv[++i];
        } else if (arg == "--db" && i + 1 < argc) {
            db = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--spawn" && i + 2 < argc) {
            has_spawn = true;
            spawn_x = std::atof(argv[++i]);
            spawn_z = std::atof(argv[++i]);
        } else if (arg == "--building" && i + 5 < argc) {
            BuildingArg building{argv[i + 1], std::atof(argv[i + 2]), std::atof(argv[i + 3]), std::atof(argv[i + 4]),
                                 std::atof(argv[i + 5])};
            buildings.push_back(building);
            i += 5;
        } else if (arg == "--seconds" && i + 1 < argc) {
            run_for = std::atof(argv[++i]);
        } else {
            return usage();
        }
    }
    if ((bake.empty() && battlefield.empty()) || port <= 0 || port > 65535 || bot_count < 0 || bot_count > 64 ||
        (!hub_file.empty() && !battlefield.empty())) {
        return usage();
    }

    try {
        swm::core::ServerConfig config;
        if (!battlefield.empty()) {
            config.battlefield = swm::core::load_battlefield(battlefield);
        }
        if (bake.empty() && config.battlefield && !config.battlefield->terrain.empty()) {
            bake = config.battlefield->terrain.string();
        }
        if (bake.empty()) {
            return usage();
        }
        const swm::core::Heightfield ground = swm::core::load_bake(bake);
        if (!loadout.empty()) {
            config.loadout = swm::core::load_loadout(loadout);
            std::cout << std::format("loadout {}: {} health\n", config.loadout.name,
                                     swm::core::stats_for(config.loadout, config.base_stats).max_health);
        }
        if (config.battlefield) {
            const auto& field = *config.battlefield;
            std::cout << std::format("battlefield {}: {} command posts, {} ({} loadouts) vs {} ({} loadouts), {} tickets\n",
                                     field.name, field.posts.size(), field.teams[0].name, field.teams[0].loadouts.size(),
                                     field.teams[1].name, field.teams[1].loadouts.size(), field.tickets);
            if (!db.empty()) {
                std::cout << "--db is ignored in a match: characters aren't saved\n";
                db.clear();
            }
        }
        std::optional<swm::core::HubConfig> hub_config;
        const swm::core::BotSkill skill = swm::core::BotSkill::preset(difficulty);
        if (!hub_file.empty()) {
            hub_config = swm::core::load_hub(hub_file);
            hub_config->bot_skill = skill;
        }
        if (has_spawn) {
            config.spawn = {-spawn_x, 0, spawn_z};  // Galaxies to game coordinates
        } else if (hub_config && hub_config->spawn) {
            config.spawn = *hub_config->spawn;
        } else {
            const swm::core::Vec3 corner = ground.clamp({-1e9, 0, 1e9});
            const swm::core::Vec3 other = ground.clamp({1e9, 0, -1e9});
            config.spawn = (corner + other) * 0.5;
        }
        std::unique_ptr<swm::core::SqliteCharacterStore> store;
        if (!db.empty()) {
            store = std::make_unique<swm::core::SqliteCharacterStore>(db);
        }
        swm::core::ItemCatalog catalog;
        if (!catalog_path.empty()) {
            catalog.load(catalog_path, bf2_items_dir);
            config.catalog = &catalog;
        }
        swm::core::net::UdpLink link{uint16_t(port)};
        swm::core::World world(ground);
        // A Battlefront map's props (bf2tool export-map), next to its bake.
        if (const size_t triangles = world.add_map_collision_of(bake); triangles > 0) {
            std::cout << std::format("map collision: {} triangles\n", triangles);
        }
        for (const BuildingArg& building : buildings) {
            const auto collision = swm::core::load_building_collision(building.path);
            world.add_building(collision,
                               swm::core::placement_from_galaxies(building.x, building.y, building.z, building.yaw));
            std::cout << std::format("building {}: {} floor triangles, {} walls\n", building.path,
                                     collision.floors.size(), collision.walls.size());
        }
        swm::core::GameServer server(link, world, config, store.get());
        const auto start = std::chrono::steady_clock::now();
        server.log = [&](const std::string& line) {
            std::cout << std::format("[{:8.2f}] {}\n", seconds_since(start), line) << std::flush;
        };
        // Bots: clients like any other, on their own sockets.
        swm::core::ClientConfig bot_config;
        bot_config.server = *swm::core::net::parse_endpoint(std::format("127.0.0.1:{}", link.port()));
        bot_config.loadout = config.loadout;
        bot_config.base_stats = config.base_stats;
        bot_config.battlefield = config.battlefield;
        swm::core::BotPlayers bots(bot_count, world, bot_config, {}, skill);
        swm::core::UdpHubNetwork hub_network;
        std::unique_ptr<swm::core::Hub> hub;
        if (hub_config) {
            std::cout << std::format("hub {}: {} war terminal(s), battles on {} battlefields, {} per team\n", hub_config->name,
                                     hub_config->recruiters.size(), hub_config->battlefields.size(), hub_config->team_size);
            hub = std::make_unique<swm::core::Hub>(server, link.port(), world, std::move(*hub_config), hub_network);
            hub->log = server.log;
        }
        std::cout << std::format("swm_server {}: UDP port {}, {} Hz, spawn ({:.1f}, {:.1f}) in Galaxies coordinates, {}\n",
                                 swm::core::version(), link.port(), config.tick_rate, -config.spawn.x, config.spawn.z,
                                 db.empty() ? std::string("not saving") : "saving to " + db)
                  << std::flush;
        std::signal(SIGINT, [](int) { stop_requested = true; });
        std::signal(SIGTERM, [](int) { stop_requested = true; });
#ifdef _WIN32
        timeBeginPeriod(1);  // 1 ms sleeps instead of the default ~16 ms
#endif
        // Entities and where they are, in Galaxies coordinates.
        const auto positions = [&] {
            std::string text;
            for (const uint32_t id : server.entities()) {
                const auto* state = server.state_of(id);
                text += std::format(" | {}: ({:.2f}, {:.2f}, {:.2f})", id, -state->position.x, state->position.y,
                                    state->position.z);
            }
            return text;
        };
        double next_status = 10;
        for (;;) {
            const double now = seconds_since(start);
            if (stop_requested || (run_for > 0 && now >= run_for)) {
                break;
            }
            server.update(now);
            bots.update(now);
            if (hub) {
                hub->update(now);
            }
            if (now >= next_status) {
                next_status += 10;
                server.log(std::format("tick {}, {} client(s){}", server.tick(), server.client_count(), positions()));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#ifdef _WIN32
        timeEndPeriod(1);
#endif
        bots.disconnect();
        hub.reset();
        server.save_all();
        server.log(std::format("stopped; saved {} connected character(s){}", store ? server.client_count() : 0,
                               store ? positions() : ""));
    } catch (const std::exception& error) {
        std::cerr << "swm_server: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
