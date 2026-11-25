#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "swm/core/hub.h"
#include "swm/core/server.h"

namespace swm::core {

class CharacterStore;

// A planet (Stage 13): its terrain bake and its hub file (a city and its
// other towns, each the hub's).
struct PlanetConfig {
    std::string name;             // "Tatooine", as characters' locations name it
    std::filesystem::path bake;   // the terrain bake's prefix (imported_assets/terrain/naboo)
    std::filesystem::path hub;    // its hub file (game/hubs/theed.ini)
    std::string stem() const { return bake.filename().string(); }  // "naboo": what clients load
};

struct GalaxyConfig {
    std::vector<PlanetConfig> planets;  // the first is the home planet
    // Starship fares between planets, by their names (lowercase), both ways
    // (Galaxies' datatables/travel/travel.iff). A pair with none has no route.
    std::map<std::pair<std::string, std::string>, int64_t> fares;
    int64_t fare(const std::string& from, const std::string& to) const;  // 0: no route
    const PlanetConfig* find(const std::string& name) const;              // by name, ignoring case
};

// Reads game/hubs/galaxy.ini: [planet] Name, Bake (relative to the file), Hub
// (likewise), the home planet first; [fare] From, To, Credits. Throws
// std::runtime_error if it can't be read or names no planet.
GalaxyConfig load_galaxy(const std::filesystem::path& path);

// The planets other than the home one, each on a world server of its own
// with its hub, in this process (as a hub's battles are), and the starports
// between them all. A starport sells passage to the other planets'
// starports ("travel_offworld" services: Detail "<planet>|<fare>"); the
// galaxy has the destination expect the player there and transfers them
// (net::Transfer::planet). A character saved on another planet who joins one
// is sent on to theirs.
class Galaxy {
public:
    // `home`: the process's own server (on `home_port`), the first planet's,
    // whose hub has already given it its services. `base`: the settings each
    // other planet's server starts from (catalog, loadout, professions...).
    // Throws std::runtime_error if a planet's world or hub can't be loaded.
    Galaxy(GameServer& home, uint16_t home_port, GalaxyConfig config, const ServerConfig& base, CharacterStore* store,
           HubNetwork& network, std::function<void(const std::string&)> log = {});
    ~Galaxy();
    Galaxy(const Galaxy&) = delete;
    Galaxy& operator=(const Galaxy&) = delete;

    // Runs the other planets (their servers, then their hubs). The home
    // planet's server and hub are its owner's to run.
    void update(double now);

    size_t planet_count() const { return planets_.size(); }
    // A planet's server and port, by name (ignoring case); null/0 if none.
    GameServer* server(const std::string& planet) const;
    uint16_t port(const std::string& planet) const;
    const Hub* hub(const std::string& planet) const;

private:
    struct Planet {
        PlanetConfig config;
        std::shared_ptr<const World> world;
        std::unique_ptr<net::Link> link;
        std::unique_ptr<GameServer> owned;
        std::unique_ptr<Hub> hub;
        GameServer* server = nullptr;  // owned, or the home one
        uint16_t port = 0;
    };
    Planet* find(const std::string& name) const;
    void travel(Planet& from, uint32_t entity, const std::string& destination);
    bool redirect(Planet& from, uint32_t entity, const std::string& location);
    void say(const std::string& text) const;

    GalaxyConfig config_;
    std::vector<std::unique_ptr<Planet>> planets_;  // the home one first
    std::function<void(const std::string&)> log_;
};

} // namespace swm::core
