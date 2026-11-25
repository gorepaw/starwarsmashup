// GameServer's vehicles in a match: given at their spawns to whoever holds
// each spawn's post, got into and out of with the use button, driven by
// their drivers' inputs (simulate_character), damaged and destroyed.
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

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
        return config_.world_vehicles;
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
    const auto classes = vehicle_classes();
    net::VehicleState s;
    s.id = v.id;
    s.vehicle = uint8_t(v.cls);
    s.position = v.state.position;
    s.velocity = v.state.velocity;
    s.yaw = v.state.yaw;
    s.pitch = v.state.pitch;
    s.bank = v.state.roll;
    s.health = float(v.health);
    s.max_health = float(classes[size_t(v.cls)].max_health);
    s.driver = v.driver;
    s.team = int8_t(v.team);
    s.alive = v.dead_for < 0;
    s.flags = (v.state.grounded ? net::VehicleState::flag_grounded : 0) |
              (conquest_ && is_mobile_spawn(v, v.team, conquest_->battlefield(), classes)
                   ? net::VehicleState::flag_mobile_spawn
                   : 0);
    s.gunners = v.gunners;
    s.owner = v.owner;
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
    if (classes.empty()) {
        return;
    }
    if (conquest_) {
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
    }

    for (Vehicle& v : vehicles_) {
        if (v.dead_for >= 0) {
            v.dead_for += dt;
            continue;
        }
        // In the world, one whose owner has gone goes too.
        if (v.owner != 0 && find(v.owner) == nullptr) {
            v.dead_for = wreck_time;
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
                const double was = v.state.velocity.length();
                v.state = driver->state;
                v.state.combat = combat;  // the driver's state has them while driving
                v.away = 0;
                driven = true;
                // A flyer stopped dead (into the ground or a wall): hurt by
                // how fast it hit (ASSUMPTION: a tenth of its health for
                // each 10 m/s over 10).
                if (cls.kind == VehicleKind::flyer && !v.state.grounded && was > 10 &&
                    v.state.velocity.length() < was * 0.3) {
                    std::vector<Blast> blasts;
                    damage_vehicle(v, cls.max_health * 0.1 * (was - 10) / 10, 0, blasts);
                    for (size_t b = 0; b < blasts.size(); ++b) {
                        const Blast blast = blasts[b];  // explode() may add more
                        explode(blast, blasts);
                    }
                    if (v.dead_for >= 0) continue;
                }
            }
        }
        if (!driven) {
            // Empty: it coasts to a stop and settles.
            MoveInput idle;
            idle.yaw = v.state.yaw;
            idle.pitch = v.state.pitch;
            const Battlefield* field_ptr = conquest_ ? &conquest_->battlefield() : nullptr;
            simulate_vehicle(v.state, idle, world_, cls, dt, field_ptr);
            // Automated ship / hangar turrets engage enemy flyers and boarders
            // when unmanned and not disabled by mainframe or frigate destruction.
            if (v.dead_for < 0 && cls.kind == VehicleKind::turret && v.team >= 0 && field_ptr != nullptr &&
                field_ptr->is_space() && conquest_->state().phase == Phase::live) {
                const bool disabled =
                    v.spawn >= 0 && size_t(v.spawn) < field_ptr->vehicle_spawns.size() &&
                    conquest_->turret_disabled(field_ptr->vehicle_spawns[size_t(v.spawn)].name);
                v.auto_cooldown = std::max(0.0, v.auto_cooldown - dt);
                if (!disabled && cls.loadout.weapons[0]) {
                    const WeaponDef& w = *cls.loadout.weapons[0];
                    const Vec3 eye = vehicle_to_world(v.state.position, v.state.yaw, cls.eye);
                    const double w_range = w.bolt_speed * w.bolt_life;
                    const double max_range = w_range > 0 ? std::min(w_range, 600.0) : 500.0;
                    double best_d2 = max_range * max_range;
                    std::optional<Vec3> target_pos;
                    for (const Vehicle& other : vehicles_) {
                        if (other.dead_for >= 0 || other.team < 0 || allied(v.team, other.team) ||
                            other.driver == 0) {
                            continue;
                        }
                        const Vec3 delta = other.state.position - eye;
                        const double d2 = delta.dot(delta);
                        if (d2 >= best_d2) continue;
                        const Vec3 aim_pt = other.state.position + Vec3{0, 0.5, 0};
                        if (!world_.raycast_hit(eye, aim_pt)) {
                            best_d2 = d2;
                            target_pos = aim_pt;
                        }
                    }
                    if (!target_pos) {
                        const double inf_range = std::min(max_range, 90.0);
                        double inf_d2 = std::min(best_d2, inf_range * inf_range);
                        for (const Client& c : clients_) {
                            if (c.life != Life::alive || allied(v.team, c.team) || c.state.vehicle >= 0) continue;
                            const Vec3 delta = c.state.position - eye;
                            const double d2 = delta.dot(delta);
                            if (d2 >= inf_d2) continue;
                            const Vec3 aim_pt = c.state.position + Vec3{0, 1.1, 0};
                            if (!world_.raycast_hit(eye, aim_pt)) {
                                inf_d2 = d2;
                                target_pos = aim_pt;
                            }
                        }
                    }
                    if (target_pos) {
                        const Vec3 d = *target_pos - eye;
                        const double wanted_yaw = std::atan2(-d.x, -d.z);
                        const double wanted_pitch =
                            std::clamp(std::atan2(d.y, std::hypot(d.x, d.z)), cls.pitch_min, cls.pitch_max);
                        double dy = wanted_yaw - double(v.state.yaw);
                        while (dy > std::numbers::pi) dy -= 2 * std::numbers::pi;
                        while (dy < -std::numbers::pi) dy += 2 * std::numbers::pi;
                        const double step = cls.turn_rate * dt;
                        v.state.yaw = float(double(v.state.yaw) + std::clamp(dy, -step, step));
                        v.state.pitch = float(double(v.state.pitch) +
                                              std::clamp(wanted_pitch - double(v.state.pitch), -step, step));
                        v.state.roll_yaw = v.state.yaw;
                        if (std::abs(dy) < 0.18 && v.auto_cooldown <= 0) {
                            const Vec3 muzzle = vehicle_muzzle(v.state, cls);
                            const Vec3 dir = aim_direction(v.state.yaw, v.state.pitch);
                            Bolt bolt;
                            bolt.id = next_bolt_++;
                            bolt.owner = 0;
                            bolt.team = v.team;
                            bolt.weapon = 0;
                            bolt.position = muzzle;
                            bolt.def = w;
                            bolt.velocity = dir * w.bolt_speed;
                            bolt.life = w.bolt_life;
                            bolt.gravity = 0;
                            bolts_.push_back(bolt);
                            v.auto_cooldown = std::max(0.25, w.shot_delay);
                        }
                    }
                }
            }
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
        if (v.spawn >= 0 && conquest_) {
            const VehicleSpawn& spawn = conquest_->battlefield().vehicle_spawns[size_t(v.spawn)];
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
        if (gone && v.spawn >= 0 && size_t(v.spawn) < spawned_.size() && spawned_[size_t(v.spawn)] == v.id) {
            spawned_[size_t(v.spawn)] = 0;
        }
        return gone;
    });
    if (!conquest_) {
        return;  // the world's are called by their owners
    }
    const Battlefield& field = conquest_->battlefield();
    const MatchState& match = conquest_->state();

    // Spawns: a vehicle for whoever holds the post, once the wait is over;
    // an unused one at home goes over to a new holder.
    for (size_t i = 0; i < field.vehicle_spawns.size(); ++i) {
        const VehicleSpawn& spawn = field.vehicle_spawns[i];
        spawn_wait_[i] = std::max(0.0, spawn_wait_[i] - dt);
        const int holder = spawn.post < 0 ? (spawn.turret_team >= 0 ? spawn.turret_team : 0)
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
        if (conquest_ && conquest_->battlefield().is_space() &&
            size_t(client.state.vehicle) < classes.size() &&
            classes[size_t(client.state.vehicle)].kind == VehicleKind::flyer && client.state.seat == 0 &&
            !client.state.grounded) {
            const Battlefield& field = conquest_->battlefield();
            const VehicleClass& cls = classes[size_t(client.state.vehicle)];
            const double floor = world_.ground_at(client.state.position.x, client.state.position.z,
                                                client.state.position.y + 2.0, 1.5).height;
            if (!field.in_landing_region(client.state.position) ||
                client.state.position.y - floor > cls.takeoff_height + 15.0) {
                notify(client.entity, "Fly into a hangar to land and exit");
                return;
            }
            client.state.position.y = floor + cls.landed_height;
            client.state.velocity = {};
            client.state.grounded = true;
            client.state.pitch = 0;
            client.state.roll = 0;
        }
        leave_vehicle(client);
        return;
    }
    Vehicle* best = nullptr;
    int best_seat = -1;
    double best_distance = 1e9;
    const Vec3 chest = client.state.position + Vec3{0, 1.0, 0};
    for (Vehicle& v : vehicles_) {
        if (v.dead_for >= 0 || (v.owner != 0 && v.owner != client.entity)) continue;
        const VehicleClass& cls = classes[size_t(v.cls)];
        if (cls.kind == VehicleKind::turret && conquest_ && conquest_->battlefield().is_space()) {
            continue;  // space defense turrets are automated or manned via turret chairs
        }
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
        const double reach = cls.kind == VehicleKind::flyer ? enter_reach + 1.5 : enter_reach;
        const double d = distance_to_vehicle(chest, v.state.position, v.state.yaw, cls);
        if (d <= reach && d < best_distance) {
            best_distance = d;
            best = &v;
            best_seat = seat;
        }
    }
    int chosen_chair = -1;
    if (best == nullptr && conquest_ && !conquest_->battlefield().turret_chairs.empty()) {
        const Battlefield& field = conquest_->battlefield();
        double best_chair_d2 = 3.5 * 3.5;
        for (size_t i = 0; i < field.turret_chairs.size(); ++i) {
            const TurretChair& chair = field.turret_chairs[i];
            if (chair.team >= 0 && !allied(chair.team, client.team)) continue;
            const Vec3 delta = chair.position - client.state.position;
            const double d2 = delta.dot(delta);
            if (d2 <= best_chair_d2) {
                best_chair_d2 = d2;
                chosen_chair = int(i);
            }
        }
        if (chosen_chair >= 0) {
            const Vec3 chair_pos = field.turret_chairs[size_t(chosen_chair)].position;
            double best_tur_d2 = 1e30;
            for (Vehicle& v : vehicles_) {
                if (v.dead_for >= 0 || v.driver != 0 || classes[size_t(v.cls)].kind != VehicleKind::turret) continue;
                if (v.team >= 0 && !allied(v.team, client.team)) continue;
                if (v.spawn >= 0 && size_t(v.spawn) < field.vehicle_spawns.size() &&
                    conquest_->turret_disabled(field.vehicle_spawns[size_t(v.spawn)].name)) {
                    continue;
                }
                const Vec3 delta = v.state.position - chair_pos;
                const double d2 = delta.dot(delta);
                if (d2 < best_tur_d2) {
                    best_tur_d2 = d2;
                    best = &v;
                    best_seat = 0;
                }
            }
        }
    }
    if (best == nullptr) {
        return;
    }
    client.remote_chair = chosen_chair;
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

void GameServer::call_vehicle(Client& client, const ItemDef& def) {
    const auto classes = vehicle_classes();
    if (conquest_) {
        return;
    }
    // Called already: put away (got out of first).
    for (auto it = vehicles_.begin(); it != vehicles_.end(); ++it) {
        if (it->owner != client.entity) continue;
        if (client.state.vehicle >= 0) leave_vehicle(client);
        const std::string label = size_t(it->cls) < classes.size() ? classes[size_t(it->cls)].label : "vehicle";
        vehicles_.erase(it);
        notify(client.entity, std::format("{} put away", label));
        return;
    }
    const auto cls = std::find_if(classes.begin(), classes.end(),
                                  [&](const VehicleClass& c) { return c.name == def.vehicle; });
    if (cls == classes.end()) {
        notify(client.entity, std::format("{} can't be called here", def.display_name));
        return;
    }
    if (client.state.vehicle >= 0) {
        return;
    }
    // Beside its owner, on their right, facing their way.
    const VehicleClass& vehicle = *cls;
    Vehicle v;
    v.id = next_entity_++;
    v.cls = int(cls - classes.begin());
    v.owner = client.entity;
    v.team = client.team;
    v.health = vehicle.max_health;
    Vec3 at = vehicle_to_world(client.state.position, client.state.yaw, {-(vehicle.bounds_max.x + 1.2), 0, 0});
    if (world_.raycast(client.state.position + Vec3{0, 1, 0}, at + Vec3{0, 1, 0})) {
        at = vehicle_to_world(client.state.position, client.state.yaw, {-vehicle.bounds_min.x + 1.2, 0, 0});
    }
    at = world_.clamp(at);
    at.y = world_.ground_at(at.x, at.z, client.state.position.y + 1, 2).height +
           (vehicle.kind == VehicleKind::hover ? vehicle.altitude : 0);
    v.state.position = at;
    v.state.yaw = client.state.yaw;
    v.state.vehicle = int8_t(v.cls);
    v.state.grounded = true;
    reset_loadout(v.state, vehicle.loadout, CharacterStats{});
    vehicles_.push_back(v);
    notify(client.entity, std::format("{} called", vehicle.label));
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
    if (client.remote_chair >= 0 && conquest_ &&
        size_t(client.remote_chair) < conquest_->battlefield().turret_chairs.size()) {
        const TurretChair& chair = conquest_->battlefield().turret_chairs[size_t(client.remote_chair)];
        client.state.position = chair.position;
        client.state.yaw = float(chair.yaw);
        client.state.velocity = {};
        client.state.combat = own;
        client.state.grounded = true;
        client.state.sprinting = false;
        client.state.energy = client.stats.move.max_energy;
        client.remote_chair = -1;
        return;
    }
    client.remote_chair = -1;
    // Out on its left, clear of its body.
    Vec3 out = vehicle_to_world(v->state.position, v->state.yaw, {vehicle.bounds_max.x + 0.8, 0, 0});
    const Vec3 inside = v->state.position + Vec3{0, 0.5, 0};
    if (world_.raycast(inside, out + Vec3{0, 0.5, 0})) {
        out = vehicle_to_world(v->state.position, v->state.yaw, {vehicle.bounds_min.x - 0.8, 0, 0});
    }
    out = world_.clamp(out);
    out.y = world_.ground_at(out.x, out.z, v->state.position.y + 2.0, 3).height;
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
    amount *= vehicle_damage_factor(v.driver);  // Stage 12: Pilot's Evasive Maneuvers and Reactor Tuning
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
            kill(*driver, attacker, cls.kind == VehicleKind::flyer);
        }
        v.driver = 0;
    }
    for (uint32_t& g : v.gunners) {
        if (Client* gunner = g != 0 ? find(g) : nullptr) {
            leave_vehicle(*gunner);
            kill(*gunner, attacker, cls.kind == VehicleKind::flyer);
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
