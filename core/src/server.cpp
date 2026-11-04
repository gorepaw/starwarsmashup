#include "swm/core/server.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "swm/core/net/bytes.h"
#include "swm/core/net/protocol.h"

namespace swm::core {

GameServer::GameServer(net::Link& link, const World& world, ServerConfig config, CharacterStore* store)
    : link_(link), world_(world), config_(config), store_(store) {}

void GameServer::say(const std::string& text) const {
    if (log) {
        log(text);
    }
}

void GameServer::send(const net::Endpoint& to, const auto& message) {
    link_.send(to, net::encode(message));
}

GameServer::Client* GameServer::find(const net::Endpoint& endpoint) {
    auto it = std::find_if(clients_.begin(), clients_.end(), [&](const Client& c) { return c.endpoint == endpoint; });
    return it == clients_.end() ? nullptr : &*it;
}

GameServer::Client* GameServer::find(uint32_t entity) {
    auto it = std::find_if(clients_.begin(), clients_.end(), [&](const Client& c) { return c.entity == entity; });
    return it == clients_.end() ? nullptr : &*it;
}

std::vector<uint32_t> GameServer::entities() const {
    std::vector<uint32_t> ids;
    for (const Client& client : clients_) {
        ids.push_back(client.entity);
    }
    return ids;
}

const MoveState* GameServer::state_of(uint32_t entity) const {
    for (const Client& client : clients_) {
        if (client.entity == entity) {
            return &client.state;
        }
    }
    return nullptr;
}

void GameServer::set_state(uint32_t entity, const MoveState& state) {
    if (Client* client = find(entity)) {
        client->state = state;
    }
}

double GameServer::health_of(uint32_t entity) const {
    for (const Client& client : clients_) {
        if (client.entity == entity) {
            return client.health;
        }
    }
    return 0;
}

bool GameServer::alive(uint32_t entity) const {
    for (const Client& client : clients_) {
        if (client.entity == entity) {
            return client.alive;
        }
    }
    return false;
}

void GameServer::update(double now) {
    if (!started_) {
        started_ = true;
        next_tick_time_ = now;
        next_save_time_ = now + config_.save_interval;
    }
    receive(now);
    const double dt = 1 / config_.tick_rate;
    if (now - next_tick_time_ > 0.25) {
        next_tick_time_ = now;  // stalled (e.g. a debugger): don't try to catch up
    }
    while (now >= next_tick_time_) {
        step();
        next_tick_time_ += dt;
    }
    drop_silent(now);
    if (now >= next_save_time_) {
        save_all();
        next_save_time_ = now + config_.save_interval;
    }
}

void GameServer::receive(double now) {
    while (auto datagram = link_.receive()) {
        try {
            handle(datagram->from, *datagram, now);
        } catch (const net::PacketError& error) {
            say(std::format("dropped a packet from {}: {}", datagram->from.to_string(), error.what()));
        }
    }
}

void GameServer::handle(const net::Endpoint& from, const net::Datagram& datagram, double now) {
    const net::Message message = net::decode(datagram.data);
    Client* client = find(from);
    if (const auto* hello = std::get_if<net::Hello>(&message)) {
        if (client == nullptr) {
            client = join(from, hello->name);
            if (client == nullptr) {
                return;
            }
        }
        // Also answers a repeated Hello whose Welcome was lost.
        send(from, net::Welcome{client->entity, config_.tick_rate, tick_, client->character});
    }
    if (client == nullptr) {
        return;  // not connected: ignore everything but Hello
    }
    client->last_heard = now;
    if (const auto* batch = std::get_if<net::InputBatch>(&message)) {
        // Batches overlap: keep only inputs newer than any already seen.
        uint32_t newest = client->queue.empty() ? client->last_input : client->queue.back().sequence;
        for (const MoveInput& input : batch->inputs) {
            if (input.sequence > newest && std::isfinite(input.yaw) && std::isfinite(input.pitch) &&
                std::isfinite(input.view_tick)) {
                client->queue.push_back(input);
                newest = input.sequence;
            }
        }
        while (client->queue.size() > config_.max_queued_inputs) {
            client->queue.pop_front();
        }
    } else if (std::holds_alternative<net::Goodbye>(message)) {
        say(std::format("{} (entity {}) left", client->name, client->entity));
        save({client, 1});
        clients_.erase(clients_.begin() + (client - clients_.data()));
    }
}

GameServer::Client* GameServer::join(const net::Endpoint& from, const std::string& name) {
    const auto refuse = [&](const std::string& reason) {
        say(std::format("refused {} from {}: {}", name, from.to_string(), reason));
        send(from, net::Refused{reason});
        return nullptr;
    };
    if (name.empty() || name.size() > 32) {
        return refuse("names are 1 to 32 characters");
    }
    if (std::any_of(clients_.begin(), clients_.end(), [&](const Client& c) { return c.name == name; })) {
        return refuse(name + " is already playing");
    }
    Client joined;
    joined.endpoint = from;
    joined.name = name;
    joined.entity = next_entity_++;
    Vec3 spawn = spawn_point(joined.entity - 1);
    std::string where = "new";
    if (store_ != nullptr) {
        try {
            if (auto saved = store_->find_by_name(name)) {
                joined.character = saved->id;
                joined.state.yaw = saved->yaw;
                spawn = world_.clamp(saved->position);
                where = "returning";
            } else {
                joined.character = store_->create(name, spawn, 0).id;
            }
        } catch (const std::exception& error) {
            say(std::format("storage error: {}", error.what()));
            return refuse("the server can't load characters right now");
        }
    }
    // Stand on whatever is under the spawn point, near its height: a building
    // floor, or the terrain. A saved position may have been mid-jump.
    spawn.y = world_.ground_at(spawn.x, spawn.z, spawn.y, config_.soldier.move.step_up).height;
    joined.state.position = spawn;
    reset_loadout(joined.state, config_.soldier);
    joined.health = config_.soldier.max_health;
    clients_.push_back(joined);
    say(std::format("{} joined from {} as entity {} ({} character {}) at ({:.2f}, {:.2f}, {:.2f})", name,
                    from.to_string(), joined.entity, where, joined.character, spawn.x, spawn.y, spawn.z));
    return &clients_.back();
}

void GameServer::save_all() {
    save(clients_);
}

void GameServer::save(std::span<const Client> clients) {
    if (store_ == nullptr || clients.empty()) {
        return;
    }
    std::vector<CharacterRecord> records;
    for (const Client& client : clients) {
        records.push_back({client.character, client.name, client.state.position, client.state.yaw});
    }
    try {
        store_->save(records);
    } catch (const std::exception& error) {
        say(std::format("storage error, {} character(s) not saved: {}", records.size(), error.what()));
    }
}

void GameServer::step() {
    const double dt = 1 / config_.tick_rate;
    for (Client& client : clients_) {
        for (size_t i = 0; i < config_.max_inputs_per_tick && !client.queue.empty(); ++i) {
            const MoveInput input = client.queue.front();
            client.queue.pop_front();
            const auto shot = simulate_character(client.state, input, client.alive, world_, config_.soldier, dt);
            client.last_input = input.sequence;
            if (shot) {
                fire(client, *shot, input.view_tick);
            }
        }
    }
    ++tick_;

    // Where everyone is this tick, kept for a second for rewinding.
    const size_t keep = size_t(config_.tick_rate) + 2;
    for (Client& client : clients_) {
        client.history.emplace_back(tick_, client.state.position);
        while (client.history.size() > keep) {
            client.history.pop_front();
        }
    }
    advance_bolts();
    for (Client& client : clients_) {
        if (!client.alive && tick_ >= client.respawn_tick) {
            respawn(client);
        }
    }

    net::Snapshot snapshot;
    snapshot.server_tick = tick_;
    for (const Client& client : clients_) {
        snapshot.entities.push_back(
            {client.entity, client.state, float(client.health), client.alive, client.spawn_count});
    }
    for (const Bolt& bolt : bolts_) {
        snapshot.bolts.push_back({bolt.id, bolt.owner, bolt.weapon, bolt.position, bolt.velocity});
    }
    snapshot.events = std::move(events_);
    events_.clear();
    for (const Client& client : clients_) {
        snapshot.last_input = client.last_input;
        send(client.endpoint, snapshot);
    }
}

void GameServer::fire(const Client& shooter, const Shot& shot, double view_tick) {
    const WeaponDef& weapon = config_.soldier.weapons[size_t(shot.weapon)];
    Bolt bolt;
    bolt.id = next_bolt_++;
    bolt.owner = shooter.entity;
    bolt.weapon = uint8_t(shot.weapon);
    bolt.position = shot.origin;
    bolt.velocity = shot.direction * weapon.bolt_speed;
    bolt.life = weapon.bolt_life;
    bolt.damage = weapon.damage;
    bolt.gravity = weapon.bolt_gravity * config_.soldier.move.gravity;
    // The tick being simulated is tick_ + 1; the shooter saw others at view_tick.
    bolt.rewind = std::clamp(double(tick_ + 1) - view_tick, 0.0, config_.max_rewind * config_.tick_rate);
    bolts_.push_back(bolt);
}

Vec3 GameServer::position_at(const Client& client, double tick) const {
    const auto& h = client.history;
    if (h.empty() || tick >= h.back().first) {
        return client.state.position;
    }
    if (tick <= h.front().first) {
        return h.front().second;
    }
    for (size_t i = 1; i < h.size(); ++i) {
        if (tick <= h[i].first) {
            const auto& [t0, p0] = h[i - 1];
            const auto& [t1, p1] = h[i];
            const double f = (tick - t0) / double(t1 - t0);
            return p0 + (p1 - p0) * f;
        }
    }
    return client.state.position;
}

void GameServer::advance_bolts() {
    const double dt = 1 / config_.tick_rate;
    std::erase_if(bolts_, [&](Bolt& bolt) {
        const Vec3 from = bolt.position;
        bolt.velocity.y -= bolt.gravity * dt;
        const Vec3 to = from + bolt.velocity * dt;
        // The first thing along the way: the world, or someone where the
        // shooter saw them.
        std::optional<double> nearest = world_.raycast(from, to);
        Client* victim = nullptr;
        for (Client& target : clients_) {
            if (target.entity == bolt.owner || !target.alive) {
                continue;
            }
            const Vec3 feet = position_at(target, double(tick_) - bolt.rewind);
            const auto t = segment_hits_cylinder(from, to, feet, hit_radius, hit_height);
            if (t && (!nearest || *t < *nearest)) {
                nearest = t;
                victim = &target;
            }
        }
        if (victim != nullptr) {
            victim->health = std::max(0.0, victim->health - bolt.damage);
            events_.push_back({net::CombatEvent::hit, bolt.owner, victim->entity, float(bolt.damage)});
            if (victim->health <= 0) {
                victim->alive = false;
                victim->respawn_tick = tick_ + uint32_t(std::lround(config_.respawn_delay * config_.tick_rate));
                events_.push_back({net::CombatEvent::kill, bolt.owner, victim->entity, 0});
                const Client* killer = find(bolt.owner);
                say(std::format("{} killed {}", killer ? killer->name : "someone", victim->name));
            }
            return true;
        }
        if (nearest) {
            return true;  // hit the world
        }
        bolt.position = to;
        bolt.life -= dt;
        return bolt.life <= 0;
    });
}

Vec3 GameServer::spawn_point(uint32_t index) const {
    Vec3 spawn = world_.clamp(config_.spawn + Vec3{config_.spawn_spacing * (index % 8), 0, 0});
    const double terrain = world_.terrain().height_at(spawn.x, spawn.z);
    spawn.y = world_.ground_at(spawn.x, spawn.z, terrain, config_.soldier.move.step_up).height;
    return spawn;
}

void GameServer::respawn(Client& client) {
    client.alive = true;
    client.health = config_.soldier.max_health;
    const float yaw = client.state.yaw;
    client.state = MoveState{};
    client.state.position = spawn_point(client.entity - 1);
    client.state.yaw = yaw;
    reset_loadout(client.state, config_.soldier);
    client.history.clear();
    ++client.spawn_count;
    say(std::format("{} respawned", client.name));
}

void GameServer::drop_silent(double now) {
    std::erase_if(clients_, [&](const Client& client) {
        const bool silent = now - client.last_heard > config_.client_timeout;
        if (silent) {
            say(std::format("{} (entity {}) timed out", client.name, client.entity));
            save({&client, 1});
        }
        return silent;
    });
}

} // namespace swm::core
