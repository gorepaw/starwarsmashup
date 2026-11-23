// GameServer's vehicles in a match: given at their spawns to whoever holds
// each spawn's post, got into and out of with the use button, driven by
// their drivers' inputs (simulate_character), damaged and destroyed.
#include <algorithm>
#include <cmath>
#include <format>

#include "swm/core/server.h"

namespace swm::core {
namespace {

constexpr double enter_reach = 2.5;   // m from a vehicle's body to get in
constexpr double home_reach = 15;     // m from its spawn: still at home (never expires there)
constexpr double wreck_time = 3;      // s a destroyed vehicle stays listed, for drawing its end

} // namespace

std::span<const VehicleClass> GameServer::vehicle_classes() const {
    if (!config_.battlefield) {
        return {};
    }
    return config_.battlefield->vehicles;
}

std::vector<net::VehicleState> GameServer::vehicles() const {
    std::vector<net::VehicleState> out;
    for (const Vehicle& v : vehicles_) {
        out.push_back(describe(v));
    }
    return out;
}

net::VehicleState GameServer::describe(const Vehicle& v) const {
    net::VehicleState s;
    s.id = v.id;
    s.vehicle = uint8_t(v.cls);
    s.position = v.state.position;
    s.velocity = v.state.velocity;
    s.yaw = v.state.yaw;
    s.pitch = v.state.pitch;
    s.health = float(v.health);
    s.max_health = float(vehicle_classes()[size_t(v.cls)].max_health);
    s.driver = v.driver;
    s.team = int8_t(v.team);
    s.alive = v.dead_for < 0;
    return s;
}

GameServer::Vehicle* GameServer::vehicle_of(uint32_t driver) {
    for (Vehicle& v : vehicles_) {
        if (v.driver == driver && v.dead_for < 0) return &v;
    }
    return nullptr;
}

void GameServer::update_vehicles(double dt) {
    const auto classes = vehicle_classes();
    if (!conquest_ || classes.empty()) {
        return;
    }
    const Battlefield& field = conquest_->battlefield();
    const MatchState& match = conquest_->state();
    // A new match: none left over.
    if (vehicles_phase_ == Phase::ended && match.phase == Phase::warmup) {
        for (Client& client : clients_) {
            if (client.state.vehicle >= 0) leave_vehicle(client);
        }
        vehicles_.clear();
        spawned_.clear();
    }
    vehicles_phase_ = match.phase;
    spawn_wait_.resize(field.vehicle_spawns.size(), 0);
    spawned_.resize(field.vehicle_spawns.size(), 0);

    for (Vehicle& v : vehicles_) {
        if (v.dead_for >= 0) {
            v.dead_for += dt;
            continue;
        }
        // Driven: where its driver is. A driver who's gone or out leaves it empty.
        if (v.driver != 0) {
            const Client* driver = find(v.driver);
            if (driver == nullptr || driver->state.vehicle != v.cls || driver->life != Life::alive) {
                v.driver = 0;
                if (driver != nullptr) v.state = driver->state;
                v.state.vehicle = int8_t(v.cls);
            } else {
                const CombatState combat = v.state.combat;
                v.state = driver->state;
                v.state.combat = combat;  // the driver's state has them while driving
                v.away = 0;
                continue;
            }
        }
        // Empty: it coasts to a stop and settles.
        MoveInput idle;
        idle.yaw = v.state.yaw;
        simulate_vehicle(v.state, idle, world_, classes[size_t(v.cls)], dt);
        // Left away from home, it expires.
        if (v.spawn >= 0) {
            const VehicleSpawn& spawn = field.vehicle_spawns[size_t(v.spawn)];
            const Vec3 d = v.state.position - spawn.position;
            v.away = std::hypot(d.x, d.z) > home_reach ? v.away + dt : 0;
            if (v.away > spawn.expire_time) {
                v.dead_for = wreck_time;  // gone, not blown up
                spawn_wait_[size_t(v.spawn)] = spawn.respawn_time;
            }
        }
    }
    std::erase_if(vehicles_, [&](const Vehicle& v) {
        const bool gone = v.dead_for >= wreck_time;
        if (gone && v.spawn >= 0 && spawned_[size_t(v.spawn)] == v.id) spawned_[size_t(v.spawn)] = 0;
        return gone;
    });

    // Spawns: a vehicle for whoever holds the post, once the wait is over;
    // an unused one at home goes over to a new holder.
    for (size_t i = 0; i < field.vehicle_spawns.size(); ++i) {
        const VehicleSpawn& spawn = field.vehicle_spawns[i];
        spawn_wait_[i] = std::max(0.0, spawn_wait_[i] - dt);
        const int holder = spawn.post < 0 ? 0
                           : size_t(spawn.post) < match.posts.size() ? int(match.posts[size_t(spawn.post)].owner)
                                                                     : -1;
        const int wanted = holder >= 0 && holder < 2 ? spawn.vehicle[size_t(holder)] : -1;
        auto current = std::find_if(vehicles_.begin(), vehicles_.end(),
                                    [&](const Vehicle& v) { return spawned_[i] != 0 && v.id == spawned_[i]; });
        if (current != vehicles_.end()) {
            const bool idle_at_home = current->dead_for < 0 && current->driver == 0 && current->away == 0 &&
                                      current->health >= classes[size_t(current->cls)].max_health;
            if (!(idle_at_home && holder >= 0 && holder != current->team)) {
                continue;
            }
            vehicles_.erase(current);  // taken over: the new holder's comes at once
            spawned_[i] = 0;
        }
        if (wanted < 0 || spawn_wait_[i] > 0 || match.phase == Phase::ended) {
            continue;
        }
        const VehicleClass& cls = classes[size_t(wanted)];
        Vehicle v;
        v.id = next_entity_++;
        v.cls = wanted;
        v.spawn = int(i);
        v.team = holder;
        v.health = cls.max_health;
        v.state.position = spawn.position;
        v.state.position.y = world_.ground_at(spawn.position.x, spawn.position.z, spawn.position.y + 2, 3).height +
                             (cls.kind == VehicleKind::hover ? cls.altitude : 0);
        v.state.yaw = float(spawn.yaw);
        v.state.vehicle = int8_t(wanted);
        v.state.grounded = true;
        reset_loadout(v.state, cls.loadout, CharacterStats{});
        spawned_[i] = v.id;
        vehicles_.push_back(v);
    }
}

// The use button: out of the vehicle driven, or into the nearest empty one
// within reach.
void GameServer::use_vehicle(Client& client) {
    const auto classes = vehicle_classes();
    if (client.state.vehicle >= 0) {
        leave_vehicle(client);
        return;
    }
    Vehicle* best = nullptr;
    double best_distance = enter_reach;
    const Vec3 chest = client.state.position + Vec3{0, 1.0, 0};
    for (Vehicle& v : vehicles_) {
        if (v.dead_for >= 0 || v.driver != 0) continue;
        const double d = distance_to_vehicle(chest, v.state.position, v.state.yaw, classes[size_t(v.cls)]);
        if (d <= best_distance) {
            best_distance = d;
            best = &v;
        }
    }
    if (best == nullptr) {
        return;
    }
    best->driver = client.entity;
    best->driver_combat = client.state.combat;
    best->team = client.team;
    best->away = 0;
    MoveState state = best->state;
    state.vehicle = int8_t(best->cls);
    client.state = state;
    client.switching_to = -1;
    say(std::format("{} got into a {}", client.name, classes[size_t(best->cls)].label));
}

// Out of the vehicle it drives: beside it, on the ground, with its own
// weapons again. The vehicle stays where it is, empty.
void GameServer::leave_vehicle(Client& client) {
    const auto classes = vehicle_classes();
    const int cls = client.state.vehicle;
    client.state.vehicle = -1;
    Vehicle* v = vehicle_of(client.entity);
    if (v == nullptr || cls < 0 || size_t(cls) >= classes.size()) {
        return;
    }
    v->state = client.state;
    v->state.vehicle = int8_t(v->cls);
    v->driver = 0;
    const VehicleClass& vehicle = classes[size_t(cls)];
    // Out on its left, clear of its body.
    Vec3 out = vehicle_to_world(v->state.position, v->state.yaw, {vehicle.bounds_max.x + 0.8, 0, 0});
    const Vec3 inside = v->state.position + Vec3{0, 0.5, 0};
    if (world_.raycast(inside, out + Vec3{0, 0.5, 0})) {
        out = vehicle_to_world(v->state.position, v->state.yaw, {vehicle.bounds_min.x - 0.8, 0, 0});
    }
    out = world_.clamp(out);
    out.y = world_.ground_at(out.x, out.z, v->state.position.y + 1, 3).height;
    client.state.position = out;
    client.state.velocity = {};
    client.state.combat = v->driver_combat;
    client.state.grounded = true;
    client.state.sprinting = false;
    client.state.energy = client.stats.move.max_energy;
}

// Damage to a vehicle; at none left it blows up, taking its driver with it.
void GameServer::damage_vehicle(Vehicle& v, double amount, uint32_t attacker, std::vector<Blast>& blasts) {
    if (v.dead_for >= 0 || amount <= 0) {
        return;
    }
    v.health = std::max(0.0, v.health - amount);
    events_.push_back({net::CombatEvent::hit, attacker, v.id, float(amount)});
    if (v.health > 0) {
        return;
    }
    const auto classes = vehicle_classes();
    const VehicleClass& cls = classes[size_t(v.cls)];
    say(std::format("a {} was destroyed", cls.label));
    events_.push_back({net::CombatEvent::kill, attacker, v.id, 0});
    v.dead_for = 0;
    if (v.spawn >= 0 && size_t(v.spawn) < spawn_wait_.size()) {
        spawn_wait_[size_t(v.spawn)] = conquest_->battlefield().vehicle_spawns[size_t(v.spawn)].respawn_time;
    }
    if (v.driver != 0) {
        if (Client* driver = find(v.driver)) {
            v.state = driver->state;
            leave_vehicle(*driver);
            kill(*driver, attacker);
        }
        v.driver = 0;
    }
    Blast blast;
    blast.bolt.owner = attacker;
    blast.bolt.team = -1;
    blast.bolt.weapon = net::vehicle_explosion_weapon;
    blast.at = v.state.position + Vec3{0, cls.height() * 0.5, 0};
    blast.normal = {0, 1, 0};
    blast.vehicle = v.cls;
    blasts.push_back(blast);
}

} // namespace swm::core
