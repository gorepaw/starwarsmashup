#include "swm/core/bot.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

#include "swm/core/nav.h"
#include "swm/core/vehicle.h"
#include "swm/core/world.h"

namespace swm::core {

namespace {

double random_unit(uint64_t& rng) {
    uint64_t z = (rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return double(z >> 11) / double(1ull << 53);
}

double horizontal_distance(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

// The yaw that faces along a direction (see aim_direction).
double yaw_toward(Vec3 d) {
    return std::atan2(-d.x, -d.z);
}

// An angle in -pi..pi.
double wrap_angle(double a) {
    a = std::fmod(a + std::numbers::pi, 2 * std::numbers::pi);
    if (a < 0) a += 2 * std::numbers::pi;
    return a - std::numbers::pi;
}

// Whether going along `move` from `feet` takes it over a deadly edge, up to
// `reach` on (what a soldier slides on stopping, a meter and a half and more
// at a run; less where its route turns, at a catwalk's corner; more for a
// jump):
// no ground below, or a fall onto or through a death region (a catwalk's
// edge over the Death Star's reactor pit, its hub's bridge without rails,
// the thin one over a lower floor by CP5) or a damage region (into
// Mustafar's lava). A fall onto safe ground is no
// edge: soldiers come to no harm (off a Kashyyyk platform, into a dip on
// Endor's forest floor); unless there's no way back from it (off a Kamino
// walkway onto the pod roofs under it, which lead nowhere).
bool over_edge(const World& world, const Battlefield* field, const NavGrid* nav, Vec3 feet, Vec3 move,
               double step_up, double reach = 1.5) {
    const double length = std::hypot(move.x, move.z);
    if (length < 1e-6) {
        return false;
    }
    constexpr double climb = 1.5;  // m up per m on: a ramp as steep as soldiers walk up (55 degrees)
    // m: a gap soldiers walk over (between the Death Star's hub's disc and
    // bridge); a wider one, a hole (a door's slot in Tantive IV's doorways)
    constexpr double crack = 0.4;
    const auto hole = [&](double along) {
        const Vec3 p = feet + Vec3{move.x, 0, move.z} * (along / length);
        const double ground = world.ground_at(p.x, p.z, feet.y + climb * along, step_up).height;
        if (ground < -1e8) {
            return true;
        }
        // Lower than a slope down goes: a drop.
        if (nav != nullptr && ground < feet.y - step_up - climb * along &&
            !nav->leads_back(feet, {p.x, ground, p.z})) {
            return true;
        }
        // From the ground up (a pit's floor deadly at once), under a body's
        // middle apart (Battlefield::deadly looks there too); the ground on
        // the level too (into Tantive IV's turbine).
        for (double y = ground; field != nullptr; y += 0.9) {
            if (field->harmful({p.x, y, p.z})) {
                return true;
            }
            if (y + 0.9 >= feet.y) {
                break;
            }
        }
        return false;
    };
    for (double on = 0.5; on < reach + 0.5; on += 0.5) {
        const double along = std::min(on, reach);
        if (hole(along) && hole(along + crack)) {
            return true;
        }
    }
    return false;
}

} // namespace

Bot::Bot(uint64_t seed, BotSkill skill) : rng_(seed * 0x9E3779B97F4A7C15ull + 1), skill_(skill) {
    aim_phase_ = random() * 100;
    queue_team_ = random() < 0.5 ? 0 : 1;
}

// Walks to the nearest war terminal and queues there.
void Bot::go_to_recruiter(GameClient& client, double now) {
    GameClient::Controls controls;
    const MoveState& me = client.predicted();
    controls.yaw = me.yaw;
    const net::Recruiter* nearest = nullptr;
    for (const net::Recruiter& r : client.recruiters()) {
        if (nearest == nullptr ||
            horizontal_distance(me.position, r.position) < horizontal_distance(me.position, nearest->position)) {
            nearest = &r;
        }
    }
    const double distance = horizontal_distance(me.position, nearest->position);
    if (client.player_status().queue == net::PlayerStatus::none) {
        if (distance > std::max(0.5, nearest->radius - 1)) {
            const Vec3 d{nearest->position.x - me.position.x, 0, nearest->position.z - me.position.z};
            controls.yaw = yaw_toward(d);
            controls.forward = 1;
            controls.sprint = distance > 20;
        } else if (now >= next_objective_) {
            client.queue(queue_team_);
            next_objective_ = now + 2;
        }
    }
    client.set_input(controls);
}

double Bot::random() {
    // splitmix64
    uint64_t z = (rng_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return double(z >> 11) / double(1ull << 53);
}

void Bot::choose_spawn(GameClient& client) {
    const Battlefield& field = *client.config().battlefield;
    const MatchState& match = *client.match();
    const int team = client.self().team;
    const auto& loadouts = field.team(team).loadouts;
    // Mostly the first loadout, sometimes another.
    const size_t loadout = random() < 0.5 ? 0 : size_t(random() * double(loadouts.size())) % loadouts.size();
    if (team == natives_team) {
        // A native: any post it may spawn at (Conquest::can_spawn's rule),
        // at random.
        const bool any_ally_points = std::any_of(field.posts.begin(), field.posts.end(),
                                                 [](const CommandPost& p) { return !p.ally_spawn_points.empty(); });
        std::vector<int> posts;
        for (size_t i = 0; i < match.posts.size() && i < field.posts.size(); ++i) {
            const int owner = match.posts[i].owner;
            if (any_ally_points ? !field.posts[i].ally_spawn_points.empty() && owner >= 0 && field.allied(team, owner)
                                : owner < 0) {
                posts.push_back(int(i));
            }
        }
        if (!posts.empty()) {
            const int post = posts[size_t(random() * double(posts.size())) % posts.size()];
            client.request_spawn(int(loadout), post);
            requested_ = true;
            spawn_post_ = post;
        }
        return;
    }
    // The owned post closest to one we don't hold; now and then any owned post.
    int best = -1;
    double best_distance = std::numeric_limits<double>::max();
    for (size_t i = 0; i < match.posts.size(); ++i) {
        if (match.posts[i].owner != team) {
            continue;
        }
        double distance = random() < 0.25 ? 0 : std::numeric_limits<double>::max() / 2;
        for (size_t j = 0; j < match.posts.size(); ++j) {
            if (match.posts[j].owner != team) {
                distance = std::min(distance,
                                    horizontal_distance(field.posts[i].position, field.posts[j].position));
            }
        }
        if (distance < best_distance) {
            best_distance = distance;
            best = int(i);
        }
    }
    if (best >= 0) {
        client.request_spawn(int(loadout), best);
        requested_ = true;
        spawn_post_ = best;
    }
}

void Bot::choose_objective(const GameClient& client, double now) {
    const Battlefield& field = *client.config().battlefield;
    const MatchState& match = *client.match();
    const int team = client.self().team;
    const Vec3 me = client.predicted().position;
    // The nearest post we don't hold, or are losing (now and then another
    // one, so a team spreads out); else guard one.
    int best = -1;
    double best_distance = std::numeric_limits<double>::max();
    for (size_t i = 0; i < match.posts.size(); ++i) {
        const PostState& post = match.posts[i];
        // (A native's: a friend's.)
        const bool ours = post.owner >= 0 && field.allied(team, post.owner) && post.charge >= 1 && !post.contested;
        if (ours || !field.posts[i].capturable) {
            continue;
        }
        const double distance = horizontal_distance(me, field.posts[i].position) * (random() < 0.3 ? 0.3 : 1.0);
        if (distance < best_distance) {
            best_distance = distance;
            best = int(i);
        }
    }
    if (best < 0) {
        best = int(random() * double(match.posts.size())) % int(match.posts.size());
    }
    // In the post it's after already, now and then somewhere else in it: one
    // spot held for good, a post's defenders and attackers can stand either
    // side of a pillar and never meet (Tantive IV's CP4).
    const bool inside = best == objective_ &&
                        horizontal_distance(me, field.posts[size_t(best)].position) < field.posts[size_t(best)].radius;
    if (best != objective_ || (inside && random() < 0.5)) {
        objective_ = best;
        // Somewhere in the post, on ground within its reach in height: not
        // in the rock beside a post in a cave (Polis Massa's CP3), where the
        // ground nearest is the hillside over it.
        const CommandPost& post = field.posts[size_t(best)];
        // Its console, where the map gives it (its middle can be nowhere to
        // stand: Mustafar's CP5, out over the lava past a wall).
        home_ = post.console ? Vec3{post.console->x - post.position.x, 0, post.console->z - post.position.z} : Vec3{};
        offset_ = home_;
        for (int attempt = 0; attempt < 6; ++attempt) {
            const double angle = random() * 2 * std::numbers::pi;
            const Vec3 offset = Vec3{std::cos(angle), 0, std::sin(angle)} * (random() * post.radius * 0.6);
            const Vec3 spot = post.position + offset;
            if (!navigation_ || post.high > 1e8 ||
                navigation_->walkable_between(spot.x, spot.z, post.low - 0.5, post.high)) {
                offset_ = offset;
                break;
            }
        }
    }
    next_objective_ = now + 4 + random() * 4;
}

double drop_pace(Vec3 position, const std::vector<Vec3>& path, size_t corner) {
    constexpr double ahead = 2;  // m along the route
    double along = 0;
    Vec3 last = position;
    for (size_t k = corner; k < path.size() && along <= ahead; ++k) {
        along += horizontal_distance(last, path[k]);
        if (last.y - path[k].y >= 1.5) {
            return std::clamp(along / ahead, 0.3, 1.0);
        }
        last = path[k];
    }
    return 1;
}

Vec3 Bot::steer(Vec3 position, Vec3 destination, double now) {
    if (!navigation_) {
        return destination;
    }
    const bool moved = path_.empty() || horizontal_distance(destination, path_goal_) > 2;
    if ((moved || now >= replan_at_) && now >= earliest_plan_) {
        path_ = navigation_->find_path(position, destination);
        // Its spot in the post out of reach (on a ledge, behind a wall): the
        // post's console (or middle) next time, not standing at the nearest
        // place for good.
        if (path_.empty() || horizontal_distance(path_.back(), destination) > 3) {
            offset_ = home_;
        }
        corner_ = 0;
        path_goal_ = destination;
        path_from_ = position;
        replan_at_ = now + 4 + random() * 2;
        earliest_plan_ = now + 0.5;
    }
    // On past corners it has reached (at their height: a corner at the foot
    // of a drop isn't reached from the top), or can cut to the one after.
    // Near one, on only once the way to the next is clear from where it
    // stands, or it's right there: beside a ramp's foot, it isn't on it.
    while (corner_ + 1 < path_.size() && horizontal_distance(position, path_[corner_]) < 0.6 &&
           std::abs(position.y - path_[corner_].y) < 1.5 &&
           (horizontal_distance(position, path_[corner_]) < 0.2 ||
            navigation_->clear_line(position, path_[corner_ + 1]))) {
        ++corner_;
    }
    // The nearest place on its leg of the route (from the corner before to
    // the one it's after).
    const auto on_leg = [&]() {
        const Vec3 from = corner_ > 0 ? path_[corner_ - 1] : path_from_;
        const Vec3 to = path_[corner_];
        const Vec3 leg{to.x - from.x, 0, to.z - from.z};
        const double length2 = leg.x * leg.x + leg.z * leg.z;
        const double t =
            length2 < 1e-9 ? 1 : std::clamp(((position.x - from.x) * leg.x + (position.z - from.z) * leg.z) / length2,
                                            0.0, 1.0);
        return Vec3{from.x + leg.x * t, from.y + (to.y - from.y) * t, from.z + leg.z * t};
    };
    if (now >= next_look_) {
        next_look_ = now + 0.25;
        if (corner_ + 1 < path_.size() && navigation_->clear_line(position, path_[corner_ + 1])) {
            ++corner_;
        }
        // Off its leg (pushed aside in a fight), its own line to the corner
        // not clear: back onto the leg first, not straight across (beside
        // Kamino's tube bridges, down a pod's roof into the sea).
        rejoin_ = corner_ < path_.size() && horizontal_distance(position, on_leg()) > 1 &&
                  !navigation_->clear_line(position, path_[corner_]);
    }
    pace_ = 1;
    if (corner_ >= path_.size()) {
        return destination;  // no route: straight at it
    }
    // Coming to a drop (within a couple of meters along the route), slowly:
    // off the edge at a run, a soldier lands meters past its foot (off a
    // narrow ledge, Yavin's temple roof's).
    pace_ = drop_pace(position, path_, corner_);
    const Vec3 corner = path_[corner_];
    if (rejoin_) {
        // Onto the leg, a meter on toward the corner.
        const Vec3 on = on_leg();
        Vec3 ahead{corner.x - on.x, 0, corner.z - on.z};
        const double left = ahead.length();
        return left > 1 ? on + ahead * (1 / left) : corner;
    }
    // Over a corner at the foot of a drop, still up top: on the way it was
    // going, over the edge.
    if (horizontal_distance(position, corner) < 0.6 && position.y - corner.y >= 1.5) {
        const Vec3 before = corner_ > 0 ? path_[corner_ - 1] : position;
        Vec3 along{corner.x - before.x, 0, corner.z - before.z};
        if (along.length() < 0.1 && corner_ + 1 < path_.size()) {
            along = {path_[corner_ + 1].x - corner.x, 0, path_[corner_ + 1].z - corner.z};
        }
        if (along.length() > 1e-6) {
            return corner + along * (1.0 / along.length());
        }
    }
    // The last corner is the reachable spot nearest the destination: there,
    // short of it (by the reactor's pit, CP3 out on a pillar in it), it
    // stays, not to and fro over it at a run.
    if (corner_ + 1 == path_.size() && horizontal_distance(path_.back(), destination) >= 0.5 &&
        horizontal_distance(position, path_.back()) < 0.4) {
        return position;
    }
    return corner_ + 1 == path_.size() && horizontal_distance(path_.back(), destination) < 0.5 ? destination
                                                                                                 : path_[corner_];
}

// Hurt below this (of full health), a bot looks for a health droid or pickup;
// it stays at a droid until this much.
constexpr double needs_health = 0.5, patched_up = 0.95;
// Its primary's ammo (clip and spare) below this (of full), for an ammo droid
// or pickup.
constexpr double needs_ammo = 0.3;
constexpr double powerup_reach = 45;     // m: a droid further than this isn't worth the walk
constexpr double dropped_reach = 25;     // m: a dropped pickup further than this isn't worth detours
constexpr double nearby_pickup = 10;     // m: grab a close dropped pack even when only lightly hurt/spent
constexpr double powerup_patience = 25;  // s: gives up on one after this

BotSkill BotSkill::preset(std::string_view difficulty) {
    if (difficulty == "easy") {
        return {.aim_error = 5.0, .reaction = 0.8, .fire_range = 45, .cover = 0.3, .dodge = 0};
    }
    if (difficulty == "hard") {
        return {.aim_error = 1.2, .reaction = 0.25, .fire_range = 80, .cover = 1.0, .dodge = 0.35};
    }
    return {};
}

std::optional<Vec3> find_cover_spot(const World& world, const NavGrid* nav, const Battlefield* field,
                                    Vec3 position, Vec3 enemy_eye, double step_up, uint64_t& rng,
                                    bool& out_crouch) {
    std::optional<Vec3> best;
    double best_score = std::numeric_limits<double>::max();
    const double start = random_unit(rng) * 2 * std::numbers::pi;
    for (const double reach : {2.5, 5.0, 8.0, 11.0}) {
        for (int i = 0; i < 12; ++i) {
            const double angle = start + i * std::numbers::pi / 6;
            Vec3 spot{position.x + std::cos(angle) * reach, 0, position.z + std::sin(angle) * reach};
            spot.y = world.support_at(spot.x, spot.z, position.y + step_up, step_up).height;
            if (std::abs(spot.y - position.y) > 2.5 || (field && field->harmful(spot)) ||
                (nav && !nav->walkable(spot))) {
                continue;
            }
            const bool high = world.raycast(enemy_eye, spot + Vec3{0, 1.6, 0}) &&
                              world.raycast(enemy_eye, spot + Vec3{0, 1.0, 0});
            const bool low = world.raycast(enemy_eye, spot + Vec3{0, 0.9, 0}) &&
                             world.raycast(enemy_eye, spot + Vec3{0, 0.4, 0});
            if (!high && !low) {
                continue;
            }
            const double toward = horizontal_distance(spot, enemy_eye) < horizontal_distance(position, enemy_eye) ? 2 : 0;
            const double score = reach + toward + (high ? 0 : 1);
            if (score < best_score) {
                best_score = score;
                best = spot;
                out_crouch = !high;
            }
        }
        if (best) {
            break;
        }
    }
    return best;
}

bool aim_at_target(Vec3 eye, Vec3 target_pos, Vec3 target_vel, const WeaponDef& weapon, double gravity,
                   double aim_error_deg, double now, double aim_phase, float& out_yaw, float& out_pitch) {
    const bool lobbed = weapon.projectile == Projectile::grenade;
    const Vec3 point = target_pos + Vec3{0, lobbed ? 0.2 : 1.2, 0};
    const double distance = (target_pos - eye).length();
    const double flight = distance / std::max(1.0, weapon.bolt_speed);
    const Vec3 d = point + target_vel * flight - eye;
    const double wobble = aim_error_deg * std::numbers::pi / 180;
    const auto arc = ballistic_pitch(weapon.bolt_speed, weapon.bolt_gravity * gravity, std::hypot(d.x, d.z), d.y);
    out_yaw = float(yaw_toward(d) + wobble * std::sin(now * 2.3 + aim_phase));
    out_pitch = float(arc.value_or(0) - weapon.shot_elevate + wobble * std::sin(now * 3.1 + aim_phase * 2));
    return arc.has_value();
}

void CombatBrain::reset() {
    cover.reset();
    slow_since = -1;
    unstick_until = 0;
    target_id = 0;
    path.clear();
    corner = 0;
}

MoveInput CombatBrain::step_combat(const MoveState& me, double health_frac, const Loadout& loadout,
                                   const CharacterStats& stats, uint32_t target_id_in, Vec3 target_pos,
                                   Vec3 target_vel, const World& world, const NavGrid* nav,
                                   const BotSkill& skill, double now, uint64_t& rng) {
    MoveInput input;
    if (target_id_in != target_id) {
        target_id = target_id_in;
        target_since = now;
    }
    const Vec3 eye = me.position + Vec3{0, eye_height_of(me), 0};
    static const WeaponDef unarmed;
    const WeaponDef* held = weapon_in_slot(loadout, me.combat.weapon);
    const WeaponDef& weapon = held != nullptr ? *held : unarmed;
    const WeaponState& clip = me.combat.weapons[me.combat.weapon];
    const bool reloading = me.combat.reload > 0 || clip.overheated ||
                           (weapon.rounds_per_clip > 0 && clip.rounds == 0);

    bool want_crouch = false;
    bool roll = false;
    if (cover && !cover_reached && horizontal_distance(*cover, me.position) < 0.6) {
        cover_reached = true;
        cover_until = now + cover_hold;
    }
    if (cover && now >= cover_until) {
        cover.reset();
        next_cover = now + 4 + random_unit(rng) * 4;
    }
    if (!cover && now >= next_cover && (reloading || health_frac < 0.5)) {
        next_cover = now + 2;
        if (random_unit(rng) < skill.cover) {
            const Vec3 enemy_eye = target_pos + Vec3{0, 1.6, 0};
            if (auto spot = find_cover_spot(world, nav, nullptr, me.position, enemy_eye, stats.move.step_up, rng,
                                            cover_crouch)) {
                cover = spot;
                cover_reached = false;
                cover_until = now + 6;
                cover_hold = std::max(2.0, me.combat.reload) + 1.0 + random_unit(rng) * 2;
            }
        }
    }

    Vec3 destination = target_pos;
    double stop_dist = 10.0;
    if (cover) {
        destination = *cover;
        stop_dist = 0.4;
        want_crouch = cover_crouch && horizontal_distance(*cover, me.position) < 1.0;
    }
    if (now >= next_roll && !cover) {
        next_roll = now + 2.5 + random_unit(rng) * 3;
        roll = random_unit(rng) < skill.dodge && me.energy > 50 && me.grounded;
    }
    if (now >= next_strafe) {
        strafe = random_unit(rng) < 0.5 ? -1 : 1;
        next_strafe = now + 0.8 + random_unit(rng) * 1.5;
    }

    Vec3 move;
    const double to_go = horizontal_distance(destination, me.position);
    if (to_go > stop_dist) {
        Vec3 toward = destination;
        if (nav) {
            const bool moved = path.empty() || horizontal_distance(destination, path_goal) > 2.0;
            if ((moved || now >= replan_at) && now >= earliest_plan) {
                path = nav->find_path(me.position, destination);
                corner = 0;
                path_goal = destination;
                replan_at = now + 3.0;
                earliest_plan = now + 0.5;
            }
            while (corner + 1 < path.size() && horizontal_distance(me.position, path[corner]) < 0.6) {
                ++corner;
            }
            if (corner < path.size()) {
                toward = path[corner];
            }
        }
        const Vec3 d{toward.x - me.position.x, 0, toward.z - me.position.z};
        if (d.length() > 1e-3) {
            move = d * (1.0 / d.length());
        }
    }

    const Vec3 enemy_chest = target_pos + Vec3{0, 1.2, 0};
    const double dist = (target_pos - me.position).length();
    const bool has_los = dist <= skill.fire_range && !world.raycast(eye, enemy_chest);
    float aim_yaw = me.yaw;
    float aim_pitch = 0.0f;
    const bool has_arc = aim_at_target(eye, target_pos, target_vel, weapon, stats.move.gravity,
                                       skill.aim_error, now, aim_phase, aim_yaw, aim_pitch);
    input.yaw = aim_yaw;
    input.pitch = aim_pitch;
    if (has_los && has_arc && now - target_since >= skill.reaction) {
        input.buttons |= MoveInput::fire;
    }

    const double s = std::sin(input.yaw), c = std::cos(input.yaw);
    move = move * 0.6 + Vec3{c, 0, -s} * (strafe * 0.7);

    const double speed = std::hypot(me.velocity.x, me.velocity.z);
    if (move.length() > 0.1 && speed < 1.0 && me.grounded) {
        if (slow_since < 0) {
            slow_since = now;
        } else if (now - slow_since > 1.0) {
            unstick_until = now + 0.8;
            slow_since = -1;
            replan_at = now;
        }
    } else {
        slow_since = -1;
    }
    if (now < unstick_until) {
        input.buttons |= MoveInput::jump;
    }

    const Vec3 forward_v{-s, 0, -c};
    const Vec3 right_v{c, 0, -s};
    input.forward = int8_t(std::lround(std::clamp(move.dot(forward_v), -1.0, 1.0) * 127.0));
    input.right = int8_t(std::lround(std::clamp(move.dot(right_v), -1.0, 1.0) * 127.0));
    if (!me.crouch_held && !me.rolling()) {
        if (roll && std::abs(input.right) > 40) {
            input.buttons |= MoveInput::crouch;
        } else if (want_crouch != me.crouched && std::abs(input.forward) < 15 && std::abs(input.right) < 15) {
            input.buttons |= MoveInput::crouch;
        }
    }
    if (weapon.rounds_per_clip > 0 && clip.rounds < weapon.rounds_per_clip / 3) {
        input.buttons |= MoveInput::reload;
    }
    return input;
}

std::optional<Vec3> Bot::find_cover(const GameClient& client, const World& world, Vec3 enemy, bool& crouch) {
    const MoveState& me = client.predicted();
    const double step_up = client.character_stats().move.step_up;
    const Battlefield* field = client.config().battlefield ? &*client.config().battlefield : nullptr;
    return find_cover_spot(world, navigation_.get(), field, me.position, enemy, step_up, rng_, crouch);
}

void Bot::choose_powerup(const GameClient& client, double now) {
    const Battlefield& field = *client.config().battlefield;
    const MoveState& me = client.predicted();
    const double max_health = std::max(1.0, client.character_stats().max_health);
    const double health = double(client.self().health) / max_health;
    double ammo = 1;
    if (const WeaponDef* primary = weapon_in_slot(client.loadout(), primary_slot);
        primary != nullptr && primary->ammo > 0 && me.combat.weapons[primary_slot].reserve >= 0) {
        const WeaponState& w = me.combat.weapons[primary_slot];
        ammo = double(w.rounds + w.reserve) / double(primary->ammo * primary->rounds_per_clip);
    }
    const auto pickup_helps = [&](PickupKind kind, bool urgent) {
        const PickupDef& def = pickup_def(kind);
        const double h_need = urgent ? needs_health : 0.8;
        const double a_need = urgent ? needs_ammo : 0.7;
        return (def.health > 0 && health < h_need) || (def.ammo_clips > 0 && ammo < a_need);
    };
    if (pickup_ != 0) {
        const auto& pickups = client.pickups();
        const auto it = std::find_if(pickups.begin(), pickups.end(),
                                     [&](const net::PickupState& p) { return p.id == pickup_; });
        const bool still_useful =
            it != pickups.end() &&
            ((pickup_def(it->kind).health > 0 && health < 0.99) || (pickup_def(it->kind).ammo_clips > 0 && ammo < 0.99));
        if (!still_useful || now >= powerup_until_) {
            pickup_ = 0;
            next_powerup_ = now + 2;
        } else {
            powerup_spot_ = it->position;
        }
        return;
    }
    const auto& match = client.match();
    const auto station_alive = [&](size_t i) {
        return !match || i >= match->powerup_health.size() || match->powerup_health[i] > 0.0f;
    };
    if (powerup_ >= 0) {
        const PowerupStation& station = field.powerups[size_t(powerup_)];
        const bool broken = !station_alive(size_t(powerup_));
        const bool done = broken || ((station.health <= 0 || health >= patched_up) && (station.ammo <= 0 || ammo >= 0.99));
        if (done || now >= powerup_until_) {
            powerup_ = -1;
            next_powerup_ = now + (broken ? 0.5 : 10.0);
        }
        return;
    }
    if (now < next_powerup_) {
        return;
    }
    const bool urgent = health < needs_health || ammo < needs_ammo;
    // Check dropped pickups first (within dropped_reach if urgent, or nearby_pickup opportunistically).
    double best_distance = urgent ? powerup_reach : nearby_pickup;
    uint32_t best_pickup = 0;
    Vec3 best_pickup_pos;
    for (const net::PickupState& p : client.pickups()) {
        const double distance = horizontal_distance(p.position, me.position);
        const double max_range = pickup_helps(p.kind, true) ? dropped_reach
                                 : pickup_helps(p.kind, false) ? nearby_pickup
                                                               : 0.0;
        if (max_range > 0 && distance < std::min(best_distance, max_range) &&
            std::abs(p.position.y - me.position.y) < 10) {
            best_distance = distance;
            best_pickup = p.id;
            best_pickup_pos = p.position;
        }
    }
    // Check health and ammo droids when urgent; whichever is nearer wins.
    int best_station = -1;
    if (urgent) {
        for (size_t i = 0; i < field.powerups.size(); ++i) {
            if (!station_alive(i)) {
                continue;
            }
            const PowerupStation& station = field.powerups[i];
            const bool helps = (health < needs_health && station.health > 0) || (ammo < needs_ammo && station.ammo > 0);
            const double distance = horizontal_distance(station.position, me.position);
            if (helps && distance < best_distance && std::abs(station.position.y - me.position.y) < 15) {
                best_distance = distance;
                best_station = int(i);
                best_pickup = 0;
            }
        }
    }
    next_powerup_ = now + 0.5;
    if (best_pickup != 0) {
        pickup_ = best_pickup;
        powerup_spot_ = best_pickup_pos;
        powerup_until_ = now + 12;
    } else if (best_station >= 0) {
        powerup_ = best_station;
        // Beside it, on the side it comes from: the droid itself is solid.
        const PowerupStation& station = field.powerups[size_t(powerup_)];
        Vec3 away{me.position.x - station.position.x, 0, me.position.z - station.position.z};
        away = away.length() > 1e-3 ? away * (1 / away.length()) : Vec3{1, 0, 0};
        powerup_spot_ = station.position + away * std::min(1.2, station.radius * 0.5);
        powerup_until_ = now + powerup_patience;
    }
}

void Bot::update(GameClient& client, const World& world, double now) {
    GameClient::Controls controls;
    controls.yaw = client.predicted().yaw;
    if (!client.spawned()) {
        client.set_input(controls);
        return;
    }
    const auto& match = client.match();
    const bool in_match = match.has_value() && client.config().battlefield.has_value();
    const net::EntityState& self = client.self();
    if (self.life != Life::alive) {
        // The post it asked for lost while it waited: another. (The server
        // holds a request until its post can be spawned at; one of Mos
        // Eisley's Imperials waited four and a half minutes for CP2.)
        if (requested_ && in_match && spawn_post_ >= 0 && size_t(spawn_post_) < match->posts.size() &&
            self.team != natives_team && match->posts[size_t(spawn_post_)].owner != self.team) {
            requested_ = false;
        }
        if (in_match && self.life == Life::waiting && !requested_ && match->phase != Phase::ended) {
            choose_spawn(client);
        }
        objective_ = -1;
        powerup_ = -1;
        cover_.reset();
        retreat_until_ = 0;
        pickup_ = 0;
        slow_since_ = -1;  // a new life isn't stuck
        unstick_until_ = 0;
        target_ = 0;
        path_.clear();
        vehicle_ = 0;
        stuck_since_ = undriven_since_ = -1;
        client.set_input(controls);
        return;
    }
    requested_ = false;
    if (!in_match && !client.recruiters().empty()) {
        go_to_recruiter(client, now);
        return;
    }

    if (in_match && client.predicted().vehicle >= 0) {
        ride(client, world, now);
        return;
    }
    const MoveState& me = client.predicted();
    const Vec3 eye = me.position + Vec3{0, eye_height_of(me), 0};
    const Loadout& loadout = client.loadout();
    const double gravity = client.character_stats().move.gravity;
    static const WeaponDef unarmed;
    const WeaponDef* held = weapon_in_slot(loadout, me.combat.weapon);
    const WeaponDef& weapon = held != nullptr ? *held : unarmed;

    // Where to go.
    std::optional<Vec3> destination;
    if (in_match) {
        if (objective_ < 0 || now >= next_objective_) {
            choose_objective(client, now);
        }
        // At the height of the post's console where the map gives it: the
        // floor the post is fought over, not a roof nearer its middle height.
        const CommandPost& post = client.config().battlefield->posts[size_t(objective_)];
        destination = post.position + offset_;
        if (post.console) {
            destination->y = post.console->y;
        }
        choose_powerup(client, now);
        if (powerup_ >= 0 || pickup_ != 0) {
            destination = powerup_spot_;
        }
    }

    // Whom to shoot: the nearest enemy in range and in sight.
    const GameClient::Remote* target = nullptr;
    double target_distance = skill_.fire_range;
    double nearest_any = std::numeric_limits<double>::max();
    const auto remotes = client.remotes();
    for (const GameClient::Remote& other : remotes) {
        const bool enemy = !in_match || !client.config().battlefield->allied(self.team, other.team);
        if (!enemy || !other.alive()) {
            continue;
        }
        const double distance = (other.state.position - me.position).length();
        if (!in_match && distance < nearest_any) {
            nearest_any = distance;
            destination = other.state.position;
        }
        const Vec3 chest = other.state.position + Vec3{0, 1.2, 0};
        if (distance < target_distance && !world.raycast(eye, chest)) {
            target_distance = distance;
            target = &other;
        }
    }
    if (target == nullptr) {
        target_ = 0;
    } else if (target->id != target_) {
        target_ = target->id;
        target_since_ = now;
    }
    // Its way blocked by something that can be destroyed: with no one to
    // shoot, it shoots that.
    if (!in_match || target != nullptr || !destination) {
        breach_ = -1;
    } else if (now >= next_breach_) {
        next_breach_ = now + 1;
        breach_ = find_breach(client, world, *destination, eye);
        if (breach_ < 0) {
            // Something its team wants destroyed, in sight and range.
            const auto& objects = client.config().battlefield->destructibles;
            double nearest = skill_.fire_range;
            for (size_t i = 0; i < objects.size() && i < match->destructibles.size(); ++i) {
                const auto& wanted = objects[i].attackers;
                if (match->destructibles[i] <= 0 ||
                    std::find(wanted.begin(), wanted.end(), self.team) == wanted.end()) {
                    continue;
                }
                const double distance = (objects[i].center - eye).length();
                const auto hit = world.raycast_hit(eye, objects[i].center);
                if (distance < nearest && (!hit || hit->destructible == int(i))) {
                    nearest = distance;
                    breach_ = int(i);
                }
            }
        }
    }
    if (breach_ >= 0 && (size_t(breach_) >= match->destructibles.size() || match->destructibles[size_t(breach_)] <= 0)) {
        breach_ = -1;
    }
    bool use = false;
    if (in_match && target == nullptr && powerup_ < 0 && pickup_ == 0) {
        choose_vehicle(client, now);
        for (const net::VehicleState& v : client.vehicles()) {
            if (v.id != vehicle_) continue;
            destination = v.position;
            const auto classes = client.vehicle_classes();
            if (v.vehicle < classes.size() &&
                distance_to_vehicle(me.position + Vec3{0, 1.0, 0}, v.position, v.yaw, classes[v.vehicle]) < 2.0 &&
                now >= next_use_) {
                use_until_ = now + 0.1;  // a press: held a moment, then let go
                next_use_ = now + 0.5;
            }
        }
    } else {
        vehicle_ = 0;
    }

    // In a fight: cover to reload or when hurt; falling back badly hurt (no
    // droid to go to); now and then a roll aside.
    bool want_crouch = false;
    bool roll = false;
    if (in_match) {
        const double max_health = std::max(1.0, client.character_stats().max_health);
        const double health = double(self.health) / max_health;
        const WeaponState& clip = me.combat.weapons[me.combat.weapon];
        const bool reloading = me.combat.reload > 0 || clip.overheated ||
                               (weapon.rounds_per_clip > 0 && clip.rounds == 0);
        // There, it stays a while (from arriving); on the way, it gives up
        // after a few seconds.
        if (cover_ && !cover_reached_ && horizontal_distance(*cover_, me.position) < 0.6) {
            cover_reached_ = true;
            cover_until_ = now + cover_hold_;
        }
        if (cover_ && now >= cover_until_) {
            cover_.reset();
            next_cover_ = now + 4 + random() * 4;
        }
        if (!cover_ && target != nullptr && now >= next_cover_ && powerup_ < 0 && (reloading || health < 0.5)) {
            next_cover_ = now + 2;
            if (random() < skill_.cover) {
                const Vec3 enemy = target->state.position + Vec3{0, eye_height, 0};
                if (auto spot = find_cover(client, world, enemy, cover_crouch_)) {
                    cover_ = spot;
                    cover_reached_ = false;
                    cover_until_ = now + 6;  // to get there
                    cover_hold_ = std::max(2.0, me.combat.reload) + 1.0 + random() * 2;
                }
            }
        }
        if (cover_) {
            destination = *cover_;
            want_crouch = cover_crouch_ && horizontal_distance(*cover_, me.position) < 1.0;
        } else if (powerup_ < 0 && health < 0.3 && target != nullptr && retreat_until_ <= now) {
            retreat_until_ = now + 5 + random() * 3;
        }
        if (!cover_ && powerup_ < 0 && now < retreat_until_) {
            // The nearest post its team holds that's further from the enemy.
            const Vec3 threat = target != nullptr ? target->state.position : me.position;
            const auto& posts = client.config().battlefield->posts;
            double best = std::numeric_limits<double>::max();
            for (size_t i = 0; i < posts.size() && i < match->posts.size(); ++i) {
                if (match->posts[i].owner == self.team &&
                    horizontal_distance(posts[i].position, threat) > horizontal_distance(me.position, threat) &&
                    horizontal_distance(posts[i].position, me.position) < best) {
                    best = horizontal_distance(posts[i].position, me.position);
                    destination = posts[i].position;
                }
            }
        }
        if (target != nullptr && !cover_ && now >= next_roll_) {
            next_roll_ = now + 2.5 + random() * 3;
            roll = random() < skill_.dodge && me.energy > 50 && me.grounded;
        }
    }

    // Movement in the world, then in the frame of where it faces.
    Vec3 move;
    double to_go = 0;
    double route_reach = 0;  // to where it's headed on its route
    if (destination) {
        // Over or under a post (on the hillside over Polis Massa's CP3, in
        // its cave) isn't there yet.
        double off_height = 0;
        if (in_match && powerup_ < 0 && pickup_ == 0 && !cover_) {
            const CommandPost& post = client.config().battlefield->posts[size_t(objective_)];
            off_height = std::max({0.0, post.low - 0.5 - me.position.y, me.position.y - post.high});
        }
        to_go = horizontal_distance(*destination, me.position) + off_height;
        const double stop = pickup_ != 0 || cover_ ? 0.4 : 1.5;  // right onto a pickup, right behind cover
        if (to_go > stop) {
            const Vec3 toward = steer(me.position, *destination, now);
            const Vec3 d{toward.x - me.position.x, 0, toward.z - me.position.z};
            route_reach = d.length();
            if (d.length() > 1e-3) {
                move = d * (pace_ / d.length());
            }
        }
    }
    const Vec3 route_move = move;
    if (now >= next_strafe_) {
        strafe_ = random() < 0.5 ? -1 : 1;
        next_strafe_ = now + 0.8 + random() * 1.5;
    }

    if (target != nullptr) {
        float aim_yaw = float(controls.yaw);
        float aim_pitch = float(controls.pitch);
        const bool has_arc = aim_at_target(eye, target->state.position, target->state.velocity, weapon, gravity,
                                           skill_.aim_error, now, aim_phase_, aim_yaw, aim_pitch);
        controls.yaw = aim_yaw;
        controls.pitch = aim_pitch;
        controls.fire = has_arc && now - target_since_ >= skill_.reaction;
        // Now and then a grenade at someone close.
        const WeaponDef* thrown = weapon_in_slot(loadout, secondary_slot);
        if (thrown != nullptr && me.combat.weapons[secondary_slot].rounds > 0 && target_distance > 8 &&
            target_distance < 25 && now >= next_grenade_ && now - target_since_ >= skill_.reaction) {
            const Vec3 feet = target->state.position - eye;
            if (const auto lob = ballistic_pitch(thrown->bolt_speed, thrown->bolt_gravity * gravity,
                                                 std::hypot(feet.x, feet.z), feet.y)) {
                throw_until_ = now + 0.15;
                next_grenade_ = now + 6 + random() * 8;
                grenade_pitch_ = *lob - thrown->shot_elevate;
            }
        }
        if (now < throw_until_) {
            controls.secondary = true;
            controls.fire = false;
            controls.pitch = grenade_pitch_;
        }
        // Keep moving toward the objective while sidestepping.
        const double s = std::sin(controls.yaw), c = std::cos(controls.yaw);
        move = move * 0.6 + Vec3{c, 0, -s} * (strafe_ * 0.7);
    } else if (breach_ >= 0) {
        // At its middle, on the projectile's arc.
        const Vec3 d = client.config().battlefield->destructibles[size_t(breach_)].center - eye;
        const auto arc = ballistic_pitch(weapon.bolt_speed, weapon.bolt_gravity * gravity, std::hypot(d.x, d.z), d.y);
        controls.yaw = yaw_toward(d);
        controls.pitch = arc.value_or(0) - weapon.shot_elevate;
        controls.fire = arc.has_value();
    } else {
        if (move.length() > 0) {
            controls.yaw = yaw_toward(move);
        }
        controls.sprint = to_go > 25 && pace_ >= 1;
        const WeaponState& clip = me.combat.weapons[me.combat.weapon];
        controls.reload = weapon.rounds_per_clip > 0 && clip.rounds < weapon.rounds_per_clip / 2;
    }

    // Not over a deadly edge: not with a sidestep in a fight (off the Death
    // Star's catwalks and bridges). Along its route alone, then, the other
    // way next sidestep. Its route cutting over one too (pushed off it, from
    // beside a ramp down, the straight way to its foot cuts over the
    // reactor's pit): standing, and a new route from there.
    // Looking further on at speed: sprinting, it slides on meters stopping
    // (down a ramp into the Death Star's thin death region by CP5).
    const double step_up = client.character_stats().move.step_up;
    const Battlefield* field = in_match ? &*client.config().battlefield : nullptr;
    const double speed = std::hypot(me.velocity.x, me.velocity.z);
    const double stopping = 1.5 + 0.2 * speed;
    const NavGrid* nav = in_match ? navigation_.get() : nullptr;
    if (over_edge(world, field, nav, me.position, move, step_up, stopping)) {
        strafe_ = -strafe_;
        move = over_edge(world, field, nav, me.position, route_move, step_up, std::min(stopping, route_reach + 0.3))
                   ? Vec3{}
                   : route_move;
        if (move.length() == 0) {
            replan_at_ = now;
        }
    }

    // Stuck against something: jump and sidestep for a moment; not over a
    // deadly edge, nor up beside one (straight on, then, if that's clear).
    const bool wants_to_move = move.length() > 0.1;
    if (wants_to_move && speed < 1.0 && me.grounded) {
        if (slow_since_ < 0) {
            slow_since_ = now;
        } else if (now - slow_since_ > 1.0) {
            unstick_until_ = now + 0.8;
            slow_since_ = -1;
            replan_at_ = now;  // the route may be wrong
        }
    } else {
        slow_since_ = -1;
    }
    if (now < unstick_until_) {
        const double s = std::sin(controls.yaw), c = std::cos(controls.yaw);
        const Vec3 aside = move + Vec3{c, 0, -s} * strafe_;
        constexpr double leap = 3;  // m a jump carries it
        if (!over_edge(world, field, nav, me.position, move, step_up, leap)) {
            controls.jump = true;
            if (!over_edge(world, field, nav, me.position, aside, step_up, leap)) {
                move = aside;
            }
        }
    }

    const double s = std::sin(controls.yaw), c = std::cos(controls.yaw);
    const Vec3 forward{-s, 0, -c};
    const Vec3 right{c, 0, -s};
    controls.forward = std::clamp(move.dot(forward), -1.0, 1.0);
    controls.right = std::clamp(move.dot(right), -1.0, 1.0);
    // A press crouches or stands still, rolls on the move: a roll aside in
    // a fight (strafing), else crouching behind low cover once there, and
    // standing up out of it.
    if (!me.crouch_held && !me.rolling()) {
        if (roll && std::abs(controls.right) > 0.3) {
            controls.crouch = true;
        } else if (want_crouch != me.crouched && std::hypot(controls.forward, controls.right) < 0.1) {
            controls.crouch = true;
        } else if (me.crouched && !want_crouch && !cover_) {
            controls.crouch = !(std::hypot(controls.forward, controls.right) > 0.1);  // stand before moving off
            if (controls.crouch) {
                controls.forward = controls.right = 0;
            }
        }
    }
    controls.weapon = 0;
    controls.use = use || now < use_until_;
    client.set_input(controls);
}

void Bot::choose_vehicle(const GameClient& client, double now) {
    constexpr double look_reach = 40;  // m: a vehicle further off isn't worth the walk (ASSUMPTION)
    const auto classes = client.vehicle_classes();
    const auto& field = *client.config().battlefield;
    const int team = client.self().team;
    if (team == natives_team) {
        return;  // Ewoks don't ride speeder bikes
    }
    // Still there, and still free for it.
    const auto free_seat = [&](const net::VehicleState& v) {
        if (!v.alive || v.vehicle >= classes.size()) return false;
        // Nothing to fight with (a turret whose guns aren't read yet): no use.
        const VehicleClass& cls = classes[v.vehicle];
        const bool armed = weapon_in_slot(cls.loadout, 0) != nullptr ||
                           std::any_of(cls.seats.begin(), cls.seats.end(), [](const VehicleSeat& s) {
                               return weapon_in_slot(s.loadout, 0) != nullptr;
                           });
        if (cls.kind == VehicleKind::turret && (!armed || field.is_space())) return false;
        if (cls.kind == VehicleKind::flyer && !field.is_space()) return false;
        const bool aboard = v.driver != 0 || std::any_of(v.gunners.begin(), v.gunners.end(),
                                                         [](uint32_t g) { return g != 0; });
        if (aboard && !field.allied(v.team, team)) return false;
        return v.driver == 0 || std::find(v.gunners.begin(), v.gunners.end(), 0u) != v.gunners.end();
    };
    if (vehicle_ != 0) {
        const auto it = std::find_if(client.vehicles().begin(), client.vehicles().end(),
                                     [&](const net::VehicleState& v) { return v.id == vehicle_; });
        if (it == client.vehicles().end() || !free_seat(*it) || now >= vehicle_until_) {
            vehicle_ = 0;
            next_vehicle_ = now + 5 + random() * 5;
        }
        return;
    }
    if (now < next_vehicle_) {
        return;
    }
    next_vehicle_ = now + 2 + random() * 2;
    double best = look_reach;
    for (const net::VehicleState& v : client.vehicles()) {
        if (std::abs(v.position.y - client.predicted().position.y) > 12.0) continue;
        const double d = horizontal_distance(v.position, client.predicted().position);
        if (free_seat(v) && d < best) {
            best = d;
            vehicle_ = v.id;
        }
    }
    if (vehicle_ != 0) {
        vehicle_until_ = now + 4 + best / 3;  // walking there, and a little more
    }
}

void Bot::ride(GameClient& client, const World& world, double now) {
    GameClient::Controls controls;
    const MoveState& me = client.predicted();
    controls.yaw = me.yaw;
    vehicle_ = 0;  // in
    const auto classes = client.vehicle_classes();
    const auto& field = *client.config().battlefield;
    const auto& match = *client.match();
    const net::EntityState& self = client.self();
    const VehicleClass& cls = classes[size_t(me.vehicle)];
    const VehicleSeat* seat = vehicle_seat(cls, me.seat);
    const net::VehicleState* riding = nullptr;
    for (const net::VehicleState& v : client.vehicles()) {
        if (v.driver == self.id || std::find(v.gunners.begin(), v.gunners.end(), self.id) != v.gunners.end()) {
            riding = &v;
        }
    }
    const Vec3 at = riding != nullptr && seat != nullptr ? riding->position : me.position;
    const double heading = riding != nullptr && seat != nullptr ? riding->yaw : me.yaw;
    MoveState frame = me;
    frame.position = at;
    frame.yaw = float(heading);
    const Vec3 eye = seat != nullptr ? seat_muzzle(frame, *seat) : vehicle_muzzle(frame, cls);
    const Loadout& loadout = seat != nullptr ? seat->loadout : cls.loadout;
    static const WeaponDef unarmed;
    const WeaponDef* held = weapon_in_slot(loadout, me.combat.weapon);
    const WeaponDef& weapon = held != nullptr ? *held : unarmed;
    const double gravity = client.character_stats().move.gravity;
    bool leave = false;

    // Whom to shoot: the nearest enemy in range and in sight (drivers too:
    // their vehicles take the hits).
    const GameClient::Remote* target = nullptr;
    double target_distance = std::max(skill_.fire_range, weapon.bolt_speed * weapon.bolt_life);
    for (const GameClient::Remote& other : client.remotes()) {
        if (field.allied(self.team, other.team) || !other.alive()) continue;
        const double distance = (other.state.position - at).length();
        if (distance < target_distance && !world.raycast(eye, other.state.position + Vec3{0, 1.2, 0})) {
            target_distance = distance;
            target = &other;
        }
    }
    if (target == nullptr) {
        target_ = 0;
    } else if (target->id != target_) {
        target_ = target->id;
        target_since_ = now;
    }
    float aim_yaw = float(heading), aim_pitch = 0;
    bool in_arc = false;
    if (target != nullptr) {
        in_arc = aim_at_target(eye, target->state.position, target->state.velocity, weapon, gravity,
                               skill_.aim_error, now, aim_phase_, aim_yaw, aim_pitch);
    }

    if (seat != nullptr) {
        // A gunner: its look is the guns'; it fires once they bear.
        controls.yaw = target != nullptr ? aim_yaw : float(heading);
        controls.pitch = aim_pitch;
        if (target != nullptr) {
            const double off = wrap_angle(double(aim_yaw) - heading);
            controls.fire = in_arc && now - target_since_ >= skill_.reaction && off >= seat->yaw_min &&
                            off <= seat->yaw_max;
        }
        // No one driving for a while: out, to find something better.
        const bool driven = riding != nullptr && riding->driver != 0;
        undriven_since_ = driven ? -1 : (undriven_since_ < 0 ? now : undriven_since_);
        leave = undriven_since_ >= 0 && now - undriven_since_ > 8;
    } else if (cls.kind == VehicleKind::turret) {
        // Manning a turret: it turns on the nearest enemy and fires once its
        // guns bear; nothing to shoot for a while, it goes back to the fight.
        controls.yaw = target != nullptr ? aim_yaw : me.yaw;
        controls.pitch = aim_pitch;
        if (target != nullptr) {
            const double off = wrap_angle(double(aim_yaw) - double(me.yaw));
            controls.fire = in_arc && now - target_since_ >= skill_.reaction && off >= cls.yaw_min - 0.05 &&
                            off <= cls.yaw_max + 0.05;
        }
        undriven_since_ = target != nullptr ? -1 : (undriven_since_ < 0 ? now : undriven_since_);
        leave = undriven_since_ >= 0 && now - undriven_since_ > 20;
    } else if (cls.kind == VehicleKind::flyer) {
        // Space flyer AI: fly straight out the hangar bay, then dogfight enemy
        // flyers or strafe unshielded enemy critical ship systems & frigates.
        if (me.grounded || field.in_landing_region(me.position)) {
            controls.forward = 1;
            controls.yaw = me.yaw;
            controls.pitch = me.grounded ? 0.12f : 0.05f;
            controls.sprint = !me.grounded && me.energy > 50.0;
            client.set_input(controls);
            return;
        }
        std::optional<Vec3> goal;
        bool want_torpedo = false;
        double best_score = 1e30;
        for (const auto& other : client.remotes()) {
            if (!other.alive() || field.allied(self.team, other.team) ||
                other.state.vehicle < 0 || size_t(other.state.vehicle) >= classes.size() ||
                classes[size_t(other.state.vehicle)].kind != VehicleKind::flyer) {
                continue;
            }
            const double d = (other.state.position - me.position).length();
            if (d < best_score) {
                best_score = d;
                goal = other.state.position;
                want_torpedo = false;
            }
        }
        if (!goal) {
            const auto is_shielded = [&](int idx) {
                for (const ShieldLink& s : field.shield_links) {
                    if (s.controller >= 0 && size_t(s.controller) < match.destructibles.size() &&
                        match.destructibles[size_t(s.controller)] > 0 &&
                        std::find(s.targets.begin(), s.targets.end(), idx) != s.targets.end()) {
                        return true;
                    }
                }
                return false;
            };
            double sys_best = 1e30;
            for (const SpaceSystem& sys : field.space_systems) {
                if (sys.owner_team == self.team || sys.destructible < 0 ||
                    size_t(sys.destructible) >= field.destructibles.size() ||
                    size_t(sys.destructible) >= match.destructibles.size() ||
                    match.destructibles[size_t(sys.destructible)] <= 0 || is_shielded(sys.destructible)) {
                    continue;
                }
                const Vec3 sys_at = field.destructibles[size_t(sys.destructible)].center;
                if (field.in_landing_region(sys_at)) continue;
                const double d = (sys_at - me.position).length();
                if (d < sys_best) {
                    sys_best = d;
                    goal = sys_at;
                    want_torpedo = true;
                }
            }
        }
        if (!goal) {
            goal = field.center() + Vec3{0, 120, 0};
        }
        Vec3 aim_target = *goal;
        const Vec3 fwd = aim_direction(me.yaw, me.pitch);
        const auto ahead = world.raycast_hit(eye, eye + fwd * 85.0);
        if (ahead && ahead->t < 0.75) {
            aim_target = me.position + (ahead->normal + Vec3{0, 0.5, 0}) * 150.0;
        }
        const Vec3 d = aim_target - eye;
        const double horiz = std::hypot(d.x, d.z);
        controls.forward = 1;
        controls.yaw = float(std::atan2(-d.x, -d.z));
        controls.pitch = float(std::clamp(std::atan2(d.y, std::max(1.0, horiz)), -1.1, 1.1));
        const double dist = d.length();
        const double dot = dist > 1e-6 ? (fwd.x * d.x + fwd.y * d.y + fwd.z * d.z) / dist : 0;
        controls.fire = dist < 550.0 && dot > 0.94 && !ahead;
        if (want_torpedo && weapon_in_slot(loadout, 1) != nullptr) {
            controls.weapon = 1;
        }
        controls.sprint = dist > 400.0 && me.energy > 40.0;
        client.set_input(controls);
        return;
    } else {
        // Driving: toward its objective along its route; turned on an enemy
        // in range (its guns turn only so far from its heading), firing once
        // they bear.
        if (objective_ < 0 || now >= next_objective_) {
            choose_objective(client, now);
        }
        const CommandPost& post = field.posts[size_t(objective_)];
        const Vec3 destination = post.position + offset_;
        const double to_go = horizontal_distance(destination, me.position);
        Vec3 move;
        if (to_go > 4) {
            const Vec3 toward = steer(me.position, destination, now);
            const Vec3 d{toward.x - me.position.x, 0, toward.z - me.position.z};
            if (d.length() > 1e-3) move = d * (1 / d.length());
        }
        controls.yaw = move.length() > 0 ? yaw_toward(move) : me.yaw;
        // Easing off nearing it (a speeder bike can't stop short at full tilt):
        // there, it holds.
        controls.forward = move.length() > 0 ? std::clamp((to_go - 4) / 20, 0.25, 1.0) : 0.0;
        if (std::hypot(me.velocity.x, me.velocity.z) > 2 * to_go) {
            controls.forward = 0;  // coasting the rest
        }
        if (target != nullptr) {
            controls.yaw = aim_yaw;
            controls.pitch = aim_pitch;
            const double off = wrap_angle(double(aim_yaw) - double(me.yaw));
            const bool bears = off >= cls.yaw_min - 0.05 && off <= cls.yaw_max + 0.05;
            controls.fire = in_arc && bears && now - target_since_ >= skill_.reaction;
            // Closing on a far one; holding off a near one.
            controls.forward = target_distance > skill_.fire_range * 0.6 ? 0.6 : 0.0;
            // Its second gun (a tank's missiles) at whatever's in a vehicle.
            controls.weapon = target->state.vehicle >= 0 && weapon_in_slot(loadout, 1) != nullptr ? 1 : 0;
        }
        controls.sprint = target == nullptr && to_go > 40 && me.energy > 30;
        // Getting nowhere: back off, turned aside; again and again, out.
        const double speed = std::hypot(me.velocity.x, me.velocity.z);
        if (controls.forward > 0.5 && speed < 1.0 && now >= reverse_until_) {
            if (stuck_since_ < 0) {
                stuck_since_ = now;
            } else if (now - stuck_since_ > 1.5) {
                reverse_until_ = now + 1.2;
                stuck_since_ = -1;
                replan_at_ = now;
                ++stuck_count_;
            }
        } else if (speed > 3) {
            stuck_since_ = -1;
            stuck_count_ = 0;
        }
        if (now < reverse_until_) {
            controls.forward = -1;
            controls.yaw = float(me.yaw + (strafe_ > 0 ? 0.8 : -0.8));
            controls.fire = false;
        }
        leave = stuck_count_ >= 4;
        if (riding != nullptr && riding->max_health > 0 && riding->health < 0.12 * riding->max_health) {
            leave = true;  // bailing out before it goes up
        }
        (void)match;
    }
    if (leave) {
        controls.forward = 0;
        controls.fire = false;
        stuck_count_ = 0;
        next_vehicle_ = now + 10;
    }
    if (leave && now >= next_use_) {
        use_until_ = now + 0.1;
        next_use_ = now + 0.5;
    }
    controls.use = now < use_until_;
    client.set_input(controls);
}

int Bot::find_breach(const GameClient& client, const World& world, Vec3 destination, Vec3 eye) const {
    constexpr double short_by = 4;  // m its route ends from where it's going
    constexpr double near_end = 6;  // m from that end the thing in the way is
    const auto& match = client.match();
    if (path_.empty() || !match || horizontal_distance(path_.back(), destination) < short_by) {
        return -1;
    }
    const auto& objects = client.config().battlefield->destructibles;
    int best = -1;
    double nearest = near_end;
    for (size_t i = 0; i < objects.size() && i < match->destructibles.size(); ++i) {
        if (match->destructibles[i] <= 0) {
            continue;
        }
        const double off = horizontal_distance(objects[i].nearest(path_.back()), path_.back());
        if (off >= nearest || (objects[i].center - eye).length() > skill_.fire_range) {
            continue;
        }
        const auto hit = world.raycast_hit(eye, objects[i].center);
        if (!hit || hit->destructible == int(i)) {
            nearest = off;
            best = int(i);
        }
    }
    return best;
}

BotPlayers::BotPlayers(int count, const World& world, const ClientConfig& base, LinkFactory open_link,
                       BotSkill skill)
    : world_(world) {
    if (base.battlefield) {
        navigation_ = std::make_shared<NavGrid>(world, *base.battlefield);
        destructibles_ = base.battlefield->destructibles;
        destroyed_.assign(destructibles_.size(), 0);
    }
    for (int i = 0; i < count; ++i) {
        ClientConfig config = base;
        config.name = std::format("Bot {}", i + 1);
        auto link = open_link ? open_link() : std::make_unique<net::UdpLink>();
        players_.push_back(std::make_unique<Player>(std::move(link), world, std::move(config), uint64_t(i + 1)));
        players_.back()->bot.set_navigation(navigation_);
        players_.back()->bot.set_skill(skill);
    }
    // The battlefield's natives, on their own team.
    const int natives = base.battlefield && base.battlefield->natives ? base.battlefield->natives->count : 0;
    for (int i = 0; i < natives; ++i) {
        ClientConfig config = base;
        config.name = std::format("{} {}", base.battlefield->natives->team.name, i + 1);
        config.team = natives_team;
        auto link = open_link ? open_link() : std::make_unique<net::UdpLink>();
        players_.push_back(
            std::make_unique<Player>(std::move(link), world, std::move(config), uint64_t(count + i + 1)));
        players_.back()->bot.set_navigation(navigation_);
        players_.back()->bot.set_skill(skill);
    }
}

void BotPlayers::set_skill(BotSkill skill) {
    for (auto& player : players_) {
        player->bot.set_skill(skill);
    }
}

void BotPlayers::update(double now) {
    // Routes past what's been destroyed (or stands again), worked out anew:
    // all of them, since what its destruction moves (a gate, a bridge) may
    // be far from it.
    for (size_t i = 0; i < destructibles_.size() && navigation_; ++i) {
        const uint8_t gone = world_.destroyed(i) ? 1 : 0;
        if (gone != destroyed_[i]) {
            destroyed_[i] = gone;
            navigation_->forget(-1e9, 1e9, -1e9, 1e9);
        }
    }
    for (auto& player : players_) {
        player->bot.update(player->client, world_, now);
        player->client.update(now);
    }
}

void BotPlayers::disconnect() {
    for (auto& player : players_) {
        player->client.disconnect();
    }
}

} // namespace swm::core
