// Conquest: battlefield files, command-post capture, reinforcements and the
// match lifecycle; a match server with teams and spawning at posts; and bots
// playing a whole match.

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>

#include "swm/core/bot.h"
#include "swm/core/client.h"
#include "swm/core/conquest.h"
#include "swm/core/net/link.h"
#include "swm/core/server.h"
#include "swm/core/world.h"
#include "test.h"

using namespace swm::core;
using swm::test::near;

namespace {

constexpr double dt = 1.0 / 30;

void test_battlefield_file() {
    const auto dir = std::filesystem::temp_directory_path() / "swm_match_test";
    std::filesystem::create_directories(dir / "classes");
    std::ofstream(dir / "classes" / "trooper.ini") << "[soldier]\nName = trooper\nMaxHealth = 300\n";
    std::ofstream(dir / "field.ini") << "[battlefield]\nName = Dunes\nBounds = 100 -500 300 -200\nTickets = 80\n"
                                        "Warmup = 5\n\n"
                                        "[team]\nName = Empire\nClass = classes/trooper.ini\n\n"
                                        "[team]\nName = Rebellion\nClass = classes/trooper.ini\n"
                                        "Class = classes/trooper.ini\n\n"
                                        "[post]\nName = West\nPosition = 120 -300\nOwner = empire\n\n"
                                        "[post]\nName = Middle\nPosition = 200 -300\nRadius = 15\nCaptureTime = 16\n\n"
                                        "[post]\nName = East\nPosition = 280 -300\nOwner = Rebellion\nCapturable = 0\n";
    const Battlefield field = load_battlefield(dir / "field.ini");
    CHECK(field.name == "Dunes" && field.tickets == 80 && field.warmup == 5);
    // Galaxies x runs the other way: game x is -300..-100.
    CHECK(field.min_x == -300 && field.max_x == -100 && field.min_z == -500 && field.max_z == -200);
    CHECK(field.contains({-200, 0, -300}) && !field.contains({200, 0, -300}));
    CHECK(field.teams[0].name == "Empire" && field.teams[0].classes.size() == 1 && field.teams[1].classes.size() == 2);
    CHECK(field.teams[0].classes[0].max_health == 300);
    CHECK(field.posts.size() == 3 && field.posts[0].owner == 0 && field.posts[1].owner == -1 && field.posts[2].owner == 1);
    CHECK(field.posts[1].position == Vec3(-200, 0, -300) && field.posts[1].radius == 15 && field.posts[1].capture_time == 16);
    CHECK(!field.posts[2].capturable && field.posts[0].neutralize_time == 12);

    std::ofstream(dir / "bad.ini") << "[battlefield]\n[team]\nName = A\nClass = classes/missing.ini\n";
    bool threw = false;
    try {
        load_battlefield(dir / "bad.ini");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    std::filesystem::remove_all(dir);
}

// Three posts in a row along x: team 0 holds West, team 1 East, Middle is neutral.
Battlefield three_posts() {
    Battlefield field;
    field.teams[0] = {"Empire", {SoldierDef{}}};
    field.teams[1] = {"Rebellion", {SoldierDef{}}};
    field.min_x = -100;
    field.max_x = 100;
    field.min_z = -50;
    field.max_z = 50;
    field.posts = {{.name = "West", .position = {-60, 0, 0}, .owner = 0},
                   {.name = "Middle", .position = {0, 0, 0}},
                   {.name = "East", .position = {60, 0, 0}, .owner = 1}};
    field.warmup = 1;
    field.results = 2;
    field.tickets = 50;
    return field;
}

// Steps the rules for `seconds`; returns the events.
std::vector<MatchEvent> run(Conquest& match, double seconds, std::vector<Conquest::Unit> units, size_t players = 2) {
    std::vector<MatchEvent> events;
    for (int i = 0; i < int(std::lround(seconds / dt)); ++i) {
        for (const MatchEvent& e : match.step(dt, units, players)) {
            events.push_back(e);
        }
    }
    return events;
}

bool has(const std::vector<MatchEvent>& events, MatchEvent::Kind kind, int team = -2, int post = -2) {
    for (const MatchEvent& e : events) {
        if (e.kind == kind && (team == -2 || e.team == team) && (post == -2 || e.post == post)) {
            return true;
        }
    }
    return false;
}

void test_capture() {
    Conquest match(three_posts());
    const auto& state = match.state();
    // Warmup is practice: the middle post doesn't change hands, deaths are free.
    CHECK(state.phase == Phase::warmup && state.posts[1].owner == -1);
    run(match, 0.5, {{0, {0, 0, 0}}});
    match.killed(0);
    CHECK(state.posts[1].charge == 0 && state.tickets[0] == 50);
    CHECK(match.can_spawn(0, 0) && !match.can_spawn(0, 1) && !match.can_spawn(0, 2) && match.can_spawn(1, 2));
    // Waits for players.
    CHECK(run(match, 2, {}, 0).empty() && state.phase == Phase::warmup);
    CHECK(has(run(match, 1.1, {}), MatchEvent::went_live) && state.phase == Phase::live);

    // One soldier takes a neutral post in CaptureTime (10 s); two take half that.
    auto events = run(match, 9.8, {{0, {3, 0, 0}}});
    CHECK(state.posts[1].owner == -1 && state.posts[1].holder == 0 && state.posts[1].charge > 0.95f);
    CHECK(has(run(match, 0.3, {{0, {3, 0, 0}}}), MatchEvent::captured, 0, 1) && state.posts[1].owner == 0);
    match.killed(0);
    CHECK(state.tickets[0] == 49);  // live: a death costs a reinforcement
    // Holding two posts to one bleeds the other team: 0.1 tickets a second per post of lead.
    const double before = state.tickets[1];
    run(match, 10, {});
    CHECK(near(state.tickets[1], before - 1.0, 0.01) && state.tickets[0] == 49);

    // Two Rebels neutralize East's neighbor... the middle, in half of NeutralizeTime (12 s).
    events = run(match, 6.1, {{1, {0, 0, 2}}, {1, {2, 0, 0}}});
    CHECK(has(events, MatchEvent::neutralized, 1, 1) && state.posts[1].owner == -1);
    // Contested: nothing changes.
    const float charge = state.posts[1].charge;
    run(match, 3, {{1, {0, 0, 2}}, {0, {1, 0, 0}}});
    CHECK(state.posts[1].contested && state.posts[1].charge == charge);
    // Empire comes back alone: first undoes the Rebel charge, then captures.
    run(match, 0.5, {{0, {1, 0, 0}}});
    CHECK(!state.posts[1].contested && state.posts[1].holder == 0);
    CHECK(has(run(match, 10, {{0, {1, 0, 0}}}), MatchEvent::captured, 0, 1));

    // Empire takes East too: holding every post for VictoryTime (20 s) wins.
    run(match, 12.1 + 10.1, {{0, {60, 0, 0}}});
    CHECK(state.posts[2].owner == 0 && state.dominant == 0 && state.phase == Phase::live);
    events = run(match, 20.1, {});
    CHECK(has(events, MatchEvent::ended, 0) && state.phase == Phase::ended && state.winner == 0);
    CHECK(!match.can_spawn(0, 0));
    // The results show, then the next match warms up with fresh posts and tickets.
    events = run(match, 2.1, {});
    CHECK(has(events, MatchEvent::restarted) && state.phase == Phase::warmup);
    CHECK(state.posts[2].owner == 1 && state.tickets[0] == 50 && state.tickets[1] == 50 && state.winner == -1);
}

void test_tickets_run_out() {
    Battlefield field = three_posts();
    field.tickets = 3;
    Conquest match(field);
    run(match, 1.1, {});
    match.killed(1);
    match.killed(1);
    CHECK(match.can_spawn(1, 2));
    match.killed(1);
    CHECK(!match.can_spawn(1, 2));
    const auto events = run(match, dt, {});
    CHECK(has(events, MatchEvent::ended, 0) && match.state().winner == 0);
}

// A match server and clients on flat ground.
struct Field {
    double now = 0;
    Heightfield ground{401, 1.0, -200, -200, std::vector<float>(401 * 401, 0.0f)};
    World world{ground};
    net::LoopbackHub hub;
    net::LoopbackLink server_link{hub, {1, 1}};
    std::unique_ptr<GameServer> server;
    std::vector<std::unique_ptr<net::LoopbackLink>> links;
    std::vector<std::unique_ptr<GameClient>> clients;

    explicit Field(const Battlefield& battlefield) {
        ServerConfig config;
        config.battlefield = battlefield;
        config.respawn_delay = 2;
        server = std::make_unique<GameServer>(server_link, world, config);
    }

    GameClient& join(const std::string& name) {
        links.push_back(std::make_unique<net::LoopbackLink>(hub, net::Endpoint{uint32_t(10 + links.size()), 1}));
        ClientConfig config{.server = {1, 1}, .name = name};
        config.battlefield = server->match()->battlefield();
        clients.push_back(std::make_unique<GameClient>(*links.back(), world, config));
        return *clients.back();
    }

    template <typename Steer>
    void run(double seconds, Steer steer, double step = 1.0 / 60) {
        for (const double end = now + seconds; now < end; now += step) {
            steer();
            server->update(now);
            for (auto& client : clients) {
                client->update(now);
            }
        }
    }
    void run(double seconds) {
        run(seconds, [] {});
    }
};

void test_match_server() {
    Battlefield battlefield = three_posts();
    battlefield.out_of_bounds_time = 3;
    battlefield.warmup = 2;
    SoldierDef scout;
    scout.name = "scout";
    scout.max_health = 200;
    battlefield.teams[1].classes.push_back(scout);
    Field field(battlefield);
    GameClient& a = field.join("a");
    GameClient& b = field.join("b");
    GameClient& c = field.join("c");
    field.run(0.5);
    // Teams are balanced as players join; everyone waits to spawn.
    CHECK(a.self().team == 0 && b.self().team == 1 && c.self().team == 0);
    CHECK(a.self().life == Life::waiting && !a.alive() && a.match() && a.match()->phase == Phase::warmup);
    // No spawning at a post the team doesn't hold.
    b.request_spawn(1, 0);
    field.run(0.5);
    CHECK(b.self().life == Life::waiting);
    b.request_spawn(1, 2);
    a.request_spawn(0, 0);
    c.request_spawn(0, 0);
    field.run(0.5);
    CHECK(a.alive() && b.alive() && c.alive() && b.self().soldier_class == 1 && b.health() == 200);
    CHECK(b.soldier().name == "scout");
    CHECK((b.predicted().position - battlefield.posts[2].position).length() < 6.5);
    // The match goes live at the end of the warmup: everyone spawns again.
    field.run(0.6);
    CHECK(a.match()->phase == Phase::live && a.self().life == Life::waiting);
    a.request_spawn(0, 0);
    b.request_spawn(0, 2);
    c.request_spawn(0, 0);
    field.run(0.3);
    CHECK(a.alive() && b.alive() && c.alive());

    // C walks into A's line of fire: teammates don't hurt each other.
    const auto aim = [&](GameClient& from, uint32_t at) {
        GameClient::Controls controls;
        for (const auto& r : from.remotes()) {
            if (r.id == at) {
                const Vec3 d = r.state.position + Vec3{0, 1.0, 0} - (from.predicted().position + Vec3{0, eye_height, 0});
                controls.yaw = std::atan2(-d.x, -d.z);
                controls.pitch = std::atan2(d.y, std::hypot(d.x, d.z));
                controls.fire = true;
            }
        }
        return controls;
    };
    field.run(2, [&] { a.set_input(aim(a, c.entity_id())); });
    CHECK(c.health() == 400 && a.take_shots().size() >= 5);

    // B runs out of bounds (East is 40 m from the edge) and dies there.
    double tickets = a.match()->tickets[1];
    field.run(11, [&] { b.set_input({.forward = 1, .yaw = float(-std::numbers::pi / 2)}); });
    CHECK(!b.alive() && b.self().deaths == 1 && a.match()->tickets[1] == tickets - 1);
    // Dead, then waiting to spawn again after the respawn delay.
    CHECK(b.self().life == Life::dead);
    field.run(2.1, [&] { b.set_input({}); });
    CHECK(b.self().life == Life::waiting);

    // B comes back and runs at A, who shoots it: a kill, a death, and a
    // reinforcement lost.
    b.request_spawn(0, 2);
    field.run(0.3);
    tickets = a.match()->tickets[1];
    bool killed = false;
    field.run(10, [&] {
        a.set_input(aim(a, b.entity_id()));
        b.set_input({.forward = b.alive() ? 1.0 : 0.0, .yaw = float(std::numbers::pi / 2)});
        for (const auto& event : b.take_events()) {
            killed |= event.kind == net::CombatEvent::kill && event.attacker == a.entity_id();
        }
    });
    CHECK(killed && a.self().kills == 1 && b.self().deaths == 2 && a.match()->tickets[1] < tickets);
}

// Bots on both teams play a match to the end.
void test_bots_play_a_match() {
    Battlefield battlefield = three_posts();
    battlefield.posts.insert(battlefield.posts.begin() + 1, CommandPost{.name = "Inner west", .position = {-30, 0, 10}});
    battlefield.posts.insert(battlefield.posts.begin() + 3, CommandPost{.name = "Inner east", .position = {30, 0, -10}});
    battlefield.tickets = 40;
    battlefield.bleed = 0.2;
    Field field(battlefield);
    std::vector<Bot> bots;
    for (int i = 0; i < 6; ++i) {
        field.join("bot" + std::to_string(i));
        bots.emplace_back(uint64_t(i + 1));
    }
    bool ended = false;
    MatchState result;
    int captures = 0, kills = 0;
    while (field.now < 900 && !ended) {
        field.run(10, [&] {
            for (size_t i = 0; i < bots.size(); ++i) {
                bots[i].update(*field.clients[i], field.world, field.now);
                for (const auto& event : field.clients[i]->take_events()) {
                    if (i == 0) {
                        captures += event.kind == net::CombatEvent::captured;
                        kills += event.kind == net::CombatEvent::kill;
                    }
                }
            }
            const MatchState& state = field.server->match()->state();
            if (!ended && state.phase == Phase::ended) {
                ended = true;
                result = state;
                std::printf("bots: the match ended after %.0f s (winner %d, tickets %.0f to %.0f)\n", field.now,
                            result.winner, result.tickets[0], result.tickets[1]);
            }
        }, 1.0 / 30);
    }
    std::printf("bots: %d captures, %d kills\n", captures, kills);
    CHECK(result.winner >= 0);
    CHECK(ended && captures >= 2 && kills >= 5);
}

} // namespace

int main() {
    test_battlefield_file();
    test_capture();
    test_tickets_run_out();
    test_match_server();
    test_bots_play_a_match();
    return swm::test::finish("match_test");
}
