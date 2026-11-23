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
constexpr double eject_speed = 3;     // m/s: getting out of a vehicle going faster throws you clear
constexpr double eject_hop = 4;       // m/s up, thrown clear (ASSUMPTION)

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
    s.gunners = v.gunners;
    return s;
}

GameServer::Vehicle* GameServer::vehicle_of(uint32_t driver) {
    for (Vehicle& v : vehicles_) {
        if (v.driver == driver && v.dead_for < 0) return &v;
    }
    return nullptr;
}

GameServer::Vehicle* GameServer::vehicle_gunned_by(uint32_t gunner) {
    for (Vehicle& v : vehicles_) {
        if (v.dead_for < 0 && std::find(v.gunners.begin(), v.gunners.end(), gunner) != v.gunners.end()) return &v;
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
        const VehicleClass& cls = classes[size_t(v.cls)];
        v.gunners.resize(cls.seats.size(), 0);
        v.gunner_combat.resize(cls.seats.size());
        bool driven = false;
        // Driven: where its driver is. A driver who's gone or out leaves it empty.
        if (v.driver != 0) {
            const Client* driver = find(v.driver);
            if (driver == nullptr || driver->state.vehicle != v.cls || driver->state.seat != 0 ||
                driver->life != Life::alive) {
                v.driver = 0;
                if (driver != nullptr) v.state = driver->state;
                v.state.vehicle = int8_t(v.cls);
                v.state.seat = 0;
            } else {
                const CombatState combat = v.state.combat;
                v.state = driver->state;
                v.state.combat = combat;  // the driver's state has them while driving
                v.away = 0;
                driven = true;
            }
        }
        if (!driven) {
            // Empty: it coasts to a stop and settles.
            MoveInput idle;
            idle.yaw = v.state.yaw;
            simulate_vehicle(v.state, idle, world_, cls, dt);
        }
        // Its gunners ride where it is, facing its way (their look turns the guns).
        bool manned = driven;
        for (size_t k = 0; k < v.gunners.size(); ++k) {
            Client* gunner = v.gunners[k] != 0 ? find(v.gunners[k]) : nullptr;
            if (gunner == nullptr || gunner->state.vehicle != v.cls || gunner->state.seat != int8_t(k + 1) ||
                gunner->life != Life::alive) {
                v.gunners[k] = 0;
                continue;
            }
            gunner->state.position = v.state.position;
            gunner->state.velocity = v.state.velocity;
            gunner->state.yaw = v.state.yaw;
            gunner->state.grounded = true;
            manned = true;
        }
        if (driven) {
            continue;
        }
        if (manned) {
            v.away = 0;
        }
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
            const bool idle_at_home = current->dead_for < 0 && current->driver == 0 &&
                                      std::all_of(current->gunners.begin(), current->gunners.end(),
                                                  [](uint32_t g) { return g == 0; }) &&
                                      current->away == 0 &&
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
        v.gunners.assign(cls.seats.size(), 0);
        v.gunner_combat.assign(cls.seats.size(), CombatState{});
        spawned_[i] = v.id;
        vehicles_.push_back(v);
    }
}

// The use button: out of the vehicle ridden in, or into the nearest one
// within reach with a free seat: its driver's if free, else its first free
// gunner's (only beside one's own side).
void GameServer::use_vehicle(Client& client) {
    const auto classes = vehicle_classes();
    if (client.state.vehicle >= 0) {
        leave_vehicle(client);
        return;
    }
    Vehicle* best = nullptr;
    int best_seat = -1;
    double best_distance = enter_reach;
    const Vec3 chest = client.state.position + Vec3{0, 1.0, 0};
    for (Vehicle& v : vehicles_) {
        if (v.dead_for >= 0) continue;
        int seat = -1;
        const bool aboard = v.driver != 0 || std::any_of(v.gunners.begin(), v.gunners.end(),
                                                         [](uint32_t g) { return g != 0; });
        if (v.driver == 0) {
            seat = 0;
        } else if (!aboard || allied(v.team, client.team)) {
            for (size_t k = 0; k < v.gunners.size(); ++k) {
                if (v.gunners[k] == 0) {
                    seat = int(k + 1);
                    break;
                }
            }
        }
        if (seat < 0 || (aboard && !allied(v.team, client.team))) continue;
        const double d = distance_to_vehicle(chest, v.state.position, v.state.yaw, classes[size_t(v.cls)]);
        if (d <= best_distance) {
            best_distance = d;
            best = &v;
            best_seat = seat;
        }
    }
    if (best == nullptr) {
        return;
    }
    if (best_seat > 0) {
        const VehicleSeat& seat = classes[size_t(best->cls)].seats[size_t(best_seat - 1)];
        best->gunners[size_t(best_seat - 1)] = client.entity;
        best->gunner_combat[size_t(best_seat - 1)] = client.state.combat;
        client.state.vehicle = int8_t(best->cls);
        client.state.seat = int8_t(best_seat);
        client.state.position = best->state.position;
        client.state.velocity = best->state.velocity;
        client.state.yaw = best->state.yaw;
        client.state.crouched = false;
        reset_loadout(client.state, seat.loadout, client.stats);
        client.switching_to = -1;
        say(std::format("{} took the {}'s {}", client.name, classes[size_t(best->cls)].label, seat.name));
        return;
    }
    best->driver = client.entity;
    best->driver_combat = client.state.combat;
    best->team = client.team;
    best->away = 0;
    MoveState state = best->state;
    state.vehicle = int8_t(best->cls);
    state.seat = 0;
    client.state = state;
    client.switching_to = -1;
    say(std::format("{} got into a {}", client.name, classes[size_t(best->cls)].label));
}

// Out of the vehicle it rides in: beside it, on the ground, with its own
// weapons again; going fast, thrown clear with its speed (ejecting). The
// vehicle goes on without it (a driver's coasts to a stop).
void GameServer::leave_vehicle(Client& client) {
    const auto classes = vehicle_classes();
    const int cls = client.state.vehicle;
    const int seat = client.state.seat;
    client.state.vehicle = -1;
    client.state.seat = 0;
    Vehicle* v = seat > 0 ? vehicle_gunned_by(client.entity) : vehicle_of(client.entity);
    if (v == nullptr || cls < 0 || size_t(cls) >= classes.size()) {
        return;
    }
    CombatState own;
    if (seat > 0) {
        own = v->gunner_combat[size_t(seat - 1)];
        v->gunners[size_t(seat - 1)] = 0;
    } else {
        v->state = client.state;
        v->state.vehicle = int8_t(v->cls);
        v->state.seat = 0;
        v->driver = 0;
        own = v->driver_combat;
    }
    const Vec3 carried = v->state.velocity;
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
    client.state.combat = own;
    client.state.grounded = true;
    if (std::hypot(carried.x, carried.z) > eject_speed) {
        client.state.velocity = {carried.x, eject_hop, carried.z};
        client.state.grounded = false;
    }
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
    for (uint32_t& g : v.gunners) {
        if (Client* gunner = g != 0 ? find(g) : nullptr) {
            leave_vehicle(*gunner);
            kill(*gunner, attacker);
        }
        g = 0;
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
