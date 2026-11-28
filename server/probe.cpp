// Joins a game server as a player for a few seconds and reports what it saw:
// whether it was let in, the round trip, snapshots, and who else is about. A
// quick check that a deployed server is reachable and running (docs/BACKEND.md).
//
//   swbfo_probe <address:port> --bake <prefix> [--name <name>] [--seconds <n>]
//
// --bake is the terrain the server's first planet runs on (e.g.
// game/imported_assets/terrain/tatooine): the client predicts its movement
// against it, as the game does. The character stands still. Exit code 0 if it
// connected and received snapshots, 1 if not, 2 on bad arguments.

#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <thread>

#include "swbfo/core/client.h"
#include "swbfo/core/net/link.h"
#include "swbfo/core/net/protocol.h"
#include "swbfo/core/net/udp.h"
#include "swbfo/core/world.h"

namespace {

const char* status_name(swbfo::core::GameClient::Status status) {
    using S = swbfo::core::GameClient::Status;
    switch (status) {
    case S::connecting: return "connecting";
    case S::connected: return "connected";
    case S::refused: return "refused";
    case S::timed_out: return "timed out";
    case S::disconnected: return "disconnected";
    case S::transferring: return "transferring";
    }
    return "?";
}

int usage() {
    std::cerr << "usage: swbfo_probe <address:port> --bake <prefix> [--name <name>] [--seconds <n>]\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return usage();
    }
    const auto server = swbfo::core::net::parse_endpoint(argv[1]);
    if (!server) {
        return usage();
    }
    std::string bake;
    std::string name = "probe";
    double seconds = 10;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--bake" && i + 1 < argc) {
            bake = argv[++i];
        } else if (arg == "--name" && i + 1 < argc) {
            name = argv[++i];
        } else if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::atof(argv[++i]);
        } else {
            return usage();
        }
    }
    if (bake.empty()) {
        return usage();
    }

    try {
        const auto world = swbfo::core::load_world(bake);
        swbfo::core::net::UdpLink link;
        swbfo::core::ClientConfig config;
        config.server = *server;
        config.name = name;
        swbfo::core::GameClient client(link, *world, config);

        std::cout << std::format("swbfo_probe: protocol {}, joining {} as {}\n", swbfo::core::net::protocol_version,
                                 server->to_string(), name);
        const auto start = std::chrono::steady_clock::now();
        auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
        bool reported = false;
        while (elapsed() < seconds) {
            client.set_input({});
            client.update(elapsed());
            const auto status = client.status();
            if (status == swbfo::core::GameClient::Status::connected && !reported) {
                std::cout << std::format("[{:5.2f}] connected: entity {}, character {}, {} Hz\n", elapsed(),
                                         client.entity_id(), client.character_id(), client.tick_rate());
                reported = true;
            }
            if (status == swbfo::core::GameClient::Status::refused || status == swbfo::core::GameClient::Status::timed_out ||
                status == swbfo::core::GameClient::Status::disconnected) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto stats = client.stats();
        std::cout << std::format("status {}{}; {} snapshots, rtt {:.0f} ms, {} corrections, {} others in view\n",
                                 status_name(client.status()),
                                 client.refusal().empty() ? "" : " (" + client.refusal() + ")", stats.snapshots,
                                 stats.rtt * 1000, stats.corrections, client.remotes().size());
        const bool ok = reported && stats.snapshots > 0;
        client.disconnect();
        for (int i = 0; i < 10; ++i) {  // let the goodbye go out
            client.update(elapsed());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "swbfo_probe: " << e.what() << "\n";
        return 1;
    }
}
