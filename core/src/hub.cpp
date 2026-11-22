#include "swm/core/hub.h"

#include <algorithm>
#include <cctype>
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
    load_battle_world = [](const Battlefield& field) -> std::shared_ptr<const World> {
        return field.terrain.empty() ? nullptr : load_world(field.terrain);
    };
    server_.on_queue = [this](uint32_t entity, int team) { queue(entity, team); };
    server_.on_join = [this](uint32_t entity, const std::string& name) {
        if (auto it = notices_.find(name); it != notices_.end()) {
            server_.notify(entity, it->second);
            notices_.erase(it);
        }
    };
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

void Hub::queue(uint32_t entity, int team) {
    std::erase_if(queue_, [&](const Queued& q) { return q.entity == entity; });
    if (net::PlayerStatus* status = server_.status(entity)) {
        *status = net::PlayerStatus{.notice_id = status->notice_id, .notice = status->notice};
    }
    const std::string name = server_.name_of(entity);
    if (team >= 0) {
        queue_.push_back({entity, name, std::min(team, 1), now_});
        say(std::format("{} queued for the {}", name, config_.sides[size_t(std::min(team, 1))]));
    } else {
        say(std::format("{} left the queue", name));
    }
}

bool Hub::has_room(const Battle& battle, int side) const {
    const MatchState& match = battle.server->match()->state();
    const int team = battle.field.teams[size_t(side)];
    return battle.over_since < 0 && match.phase != Phase::ended && battle.players[size_t(team)] < config_.team_size;
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
    ++battle.soldiers[size_t(team)];
    ++battle.players[size_t(team)];
    server_.transfer(player.entity, net::Transfer{0, battle.port, ticket, battle.field.id});
    if (net::PlayerStatus* status = server_.status(player.entity)) {
        *status = net::PlayerStatus{.notice_id = status->notice_id, .notice = status->notice};
    }
    say(std::format("sent {} to battle {} on {} for the {}", player.name, battle.id, battle.field.field.name,
                    config_.sides[size_t(player.team)]));
}

void Hub::choose_next() {
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
    next_ = std::move(next);
}

bool Hub::next_ready() {
    if (!next_ || next_->world.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }
    try {
        next_->world.get();
        return true;
    } catch (const std::exception& error) {
        // Off the list; the queue waits for another.
        say(std::format("can't load {}: {}", config_.battlefields[next_->battlefield].field.name, error.what()));
        config_.battlefields.erase(config_.battlefields.begin() + std::ptrdiff_t(next_->battlefield));
        next_.reset();
        return false;
    }
}

void Hub::start_battle() {
    auto battle = std::make_unique<Battle>();
    Battle* b = battle.get();
    b->field = config_.battlefields[next_->battlefield];
    b->world = next_->world.get();
    next_.reset();
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
    // Players with their ticket, on their side; bots wherever there's room.
    b->server->admit = [b](const std::string& name, uint64_t ticket) -> std::optional<int> {
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
            const auto count = [](int n, const char* one, const char* many) {
                return std::format("{} {}", n, n == 1 ? one : many);
            };
            notices_[player.name] = std::format("{} at {}: {}, {}, {}", outcome, result.battlefield,
                                                count(player.kills, "kill", "kills"),
                                                count(player.deaths, "death", "deaths"),
                                                count(player.captures, "capture", "captures"));
        }
    };

    // Send the queue, up to the team size per side.
    std::vector<Queued> waiting;
    std::swap(waiting, queue_);
    for (const Queued& player : waiting) {
        if (has_room(*b, player.team)) {
            send(*b, player);
        } else {
            queue_.push_back(player);
        }
    }
    const int bots = std::max(0, 2 * config_.team_size - b->players[0] - b->players[1]);
    ClientConfig bot_config;
    bot_config.server = network_.local(b->port);
    bot_config.battlefield = b->field.field;
    bot_config.ticket = b->bot_ticket;
    b->bots = std::make_unique<BotPlayers>(bots, world, bot_config, [this] { return network_.open_client(); });
    say(std::format("battle {} on {}: port {}, {} players, {} bots", b->id, b->field.field.name, b->port,
                    b->players[0] + b->players[1], bots));
    battles_.push_back(std::move(battle));
}

void Hub::update(double now) {
    now_ = now;
    // Players who left the hub leave the queue.
    std::erase_if(queue_, [&](const Queued& q) { return server_.name_of(q.entity) != q.name; });
    // Join a battle that's on, or start one once the first player has waited.
    std::vector<Queued> waiting;
    std::swap(waiting, queue_);
    for (const Queued& player : waiting) {
        auto open = std::find_if(battles_.begin(), battles_.end(),
                                 [&](const auto& b) { return has_room(*b, player.team); });
        if (open != battles_.end()) {
            send(**open, player);
        } else {
            queue_.push_back(player);
        }
    }
    if (!queue_.empty() && !next_) {
        choose_next();
    }
    if (!queue_.empty() && now - queue_.front().since >= config_.queue_time && next_ready()) {
        start_battle();
    }
    for (const Queued& player : queue_) {
        if (net::PlayerStatus* status = server_.status(player.entity)) {
            status->queue = net::PlayerStatus::queued;
            status->team = int8_t(player.team);
            status->in_queue = uint16_t(queue_.size());
            status->starts_in = float(std::max(0.0, config_.queue_time - (now - queue_.front().since)));
            status->battlefield = next_ ? config_.battlefields[next_->battlefield].field.name : std::string();
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

} // namespace swm::core
