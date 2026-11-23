#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "swm/core/combat.h"
#include "swm/core/conquest.h"
#include "swm/core/items.h"
#include "swm/core/movement.h"
#include "swm/core/net/link.h"
#include "swm/core/net/protocol.h"
#include "swm/core/persistence.h"
#include "swm/core/quest.h"
#include "swm/core/species.h"
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
    bool friendly_fire = false;  // in a match
    bool single_match = false;   // one match, no next one (a hub's match: see match_over())
    bool pvp_enabled = true;     // false in open-world hubs (no player-vs-player harm outside matches)
};

inline constexpr int hostile_npc_team = 100;

struct PlayerResult {
    std::string name;
    int team = -1;
    uint16_t kills = 0, deaths = 0, captures = 0;
};

struct MatchResult {
    std::string battlefield;  // its name
    int winner = -1;          // -1: a draw
    std::vector<PlayerResult> players;
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
    size_t pickup_count() const { return pickups_.size(); }
    // Drops a health, ammo, dual or energy canister on the ground at `position`.
    void spawn_pickup(PickupKind kind, Vec3 position);
    // The match, if there is one.
    const Conquest* match() const { return conquest_ ? &*conquest_ : nullptr; }
    // A single match is over once its results have shown.
    bool match_over() const;
    // Saves every connected character now. Storage errors are logged.
    void save_all();

    // The war terminals players can queue at and the sides they queue for,
    // told to clients as they join.
    void set_recruiters(std::vector<net::Recruiter> recruiters, std::array<std::string, 2> factions);
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
    // Persists battle rewards for a player by name and updates live balance if connected.
    void award_battle_rewards(const std::string& name, const BattleReward& reward);
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
    void set_looks(std::vector<std::string> looks) { looks_ = std::move(looks); }
    // Adds one standing on the ground under `position` (from a meter
    // over it), facing `yaw`; returns its entity. `colors`: 0 for its look's
    // own colors, else a seed clients vary them by (net::EntityState).
    // `radius`: its body's, against walls (a creature's), 0 for a person's
    // (the loadout's).
    uint32_t add_npc(const std::string& name, Vec3 position, double yaw, uint8_t look, uint8_t pose = 0,
                     uint16_t colors = 0, double radius = 0);
    struct NpcCombatSpec {
        uint8_t actor_kind = 1;  // 1=citizen, 2=wild creature, 3=bandit
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
    const ItemCatalog* catalog() const { return config_.catalog; }
    void steer_npc(uint32_t entity, const MoveInput& input);
    std::vector<uint32_t> npcs() const;

    std::function<void(const std::string&)> log;
    std::function<void(uint32_t npc_entity, uint32_t killer_entity)> on_npc_killed;
    // Decides who may join, given the team they ask for (-1: any;
    // natives_team: a native, a bot): their team (-1: the smaller one), or
    // nullopt to refuse. Unset: anyone, on the smaller team (natives on
    // theirs, while there's room).
    std::function<std::optional<int>(const std::string& name, uint64_t ticket, int team)> admit;
    std::function<void(uint32_t entity, const std::string& name)> on_join;
    // A player standing at a war terminal asked to queue for a side (-1: leave).
    std::function<void(uint32_t entity, int team, QueueMode mode)> on_queue;
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
        uint16_t kills = 0, deaths = 0, captures = 0;
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
        uint16_t offer_serial = 0;
        uint32_t notice_tick = UINT32_MAX;  // when the notice was last set
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
    };
    struct CorpseLoot {
        uint32_t npc_entity = 0;
        uint32_t owner_entity = 0;
        std::string name;
        Vec3 position;
        std::vector<net::LootItemEntry> items;
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
    void fire(const Client& shooter, const Shot& shot, double view_tick);
    void fire_npc(const Npc& shooter, const Shot& shot);
    void advance_bolts();
    void advance_pickups();
    void drop_pickup(Vec3 at);
    double random_unit();
    void damage(Client& victim, double raw, DamageType type, uint32_t attacker);
    void damage_npc(Npc& npc, double raw, DamageType type, uint32_t attacker);
    Npc* find_npc(uint32_t entity);
    const Npc* find_npc(uint32_t entity) const;
    void damage_powerup(size_t index, double amount, uint32_t attacker, std::vector<Blast>& blasts);
    void explode(const Blast& blast, std::vector<Blast>& blasts);
    void kill(Client& victim, uint32_t attacker);
    void respawn(Client& client);
    void spawn_at_post(Client& client, size_t loadout, size_t post);
    void return_to_waiting(Client& client);
    void rebuild_client_loadouts(Client& client);
    void scrub_item_from_loadouts(Client& client, uint64_t item_id);
    void send_inventory_sync(Client& client);
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

    net::Link& link_;
    const World& world_;
    ServerConfig config_;
    CharacterStore* store_;
    std::optional<Conquest> conquest_;
    std::vector<Client> clients_;
    std::vector<Npc> npcs_;
    std::vector<std::string> looks_;
    std::vector<Bolt> bolts_;
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
    std::vector<CorpseLoot> corpse_loots_;
    std::vector<RangeTarget> targets_;
    std::array<std::string, 2> factions_;
    uint32_t tick_ = 0;
    uint32_t next_entity_ = 1;
    uint32_t next_bolt_ = 1;
    uint32_t next_pickup_ = 1;
    uint64_t rng_ = 0x574D5049434B5550ull;
    uint32_t next_spawn_slot_ = 0;  // spreads spawns around a post
    double next_tick_time_ = 0;
    double next_save_time_ = 0;
    bool started_ = false;
    std::optional<Vec3> cloning_facility_;
};

} // namespace swm::core
