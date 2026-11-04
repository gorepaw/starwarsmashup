#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "swm/core/combat.h"
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
    double spawn_radius = 6;      // m: soldiers spawn on a ring this far from the post

    bool contains(Vec3 point) const;
};

struct Team {
    std::string name;
    std::vector<SoldierDef> classes;  // what its soldiers can spawn as
};

// Where a conquest match is fought: a bounded part of a planet, two teams
// and their classes, and the command posts they fight over.
struct Battlefield {
    std::string name;
    double min_x = -1e9, min_z = -1e9, max_x = 1e9, max_z = 1e9;  // game space
    std::array<Team, 2> teams;
    std::vector<CommandPost> posts;
    double tickets = 150;          // reinforcements per team: each death costs one
    double warmup = 15;            // seconds of practice before the match goes live
    double results = 15;           // seconds the results show before the next match
    double victory_time = 20;      // seconds a team must hold every post to win
    double bleed = 0.1;            // tickets per second the other team loses, per post of lead
    double out_of_bounds_time = 10;  // seconds outside the bounds before dying
    size_t min_players = 1;        // the warmup waits for this many

    bool contains(Vec3 point) const;
    Vec3 center() const;
};

// Reads a battlefield file (see game/battlefields/). Positions in it are
// Galaxies world coordinates (x z), as the snapshot tools print them; class
// files are relative to it. Throws std::runtime_error if it, or a class
// file, can't be read or makes no sense.
Battlefield load_battlefield(const std::filesystem::path& path);

// Every character is waiting to spawn (choosing a class and a post), alive,
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
    const MatchState& state() const { return state_; }
    const Battlefield& battlefield() const { return battlefield_; }

private:
    void reset();
    void capture(double dt, std::span<const Unit> units, std::vector<MatchEvent>& events);
    void finish(int winner, std::vector<MatchEvent>& events);

    Battlefield battlefield_;
    bool repeat_ = true;
    MatchState state_;
};

// More soldiers capture faster, up to this many (ASSUMPTION: Battlefront
// speeds captures up with more soldiers; its exact rule isn't known).
inline constexpr int max_capturing_soldiers = 3;

} // namespace swm::core
