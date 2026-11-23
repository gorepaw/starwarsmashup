#include "swm/core/hub.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>
#include <format>
#include <sstream>
#include <stdexcept>

#include "swm/core/ini.h"
#include "swm/core/net/udp.h"
#include "swm/core/world.h"

namespace swm::core {

namespace {

bool same_name(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); });
}

std::string trimmed(std::string_view text) {
    const size_t first = text.find_first_not_of(" \t");
    const size_t last = text.find_last_not_of(" \t");
    return first == std::string_view::npos ? std::string() : std::string(text.substr(first, last - first + 1));
}

} // namespace

std::array<int, 2> teams_for_sides(const Battlefield& field, const std::array<std::string, 2>& sides) {
    std::array<int, 2> teams{};
    for (size_t side = 0; side < 2; ++side) {
        const auto it = std::find_if(field.teams.begin(), field.teams.end(),
                                     [&](const Team& team) { return same_name(team.name, sides[side]); });
        if (it == field.teams.end()) {
            throw std::runtime_error(std::format("{} has no team named {}", field.name, sides[side]));
        }
        teams[side] = int(it - field.teams.begin());
    }
    if (teams[0] == teams[1]) {
        throw std::runtime_error(std::format("{}: both sides are its {}", field.name, sides[0]));
    }
    return teams;
}

HubConfig load_hub(const std::filesystem::path& path) {
    const Ini ini = Ini::load(path);
    const std::string where = path.string();
    const Ini::Section* main = ini.section("hub");
    if (main == nullptr) {
        throw std::runtime_error(where + ": no [hub] section");
    }
    HubConfig hub;
    hub.name = main->get("Name").value_or(path.stem().string());
    if (const auto spawn = main->get("Spawn")) {
        std::istringstream fields(*spawn);
        double x = 0, z = 0;
        if (!(fields >> x >> z)) {
            throw std::runtime_error(where + ": Spawn needs x z");
        }
        hub.spawn = Vec3{-x, 0, z};
    }
    if (const auto cloning = main->get("CloningFacility")) {
        std::istringstream fields(*cloning);
        double x = 0, z = 0;
        if (!(fields >> x >> z)) {
            throw std::runtime_error(where + ": CloningFacility needs x z");
        }
        hub.cloning_facility = Vec3{-x, 0, z};
    }
    for (const auto& [key, value] : main->values) {
        if (!same_name(key, "Battlefield")) {
            continue;
        }
        const std::filesystem::path battlefield_path = path.parent_path() / value;
        hub.battlefields.push_back({battlefield_path.stem().string(), load_battlefield(battlefield_path)});
    }
    if (hub.battlefields.empty()) {
        throw std::runtime_error(where + ": no Battlefield");
    }
    // Sides = Rebellion, Empire; by default the first battlefield's teams.
    if (const auto sides = main->get("Sides")) {
        const size_t comma = sides->find(',');
        if (comma == std::string::npos) {
            throw std::runtime_error(where + ": Sides needs two names, separated by a comma");
        }
        hub.sides = {trimmed(std::string_view(*sides).substr(0, comma)),
                     trimmed(std::string_view(*sides).substr(comma + 1))};
    } else {
        hub.sides = {hub.battlefields[0].field.teams[0].name, hub.battlefields[0].field.teams[1].name};
    }
    for (HubBattlefield& battlefield : hub.battlefields) {
        try {
            battlefield.teams = teams_for_sides(battlefield.field, hub.sides);
        } catch (const std::runtime_error& error) {
            throw std::runtime_error(where + ": " + error.what());
        }
    }
    hub.team_size = std::clamp(int(main->number("TeamSize", hub.team_size)), 1, 32);
    hub.queue_time = std::max(0.0, main->number("QueueTime", hub.queue_time));
    hub.return_delay = std::max(0.0, main->number("ReturnDelay", hub.return_delay));
    for (const Ini::Section& s : ini.sections()) {
        if (s.name != "recruiter") {
            continue;
        }
        net::Recruiter recruiter;
        recruiter.name = s.get("Name").value_or("War Terminal");
        std::istringstream fields(s.get("Position").value_or(""));
        double x = 0, z = 0;
        if (!(fields >> x >> z)) {
            throw std::runtime_error(where + ": recruiter " + recruiter.name + " needs Position = x z");
        }
        recruiter.position = {-x, 0, z};  // Galaxies to game coordinates
        recruiter.radius = s.number("Radius", recruiter.radius);
        hub.recruiters.push_back(recruiter);
    }
    if (hub.recruiters.empty()) {
        throw std::runtime_error(where + ": no [recruiter] sections");
    }
    // Its people. A look per template, in the order they come.
    const auto look_of = [&](const std::string& template_name) {
        const auto it = std::find(hub.looks.begin(), hub.looks.end(), template_name);
        if (it != hub.looks.end()) {
            return int(it - hub.looks.begin());
        }
        if (hub.looks.size() >= 255) {
            throw std::runtime_error(where + ": more than 255 kinds of people");
        }
        hub.looks.push_back(template_name);
        return int(hub.looks.size() - 1);
    };
    const auto point = [&](std::string_view text, const std::string& what) {
        std::istringstream fields{std::string(text)};
        double x = 0, z = 0;
        if (!(fields >> x >> z)) {
            throw std::runtime_error(where + ": " + what + " needs x z");
        }
        return Vec3{-x, 0, z};  // Galaxies to game coordinates
    };
    for (const Ini::Section& s : ini.sections()) {
        if (s.name == "npc") {
            HubPerson person;
            const auto template_name = s.get("Template");
            if (!template_name) {
                throw std::runtime_error(where + ": an [npc] needs a Template");
            }
            person.look = look_of(*template_name);
            person.name = s.get("Name").value_or("Citizen");
            person.position = point(s.get("Position").value_or(""), "an [npc]'s Position");
            // Galaxies' yaw faces +Z at 0 and turns toward +X; the game's faces -Z.
            person.yaw = std::numbers::pi - s.number("Yaw", 0) * std::numbers::pi / 180;
            person.pose = same_name(s.get("Pose").value_or("stand"), "sit") ? 1 : 0;
            person.radius = std::clamp(s.number("Radius", 0), 0.0, 5.0);
            if (const auto route = s.get("Route")) {
                std::string_view rest = *route;
                while (!rest.empty()) {
                    const size_t semicolon = rest.find(';');
                    const std::string_view part = rest.substr(0, semicolon);
                    if (part.find_first_not_of(" \t") != std::string_view::npos) {
                        person.route.push_back(point(part, "a Route's point"));
                    }
                    rest = semicolon == std::string_view::npos ? std::string_view() : rest.substr(semicolon + 1);
                }
            }
            hub.people.push_back(std::move(person));
        } else if (s.name == "quartermaster") {
            net::Quartermaster qm;
            qm.name = s.get("Name").value_or("Quartermaster");
            qm.faction = s.get("Faction").value_or("neutral");
            qm.position = point(s.get("Position").value_or(""), "a [quartermaster]'s Position");
            qm.radius = s.number("Radius", qm.radius);
            hub.quartermasters.push_back(std::move(qm));
        } else if (s.name == "target") {
            HubRangeTarget rt;
            rt.name = s.get("Name").value_or("Target");
            rt.position = point(s.get("Position").value_or(""), "a [target]'s Position");
            rt.patrol_end = s.get("Patrol") ? point(*s.get("Patrol"), "a [target]'s Patrol") : rt.position;
            rt.speed = s.number("Speed", 0.0);
            rt.health = s.number("Health", 400.0);
            rt.mitigation = s.number("Mitigation", 0.0);
            rt.respawn = s.number("Respawn", 2.5);
            hub.range_targets.push_back(std::move(rt));
        } else if (s.name == "crowd") {
            hub.crowd.count = std::clamp(int(s.number("Count", 0)), 0, 200);
            for (const auto& [key, value] : s.values) {
                if (same_name(key, "Template")) {
                    hub.crowd.looks.push_back(look_of(value));
                }
            }
            std::istringstream area(s.get("Area").value_or(""));
            double x = 0, z = 0, radius = 0;
            if (hub.crowd.count > 0 && (!(area >> x >> z >> radius) || radius <= 0 || hub.crowd.looks.empty())) {
                throw std::runtime_error(where + ": a [crowd] needs Area = x z radius and a Template");
            }
            hub.crowd.center = {-x, 0, z};
            hub.crowd.radius = radius;
        }
    }
    return hub;
}

std::unique_ptr<net::Link> UdpHubNetwork::open_server(uint16_t& port) {
    auto link = std::make_unique<net::UdpLink>();
    port = link->port();
    return link;
}

std::unique_ptr<net::Link> UdpHubNetwork::open_client() {
    return std::make_unique<net::UdpLink>();
}

net::Endpoint UdpHubNetwork::local(uint16_t port) const {
    return {0x7F000001, port};  // 127.0.0.1
}

Hub::Hub(GameServer& server, uint16_t port, const World& world, HubConfig config, HubNetwork& network)
    : server_(server), port_(port), world_(world), config_(std::move(config)), network_(network) {
    for (net::Recruiter& recruiter : config_.recruiters) {
        recruiter.position = world_.clamp(recruiter.position);
        recruiter.position.y = world_.terrain().height_at(recruiter.position.x, recruiter.position.z);
    }
    server_.set_recruiters(config_.recruiters, config_.sides);
    for (net::Quartermaster& qm : config_.quartermasters) {
        qm.position = world_.clamp(qm.position);
        qm.position.y = world_.terrain().height_at(qm.position.x, qm.position.z);
    }
    server_.set_quartermasters(config_.quartermasters);
    for (const HubRangeTarget& rt : config_.range_targets) {
        server_.add_range_target(rt.name, rt.position, rt.patrol_end, rt.speed, rt.health, rt.mitigation, rt.respawn);
    }
    load_battle_world = [](const Battlefield& field) -> std::shared_ptr<const World> {
        return field.terrain.empty() ? nullptr : load_world(field.terrain);
    };
    server_.on_queue = [this](uint32_t entity, int team, QueueMode mode) { queue(entity, team, mode); };
    server_.on_join = [this](uint32_t entity, const std::string& name) {
        if (auto it = notices_.find(name); it != notices_.end()) {
            server_.notify(entity, it->second);
            notices_.erase(it);
        }
    };
}

namespace {

constexpr int8_t walking = 25;        // of 127: about 1.4 m/s, a walk
constexpr double arrived = 0.6;       // meters from a corner
constexpr double stuck_after = 4;     // seconds without getting anywhere

double yaw_to(Vec3 d) {
    return std::atan2(-d.x, -d.z);  // 0 faces -Z, positive turns left
}

} // namespace

void Hub::add_people() {
    if (config_.people.empty() && config_.crowd.count == 0) {
        return;
    }
    server_.set_looks(config_.looks);
    // Where they walk: the streets around the crowd's area and every route.
    Battlefield streets;
    streets.name = config_.name + " streets";
    double lo_x = 1e30, lo_z = 1e30, hi_x = -1e30, hi_z = -1e30;
    const auto grow = [&](Vec3 p, double r) {
        lo_x = std::min(lo_x, p.x - r), hi_x = std::max(hi_x, p.x + r);
        lo_z = std::min(lo_z, p.z - r), hi_z = std::max(hi_z, p.z + r);
    };
    if (config_.crowd.count > 0) {
        grow(config_.crowd.center, config_.crowd.radius + 20);
    }
    for (const HubPerson& person : config_.people) {
        grow(person.position, 20);
        for (const Vec3& p : person.route) {
            grow(p, 20);
        }
    }
    streets.min_x = lo_x, streets.max_x = hi_x, streets.min_z = lo_z, streets.max_z = hi_z;
    streets_ = std::make_unique<NavGrid>(world_, streets);

    for (const HubPerson& person : config_.people) {
        Vec3 at = person.position;
        at.y = world_.terrain().height_at(at.x, at.z);
        const uint32_t entity =
            server_.add_npc(person.name, at, person.yaw, uint8_t(person.look + 1), person.pose, 0, person.radius);
        if (person.route.size() >= 1) {
            walkers_.push_back({.entity = entity, .route = person.route});
        } else {
            ++standing_;
        }
    }
    for (int i = 0; i < config_.crowd.count; ++i) {
        const auto at = wander_point();
        if (!at) {
            break;
        }
        const int look = config_.crowd.looks[size_t(i) % config_.crowd.looks.size()];
        const double yaw = std::uniform_real_distribution<double>(-std::numbers::pi, std::numbers::pi)(random_);
        // Each in colors of their own (the set pieces keep their looks').
        const auto colors = uint16_t(std::uniform_int_distribution<int>(1, 65535)(random_));
        const uint32_t entity = server_.add_npc("Citizen", *at, yaw, uint8_t(look + 1), 0, colors);
        // Some linger a while before they set off.
        Walker walker{.entity = entity};
        walker.pause_until = std::uniform_real_distribution<double>(0, 10)(random_);
        walkers_.push_back(walker);
    }
    say(std::format("{} people in {}: {} walking", walkers_.size() + standing_, config_.name, walkers_.size()));
}

// Somewhere in the crowd's area to walk to: on the ground (not a roof),
// where a soldier could stand.
std::optional<Vec3> Hub::wander_point() {
    const MoveParams params;
    std::uniform_real_distribution<double> unit(0, 1);
    for (int attempt = 0; attempt < 60; ++attempt) {
        const double angle = unit(random_) * 2 * std::numbers::pi;
        const double r = config_.crowd.radius * std::sqrt(unit(random_));
        Vec3 p{config_.crowd.center.x + r * std::cos(angle), 0, config_.crowd.center.z + r * std::sin(angle)};
        const double ground = world_.terrain().height_at(p.x, p.z);
        p.y = world_.support_at(p.x, p.z, ground + 1, params.step_up).height;
        if (p.y > ground - 0.5 && p.y < ground + 1 && streets_->walkable(p)) {
            return p;
        }
    }
    return std::nullopt;
}

void Hub::walk_people() {
    for (Walker& walker : walkers_) {
        const MoveState* state = server_.state_of(walker.entity);
        if (state == nullptr) {
            continue;
        }
        MoveInput input;
        input.yaw = state->yaw;
        if (now_ < walker.pause_until) {
            server_.steer_npc(walker.entity, input);
            continue;
        }
        // Somewhere to go: the crowd anywhere in its area, a route its next point.
        if (walker.next >= walker.path.size()) {
            std::optional<Vec3> goal;
            if (walker.route.empty()) {
                goal = wander_point();
            } else {
                goal = walker.route[walker.route_index % walker.route.size()];
                ++walker.route_index;
            }
            walker.path = goal ? streets_->find_path(state->position, *goal) : std::vector<Vec3>{};
            walker.next = 0;
            walker.checked = state->position;
            walker.checked_at = now_;
            if (walker.path.empty()) {
                walker.pause_until = now_ + 2;
                server_.steer_npc(walker.entity, input);
                continue;
            }
        }
        Vec3 to = walker.path[walker.next] - state->position;
        to.y = 0;
        if (to.length() < arrived) {
            if (++walker.next >= walker.path.size()) {
                // There: linger (the crowd longer than a route's walker).
                const double low = walker.route.empty() ? 4 : 2, high = walker.route.empty() ? 15 : 5;
                walker.pause_until = now_ + std::uniform_real_distribution<double>(low, high)(random_);
                server_.steer_npc(walker.entity, input);
                continue;
            }
            to = walker.path[walker.next] - state->position;
            to.y = 0;
        }
        // Not getting anywhere (someone in the way): try somewhere else soon.
        if (now_ - walker.checked_at > stuck_after) {
            Vec3 moved = state->position - walker.checked;
            moved.y = 0;
            if (moved.length() < 0.5) {
                walker.path.clear();
                walker.pause_until = now_ + 1;
            }
            walker.checked = state->position;
            walker.checked_at = now_;
        }
        input.yaw = float(yaw_to(to));
        input.forward = walking;
        server_.steer_npc(walker.entity, input);
    }
}

Hub::~Hub() {
    server_.on_queue = nullptr;
    server_.on_join = nullptr;
    for (auto& battle : battles_) {
        battle->bots->disconnect();
    }
}

void Hub::say(const std::string& text) const {
    if (log) {
        log(text);
    }
}

void Hub::queue(uint32_t entity, int team, QueueMode mode) {
    for (auto& q : queues_) {
        std::erase_if(q, [&](const Queued& entry) { return entry.entity == entity; });
    }
    if (net::PlayerStatus* status = server_.status(entity)) {
        status->queue = net::PlayerStatus::none;
        status->team = -1;
        status->queue_mode = QueueMode::standard;
        status->in_queue = 0;
        status->starts_in = 0;
        status->battlefield.clear();
    }
    const std::string name = server_.name_of(entity);
    if (team >= 0) {
        queues_[size_t(mode)].push_back({entity, name, std::min(team, 1), now_});
        say(std::format("{} queued ({}) for the {}", name, mode == QueueMode::gear ? "Gear" : "Standard",
                        config_.sides[size_t(std::min(team, 1))]));
    } else {
        say(std::format("{} left the queue", name));
    }
}

bool Hub::has_room(const Battle& battle, int side, QueueMode mode) const {
    const MatchState& match = battle.server->match()->state();
    const int team = battle.field.teams[size_t(side)];
    return battle.mode == mode && battle.over_since < 0 && match.phase != Phase::ended &&
           battle.players[size_t(team)] < config_.team_size;
}

bool Hub::players_connected(const Battle& battle) const {
    for (const uint32_t entity : battle.server->entities()) {
        const std::string name = battle.server->name_of(entity);
        for (const auto& [ticket, player] : battle.tickets) {
            if (player.first == name) {
                return true;
            }
        }
    }
    return false;
}

void Hub::send(Battle& battle, const Queued& player) {
    uint64_t ticket = 0;
    while (ticket == 0 || ticket == battle.bot_ticket || battle.tickets.contains(ticket)) {
        ticket = random_();
    }
    const int team = battle.field.teams[size_t(player.team)];
    battle.tickets[ticket] = {player.name, team};
    if (const auto prof = server_.profile_of(player.entity)) {
        battle.profiles[player.name] = *prof;
    }
    std::vector<std::string> worn;
    auto loadouts = server_.build_player_loadouts(player.entity, battle.mode == QueueMode::gear, &worn);
    if (!loadouts.empty()) {
        battle.player_loadouts[player.name] = std::move(loadouts);
        battle.player_worn[player.name] = std::move(worn);
    }
    ++battle.soldiers[size_t(team)];
    ++battle.players[size_t(team)];
    server_.transfer(player.entity, net::Transfer{0, battle.port, ticket, battle.field.id});
    if (net::PlayerStatus* status = server_.status(player.entity)) {
        status->queue = net::PlayerStatus::none;
        status->team = -1;
        status->queue_mode = QueueMode::standard;
        status->in_queue = 0;
        status->starts_in = 0;
        status->battlefield.clear();
    }
    say(std::format("sent {} to battle {} on {} for the {}", player.name, battle.id, battle.field.field.name,
                    config_.sides[size_t(player.team)]));
}

void Hub::choose_next(QueueMode mode) {
    if (config_.battlefields.empty()) {
        return;
    }
    // Not the last battle's again, when there's another.
    std::vector<size_t> choices;
    for (size_t i = 0; i < config_.battlefields.size(); ++i) {
        if (config_.battlefields.size() == 1 || config_.battlefields[i].id != last_battlefield_) {
            choices.push_back(i);
        }
    }
    Next next;
    next.battlefield = choices[std::uniform_int_distribution<size_t>(0, choices.size() - 1)(random_)];
    const HubBattlefield& chosen = config_.battlefields[next.battlefield];
    if (const auto it = worlds_.find(chosen.id); it != worlds_.end() && !it->second.expired()) {
        std::promise<std::shared_ptr<const World>> loaded;
        loaded.set_value(it->second.lock());
        next.world = loaded.get_future().share();
    } else {
        next.world = std::async(std::launch::async, load_battle_world, chosen.field).share();
    }
    say(std::format("next battle on {}", chosen.field.name));
    next_[size_t(mode)] = std::move(next);
}

bool Hub::next_ready(QueueMode mode) {
    auto& next = next_[size_t(mode)];
    if (!next || next->world.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }
    try {
        next->world.get();
        return true;
    } catch (const std::exception& error) {
        // Off the list; the queue waits for another.
        say(std::format("can't load {}: {}", config_.battlefields[next->battlefield].field.name, error.what()));
        config_.battlefields.erase(config_.battlefields.begin() + std::ptrdiff_t(next->battlefield));
        next.reset();
        return false;
    }
}

void Hub::start_battle(QueueMode mode) {
    auto& next = next_[size_t(mode)];
    auto& queue = queues_[size_t(mode)];
    auto battle = std::make_unique<Battle>();
    Battle* b = battle.get();
    b->field = config_.battlefields[next->battlefield];
    b->mode = mode;
    b->world = next->world.get();
    next.reset();
    last_battlefield_ = b->field.id;
    if (b->world) {
        worlds_[b->field.id] = b->world;
    }
    const World& world = b->world ? *b->world : world_;
    b->id = next_battle_++;
    b->link = network_.open_server(b->port);
    b->bot_ticket = random_() | 1;
    b->last_player = now_;
    ServerConfig config;
    config.battlefield = b->field.field;
    config.single_match = true;
    b->server = std::make_unique<GameServer>(*b->link, world, config);
    b->server->log = [this, id = b->id](const std::string& line) { say(std::format("battle {}: {}", id, line)); };
    b->server->on_join = [b](uint32_t entity, const std::string& name) {
        if (const auto it = b->profiles.find(name); it != b->profiles.end()) {
            b->server->set_player_profile(entity, it->second);
        }
        if (const auto lit = b->player_loadouts.find(name); lit != b->player_loadouts.end()) {
            const auto wit = b->player_worn.find(name);
            b->server->set_player_loadouts(entity, lit->second, wit != b->player_worn.end() ? wit->second : std::vector<std::string>{});
        }
    };
    // Players with their ticket, on their side; bots wherever there's room.
    b->server->admit = [b](const std::string& name, uint64_t ticket, int asked) -> std::optional<int> {
        if (ticket == b->bot_ticket && asked == natives_team) {
            return natives_team;  // a native: not a side's soldier
        }
        if (ticket == b->bot_ticket) {
            const int team = b->soldiers[0] <= b->soldiers[1] ? 0 : 1;
            ++b->soldiers[size_t(team)];
            return team;
        }
        const auto it = b->tickets.find(ticket);
        if (it != b->tickets.end() && it->second.first == name) {
            return it->second.second;
        }
        return std::nullopt;
    };
    b->server->on_match_ended = [this, b](const MatchResult& result) {
        last_result_ = result;
        for (const PlayerResult& player : result.players) {
            const bool sent = std::any_of(b->tickets.begin(), b->tickets.end(),
                                          [&](const auto& t) { return t.second.first == player.name; });
            if (!sent) {
                continue;  // a bot
            }
            const char* outcome = result.winner < 0 ? "Draw" : result.winner == player.team ? "Victory" : "Defeat";
            const BattleReward reward = compute_battle_reward(
                result.winner == player.team, result.winner < 0, player.kills, player.captures, b->mode);
            server_.award_battle_rewards(player.name, reward);
            const auto count = [](int n, const char* one, const char* many) {
                return std::format("{} {}", n, n == 1 ? one : many);
            };
            notices_[player.name] = std::format("{} at {}: {}, {}, {} (+{} FP, +{} Cr, +{} XP)", outcome,
                                                result.battlefield, count(player.kills, "kill", "kills"),
                                                count(player.deaths, "death", "deaths"),
                                                count(player.captures, "capture", "captures"),
                                                reward.faction_points, reward.credits, reward.xp);
        }
    };

    // Send the queue, up to the team size per side.
    std::vector<Queued> waiting;
    std::swap(waiting, queue);
    for (const Queued& player : waiting) {
        if (has_room(*b, player.team, mode)) {
            send(*b, player);
        } else {
            queue.push_back(player);
        }
    }
    const int bots = std::max(0, 2 * config_.team_size - b->players[0] - b->players[1]);
    ClientConfig bot_config;
    bot_config.server = network_.local(b->port);
    bot_config.battlefield = b->field.field;
    bot_config.ticket = b->bot_ticket;
    b->bots = std::make_unique<BotPlayers>(bots, world, bot_config, [this] { return network_.open_client(); },
                                           config_.bot_skill);
    say(std::format("battle {} on {}: port {}, {} players, {} bots", b->id, b->field.field.name, b->port,
                    b->players[0] + b->players[1], bots));
    battles_.push_back(std::move(battle));
}

void Hub::update(double now) {
    now_ = now;
    if (!people_added_) {
        people_added_ = true;  // on the first update, so the log says so
        add_people();
    }
    walk_people();
    // Players who left the hub leave the queue.
    for (size_t m = 0; m < 2; ++m) {
        const QueueMode mode = QueueMode(m);
        auto& queue = queues_[m];
        auto& next = next_[m];
        std::erase_if(queue, [&](const Queued& q) { return server_.name_of(q.entity) != q.name; });
        // Join a battle that's on, or start one once the first player has waited.
        std::vector<Queued> waiting;
        std::swap(waiting, queue);
        for (const Queued& player : waiting) {
            auto open = std::find_if(battles_.begin(), battles_.end(),
                                     [&](const auto& b) { return has_room(*b, player.team, mode); });
            if (open != battles_.end()) {
                send(**open, player);
            } else {
                queue.push_back(player);
            }
        }
        if (!queue.empty() && !next) {
            choose_next(mode);
        }
        if (!queue.empty() && now - queue.front().since >= config_.queue_time && next_ready(mode)) {
            start_battle(mode);
        }
        for (const Queued& player : queue) {
            if (net::PlayerStatus* status = server_.status(player.entity)) {
                status->queue = net::PlayerStatus::queued;
                status->team = int8_t(player.team);
                status->queue_mode = mode;
                status->in_queue = uint16_t(queue.size());
                status->starts_in = float(std::max(0.0, config_.queue_time - (now - queue.front().since)));
                status->battlefield = next ? config_.battlefields[next->battlefield].field.name : std::string();
            }
        }
    }

    for (auto& battle : battles_) {
        battle->server->update(now);
        battle->bots->update(now);
        if (players_connected(*battle)) {
            battle->last_player = now;
        }
        if (battle->over_since < 0 && battle->server->match_over()) {
            battle->over_since = now;
            for (const uint32_t entity : battle->server->entities()) {
                const std::string name = battle->server->name_of(entity);
                if (std::any_of(battle->tickets.begin(), battle->tickets.end(),
                                [&](const auto& t) { return t.second.first == name; })) {
                    battle->server->transfer(entity, net::Transfer{0, port_, 0, ""});
                }
            }
            say(std::format("battle {} is over: sending everyone back", battle->id));
        }
    }
    std::erase_if(battles_, [&](const std::unique_ptr<Battle>& battle) {
        const bool done = battle->over_since >= 0 && now - battle->over_since >= config_.return_delay;
        const bool abandoned = now - battle->last_player > 30;
        if (done || abandoned) {
            say(std::format("battle {} closed{}", battle->id, abandoned && !done ? " (no players left)" : ""));
            battle->bots->disconnect();
        }
        return done || abandoned;
    });
}

void Hub::set_bot_skill(BotSkill skill) {
    config_.bot_skill = skill;
    for (const auto& battle : battles_) {
        if (battle->bots) {
            battle->bots->set_skill(skill);
        }
    }
}

} // namespace swm::core
