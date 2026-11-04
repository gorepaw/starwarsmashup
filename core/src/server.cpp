#include "swm/core/server.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "swm/core/net/bytes.h"
#include "swm/core/net/protocol.h"

namespace swm::core {

GameServer::GameServer(net::Link& link, const Heightfield& ground, ServerConfig config)
    : link_(link), ground_(ground), config_(config) {}

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

void GameServer::update(double now) {
    if (!started_) {
        started_ = true;
        next_tick_time_ = now;
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
            Client joined;
            joined.endpoint = from;
            joined.name = hello->name;
            joined.entity = next_entity_++;
            Vec3 spawn = config_.spawn;
            spawn.x += config_.spawn_spacing * spawned_++;
            spawn = ground_.clamp(spawn);
            spawn.y = ground_.height_at(spawn.x, spawn.z);
            joined.state.position = spawn;
            clients_.push_back(joined);
            client = &clients_.back();
            say(std::format("{} joined from {} as entity {}", client->name, from.to_string(), client->entity));
        }
        // Also answers a repeated Hello whose Welcome was lost.
        send(from, net::Welcome{client->entity, config_.tick_rate, tick_});
    }
    if (client == nullptr) {
        return;  // not connected: ignore everything but Hello
    }
    client->last_heard = now;
    if (const auto* batch = std::get_if<net::InputBatch>(&message)) {
        // Batches overlap: keep only inputs newer than any already seen.
        uint32_t newest = client->queue.empty() ? client->last_input : client->queue.back().sequence;
        for (const MoveInput& input : batch->inputs) {
            if (input.sequence > newest && std::isfinite(input.yaw)) {
                client->queue.push_back(input);
                newest = input.sequence;
            }
        }
        while (client->queue.size() > config_.max_queued_inputs) {
            client->queue.pop_front();
        }
    } else if (std::holds_alternative<net::Goodbye>(message)) {
        say(std::format("{} (entity {}) left", client->name, client->entity));
        clients_.erase(clients_.begin() + (client - clients_.data()));
    }
}

void GameServer::step() {
    const double dt = 1 / config_.tick_rate;
    for (Client& client : clients_) {
        for (size_t i = 0; i < config_.max_inputs_per_tick && !client.queue.empty(); ++i) {
            const MoveInput input = client.queue.front();
            client.queue.pop_front();
            client.state = simulate_move(client.state, input, ground_, config_.move, dt);
            client.last_input = input.sequence;
        }
    }
    ++tick_;

    net::Snapshot snapshot;
    snapshot.server_tick = tick_;
    for (const Client& client : clients_) {
        snapshot.entities.push_back({client.entity, client.state});
    }
    for (const Client& client : clients_) {
        snapshot.last_input = client.last_input;
        send(client.endpoint, snapshot);
    }
}

void GameServer::drop_silent(double now) {
    std::erase_if(clients_, [&](const Client& client) {
        const bool silent = now - client.last_heard > config_.client_timeout;
        if (silent) {
            say(std::format("{} (entity {}) timed out", client.name, client.entity));
        }
        return silent;
    });
}

} // namespace swm::core
