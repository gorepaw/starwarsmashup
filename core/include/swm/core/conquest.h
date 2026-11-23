#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "swm/core/loadout.h"
#include "swm/core/vec3.h"

namespace swm::core {

// A command post: a capture region that, while a team holds it, is where
// that team's soldiers can spawn.
struct CommandPost {
    std::string name;
    Vec3 position;              // game space; only x and z matter
    double radius = 10;         // m: the capture region, a circle on the ground
    int owner = -1;             // team holding it at the start, or -1: neutral
    bool capturable = true;
    double neutralize_time = 12;  // NeutralizeTime: seconds for one soldier to take it from its owner
    double capture_time = 10;     // CaptureTime: seconds for one soldier to take it once it's neutral
    double spawn_radius = 6;      // m: without spawn points, soldiers spawn on a ring this far from the post
    std::vector<Vec3> spawn_points;  // game space (x, z, and y if spawn_heights); used in turn
    // The spawn points' heights are given: soldiers spawn on the surface
    // there (a walkway, a bunker's floor), not on the terrain.
    bool spawn_heights = false;
    // How high the capture region reaches (game y): soldiers above or below
    // it (on the hill over a bunker) don't count. The position's y is its
    // middle.
    double low = -1e9, high = 1e9;
    // Where its console stands (Battlefront's post object), game space, if
    // the map says: on the floor the post is fought over. Its middle can be
    // nowhere to stand (Mustafar's CP5, out over the lava past a wall), and
    // its middle height nearer a roof no one reaches than that floor.
    std::optional<Vec3> console;

    bool contains(Vec3 point) const;
};

// An area bots keep out of when they can (Battlefront's AI barriers): a
// convex quadrilateral on the ground, game space (only x and z matter).
struct Barrier {
    std::array<Vec3, 4> corners;

    bool contains(Vec3 point) const;
};

// A region of space where soldiers die (Battlefront's death regions: a
// chasm, the bottom of a shaft): a box, sphere or cylinder, placed and
// turned. Game space.
struct DeathRegion {
    enum class Shape : uint8_t { box, sphere, cylinder };
    Shape shape = Shape::box;
    Vec3 center;
    // ASSUMPTION: Battlefront's region sizes are half sizes: a box's half
    // extents, a sphere's radius (x), a cylinder's radius (x) and half
    // height (y).
    Vec3 size;
    std::array<double, 9> basis{1, 0, 0, 0, 1, 0, 0, 0, 1};  // row-major; its columns are the region's axes

    bool contains(Vec3 point) const;
};

// A region that hurts the soldiers in it, `rate` health a second, whatever
// their armor (Battlefront's damage regions, always on: Mustafar's lava,
// the vacuum round Polis Massa's halls, some of Felucia's plants).
struct DamageRegion {
    DeathRegion where;  // its shape and place, as a death region's
    double rate = 0;
};

// A door (Battlefront's "door" class: Polis Massa's, Tantive IV's,
// Mustafar's...). It opens while a living soldier is within `radius` of its
// trigger (the class's AnimationTrigger: a hardpoint and a radius) and
// shuts when none is, taking `open_time` (its opening clip's length) either
// way. Shut more than halfway, it stops shots and blasts across its box (the
// door model's collision, in its place). Soldiers pass it either way: it's
// open by the time one reaches it. A locked door (IsLocked) isn't one of
// these: its collision is the map's, and it stays shut.
struct Door {
    Vec3 trigger;
    double radius = 3;
    double open_time = 1;
    DeathRegion box;  // a box: center, half extents, axes

    bool triggered_by(Vec3 feet) const;
    // Where along a..b (0..1) the segment enters its box, if it does.
    std::optional<double> hit(Vec3 a, Vec3 b) const;
};

// A health or ammo droid (Battlefront's "powerupstation" class: its
// com_item_healthrecharge and com_item_weaponrecharge). Every `delay` seconds
// each living soldier within `radius` gets `health` health and `ammo` of
// each weapon's full ammunition back (a fraction: 1 fills it), as the class's
// PowerupDelay, SoldierHealth and SoldierAmmo say. ASSUMPTION: SoldierAmmo
// is that fraction (the ammo droid's is 1.0, every 2 s), and a droid whose
// class sets no Radius reaches `default_radius`.
struct PowerupStation {
    static constexpr double default_radius = 3.0;
    static constexpr double default_max_health = 600.0;
    static constexpr double hit_radius = 0.75;
    static constexpr double hit_height = 1.6;
    Vec3 position;  // game space, at its feet
    double radius = default_radius;
    double delay = 1;
    double health = 0;
    double ammo = 0;
    double max_health = default_max_health;

    // Whether a soldier standing at `feet` is in its reach (a story above or
    // below it isn't).
    bool reaches(Vec3 feet) const;
};

// Dropped pickups left by dead soldiers (Battlefront's com_inf_default drop
// table and powerupitem classes in ingame.lvl): small and large health
// canisters, ammo canisters, dual health+ammo packs, and stamina packs.
// Offense and defense buffs wait until temporary buffs exist.
enum class PickupKind : uint8_t {
    health_small = 0,  // com_item_powerup_health25:  +100 health (0.15)
    health_large = 1,  // com_item_powerup_health100: +200 health (0.08)
    ammo = 2,          // com_item_powerup_ammo:      +1 clip (0.25)
    dual = 3,          // com_item_powerup_dual:      +200 health, +2 clips (0.08)
    energy = 4,        // com_item_powerup_energy:    +150 stamina (0.10)
};
inline constexpr size_t pickup_kind_count = 5;

// How near a soldier walks to pick up a dropped canister (ASSUMPTION:
// Battlefront's powerupitem ODF sets no radius).
inline constexpr double pickup_radius = 1.2;

struct PickupDef {
    PickupKind kind = PickupKind::health_small;
    const char* name = "";   // Battlefront class name
    const char* model = "";  // GeometryName in ingame.lvl
    double probability = 0;  // DropItemProbability in com_inf_default
    double health = 0;       // SoldierHealth
    int ammo_clips = 0;      // SoldierAmmo (clips)
    double energy = 0;       // SoldierEnergy
    double lifespan = 30;    // LifeSpan, seconds
};

const PickupDef& pickup_def(PickupKind kind);
// Rolls Battlefront's com_inf_default drop table for `roll` in [0, 1).
// Returns nullopt when nothing drops (~34% of deaths).
std::optional<PickupKind> roll_pickup(double roll);

struct Team {
    std::string name;
    // Loadouts its soldiers can spawn with: a stand-in for testing until the
    // hub sends each player's own gear.
    std::vector<Loadout> loadouts;
};

// Where a conquest match is fought: a bounded part of a planet, two teams
// and their test loadouts, and the command posts they fight over.
struct Battlefield {
    std::string name;
    std::filesystem::path terrain;  // its heightfield bake prefix, if its map names one
    double min_x = -1e9, min_z = -1e9, max_x = 1e9, max_z = 1e9;  // game space
    // The playable area within those (Battlefront's boundary paths): inside
    // any of these polygons (game space, x and z). None: all of the bounds.
    std::vector<std::vector<Vec3>> boundaries;
    std::vector<DeathRegion> death_regions;
    std::vector<DamageRegion> damage_regions;
    std::vector<PowerupStation> powerups;
    std::vector<Door> doors;
    std::array<Team, 2> teams;
    std::vector<CommandPost> posts;
    std::vector<Barrier> barriers;
    double tickets = 150;          // reinforcements per team: each death costs one
    double warmup = 15;            // seconds of practice before the match goes live
    double results = 15;           // seconds the results show before the next match
    double victory_time = 20;      // seconds a team must hold every post to win
    double bleed = 0.1;            // tickets per second the other team loses, per post of lead
    double out_of_bounds_time = 10;  // seconds outside the bounds before dying
    size_t min_players = 1;        // the warmup waits for this many
    // Where the HUD's minimap image lies, for drawing: the game-space x and z
    // under its left, top, right and bottom edges. Unset (all zero): the
    // client fits the image to the bounds.
    std::array<double, 4> minimap{};

    // Whether a point is on the battlefield: within the bounds and a
    // boundary.
    bool contains(Vec3 point) const;
    // Whether a soldier standing at `feet` is in a death region.
    bool deadly(Vec3 feet) const;
    // How much health a second a soldier standing at `feet` loses to damage
    // regions (feet or body in them, as for death regions).
    double damage_rate(Vec3 feet) const;
    // Somewhere not to be: deadly, or hurting. Bots keep out.
    bool harmful(Vec3 feet) const { return deadly(feet) || damage_rate(feet) > 0; }
    Vec3 center() const;
};

// Reads a battlefield file (see game/battlefields/). A post's `Altitude = low
// high` bounds its capture region in height, and its spawn points may give
// their heights (`SpawnPoint = x z y`), as does its console's place
// (`Console = x z y`). Positions in it are
// source-world coordinates (x z): Galaxies', as the snapshot tools print
// them, or Battlefront's (both are left-handed: game x = -x). Loadout files
// are relative to it. `Map = <file>` takes the bounds, terrain and command
// posts from a map file written by `bf2tool export-map` (its posts name
// their starting team by number: 1 is the first [team]), and its barriers
// (`Barrier = x1 z1 x2 z2 x3 z3 x4 z4`, corners in order), boundaries
// (`Boundary = x1 z1 x2 z2 ...`, a polygon; the bounds shrink to fit them)
// and death regions (`DeathRegion = box|sphere|cylinder x y z sx sy sz` and
// the rotation's rows, 9 numbers, as Battlefront stores them), and damage
// regions (`DamageRegion = <health a second>` and a death region's shape,
// place and rotation), and its health and ammo droids (`Powerup = x y z
// radius delay health ammo`, see PowerupStation), and its doors (`Door = x y z
// radius open_time` for the trigger, then its box as a death region's, `x y
// z sx sy sz` and 9 rotation numbers; see Door). `Minimap = x0 z0
// x1 z1` is where the map's minimap image lies: the coordinates under its
// left, top, right and bottom edges (north up). Throws
// std::runtime_error if it, a map or a loadout can't be read or makes no
// sense.
Battlefield load_battlefield(const std::filesystem::path& path);

// Every character is waiting to spawn (choosing a loadout and a post), alive,
// or dead (down until it may spawn again).
enum class Life : uint8_t { waiting, alive, dead };

enum class Phase : uint8_t { warmup, live, ended };

struct PostState {
    int8_t owner = -1;     // team holding it, or -1: neutral
    int8_t holder = -1;    // team the charge belongs to: the owner, or whoever is capturing a neutral post
    float charge = 0;      // 0..1; an owned post is full
    bool contested = false;  // both teams are in it: nothing changes

    friend bool operator==(const PostState&, const PostState&) = default;
};

struct MatchState {
    Phase phase = Phase::warmup;
    double time_left = 0;            // seconds left of the warmup or the results
    std::array<double, 2> tickets{};
    int8_t dominant = -1;            // team holding every post, counting down to victory
    double victory_left = 0;
    int8_t winner = -1;              // when ended; -1: a draw
    std::vector<PostState> posts;
    std::vector<float> powerup_health;  // current health per Battlefield::powerups[i]; <= 0: destroyed
    std::vector<float> doors;           // how open each of Battlefield::doors is, 0 (shut) .. 1
};

struct MatchEvent {
    enum Kind : uint8_t { went_live, ended, restarted, captured, neutralized };
    Kind kind = went_live;
    int team = -1;   // captured/neutralized: who took it; ended: the winner
    int post = -1;
};

// Battlefront conquest rules, without networking: command posts change hands
// while soldiers of one team stand in them, holding more posts bleeds the
// other team's reinforcements, and a team wins when the other runs out or
// when it holds every post for `victory_time`. A match is warmup (practice:
// deaths cost nothing and posts can't change hands), then live, then ended
// (the results), then the next warmup.
class Conquest {
public:
    // With `repeat` false there's one match: after the results it stays
    // ended (time_left 0).
    explicit Conquest(const Battlefield& battlefield, bool repeat = true);

    struct Unit {
        int team = 0;
        Vec3 position;
    };
    // Advances by `dt` seconds. `units` are the living soldiers; `players`
    // counts everyone in the match, for the warmup's minimum.
    std::vector<MatchEvent> step(double dt, std::span<const Unit> units, size_t players);
    // A soldier of `team` died: a reinforcement lost while live.
    void killed(int team);
    // Whether a soldier of `team` may spawn at `post` now.
    bool can_spawn(int team, size_t post) const;
    // Whether health/ammo droid `index` is intact.
    bool powerup_alive(size_t index) const;
    // Damages health/ammo droid `index`. Returns true on the hit that destroys it.
    bool damage_powerup(size_t index, double amount);
    // Whether door `index` is shut (more than half), stopping shots.
    bool door_shut(size_t index) const;
    // Where along a..b (0..1) the segment first meets a shut door, if it does.
    std::optional<double> door_hit(Vec3 a, Vec3 b) const;
    const MatchState& state() const { return state_; }
    const Battlefield& battlefield() const { return battlefield_; }

private:
    void reset();
    void capture(double dt, std::span<const Unit> units, std::vector<MatchEvent>& events);
    void move_doors(double dt, std::span<const Unit> units);
    void finish(int winner, std::vector<MatchEvent>& events);

    Battlefield battlefield_;
    bool repeat_ = true;
    MatchState state_;
};

// More soldiers capture faster, up to this many (ASSUMPTION: Battlefront
// speeds captures up with more soldiers; its exact rule isn't known).
inline constexpr int max_capturing_soldiers = 3;

} // namespace swm::core
