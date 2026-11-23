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
    if (const Client* client = find(entity)) {
        return &client->state;
    }
    const auto npc = std::find_if(npcs_.begin(), npcs_.end(), [&](const Npc& n) { return n.entity == entity; });
    return npc == npcs_.end() ? nullptr : &npc->state;
}

uint32_t GameServer::add_npc(const std::string& name, Vec3 position, double yaw, uint8_t look, uint8_t pose,
                             uint16_t colors, double radius) {
    constexpr double lift = 1;  // from a meter over where it's put (as spawns are)
    const MoveParams params;
    Npc npc;
    npc.entity = next_entity_++;
    npc.name = name;
    npc.look = look;
    npc.pose = pose;
    npc.colors = colors;
    npc.radius = radius;
    npc.state.position = world_.clamp(position);
    npc.state.position.y = world_.support_at(npc.state.position.x, npc.state.position.z,
                                             position.y + lift, params.step_up).height;
    if (npc.state.position.y < -1e8) {
        npc.state.position.y = world_.terrain().height_at(npc.state.position.x, npc.state.position.z);
    }
    npc.state.yaw = float(yaw);
    npc.state.grounded = true;
    npc.input.yaw = float(yaw);
    npcs_.push_back(npc);
    return npc.entity;
}

void GameServer::steer_npc(uint32_t entity, const MoveInput& input) {
    for (Npc& npc : npcs_) {
        if (npc.entity == entity) {
            npc.input = input;
            npc.input.buttons &= MoveInput::sprint | MoveInput::jump;  // they don't fight
        }
    }
}

std::vector<uint32_t> GameServer::npcs() const {
    std::vector<uint32_t> ids;
    for (const Npc& npc : npcs_) {
        ids.push_back(npc.entity);
    }
    return ids;
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

std::optional<PlayerProfile> GameServer::profile_of(uint32_t entity) const {
    const Client* client = find(entity);
    if (client == nullptr) {
        return std::nullopt;
    }
    return PlayerProfile{client->species, client->gender, client->appearance};
}

void GameServer::set_player_profile(uint32_t entity, const PlayerProfile& profile) {
    Client* client = find(entity);
    if (client == nullptr) {
        return;
    }
    client->species = profile.species;
    client->gender = profile.gender;
    if (!profile.appearance.empty()) {
        client->appearance = profile.appearance;
    }
    client->stats = stats_for(loadout_of(*client), client->species, config_.base_stats);
    if (client->life == Life::waiting || client->health <= 0 || client->health > client->stats.max_health) {
        client->health = client->stats.max_health;
    }
}

net::EntityState GameServer::describe(const Client& c) const {
    net::EntityState out{c.entity, c.state, float(c.health), c.life, c.spawn_count, int8_t(c.team),
                         uint8_t(c.loadout), c.kills, c.deaths, c.captures};
    out.species = uint8_t(c.species);
    out.gender = uint8_t(c.gender);
    out.name = c.name;
    out.appearance = c.appearance;
    return out;
}

const Loadout& GameServer::loadout_of(const Client& client) const {
    if (config_.battlefield && client.team >= 0) {
        const auto& loadouts = config_.battlefield->teams[size_t(client.team)].loadouts;
        return loadouts[std::min(client.loadout, loadouts.size() - 1)];
    }
    return config_.loadout;
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
        send(from, net::Welcome{client->entity, config_.tick_rate, tick_, client->character, recruiters_, factions_,
                                looks_});
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
        joined.stats = stats_for(loadout_of(joined), joined.species, config_.base_stats);
        reset_loadout(joined.state, loadout_of(joined), joined.stats);
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
                joined.species = species_from_name(saved->species).value_or(Species::human);
                joined.gender = gender_from_name(saved->gender).value_or(Gender::male);
                if (!saved->location.empty()) {
                    joined.location = saved->location;
                }
                if (!saved->appearance.empty()) {
                    joined.appearance = saved->appearance;
                }
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
    joined.stats = stats_for(config_.loadout, joined.species, config_.base_stats);
    spawn.y = world_.ground_at(spawn.x, spawn.z, spawn.y, joined.stats.move.step_up).height;
    joined.state.position = spawn;
    reset_loadout(joined.state, config_.loadout, joined.stats);
    joined.health = joined.stats.max_health;
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
        records.push_back({client.character, client.name, client.state.position, client.state.yaw,
                           species_def(client.species).id, client.gender == Gender::female ? "female" : "male",
                           client.location, client.appearance});
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
            const auto shot = simulate_character(client.state, input, client.life == Life::alive, world_,
                                                 loadout_of(client), client.stats, dt);
            if (shot) {
                fire(client, *shot, input.view_tick);
            }
        }
    }
    // The server's own people, by how they're steered.
    if (!npcs_.empty()) {
        const CharacterStats stats = stats_for(config_.loadout, config_.base_stats);
        CharacterStats own = stats;  // a creature's body
        for (Npc& npc : npcs_) {
            own.move.radius = npc.radius > 0 ? npc.radius : stats.move.radius;
            simulate_character(npc.state, npc.input, true, world_, config_.loadout, own, dt);
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
    advance_pickups();

    net::Snapshot snapshot;
    snapshot.server_tick = tick_;
    for (const Client& client : clients_) {
        snapshot.entities.push_back(describe(client));
    }
    for (const Npc& npc : npcs_) {
        net::EntityState entity;
        entity.id = npc.entity;
        entity.state = npc.state;
        entity.health = float(config_.base_stats.max_health);
        entity.look = npc.look;
        entity.pose = npc.pose;
        entity.colors = npc.colors;
        snapshot.entities.push_back(entity);
    }
    for (const Bolt& bolt : bolts_) {
        snapshot.bolts.push_back({bolt.id, bolt.owner, bolt.weapon, bolt.def->projectile, bolt.position, bolt.velocity});
    }
    snapshot.events = std::move(events_);
    events_.clear();
    snapshot.explosions = std::move(explosions_);
    explosions_.clear();
    snapshot.impacts = std::move(impacts_);
    impacts_.clear();
    for (const Pickup& pickup : pickups_) {
        snapshot.pickups.push_back({pickup.id, pickup.kind, pickup.position, float(pickup.life)});
    }
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

// How far below the lowest terrain a falling soldier dies.
constexpr double fall_depth = 50;

void GameServer::run_match() {
    const double dt = 1 / config_.tick_rate;
    const Battlefield& field = conquest_->battlefield();
    for (Client& client : clients_) {
        if (client.life == Life::dead && tick_ >= client.respawn_tick) {
            return_to_waiting(client);
        }
        // So do death regions, at once, and falling through a hole in the
        // terrain with nothing below.
        if (client.life == Life::alive && (conquest_->deadly(client.state.position) ||
                                           client.state.position.y < world_.terrain().lowest() - fall_depth)) {
            kill(client, 0);
            continue;
        }
        // Damage regions hurt so much a second, armor or not (Mustafar's
        // lava, at once).
        if (client.life == Life::alive) {
            if (const double rate = field.damage_rate(client.state.position); rate > 0) {
                client.health = std::max(0.0, client.health - rate * dt);
                if (client.health <= 0) {
                    kill(client, 0);
                    continue;
                }
            }
        }
        // Health and ammo droids: every so often (each its own delay), to
        // whoever stands by them (while the droid is intact).
        if (client.life == Life::alive) {
            for (size_t i = 0; i < field.powerups.size(); ++i) {
                if (!conquest_->powerup_alive(i)) {
                    continue;
                }
                const PowerupStation& station = field.powerups[i];
                const double now = tick_ * dt / station.delay;
                if (std::floor(now) == std::floor(now - dt / station.delay) || !station.reaches(client.state.position)) {
                    continue;
                }
                client.health = std::min(client.stats.max_health, client.health + station.health);
                resupply(client.state, loadout_of(client), station.ammo);
            }
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
    const auto match_events = conquest_->step(dt, units, clients_.size());
    for (size_t i = 0; i < field.destructibles.size(); ++i) {
        world_.set_destroyed(i, !conquest_->destructible_alive(i));
    }
    for (const MatchEvent& event : match_events) {
        switch (event.kind) {
        case MatchEvent::went_live:
            say("the match is live");
            bolts_.clear();
            pickups_.clear();
            for (Client& client : clients_) {
                client.kills = client.deaths = client.captures = 0;
                return_to_waiting(client);
            }
            break;
        case MatchEvent::restarted:
            say("warmup for the next match");
            pickups_.clear();
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
        const size_t loadout = client.wants_spawn->loadout;
        const size_t post = client.wants_spawn->post;
        if (loadout < field.teams[size_t(client.team)].loadouts.size() && conquest_->can_spawn(client.team, post)) {
            spawn_at_post(client, loadout, post);
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

void GameServer::spawn_at_post(Client& client, size_t loadout_index, size_t post_index) {
    const Battlefield& field = conquest_->battlefield();
    const CommandPost& post = field.posts[post_index];
    // At the post's spawn points in turn, or spread around it (golden-angle
    // steps); facing away from it.
    const uint32_t slot = next_spawn_slot_++;
    const double angle = double(slot) * 2.399963;
    Vec3 position = post.spawn_points.empty()
                        ? post.position + Vec3{std::cos(angle), 0, std::sin(angle)} * post.spawn_radius
                        : post.spawn_points[slot % post.spawn_points.size()];
    position.x = std::clamp(position.x, field.min_x, field.max_x);
    position.z = std::clamp(position.z, field.min_z, field.max_z);
    position = world_.clamp(position);
    // On the surface at the spawn point's height, if it has one; else on
    // the terrain (or what stands on it).
    const double from = post.spawn_heights && !post.spawn_points.empty()
                            ? position.y
                            : world_.terrain().height_at(position.x, position.z);
    client.loadout = loadout_index;
    const Loadout& loadout = loadout_of(client);
    client.stats = stats_for(loadout, client.species, config_.base_stats);
    // (A spawn point over a seam in the floor, one of Mygeeto's CP2's: on
    // the floor both sides of it.) A spawn point a little under its floor
    // (one of Utapau's, a meter down) is on it: looking from a meter up.
    constexpr double spawn_lift = 1;
    position.y = world_.support_at(position.x, position.z, from + spawn_lift, client.stats.move.step_up).height;

    client.state = MoveState{};
    client.state.position = position;
    client.state.yaw = float(std::atan2(-(position.x - post.position.x), -(position.z - post.position.z)));
    reset_loadout(client.state, loadout, client.stats);
    client.health = client.stats.max_health;
    client.life = Life::alive;
    client.outside = 0;
    client.history.clear();
    client.wants_spawn.reset();
    ++client.spawn_count;
    say(std::format("{} spawned with {} at {}", client.name, loadout.name, post.name));
}

void GameServer::fire(const Client& shooter, const Shot& shot, double view_tick) {
    const WeaponDef* def = weapon_in_slot(loadout_of(shooter), shot.weapon);
    if (def == nullptr) {
        return;
    }
    const WeaponDef& weapon = *def;
    // The tick being simulated is tick_ + 1; the shooter saw others at view_tick.
    const double rewind = std::clamp(double(tick_ + 1) - view_tick, 0.0, config_.max_rewind * config_.tick_rate);
    for (const Vec3& direction : shot.directions) {
        Bolt bolt;
        bolt.id = next_bolt_++;
        bolt.owner = shooter.entity;
        bolt.team = shooter.team;
        bolt.weapon = uint8_t(shot.weapon);
        bolt.position = shot.origin;
        bolt.def = def;
        bolt.velocity = direction * weapon.bolt_speed;
        // A grenade timed from where it lands flies at most this long first.
        constexpr double longest_flight = 10;
        const bool timed = weapon.projectile == Projectile::grenade && !weapon.explode_on_impact;
        bolt.life = !timed ? weapon.bolt_life : weapon.timer_on_contact ? longest_flight : weapon.fuse;
        bolt.gravity = weapon.bolt_gravity * shooter.stats.move.gravity;
        bolt.rewind = rewind;
        bolts_.push_back(bolt);
    }
}

std::optional<World::RayHit> GameServer::shot_hit(Vec3 a, Vec3 b) const {
    auto hit = world_.raycast_hit(a, b);
    if (conquest_) {
        if (const auto t = conquest_->door_hit(a, b); t && (!hit || *t < hit->t)) {
            const Vec3 d = b - a;
            const double length = d.length();
            hit = World::RayHit{*t, length > 1e-9 ? d * (-1.0 / length) : Vec3{0, 1, 0}};
        }
    }
    return hit;
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
    // Explosions damage the bolt list's owners, so collect them first.
    std::vector<Blast> blasts;
    std::erase_if(bolts_, [&](Bolt& bolt) {
        const WeaponDef& def = *bolt.def;
        bolt.life -= dt;
        if (bolt.resting) {
            if (bolt.life <= 0) {
                blasts.push_back({bolt, bolt.position, {0, 1, 0}, false});
                return true;
            }
            return false;
        }
        const Vec3 from = bolt.position;
        bolt.velocity.y -= bolt.gravity * dt;
        if (def.projectile == Projectile::missile && def.bolt_acceleration > 0) {
            const double speed = bolt.velocity.length();
            if (speed > 0) {
                bolt.velocity += bolt.velocity * (def.bolt_acceleration * dt / speed);
            }
        }
        const Vec3 to = from + bolt.velocity * dt;
        // The first thing along the way: the world (shut doors too), someone
        // where the shooter saw them, or a living health/ammo droid.
        // Bouncing grenades pass people by.
        const auto world_hit = shot_hit(from, to);
        std::optional<double> nearest = world_hit ? std::optional(world_hit->t) : std::nullopt;
        Vec3 normal = world_hit ? world_hit->normal : Vec3{0, 1, 0};
        Client* victim = nullptr;
        double victim_top = 0;  // the top of the victim's body (its head's), when hit
        int hit_powerup = -1;
        const bool bounces = def.projectile == Projectile::grenade && !def.explode_on_impact;
        for (Client& target : clients_) {
            const bool teammate = bolt.team >= 0 && target.team == bolt.team && !config_.friendly_fire;
            if (bounces || harmless || target.entity == bolt.owner || target.life != Life::alive || teammate) {
                continue;
            }
            // Its posture now (not rewound: ASSUMPTION, close enough).
            const Vec3 feet = position_at(target, double(tick_) - bolt.rewind);
            const double height = target.state.crouched ? crouched_hit_height : hit_height;
            const auto t = segment_hits_cylinder(from, to, feet, hit_radius, height);
            if (t && (!nearest || *t < *nearest)) {
                nearest = t;
                victim = &target;
                victim_top = feet.y + height;
                const double speed = bolt.velocity.length();
                normal = speed > 1e-6 ? bolt.velocity * (-1.0 / speed) : Vec3{0, 1, 0};
            }
        }
        if (conquest_ && !harmless && !bounces) {
            const auto& stations = conquest_->battlefield().powerups;
            for (size_t i = 0; i < stations.size(); ++i) {
                if (!conquest_->powerup_alive(i)) {
                    continue;
                }
                const auto t = segment_hits_cylinder(from, to, stations[i].position, PowerupStation::hit_radius,
                                                     PowerupStation::hit_height);
                if (t && (!nearest || *t < *nearest)) {
                    nearest = t;
                    victim = nullptr;
                    hit_powerup = int(i);
                    const double speed = bolt.velocity.length();
                    normal = speed > 1e-6 ? bolt.velocity * (-1.0 / speed) : Vec3{0, 1, 0};
                }
            }
            // A droid's own exported collision mesh may catch the ray slightly
            // ahead of its cylinder hitbox; attribute world hits inside its padded volume.
            if (victim == nullptr && hit_powerup < 0 && nearest) {
                const Vec3 at = from + (to - from) * *nearest;
                constexpr double pad_r = PowerupStation::hit_radius + 0.25;
                for (size_t i = 0; i < stations.size(); ++i) {
                    if (!conquest_->powerup_alive(i)) {
                        continue;
                    }
                    const Vec3 d = at - stations[i].position;
                    if (d.x * d.x + d.z * d.z <= pad_r * pad_r && d.y >= -0.25 &&
                        d.y <= PowerupStation::hit_height + 0.25) {
                        hit_powerup = int(i);
                        break;
                    }
                }
            }
        }
        if (victim != nullptr) {
            const Vec3 at = from + (to - from) * *nearest;
            // Less after a long flight; double in the head.
            const bool headshot = at.y >= victim_top - head_size;
            const double flown = def.bolt_life - bolt.life;
            damage(*victim, bolt_damage(def, flown) * (headshot ? headshot_multiplier : 1.0), def.damage_type,
                   bolt.owner);
            if (def.explosion_damage > 0 && def.explosion_outer > 0) {
                blasts.push_back({bolt, at, normal, false});
            } else {
                impacts_.push_back({bolt.owner, bolt.weapon, at, normal, true});
            }
            return true;
        }
        if (hit_powerup >= 0) {
            const Vec3 at = from + (to - from) * *nearest;
            damage_powerup(size_t(hit_powerup), def.damage, bolt.owner, blasts);
            if (def.explosion_damage > 0 && def.explosion_outer > 0) {
                blasts.push_back({bolt, at, normal, false});
            } else {
                impacts_.push_back({bolt.owner, bolt.weapon, at, normal, false});
            }
            return true;
        }
        if (nearest) {
            const Vec3 at = from + (to - from) * *nearest;
            if (!bounces && !harmless && world_hit && world_hit->t == *nearest && world_hit->destructible >= 0) {
                damage_destructible(size_t(world_hit->destructible), bolt_damage(def, def.bolt_life - bolt.life),
                                    bolt.owner, blasts);
            }
            if (!bounces) {
                if (def.explosion_damage > 0 && def.explosion_outer > 0) {
                    blasts.push_back({bolt, at, normal, false});
                } else {
                    impacts_.push_back({bolt.owner, bolt.weapon, at, normal, false});
                }
                return true;
            }
            // A bounce off the surface it hit: back out of it with Rebound
            // of the speed it came in with, along it losing some to Friction
            // (ASSUMPTION: kept 1 / (1 + Friction / 4) of it). It comes to
            // rest on ground once slow. Its timer may start now.
            bolt.position = from + (to - from) * std::max(0.0, *nearest - 0.05);
            const double into = bolt.velocity.dot(normal);
            const Vec3 across = bolt.velocity - normal * into;
            bolt.velocity = across * (1 / (1 + def.friction / 4)) - normal * (into * def.rebound);
            if (!bolt.touched && def.timer_on_contact) {
                bolt.life = def.fuse;
            }
            bolt.touched = true;
            if (normal.y > 0.7 && bolt.velocity.length() < 1.5) {
                bolt.resting = true;
                bolt.velocity = {};
            }
        } else {
            bolt.position = to;
        }
        if (bolt.life <= 0) {
            if (bounces) {
                blasts.push_back({bolt, bolt.position, {0, 1, 0}, false});  // the fuse ran out
            }
            return true;
        }
        return false;
    });
    if (!harmless) {
        for (size_t i = 0; i < blasts.size(); ++i) {
            const Blast blast = blasts[i];
            explode(blast, blasts);
        }
    }
}

void GameServer::damage_powerup(size_t index, double amount, uint32_t attacker, std::vector<Blast>& blasts) {
    if (!conquest_ || !conquest_->powerup_alive(index) || amount <= 0) {
        return;
    }
    events_.push_back({net::CombatEvent::hit, attacker, 0, float(amount)});
    if (conquest_->damage_powerup(index, amount)) {
        Blast blast;
        blast.bolt.owner = attacker;
        blast.bolt.team = -1;
        blast.bolt.weapon = net::droid_explosion_weapon;
        blast.at = conquest_->battlefield().powerups[index].position + Vec3{0, 0.7, 0};
        blast.normal = {0, 1, 0};
        blast.droid = true;
        blasts.push_back(blast);
    }
}

void GameServer::damage_destructible(size_t index, double amount, uint32_t attacker, std::vector<Blast>& blasts) {
    if (!conquest_ || !conquest_->destructible_alive(index) || amount <= 0) {
        return;
    }
    if (conquest_->damage_destructible(index, amount)) {
        const Destructible& d = conquest_->battlefield().destructibles[index];
        world_.set_destroyed(index, true);
        say(std::format("{} destroyed", d.name));
        Blast blast;
        blast.bolt.owner = attacker;
        blast.bolt.team = -1;
        blast.bolt.weapon = net::object_explosion_weapon;
        blast.at = d.center;
        blast.normal = {0, 1, 0};
        blast.destructible = int(index);
        blasts.push_back(blast);
    }
}

// Damage to everyone near: teammates are spared (unless friendly fire is
// on), the thrower isn't (ASSUMPTION: Battlefront hurts the thrower too).
void GameServer::explode(const Blast& blast, std::vector<Blast>& blasts) {
    const Bolt& bolt = blast.bolt;
    WeaponDef droid_def;
    if (blast.droid) {
        // Battlefront's com_inf_rechargedroid_exp: small burst when a droid blows up.
        droid_def.explosion_damage = 20.0;
        droid_def.explosion_inner = 1.25;
        droid_def.explosion_outer = 2.5;
    } else if (blast.destructible >= 0 && conquest_) {
        // A destroyed object's own explosion, out from its middle.
        const Destructible& d = conquest_->battlefield().destructibles[size_t(blast.destructible)];
        droid_def.explosion_damage = d.explosion_damage;
        droid_def.explosion_inner = d.explosion_inner;
        droid_def.explosion_outer = d.explosion_outer;
    }
    const WeaponDef& def = blast.droid || blast.destructible >= 0 ? droid_def : *bolt.def;
    if (def.explosion_damage <= 0 || def.explosion_outer <= 0) {
        return;
    }
    explosions_.push_back({bolt.owner, bolt.weapon, blast.at, blast.normal, float(def.explosion_outer)});
    // From a little off the surface it went off at: walls shelter whoever
    // it can't see (chest or head).
    const Vec3 center = blast.at + blast.normal * 0.1;
    const auto sheltered = [&](Vec3 feet) {
        return shot_hit(center, feet + Vec3{0, 1.0, 0}) && shot_hit(center, feet + Vec3{0, 1.6, 0});
    };
    for (Client& target : clients_) {
        const bool teammate = !blast.droid && bolt.team >= 0 && target.team == bolt.team &&
                              target.entity != bolt.owner && !config_.friendly_fire;
        if (target.life != Life::alive) {
            continue;
        }
        const Vec3 target_pos = blast.droid ? target.state.position : position_at(target, double(tick_) - bolt.rewind);
        if (sheltered(target_pos)) {
            continue;
        }
        // Thrown away from it (teammates too: a blast doesn't take sides).
        const double push = explosion_push(def, blast.at, target.state.position);
        if (push > 0) {
            Vec3 away = target.state.position + Vec3{0, 1.0, 0} - blast.at;
            away = away.length() > 1e-3 ? away * (1 / away.length()) : Vec3{0, 1, 0};
            away.y = std::max(away.y, 0.35);  // and off its feet
            target.state.velocity += away * (push / away.length());
            target.state.grounded = false;
        }
        if (teammate) {
            continue;
        }
        const double amount = explosion_damage(def, blast.at, target_pos);
        if (amount > 0) {
            damage(target, amount, DamageType::explosive, bolt.owner);
        }
    }
    if (conquest_) {
        const auto& stations = conquest_->battlefield().powerups;
        for (size_t i = 0; i < stations.size(); ++i) {
            if (!conquest_->powerup_alive(i)) {
                continue;
            }
            const double amount = explosion_damage(def, blast.at, stations[i].position);
            if (amount > 0 && !sheltered(stations[i].position)) {
                damage_powerup(i, amount, bolt.owner, blasts);
            }
        }
        // Objects whose bounds it reaches (not its own: that's gone).
        // ASSUMPTION: full damage within the inner radius of the nearest
        // point of its bounds, falling to nothing at the outer, unsheltered.
        const auto& objects = conquest_->battlefield().destructibles;
        for (size_t i = 0; i < objects.size(); ++i) {
            if (int(i) == blast.destructible || !conquest_->destructible_alive(i)) {
                continue;
            }
            const double distance = (objects[i].nearest(blast.at) - blast.at).length();
            const double amount =
                distance <= def.explosion_inner ? def.explosion_damage
                : distance >= def.explosion_outer
                    ? 0.0
                    : def.explosion_damage * (def.explosion_outer - distance) /
                          (def.explosion_outer - def.explosion_inner);
            if (amount > 0) {
                damage_destructible(i, amount, bolt.owner, blasts);
            }
        }
    }
}

// Damage after the victim's mitigation (its armor, mostly).
void GameServer::damage(Client& victim, double raw, DamageType type, uint32_t attacker) {
    const double amount = mitigated_damage(victim.stats, raw, type);
    if (victim.life != Life::alive || amount <= 0) {
        return;
    }
    victim.health = std::max(0.0, victim.health - amount);
    events_.push_back({net::CombatEvent::hit, attacker, victim.entity, float(amount)});
    if (victim.health <= 0) {
        kill(victim, attacker);
    }
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
        drop_pickup(victim.state.position);
    }
    say(killer != nullptr ? std::format("{} killed {}", killer->name, victim.name)
                          : std::format("{} died", victim.name));
}

double GameServer::random_unit() {
    uint64_t z = (rng_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return double(z >> 11) / double(1ull << 53);
}

void GameServer::spawn_pickup(PickupKind kind, Vec3 position) {
    constexpr size_t max_pickups = 64;
    if (pickups_.size() >= max_pickups) {
        pickups_.erase(pickups_.begin());
    }
    pickups_.push_back({next_pickup_++, kind, position, pickup_def(kind).lifespan});
}

void GameServer::drop_pickup(Vec3 at) {
    const auto kind = roll_pickup(random_unit());
    if (!kind) {
        return;
    }
    // Rest on the floor right under where the soldier fell; nothing dropped
    // over a chasm, in lava, or out of bounds.
    const double ground = world_.ground_at(at.x, at.z, at.y + 0.5, 0).height;
    if (ground < -1e8 || ground > at.y + 1.0 || at.y - ground > 3.0) {
        return;
    }
    const Vec3 landed{at.x, ground, at.z};
    if (conquest_) {
        const Battlefield& field = conquest_->battlefield();
        if (!field.contains(landed) || field.harmful(landed)) {
            return;
        }
    }
    spawn_pickup(*kind, landed);
}

void GameServer::advance_pickups() {
    if (pickups_.empty()) {
        return;
    }
    const double dt = 1 / config_.tick_rate;
    constexpr double reach2 = pickup_radius * pickup_radius;
    constexpr double vertical_reach = 1.5;
    std::erase_if(pickups_, [&](Pickup& pickup) {
        pickup.life -= dt;
        if (pickup.life <= 0) {
            return true;
        }
        const PickupDef& def = pickup_def(pickup.kind);
        for (Client& client : clients_) {
            if (client.life != Life::alive) {
                continue;
            }
            const double dx = client.state.position.x - pickup.position.x;
            const double dz = client.state.position.z - pickup.position.z;
            if (dx * dx + dz * dz > reach2 || std::abs(client.state.position.y - pickup.position.y) > vertical_reach) {
                continue;
            }
            // Only consumed if the soldier can actually use some of it:
            // walking over a health pack at full health leaves it for someone
            // else, as in Battlefront.
            bool used = false;
            if (def.health > 0 && client.health < client.stats.max_health) {
                client.health = std::min(client.stats.max_health, client.health + def.health);
                used = true;
            }
            if (def.ammo_clips > 0 && resupply_clips(client.state, loadout_of(client), def.ammo_clips)) {
                used = true;
            }
            if (def.energy > 0 && client.state.energy < client.stats.move.max_energy) {
                client.state.energy = std::min(client.stats.move.max_energy, client.state.energy + def.energy);
                used = true;
            }
            if (used) {
                return true;
            }
        }
        return false;
    });
}

Vec3 GameServer::spawn_point(uint32_t index) const {
    Vec3 spawn = world_.clamp(config_.spawn + Vec3{config_.spawn_spacing * (index % 8), 0, 0});
    const double terrain = world_.terrain().height_at(spawn.x, spawn.z);
    spawn.y = world_.ground_at(spawn.x, spawn.z, terrain, config_.base_stats.move.step_up).height;
    return spawn;
}

void GameServer::respawn(Client& client) {
    client.life = Life::alive;
    client.health = client.stats.max_health;
    const float yaw = client.state.yaw;
    client.state = MoveState{};
    client.state.position = spawn_point(client.entity - 1);
    client.state.yaw = yaw;
    reset_loadout(client.state, config_.loadout, client.stats);
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
