#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "swm/core/world.h"
#include "swm/core/combat.h"
#include "swm/core/conquest.h"
#include "swm/core/movement.h"
#include "swm/core/net/protocol.h"
#include "swm/core/net/link.h"
#include "swm/core/persistence.h"

namespace swm::core {

struct ClientConfig {
    net::Endpoint server;
    std::string name = "player";
    double interpolation_delay = 0.1;  // seconds other players are shown in the past
    double hello_interval = 0.25;
    double timeout = 5;                // seconds without packets before giving up
    double smoothing = 10;             // 1/s: how fast a correction's visible jump fades
    Loadout loadout;                   // outside a match; must match the server's
    CharacterStats base_stats;         // before gear; must match the server's
    std::optional<Battlefield> battlefield;  // in a match: the server's, for its loadouts
    double spawn_request_interval = 0.25;
    uint64_t ticket = 0;               // from a hub, for the match it sent us to
    int team = -1;                     // asked for: -1 any; natives_team, a battlefield's natives (bots)
    // Outside a match: the world's vehicle classes (the server's, in its order).
    std::vector<VehicleClass> world_vehicles;
};

// Game client. Predicts its own character by running each input locally as
// soon as it's made, and reconciles when a snapshot arrives: it takes the
// server's state for the newest input the server has simulated and replays
// the inputs the server hasn't seen yet. Other characters are shown slightly
// in the past, interpolated between snapshots.
//
// In a match the character starts out waiting: request_spawn() picks a class
// and a command post, and the server spawns it there when it can.
class GameClient {
public:
    // transferring: the server sent us elsewhere (see transfer(); ClientSession follows it).
    enum class Status { connecting, connected, refused, timed_out, disconnected, transferring };

    // Another character as the newest snapshots describe it, its state
    // interpolated.
    using Remote = net::EntityState;

    struct Stats {
        uint32_t ticks = 0;          // inputs made
        uint32_t snapshots = 0;
        uint32_t corrections = 0;    // snapshots whose state differed from the prediction
        double max_correction = 0;   // meters
        double rtt = 0;              // seconds, smoothed
    };

    GameClient(net::Link& link, const World& world, ClientConfig config);

    // Input for the next ticks: axes -1..1, yaw and pitch in radians, the
    // weapon slot wanted (see MoveInput).
    struct Controls {
        double forward = 0;
        double right = 0;
        double yaw = 0;
        double pitch = 0;
        bool sprint = false;
        bool jump = false;
        bool fire = false;
        bool reload = false;
        bool secondary = false;  // throw a grenade
        bool crouch = false;     // crouch, stand, or roll on the move (see MoveInput::crouch)
        bool use = false;        // get into the vehicle at hand, or out of the one driven (a press)
        bool block = false;      // melee block / timed parry
        bool aim = false;        // aimed down the sights (zoomed in)
        int weapon = 0;
    };
    void set_input(const Controls& controls);
    // Receives packets, runs due ticks and sends inputs. `now` is in seconds.
    void update(double now);
    void disconnect();

    Status status() const { return status_; }
    const ClientConfig& config() const { return config_; }
    // True once the server has placed the character.
    bool spawned() const { return spawned_; }
    uint32_t entity_id() const { return entity_; }
    // The saved character this client plays (0 if the server doesn't save).
    PersistentId character_id() const { return character_; }
    // Why the server refused the connection, if it did.
    const std::string& refusal() const { return refusal_; }
    double tick_rate() const { return tick_rate_; }
    // The newest predicted state.
    const MoveState& predicted() const { return current_; }
    // What to draw: interpolated between the last two ticks, with any
    // correction eased in.
    MoveState presented() const;
    std::vector<Remote> remotes() const;
    const Stats& stats() const { return stats_; }

    // Own character as the newest snapshot describes it (health, life, team,
    // loadout, score); its state is the server's, not the prediction.
    const net::EntityState& self() const { return self_; }
    double health() const { return self_.health; }
    double max_shield() const { return character_stats_.shield_capacity; }
    bool alive() const { return self_.life == Life::alive; }
    // What we carry, and our statistics with it.
    const Loadout& loadout() const;
    const CharacterStats& character_stats() const { return character_stats_; }
    // In a match: asks to spawn with one of our team's loadouts at a command
    // post, until the character spawns. Spawning again after dying needs a
    // new request.
    void request_spawn(int loadout, int post);
    // The match as of the newest snapshot, if there is one.
    const std::optional<MatchState>& match() const { return match_; }

    // A hub's war terminals and the sides its battles are between.
    const std::vector<net::Recruiter>& recruiters() const { return recruiters_; }
    const std::vector<net::Quartermaster>& quartermasters() const { return quartermasters_; }
    const std::vector<net::Vendor>& vendors() const { return vendors_; }
    const std::vector<net::TargetState>& targets() const { return targets_; }
    // A match's vehicles as of the newest snapshot (ours where we drove it
    // then; predicted() says where it is now).
    const std::vector<net::VehicleState>& vehicles() const { return vehicles_; }
    // The match's vehicle classes (the battlefield's).
    std::span<const VehicleClass> vehicle_classes() const;
    const std::vector<net::CorpseLootState>& corpses() const { return corpses_; }
    const std::vector<std::string>& factions() const { return factions_; }
    const std::map<std::string, int64_t>& reputation() const { return reputation_; }
    // How the server's people look (an entity's look - 1 indexes these).
    const std::vector<std::string>& looks() const { return looks_; }
    // Queue for a battle on a side (0..3) while at a war terminal, or
    // leave the queue (-1). Repeated until the server's status shows it.
    // `mode`: the core::GameMode wanted (Stage 11; net::QueueRequest::any_mode
    // for any); a Space Terminal queues for Space Assault whatever it says.
    void queue(int team, uint8_t mode = 0);
    // Authoritative inventory & loadouts synced from the server.
    uint32_t inventory_rev() const { return inventory_rev_; }
    const std::vector<ItemInstance>& items() const { return items_; }
    const std::vector<CharacterLoadout>& char_loadouts() const { return char_loadouts_; }
    const std::vector<Loadout>& custom_loadouts() const { return custom_loadouts_; }
    void save_loadout(const CharacterLoadout& loadout);
    void switch_loadout(int slot);
    void buy_item(const std::string& def_id, bool use_faction_points = true);
    void destroy_item(uint64_t item_id);
    void take_loot(uint32_t corpse_entity, int slot = -1);
    void sell_item(uint64_t item_id);
    void sell_all_junk();
    void use_item(uint64_t item_id);
    // Quests: the hub's quest people and mission terminals, our journal and
    // what was last offered (QuestSync), and asking to talk to one of them
    // (by entity), use a terminal (by index), or accept or abandon a quest.
    const std::vector<net::QuestNpc>& quest_npcs() const { return quest_npcs_; }
    const std::vector<net::MissionTerminal>& mission_terminals() const { return mission_terminals_; }
    const net::QuestSync& quests() const { return quests_; }
    // Our experience in each profession (ProfessionSync).
    const net::ProfessionSync& professions() const { return professions_; }
    // Stage 12: our kit, effects, datapad, contracts and bazaar page
    // (SpecialistSync), what specialists have put in the field (each
    // snapshot's), and asking to do something with them.
    const net::SpecialistSync& specialist() const { return specialist_; }
    const net::FieldState& field() const { return field_; }
    void specialist_request(const net::SpecialistRequest& request);
    const std::vector<net::Workbench>& workbenches() const { return workbenches_; }
    const net::CraftingSync& crafting() const { return crafting_; }
    void gather_extract(ResourceCategory category);
    // Service points (a trainer, the bank, a clinic, a cloning terminal) and the bank (InventorySync).
    void service(uint8_t action, const std::string& id, int64_t amount, uint64_t item_id);
    int64_t bank_credits() const { return bank_credits_; }
    const std::vector<ItemInstance>& bank_items() const { return bank_items_; }
    void harvest_corpse(uint32_t corpse_entity);
    void salvage_item(uint64_t item_id);
    // `mark`: the Mk to make (-1: the highest the crafter may).
    void craft_item(const std::string& schematic_id, std::vector<std::string> resource_ids,
                    std::vector<uint64_t> enhancement_item_ids, int exp_quality_pct, int batch_count, int mark = -1);
    // Dyes an owned piece ("index_color_1=5&..."; "" takes the dye out).
    void dye_item(uint64_t item_id, const std::string& dye);
    void save_recipe_preset(const std::string& name, const std::string& schematic_id,
                            std::vector<std::string> resource_ids, std::vector<std::string> enhancement_defs,
                            int exp_quality_pct);
    void delete_recipe_preset(const std::string& name);
    void talk_to(uint32_t npc_entity);
    void use_mission_terminal(int index);
    void accept_quest(const std::string& id);
    void abandon_quest(const std::string& id);
    // What the server says about us: queue, notices.
    const net::PlayerStatus& player_status() const { return player_status_; }
    // Where the server sent us (status transferring).
    const std::optional<net::Transfer>& transfer() const { return transfer_; }
    // Other characters' bolts in flight, moved on from the newest snapshot.
    // (Own bolts come from take_shots(), the moment they're fired.)
    std::vector<net::BoltState> bolts(double now) const;
    // Shots this client fired since the last call, predicted: draw them now.
    std::vector<Shot> take_shots();
    // Hits and kills since the last call.
    std::vector<net::CombatEvent> take_events();
    // Explosions since the last call.
    std::vector<net::Explosion> take_explosions();
    // Bolt impacts since the last call (for sparks and scorch marks).
    std::vector<net::Impact> take_impacts();
    // Dropped health, ammo, dual and energy canisters from the newest snapshot.
    const std::vector<net::PickupState>& pickups() const { return pickups_; }

private:
    struct Pending {
        MoveInput input;
        MoveState result;  // predicted state after this input
        double sent = 0;
    };
    struct Sample {
        uint32_t tick = 0;
        net::EntityState entity;
    };

    void receive(double now);
    void handle(const net::Datagram& datagram, double now);
    void reconcile(uint32_t last_input, const net::EntityState& server, double now);
    void tick(double now);
    void advance_clocks(double now);

    net::Link& link_;
    const World& world_;
    ClientConfig config_;
    Status status_ = Status::connecting;
    bool spawned_ = false;
    uint32_t entity_ = 0;
    PersistentId character_ = 0;
    std::string refusal_;
    double tick_rate_ = 30;
    MoveInput next_input_;
    uint32_t sequence_ = 0;
    std::deque<Pending> pending_;
    MoveState previous_;
    MoveState current_;
    Vec3 correction_;  // eased toward zero
    double next_tick_time_ = 0;
    double last_update_ = -1;
    double last_hello_ = -1e9;
    double last_heard_ = 0;
    bool started_ = false;
    // Other characters
    uint32_t latest_tick_ = 0;
    double latest_tick_time_ = 0;
    double render_tick_ = 0;
    std::map<uint32_t, std::deque<Sample>> history_;
    // Each entity's rarely changing strings, as last sent in full (Stage 13:
    // snapshots carry them only now and then).
    std::map<uint32_t, net::EntityState> static_cache_;
    // Fills an entity's strings in from the cache (or caches them when it
    // brought them). False: they were never sent, so it can't be drawn yet.
    bool fill_static(net::EntityState& entity);
    Stats stats_;
    // Combat and the match
    net::EntityState self_;
    CharacterStats character_stats_;  // of loadout()
    std::optional<MatchState> match_;
    std::optional<net::SpawnRequest> spawn_request_;
    double last_spawn_request_ = -1e9;
    // Hubs
    std::vector<net::Recruiter> recruiters_;
    std::vector<net::Quartermaster> quartermasters_;
    std::vector<net::Vendor> vendors_;
    int64_t bank_credits_ = 0;
    std::vector<ItemInstance> bank_items_;
    std::vector<net::QuestNpc> quest_npcs_;
    std::vector<net::MissionTerminal> mission_terminals_;
    std::vector<net::Workbench> workbenches_;
    net::QuestSync quests_;
    net::ProfessionSync professions_;
    net::SpecialistSync specialist_;
    net::FieldState field_;
    net::CraftingSync crafting_;
    std::vector<net::TargetState> targets_;
    std::vector<net::VehicleState> vehicles_;
    std::vector<net::CorpseLootState> corpses_;
    std::vector<std::string> factions_;
    std::map<std::string, int64_t> reputation_;
    std::vector<std::string> looks_;
    net::PlayerStatus player_status_;
    int wanted_queue_ = -2;  // -2: no request
    uint8_t wanted_mode_ = 0;
    int queue_attempts_ = 0;  // requests sent: the hub may refuse (no such battle here), so only a few
    double last_queue_request_ = -1e9;
    uint32_t inventory_rev_ = 0;
    std::vector<ItemInstance> items_;
    std::vector<CharacterLoadout> char_loadouts_;
    std::vector<Loadout> custom_loadouts_;
    std::optional<net::Transfer> transfer_;
    std::vector<net::BoltState> bolts_;  // others', from the newest snapshot
    double bolts_time_ = 0;
    std::vector<Shot> shots_;
    std::vector<net::CombatEvent> events_;
    std::vector<net::Explosion> explosions_;
    std::vector<net::Impact> impacts_;
    std::vector<net::PickupState> pickups_;
};

// A client that follows the server's transfers, from a hub to a match and
// back. Each server gets a new GameClient: entity ids and everything else
// start over, and generation() counts them. `battlefields` finds a match's
// battlefield by name, for its loadouts, and `worlds` the world it's fought
// in (null, or no `worlds`: the hub's, `world`). Back at the hub, the
// client is in `world` again.
class ClientSession {
public:
    using BattlefieldFinder = std::function<std::optional<Battlefield>(const std::string& name)>;
    using WorldFinder = std::function<std::shared_ptr<const World>(const Battlefield&)>;
    // A planet's world by its bake's name (Stage 13: a transfer to another planet's hub).
    using PlanetFinder = std::function<std::shared_ptr<const World>(const std::string& planet)>;

    ClientSession(net::Link& link, const World& world, ClientConfig config, BattlefieldFinder battlefields,
                  WorldFinder worlds = {}, PlanetFinder planets = {}, std::string home_planet = {});

    // Follows a pending transfer (the new client starts on the next call),
    // or updates the client.
    void update(double now);
    GameClient& client() { return *client_; }
    const GameClient& client() const { return *client_; }
    // The world the client is in now.
    const World& world() const { return battle_world_ ? *battle_world_ : world_; }
    // The planet it's on (its bake's name; empty: the home planet's, or in a battle).
    const std::string& planet() const { return planet_; }
    int generation() const { return generation_; }
    // Why the last transfer couldn't be followed, if it couldn't.
    const std::string& error() const { return error_; }

private:
    net::Link& link_;
    const World& world_;
    ClientConfig config_;
    BattlefieldFinder battlefields_;
    WorldFinder worlds_;
    PlanetFinder planets_;
    std::string home_planet_;
    std::string planet_;  // empty: home
    std::map<std::string, std::shared_ptr<const World>> planet_worlds_;  // loaded once each
    std::shared_ptr<const World> battle_world_;  // outlives client_, which refers to it
    std::unique_ptr<GameClient> client_;
    int generation_ = 0;
    std::string error_;
};

} // namespace swm::core
