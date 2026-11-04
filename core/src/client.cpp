#include "swm/core/client.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "swm/core/net/bytes.h"
#include "swm/core/net/protocol.h"

namespace swm::core {

namespace {

constexpr size_t max_pending = 120;  // inputs the server hasn't acknowledged; older ones are dropped

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

float lerp_angle(float a, float b, double t) {
    double delta = std::remainder(double(b) - a, 2 * std::numbers::pi);
    return float(a + delta * t);
}

MoveState lerp_state(const MoveState& a, const MoveState& b, double t) {
    MoveState out = b;
    out.position = {lerp(a.position.x, b.position.x, t), lerp(a.position.y, b.position.y, t),
                    lerp(a.position.z, b.position.z, t)};
    out.velocity = {lerp(a.velocity.x, b.velocity.x, t), lerp(a.velocity.y, b.velocity.y, t),
                    lerp(a.velocity.z, b.velocity.z, t)};
    out.yaw = lerp_angle(a.yaw, b.yaw, t);
    return out;
}

} // namespace

GameClient::GameClient(net::Link& link, const World& world, ClientConfig config)
    : link_(link), world_(world), config_(std::move(config)) {}

void GameClient::set_input(const Controls& c) {
    next_input_.forward = quantize_axis(c.forward);
    next_input_.right = quantize_axis(c.right);
    next_input_.yaw = float(std::remainder(c.yaw, 2 * std::numbers::pi));
    next_input_.pitch = float(std::clamp(c.pitch, -1.5, 1.5));
    next_input_.buttons = uint8_t((c.sprint ? MoveInput::sprint : 0) | (c.jump ? MoveInput::jump : 0) |
                                  (c.fire ? MoveInput::fire : 0) | (c.reload ? MoveInput::reload : 0));
    next_input_.weapon = uint8_t(std::clamp(c.weapon, 0, max_weapons - 1));
}

std::vector<net::BoltState> GameClient::bolts(double now) const {
    std::vector<net::BoltState> out = bolts_;
    const double elapsed = std::max(0.0, now - bolts_time_);
    for (net::BoltState& bolt : out) {
        bolt.position += bolt.velocity * elapsed;
    }
    return out;
}

std::vector<Shot> GameClient::take_shots() {
    return std::exchange(shots_, {});
}

std::vector<net::CombatEvent> GameClient::take_events() {
    return std::exchange(events_, {});
}

void GameClient::update(double now) {
    if (!started_) {
        started_ = true;
        last_heard_ = now;
    }
    receive(now);  // also lets a delayed link deliver a Goodbye after disconnect()
    if (status_ == Status::timed_out || status_ == Status::disconnected || status_ == Status::refused) {
        return;
    }
    if (now - last_heard_ > config_.timeout) {
        status_ = Status::timed_out;
        return;
    }
    if (status_ == Status::connecting && now - last_hello_ >= config_.hello_interval) {
        link_.send(config_.server, net::encode(net::Hello{config_.name}));
        last_hello_ = now;
    }
    if (spawned_) {
        const double dt = 1 / tick_rate_;
        if (now - next_tick_time_ > 0.25) {
            next_tick_time_ = now;  // stalled: skip ahead instead of sending a burst
        }
        while (now >= next_tick_time_) {
            tick(now);
            next_tick_time_ += dt;
        }
    }
    advance_clocks(now);
}

void GameClient::disconnect() {
    if (status_ == Status::connected) {
        // Best effort: if every copy is lost, the server times the client out.
        for (int i = 0; i < 3; ++i) {
            link_.send(config_.server, net::encode(net::Goodbye{}));
        }
    }
    status_ = Status::disconnected;
}

void GameClient::receive(double now) {
    while (auto datagram = link_.receive()) {
        if (datagram->from != config_.server) {
            continue;
        }
        try {
            handle(*datagram, now);
        } catch (const net::PacketError&) {
            // Malformed or another version: ignore it.
        }
    }
}

void GameClient::handle(const net::Datagram& datagram, double now) {
    const net::Message message = net::decode(datagram.data);
    last_heard_ = now;
    if (const auto* welcome = std::get_if<net::Welcome>(&message)) {
        if (status_ == Status::connecting) {
            status_ = Status::connected;
            entity_ = welcome->entity_id;
            character_ = welcome->character_id;
            tick_rate_ = welcome->tick_rate;
        }
        return;
    }
    if (const auto* refused = std::get_if<net::Refused>(&message)) {
        if (status_ == Status::connecting) {
            status_ = Status::refused;
            refusal_ = refused->reason;
        }
        return;
    }
    const auto* snapshot = std::get_if<net::Snapshot>(&message);
    if (snapshot == nullptr || status_ != Status::connected || snapshot->server_tick <= latest_tick_) {
        return;  // snapshots can arrive late or out of order: only the newest counts
    }
    ++stats_.snapshots;
    if (latest_tick_ == 0) {
        render_tick_ = snapshot->server_tick - config_.interpolation_delay * tick_rate_;
    }
    latest_tick_ = snapshot->server_tick;
    latest_tick_time_ = now;

    bolts_.clear();
    for (const net::BoltState& bolt : snapshot->bolts) {
        if (bolt.owner != entity_) {
            bolts_.push_back(bolt);
        }
    }
    bolts_time_ = now;
    events_.insert(events_.end(), snapshot->events.begin(), snapshot->events.end());

    std::map<uint32_t, std::deque<Sample>> seen;
    for (const net::EntityState& entity : snapshot->entities) {
        if (entity.id == entity_) {
            reconcile(snapshot->last_input, entity, now);
            continue;
        }
        // Characters missing from the snapshot have left.
        auto& samples = seen[entity.id];
        if (auto it = history_.find(entity.id); it != history_.end()) {
            samples = std::move(it->second);
        }
        samples.push_back({snapshot->server_tick, entity.state, entity.health, entity.alive, entity.spawn_count});
        while (samples.size() > 2 && samples[1].tick + 1 < render_tick_) {
            samples.pop_front();
        }
    }
    history_ = std::move(seen);
}

void GameClient::reconcile(uint32_t last_input, const net::EntityState& server, double now) {
    const MoveState& server_state = server.state;
    health_ = server.health;
    alive_ = server.alive;
    if (!spawned_) {
        spawned_ = true;
        spawn_count_ = server.spawn_count;
        current_ = previous_ = server_state;
        next_tick_time_ = now;
        return;
    }
    // Drop acknowledged inputs, checking the prediction for the newest one.
    while (!pending_.empty() && pending_.front().input.sequence <= last_input) {
        const Pending& acked = pending_.front();
        if (acked.input.sequence == last_input) {
            const double error = (acked.result.position - server_state.position).length();
            // A respawn moves the character on purpose: not a misprediction.
            if (!(acked.result == server_state) && server.spawn_count == spawn_count_) {
                ++stats_.corrections;
                stats_.max_correction = std::max(stats_.max_correction, error);
            }
            const double sample = now - acked.sent;
            stats_.rtt = stats_.rtt == 0 ? sample : lerp(stats_.rtt, sample, 0.1);
        }
        pending_.pop_front();
    }
    // Replay what the server hasn't simulated yet on top of its state.
    const double dt = 1 / tick_rate_;
    MoveState state = server_state;
    for (Pending& p : pending_) {
        simulate_character(state, p.input, alive_, world_, config_.soldier, dt);  // shots were drawn already
        p.result = state;
    }
    if (server.spawn_count != spawn_count_) {
        // Respawned: appear at the spawn point at once.
        spawn_count_ = server.spawn_count;
        correction_ = {};
        previous_ = current_ = state;
        return;
    }
    // Any jump this causes is eased in rather than shown at once.
    correction_ += current_.position - state.position;
    current_ = state;
}

void GameClient::tick(double now) {
    MoveInput input = next_input_;
    input.sequence = ++sequence_;
    input.view_tick = render_tick_;
    previous_ = current_;
    if (auto shot = simulate_character(current_, input, alive_, world_, config_.soldier, 1 / tick_rate_)) {
        shots_.push_back(*shot);
    }
    pending_.push_back({input, current_, now});
    while (pending_.size() > max_pending) {
        pending_.pop_front();
    }
    ++stats_.ticks;

    net::InputBatch batch;
    const size_t first = pending_.size() > net::max_batched_inputs ? pending_.size() - net::max_batched_inputs : 0;
    for (size_t i = first; i < pending_.size(); ++i) {
        batch.inputs.push_back(pending_[i].input);
    }
    link_.send(config_.server, net::encode(batch));
}

void GameClient::advance_clocks(double now) {
    const double elapsed = last_update_ < 0 ? 0 : now - last_update_;
    last_update_ = now;
    correction_ = correction_ * std::exp(-config_.smoothing * elapsed);
    if (latest_tick_ == 0) {
        return;
    }
    // The remote clock runs at the tick rate and is steered toward
    // `interpolation_delay` behind the newest snapshot.
    render_tick_ += elapsed * tick_rate_;
    const double target =
        latest_tick_ + (now - latest_tick_time_) * tick_rate_ - config_.interpolation_delay * tick_rate_;
    const double error = target - render_tick_;
    render_tick_ = std::abs(error) > 10 ? target : render_tick_ + error * std::min(1.0, 2 * elapsed);
}

MoveState GameClient::presented() const {
    const double dt = 1 / tick_rate_;
    const double t = std::clamp(1 - (next_tick_time_ - last_update_) / dt, 0.0, 1.0);
    MoveState state = lerp_state(previous_, current_, t);
    state.position += correction_;
    return state;
}

std::vector<GameClient::Remote> GameClient::remotes() const {
    std::vector<Remote> out;
    for (const auto& [id, samples] : history_) {
        if (samples.empty()) {
            continue;
        }
        // Between the two samples around the render time; held at the ends.
        // Across a respawn, jump rather than glide.
        const Sample* shown = &samples.back();
        MoveState state = shown->state;
        if (render_tick_ <= samples.front().tick) {
            shown = &samples.front();
            state = shown->state;
        }
        for (size_t i = 0; i + 1 < samples.size(); ++i) {
            const Sample& a = samples[i];
            const Sample& b = samples[i + 1];
            if (render_tick_ >= a.tick && render_tick_ <= b.tick) {
                shown = &a;
                state = a.spawn_count == b.spawn_count
                            ? lerp_state(a.state, b.state, (render_tick_ - a.tick) / double(b.tick - a.tick))
                            : a.state;
                break;
            }
        }
        out.push_back({id, state, shown->health, shown->alive});
    }
    return out;
}

} // namespace swm::core
