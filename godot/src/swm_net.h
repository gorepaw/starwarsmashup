#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <string>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "swm/core/bot.h"
#include "swm/core/client.h"
#include "swm/core/conquest.h"
#include "swm/core/heightfield.h"
#include "swm/core/hub.h"
#include "swm/core/net/link.h"
#include "swm/core/net/udp.h"
#include "swm/core/server.h"
#include "swm/core/sqlite_store.h"
#include "swm/core/world.h"

namespace swm::godot_bindings {

// A game client (core::GameClient) over UDP. Call update() every frame.
// Positions are in game space (Godot's axes). It follows the server's
// transfers (from a hub to a battle and back): each one starts over with new
// entity ids, and get_generation() counts them.
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
    // The soldier class (an OS path to a file from bf2tool export-soldier).
    // Must match the server's. Call before start().
    godot::Error set_soldier(const godot::String& path);
    // A battlefield file (an OS path; see game/battlefields/), for a server
    // running a match on it. Must match the server's. Call before start().
    godot::Error set_battlefield(const godot::String& path);
    // Where battlefield files are (an OS path), for battles a hub sends us to:
    // <dir>/<name>.ini. Call before start().
    void set_battlefield_dir(const godot::String& path);
    // Lets a bot (core::Bot) drive the character instead of set_input.
    void set_bot(bool enabled);
    // `server` is "a.b.c.d:port". `bake_prefix` is an OS path to the terrain
    // bake the server uses (swgtool bake-terrain).
    godot::Error start(const godot::String& server, const godot::String& bake_prefix, const godot::String& name);
    // {forward, right, yaw, pitch, sprint, jump, fire, reload, weapon}; missing keys are 0/false.
    void set_input(const godot::Dictionary& controls);
    void update();
    void leave();

    godot::String get_status() const;
    bool is_spawned() const;
    int64_t get_entity_id() const;
    // The saved character's persistent id (0 if the server doesn't save).
    int64_t get_character_id() const;
    // Why the server refused the connection (status "refused").
    godot::String get_refusal() const;
    // Own character as drawn: {position, velocity, yaw, grounded, energy,
    // sprinting, weapon, rounds, heat, overheated, reloading, health, alive,
    // life ("waiting", "alive" or "dead"), team (-1 outside a match),
    // soldier_class, kills, deaths, captures}.
    godot::Dictionary get_presented() const;
    // Other characters: [{id, position, velocity, yaw, ..., health, alive, life, team, ...}].
    godot::Array get_remotes() const;
    // Others' bolts in flight: [{owner, weapon, position, velocity, color, length, width}]
    // (the looks come from the firing soldier's weapon).
    godot::Array get_bolts() const;
    // Own shots since the last call, to draw at once:
    // [{origin, directions (one per pellet), weapon, color, length, width}].
    godot::Array take_shots();
    // Events since the last call: [{kind, attacker, victim, damage}]. Kinds
    // "hit" and "kill" are between entities (attacker 0: the world);
    // "captured" and "neutralized" have the team as attacker and the command
    // post as victim.
    godot::Array take_events();
    // The current class's weapons: [{name, rounds_per_clip, heat_per_shot,
    // bolt_speed, bolt_life, bolt_color, bolt_length, bolt_width}].
    godot::Array get_weapons() const;
    double get_max_health() const;
    // The battlefield: {name, min_x, min_z, max_x, max_z (game space), teams:
    // [{name, classes: [{name, max_health, weapons: [names]}]}], posts: [{name,
    // position, radius, capturable}]}; empty outside a match.
    godot::Dictionary get_battlefield() const;
    // The match: {phase ("warmup", "live" or "ended"), time_left, tickets: [a, b],
    // dominant, victory_left, winner, posts: [{owner, holder, charge, contested}]};
    // empty outside a match.
    godot::Dictionary get_match() const;
    // Asks to spawn as a class of our team at a command post (indexes into
    // get_battlefield()), until the character spawns.
    void request_spawn(int64_t soldier_class, int64_t post);
    // How many times the server has sent us to another server.
    int64_t get_generation() const;
    // Why the last transfer couldn't be followed (e.g. a missing battlefield).
    godot::String get_session_error() const;
    // A hub's war terminals: [{name, position, radius}]; empty elsewhere.
    godot::Array get_recruiters() const;
    // The two sides a hub's battles are between.
    godot::PackedStringArray get_factions() const;
    // Queue for a battle on a side (0 or 1) at a war terminal; -1 leaves the queue.
    void queue(int64_t team);
    // {queue ("none" or "queued"), team, in_queue, starts_in, notice_id, notice}.
    godot::Dictionary get_player_status() const;
    // {ticks, snapshots, corrections, max_correction, rtt_ms}
    godot::Dictionary get_stats() const;
    double get_ground_height(double x, double z) const;

protected:
    static void _bind_methods();

private:
    double now() const;
    core::GameClient* client() const { return session_ ? &session_->client() : nullptr; }
    const std::optional<core::Battlefield>& battlefield() const;
    const core::SoldierDef& soldier() const;
    // The class an entity plays, by team and class index.
    const core::SoldierDef& soldier_of(int team, int soldier_class) const;

    std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();
    core::net::ImpairedLink::Settings impairment_;
    core::Heightfield ground_;
    std::vector<std::pair<core::BuildingCollision, core::Placement>> buildings_;
    core::SoldierDef soldier_;
    std::optional<core::Battlefield> battlefield_;
    std::string battlefield_dir_;
    std::unique_ptr<core::Bot> bot_;
    int bot_generation_ = 0;
    std::unique_ptr<core::World> world_;
    std::unique_ptr<core::net::UdpLink> udp_;
    std::unique_ptr<core::net::ImpairedLink> impaired_;
    std::unique_ptr<core::ClientSession> session_;
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
    // As SwmClient.set_soldier. Call before start().
    godot::Error set_soldier(const godot::String& path);
    // Runs a conquest match on this battlefield (as SwmClient.set_battlefield).
    // Call before start().
    godot::Error set_battlefield(const godot::String& path);
    // Adds bots, connected like players, when the server starts.
    void set_bots(int64_t count);
    // Makes the server a hub (a hub file, an OS path; see game/hubs/): its
    // war terminals queue players for battles it launches. Call before start().
    godot::Error set_hub(const godot::String& path);
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
    core::SoldierDef soldier_;
    std::optional<core::Battlefield> battlefield_;
    int bot_count_ = 0;
    std::unique_ptr<core::World> world_;
    std::unique_ptr<core::SqliteCharacterStore> store_;
    std::unique_ptr<core::net::UdpLink> udp_;
    std::unique_ptr<core::GameServer> server_;
    std::unique_ptr<core::BotPlayers> bots_;
    std::optional<core::HubConfig> hub_config_;
    core::UdpHubNetwork hub_network_;
    std::unique_ptr<core::Hub> hub_;
};

} // namespace swm::godot_bindings
