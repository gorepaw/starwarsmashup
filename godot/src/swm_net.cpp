#include "swm_net.h"

#include <exception>
#include <string>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace swm::godot_bindings {

namespace {

std::string to_std(const godot::String& text) {
    return text.utf8().get_data();
}

godot::Vector3 to_godot(core::Vec3 v) {
    return {v.x, v.y, v.z};
}

godot::Dictionary to_dictionary(const core::MoveState& state) {
    godot::Dictionary out;
    out["position"] = to_godot(state.position);
    out["velocity"] = to_godot(state.velocity);
    out["yaw"] = double(state.yaw);
    out["grounded"] = state.grounded;
    return out;
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

double SwmClient::now() const {
    return seconds_since(start_time_);
}

void SwmClient::set_simulated_network(double latency_ms, double jitter_ms, double loss) {
    impairment_.latency = latency_ms / 1000;
    impairment_.jitter = jitter_ms / 1000;
    impairment_.loss = loss;
}

godot::Error SwmClient::start(const godot::String& server, const godot::String& bake_prefix,
                              const godot::String& name) {
    const auto endpoint = core::net::parse_endpoint(to_std(server));
    if (!endpoint) {
        godot::UtilityFunctions::push_error("SwmClient: bad server address ", server);
        return godot::ERR_INVALID_PARAMETER;
    }
    client_.reset();  // it refers to the ground and link replaced below
    impaired_.reset();
    try {
        ground_ = core::load_bake(to_std(bake_prefix));
        udp_ = std::make_unique<core::net::UdpLink>();
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("SwmClient: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    core::net::Link* link = udp_.get();
    if (impairment_.latency > 0 || impairment_.jitter > 0 || impairment_.loss > 0) {
        impaired_ = std::make_unique<core::net::ImpairedLink>(*udp_, impairment_, [this] { return now(); });
        link = impaired_.get();
    }
    client_ = std::make_unique<core::GameClient>(*link, ground_,
                                                 core::ClientConfig{.server = *endpoint, .name = to_std(name)});
    return godot::OK;
}

void SwmClient::set_input(double forward, double right, double yaw, bool sprint, bool jump) {
    if (client_) {
        client_->set_input(forward, right, yaw, sprint, jump);
    }
}

void SwmClient::update() {
    if (client_) {
        client_->update(now());
    }
}

void SwmClient::leave() {
    if (client_) {
        client_->disconnect();
    }
}

godot::String SwmClient::get_status() const {
    if (!client_) {
        return "stopped";
    }
    switch (client_->status()) {
    case core::GameClient::Status::connecting:
        return "connecting";
    case core::GameClient::Status::connected:
        return "connected";
    case core::GameClient::Status::timed_out:
        return "timed_out";
    case core::GameClient::Status::disconnected:
        return "disconnected";
    }
    return "";
}

bool SwmClient::is_spawned() const {
    return client_ && client_->spawned();
}

int64_t SwmClient::get_entity_id() const {
    return client_ ? client_->entity_id() : 0;
}

godot::Dictionary SwmClient::get_presented() const {
    return client_ ? to_dictionary(client_->presented()) : godot::Dictionary();
}

godot::Array SwmClient::get_remotes() const {
    godot::Array out;
    if (client_) {
        for (const core::GameClient::Remote& remote : client_->remotes()) {
            godot::Dictionary entry = to_dictionary(remote.state);
            entry["id"] = int64_t(remote.id);
            out.push_back(entry);
        }
    }
    return out;
}

godot::Dictionary SwmClient::get_stats() const {
    godot::Dictionary out;
    if (client_) {
        const core::GameClient::Stats& stats = client_->stats();
        out["ticks"] = int64_t(stats.ticks);
        out["snapshots"] = int64_t(stats.snapshots);
        out["corrections"] = int64_t(stats.corrections);
        out["max_correction"] = stats.max_correction;
        out["rtt_ms"] = stats.rtt * 1000;
    }
    return out;
}

double SwmClient::get_ground_height(double x, double z) const {
    return ground_.height_at(x, z);
}

void SwmClient::_bind_methods() {
    using godot::ClassDB;
    using godot::D_METHOD;
    ClassDB::bind_method(D_METHOD("set_simulated_network", "latency_ms", "jitter_ms", "loss"),
                         &SwmClient::set_simulated_network);
    ClassDB::bind_method(D_METHOD("start", "server", "bake_prefix", "name"), &SwmClient::start);
    ClassDB::bind_method(D_METHOD("set_input", "forward", "right", "yaw", "sprint", "jump"), &SwmClient::set_input);
    ClassDB::bind_method(D_METHOD("update"), &SwmClient::update);
    ClassDB::bind_method(D_METHOD("leave"), &SwmClient::leave);
    ClassDB::bind_method(D_METHOD("get_status"), &SwmClient::get_status);
    ClassDB::bind_method(D_METHOD("is_spawned"), &SwmClient::is_spawned);
    ClassDB::bind_method(D_METHOD("get_entity_id"), &SwmClient::get_entity_id);
    ClassDB::bind_method(D_METHOD("get_presented"), &SwmClient::get_presented);
    ClassDB::bind_method(D_METHOD("get_remotes"), &SwmClient::get_remotes);
    ClassDB::bind_method(D_METHOD("get_stats"), &SwmClient::get_stats);
    ClassDB::bind_method(D_METHOD("get_ground_height", "x", "z"), &SwmClient::get_ground_height);
}

godot::Error SwmServer::start(int64_t port, const godot::String& bake_prefix, const godot::Vector2& spawn) {
    server_.reset();  // it refers to the ground and link replaced below
    try {
        ground_ = core::load_bake(to_std(bake_prefix));
        udp_ = std::make_unique<core::net::UdpLink>(uint16_t(port));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("SwmServer: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    core::ServerConfig config;
    config.spawn = {-spawn.x, 0, spawn.y};  // Galaxies to game coordinates
    server_ = std::make_unique<core::GameServer>(*udp_, ground_, config);
    server_->log = [](const std::string& line) { godot::UtilityFunctions::print("server: ", line.c_str()); };
    return godot::OK;
}

void SwmServer::update() {
    if (server_) {
        server_->update(seconds_since(start_time_));
    }
}

int64_t SwmServer::get_client_count() const {
    return server_ ? int64_t(server_->client_count()) : 0;
}

int64_t SwmServer::get_port() const {
    return udp_ ? udp_->port() : 0;
}

void SwmServer::_bind_methods() {
    using godot::ClassDB;
    using godot::D_METHOD;
    ClassDB::bind_method(D_METHOD("start", "port", "bake_prefix", "spawn"), &SwmServer::start);
    ClassDB::bind_method(D_METHOD("update"), &SwmServer::update);
    ClassDB::bind_method(D_METHOD("get_client_count"), &SwmServer::get_client_count);
    ClassDB::bind_method(D_METHOD("get_port"), &SwmServer::get_port);
}

} // namespace swm::godot_bindings
