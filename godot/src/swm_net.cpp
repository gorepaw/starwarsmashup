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
    out["energy"] = state.energy;
    out["sprinting"] = state.sprinting;
    const core::CombatState& c = state.combat;
    out["weapon"] = int64_t(c.weapon);
    out["rounds"] = int64_t(c.weapons[c.weapon].rounds);
    out["heat"] = double(c.weapons[c.weapon].heat);
    out["overheated"] = c.weapons[c.weapon].overheated;
    out["reloading"] = c.reload > 0;
    return out;
}

const char* life_name(core::Life life) {
    switch (life) {
    case core::Life::waiting:
        return "waiting";
    case core::Life::alive:
        return "alive";
    case core::Life::dead:
        return "dead";
    }
    return "";
}

// What snapshots say about an entity, besides its state.
void describe(godot::Dictionary& out, const core::net::EntityState& entity) {
    out["health"] = double(entity.health);
    out["alive"] = entity.alive();
    out["life"] = life_name(entity.life);
    out["team"] = int64_t(entity.team);
    out["soldier_class"] = int64_t(entity.soldier_class);
    out["kills"] = int64_t(entity.kills);
    out["deaths"] = int64_t(entity.deaths);
    out["captures"] = int64_t(entity.captures);
}

void add_bolt_look(godot::Dictionary& entry, const core::WeaponDef& w) {
    entry["color"] = godot::Color(float(w.bolt_color[0]), float(w.bolt_color[1]), float(w.bolt_color[2]),
                                  float(w.bolt_color[3]));
    entry["length"] = w.bolt_length;
    entry["width"] = w.bolt_width;
}

godot::Error load_battlefield_file(std::optional<core::Battlefield>& battlefield, const godot::String& path) {
    try {
        battlefield = core::load_battlefield(to_std(path));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("set_battlefield: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    return godot::OK;
}

godot::Error load_soldier_file(core::SoldierDef& soldier, const godot::String& path) {
    try {
        soldier = core::load_soldier(to_std(path));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("set_soldier: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    return godot::OK;
}

// Loads a building for SwmClient/SwmServer.add_building.
godot::Error load_building(std::vector<std::pair<core::BuildingCollision, core::Placement>>& buildings,
                           const godot::String& path, const godot::Vector3& position, double yaw_degrees) {
    try {
        buildings.emplace_back(core::load_building_collision(to_std(path)),
                               core::placement_from_galaxies(position.x, position.y, position.z, yaw_degrees));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("add_building: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    return godot::OK;
}

std::unique_ptr<core::World> make_world(const core::Heightfield& ground,
                                        const std::vector<std::pair<core::BuildingCollision, core::Placement>>& buildings) {
    auto world = std::make_unique<core::World>(ground);
    for (const auto& [collision, placement] : buildings) {
        world->add_building(collision, placement);
    }
    return world;
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

double SwmClient::now() const {
    return seconds_since(start_time_);
}

const core::SoldierDef& SwmClient::soldier() const {
    return client_ ? client_->soldier() : soldier_;
}

const core::SoldierDef& SwmClient::soldier_of(int team, int soldier_class) const {
    if (battlefield_ && team >= 0 && team < 2) {
        const auto& classes = battlefield_->teams[size_t(team)].classes;
        return classes[std::min<size_t>(size_t(soldier_class), classes.size() - 1)];
    }
    return soldier_;
}

godot::Error SwmClient::set_battlefield(const godot::String& path) {
    return load_battlefield_file(battlefield_, path);
}

void SwmClient::set_bot(bool enabled) {
    if (!enabled) {
        bot_.reset();
    } else if (!bot_) {
        bot_ = std::make_unique<core::Bot>(uint64_t(start_time_.time_since_epoch().count()));
    }
}

void SwmClient::request_spawn(int64_t soldier_class, int64_t post) {
    if (client_) {
        client_->request_spawn(int(soldier_class), int(post));
    }
}

godot::Dictionary SwmClient::get_battlefield() const {
    godot::Dictionary out;
    if (!battlefield_) {
        return out;
    }
    const core::Battlefield& field = *battlefield_;
    out["name"] = godot::String::utf8(field.name.c_str());
    out["min_x"] = field.min_x;
    out["min_z"] = field.min_z;
    out["max_x"] = field.max_x;
    out["max_z"] = field.max_z;
    godot::Array teams;
    for (const core::Team& team : field.teams) {
        godot::Array classes;
        for (const core::SoldierDef& soldier : team.classes) {
            godot::Array weapons;
            for (const core::WeaponDef& weapon : soldier.weapons) {
                weapons.push_back(godot::String::utf8(weapon.name.c_str()));
            }
            godot::Dictionary entry;
            entry["name"] = godot::String::utf8(soldier.name.c_str());
            entry["max_health"] = soldier.max_health;
            entry["weapons"] = weapons;
            classes.push_back(entry);
        }
        godot::Dictionary entry;
        entry["name"] = godot::String::utf8(team.name.c_str());
        entry["classes"] = classes;
        teams.push_back(entry);
    }
    out["teams"] = teams;
    godot::Array posts;
    for (const core::CommandPost& post : field.posts) {
        godot::Dictionary entry;
        entry["name"] = godot::String::utf8(post.name.c_str());
        core::Vec3 position = post.position;
        if (world_) {
            position.y = world_->terrain().height_at(position.x, position.z);
        }
        entry["position"] = to_godot(position);
        entry["radius"] = post.radius;
        entry["capturable"] = post.capturable;
        posts.push_back(entry);
    }
    out["posts"] = posts;
    return out;
}

godot::Dictionary SwmClient::get_match() const {
    godot::Dictionary out;
    if (!client_ || !client_->match()) {
        return out;
    }
    const core::MatchState& match = *client_->match();
    out["phase"] = match.phase == core::Phase::warmup ? "warmup" : match.phase == core::Phase::live ? "live" : "ended";
    out["time_left"] = match.time_left;
    godot::Array tickets;
    tickets.push_back(match.tickets[0]);
    tickets.push_back(match.tickets[1]);
    out["tickets"] = tickets;
    out["dominant"] = int64_t(match.dominant);
    out["victory_left"] = match.victory_left;
    out["winner"] = int64_t(match.winner);
    godot::Array posts;
    for (const core::PostState& post : match.posts) {
        godot::Dictionary entry;
        entry["owner"] = int64_t(post.owner);
        entry["holder"] = int64_t(post.holder);
        entry["charge"] = double(post.charge);
        entry["contested"] = post.contested;
        posts.push_back(entry);
    }
    out["posts"] = posts;
    return out;
}

void SwmClient::set_simulated_network(double latency_ms, double jitter_ms, double loss) {
    impairment_.latency = latency_ms / 1000;
    impairment_.jitter = jitter_ms / 1000;
    impairment_.loss = loss;
}

godot::Error SwmClient::add_building(const godot::String& collision_path, const godot::Vector3& position,
                                     double yaw_degrees) {
    return load_building(buildings_, collision_path, position, yaw_degrees);
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
        world_ = make_world(ground_, buildings_);
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
    core::ClientConfig config{.server = *endpoint, .name = to_std(name), .soldier = soldier_};
    config.battlefield = battlefield_;
    client_ = std::make_unique<core::GameClient>(*link, *world_, config);
    return godot::OK;
}

godot::Error SwmClient::set_soldier(const godot::String& path) {
    return load_soldier_file(soldier_, path);
}

void SwmClient::set_input(const godot::Dictionary& c) {
    if (!client_) {
        return;
    }
    core::GameClient::Controls controls;
    controls.forward = double(c.get("forward", 0.0));
    controls.right = double(c.get("right", 0.0));
    controls.yaw = double(c.get("yaw", 0.0));
    controls.pitch = double(c.get("pitch", 0.0));
    controls.sprint = bool(c.get("sprint", false));
    controls.jump = bool(c.get("jump", false));
    controls.fire = bool(c.get("fire", false));
    controls.reload = bool(c.get("reload", false));
    controls.weapon = int(int64_t(c.get("weapon", 0)));
    client_->set_input(controls);
}

void SwmClient::update() {
    if (client_) {
        if (bot_) {
            bot_->update(*client_, *world_, now());
        }
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
    case core::GameClient::Status::refused:
        return "refused";
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

int64_t SwmClient::get_character_id() const {
    return client_ ? int64_t(client_->character_id()) : 0;
}

godot::String SwmClient::get_refusal() const {
    return client_ ? godot::String::utf8(client_->refusal().c_str()) : godot::String();
}

godot::Dictionary SwmClient::get_presented() const {
    if (!client_) {
        return {};
    }
    godot::Dictionary out = to_dictionary(client_->presented());
    describe(out, client_->self());
    return out;
}

godot::Array SwmClient::get_bolts() const {
    godot::Array out;
    if (client_) {
        const auto remotes = client_->remotes();
        for (const core::net::BoltState& bolt : client_->bolts(now())) {
            godot::Dictionary entry;
            entry["owner"] = int64_t(bolt.owner);
            entry["weapon"] = int64_t(bolt.weapon);
            entry["position"] = to_godot(bolt.position);
            entry["velocity"] = to_godot(bolt.velocity);
            int team = -1, soldier_class = 0;
            for (const auto& remote : remotes) {
                if (remote.id == bolt.owner) {
                    team = remote.team;
                    soldier_class = remote.soldier_class;
                }
            }
            const core::SoldierDef& owner = soldier_of(team, soldier_class);
            add_bolt_look(entry, owner.weapons[std::min<size_t>(bolt.weapon, owner.weapons.size() - 1)]);
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::take_shots() {
    godot::Array out;
    if (client_) {
        const core::SoldierDef& own = soldier();
        for (const core::Shot& shot : client_->take_shots()) {
            godot::Dictionary entry;
            entry["origin"] = to_godot(shot.origin);
            godot::Array directions;
            for (const core::Vec3& direction : shot.directions) {
                directions.push_back(to_godot(direction));
            }
            entry["directions"] = directions;
            entry["weapon"] = int64_t(shot.weapon);
            add_bolt_look(entry, own.weapons[std::min<size_t>(size_t(shot.weapon), own.weapons.size() - 1)]);
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::take_events() {
    godot::Array out;
    if (client_) {
        for (const core::net::CombatEvent& event : client_->take_events()) {
            godot::Dictionary entry;
            switch (event.kind) {
            case core::net::CombatEvent::hit:
                entry["kind"] = "hit";
                break;
            case core::net::CombatEvent::kill:
                entry["kind"] = "kill";
                break;
            case core::net::CombatEvent::captured:
                entry["kind"] = "captured";
                break;
            case core::net::CombatEvent::neutralized:
                entry["kind"] = "neutralized";
                break;
            }
            entry["attacker"] = int64_t(event.attacker);
            entry["victim"] = int64_t(event.victim);
            entry["damage"] = double(event.damage);
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::get_weapons() const {
    godot::Array out;
    for (const core::WeaponDef& w : soldier().weapons) {
        godot::Dictionary entry;
        entry["name"] = godot::String::utf8(w.name.c_str());
        entry["rounds_per_clip"] = int64_t(w.rounds_per_clip);
        entry["heat_per_shot"] = w.heat_per_shot;
        entry["bolt_speed"] = w.bolt_speed;
        entry["bolt_life"] = w.bolt_life;
        entry["bolt_color"] = godot::Color(float(w.bolt_color[0]), float(w.bolt_color[1]), float(w.bolt_color[2]),
                                           float(w.bolt_color[3]));
        entry["bolt_length"] = w.bolt_length;
        entry["bolt_width"] = w.bolt_width;
        out.push_back(entry);
    }
    return out;
}

double SwmClient::get_max_health() const {
    return soldier().max_health;
}

godot::Array SwmClient::get_remotes() const {
    godot::Array out;
    if (client_) {
        for (const core::GameClient::Remote& remote : client_->remotes()) {
            godot::Dictionary entry = to_dictionary(remote.state);
            entry["id"] = int64_t(remote.id);
            describe(entry, remote);
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
    return ground_.height_at(x, z);  // terrain only, for keeping a camera above it
}

void SwmClient::_bind_methods() {
    using godot::ClassDB;
    using godot::D_METHOD;
    ClassDB::bind_method(D_METHOD("set_simulated_network", "latency_ms", "jitter_ms", "loss"),
                         &SwmClient::set_simulated_network);
    ClassDB::bind_method(D_METHOD("add_building", "collision_path", "position", "yaw_degrees"),
                         &SwmClient::add_building);
    ClassDB::bind_method(D_METHOD("start", "server", "bake_prefix", "name"), &SwmClient::start);
    ClassDB::bind_method(D_METHOD("set_soldier", "path"), &SwmClient::set_soldier);
    ClassDB::bind_method(D_METHOD("set_battlefield", "path"), &SwmClient::set_battlefield);
    ClassDB::bind_method(D_METHOD("set_bot", "enabled"), &SwmClient::set_bot);
    ClassDB::bind_method(D_METHOD("get_battlefield"), &SwmClient::get_battlefield);
    ClassDB::bind_method(D_METHOD("get_match"), &SwmClient::get_match);
    ClassDB::bind_method(D_METHOD("request_spawn", "soldier_class", "post"), &SwmClient::request_spawn);
    ClassDB::bind_method(D_METHOD("set_input", "controls"), &SwmClient::set_input);
    ClassDB::bind_method(D_METHOD("get_bolts"), &SwmClient::get_bolts);
    ClassDB::bind_method(D_METHOD("take_shots"), &SwmClient::take_shots);
    ClassDB::bind_method(D_METHOD("take_events"), &SwmClient::take_events);
    ClassDB::bind_method(D_METHOD("get_weapons"), &SwmClient::get_weapons);
    ClassDB::bind_method(D_METHOD("get_max_health"), &SwmClient::get_max_health);
    ClassDB::bind_method(D_METHOD("update"), &SwmClient::update);
    ClassDB::bind_method(D_METHOD("leave"), &SwmClient::leave);
    ClassDB::bind_method(D_METHOD("get_status"), &SwmClient::get_status);
    ClassDB::bind_method(D_METHOD("is_spawned"), &SwmClient::is_spawned);
    ClassDB::bind_method(D_METHOD("get_entity_id"), &SwmClient::get_entity_id);
    ClassDB::bind_method(D_METHOD("get_character_id"), &SwmClient::get_character_id);
    ClassDB::bind_method(D_METHOD("get_refusal"), &SwmClient::get_refusal);
    ClassDB::bind_method(D_METHOD("get_presented"), &SwmClient::get_presented);
    ClassDB::bind_method(D_METHOD("get_remotes"), &SwmClient::get_remotes);
    ClassDB::bind_method(D_METHOD("get_stats"), &SwmClient::get_stats);
    ClassDB::bind_method(D_METHOD("get_ground_height", "x", "z"), &SwmClient::get_ground_height);
}

SwmServer::~SwmServer() {
    stop();
}

void SwmServer::set_database(const godot::String& path) {
    database_ = to_std(path);
}

godot::Error SwmServer::add_building(const godot::String& collision_path, const godot::Vector3& position,
                                     double yaw_degrees) {
    return load_building(buildings_, collision_path, position, yaw_degrees);
}

godot::Error SwmServer::set_soldier(const godot::String& path) {
    return load_soldier_file(soldier_, path);
}

godot::Error SwmServer::set_battlefield(const godot::String& path) {
    return load_battlefield_file(battlefield_, path);
}

void SwmServer::set_bots(int64_t count) {
    bot_count_ = int(std::clamp<int64_t>(count, 0, 64));
}

godot::Error SwmServer::start(int64_t port, const godot::String& bake_prefix, const godot::Vector2& spawn) {
    stop();  // the server refers to the ground, store and link replaced below
    try {
        ground_ = core::load_bake(to_std(bake_prefix));
        world_ = make_world(ground_, buildings_);
        if (!database_.empty()) {
            store_ = std::make_unique<core::SqliteCharacterStore>(database_);
        }
        udp_ = std::make_unique<core::net::UdpLink>(uint16_t(port));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("SwmServer: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    core::ServerConfig config;
    config.soldier = soldier_;
    config.battlefield = battlefield_;
    config.spawn = {-spawn.x, 0, spawn.y};  // Galaxies to game coordinates
    server_ = std::make_unique<core::GameServer>(*udp_, *world_, config, store_.get());
    server_->log = [](const std::string& line) { godot::UtilityFunctions::print("server: ", line.c_str()); };
    if (bot_count_ > 0) {
        core::ClientConfig bot_config;
        bot_config.server = *core::net::parse_endpoint("127.0.0.1:" + std::to_string(udp_->port()));
        bot_config.soldier = soldier_;
        bot_config.battlefield = battlefield_;
        try {
            bots_ = std::make_unique<core::BotPlayers>(bot_count_, *world_, bot_config);
        } catch (const std::exception& error) {
            godot::UtilityFunctions::push_error("SwmServer: bots: ", error.what());
        }
    }
    return godot::OK;
}

void SwmServer::update() {
    if (server_) {
        const double now = seconds_since(start_time_);
        server_->update(now);
        if (bots_) {
            bots_->update(now);
        }
    }
}

void SwmServer::stop() {
    if (bots_) {
        bots_->disconnect();
    }
    bots_.reset();
    if (server_) {
        server_->save_all();
    }
    server_.reset();
    store_.reset();
    udp_.reset();
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
    ClassDB::bind_method(D_METHOD("set_database", "path"), &SwmServer::set_database);
    ClassDB::bind_method(D_METHOD("set_soldier", "path"), &SwmServer::set_soldier);
    ClassDB::bind_method(D_METHOD("set_battlefield", "path"), &SwmServer::set_battlefield);
    ClassDB::bind_method(D_METHOD("set_bots", "count"), &SwmServer::set_bots);
    ClassDB::bind_method(D_METHOD("add_building", "collision_path", "position", "yaw_degrees"),
                         &SwmServer::add_building);
    ClassDB::bind_method(D_METHOD("start", "port", "bake_prefix", "spawn"), &SwmServer::start);
    ClassDB::bind_method(D_METHOD("update"), &SwmServer::update);
    ClassDB::bind_method(D_METHOD("stop"), &SwmServer::stop);
    ClassDB::bind_method(D_METHOD("get_client_count"), &SwmServer::get_client_count);
    ClassDB::bind_method(D_METHOD("get_port"), &SwmServer::get_port);
}

} // namespace swm::godot_bindings
