#include "swm/core/hub.h"

#include <algorithm>
#include <format>
#include <sstream>
#include <stdexcept>

#include "swm/core/ini.h"
#include "swm/core/net/udp.h"
#include "swm/core/world.h"

namespace swm::core {

HubConfig load_hub(const std::filesystem::path& path) {
    const Ini ini = Ini::load(path);
    const std::string where = path.string();
    const Ini::Section* main = ini.section("hub");
    if (main == nullptr) {
        throw std::runtime_error(where + ": no [hub] section");
    }
    HubConfig hub;
    hub.name = main->get("Name").value_or(path.stem().string());
    const auto battlefield = main->get("Battlefield");
    if (!battlefield) {
        throw std::runtime_error(where + ": no Battlefield");
    }
    const std::filesystem::path battlefield_path = path.parent_path() / *battlefield;
    hub.battlefield = load_battlefield(battlefield_path);
    hub.battlefield_id = battlefield_path.stem().string();
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
    server_.set_recruiters(config_.recruiters,
                           {config_.battlefield.teams[0].name, config_.battlefield.teams[1].name});
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
        say(std::format("{} queued for the {}", name, config_.battlefield.teams[size_t(std::min(team, 1))].name));
    } else {
        say(std::format("{} left the queue", name));
    }
}

bool Hub::has_room(const Battle& battle, int team) const {
    const MatchState& match = battle.server->match()->state();
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
    battle.tickets[ticket] = {player.name, player.team};
    ++battle.soldiers[size_t(player.team)];
    ++battle.players[size_t(player.team)];
    server_.transfer(player.entity, net::Transfer{0, battle.port, ticket, config_.battlefield_id});
    if (net::PlayerStatus* status = server_.status(player.entity)) {
        *status = net::PlayerStatus{.notice_id = status->notice_id, .notice = status->notice};
    }
    say(std::format("sent {} to battle {} for the {}", player.name, battle.id,
                    config_.battlefield.teams[size_t(player.team)].name));
}

void Hub::start_battle() {
    auto battle = std::make_unique<Battle>();
    Battle* b = battle.get();
    b->id = next_battle_++;
    b->link = network_.open_server(b->port);
    b->bot_ticket = random_() | 1;
    b->last_player = now_;
    ServerConfig config;
    config.battlefield = config_.battlefield;
    config.single_match = true;
    b->server = std::make_unique<GameServer>(*b->link, world_, config);
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
            notices_[player.name] = std::format("{} at {}: {} kills, {} deaths, {} captures", outcome,
                                                result.battlefield, player.kills, player.deaths, player.captures);
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
    bot_config.battlefield = config_.battlefield;
    bot_config.ticket = b->bot_ticket;
    b->bots = std::make_unique<BotPlayers>(bots, world_, bot_config, [this] { return network_.open_client(); });
    say(std::format("battle {} on {}: port {}, {} players, {} bots", b->id, config_.battlefield.name, b->port,
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
    if (!queue_.empty() && now - queue_.front().since >= config_.queue_time) {
        start_battle();
    }
    for (const Queued& player : queue_) {
        if (net::PlayerStatus* status = server_.status(player.entity)) {
            status->queue = net::PlayerStatus::queued;
            status->team = int8_t(player.team);
            status->in_queue = uint16_t(queue_.size());
            status->starts_in = float(std::max(0.0, config_.queue_time - (now - queue_.front().since)));
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
