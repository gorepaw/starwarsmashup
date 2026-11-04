// Standalone game server: core/ only, no Godot.
//
//   swm_server --bake <prefix> [--port 7777] [--spawn <swg_x> <swg_z>] [--seconds <n>]
//
// <prefix> names a terrain bake (swgtool bake-terrain). --spawn is in Galaxies
// coordinates, as the snapshot tools print them (default: the bake's center).
// Runs until stopped, or for --seconds.

#include <chrono>
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

namespace {

int usage() {
    std::cerr << "usage: swm_server --bake <prefix> [--port 7777] [--spawn <swg_x> <swg_z>] [--seconds <n>]\n";
    return 2;
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

int main(int argc, char** argv) {
    std::string bake;
    int port = 7777;
    bool has_spawn = false;
    double spawn_x = 0, spawn_z = 0, run_for = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--bake" && i + 1 < argc) {
            bake = argv[++i];
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
        swm::core::net::UdpLink link{uint16_t(port)};
        swm::core::GameServer server(link, ground, config);
        const auto start = std::chrono::steady_clock::now();
        server.log = [&](const std::string& line) {
            std::cout << std::format("[{:8.2f}] {}\n", seconds_since(start), line) << std::flush;
        };
        std::cout << std::format("swm_server {}: UDP port {}, {} Hz, spawn ({:.1f}, {:.1f}) in Galaxies coordinates\n",
                                 swm::core::version(), link.port(), config.tick_rate, -config.spawn.x, config.spawn.z)
                  << std::flush;
#ifdef _WIN32
        timeBeginPeriod(1);  // 1 ms sleeps instead of the default ~16 ms
#endif
        double next_status = 10;
        for (;;) {
            const double now = seconds_since(start);
            if (run_for > 0 && now >= run_for) {
                break;
            }
            server.update(now);
            if (now >= next_status) {
                next_status += 10;
                std::string positions;
                for (const uint32_t id : server.entities()) {
                    const auto* state = server.state_of(id);
                    positions += std::format(" | {}: ({:.1f}, {:.1f}, {:.1f})", id, -state->position.x,
                                             state->position.y, state->position.z);
                }
                server.log(std::format("tick {}, {} client(s){}", server.tick(), server.client_count(), positions));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#ifdef _WIN32
        timeEndPeriod(1);
#endif
    } catch (const std::exception& error) {
        std::cerr << "swm_server: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
