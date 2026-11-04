// Standalone game server: core/ only, no Godot.
//
//   swm_server --bake <prefix> [--db <file>] [--port 7777] [--spawn <swg_x> <swg_z>] [--seconds <n>]
//
// <prefix> names a terrain bake (swgtool bake-terrain). --db is the SQLite
// file characters are saved in (created if missing; default: none, nothing is
// saved). --spawn is where new characters start, in Galaxies coordinates as
// the snapshot tools print them (default: the bake's center). Runs until
// Ctrl+C or for --seconds, then saves every connected character.

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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#endif

#include "swm/core/build_info.h"
#include "swm/core/heightfield.h"
#include "swm/core/net/udp.h"
#include "swm/core/server.h"
#include "swm/core/sqlite_store.h"

namespace {

std::atomic<bool> stop_requested = false;

int usage() {
    std::cerr << "usage: swm_server --bake <prefix> [--db <file>] [--port 7777] [--spawn <swg_x> <swg_z>] "
                 "[--seconds <n>]\n";
    return 2;
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

int main(int argc, char** argv) {
    std::string bake;
    std::string db;
    int port = 7777;
    bool has_spawn = false;
    double spawn_x = 0, spawn_z = 0, run_for = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--bake" && i + 1 < argc) {
            bake = argv[++i];
        } else if (arg == "--db" && i + 1 < argc) {
            db = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--spawn" && i + 2 < argc) {
            has_spawn = true;
            spawn_x = std::atof(argv[++i]);
            spawn_z = std::atof(argv[++i]);
        } else if (arg == "--seconds" && i + 1 < argc) {
            run_for = std::atof(argv[++i]);
        } else {
            return usage();
        }
    }
    if (bake.empty() || port <= 0 || port > 65535) {
        return usage();
    }

    try {
        const swm::core::Heightfield ground = swm::core::load_bake(bake);
        swm::core::ServerConfig config;
        if (has_spawn) {
            config.spawn = {-spawn_x, 0, spawn_z};  // Galaxies to game coordinates
        } else {
            const swm::core::Vec3 corner = ground.clamp({-1e9, 0, 1e9});
            const swm::core::Vec3 other = ground.clamp({1e9, 0, -1e9});
            config.spawn = (corner + other) * 0.5;
        }
        std::unique_ptr<swm::core::SqliteCharacterStore> store;
        if (!db.empty()) {
            store = std::make_unique<swm::core::SqliteCharacterStore>(db);
        }
        swm::core::net::UdpLink link{uint16_t(port)};
        swm::core::GameServer server(link, ground, config, store.get());
        const auto start = std::chrono::steady_clock::now();
        server.log = [&](const std::string& line) {
            std::cout << std::format("[{:8.2f}] {}\n", seconds_since(start), line) << std::flush;
        };
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
            if (now >= next_status) {
                next_status += 10;
                server.log(std::format("tick {}, {} client(s){}", server.tick(), server.client_count(), positions()));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#ifdef _WIN32
        timeEndPeriod(1);
#endif
        server.save_all();
        server.log(std::format("stopped; saved {} connected character(s){}", store ? server.client_count() : 0,
                               store ? positions() : ""));
    } catch (const std::exception& error) {
        std::cerr << "swm_server: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
