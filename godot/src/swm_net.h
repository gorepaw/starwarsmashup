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
#include "swm/core/items.h"
#include "swm/core/nav.h"
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
    // The loadout outside a match (an OS path to a loadout file; see
    // game/loadouts/). Must match the server's. Call before start().
    godot::Error set_loadout(const godot::String& path);
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
    // {forward, right, yaw, pitch, sprint, jump, fire, reload, secondary, weapon};
    // missing keys are 0/false. `secondary` throws a grenade.
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
    // sprinting, weapon, rounds, reserve (-1: unlimited), heat, overheated,
    // reloading, grenades, health, alive,
    // life ("waiting", "alive" or "dead"), team (-1 outside a match),
    // loadout (index into the team's loadouts), kills, deaths, captures}.
    godot::Dictionary get_presented() const;
    // Other characters: [{id, position, velocity, yaw, ..., health, alive, life, team, ...}].
    godot::Array get_remotes() const;
    // Projectiles in flight: others' bolts, and everyone's missiles and grenades:
    // [{owner, weapon, kind ("bolt", "missile" or "grenade"), position, velocity,
    // color, length, width}] (the looks come from the firing character's weapon).
    // Own bolts come from take_shots.
    godot::Array get_bolts() const;
    // Own bolts since the last call, to draw at once:
    // [{origin, directions (one per pellet), weapon, color, length, width}].
    // (Own missiles and grenades are drawn from get_bolts.)
    godot::Array take_shots();
    // Events since the last call: [{kind, attacker, victim, damage}]. Kinds
    // "hit" and "kill" are between entities (attacker 0: the world);
    // "captured" and "neutralized" have the team as attacker and the command
    // post as victim.
    godot::Array take_events();
    // Explosions since the last call: [{position, normal, radius, owner, effect,
    // light_color, light_radius}].
    godot::Array take_explosions();
    // Bolt impacts since the last call (for sparks and scorch marks):
    // [{position, normal, hit_character, effect, color}].
    godot::Array take_impacts();
    // Dropped health, ammo, dual and energy canisters on the ground:
    // [{id, kind, model, position, time_left}].
    godot::Array get_pickups() const;
    // The current loadout's secondary (a throwable): {name}, or empty.
    godot::Dictionary get_secondary() const;
    // The current loadout's held weapons, by slot (primary, sidearm; {} for an
    // empty slot): [{name, rounds_per_clip, heat_per_shot,
    // bolt_speed, bolt_life, bolt_color, bolt_length, bolt_width}].
    godot::Array get_weapons() const;
    double get_max_health() const;
    // The battlefield: {name, min_x, min_z, max_x, max_z (game space),
    // boundaries: [PackedVector2Array of (x, z)], teams:
    // [{name, loadouts: [{name, max_health, weapons: [names], secondary}]}], posts: [{name,
    // position, radius, capturable}]}; empty outside a match.
    godot::Dictionary get_battlefield() const;
    // Whether teams a and b don't fight: the same team, or a battlefield's
    // natives (team 2) and a side they're friends with.
    bool is_allied(int a, int b) const;
    // The match: {phase ("warmup", "live" or "ended"), time_left, tickets: [a, b],
    // dominant, victory_left, winner, posts: [{owner, holder, charge, contested}]};
    // empty outside a match.
    godot::Dictionary get_match() const;
    // Asks to spawn with one of our team's loadouts at a command post (indexes into
    // get_battlefield()), until the character spawns.
    void request_spawn(int64_t loadout, int64_t post);
    // How many times the server has sent us to another server.
    int64_t get_generation() const;
    // Why the last transfer couldn't be followed (e.g. a missing battlefield).
    godot::String get_session_error() const;
    // A hub's war terminals: [{name, position, radius}]; empty elsewhere.
    godot::Array get_recruiters() const;
    // The two sides a hub's battles are between.
    godot::PackedStringArray get_factions() const;
    // How the server's people look: Galaxies object templates; a remote's
    // look (1 and up) indexes these from 1.
    godot::PackedStringArray get_looks() const;
    // Queue for a battle on a side (0 or 1) at a war terminal; -1 leaves the queue.
    // `mode` is "standard" or "gear".
    void queue(int64_t team, const godot::String& mode = "standard");
    // {queue ("none" or "queued"), team, queue_mode, in_queue, starts_in, battlefield,
    // notice_id, notice, credits, faction_points, experience, active_loadout,
    // switching_loadout, switch_progress, inventory_rev}.
    godot::Dictionary get_player_status() const;
    // Quartermasters in the hub: [{name, faction, position, radius}].
    godot::Array get_quartermasters() const;
    // Open-world vendors in the hub: [{name, kind, position, radius}].
    godot::Array get_vendors() const;
    // Shooting-range targets in the hub: [{id, name, position, health, max_health, alive}].
    godot::Array get_targets() const;
    // Nearby lootable corpses: [{entity_id, name, position, items: [{slot, def_id, quality}]}].
    godot::Array get_corpses() const;
    // Authoritative inventory & loadouts synced from the server.
    godot::Array get_inventory() const;
    godot::Array get_character_loadouts() const;
    godot::Array get_custom_loadouts() const;
    void save_loadout(int64_t slot_index, const godot::Dictionary& loadout);
    void switch_loadout(int64_t slot_index);
    void buy_item(const godot::String& def_id, bool use_faction_points = true);
    void destroy_item(int64_t item_id);
    void take_loot(int64_t corpse_entity, int64_t slot = -1);
    void sell_item(int64_t item_id);
    void sell_all_junk();
    void use_item(int64_t item_id);
    // Quests. The hub's quest people: [{entity, name, position, giver}];
    // its mission terminals: [{index, name, position, radius}]; the journal:
    // {revision, active: [quest], completed: [title], offers: [quest],
    // offer_source, offer_serial}, a quest being {id, title, giver, summary,
    // steps: [text], step, count, needed, waypoint (or absent), credits,
    // experience, faction_points, items: [def id], mission}.
    godot::Array get_quest_npcs() const;
    godot::Array get_mission_terminals() const;
    godot::Dictionary get_quests() const;
    void talk_to(int64_t npc_entity);
    void use_mission_terminal(int64_t index);
    void accept_quest(const godot::String& id);
    void abandon_quest(const godot::String& id);
    // The terrain bake (prefix) of the world the client is in now: a
    // battle's own (its battlefield's map), or the one start() was given.
    godot::String get_bake() const;
    // {ticks, snapshots, corrections, max_correction, rtt_ms}
    godot::Dictionary get_stats() const;
    // The ground at (x, z) under height `below` (by default, the highest).
    double get_ground_height(double x, double z, double below) const;
    // Where along from..to (0..1) the segment first hits the world (terrain,
    // buildings, a map's props), as a shot would; -1 if nothing.
    double raycast(const godot::Vector3& from, const godot::Vector3& to) const;
    // Whether a point is on the battlefield (its bounds and boundaries);
    // true outside a match.
    bool in_battlefield(const godot::Vector3& point) const;

protected:
    static void _bind_methods();

private:
    double now() const;
    core::GameClient* client() const { return session_ ? &session_->client() : nullptr; }
    // The world the client is in now (see get_bake()).
    const core::World* world() const { return session_ ? &session_->world() : world_.get(); }
    const std::optional<core::Battlefield>& battlefield() const;
    const core::Loadout& loadout() const;
    // An entity's loadout, by team and loadout index.
    const core::Loadout& loadout_of(int team, int loadout) const;

    std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();
    core::net::ImpairedLink::Settings impairment_;
    core::Heightfield ground_;
    std::vector<std::pair<core::BuildingCollision, core::Placement>> buildings_;
    core::Loadout loadout_;
    std::optional<core::Battlefield> battlefield_;
    std::string battlefield_dir_;
    std::unique_ptr<core::Bot> bot_;
    int bot_generation_ = 0;
    std::shared_ptr<const core::NavGrid> navigation_;  // the bot's, for the current battlefield
    std::string bake_;
    std::unique_ptr<core::World> world_;  // the hub's (or the only server's)
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
    // As SwmClient.set_loadout. Call before start().
    godot::Error set_loadout(const godot::String& path);
    // Runs a conquest match on this battlefield (as SwmClient.set_battlefield).
    // Call before start().
    godot::Error set_battlefield(const godot::String& path);
    // Adds bots, connected like players, when the server starts.
    void set_bots(int64_t count);
    // How well bots play: "easy", "normal" or "hard" (core::BotSkill::preset),
    // here and in a hub's battles; any time.
    void set_bot_difficulty(const godot::String& difficulty);
    // Makes the server a hub (a hub file, an OS path; see game/hubs/): its
    // war terminals queue players for battles it launches. Call before start().
    godot::Error set_hub(const godot::String& path);
    // Loads the item catalog for hub inventory, quartermasters, and loadouts.
    godot::Error set_catalog(const godot::String& catalog_ini, const godot::String& bf2_items_dir);
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
    core::Loadout loadout_;
    std::optional<core::Battlefield> battlefield_;
    int bot_count_ = 0;
    core::BotSkill bot_skill_;
    std::unique_ptr<core::World> world_;
    std::unique_ptr<core::SqliteCharacterStore> store_;
    std::unique_ptr<core::net::UdpLink> udp_;
    std::unique_ptr<core::GameServer> server_;
    std::unique_ptr<core::BotPlayers> bots_;
    std::optional<core::HubConfig> hub_config_;
    core::ItemCatalog catalog_;
    bool has_catalog_ = false;
    core::UdpHubNetwork hub_network_;
    std::unique_ptr<core::Hub> hub_;
};

} // namespace swm::godot_bindings
