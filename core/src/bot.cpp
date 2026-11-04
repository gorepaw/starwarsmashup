#include "swm/core/bot.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

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
    const auto& classes = field.teams[size_t(team)].classes;
    // Mostly the first class (the rifleman), sometimes another.
    const size_t soldier_class = random() < 0.5 ? 0 : size_t(random() * double(classes.size())) % classes.size();
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
        client.request_spawn(int(soldier_class), best);
        requested_ = true;
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
    if (best != objective_) {
        objective_ = best;
        const double angle = random() * 2 * std::numbers::pi;
        offset_ = Vec3{std::cos(angle), 0, std::sin(angle)} * (random() * field.posts[size_t(best)].radius * 0.6);
    }
    next_objective_ = now + 4 + random() * 4;
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
        if (in_match && self.life == Life::waiting && !requested_ && match->phase != Phase::ended) {
            choose_spawn(client);
        }
        objective_ = -1;
        target_ = 0;
        client.set_input(controls);
        return;
    }
    requested_ = false;
    if (!in_match && !client.recruiters().empty()) {
        go_to_recruiter(client, now);
        return;
    }

    const MoveState& me = client.predicted();
    const Vec3 eye = me.position + Vec3{0, eye_height, 0};
    const SoldierDef& soldier = client.soldier();
    const WeaponDef& weapon = soldier.weapons[std::min<size_t>(me.combat.weapon, soldier.weapons.size() - 1)];

    // Where to go.
    std::optional<Vec3> destination;
    if (in_match) {
        if (objective_ < 0 || now >= next_objective_) {
            choose_objective(client, now);
        }
        destination = client.config().battlefield->posts[size_t(objective_)].position + offset_;
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
    if (destination) {
        const Vec3 d{destination->x - me.position.x, 0, destination->z - me.position.z};
        to_go = d.length();
        if (to_go > 1.5) {
            move = d * (1 / to_go);
        }
    }
    if (now >= next_strafe_) {
        strafe_ = random() < 0.5 ? -1 : 1;
        next_strafe_ = now + 0.8 + random() * 1.5;
    }

    if (target != nullptr) {
        // Lead the target by the bolt's flight time; aim wanders a little.
        const Vec3 chest = target->state.position + Vec3{0, 1.2, 0};
        const double flight = target_distance / std::max(1.0, weapon.bolt_speed);
        const Vec3 aim_at = chest + target->state.velocity * flight;
        const Vec3 d = aim_at - eye;
        const double wobble = skill_.aim_error * std::numbers::pi / 180;
        controls.yaw = yaw_toward(d) + wobble * std::sin(now * 2.3 + aim_phase_);
        controls.pitch = std::atan2(d.y, std::hypot(d.x, d.z)) + wobble * std::sin(now * 3.1 + aim_phase_ * 2);
        controls.fire = now - target_since_ >= skill_.reaction;
        // Keep moving toward the objective while sidestepping.
        const double s = std::sin(controls.yaw), c = std::cos(controls.yaw);
        move = move * 0.6 + Vec3{c, 0, -s} * (strafe_ * 0.7);
    } else {
        if (move.length() > 0) {
            controls.yaw = yaw_toward(move);
        }
        controls.sprint = to_go > 25;
        const WeaponState& clip = me.combat.weapons[me.combat.weapon];
        controls.reload = weapon.rounds_per_clip > 0 && clip.rounds < weapon.rounds_per_clip / 2;
    }

    // Stuck against something: jump and sidestep for a moment.
    const double speed = std::hypot(me.velocity.x, me.velocity.z);
    const bool wants_to_move = move.length() > 0.1;
    if (wants_to_move && speed < 1.0 && me.grounded) {
        if (slow_since_ < 0) {
            slow_since_ = now;
        } else if (now - slow_since_ > 1.0) {
            unstick_until_ = now + 0.8;
            slow_since_ = -1;
        }
    } else {
        slow_since_ = -1;
    }
    if (now < unstick_until_) {
        controls.jump = true;
        const double s = std::sin(controls.yaw), c = std::cos(controls.yaw);
        move = move + Vec3{c, 0, -s} * strafe_;
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
    for (int i = 0; i < count; ++i) {
        ClientConfig config = base;
        config.name = std::format("Bot {}", i + 1);
        auto link = open_link ? open_link() : std::make_unique<net::UdpLink>();
        players_.push_back(std::make_unique<Player>(std::move(link), world, std::move(config), uint64_t(i + 1)));
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
