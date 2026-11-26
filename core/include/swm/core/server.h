#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "swm/core/combat.h"
#include "swm/core/conquest.h"
#include "swm/core/crafting.h"
#include "swm/core/items.h"
#include "swm/core/movement.h"
#include "swm/core/net/link.h"
#include "swm/core/net/protocol.h"
#include "swm/core/persistence.h"
#include "swm/core/profession.h"
#include "swm/core/quest.h"
#include "swm/core/species.h"
#include "swm/core/specialist.h"
#include "swm/core/spatial.h"
#include "swm/core/world.h"

namespace swm::core {

struct PlayerProfile {
    Species species = Species::human;
    Gender gender = Gender::male;
    std::string appearance;
};

struct ServerConfig {
    double tick_rate = 30;
    Vec3 spawn;                  // game space; y comes from the ground
    double spawn_spacing = 3;    // meters between players' spawn points, along x (8 points)
    double client_timeout = 5;   // seconds of silence before a client is dropped
    size_t max_inputs_per_tick = 3;   // lets a client catch up after jitter, but not speed up for long
    size_t max_queued_inputs = 30;
    double save_interval = 10;   // seconds between saves of every connected character
    double respawn_delay = 5;    // seconds dead before respawning (in a match: before choosing a spawn)
    double max_rewind = 0.5;     // seconds: the most the server rewinds targets for a shooter
    Loadout loadout;             // everyone's outside a match; must match the clients'
    CharacterStats base_stats;   // before gear; must match the clients'
    // With a battlefield the server runs a conquest match on it: players
    // join a team, choose a class and a command post to spawn at, and the
    // match's rules decide who wins. Clients must load the same battlefield.
    // Characters aren't saved in a match.
    std::optional<Battlefield> battlefield;
    const ItemCatalog* catalog = nullptr;
    // The profession roster (outliving the server): experience is earned and
    // loadouts' Active Specialization Slots apply with one.
    const ProfessionBook* professions = nullptr;
    // Stage 12: the specialist and companion professions' abilities,
    // companions, droid modules and slices (outliving the server).
    const SpecialistBook* specialists = nullptr;
    // Outside a match: the world's vehicle classes (game/vehicles/), called
    // by their owners from items (ItemKind::vehicle).
    std::vector<VehicleClass> world_vehicles;
    const CraftingBook* crafting = nullptr;
    bool friendly_fire = false;  // in a match
    bool single_match = false;   // one match, no next one (a hub's match: see match_over())
    bool pvp_enabled = true;     // false in open-world hubs (no player-vs-player harm outside matches)
    // Extracting resources from the ground: it takes this many seconds, standing
    // still, and the next must be this many meters from where the last was.
    double extract_time = 6.0;
    double extract_spacing = 25.0;
    // Stage 13: interest management outside a match (a match sends everything).
    // NPCs farther than npc_sim_radius from every player in the world stand
    // still, unsimulated (0: always simulate). A client is sent NPCs within
    // npc_view_radius, other players within player_view_radius, and bolts,
    // explosions and impacts within effect_view_radius (0: all); once sent,
    // an entity is kept until it's view_margin farther, so it doesn't blink
    // at the edge.
    double npc_sim_radius = 350;
    double npc_view_radius = 240;
    double player_view_radius = 1200;
    double effect_view_radius = 450;
    double view_margin = 30;
    // Stage 13: the planet this server's world is (e.g. "Tatooine"), kept as
    // each character's location; empty: no planets (a match, a test).
    std::string planet;
};

inline constexpr int hostile_npc_team = 100;

// Stage 13: what a server's ticks cost, over a window (GameServer::take_perf).
struct ServerPerf {
    uint64_t ticks = 0;
    double tick_seconds = 0;      // total time spent in ticks
    double worst_tick = 0;        // seconds, the slowest tick
    uint64_t npc_ticks = 0;       // NPCs simulated, summed over ticks
    uint64_t npc_frozen = 0;      // NPCs left standing (no player near), summed over ticks
    uint64_t snapshots = 0;
    uint64_t snapshot_bytes = 0;
    size_t largest_snapshot = 0;  // bytes
    uint64_t entities_sent = 0;   // entities in snapshots, summed
    uint64_t static_sent = 0;     // of which with their strings
    double window = 0;            // seconds the window covers (set by take_perf)
};

struct PlayerResult {
    std::string name;
    int team = -1;
    uint16_t kills = 0, deaths = 0, captures = 0;
    std::map<std::string, int64_t> profession_xp;  // earned in the match, by profession
    int remaining_std_ammo = -1;
    int remaining_hvy_ammo = -1;
    // Stage 11 telemetry: the loadout's primary weapon, and kills by weapon.
    std::string primary_weapon;
    std::map<std::string, uint16_t> weapon_kills;
};

struct MatchResult {
    std::string battlefield;  // its name
    int winner = -1;          // -1: a draw
    std::vector<PlayerResult> players;
    // Stage 11 telemetry: how it was fought and how it ended.
    GameMode mode = GameMode::conquest;
    std::array<std::string, 2> teams;  // the two sides' names
    std::array<double, 2> tickets{};   // reinforcements left
    std::array<double, 2> scores{};    // points (space assault), captures (flags) or kills (hunt)
    double duration = 0;               // seconds live
    uint16_t captures = 0;             // command posts taken or neutralized, and flags captured
};

// Authoritative game server. Clients send inputs; the server simulates every
// character from those inputs alone and sends each client a snapshot of the
// world every tick. A character only moves when its inputs arrive, so the
// server's result for an input is exactly what the client predicted.
//
// Combat: shots become bolts that fly through the world. Bolts are tested
// against other characters where the shooter saw them: each input says which
// server tick its client was showing others at, and the server rewinds them
// that far (up to `max_rewind`).
//
// Outside a match everyone is hostile and respawns where they first spawned.
// In a match (ServerConfig::battlefield) players are put on the smaller team
// and wait until their client asks to spawn at a command post their team
// holds; see Conquest for the rules.
//
// With a CharacterStore (and no match), a player's name picks their saved
// character: it spawns where it was last saved. Characters are saved when
// they leave or time out, every `save_interval`, and on save_all() (call it
// at shutdown).
//
// Hubs and the matches they launch (see Hub) use the hooks below: war
// terminals and queue requests, admission by ticket, transfers to another
// server, and the match's result.
class GameServer {
public:
    GameServer(net::Link& link, const World& world, ServerConfig config, CharacterStore* store = nullptr);

    // Receives packets and runs any ticks that are due. `now` is in seconds,
    // from any steady clock.
    void update(double now);

    uint32_t tick() const { return tick_; }
    size_t client_count() const { return clients_.size(); }
    std::vector<uint32_t> entities() const;
    // A player's or an NPC's.
    const MoveState* state_of(uint32_t entity) const;
    // Moves a character, e.g. a teleport. Its client gets corrected.
    void set_state(uint32_t entity, const MoveState& state);
    double health_of(uint32_t entity) const;
    bool alive(uint32_t entity) const;
    // The entity as snapshots describe it, if it's connected.
    std::optional<net::EntityState> describe(uint32_t entity) const;
    std::optional<PlayerProfile> profile_of(uint32_t entity) const;
    void set_player_profile(uint32_t entity, const PlayerProfile& profile);
    size_t bolt_count() const { return bolts_.size(); }
    size_t mine_count() const { return mines_.size(); }
    size_t pickup_count() const { return pickups_.size(); }
    // Drops a health, ammo, dual or energy canister on the ground at `position`.
    void spawn_pickup(PickupKind kind, Vec3 position);
    // The match, if there is one.
    const Conquest* match() const { return conquest_ ? &*conquest_ : nullptr; }
    // Its vehicles as snapshots describe them (server_vehicles.cpp).
    std::vector<net::VehicleState> vehicles() const;
    // A single match is over once its results have shown.
    bool match_over() const;
    // Saves every connected character now. Storage errors are logged.
    void save_all();

    // The war terminals players can queue at and the sides they queue for,
    // told to clients as they join.
    void set_recruiters(std::vector<net::Recruiter> recruiters, std::vector<std::string> factions);
    // Faction quartermasters in a hub, told to clients as they join.
    void set_quartermasters(std::vector<net::Quartermaster> quartermasters);
    // Open-world vendors (junk dealers, gear/supplies vendors) in a hub.
    void set_vendors(std::vector<net::Vendor> vendors);
    // A hub's quests: its quest book (outliving the server), the people who
    // give quests or take part in them, and its mission terminals, told to
    // clients as they join. Characters' journals are saved with them.
    void set_quests(const QuestBook* book, std::vector<net::QuestNpc> npcs,
                    std::vector<net::MissionTerminal> terminals);
    // A player killed a creature or bandit at `at`: advances their quests.
    void quest_kill(uint32_t killer, const KillTags& tags, Vec3 at);
    // A connected player's quest journal; nullptr if not connected.
    const std::vector<QuestProgress>* quests_of(uint32_t entity) const;
    // Adds a shooting-range target in a hub (stationary if speed <= 0).
    uint32_t add_range_target(std::string name, Vec3 home, Vec3 patrol_end, double speed, double max_health = 400,
                              double mitigation = 0, double respawn_time = 2.5);
    // Custom loadouts for a player (in a hub, or handed from a hub to a match).
    void set_player_loadouts(uint32_t entity, std::vector<Loadout> loadouts,
                             std::vector<std::string> worn_per_loadout = {});
    // Resolves a connected hub player's saved loadouts (with or without quality).
    std::vector<Loadout> build_player_loadouts(uint32_t entity, bool apply_quality,
                                               std::vector<std::string>* out_worn = nullptr) const;
    // Persists battle rewards & faction reputation for a player by name and updates live balance if connected.
    void award_battle_rewards(const std::string& name, const std::string& faction, const BattleReward& reward);
    // Syncs remaining unified ammo quantities back from a finished battle to the player's equipped stacks.
    void apply_match_ammo_consumption(const std::string& name, int remaining_std_ammo, int remaining_hvy_ammo);
    // Professions: experience a player earned in a match (by profession), and
    // `routed` more for the professions their active loadout routes it to
    // (routed_professions); saved, and shown if they're here.
    void award_profession_xp(const std::string& name, const std::map<std::string, int64_t>& earned, int64_t routed);
    const ProfessionBook* profession_book() const { return config_.professions; }
    // Its store, if it saves (Stage 11: hubs record match telemetry there).
    CharacterStore* store() const { return store_; }
    // A connected player's experience in each profession; nullptr if not connected.
    const std::vector<ProfessionProgress>* professions_of(uint32_t entity) const;
    // Crafting & Material Vault (server_crafting.cpp).
    void set_crafting(const CraftingBook* book, std::vector<net::Workbench> workbenches);
    const std::vector<MaterialStack>* materials_of(uint32_t entity) const;
    const std::vector<RecipePreset>* presets_of(uint32_t entity) const;
    // What this client's snapshots say about it; nullptr if not connected.
    net::PlayerStatus* status(uint32_t entity);
    // Shows a client a message.
    void notify(uint32_t entity, const std::string& text);
    // Sends a client (or everyone) to another server. It's dropped, and
    // saved, when it says goodbye or after a few seconds.
    void transfer(uint32_t entity, const net::Transfer& transfer);
    void transfer_all(const net::Transfer& transfer);
    std::string name_of(uint32_t entity) const;

    // People the server runs itself (a hub's: its crowd and the people
    // standing or sitting about), drawn by `looks` (told to clients as
    // they join; an NPC's look is an index into them). They walk as players
    // do, by the input they're steered with, and take no harm.
    void set_looks(std::vector<std::string> looks) {
        looks_ = std::move(looks);
        add_companion_looks();  // Stage 12: after the hub's own, so theirs keep their indices
    }
    // Adds one standing on the ground under `position` (from a meter
    // over it), facing `yaw`; returns its entity. `colors`: 0 for its look's
    // own colors, else a seed clients vary them by (net::EntityState).
    // `radius`: its body's, against walls (a creature's), 0 for a person's
    // (the loadout's).
    uint32_t add_npc(const std::string& name, Vec3 position, double yaw, uint8_t look, uint8_t pose = 0,
                     uint16_t colors = 0, double radius = 0);
    struct NpcCombatSpec {
        uint8_t actor_kind = 1;  // 1=citizen, 2=wild creature, 3=bandit
        bool passive = false;    // a creature that leaves you be unless you hit it
        double max_health = 100;
        double hit_radius = 0.45;
        double hit_height = 1.8;
        double mitigation = 0.0;
        double walk_speed = 4.0;
        double run_speed = 6.5;
        Loadout loadout;
        std::string weapon_model;
        std::string anim_set;
    };
    struct NpcInfo {
        uint32_t entity = 0;
        std::string name;
        MoveState state;
        double health = 0;
        double max_health = 0;
        Life life = Life::alive;
        uint8_t actor_kind = 1;
        uint32_t last_attacker = 0;
        uint32_t last_damaged_tick = 0;
        bool hidden = false;
    };
    uint32_t add_combat_npc(const std::string& name, Vec3 position, double yaw, uint8_t look,
                            const NpcCombatSpec& spec, uint16_t colors = 0);
    std::optional<NpcInfo> npc_info(uint32_t entity) const;
    void set_npc_pose(uint32_t entity, uint8_t pose);
    void heal_npc(uint32_t entity, double amount);
    void respawn_npc(uint32_t entity, Vec3 position, double yaw);
    void set_npc_hidden(uint32_t entity, bool hidden);
    void npc_melee(uint32_t attacker_entity, uint32_t victim_entity, double raw_damage, DamageType type);
    void award_xp(uint32_t entity, int64_t xp);
    void award_credits(uint32_t entity, int64_t credits);
    void set_corpse_loot(uint32_t npc_entity, uint32_t owner_entity, const std::string& name, Vec3 position,
                         std::vector<net::LootItemEntry> items);
    void clear_corpse_loot(uint32_t npc_entity);
    size_t corpse_loot_count() const { return corpse_loots_.size(); }
    void set_pvp_enabled(bool enabled) { config_.pvp_enabled = enabled; }
    void set_cloning_facility(Vec3 position) { cloning_facility_ = position; }
    // Stage 13: every town's cloning facilities (name, position); the dead
    // are cloned at the nearest to where they fell (unless bound elsewhere).
    void set_cloning_facilities(std::vector<std::pair<std::string, Vec3>> facilities) {
        cloning_facilities_ = std::move(facilities);
    }
    // Stage 13: more service points (another planet's starports, as
    // "travel_offworld": Detail "<planet>|<fare>"), for clients joining after.
    void add_vendors(std::vector<net::Vendor> vendors) {
        vendors_.insert(vendors_.end(), vendors.begin(), vendors.end());
    }
    const std::vector<net::Vendor>& vendors() const { return vendors_; }
    // Stage 13: the next time `name` joins, they're put there (off a
    // starship from another planet) instead of where they were saved. A
    // height below -1e8: on the ground there.
    void expect_arrival(const std::string& name, Vec3 position) { arrivals_[name] = position; }
    // A player paid for a ticket to another planet's starport (a
    // "travel_offworld" service by that name): the galaxy sends them there.
    std::function<void(uint32_t entity, const std::string& destination)> on_offworld_travel;
    // A character saved on another planet (its location) joined: true if
    // the galaxy is sending them there (they keep that location meanwhile).
    std::function<bool(uint32_t entity, const std::string& location)> on_wrong_planet;
    const ItemCatalog* catalog() const { return config_.catalog; }
    void steer_npc(uint32_t entity, const MoveInput& input);
    std::vector<uint32_t> npcs() const;
    // Stage 12 (server_specialists.cpp): the specialist roster, a player's
    // kit and datapad, their fielded companion, and what's in the field.
    const SpecialistBook* specialist_book() const { return config_.specialists; }
    const SpecialistState* specialist_state(uint32_t entity) const;
    std::optional<ActiveCompanion> companion_of(uint32_t owner) const;
    size_t deployable_count() const { return deployables_.size(); }
    size_t hazard_count() const { return hazards_.size(); }
    // A bazaar terminal's listings, everyone's (they're saved with the world).
    const std::vector<BazaarListing>& bazaar_listings() const { return bazaar_; }
    // The field as a player sees it (what their snapshots carry).
    net::FieldState field_state_for(uint32_t entity) const;
    // Whether `entity` is hidden from `viewer` (cloaked, or in smoke, and an enemy).
    bool hidden_from(uint32_t viewer, uint32_t entity) const;
    // Whether a player is cloaked (creatures and outlaws don't pick them as targets).
    bool cloaked(uint32_t entity) const;
    // Stage 13: takes an NPC out of the world for good (a companion dismissed).
    void remove_npc(uint32_t entity);
    // Where the players in the world are (alive or dead, not waiting to spawn).
    std::vector<Vec3> player_positions() const;
    // Whether an NPC there is simulated (a player is within npc_sim_radius,
    // as of the last tick; always in a match). Steering one that isn't is wasted.
    bool npc_awake_at(Vec3 position) const;
    // Whether a player in the world is within `radius` of there, as of the last tick.
    bool player_near(Vec3 position, double radius) const { return player_grid_.any_near(position, radius); }
    // What ticks have cost since the last call, and starts a new window.
    ServerPerf take_perf(double now);
    const ServerConfig& config() const { return config_; }

    std::function<void(const std::string&)> log;
    std::function<void(uint32_t npc_entity, uint32_t killer_entity)> on_npc_killed;
    // Decides who may join, given the team they ask for (-1: any;
    // natives_team: a native, a bot): their team (-1: the smaller one), or
    // nullopt to refuse. Unset: anyone, on the smaller team (natives on
    // theirs, while there's room).
    std::function<std::optional<int>(const std::string& name, uint64_t ticket, int team)> admit;
    std::function<void(uint32_t entity, const std::string& name)> on_join;
    // A player standing at a war terminal asked to queue for a side (-1: leave).
    // Stage 11: `mode` is the core::GameMode asked for (or
    // net::QueueRequest::any_mode), `terminal` the net::Recruiter::Kind of
    // the terminal it stands at (ground, space or both).
    std::function<void(uint32_t entity, int team, uint8_t mode, uint8_t terminal)> on_queue;
    // A match just ended.
    std::function<void(const MatchResult&)> on_match_ended;

private:
    struct Client {
        net::Endpoint endpoint;
        std::string name;
        uint32_t entity = 0;
        PersistentId character = 0;  // 0 without a store
        Species species = Species::human;
        Gender gender = Gender::male;
        std::string location = "Mos Eisley";
        std::string appearance = "{}";
        MoveState state;
        double health = 0;
        Life life = Life::alive;
        uint32_t respawn_tick = 0;
        uint8_t spawn_count = 0;
        int team = -1;
        size_t loadout = 0;          // index into the team's loadouts (in a match)
        CharacterStats stats;        // with the loadout's gear
        std::optional<net::SpawnRequest> wants_spawn;
        double outside = 0;  // seconds out of bounds
        int remote_chair = -1;
        uint16_t kills = 0, deaths = 0, captures = 0;
        std::map<std::string, uint16_t> weapon_kills;  // in a match: kills by weapon name (telemetry)
        std::deque<std::pair<uint32_t, Vec3>> history;  // recent positions, for rewinding
        uint32_t last_input = 0;  // newest simulated input
        std::deque<MoveInput> queue;
        double last_heard = 0;
        net::PlayerStatus status;
        std::optional<net::Transfer> transfer;  // sent every tick until it leaves
        uint32_t transfer_tick = 0;            // dropped then if it hasn't left
        int64_t credits = 0;
        int64_t faction_points = 0;
        int64_t experience = 0;
        std::map<std::string, int64_t> reputation;
        std::vector<ItemInstance> items;
        std::vector<CharacterLoadout> char_loadouts;
        std::vector<Loadout> custom_loadouts;
        std::vector<std::string> worn_per_loadout;
        uint32_t inventory_rev = 0;
        uint32_t acked_inventory_rev = 0;
        uint32_t last_sync_tick = 0;
        int switching_to = -1;
        double switch_remaining = 0.0;
        std::vector<QuestProgress> quests;
        uint32_t quest_rev = 0;
        uint32_t acked_quest_rev = 0;
        uint32_t quest_sync_tick = 0;
        uint32_t quest_inventory_rev = 0;  // the inventory revision quests last looked at
        std::vector<std::string> offers;   // quest ids the last giver or terminal offered
        std::string offer_source;
        std::string offer_note;  // the last giver offered nothing: why
        uint16_t offer_serial = 0;
        uint32_t notice_tick = UINT32_MAX;  // when the notice was last set
        bool use_held = false;              // the use button, last input
        std::vector<ProfessionProgress> professions;
        uint32_t profession_rev = 0;
        uint32_t acked_profession_rev = 0;
        uint32_t profession_sync_tick = 0;
        mutable bool professions_dirty = false;  // not saved since it last changed
        std::map<std::string, int64_t> battle_profession_xp;  // in a match: for the hub to save
        double armor_xp = 0;  // toward the armor profession's next point, from hits taken
        double shield = 0;
        double shield_recharge_timer = 0;
        double shield_disrupt_timer = 0;
        double secondary_recharge_timer = 0;
        double ammo_sync_timer = 0;
        int synced_std_ammo = -1;
        int synced_hvy_ammo = -1;
        std::vector<MaterialStack> materials;
        std::vector<RecipePreset> presets;
        uint32_t crafting_rev = 0;
        uint32_t acked_crafting_rev = 0;
        uint32_t crafting_sync_tick = 0;
        double extract_started = -1.0;  // when the extraction under way began; < 0: none
        Vec3 extract_from;              // where it began (you must stay there)
        ResourceCategory extract_category = ResourceCategory::mineral;
        bool has_extracted = false;
        Vec3 last_extract_site;         // where the last one finished
        int64_t bank_credits = 0;
        std::vector<ItemInstance> bank_items;  // ids are the bank's own
        uint64_t next_bank_id = 1;
        std::optional<Vec3> bound;      // where their clone is kept (cloning terminals), else the hub's facility
        // Stage 13: what its last snapshot had (interest management), and
        // which entities' strings it has been sent (net::EntityState::has_static).
        std::set<uint32_t> seen_entities;
        struct SentStatic {
            uint32_t key = 0;
            uint32_t since = 0;      // tick the key was first sent
            uint32_t last_full = 0;  // tick the strings were last sent
        };
        std::map<uint32_t, SentStatic> sent_static;
        SpecialistState spec;  // Stage 12: kit, effects, datapad, contracts (server_specialists.cpp)
    };
    // A vehicle in a match. Empty, it keeps its own state; driven, its
    // driver's state is its state (MoveState::vehicle), and the driver's
    // own weapons wait here.
    struct Vehicle {
        uint32_t id = 0;
        int cls = 0;     // an index into the battlefield's vehicles
        int spawn = -1;  // an index into its vehicle spawns
        int team = -1;
        MoveState state;
        double health = 0;
        uint32_t driver = 0;
        CombatState driver_combat;
        std::vector<uint32_t> gunners;            // per seat: who sits there (0: no one)
        std::vector<CombatState> gunner_combat;   // per seat: its gunner's own weapons, waiting
        uint32_t owner = 0;                       // in the world: who called it (only they ride it)
        double away = 0;        // seconds empty away from its spawn
        double dead_for = -1;   // seconds since destroyed; -1 alive
        double auto_cooldown = 0;  // seconds until automated ship turret fires next
        // Stage 11: its tow cable on a walker (0: none), who fired it, the
        // bearing from the walker it was last at, how far round it's wound
        // (radians, signed) and for how long.
        uint32_t towing = 0;
        uint32_t tow_gunner = 0;
        double tow_bearing = 0, tow_wound = 0, tow_time = 0;
    };
    struct RangeTarget {
        uint32_t entity = 0;
        std::string name;
        Vec3 home;
        Vec3 patrol_end;
        Vec3 position;
        double speed = 0;
        double phase = 0;
        double health = 400;
        double max_health = 400;
        double mitigation = 0;
        double respawn_time = 2.5;
        double respawn_timer = 0;
        bool alive = true;
    };
    struct Npc {
        uint32_t entity = 0;
        std::string name;
        MoveState state;
        MoveInput input;
        uint8_t look = 0, pose = 0;
        uint8_t actor_kind = 1;
        bool passive = false;
        uint16_t colors = 0;
        double radius = 0;  // 0: a person's
        double health = 100;
        double max_health = 100;
        double hit_radius = 0.45;
        double hit_height = 1.8;
        double mitigation = 0.0;
        double walk_speed = 4.0;
        double run_speed = 6.5;
        Life life = Life::alive;
        uint8_t spawn_count = 0;
        Loadout loadout;
        std::string weapon_model;
        std::string anim_set;
        uint32_t last_attacker = 0;
        uint32_t last_damaged_tick = 0;
        bool hidden = false;
    };
    struct Bolt {
        uint32_t id = 0;
        uint32_t owner = 0;
        int team = -1;
        uint8_t weapon = 0;
        WeaponDef def;  // snapshot of the weapon's projectile and explosion
        Vec3 position;
        Vec3 velocity;
        double life = 0;     // seconds left (a grenade's fuse)
        double gravity = 0;  // m/s^2
        double rewind = 0;   // ticks: targets are tested this far in the past
        bool resting = false;  // a grenade that stopped bouncing
        bool touched = false;  // a grenade that's hit something (its timer may start then)
        // A placed mine or trap at rest: seconds until it's armed (or a
        // trap's next shot), and the shots its trap has left.
        double wait = 0;
        int shots_left = 0;
        double health = 0;  // a mine at rest: shot or blasted to nothing, it's destroyed (harmlessly)
    };

    struct Pickup {
        uint32_t id = 0;
        PickupKind kind = PickupKind::health_small;
        Vec3 position;
        double life = 0;  // seconds left
    };
    struct Blast {
        Bolt bolt;
        Vec3 at;
        Vec3 normal{0, 1, 0};
        bool droid = false;
        int destructible = -1;  // a destroyed object's own explosion (its index)
        int vehicle = -1;       // a destroyed vehicle's (its class)
    };
    struct CorpseLoot {
        uint32_t npc_entity = 0;
        uint32_t owner_entity = 0;
        std::string name;
        Vec3 position;
        std::vector<net::LootItemEntry> items;
        bool harvested = false;
    };

    void receive(double now);
    void handle(const net::Endpoint& from, const net::Datagram& datagram, double now);
    Client* join(const net::Endpoint& from, const std::string& name, uint64_t ticket, int team = -1);
    // Whether teams a and b don't fight (Battlefield::allied; without a
    // battlefield, the same team).
    bool allied(int a, int b) const;
    void save(std::span<const Client> clients);
    void step();
    void run_match();
    void fire(Client& shooter, const Shot& shot, double view_tick);
    void fire_npc(const Npc& shooter, const Shot& shot);
    void advance_bolts();
    void advance_pickups();
    void drop_pickup(Vec3 at);
    double random_unit();
    void damage(Client& victim, double raw, DamageType type, uint32_t attacker, double shield_disrupt = 0.0,
                double shield_factor = 1.0);
    void damage_npc(Npc& npc, double raw, DamageType type, uint32_t attacker);
    Npc* find_npc(uint32_t entity);
    const Npc* find_npc(uint32_t entity) const;
    void damage_powerup(size_t index, double amount, uint32_t attacker, std::vector<Blast>& blasts);
    void explode(const Blast& blast, std::vector<Blast>& blasts);
    void kill(Client& victim, uint32_t attacker, bool is_flyer = false);
    void respawn(Client& client);
    bool is_mobile_spawn(const Vehicle& vehicle, int team, const Battlefield& field,
                         std::span<const VehicleClass> classes) const;
    bool can_spawn_at(int team, size_t post) const;
    void spawn_at_post(Client& client, size_t loadout, size_t post);
    void return_to_waiting(Client& client);
    void rebuild_client_loadouts(Client& client);
    void scrub_item_from_loadouts(Client& client, uint64_t item_id);
    ItemInstance grant_stacked_item(Client& client, const ItemDef& def, int delta_qty, double quality);
    void sync_client_ammo(Client& client, bool flush_now);
    void send_inventory_sync(Client& client);
    // Vehicles (server_vehicles.cpp).
    std::span<const VehicleClass> vehicle_classes() const;
    void update_vehicles(double dt);
    void use_vehicle(Client& client);
    void leave_vehicle(Client& client);
    Vehicle* vehicle_of(uint32_t driver);
    Vehicle* vehicle_gunned_by(uint32_t gunner);  // the one it sits in as a gunner
    // Stage 11: a tow-cable seat's shot: harpoons the nearest enemy AT-AT in
    // reach, if the snowspeeder flies.
    void fire_tow_cable(Client& gunner, const VehicleSeat& seat);
    // Winds each cable as its snowspeeder circles; trips the walker, or snaps.
    void update_tow_cables(double dt);
    // In the world: an owned vehicle item used: its vehicle called beside
    // its owner, or (called already) put away.
    void call_vehicle(Client& client, const ItemDef& def);
    void damage_vehicle(Vehicle& vehicle, double amount, uint32_t attacker, std::vector<Blast>& blasts);
    net::VehicleState describe(const Vehicle& vehicle) const;
    // Professions (server_professions.cpp).
    std::string held_profession(const Client& client) const;   // the weapon in hand's
    std::string armor_profession(const Client& client) const;  // the loadout's armor's
    void earn_profession_xp(Client& client, const std::string& id, int64_t xp);
    // Experience for what isn't one profession's: to the active loadout's routed professions.
    void route_xp(Client& client, int64_t xp);
    void save_professions(Client& client);
    void send_profession_sync(Client& client);
    void apply_professions(const Client& client, const CharacterLoadout& cl, Loadout& loadout) const;
    // Crafting & gathering (server_crafting.cpp).
    void handle_gather_request(Client& client, const net::GatherRequest& request, double now);
    void update_extractions(double now);
    void quest_event(Client& client, const std::string& name);
    // Service points (server_services.cpp): trainers, the bank, clinics and cloning terminals.
    void handle_service_request(Client& client, const net::ServiceRequest& request);
    void load_services_data(Client& client);
    void save_bank(Client& client);
    const net::Vendor* service_near(const Client& client, const std::string& kind, const std::string& detail = "") const;
    int combat_level_of(const Client& client) const;
    void finish_extraction(Client& client, ResourceCategory category);
    void handle_craft_request(Client& client, const net::CraftRequest& request);
    void send_crafting_sync(Client& client);
    void save_materials(Client& client);
    // Quests (server_quests.cpp).
    void handle_quest_request(Client& client, const net::QuestRequest& request);
    void apply_quests(Client& client, QuestEffects effects);
    void check_quest_positions();
    void send_quest_sync(Client& client);
    net::QuestInfo describe_quest(const Client& client, const QuestDef& def, const QuestProgress* progress) const;
    std::string step_text(const QuestStep& step) const;
    std::string item_name(const std::string& def_id) const;
    int held_count(const Client& client, const std::string& def_id) const;
    void give_item(Client& client, const std::string& def_id, double quality);
    void take_items(Client& client, const std::string& def_id, int count);
    void save_economy(Client& client);
    const Loadout& loadout_of(const Client& client) const;
    net::EntityState describe(const Client& client) const;
    Vec3 spawn_point(uint32_t index) const;
    Vec3 position_at(const Client& client, double tick) const;
    void damage_destructible(size_t index, double amount, uint32_t attacker, std::vector<Blast>& blasts);
    void damage_target(RangeTarget& target, double raw, DamageType type, uint32_t attacker);
    // What a shot from a to b first meets: the world, or a shut door.
    std::optional<World::RayHit> shot_hit(Vec3 a, Vec3 b) const;
    void drop_silent(double now);
    void send(const net::Endpoint& to, const auto& message);
    Client* find(const net::Endpoint& endpoint);
    Client* find(uint32_t entity);
    const Client* find(uint32_t entity) const;
    void say(const std::string& text) const;
    // Stage 12 (server_specialists.cpp).
    void add_companion_looks();
    void handle_specialist_request(Client& client, const net::SpecialistRequest& request);
    void update_specialists(double dt);
    void send_specialist_sync(Client& client);
    void load_specialist_data(Client& client);
    void save_specialist_data(const Client& client) const;
    bool use_specialist_item(Client& client, const ItemDef& def, uint64_t item_id);
    bool spawn_at_beacon(Client& client, size_t loadout);
    void refresh_abilities(Client& client);
    void refresh_effect_gear(Client& client);
    bool use_ability(Client& client, const AbilityDef& ability, uint32_t target, Vec3 at);
    void finish_channel(Client& client);
    void give_effect(Client& client, TimedEffect effect);
    // A hit's effect (the weapon's, its ammo's), if it lands: a debuff on a
    // character, a snare and damage over time on an NPC.
    void give_hit_effect(const WeaponDef& weapon, uint32_t attacker, Client& victim);
    void give_hit_effect(const WeaponDef& weapon, uint32_t attacker, Npc& victim);
    void update_effects(Client& client, double dt);
    void update_npc_effects(double dt);
    // Heals by `amount` times the character's healing_received, up to full; returns how much.
    double heal(Client& client, double amount);
    int specialist_level(const Client& client, const std::string& profession) const;
    void earn_specialist_xp(Client& client, const std::string& profession, int64_t xp);
    bool is_enemy(const Client& a, uint32_t entity) const;
    void summon_companion(Client& client, uint64_t record_id);
    void dismiss_companion(Client& client, bool destroyed);
    void update_companions(double dt);
    void companion_died(uint32_t npc_entity);
    // A hit on an NPC, if it's a companion: nothing from its owner's side, more from electro on a droid.
    double companion_hit(uint32_t npc_entity, uint32_t attacker, double raw, DamageType type) const;
    void mark_entity(uint32_t entity, int team, uint32_t owner, double seconds, uint8_t kind);
    void specialist_kill(uint32_t victim, uint32_t killer, bool npc);
    bool bounty_pvp_allowed(uint32_t shooter, uint32_t victim) const;
    double vehicle_damage_factor(uint32_t driver) const;
    void break_cloak(Client& client);
    void offer_contracts(Client& client, size_t terminal);
    void update_contracts(Client& client);
    void bazaar_action(Client& client, const net::SpecialistRequest& request);
    void load_bazaar();
    void save_bazaar();
    void datapad_action(Client& client, const net::SpecialistRequest& request);
    void apply_slices(const Client& client, const CharacterLoadout& cl, Loadout& loadout) const;
    void fill_specialist_loadout(const Client& client, const CharacterLoadout& cl, Loadout& loadout) const;
    int field_team(const Client& client) const;

    net::Link& link_;
    const World& world_;
    ServerConfig config_;
    CharacterStore* store_;
    std::optional<Conquest> conquest_;
    double match_live_time_ = 0;    // Stage 11: seconds the current match has been live
    uint16_t match_captures_ = 0;   // Stage 11: posts taken/neutralized and flags captured this match
    std::vector<Client> clients_;
    std::vector<Npc> npcs_;
    std::unordered_map<uint32_t, size_t> npc_index_;  // Stage 13: entity -> index into npcs_
    PointGrid player_grid_{64};  // Stage 13: players in the world, this tick
    PointGrid npc_grid_{64};     // Stage 13: visible NPCs, this tick (indices into npcs_)
    ServerPerf perf_;
    double perf_since_ = -1;
    std::vector<std::string> looks_;
    std::vector<Bolt> bolts_;
    // Placed mines and traps at rest (bolts that settled): Battlefront's
    // landmine has 200 health, as ours do; an enemy can disarm one with the
    // use button, standing within 3 m (outside its trigger).
    std::vector<Bolt> mines_;
    static constexpr double mine_health = 200;
    static constexpr double disarm_reach = 3.0;  // m: just outside a mine's trigger, if you spot it
    bool disarm_mine(const Client& client);
    std::vector<Pickup> pickups_;
    std::vector<net::CombatEvent> events_;  // this tick's
    std::vector<net::Explosion> explosions_;  // this tick's
    std::vector<net::Impact> impacts_;        // this tick's
    std::vector<net::Recruiter> recruiters_;
    std::vector<net::Quartermaster> quartermasters_;
    std::vector<net::Vendor> vendors_;
    const QuestBook* quest_book_ = nullptr;
    std::vector<net::QuestNpc> quest_npcs_;
    std::vector<net::MissionTerminal> mission_terminals_;
    std::vector<net::Workbench> workbenches_;
    std::set<uint32_t> harvested_corpses_;
    std::vector<CorpseLoot> corpse_loots_;
    std::vector<Vehicle> vehicles_;
    std::vector<double> spawn_wait_;    // per vehicle spawn: seconds until it gives another
    std::vector<uint32_t> spawned_;     // per vehicle spawn: its vehicle (0: none)
    Phase vehicles_phase_ = Phase::warmup;
    std::vector<RangeTarget> targets_;
    std::vector<std::string> factions_;
    uint32_t tick_ = 0;
    uint32_t next_entity_ = 1;
    uint32_t next_bolt_ = 1;
    uint32_t next_pickup_ = 1;
    uint64_t rng_ = 0x574D5049434B5550ull;
    uint32_t next_spawn_slot_ = 0;  // spreads spawns around a post
    double next_tick_time_ = 0;
    double now_ = 0;  // the clock as of the last update()
    double next_save_time_ = 0;
    bool started_ = false;
    std::optional<Vec3> cloning_facility_;
    std::vector<std::pair<std::string, Vec3>> cloning_facilities_;
    std::map<std::string, Vec3> arrivals_;  // Stage 13: players expected from another planet
    // Stage 12: what specialists have put in the field, the companions
    // fielded, marks shown to a side, and the bazaar.
    struct Mark {
        uint32_t entity = 0;
        int team = -1;        // the side that sees it (-1: every player, outside a match; -2: only its owner)
        uint32_t owner = 0;
        double remaining = 0;
        uint8_t kind = 0;     // net::FieldMark::Kind
    };
    // A snare, toxin or tranquilizer on an NPC (a creature or an outlaw).
    struct NpcEffect {
        uint32_t entity = 0;
        std::string id;
        double remaining = 0;
        double speed = 1;       // its walk and run speeds times this
        double dps = 0;         // damage a second
        DamageType type = DamageType::bio;
        uint32_t source = 0;
        bool tranquilized = false;
    };
    std::vector<Deployable> deployables_;
    std::vector<Hazard> hazards_;
    std::vector<PendingStrike> strikes_;
    std::vector<ActiveCompanion> companions_;
    std::vector<Mark> marks_;
    std::vector<NpcEffect> npc_effects_;
    std::map<uint32_t, std::pair<double, double>> npc_base_speed_;  // walk, run before effects
    std::vector<BazaarListing> bazaar_;
    std::vector<BazaarSale> bazaar_sales_;
    bool bazaar_loaded_ = false;
    uint64_t next_listing_ = 1;
    uint64_t next_contract_ = 1;
    uint32_t next_field_id_ = 1;
    double specialist_clock_ = 0;  // seconds of ticks (contracts expire by it)
    double cohesion_pulse_ = 0;
};

} // namespace swm::core
