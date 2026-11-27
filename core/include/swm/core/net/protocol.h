#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "swm/core/conquest.h"
#include "swm/core/crafting.h"
#include "swm/core/movement.h"
#include "swm/core/persistence.h"
#include "swm/core/species.h"
#include "swm/core/specialist.h"

namespace swm::core::net {

// Every packet starts with a magic number, the protocol version and a message
// type. Peers drop packets with another version. Bump it on any wire change.
inline constexpr uint16_t protocol_magic = 0x4D53;  // "SM"
inline constexpr uint16_t protocol_version = 49;
// BoltState/Impact::weapon: a placed trap's shot, from that weapon slot
// (drawn from the trap, not its owner's gun).
inline constexpr uint8_t trap_shot_flag = 0x40;
inline constexpr uint8_t droid_explosion_weapon = 0xFF;  // Explosion::weapon sentinel for destroyed health/ammo droids
// ...for a destroyed object (Battlefield::destructibles); clients draw it
// from the object's own explosion as they see it go.
inline constexpr uint8_t object_explosion_weapon = 0xFE;
// ...for a destroyed vehicle (clients draw its class's explosion).
inline constexpr uint8_t vehicle_explosion_weapon = 0xFD;

// Client -> server, repeated until a Welcome or Refused arrives. The name
// picks the saved character (there are no accounts yet). A match server
// launched by a hub only admits players with the ticket the hub gave them.
struct Hello {
    std::string name;
    uint64_t ticket = 0;
    int8_t team = -1;  // the team asked for: -1 any; 2 the battlefield's natives (bots)
};

// A hub's war terminal: standing within `radius` of it, a player can queue
// for a battle.
struct Recruiter {
    // Stage 11: which battles it queues for: Battlefront's ground battles,
    // its space battles (a Starport's Space Terminal, which also hosts
    // interplanetary travel once more planets open), or both.
    enum Kind : uint8_t { ground = 0, space = 1, both = 2 };
    std::string name;
    Vec3 position;  // game space
    double radius = 4;
    Kind kind = ground;

    friend bool operator==(const Recruiter&, const Recruiter&) = default;
};

// A hub's faction quartermaster: standing within `radius` of it, a player can
// browse and buy standard-issue gear for faction points or credits.
struct Quartermaster {
    std::string name;
    std::string faction;  // "empire", "rebellion", or "neutral"
    Vec3 position;        // game space
    double radius = 4;

    friend bool operator==(const Quartermaster&, const Quartermaster&) = default;
};

// A hub's open-world vendor or service point. Shops: "junk_dealer", "gear_vendor",
// "garage" (vehicles). Services: "trainer" (`detail`: the professions it
// trains), "bank", "clinic" and "cloning".
struct Vendor {
    std::string name;
    std::string kind;
    Vec3 position;     // game space
    double radius = 4;
    std::string detail;

    friend bool operator==(const Vendor&, const Vendor&) = default;
};

// One of a hub's people who gives quests or takes part in them (someone
// to talk to or deliver to): the player talks to them with the interact key.
struct QuestNpc {
    uint32_t entity = 0;
    std::string name;
    Vec3 position;       // game space, where they stand
    bool giver = false;  // offers quests (clients mark them)

    friend bool operator==(const QuestNpc&, const QuestNpc&) = default;
};

// A hub's mission terminal: standing within `radius`, a player can browse
// its generated missions.
struct MissionTerminal {
    std::string name;
    Vec3 position;                 // game space
    double radius = 3;
    std::string kind = "general";  // "general", "artisan", "bounty", "entertainer", "scout", "imperial", "rebel"

    friend bool operator==(const MissionTerminal&, const MissionTerminal&) = default;
};

struct Workbench {
    std::string name;
    Vec3 position;                 // game space
    double radius = 4;
    std::string kind = "general";  // "general", "weapon", "clothing", "food", "structure"

    friend bool operator==(const Workbench&, const Workbench&) = default;
};

// A quest as the quest log, the tracker and an offer show it.
struct QuestInfo {
    std::string id;
    std::string title;
    std::string giver;
    std::string summary;
    std::vector<std::string> steps;  // an offer's every step; an active quest's up to its current one
    uint8_t step = 0;                // the current step (active)
    uint16_t count = 0, needed = 0;  // the current step's progress
    bool has_waypoint = false;
    Vec3 waypoint;                   // game space: where the current step is
    int64_t credits = 0, experience = 0, faction_points = 0;
    std::vector<std::string> items;  // reward item def ids
    bool mission = false;
    uint8_t level = 1;               // the combat level it suits
    std::string step_npc;            // the current step's person (talk_to, deliver)
    std::vector<std::string> targets;  // the current step's kill (or drop) targets, by the names they go by
    float area_radius = 0;           // > 0: they count only this near `waypoint`

    friend bool operator==(const QuestInfo&, const QuestInfo&) = default;
};

struct LootItemEntry {
    uint8_t slot = 0;
    std::string def_id;
    ItemGrade grade = ItemGrade::standard;
    uint8_t mark = 0;

    friend bool operator==(const LootItemEntry&, const LootItemEntry&) = default;
};

struct CorpseLootState {
    uint32_t entity_id = 0;
    std::string name;
    Vec3 position;
    std::vector<LootItemEntry> items;

    friend bool operator==(const CorpseLootState&, const CorpseLootState&) = default;
};

// A shooting-range target in a hub.
struct TargetState {
    uint32_t id = 0;
    std::string name;
    Vec3 position;
    float health = 0;
    float max_health = 400;
    bool alive = true;

    friend bool operator==(const TargetState&, const TargetState&) = default;
};

// Server -> client: the handshake answer.
struct Welcome {
    uint32_t entity_id = 0;  // the client's own character
    double tick_rate = 30;   // simulation ticks per second
    uint32_t server_tick = 0;
    PersistentId character_id = 0;  // the saved character; 0 if the server doesn't save
    // A hub's war terminals, and the sides its battles are fought between.
    std::vector<Recruiter> recruiters;
    std::vector<std::string> factions;
    // How its people look (a Galaxies object template each): an entity's
    // look is an index into these, plus one.
    std::vector<std::string> looks;
    std::vector<Quartermaster> quartermasters;
    std::vector<Vendor> vendors;
    std::vector<QuestNpc> quest_npcs;
    std::vector<MissionTerminal> mission_terminals;
    std::vector<Workbench> workbenches;
};

// Client -> hub, repeated until the hub's PlayerStatus shows it: queue for a
// battle on a side (0..3), near a war terminal; -1 leaves the queue.
struct QueueRequest {
    int8_t team = -1;
    // Stage 11: the game mode wanted (core::GameMode; a space terminal's
    // battles are Space Assault whatever this says), or any_mode.
    static constexpr uint8_t any_mode = 0xFF;
    uint8_t mode = 0;
};

// Server -> client, repeated until the client leaves: go to another server
// (a hub sends players to a match, a match sends them back).
struct Transfer {
    uint32_t address = 0;  // IPv4; 0: the same address as this server
    uint16_t port = 0;
    uint64_t ticket = 0;      // to show the next server
    std::string battlefield;  // the match's battlefield (a file name without .ini); empty for a hub
    // A hub on another planet (Stage 13): its terrain bake's name (e.g.
    // "naboo", next to the client's own), whose world the client loads;
    // empty: the session's home planet.
    std::string planet;

    friend bool operator==(const Transfer&, const Transfer&) = default;
};

// What a server tells one client about itself (in every snapshot).
struct PlayerStatus {
    enum Queue : uint8_t { none, queued };
    Queue queue = none;
    int8_t team = -1;          // queued for
    uint16_t in_queue = 0;     // players queued for the next battle
    float starts_in = 0;       // seconds until it starts
    std::string battlefield;   // where it's fought (its name)
    uint16_t notice_id = 0;    // changes with each new notice
    std::string notice;        // e.g. how the last battle went
    int64_t credits = 0;
    int64_t faction_points = 0;
    int64_t experience = 0;
    uint8_t active_loadout = 0;
    int8_t switching_loadout = -1;
    float switch_progress = 0.0f;
    uint8_t combat_level = 1;        // the higher of the levels of the carried weapon's and the worn armor's professions
    float extract_progress = -1.0f;  // an extraction under way: how far along it is (0..1); else -1
    uint32_t inventory_rev = 0;

    friend bool operator==(const PlayerStatus&, const PlayerStatus&) = default;
};

// Server -> client: the handshake failed.
struct Refused {
    std::string reason;
};

// Client -> server: the newest inputs, oldest first. Each packet repeats the
// inputs the server hasn't acknowledged yet, so a lost packet costs nothing.
struct InputBatch {
    std::vector<MoveInput> inputs;
};

// Client -> server, repeated while waiting to spawn: the loadout and command
// post wanted (indexes into the battlefield's team loadouts and posts).
struct SpawnRequest {
    uint8_t loadout = 0;
    uint8_t post = 0;
};

struct EntityState {
    uint32_t id = 0;
    MoveState state;
    float health = 0;
    float max_health = 0;
    float shield = 0;
    float max_shield = 0;
    Life life = Life::alive;
    uint8_t spawn_count = 0;  // changes on every respawn: clients jump there instead of easing
    int8_t team = -1;         // -1: no teams (outside a match)
    uint8_t loadout = 0;      // index into the team's loadouts
    uint16_t kills = 0;
    uint16_t deaths = 0;
    uint16_t captures = 0;    // command posts taken or neutralized while in them
    uint8_t look = 0;         // 0: a player; else the server's looks[look - 1] (a hub's people)
    uint8_t pose = 0;         // 0: on their feet; 1: sitting; 2: attacking
    uint8_t actor_kind = 0;   // 0: player/match bot; 1: hub citizen; 2: wild creature; 3: bandit
    bool passive = false;     // a creature that leaves you be unless you hit it
    bool quiet = false;       // its shots don't show it on enemies' radar (a perk's: Low Profile)
    uint16_t colors = 0;      // a look's colors: 0 its own; else a seed the client varies them by
    uint8_t species = 0;      // index into core::all_species() (0 = human .. 9 = ithorian)
    uint8_t gender = 0;       // 0 = male, 1 = female
    std::string name;         // player name (empty for NPCs)
    std::string appearance;   // player customization JSON (empty for NPCs/bots)
    std::string weapon_model; // active weapon glTF (empty: use team loadout)
    std::string anim_set;     // active weapon anim set (empty: use team loadout)
    std::string worn;         // comma-separated worn armor/clothing glTFs
    // The strings above rarely change, so a snapshot carries them only when a
    // client may not have them yet (has_static); otherwise just their key,
    // and the client fills them in from what it was sent before (Stage 13).
    uint32_t static_key = 0;  // static_key(*this); 0: not computed
    bool has_static = true;
    // Its whole state (a client's own, which it predicts and reconciles), or
    // only what's drawn (everyone else's: where it is and how it moves, in
    // floats, its held weapon and whether it's reloading; Stage 13).
    bool full_state = true;

    bool alive() const { return life == Life::alive; }
};

// A hash of an entity's rarely changing strings (name, appearance, weapon
// model, anim set, worn), never 0.
uint32_t static_key(const EntityState& entity);

// A projectile in flight: a blaster bolt, a missile or a grenade.
struct BoltState {
    uint32_t id = 0;
    uint32_t owner = 0;  // entity that fired it
    uint8_t weapon = 0;  // the owner's weapon slot, for drawing it
    Projectile kind = Projectile::bolt;
    Vec3 position;
    Vec3 velocity;
};

// An explosion this tick, for drawing.
struct Explosion {
    uint32_t owner = 0;
    uint8_t weapon = 0;
    Vec3 position;
    Vec3 normal{0, 1, 0};  // surface normal, for orienting decals and bursts
    float radius = 0;      // the outer damage radius
};

// A projectile impact this tick (sparks and scorch marks), for drawing.
struct Impact {
    uint32_t owner = 0;
    uint8_t weapon = 0;
    Vec3 position;
    Vec3 normal{0, 1, 0};
    bool hit_character = false;

    friend bool operator==(const Impact&, const Impact&) = default;
};

// Something that happened this tick. Hits and kills are between entities
// (attacker 0: the world, e.g. out of bounds); for captured and neutralized,
// `attacker` is the team and `victim` the command post.
struct CombatEvent {
    // Flag events (Stage 11): attacker = the soldier (0: none), victim = the
    // flag's index, damage = the soldier's team.
    // shielded: a shot a capital ship's shield took (victim = the system it
    // covers, a destructible's index; damage = what it would have done).
    enum Kind : uint8_t {
        hit = 1, kill = 2, captured = 3, neutralized = 4,
        flag_taken = 5, flag_dropped = 6, flag_returned = 7, flag_captured = 8,
        shielded = 9,
        // A triggered perk the server fired (docs/PERKS.md): attacker = its
        // character, victim = its index in the loadout's triggered perks.
        perk = 10,
    };
    Kind kind = hit;
    uint32_t attacker = 0;
    uint32_t victim = 0;
    float damage = 0;

    friend bool operator==(const CombatEvent&, const CombatEvent&) = default;
};

// A dropped health, ammo, dual or energy canister lying on the ground.
struct PickupState {
    uint32_t id = 0;
    PickupKind kind = PickupKind::health_small;
    Vec3 position;
    float time_left = 0;  // seconds until it disappears

    friend bool operator==(const PickupState&, const PickupState&) = default;
};

// A vehicle in a match: where it is, its health, who drives it. A driven
// one moves with its driver (whose state says it drives: MoveState::vehicle).
// A destroyed one is listed (not alive) for a moment, for drawing its end.
struct VehicleState {
    uint32_t id = 0;
    uint8_t vehicle = 0;  // its class: an index into the battlefield's vehicles
    Vec3 position;
    Vec3 velocity;
    float yaw = 0;
    float pitch = 0;
    float bank = 0;       // flyer visual roll/bank angle (radians)
    float health = 0;
    float max_health = 0;
    uint32_t driver = 0;  // entity; 0: empty
    int8_t team = -1;     // whose it is: its driver's, else the side it spawned for
    bool alive = true;
    uint8_t flags = 0;    // bit 0: landed/grounded; bit 1: active mobile spawn
    std::vector<uint32_t> gunners;  // per gunner's seat: who sits there (0: no one)
    uint32_t owner = 0;             // in the world: whose it is (only they ride it); 0 in a match
    uint32_t towing = 0;            // Stage 11: the walker its tow cable holds (0: none)
    float tow_wraps = 0;            // ...and how many times round its legs so far

    static constexpr uint8_t flag_grounded = 1 << 0;
    static constexpr uint8_t flag_mobile_spawn = 1 << 1;

    friend bool operator==(const VehicleState&, const VehicleState&) = default;
};

// Stage 12 (specialist.h): what specialists have put in the field, as one
// client sees it (in every snapshot).
struct FieldMark {
    enum Kind : uint8_t { marked = 0, tracked = 1, bounty = 2 };
    uint32_t entity = 0;
    float remaining = 0;
    uint8_t kind = marked;

    friend bool operator==(const FieldMark&, const FieldMark&) = default;
};

struct FieldDeployable {
    uint32_t id = 0;
    uint8_t kind = 0;  // DeployKind
    uint32_t owner = 0;
    bool friendly = false;  // the viewer's side's
    Vec3 position;
    float radius = 0;
    float remaining = 0;

    friend bool operator==(const FieldDeployable&, const FieldDeployable&) = default;
};

struct FieldHazard {
    uint32_t id = 0;
    std::string kind;  // bio, smoke, strike (a call-in about to land)
    Vec3 position;
    float radius = 0;
    float remaining = 0;

    friend bool operator==(const FieldHazard&, const FieldHazard&) = default;
};

struct FieldState {
    std::vector<FieldMark> marks;
    std::vector<uint32_t> cloaked;     // the viewer's side's cloaked (drawn faint); enemies' aren't sent at all
    std::vector<uint32_t> performers;
    std::vector<std::pair<uint32_t, uint32_t>> companions;  // companion entity, owner entity
    std::vector<FieldDeployable> deployables;
    std::vector<FieldHazard> hazards;
    std::vector<std::pair<uint32_t, uint8_t>> flair;  // entity, effect flags worth drawing (holo flair, inspired)

    friend bool operator==(const FieldState&, const FieldState&) = default;
};

// Server -> client, every tick: the world as the server sees it.
struct Snapshot {
    uint32_t server_tick = 0;
    uint32_t last_input = 0;  // newest input of this client's that the server has simulated
    std::vector<EntityState> entities;
    std::vector<BoltState> bolts;
    // This tick's events. A lost snapshot loses them; the health in later
    // snapshots stays right.
    std::vector<CombatEvent> events;
    std::vector<Explosion> explosions;  // this tick's
    std::vector<Impact> impacts;        // this tick's bolt hits on surfaces and bodies
    std::vector<PickupState> pickups;   // dropped canisters on the ground
    std::vector<TargetState> targets;   // shooting-range targets in a hub
    std::vector<VehicleState> vehicles; // in a match
    std::vector<CorpseLootState> corpses;  // nearby lootable dead creatures/bandits
    std::optional<MatchState> match;  // in a conquest match
    PlayerStatus you;                 // differs per client
    FieldState field;                 // Stage 12: marks, cloaks, deployables... as this client sees them
};

// Either direction: leaving.
struct Goodbye {};

// Server -> client: authoritative inventory, saved loadouts, resolved combat loadouts, and faction standing.
struct InventorySync {
    uint32_t revision = 0;
    std::vector<ItemInstance> items;
    std::vector<CharacterLoadout> loadouts;
    std::vector<Loadout> resolved_loadouts;
    int64_t bank_credits = 0;
    std::vector<ItemInstance> bank_items;  // the bank's: their ids are its own
    std::map<std::string, int64_t> reputation;

    friend bool operator==(const InventorySync&, const InventorySync&) = default;
};

// Client -> server: use a service point (a trainer, the bank, a clinic, a
// cloning terminal) you stand at.
struct ServiceRequest {
    enum Action : uint8_t {
        train = 0,                 // `id`: the profession, `amount`: the milestone's level
        bank_deposit_credits = 1,  // `amount`
        bank_withdraw_credits = 2,
        bank_deposit_item = 3,     // `item_id`: yours
        bank_withdraw_item = 4,    // `item_id`: the bank's
        heal = 5,
        bind = 6,
        travel = 7,                // `id`: the travel point (a "travel" service's name) to fly to (Stage 13)
        learn_perk = 8,            // `id`: the perk (docs/PERKS.md)
        forget = 9,                // `id`: a perk, or a profession (its last milestone learned)
        leave_match = 10,          // in a match a hub sent you to: back to that hub now (the pause menu's)
    };
    uint8_t action = 0;
    std::string id;
    int64_t amount = 0;
    uint64_t item_id = 0;
};

// Client -> server: acknowledges receipt of InventorySync `revision`.
struct InventoryAck {
    uint32_t revision = 0;
};

// Client -> server: save one of the character's 3 loadouts.
struct SaveLoadoutRequest {
    CharacterLoadout loadout;
};

// Client -> server: begin channeling a switch to saved loadout `slot` (0..2).
struct SwitchLoadoutRequest {
    uint8_t slot = 0;
};

// Client -> server: purchase an item from a nearby quartermaster.
struct ShopBuyRequest {
    std::string def_id;
    bool use_faction_points = true;
};

// Client -> server: destroy an owned item instance.
struct DestroyItemRequest {
    uint64_t item_id = 0;
};

// Client -> server: take one slot (or all when slot < 0) from a nearby corpse.
struct LootTakeRequest {
    uint32_t corpse_entity = 0;
    int8_t slot = -1;
};

// Client -> server: sell an owned item or all junk at a nearby vendor.
struct VendorSellRequest {
    uint64_t item_id = 0;
    bool sell_all_junk = false;
};

// Client -> server: consume an owned supply item.
struct UseItemRequest {
    uint64_t item_id = 0;
};

// Server -> client, repeated until acknowledged: the quest journal, and
// what the giver or terminal the player last talked to offers.
struct QuestSync {
    uint32_t revision = 0;
    std::vector<QuestInfo> active;
    std::vector<std::string> completed;  // titles, oldest first
    std::vector<QuestInfo> offers;
    std::string offer_source;   // who offers them: a person's name, or a terminal's
    uint16_t offer_serial = 0;  // changes each time the player talks to a giver or uses a terminal
    std::string offer_note;     // offering nothing: why (QuestBook::why_none)
    std::vector<std::string> ready_givers;  // the quest givers with something for this player now
    std::vector<std::string> waiting_givers;  // those with work that isn't open to them yet (or is under way)

    friend bool operator==(const QuestSync&, const QuestSync&) = default;
};

// Client -> server: acknowledges QuestSync `revision`.
struct QuestAck {
    uint32_t revision = 0;
};

// Client -> server: talk to a quest NPC (by entity), use a mission
// terminal (by index), or accept or abandon a quest (by id).
struct QuestRequest {
    enum Action : uint8_t { talk = 0, terminal = 1, accept = 2, abandon = 3 };
    Action action = talk;
    uint32_t target = 0;
    std::string quest_id;
};

// Server -> client, repeated until acknowledged: the character's experience
// in each profession it has any in (levels follow from the roster's curve).
struct ProfessionSync {
    uint32_t revision = 0;
    std::vector<ProfessionProgress> professions;

    friend bool operator==(const ProfessionSync&, const ProfessionSync&) = default;
};

// Client -> server: acknowledges ProfessionSync `revision`.
struct ProfessionAck {
    uint32_t revision = 0;
};

// Server -> client, repeated until acknowledged: Material Vault & Recipe Presets.
struct CraftingSync {
    uint32_t revision = 0;
    std::vector<MaterialStack> materials;
    std::vector<RecipePreset> presets;
    // Blueprints learned (docs/CRAFTING.md): the ids of the `Blueprint = yes`
    // schematics this character may make.
    std::vector<std::string> blueprints;

    friend bool operator==(const CraftingSync&, const CraftingSync&) = default;
};

struct CraftingAck {
    uint32_t revision = 0;
};

// Client -> server: active field extraction, corpse harvest/salvage, or item salvage.
struct GatherRequest {
    enum Action : uint8_t { extract_terrain = 0, harvest_corpse = 1, salvage_item = 2 };
    Action action = extract_terrain;
    uint8_t category = 0;
    uint64_t target_id = 0;
};

// Client -> server: craft item(s), manage recipe presets, or fulfill a Hub Work Order.
struct CraftRequest {
    enum Action : uint8_t { craft = 0, save_preset = 1, delete_preset = 2, fulfill_order = 3, dye = 4 };
    Action action = craft;
    std::string schematic_id;
    std::vector<std::string> resource_ids;
    std::vector<uint64_t> enhancement_item_ids;
    std::vector<std::string> enhancement_defs;
    uint8_t exp_quality_pct = 50;
    uint8_t batch_count = 1;
    std::string preset_name;
    std::string order_id;
    // A craft's mark (docs/CRAFTING.md, Mk tiers): top_mark for the highest
    // its crafter may make.
    static constexpr uint8_t top_mark = 255;
    uint8_t mark = top_mark;
    // dye: an owned piece and its colors ("index_color_1=5&index_color_2=12";
    // "" takes the dye out).
    uint64_t item_id = 0;
    std::string dye_values;
};

// Stage 12 (specialist.h): Client -> server, what a player does with their
// specialist and companion professions.
struct SpecialistRequest {
    enum Action : uint8_t {
        use_ability = 0,        // `id`: the ability; `target`: an entity; `position`: where it's aimed
        companion_summon = 1,   // `amount`: a datapad id (0: the active loadout's Companion Slot)
        companion_dismiss = 2,
        companion_command = 3,  // `amount`: CompanionMode; `target`: whom to attack; `position`: where to guard
        companion_rename = 4,   // `amount`: a datapad id; `text`: its name
        companion_release = 5,  // `amount`: a datapad id
        companion_mount = 6,    // `amount`: a datapad id: trained as a mount, its mount item granted
        engineer_egg = 7,       // `amount`: a DNA sample's id; `item_id`: a second's (0: none)
        hatch_egg = 8,          // `item_id`: an egg
        grow_tissue = 9,        // `amount`: a DNA sample's id; `id`: "tissue" or "additive"
        assemble_droid = 10,    // `id`: the chassis; `text`: its modules, comma-separated
        register_droid = 11,    // `item_id`: a droid: into the datapad
        contracts = 12,         // `target`: a mission terminal's index: what it offers you
        contract_accept = 13,   // `amount`: an offer's id
        contract_abandon = 14,  // `amount`: a contract's id
        bazaar_browse = 15,     // `text`: what to search for
        bazaar_list = 16,       // `item_id`, `amount`: its price
        bazaar_buy = 17,        // `amount`: a listing's id
        bazaar_cancel = 18,     // `amount`: a listing's id (yours)
        // 19 and 20 were Image Designer's design offer and answer.
        slice_item = 21,        // `item_id`; `id`: the slice
        // 22 was Image Designer's dye.
        bounty_flag = 23,       // `amount`: 1 opts in to being hunted by other players, 0 out
        supply_contract = 24,   // `item_id`: supplied to the quartermaster you stand at
        set_companion_slot = 25,  // `amount`: a datapad id (0: none) for the active loadout
        cancel_channel = 26,
        choose_inspiration = 27,  // `id`: the Inspiration to build in a performer's audience ("": theirs)
    };
    uint8_t action = 0;
    std::string id;
    uint32_t target = 0;
    Vec3 position;
    int64_t amount = 0;
    uint64_t item_id = 0;
    std::string text;
};

struct AbilityStatus {
    std::string id;
    float cooldown = 0;  // seconds left

    friend bool operator==(const AbilityStatus&, const AbilityStatus&) = default;
};

struct EffectStatus {
    std::string id;
    std::string name;
    float remaining = 0;
    float duration = 0;
    uint16_t flags = 0;  // effect_flag

    friend bool operator==(const EffectStatus&, const EffectStatus&) = default;
};

// Server -> client, repeated until acknowledged: a character's kit, effects,
// datapad, contracts, the bazaar page it last browsed and any design offered.
struct SpecialistSync {
    uint32_t revision = 0;
    std::vector<AbilityStatus> abilities;  // the active loadout's, here (match or world)
    std::vector<EffectStatus> effects;
    std::vector<CompanionRecord> companions;
    uint64_t active_companion = 0;
    uint32_t companion_entity = 0;
    float companion_cooldown = 0;
    uint8_t companion_mode = 0;
    uint64_t companion_slot = 0;  // the active loadout's Companion Slot
    std::vector<DnaSample> samples;
    std::vector<Contract> contracts;
    std::vector<Contract> offers;
    std::string offer_source;
    std::vector<BazaarListing> bazaar;     // the last browse
    std::vector<BazaarListing> my_listings;
    std::vector<std::pair<std::string, PriceStats>> prices;  // Market Analytics: by item def
    bool performing = false;
    bool bounty_flag = false;
    std::string inspiration_choice;  // what it builds in an audience (empty: the performer's)
    std::string channel;            // an ability being channeled (empty: none)
    float channel_remaining = 0;

    friend bool operator==(const SpecialistSync&, const SpecialistSync&) = default;
};

struct SpecialistAck {
    uint32_t revision = 0;
};

using Message = std::variant<Hello, Welcome, Refused, InputBatch, Snapshot, Goodbye, SpawnRequest, QueueRequest,
                             Transfer, InventorySync, InventoryAck, SaveLoadoutRequest, SwitchLoadoutRequest,
                             ShopBuyRequest, DestroyItemRequest, LootTakeRequest, VendorSellRequest, UseItemRequest,
                             QuestSync, QuestAck, QuestRequest, ProfessionSync, ProfessionAck, CraftingSync,
                             CraftingAck, GatherRequest, CraftRequest, ServiceRequest, SpecialistRequest,
                             SpecialistSync, SpecialistAck>;

std::vector<uint8_t> encode(const Message& message);
// Throws PacketError on malformed packets or another protocol version.
Message decode(std::span<const uint8_t> packet);

// Most inputs one InputBatch carries.
inline constexpr size_t max_batched_inputs = 16;

} // namespace swm::core::net
