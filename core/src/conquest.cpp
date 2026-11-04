#include "swm/core/conquest.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "swm/core/ini.h"

namespace swm::core {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); });
}

// Galaxies "x z" to game space.
Vec3 galaxies_point(const std::string& text, const std::string& what) {
    std::istringstream fields(text);
    double x = 0, z = 0;
    if (!(fields >> x >> z)) {
        throw std::runtime_error(what + ": expected \"x z\", got \"" + text + "\"");
    }
    return {-x, 0, z};
}

} // namespace

bool CommandPost::contains(Vec3 point) const {
    const double dx = point.x - position.x, dz = point.z - position.z;
    return dx * dx + dz * dz <= radius * radius;
}

bool Battlefield::contains(Vec3 point) const {
    return point.x >= min_x && point.x <= max_x && point.z >= min_z && point.z <= max_z;
}

Vec3 Battlefield::center() const {
    return {(min_x + max_x) / 2, 0, (min_z + max_z) / 2};
}

Battlefield load_battlefield(const std::filesystem::path& path) {
    const Ini ini = Ini::load(path);
    const std::string where = path.string();
    Battlefield field;
    const Ini::Section* main = ini.section("battlefield");
    if (main == nullptr) {
        throw std::runtime_error(where + ": no [battlefield] section");
    }
    field.name = main->get("Name").value_or(path.stem().string());
    if (const auto bounds = main->get("Bounds")) {
        std::istringstream fields(*bounds);
        double x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        if (!(fields >> x0 >> z0 >> x1 >> z1) || x0 >= x1 || z0 >= z1) {
            throw std::runtime_error(where + ": Bounds is \"min_x min_z max_x max_z\"");
        }
        field.min_x = -x1;  // Galaxies x runs the other way
        field.max_x = -x0;
        field.min_z = z0;
        field.max_z = z1;
    }
    field.tickets = main->number("Tickets", field.tickets);
    field.warmup = main->number("Warmup", field.warmup);
    field.results = main->number("Results", field.results);
    field.victory_time = main->number("VictoryTime", field.victory_time);
    field.bleed = main->number("Bleed", field.bleed);
    field.out_of_bounds_time = main->number("OutOfBoundsTime", field.out_of_bounds_time);
    field.min_players = size_t(std::max(1.0, main->number("MinPlayers", double(field.min_players))));

    size_t team_count = 0;
    for (const Ini::Section& s : ini.sections()) {
        if (s.name == "team") {
            if (team_count == field.teams.size()) {
                throw std::runtime_error(where + ": more than two teams");
            }
            Team& team = field.teams[team_count++];
            team.name = s.get("Name").value_or("Team " + std::to_string(team_count));
            for (const auto& [key, value] : s.values) {
                if (iequals(key, "Class")) {
                    const std::filesystem::path file = path.parent_path() / value;
                    if (!std::filesystem::exists(file)) {
                        throw std::runtime_error(where + ": class file " + file.string() +
                                                 " is missing (export it with bf2tool export-soldier)");
                    }
                    team.classes.push_back(load_soldier(file));
                }
            }
            if (team.classes.empty()) {
                throw std::runtime_error(where + ": team " + team.name + " has no Class");
            }
        }
    }
    if (team_count != field.teams.size()) {
        throw std::runtime_error(where + ": needs two [team] sections");
    }
    for (const Ini::Section& s : ini.sections()) {
        if (s.name != "post") {
            continue;
        }
        CommandPost post;
        post.name = s.get("Name").value_or("Post " + std::to_string(field.posts.size() + 1));
        post.position = galaxies_point(s.get("Position").value_or(""), where + ": post " + post.name);
        post.radius = s.number("Radius", post.radius);
        post.neutralize_time = std::max(0.1, s.number("NeutralizeTime", post.neutralize_time));
        post.capture_time = std::max(0.1, s.number("CaptureTime", post.capture_time));
        post.spawn_radius = s.number("SpawnRadius", post.spawn_radius);
        post.capturable = s.number("Capturable", 1) != 0;
        if (const auto owner = s.get("Owner"); owner && !owner->empty()) {
            for (size_t t = 0; t < field.teams.size(); ++t) {
                if (iequals(*owner, field.teams[t].name)) {
                    post.owner = int(t);
                }
            }
            if (post.owner < 0) {
                throw std::runtime_error(where + ": post " + post.name + " has unknown Owner " + *owner);
            }
        }
        field.posts.push_back(post);
    }
    if (field.posts.empty()) {
        throw std::runtime_error(where + ": no [post] sections");
    }
    for (size_t t = 0; t < field.teams.size(); ++t) {
        if (std::none_of(field.posts.begin(), field.posts.end(), [&](const CommandPost& p) { return p.owner == int(t); })) {
            throw std::runtime_error(where + ": team " + field.teams[t].name + " starts with no post to spawn at");
        }
    }
    return field;
}

Conquest::Conquest(const Battlefield& battlefield) : battlefield_(battlefield) {
    reset();
    state_.time_left = battlefield_.warmup;
}

void Conquest::reset() {
    state_.tickets = {battlefield_.tickets, battlefield_.tickets};
    state_.dominant = -1;
    state_.victory_left = 0;
    state_.winner = -1;
    state_.posts.clear();
    for (const CommandPost& post : battlefield_.posts) {
        PostState s;
        s.owner = s.holder = int8_t(post.owner);
        s.charge = post.owner >= 0 ? 1.0f : 0.0f;
        state_.posts.push_back(s);
    }
}

bool Conquest::can_spawn(int team, size_t post) const {
    return state_.phase != Phase::ended && team >= 0 && team < 2 && post < state_.posts.size() &&
           state_.posts[post].owner == team && (state_.phase != Phase::live || state_.tickets[size_t(team)] > 0);
}

void Conquest::killed(int team) {
    if (state_.phase == Phase::live && team >= 0 && team < 2) {
        state_.tickets[size_t(team)] = std::max(0.0, state_.tickets[size_t(team)] - 1);
    }
}

std::vector<MatchEvent> Conquest::step(double dt, std::span<const Unit> units, size_t players) {
    std::vector<MatchEvent> events;
    switch (state_.phase) {
    case Phase::warmup:
        if (players < battlefield_.min_players) {
            state_.time_left = battlefield_.warmup;  // waiting for players
            break;
        }
        state_.time_left -= dt;
        if (state_.time_left <= 0) {
            reset();
            state_.phase = Phase::live;
            state_.time_left = 0;
            events.push_back({MatchEvent::went_live});
        }
        break;
    case Phase::live: {
        capture(dt, units, events);
        std::array<int, 2> held{};
        for (const PostState& post : state_.posts) {
            if (post.owner >= 0) {
                ++held[size_t(post.owner)];
            }
        }
        // The team holding more posts bleeds the other's reinforcements.
        // (Ours: Battlefront's bleed comes from per-map scripts.)
        const int lead = held[0] - held[1];
        if (lead != 0) {
            double& tickets = state_.tickets[lead > 0 ? 1 : 0];
            tickets = std::max(0.0, tickets - battlefield_.bleed * std::abs(lead) * dt);
        }
        if (state_.tickets[0] <= 0 || state_.tickets[1] <= 0) {
            const int winner = state_.tickets[0] > 0 ? 0 : state_.tickets[1] > 0 ? 1 : -1;
            finish(winner, events);
            break;
        }
        // Holding every post starts the countdown to victory.
        const int total = int(state_.posts.size());
        const int dominant = held[0] == total ? 0 : held[1] == total ? 1 : -1;
        if (dominant != state_.dominant) {
            state_.dominant = int8_t(dominant);
            state_.victory_left = battlefield_.victory_time;
        }
        if (dominant >= 0) {
            state_.victory_left -= dt;
            if (state_.victory_left <= 0) {
                finish(dominant, events);
            }
        }
        break;
    }
    case Phase::ended:
        state_.time_left -= dt;
        if (state_.time_left <= 0) {
            reset();
            state_.phase = Phase::warmup;
            state_.time_left = battlefield_.warmup;
            events.push_back({MatchEvent::restarted});
        }
        break;
    }
    return events;
}

void Conquest::finish(int winner, std::vector<MatchEvent>& events) {
    state_.phase = Phase::ended;
    state_.winner = int8_t(winner);
    state_.dominant = -1;
    state_.victory_left = 0;
    state_.time_left = battlefield_.results;
    events.push_back({MatchEvent::ended, winner});
}

void Conquest::capture(double dt, std::span<const Unit> units, std::vector<MatchEvent>& events) {
    for (size_t i = 0; i < state_.posts.size(); ++i) {
        const CommandPost& def = battlefield_.posts[i];
        PostState& post = state_.posts[i];
        std::array<int, 2> present{};
        for (const Unit& unit : units) {
            if (unit.team >= 0 && unit.team < 2 && def.contains(unit.position)) {
                ++present[size_t(unit.team)];
            }
        }
        post.contested = present[0] > 0 && present[1] > 0;
        if (!def.capturable || post.contested || (present[0] == 0 && present[1] == 0)) {
            continue;  // a post nobody is in keeps its charge
        }
        const int team = present[0] > 0 ? 0 : 1;
        const double soldiers = std::min(present[size_t(team)], max_capturing_soldiers);
        if (post.owner == team) {
            post.charge = float(std::min(1.0, post.charge + soldiers * dt / def.neutralize_time));  // repairing
        } else if (post.owner >= 0) {
            post.charge = float(post.charge - soldiers * dt / def.neutralize_time);
            if (post.charge <= 0) {
                post.owner = post.holder = -1;
                post.charge = 0;
                events.push_back({MatchEvent::neutralized, team, int(i)});
            }
        } else if (post.holder >= 0 && post.holder != team) {
            post.charge = float(post.charge - soldiers * dt / def.capture_time);  // undoing the other team's capture
            if (post.charge <= 0) {
                post.holder = -1;
                post.charge = 0;
            }
        } else {
            post.holder = int8_t(team);
            post.charge = float(post.charge + soldiers * dt / def.capture_time);
            if (post.charge >= 1) {
                post.owner = int8_t(team);
                post.charge = 1;
                events.push_back({MatchEvent::captured, team, int(i)});
            }
        }
    }
}

} // namespace swm::core
