#include "swm/core/server.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "swm/core/net/bytes.h"
#include "swm/core/net/protocol.h"

namespace swm::core {

GameServer::GameServer(net::Link& link, const World& world, ServerConfig config, CharacterStore* store)
    : link_(link), world_(world), config_(std::move(config)), store_(config_.battlefield ? nullptr : store) {
    if (config_.battlefield) {
        conquest_.emplace(*config_.battlefield, !config_.single_match);
    }
}

bool GameServer::match_over() const {
    return conquest_ && config_.single_match && conquest_->state().phase == Phase::ended &&
           conquest_->state().time_left <= 0;
}

void GameServer::set_recruiters(std::vector<net::Recruiter> recruiters, std::array<std::string, 2> factions) {
    recruiters_ = std::move(recruiters);
    factions_ = std::move(factions);
}

net::PlayerStatus* GameServer::status(uint32_t entity) {
    Client* client = find(entity);
    return client ? &client->status : nullptr;
}

void GameServer::notify(uint32_t entity, const std::string& text) {
    if (Client* client = find(entity)) {
        client->status.notice = text.substr(0, 255);
        ++client->status.notice_id;
    }
}

void GameServer::transfer(uint32_t entity, const net::Transfer& transfer) {
    if (Client* client = find(entity)) {
        client->transfer = transfer;
        client->transfer_tick = tick_ + uint32_t(3 * config_.tick_rate);
    }
}

void GameServer::transfer_all(const net::Transfer& transfer) {
    for (const Client& client : clients_) {
        this->transfer(client.entity, transfer);
    }
}

std::string GameServer::name_of(uint32_t entity) const {
    const Client* client = find(entity);
    return client ? client->name : std::string();
}

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

const GameServer::Client* GameServer::find(uint32_t entity) const {
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
    const Client* client = find(entity);
    return client ? &client->state : nullptr;
}

void GameServer::set_state(uint32_t entity, const MoveState& state) {
    if (Client* client = find(entity)) {
        client->state = state;
    }
}

double GameServer::health_of(uint32_t entity) const {
    const Client* client = find(entity);
    return client ? client->health : 0;
}

bool GameServer::alive(uint32_t entity) const {
    const Client* client = find(entity);
    return client && client->life == Life::alive;
}

std::optional<net::EntityState> GameServer::describe(uint32_t entity) const {
    const Client* client = find(entity);
    return client ? std::optional(describe(*client)) : std::nullopt;
}

net::EntityState GameServer::describe(const Client& c) const {
    return {c.entity,      c.state,     float(c.health), c.life,    c.spawn_count, int8_t(c.team),
            uint8_t(c.soldier_class), c.kills, c.deaths, c.captures};
}

const SoldierDef& GameServer::soldier_of(const Client& client) const {
    if (config_.battlefield && client.team >= 0) {
        const auto& classes = config_.battlefield->teams[size_t(client.team)].classes;
        return classes[std::min(client.soldier_class, classes.size() - 1)];
    }
    return config_.soldier;
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
            client = join(from, hello->name, hello->ticket);
            if (client == nullptr) {
                return;
            }
            if (on_join) {
                const uint32_t entity = client->entity;
                on_join(entity, client->name);
                client = find(entity);
            }
        }
        // Also answers a repeated Hello whose Welcome was lost.
        send(from, net::Welcome{client->entity, config_.tick_rate, tick_, client->character, recruiters_, factions_});
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
    } else if (const auto* request = std::get_if<net::SpawnRequest>(&message)) {
        client->wants_spawn = *request;
    } else if (const auto* queue = std::get_if<net::QueueRequest>(&message)) {
        // Joining a queue takes standing at a war terminal; leaving doesn't.
        const bool at_terminal = std::any_of(recruiters_.begin(), recruiters_.end(), [&](const net::Recruiter& r) {
            const Vec3 d = client->state.position - r.position;
            return std::hypot(d.x, d.z) <= r.radius + 1;
        });
        if (on_queue && !client->transfer && (queue->team < 0 || (client->life == Life::alive && at_terminal))) {
            on_queue(client->entity, queue->team);
        }
    } else if (std::holds_alternative<net::Goodbye>(message)) {
        say(std::format("{} (entity {}) left", client->name, client->entity));
        save({client, 1});
        clients_.erase(clients_.begin() + (client - clients_.data()));
    }
}

GameServer::Client* GameServer::join(const net::Endpoint& from, const std::string& name, uint64_t ticket) {
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
    std::optional<int> admitted_team = -1;
    if (admit) {
        admitted_team = admit(name, ticket);
        if (!admitted_team) {
            return refuse("this match only admits players sent by its hub");
        }
    }
    Client joined;
    joined.endpoint = from;
    joined.name = name;
    joined.entity = next_entity_++;

    if (conquest_) {
        // The smaller team; then wait for the client to choose a spawn.
        std::array<int, 2> sizes{};
        for (const Client& c : clients_) {
            ++sizes[size_t(c.team)];
        }
        joined.team = *admitted_team >= 0 ? std::min(*admitted_team, 1) : sizes[1] < sizes[0] ? 1 : 0;
        joined.life = Life::waiting;
        const Battlefield& field = conquest_->battlefield();
        Vec3 view = field.center();
        for (const CommandPost& post : field.posts) {
            if (post.owner == joined.team) {
                view = post.position;
                break;
            }
        }
        view = world_.clamp(view);
        view.y = world_.terrain().height_at(view.x, view.z);
        joined.state.position = view;
        reset_loadout(joined.state, soldier_of(joined));
        clients_.push_back(joined);
        say(std::format("{} joined from {} as entity {} on team {}", name, from.to_string(), joined.entity,
                        field.teams[size_t(joined.team)].name));
        return &clients_.back();
    }

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
            client.last_input = input.sequence;
            if (client.life == Life::waiting) {
                continue;  // not in the world
            }
            const auto shot =
                simulate_character(client.state, input, client.life == Life::alive, world_, soldier_of(client), dt);
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
    if (conquest_) {
        run_match();
    } else {
        for (Client& client : clients_) {
            if (client.life == Life::dead && tick_ >= client.respawn_tick) {
                respawn(client);
            }
        }
    }

    net::Snapshot snapshot;
    snapshot.server_tick = tick_;
    for (const Client& client : clients_) {
        snapshot.entities.push_back(describe(client));
    }
    for (const Bolt& bolt : bolts_) {
        snapshot.bolts.push_back({bolt.id, bolt.owner, bolt.weapon, bolt.position, bolt.velocity});
    }
    snapshot.events = std::move(events_);
    events_.clear();
    if (conquest_) {
        snapshot.match = conquest_->state();
    }
    for (const Client& client : clients_) {
        snapshot.last_input = client.last_input;
        snapshot.you = client.status;
        send(client.endpoint, snapshot);
        if (client.transfer) {
            send(client.endpoint, *client.transfer);
        }
    }
    // Transferred clients that never said goodbye.
    std::erase_if(clients_, [&](const Client& client) {
        const bool gone = client.transfer && tick_ >= client.transfer_tick;
        if (gone) {
            say(std::format("{} (entity {}) transferred", client.name, client.entity));
            save({&client, 1});
        }
        return gone;
    });
}

void GameServer::run_match() {
    const double dt = 1 / config_.tick_rate;
    const Battlefield& field = conquest_->battlefield();
    for (Client& client : clients_) {
        if (client.life == Life::dead && tick_ >= client.respawn_tick) {
            return_to_waiting(client);
        }
        // Leaving the battlefield kills, after a warning.
        if (client.life == Life::alive && !field.contains(client.state.position)) {
            client.outside += dt;
            if (client.outside >= field.out_of_bounds_time) {
                kill(client, 0);
            }
        } else {
            client.outside = 0;
        }
    }

    std::vector<Conquest::Unit> units;
    for (const Client& client : clients_) {
        if (client.life == Life::alive) {
            units.push_back({client.team, client.state.position});
        }
    }
    for (const MatchEvent& event : conquest_->step(dt, units, clients_.size())) {
        switch (event.kind) {
        case MatchEvent::went_live:
            say("the match is live");
            bolts_.clear();
            for (Client& client : clients_) {
                client.kills = client.deaths = client.captures = 0;
                return_to_waiting(client);
            }
            break;
        case MatchEvent::restarted:
            say("warmup for the next match");
            for (Client& client : clients_) {
                return_to_waiting(client);
            }
            break;
        case MatchEvent::ended: {
            say(event.team >= 0 ? std::format("{} won", field.teams[size_t(event.team)].name)
                                : std::string("the match is a draw"));
            MatchResult result{field.name, event.team, {}};
            for (const Client& client : clients_) {
                say(std::format("  {:<16} {:<12} {} kills, {} deaths, {} captures", client.name,
                                field.teams[size_t(client.team)].name, client.kills, client.deaths, client.captures));
                result.players.push_back({client.name, client.team, client.kills, client.deaths, client.captures});
            }
            if (on_match_ended) {
                on_match_ended(result);
            }
            break;
        }
        case MatchEvent::captured:
        case MatchEvent::neutralized: {
            const CommandPost& post = field.posts[size_t(event.post)];
            for (Client& client : clients_) {
                if (client.team == event.team && client.life == Life::alive && post.contains(client.state.position)) {
                    ++client.captures;
                }
            }
            events_.push_back({event.kind == MatchEvent::captured ? net::CombatEvent::captured
                                                                   : net::CombatEvent::neutralized,
                               uint32_t(event.team), uint32_t(event.post), 0});
            say(std::format("{} {} {}", field.teams[size_t(event.team)].name,
                            event.kind == MatchEvent::captured ? "captured" : "neutralized", post.name));
            break;
        }
        }
    }

    for (Client& client : clients_) {
        if (client.life != Life::waiting || !client.wants_spawn) {
            continue;
        }
        const size_t soldier_class = client.wants_spawn->soldier_class;
        const size_t post = client.wants_spawn->post;
        if (soldier_class < field.teams[size_t(client.team)].classes.size() &&
            conquest_->can_spawn(client.team, post)) {
            spawn_at_post(client, soldier_class, post);
        }
    }
}

void GameServer::return_to_waiting(Client& client) {
    client.life = Life::waiting;
    client.health = 0;
    client.outside = 0;
    client.wants_spawn.reset();
    client.state.velocity = {};
}

void GameServer::spawn_at_post(Client& client, size_t soldier_class, size_t post_index) {
    const Battlefield& field = conquest_->battlefield();
    const CommandPost& post = field.posts[post_index];
    // Spread around the post (golden-angle steps), facing away from it.
    const double angle = double(next_spawn_slot_++) * 2.399963;
    Vec3 position = post.position + Vec3{std::cos(angle), 0, std::sin(angle)} * post.spawn_radius;
    position.x = std::clamp(position.x, field.min_x, field.max_x);
    position.z = std::clamp(position.z, field.min_z, field.max_z);
    position = world_.clamp(position);
    const double terrain = world_.terrain().height_at(position.x, position.z);
    client.soldier_class = soldier_class;
    const SoldierDef& soldier = soldier_of(client);
    position.y = world_.ground_at(position.x, position.z, terrain, soldier.move.step_up).height;

    client.state = MoveState{};
    client.state.position = position;
    client.state.yaw = float(std::atan2(-(position.x - post.position.x), -(position.z - post.position.z)));
    reset_loadout(client.state, soldier);
    client.health = soldier.max_health;
    client.life = Life::alive;
    client.outside = 0;
    client.history.clear();
    client.wants_spawn.reset();
    ++client.spawn_count;
    say(std::format("{} spawned as {} at {}", client.name, soldier.name, post.name));
}

void GameServer::fire(const Client& shooter, const Shot& shot, double view_tick) {
    const SoldierDef& soldier = soldier_of(shooter);
    const WeaponDef& weapon = soldier.weapons[size_t(shot.weapon)];
    // The tick being simulated is tick_ + 1; the shooter saw others at view_tick.
    const double rewind = std::clamp(double(tick_ + 1) - view_tick, 0.0, config_.max_rewind * config_.tick_rate);
    for (const Vec3& direction : shot.directions) {
        Bolt bolt;
        bolt.id = next_bolt_++;
        bolt.owner = shooter.entity;
        bolt.team = shooter.team;
        bolt.weapon = uint8_t(shot.weapon);
        bolt.position = shot.origin;
        bolt.velocity = direction * weapon.bolt_speed;
        bolt.life = weapon.bolt_life;
        bolt.damage = weapon.damage;
        bolt.gravity = weapon.bolt_gravity * soldier.move.gravity;
        bolt.rewind = rewind;
        bolts_.push_back(bolt);
    }
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
    // After a match ends, bolts fly on but harm nobody.
    const bool harmless = conquest_ && conquest_->state().phase == Phase::ended;
    std::erase_if(bolts_, [&](Bolt& bolt) {
        const Vec3 from = bolt.position;
        bolt.velocity.y -= bolt.gravity * dt;
        const Vec3 to = from + bolt.velocity * dt;
        // The first thing along the way: the world, or someone where the
        // shooter saw them.
        std::optional<double> nearest = world_.raycast(from, to);
        Client* victim = nullptr;
        for (Client& target : clients_) {
            const bool teammate = bolt.team >= 0 && target.team == bolt.team && !config_.friendly_fire;
            if (harmless || target.entity == bolt.owner || target.life != Life::alive || teammate) {
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
                kill(*victim, bolt.owner);
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

void GameServer::kill(Client& victim, uint32_t attacker) {
    victim.health = 0;
    victim.life = Life::dead;
    victim.respawn_tick = tick_ + uint32_t(std::lround(config_.respawn_delay * config_.tick_rate));
    ++victim.deaths;
    events_.push_back({net::CombatEvent::kill, attacker, victim.entity, 0});
    Client* killer = attacker != 0 ? find(attacker) : nullptr;
    if (killer != nullptr && killer != &victim) {
        ++killer->kills;
    }
    if (conquest_) {
        conquest_->killed(victim.team);
    }
    say(killer != nullptr ? std::format("{} killed {}", killer->name, victim.name)
                          : std::format("{} died", victim.name));
}

Vec3 GameServer::spawn_point(uint32_t index) const {
    Vec3 spawn = world_.clamp(config_.spawn + Vec3{config_.spawn_spacing * (index % 8), 0, 0});
    const double terrain = world_.terrain().height_at(spawn.x, spawn.z);
    spawn.y = world_.ground_at(spawn.x, spawn.z, terrain, config_.soldier.move.step_up).height;
    return spawn;
}

void GameServer::respawn(Client& client) {
    client.life = Life::alive;
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
