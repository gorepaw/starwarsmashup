// Standalone game server: core/ only, no Godot.
//
//   swbfo_server [--bake <prefix>] [--db <file>] [--port 7777] [--spawn <swg_x> <swg_z>] [--seconds <n>]
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
//
// --galaxy <galaxy.ini> (see game/hubs/galaxy.ini) runs every planet it
// names, each a hub on a world server of its own in this process: the first
// on --port (its bake and hub are --bake and --hub's, unless given), the
// others on free ports, with starships between their starports (Stage 13).
// --ports <first>-<last> takes those free ports (other planets', battles')
// from that range, so a firewall can open exactly it; without it, any port.
// Each finished battle's telemetry (map, mode, era, sides, winner, tickets,
// duration, players and bots, kills by weapon) is recorded in the --db file.
//
//   swbfo_server --telemetry <db file> [--last <n>]
//
// prints what that telemetry says: win rates by faction, mode and map, K/D,
// and the deadliest weapons, over the last n battles (default: all).

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

#include "swbfo/core/bot.h"
#include "swbfo/core/build_info.h"
#include "swbfo/core/client.h"
#include "swbfo/core/combat.h"
#include "swbfo/core/conquest.h"
#include "swbfo/core/heightfield.h"
#include "swbfo/core/galaxy.h"
#include "swbfo/core/hub.h"
#include "swbfo/core/net/udp.h"
#include "swbfo/core/server.h"
#include "swbfo/core/sqlite_store.h"
#include "swbfo/core/world.h"

namespace {

std::atomic<bool> stop_requested = false;

int usage() {
    std::cerr << "usage: swbfo_server [--bake <prefix>] [--db <file>] [--port 7777] [--spawn <swg_x> <swg_z>] "
                 "[--seconds <n>] [--building <file.collision> <swg_x> <swg_y> <swg_z> <yaw_degrees>]... "
                 "[--loadout <file.ini>] [--battlefield <file.ini>] [--bots <n>] "
                 "[--hub <file.ini>] [--galaxy <galaxy.ini>] [--ports <first>-<last>]\n"
                 "       swbfo_server --telemetry <db file> [--last <n>]\n";
    return 2;
}

// Stage 11: the battles' telemetry in a database, summed up.
int telemetry_report(int argc, char** argv) {
    size_t last = size_t(-1);
    if (argc == 5 && std::string(argv[3]) == "--last") {
        last = size_t(std::max(1, std::atoi(argv[4])));
    } else if (argc != 3) {
        return usage();
    }
    try {
        swbfo::core::SqliteCharacterStore store(argv[2]);
        const auto matches = store.list_matches(last);
        std::cout << swbfo::core::format_telemetry_report(swbfo::core::summarize_telemetry(matches));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--telemetry") {
        return telemetry_report(argc, argv);
    }
    std::string bake;
    std::string db;
    std::string loadout;
    std::string catalog_path;
    std::string bf2_items_dir;
    std::string battlefield;
    std::string hub_file;
    std::string galaxy_file;
    std::optional<swbfo::core::GalaxyConfig> galaxy_config;
    int bot_count = 0;
    int port = 7777;
    int ports_first = 0, ports_last = 0;  // --ports
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
        } else if (arg == "--galaxy" && i + 1 < argc) {
            galaxy_file = argv[++i];
        } else if (arg == "--bots" && i + 1 < argc) {
            bot_count = std::atoi(argv[++i]);
        } else if (arg == "--db" && i + 1 < argc) {
            db = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--ports" && i + 1 < argc) {
            const std::string range = argv[++i];
            const size_t dash = range.find('-');
            ports_first = dash == std::string::npos ? 0 : std::atoi(range.substr(0, dash).c_str());
            ports_last = dash == std::string::npos ? 0 : std::atoi(range.substr(dash + 1).c_str());
            if (ports_first < 1 || ports_last < ports_first || ports_last > 65535) {
                std::cerr << "--ports wants <first>-<last>, e.g. 7778-7899\n";
                return usage();
            }
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
    if (!galaxy_file.empty()) {
        try {
            galaxy_config = swbfo::core::load_galaxy(galaxy_file);
        } catch (const std::exception& error) {
            std::cerr << "swbfo_server: " << error.what() << "\n";
            return 1;
        }
        if (bake.empty()) bake = galaxy_config->planets[0].bake.string();
        if (hub_file.empty()) hub_file = galaxy_config->planets[0].hub.string();
    }
    if ((bake.empty() && battlefield.empty()) || port <= 0 || port > 65535 || bot_count < 0 || bot_count > 64 ||
        (!hub_file.empty() && !battlefield.empty())) {
        return usage();
    }

    try {
        swbfo::core::ServerConfig config;
        if (!battlefield.empty()) {
            config.battlefield = swbfo::core::load_battlefield(battlefield);
        }
        if (bake.empty() && config.battlefield && !config.battlefield->terrain.empty()) {
            bake = config.battlefield->terrain.string();
        }
        if (bake.empty()) {
            return usage();
        }
        const swbfo::core::Heightfield ground = swbfo::core::load_bake(bake);
        if (!loadout.empty()) {
            config.loadout = swbfo::core::load_loadout(loadout);
            std::cout << std::format("loadout {}: {} health\n", config.loadout.name,
                                     swbfo::core::stats_for(config.loadout, config.base_stats).max_health);
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
        std::optional<swbfo::core::HubConfig> hub_config;
        const swbfo::core::BotSkill skill = swbfo::core::BotSkill::preset("hard");
        if (galaxy_config) {
            config.planet = galaxy_config->planets[0].name;
        }
        if (!hub_file.empty()) {
            hub_config = swbfo::core::load_hub(hub_file);
            hub_config->bot_skill = skill;
            if (galaxy_config) {
                hub_config->planet = galaxy_config->planets[0].stem();
            }
        }
        if (has_spawn) {
            config.spawn = {-spawn_x, 0, spawn_z};  // Galaxies to game coordinates
        } else if (hub_config && hub_config->spawn) {
            config.spawn = *hub_config->spawn;
        } else {
            const swbfo::core::Vec3 corner = ground.clamp({-1e9, 0, 1e9});
            const swbfo::core::Vec3 other = ground.clamp({1e9, 0, -1e9});
            config.spawn = (corner + other) * 0.5;
        }
        std::unique_ptr<swbfo::core::SqliteCharacterStore> store;
        if (!db.empty()) {
            store = std::make_unique<swbfo::core::SqliteCharacterStore>(db);
        }
        swbfo::core::ItemCatalog catalog;
        if (!catalog_path.empty()) {
            catalog.load(catalog_path, bf2_items_dir);
            config.catalog = &catalog;
        }
        swbfo::core::net::UdpLink link{uint16_t(port)};
        swbfo::core::World world(ground);
        // A Battlefront map's props (bf2tool export-map), next to its bake.
        if (const size_t triangles = world.add_map_collision_of(bake); triangles > 0) {
            std::cout << std::format("map collision: {} triangles\n", triangles);
        }
        for (const BuildingArg& building : buildings) {
            const auto collision = swbfo::core::load_building_collision(building.path);
            world.add_building(collision,
                               swbfo::core::placement_from_galaxies(building.x, building.y, building.z, building.yaw));
            std::cout << std::format("building {}: {} floor triangles, {} walls\n", building.path,
                                     collision.floors.size(), collision.walls.size());
        }
        swbfo::core::GameServer server(link, world, config, store.get());
        const auto start = std::chrono::steady_clock::now();
        server.log = [&](const std::string& line) {
            std::cout << std::format("[{:8.2f}] {}\n", seconds_since(start), line) << std::flush;
        };
        // Bots: clients like any other, on their own sockets.
        swbfo::core::ClientConfig bot_config;
        bot_config.server = *swbfo::core::net::parse_endpoint(std::format("127.0.0.1:{}", link.port()));
        bot_config.loadout = config.loadout;
        bot_config.base_stats = config.base_stats;
        bot_config.battlefield = config.battlefield;
        swbfo::core::BotPlayers bots(bot_count, world, bot_config, {}, skill);
        swbfo::core::UdpHubNetwork hub_network = ports_first > 0
                                                   ? swbfo::core::UdpHubNetwork(uint16_t(ports_first), uint16_t(ports_last))
                                                   : swbfo::core::UdpHubNetwork();
        std::unique_ptr<swbfo::core::Hub> hub;
        if (hub_config) {
            std::cout << std::format("hub {}: {} war terminal(s), battles on {} battlefields, {} per team\n", hub_config->name,
                                     hub_config->recruiters.size(), hub_config->battlefields.size(), hub_config->team_size);
            hub = std::make_unique<swbfo::core::Hub>(server, link.port(), world, std::move(*hub_config), hub_network);
            hub->log = server.log;
        }
        std::unique_ptr<swbfo::core::Galaxy> galaxy;
        if (galaxy_config && hub) {
            galaxy = std::make_unique<swbfo::core::Galaxy>(server, link.port(), *galaxy_config, config, store.get(),
                                                          hub_network, server.log);
        }
        std::cout << std::format("swbfo_server {}: UDP port {}, {} Hz, spawn ({:.1f}, {:.1f}) in Galaxies coordinates, {}\n",
                                 swbfo::core::version(), link.port(), config.tick_rate, -config.spawn.x, config.spawn.z,
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
            if (galaxy) {
                galaxy->update(now);
            }
            if (now >= next_status) {
                next_status += 10;
                server.log(std::format("tick {}, {} client(s){}", server.tick(), server.client_count(), positions()));
                // What the ticks cost (Stage 13 server audit).
                const swbfo::core::ServerPerf perf = server.take_perf(now);
                if (perf.ticks > 0) {
                    const double secs = std::max(perf.window, 1e-6);
                    std::string wildlife;
                    if (hub && hub->population()) {
                        const auto* pop = hub->population();
                        wildlife = std::format(", wildlife {}/{} groups awake, {} mob(s) asleep", pop->awake_groups(),
                                               pop->group_count(), pop->dormant_mobs());
                    }
                    server.log(std::format(
                        "perf: tick {:.2f} ms avg, {:.2f} ms worst ({:.0f}% of budget), npcs {:.0f} simulated / {:.0f} "
                        "frozen per tick, snapshots {:.1f} KB/s out ({} B largest), strings in {:.0f}% of entities{}",
                        1000.0 * perf.tick_seconds / double(perf.ticks), 1000.0 * perf.worst_tick,
                        100.0 * perf.tick_seconds / secs, double(perf.npc_ticks) / double(perf.ticks),
                        double(perf.npc_frozen) / double(perf.ticks), double(perf.snapshot_bytes) / 1024.0 / secs,
                        perf.largest_snapshot,
                        perf.entities_sent ? 100.0 * double(perf.static_sent) / double(perf.entities_sent) : 0.0,
                        wildlife));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#ifdef _WIN32
        timeEndPeriod(1);
#endif
        bots.disconnect();
        if (galaxy) {
            for (const auto& planet : galaxy_config->planets) {
                if (swbfo::core::GameServer* other = galaxy->server(planet.name); other && other != &server) {
                    other->save_all();
                }
            }
        }
        galaxy.reset();
        hub.reset();
        server.save_all();
        server.log(std::format("stopped; saved {} connected character(s){}", store ? server.client_count() : 0,
                               store ? positions() : ""));
    } catch (const std::exception& error) {
        std::cerr << "swbfo_server: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
