#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "swm/core/ini.h"
#include "swm/core/items.h"
#include "swm/core/loadout.h"
#include "swm/core/profession.h"
#include "swm/core/vec3.h"

namespace swm::core {

// Stage 12 (docs/ROADMAP.md, docs/PROFESSIONS.md): Waves 7B and 7C. What the
// specialist and companion professions do, as data (game/professions/
// specialists.ini) and as the rules the server plays them by:
//
//   - abilities: a profession's active kit, unlocked at its milestones
//     (healing darts, bolas, cloaks, spawn beacons, performances...);
//   - timed effects: buffs and debuffs on characters (and the gear they make);
//   - companions: tamed or hatched creatures and assembled droids, kept in a
//     character's datapad, one fielded at a time, some rideable as mounts;
//   - contracts: bounty marks, contraband runs, tracking jobs and gigs from
//     mission terminals;
//   - the bazaar: player listings, fees and price history (Merchant);
//   - slicing: permanent sidegrades on weapons and armor (Smuggler).

// ---------------------------------------------------------------------------
// Abilities

// What using an ability does.
enum class AbilityEffect : uint8_t {
    buff,          // timed modifiers on its targets (self, or allies in `radius`)
    heal,          // `amount` health to one target
    heal_area,     // `amount` health to allies within `radius` of where it lands
    damage_area,   // `amount` damage of `damage_type` to enemies within `radius`
    hazard,        // a cloud: `amount` damage a second within `radius` for `duration`
    cleanse,       // ends a target's debuffs
    revive,        // brings a dead ally back
    cloak,         // hidden from enemies for `duration`, broken by firing
    mark,          // an enemy shown to the user's side for `duration`
    snare,         // slows an enemy (its modifiers) for `duration`
    jump,          // a jump pack: up and forward by `amount` m/s
    deploy,        // places a Deployable (`deploy`) where it lands
    strike,        // a call-in: `amount` damage within `radius` after `delay`
    supply,        // a call-in: health and ammo canisters where it lands
    vehicle_repair,  // `amount` (a share) of the user's vehicle's health back
    vehicle_guard,   // the user's vehicle takes less damage for `duration`
    companion_repair,     // `amount` (a share) of the fielded companion's health back
    companion_overcharge, // the fielded companion hits harder for `duration`
    perform,       // starts or stops a performance (Entertainer)
    track,         // marks creatures within `radius` (Ranger)
    seeker,        // reveals the user's bounty marks (Bounty Hunter)
    hvt,           // marks the enemy side's best soldier (Bounty Hunter)
    tranquilize,   // a creature: slowed and easier to tame for `duration`
    tame,          // a creature: a bonding channel, then it's yours (Creature Handler)
    sample_dna,    // a creature, living or dead: a DNA sample (Bio-Engineer)
};

// Who an ability is aimed at.
enum class AbilityTarget : uint8_t { self, ally, enemy, ground, creature };

// Something an ability leaves standing in the world.
enum class DeployKind : uint8_t {
    none,
    heal_aura,     // heals allies within its radius (bacta dispenser)
    buff_aura,     // keeps its ability's buff on allies within its radius (holo-emitter)
    sensor,        // marks enemies within its radius (motion sensor, recon probe)
    trap,          // snares and hurts the first enemy to come within its radius
    spawn_beacon,  // in a match: the user's side can spawn at it
    camp,          // heals, and makes loadout switches quick, within its radius (forward camp)
};

struct AbilityDef {
    std::string id;
    std::string name;
    std::string description;
    std::string profession;  // whose kit it is
    int level = 1;           // the profession level (a milestone: it must be trained) it needs
    AbilityEffect effect = AbilityEffect::buff;
    AbilityTarget target = AbilityTarget::self;
    DeployKind deploy = DeployKind::none;
    double cooldown = 10;  // seconds
    double range = 0;      // m the target may be from the user (0: touch, 3 m)
    double radius = 0;     // m
    double duration = 0;   // seconds
    double amount = 0;
    double delay = 0;      // seconds (a strike's)
    double channel = 0;    // seconds the user must stand still (taming, sampling)
    DamageType damage_type = DamageType::explosive;
    std::vector<StatModifier> modifiers;  // a buff's or a snare's
    bool match_only = false;
    bool world_only = false;
    int64_t xp = 20;  // its profession's experience each time it does something
    bool flair = false;  // a buff that's only for show (clients draw a holo-aura)
};

// The ability catalog and companion templates (specialists.ini).
struct CompanionTemplate;
struct DroidModule;
struct SliceDef;

enum class CompanionKind : uint8_t { creature, droid };
enum class CompanionRole : uint8_t { combat, recon, support };

std::optional<AbilityEffect> ability_effect_from_name(std::string_view name);
std::string_view ability_effect_name(AbilityEffect effect);
std::optional<AbilityTarget> ability_target_from_name(std::string_view name);
std::string_view ability_target_name(AbilityTarget target);
std::optional<DeployKind> deploy_kind_from_name(std::string_view name);
std::string_view deploy_kind_name(DeployKind kind);
std::string_view companion_kind_name(CompanionKind kind);
std::string_view companion_role_name(CompanionRole role);

// A creature that can be tamed or hatched, or a droid chassis that can be
// assembled: what it is when fielded, before its traits and modules.
struct CompanionTemplate {
    std::string id;
    std::string name;
    CompanionKind kind = CompanionKind::creature;
    CompanionRole role = CompanionRole::combat;
    std::string look;  // a Galaxies object template, as wildlife and hubs name them
    int tier = 1;
    int level = 1;      // the Creature Handler level to tame it / Droid Operations level to field it
    int build_level = 1;  // a droid: the Droid Engineer level to assemble it
    double health = 200;
    double damage = 15;  // a hit's
    DamageType damage_type = DamageType::melee;
    double attack_range = 2.5;
    double attack_cooldown = 1.5;
    double speed = 6;  // m/s, running
    double radius = 0.5;
    double height = 1.0;
    double mitigation = 0;
    int bays = 0;  // a droid's module bays
    bool mountable = false;
    std::string mount_item;  // the item (ItemKind::vehicle) it becomes as a mount
    std::vector<std::pair<std::string, int>> materials;  // a droid's: resource category, units

    // Humanoid-sized or smaller: it may go into a match (docs/PROFESSIONS.md).
    bool tactical() const { return height <= 2.0 && radius <= 0.9; }
};

// A plug-in for a droid's bays.
struct DroidModule {
    std::string id;
    std::string name;
    std::string description;
    std::string bay = "core";  // core, primary or utility
    int level = 25;            // the Droid Engineer level to fit it
    double health = 1;         // multiplies the chassis'
    double damage = 1;
    double speed = 1;
    double mitigation = 0;     // added
    std::optional<DamageType> damage_type;   // a primary function's own (stun/EMP: electro)
    std::optional<CompanionRole> role;       // e.g. a bacta sprayer makes it a support droid
    std::vector<std::pair<std::string, int>> materials;
};

// A permanent sidegrade a Smuggler slices into a weapon or a piece of armor:
// something better, something worse.
struct SliceDef {
    std::string id;
    std::string name;
    std::string description;
    std::string applies = "weapon";  // weapon or armor
    int level = 1;                   // the Smuggler level
    // A weapon's (ItemDef's) multipliers: damage, rate, heat, spread, range, reload.
    double damage = 1, rate = 1, heat = 1, spread = 1, range = 1, reload = 1;
    std::vector<StatModifier> modifiers;  // armor's
};

// An Inspiration on an Entertainer's menu (docs/PROFESSIONS.md): the long
// buff an audience builds by watching. `kind` is combat (small stats, perk
// sized) or progression (xp_gain, faction_gain); `level` is the performer's
// Entertainer level it needs; `modifiers` are at full strength.
struct InspirationDef {
    std::string id;
    std::string name;
    std::string description;
    std::string kind = "combat";
    int level = 1;
    std::vector<StatModifier> modifiers;
};

class SpecialistBook {
public:
    // Throws std::runtime_error if the file can't be read or makes no sense.
    void load(const std::filesystem::path& path);
    void parse(const Ini& ini, const std::string& where);

    const std::vector<AbilityDef>& abilities() const { return abilities_; }
    const std::vector<CompanionTemplate>& companions() const { return companions_; }
    const std::vector<DroidModule>& modules() const { return modules_; }
    const std::vector<SliceDef>& slices() const { return slices_; }
    const std::vector<InspirationDef>& inspirations() const { return inspirations_; }
    const AbilityDef* ability(std::string_view id) const;
    const CompanionTemplate* companion(std::string_view id) const;
    // The creature companion a wild creature (by its look) becomes.
    const CompanionTemplate* companion_for_look(std::string_view look) const;
    const DroidModule* module(std::string_view id) const;
    const SliceDef* slice(std::string_view id) const;
    const InspirationDef* inspiration(std::string_view id) const;

private:
    std::vector<AbilityDef> abilities_;
    std::vector<CompanionTemplate> companions_;
    std::vector<DroidModule> modules_;
    std::vector<SliceDef> slices_;
    std::vector<InspirationDef> inspirations_;
};

// A profession's level and the highest milestone learned for it.
struct ProfessionStanding {
    int level = 0;
    int trained = 0;
};
ProfessionStanding profession_standing(const ProfessionBook& professions, std::span<const ProfessionProgress> progress,
                                       std::string_view id);

// The abilities a character brings to every loadout: each profession's at
// the character's level and with its milestone learned. `in_match` leaves out
// world-only abilities, else match-only ones.
std::vector<std::string> unlocked_abilities(const SpecialistBook& book, const ProfessionBook& professions,
                                            std::span<const ProfessionProgress> progress, bool in_match);

// ---------------------------------------------------------------------------
// Timed effects

namespace effect_flag {
inline constexpr uint16_t debuff = 1 << 0;        // cleansed by a cleanse
inline constexpr uint16_t cloaked = 1 << 1;       // hidden from enemies
inline constexpr uint16_t tranquilized = 1 << 2;  // a creature: easier to tame
inline constexpr uint16_t inspired = 1 << 3;      // an entertainer's audience
inline constexpr uint16_t vehicle_guard = 1 << 4; // the vehicle driven takes less damage
inline constexpr uint16_t overcharged = 1 << 5;   // a companion's hits are stronger
inline constexpr uint16_t marked = 1 << 6;        // shown to the other side
inline constexpr uint16_t holo_flair = 1 << 7;    // a holographic aura, for show
}  // namespace effect_flag

// A buff or debuff on a character. The same id refreshes rather than stacks.
struct TimedEffect {
    std::string id;
    std::string name;
    uint32_t source = 0;  // the entity that put it there
    double remaining = 0;  // seconds
    double duration = 0;
    std::vector<StatModifier> modifiers;
    double heal_per_second = 0;
    double damage_per_second = 0;
    DamageType damage_type = DamageType::bio;
    uint16_t flags = 0;

    friend bool operator==(const TimedEffect&, const TimedEffect&) = default;
};

// Adds an effect, or refreshes the one with its id (the longer time left,
// the new modifiers). Returns whether its modifiers are new (stats change).
bool add_effect(std::vector<TimedEffect>& effects, TimedEffect effect);
// Counts time down; drops those run out. Returns whether any with modifiers ended.
bool tick_effects(std::vector<TimedEffect>& effects, double dt);
// Ends those with any of `flags`; returns whether any had modifiers.
bool remove_effects(std::vector<TimedEffect>& effects, uint16_t flags);
bool has_effect_flag(std::span<const TimedEffect> effects, uint16_t flag);
// The effects' modifiers as one more piece of gear (slot "effects"), if any.
std::optional<Gear> effects_gear(std::span<const TimedEffect> effects);

// ---------------------------------------------------------------------------
// Companions

// One of a character's companions, as their datapad keeps it.
struct CompanionRecord {
    uint64_t id = 0;
    std::string template_id;
    std::string name;
    std::string origin = "tamed";  // tamed, hatched or assembled
    double quality = 0.5;          // 0..1: how good its traits came out
    double health = 1, damage = 1, speed = 1;  // trait multipliers
    uint16_t colors = 0;           // a seed clients vary its look's colors by (0: its own)
    std::vector<std::string> modules;  // a droid's
    bool mount = false;                // trained as a mount (its mount item granted)

    friend bool operator==(const CompanionRecord&, const CompanionRecord&) = default;
};

// What a companion fights with, fielded: its template with its traits, its
// modules and its handler's or operator's level.
struct CompanionStats {
    double max_health = 200;
    double damage = 15;
    DamageType damage_type = DamageType::melee;
    double attack_range = 2.5;
    double attack_cooldown = 1.5;
    double speed = 6;
    double mitigation = 0;
    CompanionRole role = CompanionRole::combat;
};
CompanionStats companion_stats(const SpecialistBook& book, const CompanionTemplate& tpl, const CompanionRecord& record,
                               int handler_level);

// Whether a droid's modules fit its chassis and the engineer's level: at
// most one per bay kind beyond the first core, no more than its bays, each
// known. Returns why not (empty: they fit).
std::string check_droid_modules(const SpecialistBook& book, const CompanionTemplate& chassis,
                                std::span<const std::string> modules, int engineer_level);

// The highest creature tier a Creature Handler of `level` can tame.
int tameable_tier(int level);
// The chance a bonding succeeds: the handler's level against the creature's,
// helped by a tranquilizer and by its wounds (`health_share` left, 0..1).
double tame_chance(const CompanionTemplate& creature, int handler_level, bool tranquilized, double health_share);

// A DNA sample a Bio-Engineer took from a creature.
struct DnaSample {
    uint64_t id = 0;
    std::string template_id;
    double quality = 0.5;  // 0..1

    friend bool operator==(const DnaSample&, const DnaSample&) = default;
};
// A sample's quality: luck in a band that rises with the engineer's level.
double dna_quality(int bio_level, double luck);

// What an engineered egg hatches into: the first sample's creature, its
// traits from both samples (a second of another creature brings its
// toughness), and the engineer's level.
CompanionRecord engineer_egg(const DnaSample& a, const DnaSample* b, int bio_level, uint16_t colors);

// Records on a line each, as datapads, eggs and droid items keep them.
std::string format_companion(const CompanionRecord& record);
std::optional<CompanionRecord> parse_companion(std::string_view line);
std::string format_samples(std::span<const DnaSample> samples);
std::vector<DnaSample> parse_samples(std::string_view text);

// The companion a fielded one is: a character's own, with the commands it's given.
enum class CompanionMode : uint8_t { follow, attack, guard };

// In a match: SpawnRequest::post for a squad spawn beacon.
inline constexpr uint8_t beacon_spawn_post = 255;

// One active companion's per-player cooldown after it's destroyed (seconds).
inline constexpr double companion_death_cooldown = 60;
inline constexpr double companion_match_death_cooldown = 30;
// Fielded companions this far from their owner are recalled to them (m).
inline constexpr double companion_leash = 60;

// ---------------------------------------------------------------------------
// Contracts (mission terminals: bounty, general, scout, entertainer, artisan)

enum class ContractKind : uint8_t {
    npc_mark,    // kill a named outlaw (Bounty Hunter)
    pvp_mark,    // defeat a player who opted in to being hunted (Bounty Hunter)
    contraband,  // carry a package to a drop point in time (Smuggler)
    track,       // find a creature and get close (Ranger)
    gig,         // perform at a venue for a while (Entertainer)
    craft_order, // bring crafted items back to the terminal (a crafting profession: Doctor so far)
};
std::string_view contract_kind_name(ContractKind kind);
std::optional<ContractKind> contract_kind_from_name(std::string_view name);

struct Contract {
    uint64_t id = 0;
    ContractKind kind = ContractKind::npc_mark;
    std::string title;
    std::string target_name;    // the mark's, the creature's, the venue's
    uint32_t target_entity = 0; // an NPC or a player (0: a place)
    Vec3 waypoint;              // where (a mark's last known position, the drop point)
    double radius = 10;         // a place's: how near counts
    int64_t reward = 0;         // credits
    int64_t xp = 0;             // its profession's experience
    double expires = 0;         // server seconds
    double progress = 0;        // a gig's seconds performed so far
    double needed = 0;          // ... and needed
    std::string profession;     // whose experience it is
    std::string item;           // a crafting order's item (its id); progress: how many are held

    friend bool operator==(const Contract&, const Contract&) = default;
};

// A mark's or a run's pay for its tier and how far it is (a crafting
// order's: each item's, its tier its schematic's level / 25 + 1).
// The crafting professions artisan terminals take orders for (every one,
// one day).
inline constexpr std::array<std::string_view, 1> artisan_order_professions{"doctor"};
int64_t contract_reward(ContractKind kind, int tier, double distance);
// How many contracts a character may hold at once.
inline constexpr size_t max_contracts = 3;
inline constexpr double contract_lifetime = 1800;  // seconds

// ---------------------------------------------------------------------------
// The bazaar (Merchant)

struct BazaarListing {
    uint64_t id = 0;
    uint64_t seller = 0;        // the character
    std::string seller_name;
    std::string shop;           // the seller's shop name (a Merchant's)
    ItemInstance item;
    int64_t price = 0;
    int64_t listed_at = 0;      // seconds since the epoch

    friend bool operator==(const BazaarListing&, const BazaarListing&) = default;
};

struct BazaarSale {
    std::string def_id;
    int64_t price = 0;
    int64_t at = 0;
};

// Listings a character may have up at once, by Merchant level (shop slots).
int bazaar_listing_limit(int merchant_level);
// The market's cut of a sale, by the seller's Merchant level.
double bazaar_fee_rate(int merchant_level);
int64_t bazaar_fee(int64_t price, int merchant_level);
inline constexpr int64_t bazaar_max_price = 100'000'000;
inline constexpr size_t bazaar_history = 500;  // sales remembered for price trends
inline constexpr int64_t bazaar_listing_days = 14;

struct PriceStats {
    int count = 0;
    int64_t low = 0, high = 0, average = 0, last = 0;
};
PriceStats price_stats(std::span<const BazaarSale> sales, std::string_view def_id);

std::string format_listings(std::span<const BazaarListing> listings);
std::vector<BazaarListing> parse_listings(std::string_view text);
std::string format_sales(std::span<const BazaarSale> sales);
std::vector<BazaarSale> parse_sales(std::string_view text);

// Whether a listing matches a search: words in its item's name or id (any case).
bool listing_matches(const BazaarListing& listing, std::string_view item_name, std::string_view query);

// ---------------------------------------------------------------------------
// Slicing (Smuggler)

// The slice an item carries (its color_json's "slice"), if any.
std::string item_slice(const ItemInstance& item);
// The item with a slice set (replacing any other).
void set_item_slice(ItemInstance& item, std::string_view slice_id);
// A sliced weapon or armor piece's changes, applied to what it resolves to.
void apply_slice(WeaponDef& weapon, const SliceDef& slice);
void apply_slice(Gear& gear, const SliceDef& slice);

// ---------------------------------------------------------------------------
// Entertainer

// Inspiration: one long buff, built by watching a performance (every pulse
// in the audience adds `inspiration_per_second` of it per second watched,
// more with a band of performers within `band_radius`), up to a cap (a
// Virtuoso's longer). Its modifiers' strength is its performer's (0.6 at
// level 1, full at 100). Kept with the character, it counts down only while
// they play: into matches with the loadout, where a death doesn't end it; in
// the open world, a death does.
inline constexpr double performance_radius = 14;
inline constexpr double band_radius = 10;
inline constexpr double performance_pulse = 5;          // seconds between pulses
inline constexpr double inspiration_per_second = 12;    // 5 minutes watched: an hour
inline constexpr double inspiration_cap = 3600;
inline constexpr double virtuoso_inspiration_cap = 5400;

struct Inspiration {
    std::string id;        // an InspirationDef's ("" none)
    double remaining = 0;  // seconds of play left
    double strength = 1;   // the share of its full modifiers, by its performer's level

    friend bool operator==(const Inspiration&, const Inspiration&) = default;
};
double inspiration_strength(int entertainer_level);
// A performer's tips each pulse, an audience or not (so performing pays
// while there are few players): 1 credit, 2 from level 50, 3 at 100, and
// 1 more for each of up to 8 watching.
int64_t performance_tips(int entertainer_level, int audience);
// Its buff for this many seconds watched, from a band of `band_size`
// (a Band Leader's band counts double), a flourish doubling it.
double inspiration_gain(double seconds, int band_size, bool band_leader, bool flourish);
// "id remaining strength" (empty: none), as saved and carried into matches.
std::string format_inspiration(const Inspiration& insp);
std::optional<Inspiration> parse_inspiration(std::string_view text);
// The effect it shows as: id "inspiration", flag inspired, its modifiers at its strength.
TimedEffect inspiration_effect(const InspirationDef& def, const Inspiration& insp);


// ---------------------------------------------------------------------------
// Fielded things the server keeps (server_specialists.cpp)

struct Deployable {
    uint32_t id = 0;
    DeployKind kind = DeployKind::none;
    std::string ability;  // what placed it
    uint32_t owner = 0;
    int team = -1;
    Vec3 position;
    double radius = 5;
    double remaining = 30;
    double amount = 0;   // a heal aura's health a second; a trap's damage
    double pulse = 0;    // seconds to its next pulse
    bool spent = false;  // a trap that's sprung
};

struct Hazard {
    uint32_t id = 0;
    uint32_t owner = 0;
    int team = -1;
    Vec3 position;
    double radius = 5;
    double remaining = 8;
    double damage_per_second = 10;
    DamageType damage_type = DamageType::bio;
    std::string kind = "bio";  // what clients draw: bio, smoke
};

struct PendingStrike {
    uint32_t owner = 0;
    int team = -1;
    Vec3 position;
    double radius = 6;
    double damage = 300;
    DamageType damage_type = DamageType::explosive;
    double remaining = 3;
};

struct ActiveCompanion {
    uint32_t entity = 0;  // its NPC
    uint32_t owner = 0;   // the owner's entity
    uint64_t record = 0;  // its datapad id
    std::string template_id;
    std::string name;
    CompanionStats stats;
    CompanionMode mode = CompanionMode::follow;
    uint32_t target = 0;
    Vec3 guard_at;
    double next_attack = 0;  // seconds
    double pulse = 0;        // a support droid's next heal, a recon droid's next scan
    double overcharge = 0;   // seconds left
    double overcharge_factor = 1;
    double recall_cooldown = 0;  // seconds until a leash teleport may happen again
};

// A character's Stage 12 state on the server: its kit's cooldowns, the
// effects on it, its datapad, samples, contracts and offers.
struct SpecialistState {
    std::vector<std::string> abilities;      // the active loadout's (unlocked_abilities)
    std::map<std::string, double> cooldowns; // ability id -> seconds left
    std::vector<TimedEffect> effects;
    std::vector<CompanionRecord> companions;
    uint64_t next_companion_id = 1;
    uint64_t next_sample_id = 1;
    std::vector<DnaSample> samples;
    uint64_t active_companion = 0;   // the fielded one's record (0: none)
    uint32_t companion_entity = 0;
    double companion_cooldown = 0;   // seconds until another can be fielded
    std::vector<Contract> contracts;  // accepted
    std::vector<Contract> offers;     // what the last terminal offered
    std::string offer_source;
    bool performing = false;
    double perform_pulse = 0;
    // Inspiration (docs/PROFESSIONS.md): what's built, what the character
    // chose to build from a performer's menu, what was last saved, and (a
    // match) whether the loadout's has been taken up.
    Inspiration inspiration;
    std::string inspiration_choice;
    mutable std::string saved_inspiration;
    bool inspiration_carried = false;
    bool second_wind_used = false;  // this life (a Squad Leader's Second Wind)
    bool bounty_flag = false;  // opted in to being hunted
    // A channel under way (taming, sampling): its ability, target, time left, where it began.
    std::string channel;
    uint32_t channel_target = 0;
    double channel_remaining = 0;
    Vec3 channel_from;
    std::vector<BazaarListing> bazaar_view;  // the last browse
    std::string bazaar_query;
    std::vector<std::pair<std::string, PriceStats>> bazaar_prices;
    uint32_t revision = 1;
    uint32_t acked = 0;
    uint32_t sync_tick = 0;
    mutable bool dirty = false;  // the datapad, samples or flags changed since saved
    bool loaded = false;
};

}  // namespace swm::core
