#pragma once

#include <chrono>
#include <memory>
#include <utility>
#include <vector>
#include <string>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "swm/core/client.h"
#include "swm/core/heightfield.h"
#include "swm/core/net/link.h"
#include "swm/core/net/udp.h"
#include "swm/core/server.h"
#include "swm/core/sqlite_store.h"
#include "swm/core/world.h"

namespace swm::godot_bindings {

// A game client (core::GameClient) over UDP. Call update() every frame.
// Positions are in game space (Godot's axes).
class SwmClient : public godot::RefCounted {
    GDCLASS(SwmClient, godot::RefCounted)

public:
    // Simulated network trouble on this client's traffic, for testing
    // prediction. Call before start().
    void set_simulated_network(double latency_ms, double jitter_ms, double loss);
    // Places a building's floors and walls (an OS path to a .collision file
    // from swgtool export-building), at a Galaxies position and yaw in
    // degrees. Must match the server's buildings. Call before start().
    godot::Error add_building(const godot::String& collision_path, const godot::Vector3& position, double yaw_degrees);
    // `server` is "a.b.c.d:port". `bake_prefix` is an OS path to the terrain
    // bake the server uses (swgtool bake-terrain).
    godot::Error start(const godot::String& server, const godot::String& bake_prefix, const godot::String& name);
    void set_input(double forward, double right, double yaw, bool sprint, bool jump);
    void update();
    void leave();

    godot::String get_status() const;
    bool is_spawned() const;
    int64_t get_entity_id() const;
    // The saved character's persistent id (0 if the server doesn't save).
    int64_t get_character_id() const;
    // Why the server refused the connection (status "refused").
    godot::String get_refusal() const;
    // Own character as drawn: {position, velocity, yaw, grounded}.
    godot::Dictionary get_presented() const;
    // Other characters: [{id, position, velocity, yaw, grounded}].
    godot::Array get_remotes() const;
    // {ticks, snapshots, corrections, max_correction, rtt_ms}
    godot::Dictionary get_stats() const;
    double get_ground_height(double x, double z) const;

protected:
    static void _bind_methods();

private:
    double now() const;

    std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();
    core::net::ImpairedLink::Settings impairment_;
    core::Heightfield ground_;
    std::vector<std::pair<core::BuildingCollision, core::Placement>> buildings_;
    std::unique_ptr<core::World> world_;
    std::unique_ptr<core::net::UdpLink> udp_;
    std::unique_ptr<core::net::ImpairedLink> impaired_;
    std::unique_ptr<core::GameClient> client_;
};

// A game server (core::GameServer) over UDP, hosted inside Godot. The
// standalone swm_server runs the same server without Godot.
class SwmServer : public godot::RefCounted {
    GDCLASS(SwmServer, godot::RefCounted)

public:
    ~SwmServer() override;

    // Saves characters in this SQLite file (an OS path). Call before start().
    void set_database(const godot::String& path);
    // As SwmClient.add_building. Call before start().
    godot::Error add_building(const godot::String& collision_path, const godot::Vector3& position, double yaw_degrees);
    // `spawn` is where new characters start, in Galaxies coordinates (x, z).
    godot::Error start(int64_t port, const godot::String& bake_prefix, const godot::Vector2& spawn);
    void update();
    // Saves every connected character and shuts the server down.
    void stop();
    int64_t get_client_count() const;
    int64_t get_port() const;

protected:
    static void _bind_methods();

private:
    std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();
    std::string database_;
    core::Heightfield ground_;
    std::vector<std::pair<core::BuildingCollision, core::Placement>> buildings_;
    std::unique_ptr<core::World> world_;
    std::unique_ptr<core::SqliteCharacterStore> store_;
    std::unique_ptr<core::net::UdpLink> udp_;
    std::unique_ptr<core::GameServer> server_;
};

} // namespace swm::godot_bindings
