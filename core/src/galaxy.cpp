#include "swm/core/galaxy.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <stdexcept>

#include "swm/core/ini.h"
#include "swm/core/world.h"

namespace swm::core {

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

} // namespace

int64_t GalaxyConfig::fare(const std::string& from, const std::string& to) const {
    const auto it = fares.find({lower(from), lower(to)});
    return it == fares.end() ? 0 : it->second;
}

const PlanetConfig* GalaxyConfig::find(const std::string& name) const {
    for (const PlanetConfig& p : planets) {
        if (lower(p.name) == lower(name)) {
            return &p;
        }
    }
    return nullptr;
}

GalaxyConfig load_galaxy(const std::filesystem::path& path) {
    const Ini ini = Ini::load(path);
    const std::string where = path.string();
    GalaxyConfig galaxy;
    for (const Ini::Section& s : ini.sections()) {
        if (s.name == "planet") {
            PlanetConfig p;
            p.name = s.get("Name").value_or("");
            const auto bake = s.get("Bake");
            const auto hub = s.get("Hub");
            if (p.name.empty() || !bake || !hub) {
                throw std::runtime_error(where + ": a [planet] needs Name, Bake and Hub");
            }
            p.bake = path.parent_path() / *bake;
            p.hub = path.parent_path() / *hub;
            galaxy.planets.push_back(std::move(p));
        } else if (s.name == "fare") {
            const std::string from = lower(s.get("From").value_or(""));
            const std::string to = lower(s.get("To").value_or(""));
            const auto credits = int64_t(s.number("Credits", 0));
            if (from.empty() || to.empty() || credits <= 0) {
                throw std::runtime_error(where + ": a [fare] needs From, To and Credits");
            }
            galaxy.fares[{from, to}] = credits;
            galaxy.fares[{to, from}] = credits;
        }
    }
    if (galaxy.planets.empty()) {
        throw std::runtime_error(where + ": no [planet]");
    }
    return galaxy;
}

Galaxy::Galaxy(GameServer& home, uint16_t home_port, GalaxyConfig config, const ServerConfig& base,
               CharacterStore* store, HubNetwork& network, std::function<void(const std::string&)> log)
    : config_(std::move(config)), log_(std::move(log)) {
    for (size_t i = 0; i < config_.planets.size(); ++i) {
        auto planet = std::make_unique<Planet>();
        planet->config = config_.planets[i];
        if (i == 0) {
            planet->server = &home;
            planet->port = home_port;
        } else {
            HubConfig hub = load_hub(planet->config.hub);
            hub.planet = planet->config.stem();
            hub.bot_skill = BotSkill::preset("hard");
            planet->world = load_world(planet->config.bake);
            ServerConfig server_config = base;
            server_config.planet = planet->config.name;
            server_config.battlefield.reset();
            if (hub.spawn) {
                server_config.spawn = *hub.spawn;
            }
            planet->link = network.open_server(planet->port);
            planet->owned = std::make_unique<GameServer>(*planet->link, *planet->world, server_config, store);
            planet->server = planet->owned.get();
            planet->server->log = [this, name = planet->config.name](const std::string& line) {
                say(name + ": " + line);
            };
            say(std::format("{}: hub {} on port {}, {} town(s)", planet->config.name, hub.name, planet->port,
                            hub.towns.size()));
            planet->hub = std::make_unique<Hub>(*planet->server, planet->port, *planet->world, std::move(hub), network);
            planet->hub->log = planet->server->log;
        }
        planets_.push_back(std::move(planet));
    }
    // Every planet's starports, offered at every other planet's that has a route there.
    for (const auto& to : planets_) {
        std::vector<net::Vendor> starports;
        for (const net::Vendor& v : to->server->vendors()) {
            if (v.kind == "starport") {
                starports.push_back(v);
            }
        }
        for (const auto& from : planets_) {
            const int64_t fare = config_.fare(from->config.name, to->config.name);
            if (from == to || fare <= 0 || starports.empty()) {
                continue;
            }
            std::vector<net::Vendor> offered;
            for (const net::Vendor& v : starports) {
                // Out of everyone's reach here (another planet's place): only its name and the fare matter.
                offered.push_back({v.name, "travel_offworld", v.position, 0, std::format("{}|{}", to->config.name, fare)});
            }
            from->server->add_vendors(std::move(offered));
        }
    }
    for (const auto& planet : planets_) {
        Planet* p = planet.get();
        p->server->on_offworld_travel = [this, p](uint32_t entity, const std::string& destination) {
            travel(*p, entity, destination);
        };
        p->server->on_wrong_planet = [this, p](uint32_t entity, const std::string& location) {
            return redirect(*p, entity, location);
        };
    }
}

Galaxy::~Galaxy() {
    for (const auto& planet : planets_) {
        planet->server->on_offworld_travel = nullptr;
        planet->server->on_wrong_planet = nullptr;
    }
    // Hubs before their servers (members are destroyed in reverse, but
    // planets_ holds both in one struct: make the order explicit).
    for (const auto& planet : planets_) {
        planet->hub.reset();
    }
}

void Galaxy::update(double now) {
    for (const auto& planet : planets_) {
        if (planet->owned) {
            planet->owned->update(now);
            planet->hub->update(now);
        }
    }
}

Galaxy::Planet* Galaxy::find(const std::string& name) const {
    const std::string wanted = lower(name);
    for (const auto& planet : planets_) {
        if (lower(planet->config.name) == wanted) {
            return planet.get();
        }
    }
    return nullptr;
}

GameServer* Galaxy::server(const std::string& planet) const {
    const Planet* p = find(planet);
    return p ? p->server : nullptr;
}

uint16_t Galaxy::port(const std::string& planet) const {
    const Planet* p = find(planet);
    return p ? p->port : 0;
}

const Hub* Galaxy::hub(const std::string& planet) const {
    const Planet* p = find(planet);
    return p ? p->hub.get() : nullptr;
}

void Galaxy::travel(Planet& from, uint32_t entity, const std::string& destination) {
    for (const auto& to : planets_) {
        if (to.get() == &from) {
            continue;
        }
        for (const net::Vendor& v : to->server->vendors()) {
            if (v.kind == "starport" && v.name == destination) {
                const std::string name = from.server->name_of(entity);
                to->server->expect_arrival(name, v.position);
                from.server->transfer(entity, net::Transfer{0, to->port, 0, "", to->config.stem()});
                say(std::format("{} flies from {} to {} ({})", name, from.config.name, to->config.name, destination));
                return;
            }
        }
    }
    say(std::format("no starport named {} to fly to", destination));
}

bool Galaxy::redirect(Planet& from, uint32_t entity, const std::string& location) {
    Planet* to = find(location);
    if (to == nullptr || to == &from) {
        return false;  // not a planet (an old save's town name): they stay
    }
    const std::string name = from.server->name_of(entity);
    if (const MoveState* state = from.server->state_of(entity)) {
        // Where they were saved there; its height is this world's, so let theirs find the ground.
        to->server->expect_arrival(name, {state->position.x, -1e9, state->position.z});
    }
    from.server->transfer(entity, net::Transfer{0, to->port, 0, "", to->config.stem()});
    say(std::format("{} was last on {}: sending them there", name, to->config.name));
    return true;
}

void Galaxy::say(const std::string& text) const {
    if (log_) {
        log_(text);
    }
}

} // namespace swm::core
