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
#include "swm/core/nav.h"
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
    std::filesystem::create_directories(dir / "items");
    std::filesystem::create_directories(dir / "loadouts");
    std::ofstream(dir / "items" / "rifle.ini") << "[weapon]\nName = rifle\n";
    std::ofstream(dir / "items" / "suit.ini") << "[gear]\nName = suit\nSlot = chest\nModifier = max_health - 100\n";
    std::ofstream(dir / "loadouts" / "trooper.ini") << "[loadout]\nName = trooper\nPrimary = rifle\nGear = suit\n";
    std::ofstream(dir / "field.ini") << "[battlefield]\nName = Dunes\nBounds = 100 -500 300 -200\nTickets = 80\n"
                                        "Warmup = 5\n\n"
                                        "[team]\nName = Empire\nLoadout = loadouts/trooper.ini\n\n"
                                        "[team]\nName = Rebellion\nLoadout = loadouts/trooper.ini\n"
                                        "Loadout = loadouts/trooper.ini\n\n"
                                        "[post]\nName = West\nPosition = 120 -300\nOwner = empire\n\n"
                                        "[post]\nName = Middle\nPosition = 200 -300\nRadius = 15\nCaptureTime = 16\n\n"
                                        "[post]\nName = East\nPosition = 280 -300\nOwner = Rebellion\nCapturable = 0\n";
    const Battlefield field = load_battlefield(dir / "field.ini");
    CHECK(field.name == "Dunes" && field.tickets == 80 && field.warmup == 5);
    // Galaxies x runs the other way: game x is -300..-100.
    CHECK(field.min_x == -300 && field.max_x == -100 && field.min_z == -500 && field.max_z == -200);
    CHECK(field.contains({-200, 0, -300}) && !field.contains({200, 0, -300}));
    CHECK(field.teams[0].name == "Empire" && field.teams[0].loadouts.size() == 1 && field.teams[1].loadouts.size() == 2);
    CHECK(field.teams[0].loadouts[0].name == "trooper" && stats_for(field.teams[0].loadouts[0]).max_health == 300);
    CHECK(field.posts.size() == 3 && field.posts[0].owner == 0 && field.posts[1].owner == -1 && field.posts[2].owner == 1);
    CHECK(field.posts[1].position == Vec3(-200, 0, -300) && field.posts[1].radius == 15 && field.posts[1].capture_time == 16);
    CHECK(!field.posts[2].capturable && field.posts[0].neutralize_time == 12);

    // A Battlefront map's posts, bounds and terrain (bf2tool export-map), with our teams.
    std::filesystem::create_directories(dir / "maps");
    std::ofstream(dir / "maps" / "m_conquest.ini") << "[map]\nName = m\nTerrain = m\nBounds = -512 -512 512 512\n"
                                                      "Barrier = 1 1 3 1 3 2 1 2\n\n"
                                                      "[post]\nName = CP1\nPosition = 35 141\nConsole = 33 140 4.5\nRadius = 8\nOwner = 2\n"
                                                      "SpawnPoint = 23 145\nSpawnPoint = 12 146\n\n"
                                                      "[post]\nName = CP3\nPosition = 22 -120\nOwner = 1\n\n"
                                                      "[post]\nName = CP2\nPosition = 74 -9\n";
    std::ofstream(dir / "mapped.ini") << "[battlefield]\nName = Swamp\nMap = maps/m_conquest.ini\n"
                                         "[team]\nName = Rebellion\nLoadout = loadouts/trooper.ini\n"
                                         "[team]\nName = Empire\nLoadout = loadouts/trooper.ini\n";
    const Battlefield mapped = load_battlefield(dir / "mapped.ini");
    CHECK(mapped.name == "Swamp" && mapped.posts.size() == 3 && mapped.terrain == dir / "maps" / "m");
    CHECK(mapped.min_x == -512 && mapped.max_z == 512);
    CHECK(mapped.posts[0].owner == 1 && mapped.posts[1].owner == 0 && mapped.posts[2].owner == -1);
    CHECK(mapped.posts[0].spawn_points.size() == 2 && mapped.posts[0].spawn_points[1] == Vec3(-12, 0, 146));
    CHECK(mapped.posts[0].position == Vec3(-35, 0, 141) && mapped.posts[2].spawn_points.empty());
    // Its console's place, x the other way; none for the others.
    CHECK(mapped.posts[0].console == Vec3(-33, 4.5, 140) && !mapped.posts[1].console);
    // Its barrier, with x the other way.
    CHECK(mapped.barriers.size() == 1 && mapped.barriers[0].corners[1] == Vec3(-3, 0, 1));
    CHECK(mapped.barriers[0].contains({-2, 0, 1.5}) && !mapped.barriers[0].contains({-2, 0, 2.5}));

    // A boundary (a triangle: x + z < 100 in Battlefront's coordinates) and
    // a death region, a box turned a quarter: its x half size (2) runs
    // along z, its z half size (4) along x.
    std::ofstream(dir / "maps" / "b_conquest.ini") << "[map]\nName = b\nBounds = -512 -512 512 512\n"
                                                      "Boundary = 0 0 100 0 0 100\n"
                                                      "DeathRegion = box 10 0 20 2 1 4 0 0 1 0 1 0 -1 0 0\n"
                                                      "DamageRegion = 50 sphere 40 0 40 3 3 3 1 0 0 0 1 0 0 0 1\n"
                                                      "Powerup = 20 1 30 3 2 0 1\n\n"
                                                      "[post]\nName = CP1\nPosition = 10 10\nOwner = 1\n"
                                                      "Altitude = -6 -1\nSpawnPoint = 12 12 -5\nSpawnPoint = 8 8 -5\n\n"
                                                      "[post]\nName = CP2\nPosition = 30 30\nOwner = 2\n";
    std::ofstream(dir / "bounded.ini") << "[battlefield]\nName = Ridge\nMap = maps/b_conquest.ini\n"
                                          "[team]\nName = Rebellion\nLoadout = loadouts/trooper.ini\n"
                                          "[team]\nName = Empire\nLoadout = loadouts/trooper.ini\n";
    const Battlefield bounded = load_battlefield(dir / "bounded.ini");
    // The bounds shrink to the boundary; game x runs the other way.
    CHECK(bounded.boundaries.size() == 1 && bounded.min_x == -100 && bounded.max_x == 0 && bounded.min_z == 0 &&
          bounded.max_z == 100);
    CHECK(bounded.contains({-10, 0, 10}) && bounded.contains({-49, 0, 49}) && !bounded.contains({-60, 0, 60}));
    CHECK(bounded.death_regions.size() == 1 && bounded.death_regions[0].center == Vec3(-10, 0, 20));
    CHECK(bounded.deadly({-13.5, 0, 20}) && !bounded.deadly({-10, 0, 23.5}) && bounded.deadly({-10, 0, 21.5}));
    // Feet below it, or the body in it; not standing on top of it.
    CHECK(bounded.deadly({-10, -1.5, 20}) && !bounded.deadly({-10, 1.5, 20}));
    // A damage region (game x the other way): it hurts, it doesn't kill;
    // bots keep out of it as out of a death region.
    CHECK(bounded.damage_regions.size() == 1 && bounded.damage_regions[0].rate == 50);
    CHECK(bounded.damage_rate({-40, 0, 40}) == 50 && bounded.damage_rate({-40, 2.5, 40}) == 50 &&
          bounded.damage_rate({40, 0, 40}) == 0 && bounded.damage_rate({-40, 0, 44}) == 0);
    CHECK(!bounded.deadly({-40, 0, 40}) && bounded.harmful({-40, 0, 40}) && bounded.harmful({-10, 0, 20}) &&
          !bounded.harmful({-20, 0, 20}));
    // An ammo droid: every 2 s, a full refill, within 3 m (game x the other
    // way), not a story above it.
    CHECK(bounded.powerups.size() == 1 && bounded.powerups[0].position == Vec3(-20, 1, 30) &&
          bounded.powerups[0].delay == 2 && bounded.powerups[0].health == 0 && bounded.powerups[0].ammo == 1);
    CHECK(bounded.powerups[0].reaches({-22, 0, 31}) && !bounded.powerups[0].reaches({-24, 1, 30}) &&
          !bounded.powerups[0].reaches({-20, 4, 30}));
    // A post in a cellar: its middle height, taken from within it and not
    // from the ground over it; its spawn points' heights.
    const CommandPost& cellar = bounded.posts[0];
    CHECK(cellar.position == Vec3(-10, -3.5, 10) && cellar.contains({-10, -6, 12}) && !cellar.contains({-10, 0, 12}));
    CHECK(cellar.spawn_heights && cellar.spawn_points[0] == Vec3(-12, -5, 12) && !bounded.posts[1].spawn_heights);
    CHECK(bounded.posts[1].contains({-30, 50, 30}));  // no altitude: any height

    std::ofstream(dir / "bad.ini") << "[battlefield]\n[team]\nName = A\nLoadout = loadouts/missing.ini\n";
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
    field.teams[0] = {"Empire", {Loadout{}}};
    field.teams[1] = {"Rebellion", {Loadout{}}};
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
    // A light loadout: its suit costs half the health.
    Loadout scout;
    scout.name = "scout";
    scout.gear.push_back({"light suit", "chest", {{Stat::max_health, 0, 0.5}}});
    battlefield.teams[1].loadouts.push_back(scout);
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
    CHECK(a.alive() && b.alive() && c.alive() && b.self().loadout == 1 && b.health() == 200);
    CHECK(b.loadout().name == "scout" && b.character_stats().max_health == 200);
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

void add_wall(MapCollision& collision, Vec3 low, Vec3 high) {
    const auto corner = [&](int i) {
        return Vec3{i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z};
    };
    const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (const auto& f : faces) {
        collision.triangles.push_back({corner(f[0]), corner(f[2]), corner(f[1])});
        collision.triangles.push_back({corner(f[0]), corner(f[3]), corner(f[2])});
    }
}

// Bots on both teams play a match to the end, finding their way around
// walls between the posts.
// A soldier who runs into a death region dies there at once.
void test_death_region() {
    Battlefield battlefield = three_posts();
    battlefield.death_regions.push_back({.center = {-45, 0, 0}, .size = {3, 2, 30}});
    battlefield.warmup = 60;
    Field field(battlefield);
    GameClient& a = field.join("a");
    field.run(0.5);
    a.request_spawn(0, 0);
    field.run(0.5);
    CHECK(a.alive());
    bool died = false;
    double seconds = 0;
    field.run(4, [&] {
        a.set_input({.forward = 1, .yaw = float(-std::numbers::pi / 2)});
        if (!died && !a.alive()) {
            died = true;
            CHECK(a.predicted().position.x > -49);
        }
        seconds += died ? 0 : dt;
    });
    CHECK(died && a.self().deaths == 1 && seconds < 3);
}

// A soldier standing in a damage region loses health at its rate, and dies
// once it's gone; not at once.
void test_damage_region() {
    Battlefield battlefield = three_posts();
    battlefield.damage_regions.push_back({{.center = {-60, 0, 0}, .size = {15, 2, 15}}, 100});
    battlefield.warmup = 60;
    Field field(battlefield);
    GameClient& a = field.join("a");
    field.run(0.5);
    a.request_spawn(0, 0);
    field.run(0.2);
    CHECK(a.alive());
    const double before = a.health();
    field.run(1);
    std::printf("damage region: %.0f health, then %.0f a second later\n", before, a.health());
    CHECK(a.alive() && near(before - a.health(), 100, 10));
    field.run(4);
    CHECK(!a.alive() && a.self().deaths == 1);
}

// A health droid by a damage region: it gives back what the region takes
// (100 a second against 50 every half second), so a soldier standing in
// both keeps its health, and can't be healed past full.
void test_health_droid() {
    Battlefield battlefield = three_posts();
    battlefield.damage_regions.push_back({{.center = {-60, 0, 0}, .size = {15, 2, 15}}, 100});
    battlefield.powerups.push_back({.position = {-60, 0, 0}, .radius = 20, .delay = 0.5, .health = 50});
    battlefield.warmup = 60;
    Field field(battlefield);
    GameClient& a = field.join("a");
    field.run(0.5);
    a.request_spawn(0, 0);
    field.run(0.2);
    CHECK(a.alive());
    const double before = a.health();
    field.run(3);
    std::printf("health droid in a damage region: %.0f health, then %.0f 3 s later\n", before, a.health());
    CHECK(a.alive() && near(a.health(), before, 60) && a.health() <= a.character_stats().max_health);
}

// A spawn point a little under its floor (one of Utapau's, a meter down)
// puts a soldier on the floor, not under it.
void test_spawn_under_floor() {
    Battlefield battlefield = three_posts();
    battlefield.posts[0].spawn_points = {{-60, 3, 0}};
    battlefield.posts[0].spawn_heights = true;
    battlefield.warmup = 60;
    Field field(battlefield);
    MapCollision deck;
    add_wall(deck, {-65, 3.7, -5}, {-55, 4, 5});
    field.world.add_map_collision(deck);
    GameClient& a = field.join("a");
    field.run(0.5);
    a.request_spawn(0, 0);
    field.run(0.5);
    std::printf("spawned a meter under a deck at 4 m: feet at %.2f\n", a.predicted().position.y);
    CHECK(a.alive() && near(a.predicted().position.y, 4, 0.01));
}

void test_bots_play_a_match() {
    Battlefield battlefield = three_posts();
    battlefield.posts.insert(battlefield.posts.begin() + 1, CommandPost{.name = "Inner west", .position = {-30, 0, 10}});
    battlefield.posts.insert(battlefield.posts.begin() + 3, CommandPost{.name = "Inner east", .position = {30, 0, -10}});
    battlefield.tickets = 40;
    battlefield.bleed = 0.2;
    // Health droids about the field: hurt bots go to them.
    for (const Vec3 at : {Vec3{0, 0, 30}, Vec3{0, 0, -30}, Vec3{-30, 0, -25}, Vec3{30, 0, 25}}) {
        battlefield.powerups.push_back({.position = at, .delay = 1, .health = 25});
    }
    Field field(battlefield);
    // Walls 3 m tall across the field on either side of the middle, open
    // only at one end (the other end meets the bounds).
    MapCollision walls;
    add_wall(walls, {-46, 0, -51}, {-44, 3, 35});
    add_wall(walls, {-16, 0, -35}, {-14, 3, 51});
    add_wall(walls, {14, 0, -51}, {16, 3, 35});
    add_wall(walls, {44, 0, -35}, {46, 3, 51});
    field.world.add_map_collision(walls);
    const auto navigation = std::make_shared<NavGrid>(field.world, battlefield);
    std::vector<Bot> bots;
    for (int i = 0; i < 6; ++i) {
        field.join("bot" + std::to_string(i));
        bots.emplace_back(uint64_t(i + 1));
        bots.back().set_navigation(navigation);
    }
    bool ended = false;
    MatchState result;
    int captures = 0, kills = 0;
    std::vector<bool> healed(bots.size());  // reached a droid it was going to
    while (field.now < 900 && !ended) {
        field.run(10, [&] {
            for (size_t i = 0; i < bots.size(); ++i) {
                bots[i].update(*field.clients[i], field.world, field.now);
                if (const int droid = bots[i].powerup(); droid >= 0) {
                    healed[i] = healed[i] || battlefield.powerups[size_t(droid)].reaches(
                                                 field.clients[i]->predicted().position);
                }
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
    const auto at_droids = std::count(healed.begin(), healed.end(), true);
    std::printf("bots: %d captures, %d kills; %d of %zu went to a health droid\n", captures, kills, int(at_droids),
                bots.size());
    CHECK(result.winner >= 0 && at_droids >= 1);
    CHECK(ended && captures >= 2 && kills >= 5);
}

} // namespace

// Bots come to a drop slowly, to land near its foot (off a narrow ledge).
void test_drop_pace() {
    const std::vector<Vec3> path{{0, 10, 0}, {3, 10, 0}, {3.5, 7, 0}, {10, 7, 0}};
    CHECK(std::abs(drop_pace({-5, 10, 0}, path, 0) - 1.0) < 1e-9);   // far from it
    CHECK(std::abs(drop_pace({2, 10, 0}, path, 1) - 0.75) < 1e-9);   // 1.5 m along the route
    CHECK(std::abs(drop_pace({3.3, 10, 0}, path, 2) - 0.3) < 1e-9);  // at the edge: slowest
    CHECK(std::abs(drop_pace({4, 7, 0}, path, 3) - 1.0) < 1e-9);     // down: on at a run
}

int main() {
    test_drop_pace();
    test_battlefield_file();
    test_capture();
    test_tickets_run_out();
    test_match_server();
    test_death_region();
    test_damage_region();
    test_health_droid();
    test_spawn_under_floor();
    test_bots_play_a_match();
    return swm::test::finish("match_test");
}
