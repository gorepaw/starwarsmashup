#include "swm/core/bot.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

#include "swm/core/nav.h"
#include "swm/core/world.h"

namespace swm::core {

namespace {

double horizontal_distance(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

// The yaw that faces along a direction (see aim_direction).
double yaw_toward(Vec3 d) {
    return std::atan2(-d.x, -d.z);
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
    const auto& loadouts = field.teams[size_t(team)].loadouts;
    // Mostly the first loadout, sometimes another.
    const size_t loadout = random() < 0.5 ? 0 : size_t(random() * double(loadouts.size())) % loadouts.size();
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
        const bool ours = post.owner == team && post.charge >= 1 && !post.contested;
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
            match->posts[size_t(spawn_post_)].owner != self.team) {
            requested_ = false;
        }
        if (in_match && self.life == Life::waiting && !requested_ && match->phase != Phase::ended) {
            choose_spawn(client);
        }
        objective_ = -1;
        powerup_ = -1;
        pickup_ = 0;
        slow_since_ = -1;  // a new life isn't stuck
        unstick_until_ = 0;
        target_ = 0;
        path_.clear();
        client.set_input(controls);
        return;
    }
    requested_ = false;
    if (!in_match && !client.recruiters().empty()) {
        go_to_recruiter(client, now);
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
        const bool enemy = !in_match || other.team != self.team;
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

    // Movement in the world, then in the frame of where it faces.
    Vec3 move;
    double to_go = 0;
    double route_reach = 0;  // to where it's headed on its route
    if (destination) {
        // Over or under a post (on the hillside over Polis Massa's CP3, in
        // its cave) isn't there yet.
        double off_height = 0;
        if (in_match && powerup_ < 0 && pickup_ == 0) {
            const CommandPost& post = client.config().battlefield->posts[size_t(objective_)];
            off_height = std::max({0.0, post.low - 0.5 - me.position.y, me.position.y - post.high});
        }
        to_go = horizontal_distance(*destination, me.position) + off_height;
        const double stop = pickup_ != 0 ? 0.4 : 1.5;
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
        // Lead the target by the flight time, on the projectile's arc; aim
        // wanders a little. Grenades and shells go at the feet.
        const bool lobbed = weapon.projectile == Projectile::grenade;
        const Vec3 point = target->state.position + Vec3{0, lobbed ? 0.2 : 1.2, 0};
        const double flight = target_distance / std::max(1.0, weapon.bolt_speed);
        const Vec3 d = point + target->state.velocity * flight - eye;
        const double wobble = skill_.aim_error * std::numbers::pi / 180;
        const auto arc = ballistic_pitch(weapon.bolt_speed, weapon.bolt_gravity * gravity,
                                         std::hypot(d.x, d.z), d.y);
        controls.yaw = yaw_toward(d) + wobble * std::sin(now * 2.3 + aim_phase_);
        controls.pitch = arc.value_or(0) - weapon.shot_elevate + wobble * std::sin(now * 3.1 + aim_phase_ * 2);
        controls.fire = arc.has_value() && now - target_since_ >= skill_.reaction;
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
    controls.weapon = 0;
    client.set_input(controls);
}

BotPlayers::BotPlayers(int count, const World& world, const ClientConfig& base, LinkFactory open_link)
    : world_(world) {
    if (base.battlefield) {
        navigation_ = std::make_shared<NavGrid>(world, *base.battlefield);
    }
    for (int i = 0; i < count; ++i) {
        ClientConfig config = base;
        config.name = std::format("Bot {}", i + 1);
        auto link = open_link ? open_link() : std::make_unique<net::UdpLink>();
        players_.push_back(std::make_unique<Player>(std::move(link), world, std::move(config), uint64_t(i + 1)));
        players_.back()->bot.set_navigation(navigation_);
    }
}

void BotPlayers::update(double now) {
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
