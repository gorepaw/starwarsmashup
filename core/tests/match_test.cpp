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
                                         "[team]\nName = Empire\nLoadout = loadouts/trooper.ini\n"
                                         "[natives]\nName = Ewoks\nLook = ewok\nCount = 6\nFriends = 1\n"
                                         "Loadout = loadouts/trooper.ini\n";
    const Battlefield mapped = load_battlefield(dir / "mapped.ini");
    CHECK(mapped.name == "Swamp" && mapped.posts.size() == 3 && mapped.terrain == dir / "maps" / "m");
    // Its natives: Ewoks, friends of the first team, enemies of the second.
    CHECK(mapped.natives && mapped.natives->team.name == "Ewoks" && mapped.natives->look == "ewok" &&
          mapped.natives->count == 6 && mapped.natives->friends == std::vector<int>{0} &&
          mapped.natives->team.loadouts.size() == 1 && &mapped.team(natives_team) == &mapped.natives->team);
    CHECK(mapped.allied(natives_team, 0) && mapped.allied(0, natives_team) && !mapped.allied(natives_team, 1) &&
          mapped.allied(1, 1) && !mapped.allied(0, 1) && !mapped.allied(-1, -1));
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
                                                      "Powerup = 20 1 30 3 2 0 1\n"
                                                      "Door = 50 0 60 3 1 50 1.5 64 2 1.5 0.2 1 0 0 0 1 0 0 0 1\n"
                                                      "Destructible = 850 10 1 40 1.5 1.3 1.5 20 0 2 3.5 comp 5\n"
                                                      "DeathRegion = box 70 0 70 2 2 2 1 0 0 0 1 0 0 0 1 standing 0\n"
                                                      "DeathRegion = box 90 0 90 2 2 2 1 0 0 0 1 0 0 0 1 timed 7 5 15\n\n"
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
    CHECK(bounded.death_regions.size() == 3 && bounded.death_regions[0].center == Vec3(-10, 0, 20));
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
    // A door: its trigger and box, x the other way; a shot through the box
    // meets it where it enters, one over it doesn't.
    CHECK(bounded.doors.size() == 1 && bounded.doors[0].trigger == Vec3(-50, 0, 60) &&
          bounded.doors[0].box.center == Vec3(-50, 1.5, 64) && bounded.doors[0].open_time == 1);
    const auto through = bounded.doors[0].hit({-50, 1.5, 60}, {-50, 1.5, 70});
    CHECK(through && near(*through, 0.38, 1e-6) && !bounded.doors[0].hit({-50, 3.5, 60}, {-50, 3.5, 70}));
    CHECK(bounded.doors[0].triggered_by({-51, 0, 61}) && !bounded.doors[0].triggered_by({-50, 0, 56}));
    // Something that can be destroyed: its health, bounds (x the other way),
    // explosion and name (spaces and all).
    CHECK(bounded.destructibles.size() == 1 && bounded.destructibles[0].max_health == 850 &&
          bounded.destructibles[0].center == Vec3(-10, 1, 40) && bounded.destructibles[0].half_size == Vec3(1.5, 1.3, 1.5) &&
          bounded.destructibles[0].explosion_outer == 2 && bounded.destructibles[0].regen == 3.5 &&
          bounded.destructibles[0].name == "comp 5");
    // A death region there only while it stands.
    CHECK(bounded.death_regions.size() == 3 && bounded.death_regions[0].while_standing == -1 &&
          bounded.death_regions[1].while_standing == 0);
    // One on a timer: on 7 s in for 5 s, off 15 s, and again.
    const DeathRegion& timed = bounded.death_regions[2];
    CHECK(timed.first == 7 && timed.on == 5 && timed.off == 15 && !timed.on_at(6.9) && timed.on_at(7.1) &&
          timed.on_at(11.9) && !timed.on_at(12.1) && !timed.on_at(26.9) && timed.on_at(27.1));
    {
        Conquest match(bounded);
        CHECK(match.deadly({-70, 0, 70}) && match.destructible_alive(0));
        CHECK(match.damage_destructible(0, 1000) && !match.deadly({-70, 0, 70}) && bounded.deadly({-70, 0, 70}));
        // It grows back: 850 at 3.5 a second, about 243 s; then the region's back.
        CHECK(match.state().destructibles[0] == -850.0f);
        for (int i = 0; i < 240 * 30; ++i) {
            match.step(1.0 / 30, {}, 0);
        }
        CHECK(!match.destructible_alive(0) && !match.deadly({-70, 0, 70}));
        for (int i = 0; i < 4 * 30; ++i) {
            match.step(1.0 / 30, {}, 0);
        }
        CHECK(match.destructible_alive(0) && match.state().destructibles[0] == 850.0f && match.deadly({-70, 0, 70}));
    }
    CHECK(bounded.destructibles[0].nearest({0, 0, 40}) == Vec3(-8.5, 0, 40));
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

void add_wall(MapCollision& collision, Vec3 low, Vec3 high, int16_t part = 0) {
    const auto corner = [&](int i) {
        return Vec3{i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z};
    };
    const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (const auto& f : faces) {
        collision.triangles.push_back({corner(f[0]), corner(f[2]), corner(f[1]), blocks_characters | blocks_shots, part});
        collision.triangles.push_back({corner(f[0]), corner(f[3]), corner(f[2]), blocks_characters | blocks_shots, part});
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

// Dropped health, ammo, dual and energy canisters: left untouched when full,
// consumed when needed, expire after 30 s, and hurt bots walk over to grab one.
void test_dropped_pickups() {
    // Drop table rolls match Battlefront's com_inf_default probabilities.
    CHECK(roll_pickup(0.0) == PickupKind::health_small);
    CHECK(roll_pickup(0.16) == PickupKind::health_large);
    CHECK(roll_pickup(0.25) == PickupKind::ammo);
    CHECK(roll_pickup(0.50) == PickupKind::dual);
    CHECK(roll_pickup(0.60) == PickupKind::energy);
    CHECK(!roll_pickup(0.80));

    Battlefield battlefield = three_posts();
    // Give team 0 a limited-ammo rifle and grenades so ammo pickups can be tested.
    WeaponDef rifle;
    rifle.ammo = 5;
    rifle.rounds_per_clip = 50;
    WeaponDef grenade;
    grenade.projectile = Projectile::grenade;
    grenade.ammo = 3;
    grenade.rounds_per_clip = 1;
    battlefield.teams[0].loadouts[0].weapons[primary_slot] = rifle;
    battlefield.teams[0].loadouts[0].weapons[secondary_slot] = grenade;
    battlefield.warmup = 60;
    Field field(battlefield);
    GameClient& a = field.join("a");
    field.run(0.5);
    a.request_spawn(0, 0);
    field.run(0.2);
    CHECK(a.alive());

    const Vec3 at = a.predicted().position;
    // Full health and full ammo: walking over a health or ammo canister leaves it alone.
    field.server->spawn_pickup(PickupKind::health_small, at);
    field.server->spawn_pickup(PickupKind::ammo, at);
    field.run(0.2);
    CHECK(field.server->pickup_count() == 2 && a.pickups().size() == 2);

    // Spend some ammo and grenades: the ammo canister is picked up at once (+1 clip).
    MoveState spent = *field.server->state_of(a.entity_id());
    spent.combat.weapons[primary_slot].reserve = 0;
    spent.combat.weapons[secondary_slot].rounds = 0;
    spent.combat.weapons[secondary_slot].reserve = 0;
    field.server->set_state(a.entity_id(), spent);
    field.run(0.2);
    CHECK(field.server->pickup_count() == 1);  // health canister still waits
    const MoveState* after_ammo = field.server->state_of(a.entity_id());
    CHECK(after_ammo != nullptr && after_ammo->combat.weapons[primary_slot].reserve == 50 &&
          after_ammo->combat.weapons[secondary_slot].rounds == 1);

    // Drop an energy pack and drain stamina: it refills stamina and disappears.
    MoveState tired = *after_ammo;
    tired.energy = 10;
    field.server->set_state(a.entity_id(), tired);
    field.server->spawn_pickup(PickupKind::energy, at);
    field.run(0.2);
    CHECK(field.server->pickup_count() == 1 &&
          near(field.server->state_of(a.entity_id())->energy, a.character_stats().move.max_energy, 1));

    // A canister no one picks up expires after its 30 s lifespan.
    field.run(30.5);
    CHECK(field.server->pickup_count() == 0 && a.pickups().empty());

    // A hurt bot walks over to a nearby dropped health canister and heals.
    Battlefield hurt_field = three_posts();
    hurt_field.warmup = 60;
    Field bot_field(hurt_field);
    GameClient& bot_client = bot_field.join("bot");
    GameClient& shooter = bot_field.join("shooter");
    Bot bot(7);
    bot_field.run(0.3);
    bot_client.request_spawn(0, 0);
    shooter.request_spawn(0, 2);
    bot_field.run(0.3);
    CHECK(bot_client.alive() && shooter.alive());
    // Put shooter 10 m from bot and shoot bot below half health, then disconnect shooter.
    MoveState s_state = *bot_field.server->state_of(shooter.entity_id());
    s_state.position = bot_client.predicted().position + Vec3{0, 0, 10};
    bot_field.server->set_state(shooter.entity_id(), s_state);
    bot_field.run(0.2);
    bot_field.run(2.0, [&] {
        const Vec3 d = bot_client.predicted().position + Vec3{0, 1.0, 0} -
                       (shooter.predicted().position + Vec3{0, eye_height, 0});
        shooter.set_input({.yaw = std::atan2(-d.x, -d.z), .pitch = std::atan2(d.y, std::hypot(d.x, d.z)),
                           .fire = bot_client.health() > 180});
    });
    shooter.disconnect();
    bot_field.run(0.2);
    CHECK(bot_client.alive() && bot_client.health() < 200);
    // Drop a large health pack 12 m away (off the bot's straight line to Middle):
    // it detours to grab the canister and heals +200.
    const Vec3 pack_at = bot_client.predicted().position + Vec3{0, 0, -12};
    bot_field.server->spawn_pickup(PickupKind::health_large, pack_at);
    bot_field.run(0.1);  // deliver the snapshot with the pickup
    const double before_pickup = bot_client.health();
    bool healed = false;
    bot_field.run(5.0, [&] {
        bot.update(bot_client, bot_field.world, bot_field.now);
        if (bot_field.server->pickup_count() == 0) {
            healed = true;
        }
    }, 1.0 / 30);
    std::printf("hurt bot and dropped health pack: before %.0f, after %.0f, picked up %d\n", before_pickup,
                bot_client.health(), int(healed));
    CHECK(healed && near(bot_client.health(), before_pickup + 200, 1));

    // Dying in a death region never drops a pickup there.
    Battlefield pit = three_posts();
    pit.death_regions.push_back({.center = {-60, 0, 0}, .size = {20, 2, 20}});
    pit.warmup = 60;
    Field pit_field(pit);
    GameClient& doomed = pit_field.join("doomed");
    pit_field.run(0.3);
    for (int i = 0; i < 6; ++i) {
        doomed.request_spawn(0, 0);
        pit_field.run(2.5);
    }
    CHECK(pit_field.server->pickup_count() == 0);
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

// Health and ammo droids take a fair amount of damage, blow up when destroyed,
// stop healing/rearming, bots stop walking to them, and they reset on match live.
void test_destructible_droids() {
    Battlefield battlefield = three_posts();
    battlefield.warmup = 60;
    battlefield.powerups.push_back({.position = {-50, 0, 0}, .radius = 3, .delay = 0.5, .health = 50, .ammo = 0});
    Field field(battlefield);
    GameClient& a = field.join("a");
    field.run(0.4);
    a.request_spawn(0, 0);
    field.run(0.4);
    CHECK(a.alive() && a.match() && a.match()->powerup_health.size() == 1);
    CHECK(near(a.match()->powerup_health[0], PowerupStation::default_max_health, 1));

    // Place A 6 m from the droid and shoot it: a few shots chip its health without
    // destroying it immediately, then sustained fire blows it up.
    MoveState placed = *field.server->state_of(a.entity_id());
    placed.position = {-56, 0, 0};
    field.server->set_state(a.entity_id(), placed);
    field.run(0.2);
    const Vec3 to_droid = Vec3{-50, 0.8, 0} - (a.predicted().position + Vec3{0, eye_height, 0});
    const float yaw = float(std::atan2(-to_droid.x, -to_droid.z));
    const float pitch = float(std::atan2(to_droid.y, std::hypot(to_droid.x, to_droid.z)));
    // A brief burst damages it, but it survives (takes a fair amount of damage).
    field.run(0.4, [&] { a.set_input({.yaw = yaw, .pitch = pitch, .fire = true}); });
    CHECK(a.match()->powerup_health[0] < PowerupStation::default_max_health && a.match()->powerup_health[0] > 0.0f);

    // Sustained fire destroys it and emits a droid explosion.
    bool droid_exploded = false;
    field.run(4.0, [&] {
        a.set_input({.yaw = yaw, .pitch = pitch, .fire = a.match()->powerup_health[0] > 0.0f});
        for (const auto& exp : a.take_explosions()) {
            if (exp.weapon == net::droid_explosion_weapon) {
                droid_exploded = true;
            }
        }
    });
    CHECK(a.match()->powerup_health[0] == 0.0f && droid_exploded);

    // Standing next to a destroyed droid no longer heals.
    Battlefield heal_test = three_posts();
    heal_test.warmup = 60;
    heal_test.damage_regions.push_back({.where = {.center = {-60, 0, 0}, .size = {10, 2, 10}}, .rate = 100});
    heal_test.powerups.push_back({.position = {-40, 0, 0}, .radius = 4, .delay = 0.25, .health = 50, .ammo = 0,
                                  .max_health = 100});
    Field hf(heal_test);
    GameClient& h = hf.join("h");
    hf.run(0.4);
    h.request_spawn(0, 0);
    hf.run(1.5);  // take some damage in the damage region
    MoveState by_droid = *hf.server->state_of(h.entity_id());
    by_droid.position = {-42, 0, 0};
    hf.server->set_state(h.entity_id(), by_droid);
    hf.run(0.2);
    // Destroy the droid with a grenade/shots.
    const Vec3 aim_d = Vec3{-40, 0.8, 0} - (h.predicted().position + Vec3{0, eye_height, 0});
    hf.run(1.5, [&] {
        h.set_input({.yaw = float(std::atan2(-aim_d.x, -aim_d.z)),
                     .pitch = float(std::atan2(aim_d.y, std::hypot(aim_d.x, aim_d.z))),
                     .fire = h.match()->powerup_health[0] > 0.0f});
    });
    CHECK(h.match()->powerup_health[0] == 0.0f);
    const double hp_after = h.health();
    hf.run(1.0, [&] { h.set_input({}); });
    CHECK(near(h.health(), hp_after, 0.5));  // broken droid gives no health
}

// Natives, friends of the West: two bots on their own team, spawning at the
// West's post's ally spawn points; they hunt the East's soldier and leave the
// West's alone.
void test_natives() {
    Battlefield battlefield = three_posts();
    battlefield.warmup = 120;
    battlefield.natives = Natives{.team = {"Ewoks", {Loadout{}}}, .look = "ewok", .count = 2, .friends = {0}};
    battlefield.posts[0].ally_spawn_points = {{-60, 0, 6}, {-60, 0, -6}};
    Field field(battlefield);
    GameClient& west = field.join("west");
    GameClient& east = field.join("east");
    uint32_t next_port = 70;
    ClientConfig base{.server = {1, 1}};
    base.battlefield = battlefield;
    BotPlayers bots(0, field.world, base, [&] {
        return std::make_unique<net::LoopbackLink>(field.hub, net::Endpoint{next_port++, 1});
    });
    field.run(0.5, [&] { bots.update(field.now); });
    west.request_spawn(0, 0);
    east.request_spawn(0, 2);
    field.run(0.5, [&] { bots.update(field.now); });
    CHECK(west.alive() && east.alive() && west.self().team == 0 && east.self().team == 1);
    // East, near the West's post: in the natives' sights; West beside them.
    MoveState near = *field.server->state_of(east.entity_id());
    near.position = {-45, 0, 0};
    field.server->set_state(east.entity_id(), near);
    MoveState beside = *field.server->state_of(west.entity_id());
    beside.position = {-58, 0, 0};
    field.server->set_state(west.entity_id(), beside);
    int natives = 0;
    field.run(20, [&] {
        bots.update(field.now);
        if (east.alive() && east.predicted().position.x > -40) {
            // Back where they can see it.
            MoveState again = *field.server->state_of(east.entity_id());
            again.position = {-45, 0, 0};
            field.server->set_state(east.entity_id(), again);
        }
        natives = 0;
        for (const GameClient::Remote& remote : west.remotes()) {
            natives += remote.team == natives_team ? 1 : 0;
        }
        if (!east.alive() && east.self().life == Life::waiting) {
            east.request_spawn(0, 2);
        }
    });
    std::printf("natives: %d on their team; the East's soldier died %d times, the West's %d\n", natives,
                east.self().deaths, west.self().deaths);
    CHECK(natives == 2 && east.self().deaths >= 1 && west.self().deaths == 0);
    bots.disconnect();
}

// A door between a soldier and a health droid: shut, it stops the soldier's
// shots; it opens while someone stands by it (its trigger, off to the side
// here), and then they go through; it shuts again once they've gone. Clients
// hear how open it is.
void test_doors() {
    Battlefield battlefield = three_posts();
    battlefield.warmup = 60;
    battlefield.powerups.push_back({.position = {-50, 0, 0}, .radius = 1, .delay = 1, .health = 1});
    battlefield.doors.push_back(
        {.trigger = {-53, 0, 10}, .radius = 3, .open_time = 1, .box = {.center = {-53, 1.5, 0}, .size = {0.2, 1.5, 2}}});
    Field field(battlefield);
    GameClient& a = field.join("a");
    GameClient& b = field.join("b");
    field.run(0.4);
    a.request_spawn(0, 0);
    b.request_spawn(0, 2);
    field.run(0.4);
    CHECK(a.alive() && b.alive() && a.match() && a.match()->doors.size() == 1 && a.match()->doors[0] == 0.0f);
    MoveState placed = *field.server->state_of(a.entity_id());
    placed.position = {-60, 0, 0};
    field.server->set_state(a.entity_id(), placed);
    field.run(0.2);
    const Vec3 to_droid = Vec3{-50, 0.8, 0} - (a.predicted().position + Vec3{0, eye_height, 0});
    const GameClient::Controls aim{.yaw = float(std::atan2(-to_droid.x, -to_droid.z)),
                    .pitch = float(std::atan2(to_droid.y, std::hypot(to_droid.x, to_droid.z)))};
    GameClient::Controls firing = aim;
    firing.fire = true;
    field.run(1, [&] { a.set_input(firing); });
    const float full = a.match()->powerup_health[0];
    CHECK(full == float(PowerupStation::default_max_health) && a.match()->doors[0] == 0.0f);

    // B by the trigger: it opens within its open time, and shots get through.
    MoveState by_door = *field.server->state_of(b.entity_id());
    by_door.position = {-53, 0, 11};
    field.server->set_state(b.entity_id(), by_door);
    field.run(0.5, [&] { a.set_input(aim); });
    CHECK(a.match()->doors[0] > 0.3f && a.match()->doors[0] < 0.7f);
    field.run(0.7, [&] { a.set_input(aim); });
    CHECK(a.match()->doors[0] == 1.0f);
    field.run(0.5, [&] { a.set_input(firing); });
    std::printf("door: droid at %.0f of %.0f after shooting through the open door\n", a.match()->powerup_health[0],
                full);
    CHECK(a.match()->powerup_health[0] < full);

    // B gone: it shuts.
    by_door.position = {-53, 0, 30};
    field.server->set_state(b.entity_id(), by_door);
    field.run(1.2, [&] { a.set_input(aim); });
    CHECK(a.match()->doors[0] == 0.0f);
}

// A wall right across the field that can be destroyed, between a bot and
// the post it's going for: stood, it stops shots and soldiers (the world's
// part); the bot, its route ending at it with no one to shoot, shoots it
// down, and its way then goes through. The world without it, everyone's.
void test_destructibles() {
    Battlefield battlefield = three_posts();
    battlefield.warmup = 120;
    battlefield.destructibles.push_back({.name = "barricade", .max_health = 400, .center = {-40, 1.5, 0},
                                         .half_size = {1, 1.5, 51}, .explosion_damage = 20, .explosion_outer = 3});
    Field field(battlefield);
    MapCollision wall;
    add_wall(wall, {-41, 0, -51}, {-39, 3, 51}, 1);
    field.world.add_map_collision(wall);
    const auto hit = field.world.raycast_hit({-50, 1, 0}, {-30, 1, 0});
    CHECK(hit && hit->destructible == 0 && field.world.walk_blocked({-45, 0, 0}, {-35, 0, 0}));
    field.world.set_destroyed(0, true);
    CHECK(!field.world.raycast_hit({-50, 1, 0}, {-30, 1, 0}) && !field.world.walk_blocked({-45, 0, 0}, {-35, 0, 0}));
    field.world.set_destroyed(0, false);

    // One bot (the West's), and someone on the other side watching.
    uint32_t next_port = 50;
    ClientConfig base{.server = {1, 1}};
    base.battlefield = battlefield;
    BotPlayers bots(1, field.world, base, [&] {
        return std::make_unique<net::LoopbackLink>(field.hub, net::Endpoint{next_port++, 1});
    });
    GameClient& watcher = field.join("watcher");
    double destroyed_at = -1, through_at = -1;
    field.run(90, [&] {
        bots.update(field.now);
        if (destroyed_at < 0 && watcher.match() && !watcher.match()->destructibles.empty() &&
            watcher.match()->destructibles[0] <= 0) {
            destroyed_at = field.now;
        }
        for (const GameClient::Remote& remote : watcher.remotes()) {
            if (through_at < 0 && remote.alive() && remote.state.position.x > -37) {
                through_at = field.now;
            }
        }
    });
    std::printf("destructibles: the bot's barricade down at %.1f s, the bot through at %.1f s\n", destroyed_at,
                through_at);
    CHECK(destroyed_at > 0 && through_at > destroyed_at && field.world.destroyed(0));
    bots.disconnect();
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
    int captures = 0, kills = 0, max_pickups = 0;
    int in_cover = 0, crouched_in_cover = 0;  // bot-ticks behind cover (and crouched there)
    const auto horizontal = [](Vec3 d) { return std::hypot(d.x, d.z); };
    std::vector<bool> healed(bots.size());  // reached a droid it was going to
    std::vector<bool> scavenged(bots.size());  // went for a dropped pickup
    while (field.now < 900 && !ended) {
        field.run(10, [&] {
            max_pickups = std::max(max_pickups, int(field.server->pickup_count()));
            for (size_t i = 0; i < bots.size(); ++i) {
                bots[i].update(*field.clients[i], field.world, field.now);
                if (const int droid = bots[i].powerup(); droid >= 0) {
                    healed[i] = healed[i] || battlefield.powerups[size_t(droid)].reaches(
                                                 field.clients[i]->predicted().position);
                }
                if (bots[i].pickup() != 0) {
                    scavenged[i] = true;
                }
                // In cover: there, and hidden from every enemy it can see.
                if (const auto& cover = bots[i].cover();
                    cover && horizontal(field.clients[i]->predicted().position - *cover) < 1.0) {
                    ++in_cover;
                    crouched_in_cover += field.clients[i]->predicted().crouched ? 1 : 0;
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
    const auto went_for_drops = std::count(scavenged.begin(), scavenged.end(), true);
    std::printf("bots: %d captures, %d kills; %d of %zu went to a health droid, %d went for dropped pickups (peak %d on ground)\n",
                captures, kills, int(at_droids), bots.size(), int(went_for_drops), max_pickups);
    std::printf("bots: %.0f bot-seconds in cover (%.0f crouched)\n", in_cover * dt, crouched_in_cover * dt);
    CHECK(result.winner >= 0 && at_droids >= 1 && max_pickups >= 1 && in_cover > 0);
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
    test_natives();
    test_doors();
    test_destructibles();
    test_drop_pace();
    test_battlefield_file();
    test_capture();
    test_tickets_run_out();
    test_match_server();
    test_death_region();
    test_damage_region();
    test_health_droid();
    test_destructible_droids();
    test_dropped_pickups();
    test_spawn_under_floor();
    test_bots_play_a_match();
    return swm::test::finish("match_test");
}
