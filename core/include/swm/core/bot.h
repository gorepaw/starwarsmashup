#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "swm/core/client.h"
#include "swm/core/conquest.h"
#include "swm/core/net/udp.h"
#include "swm/core/vec3.h"

namespace swm::core {

class NavGrid;
class World;

struct BotSkill {
    double aim_error = 2.5;    // degrees: how far its aim wanders
    double reaction = 0.4;     // seconds from spotting an enemy to shooting
    double fire_range = 60;    // m
    double cover = 0.7;        // 0..1: how often it takes cover to reload or when hurt
    double dodge = 0.15;       // 0..1: how often it rolls aside in a fight (every few seconds)

    // "easy", "normal" or "hard" (anything else: normal).
    static BotSkill preset(std::string_view difficulty);
};

// How fast (of full speed) to go from `position` heading for `path[corner]`:
// slower within a couple of meters along the route of a drop, so as to land
// near its foot (off the edge at a run, a soldier lands meters past it).
double drop_pace(Vec3 position, const std::vector<Vec3>& path, size_t corner);

// Shared combat helpers for battle bots and open-world bandits.
std::optional<Vec3> find_cover_spot(const World& world, const NavGrid* nav, const Battlefield* field,
                                    Vec3 position, Vec3 enemy_eye, double step_up, uint64_t& rng,
                                    bool& out_crouch);
bool aim_at_target(Vec3 eye, Vec3 target_pos, Vec3 target_vel, const WeaponDef& weapon, double gravity,
                   double aim_error_deg, double now, double aim_phase, float& out_yaw, float& out_pitch);

struct CombatBrain {
    std::optional<Vec3> cover;
    bool cover_crouch = false;
    bool cover_reached = false;
    double cover_hold = 0;
    double cover_until = 0;
    double next_cover = 0;
    double strafe = 1;
    double next_strafe = 0;
    double next_roll = 0;
    uint32_t target_id = 0;
    double target_since = 0;
    double aim_phase = 0;
    double slow_since = -1;
    double unstick_until = 0;
    std::vector<Vec3> path;
    size_t corner = 0;
    Vec3 path_goal;
    double replan_at = 0;
    double earliest_plan = 0;

    void reset();
    MoveInput step_combat(const MoveState& me, double health_frac, const Loadout& loadout,
                          const CharacterStats& stats, uint32_t target_id_in, Vec3 target_pos,
                          Vec3 target_vel, const World& world, const NavGrid* nav,
                          const BotSkill& skill, double now, uint64_t& rng);
};

// A simple soldier that plays through a GameClient, with the same controls
// and the same view of the world a player has. In a match it spawns at the
// post nearest the fighting, runs to the nearest post its team doesn't hold
// (or guards one), and shoots enemies it can see, leading them (and lobbing
// grenades and mortar shells on a ballistic arc). Hurt or short of ammo, it
// goes to a health or ammo droid nearby first, and stays until it's
// patched up or rearmed. In a fight it takes cover (somewhere near the
// enemy can't see, crouched behind something low) to reload or when hurt,
// falls back toward a post its team holds when badly hurt with no droid
// near, and now and then rolls aside, as often as its skill says. In a hub it
// walks to the nearest war terminal and queues for a battle. Elsewhere it
// hunts the nearest other character. With a NavGrid it walks around
// obstacles on the grid's routes; without one it runs straight. Either way
// it jumps when stuck.
class Bot {
public:
    explicit Bot(uint64_t seed, BotSkill skill = {});

    // Sets the client's controls (and spawn requests) for the next ticks.
    // Call before client.update().
    void update(GameClient& client, const World& world, double now);

    // The command post it's heading for, or -1.
    int objective() const { return objective_; }
    // The health or ammo droid (index into the battlefield's powerups) it's
    // going to, or -1.
    int powerup() const { return powerup_; }
    // The dropped health/ammo pickup id it's heading for, or 0.
    uint32_t pickup() const { return pickup_; }
    void set_skill(BotSkill skill) { skill_ = skill; }
    const BotSkill& skill() const { return skill_; }
    // Where it's taking cover, if it is.
    const std::optional<Vec3>& cover() const { return cover_; }
    // The destructible (index into the battlefield's) it's shooting its way
    // through, or -1.
    int breach() const { return breach_; }
    // The side it queues for in a hub (default: picked from the seed).
    void set_queue_team(int team) { queue_team_ = team; }
    // The walkable grid of the battlefield it plays on (shared between bots;
    // null: run straight).
    void set_navigation(std::shared_ptr<const NavGrid> navigation) { navigation_ = std::move(navigation); }
    // The route it's following (corners on the ground), and the next corner.
    const std::vector<Vec3>& path() const { return path_; }
    size_t next_corner() const { return corner_; }

private:
    double random();  // 0..1
    void go_to_recruiter(GameClient& client, double now);
    void choose_spawn(GameClient& client);
    void choose_objective(const GameClient& client, double now);
    // Whether it needs a droid (hurt, or short of ammo), and if so heads for
    // the nearest that helps; lets go once it's patched up or rearmed.
    void choose_powerup(const GameClient& client, double now);
    // Somewhere near (on ground it can stand on) that the enemy at `enemy`
    // (its eyes) can't see: behind something tall, or low (then crouched).
    std::optional<Vec3> find_cover(const GameClient& client, const World& world, Vec3 enemy, bool& crouch);
    // Its route ending short of `destination` by something standing that can
    // be destroyed (Kashyyyk's barricades), in sight from `eye` and range:
    // that (its index), or -1.
    int find_breach(const GameClient& client, const World& world, Vec3 destination, Vec3 eye) const;
    // Where to head now on the way to `destination`: the next corner of its
    // route (planned again when the destination moves, when it's stuck, and
    // now and then), or the destination itself without a grid. Sets pace_.
    Vec3 steer(Vec3 position, Vec3 destination, double now);

    uint64_t rng_;
    BotSkill skill_;
    bool requested_ = false;  // asked to spawn since it last died
    int spawn_post_ = -1;     // the post it asked to spawn at
    int objective_ = -1;
    int powerup_ = -1;
    uint32_t pickup_ = 0;     // dropped pickup id it's heading for
    Vec3 powerup_spot_;       // beside the droid, or on the pickup, where it goes
    double powerup_until_ = 0;  // gives up then
    double next_powerup_ = 0;   // looks for one again then
    std::optional<Vec3> cover_;  // where it's taking cover
    bool cover_crouch_ = false;  // ...crouched, behind something low
    bool cover_reached_ = false;
    double cover_hold_ = 0;      // seconds it stays once there
    double cover_until_ = 0;     // leaves (or gives up getting there) then
    double next_cover_ = 0;      // thinks about cover again then
    double retreat_until_ = 0;   // falling back to a held post until then
    double next_roll_ = 0;       // may roll aside then
    int breach_ = -1;            // the destructible in its way it's shooting, or -1
    double next_breach_ = 0;     // looks for one again then
    Vec3 offset_;            // where in the post it stands
    Vec3 home_;              // the post's console (or middle), from its middle: where a spot out of reach gives way to
    double next_objective_ = 0;
    uint32_t target_ = 0;
    double target_since_ = 0;
    double strafe_ = 1;
    double next_strafe_ = 0;
    double aim_phase_ = 0;
    double slow_since_ = -1;  // moving too slowly since
    double unstick_until_ = 0;
    double next_grenade_ = 0;
    double throw_until_ = 0;
    double grenade_pitch_ = 0;
    int queue_team_ = 0;
    std::shared_ptr<const NavGrid> navigation_;
    std::vector<Vec3> path_;
    size_t corner_ = 0;
    Vec3 path_goal_;
    Vec3 path_from_;           // where the route was planned from: its first leg's start
    bool rejoin_ = false;      // off its leg, its own way to the corner not clear: back onto the leg
    double replan_at_ = 0;     // plans again then (sooner if the destination moves)
    double earliest_plan_ = 0;  // but not before then
    double next_look_ = 0;     // checks then whether the corner after next is in a straight line
    double pace_ = 1;          // of full speed: slower coming to a drop (drop_pace)
};

// Bots connected to a server like players, each with its own UDP socket,
// e.g. to fill the teams of a match. They're named "Bot 1", "Bot 2", ...
class BotPlayers {
public:
    using LinkFactory = std::function<std::unique_ptr<net::Link>()>;

    // `base` is each bot's client config: the server, and the soldier or
    // battlefield the server uses. Each bot gets a link from `open_link`
    // (default: a UDP socket on a free port). Throws if one can't be opened.
    BotPlayers(int count, const World& world, const ClientConfig& base, LinkFactory open_link = {},
               BotSkill skill = {});
    // Every bot's skill, from now on.
    void set_skill(BotSkill skill);
    void update(double now);
    void disconnect();
    size_t size() const { return players_.size(); }

private:
    struct Player {
        std::unique_ptr<net::Link> link;
        GameClient client;
        Bot bot;
        Player(std::unique_ptr<net::Link> l, const World& world, ClientConfig config, uint64_t seed)
            : link(std::move(l)), client(*link, world, std::move(config)), bot(seed) {}
    };
    const World& world_;
    std::shared_ptr<const NavGrid> navigation_;  // the battlefield's, shared by the bots
    std::vector<Destructible> destructibles_;    // the battlefield's
    std::vector<uint8_t> destroyed_;             // as last seen, to forget routes past them when it changes
    std::vector<std::unique_ptr<Player>> players_;
};

} // namespace swm::core
