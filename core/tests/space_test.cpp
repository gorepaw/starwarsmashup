// Space combat simulation: 3D flight below origin, hangar-only landing,
// Space Assault scoring, ShieldLinks, DestroyLinks, TurretMainframes, turret
// control chairs, mobile transport spawns, automated turrets, and exported
// space battlefields / Mos Eisley hub integration.

#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

#include "swm/core/bot.h"
#include "swm/core/client.h"
#include "swm/core/conquest.h"
#include "swm/core/heightfield.h"
#include "swm/core/hub.h"
#include "swm/core/net/link.h"
#include "swm/core/server.h"
#include "swm/core/vehicle.h"
#include "swm/core/world.h"
#include "test.h"

namespace swm::core {
namespace {

Heightfield flat_ground(float y = 0.0f) {
    return Heightfield(9, 100.0, -400.0, -400.0, std::vector<float>(81, y));
}

WeaponDef test_cannon() {
    WeaponDef w;
    w.name = "laser";
    w.projectile = Projectile::bolt;
    w.damage = 500;
    w.bolt_speed = 400;
    w.bolt_life = 2.0;
    w.shot_delay = 0.2;
    return w;
}

VehicleClass space_fighter(bool transport = false) {
    VehicleClass v;
    v.name = transport ? "test_transport" : "test_fighter";
    v.label = transport ? "Transport" : "Starfighter";
    v.kind = VehicleKind::flyer;
    v.is_transport = transport;
    if (transport) {
        v.spawn_points = {{0, 0, 0}};
    }
    v.max_health = 1000;
    v.max_speed = 80;
    v.min_speed = 25;
    v.boost_speed = 110;
    v.boost_acceleration = 60;
    v.turn_rate = 2.5;
    v.pitch_rate = 2.5;
    v.takeoff_time = 0.5;
    v.takeoff_height = 4;
    v.landing_speed = 12;
    v.landed_height = 0.5;
    v.energy_restore = 10;
    v.bounds_min = {-2, -1, -3};
    v.bounds_max = {2, 1, 3};
    v.loadout.weapons[0] = test_cannon();
    return v;
}

VehicleClass space_turret() {
    VehicleClass t;
    t.name = "test_turret";
    t.label = "Defense Turret";
    t.kind = VehicleKind::turret;
    t.max_health = 2000;
    t.turn_rate = 4.0;
    t.pitch_rate = 4.0;
    t.pitch_min = -0.8;
    t.pitch_max = 0.8;
    t.eye = {0, 1.5, 0};
    t.bounds_min = {-2, 0, -2};
    t.bounds_max = {2, 2, 2};
    t.loadout.weapons[0] = test_cannon();
    return t;
}

Loadout marine_loadout() {
    Loadout lo;
    lo.name = "Marine";
    lo.weapons[0] = test_cannon();
    return lo;
}

struct SpaceRig {
    Heightfield ground = flat_ground();
    World world{ground};
    net::LoopbackHub hub;
    net::LoopbackLink server_link{hub, {1, 1}};
    std::unique_ptr<GameServer> server;
    std::vector<std::unique_ptr<net::LoopbackLink>> links;
    std::vector<std::unique_ptr<GameClient>> clients;

    explicit SpaceRig(const Battlefield& field) {
        ServerConfig cfg;
        cfg.battlefield = field;
        cfg.respawn_delay = 1;
        server = std::make_unique<GameServer>(server_link, world, cfg);
    }

    GameClient& join(const std::string& name) {
        links.push_back(std::make_unique<net::LoopbackLink>(hub, net::Endpoint{uint32_t(10 + links.size()), 1}));
        ClientConfig cc{.server = {1, 1}, .name = name};
        cc.battlefield = server->match()->battlefield();
        clients.push_back(std::make_unique<GameClient>(*links.back(), world, cc));
        return *clients.back();
    }

    void step(double seconds, double t0 = 0) {
        constexpr double dt = 1.0 / 60.0;
        const int steps = int(std::round(seconds / dt));
        for (int i = 0; i < steps; ++i) {
            const double now = t0 + i * dt;
            server->update(now);
            for (auto& c : clients) {
                c->update(now);
            }
        }
    }
};

void test_space_flight_and_hangar_landing() {
    Battlefield field;
    field.name = "Space Flight";
    field.score_limit = 180;
    field.min_fly_y = -1800;
    field.max_fly_y = 1800;
    DeathRegion hangar;
    hangar.shape = DeathRegion::Shape::box;
    hangar.center = {0, 0, 0};
    hangar.size = {40, 20, 40};
    field.landing_regions.push_back(hangar);
    DeathRegion abyss;
    abyss.shape = DeathRegion::Shape::box;
    abyss.center = {0, -1000, 0};
    abyss.size = {2000, 50, 2000};
    abyss.characters_only = true;
    field.death_regions.push_back(abyss);

    CHECK(field.is_space());
    CHECK(field.in_landing_region({0, 5, 0}));
    CHECK(!field.in_landing_region({200, 0, 0}));
    const Conquest c(field);
    CHECK(c.deadly({200, -1000, 200}, true));
    CHECK(!c.deadly({200, -1000, 200}, false));

    const Heightfield space_floor = flat_ground(-2000.0f);
    const World space_world(space_floor);
    const VehicleClass f = space_fighter();
    MoveState state;
    state.position = {0, 0.5, 0};
    state.grounded = true;
    constexpr double dt = 1.0 / 60.0;

    // Take off and continuously turn right while pitching sharply down past y = 0.
    MoveInput input;
    input.forward = 127;
    input.pitch = -1.25f;
    for (int i = 0; i < 6 * 60; ++i) {
        input.yaw -= float(1.2 * dt);
        simulate_vehicle(state, input, space_world, f, dt, &field);
    }
    CHECK(!state.grounded);
    CHECK(state.position.y < -50.0);  // dives below y = 0 in open space
    CHECK(state.pitch < -1.1);        // deeper than ground-map 1.0 rad pitch cap
    CHECK(state.roll > 0.05);         // banks into continuous right turns

    // Open-space slow fly near y = 0 does NOT land outside LandingRegion.
    const Heightfield hangar_floor = flat_ground(0.0f);
    const World hangar_world(hangar_floor);
    state.position = {250, 0.5, 0};
    state.velocity = {0, 0, -5};
    state.pitch = 0;
    state.yaw = 0;
    input = MoveInput{};
    input.forward = -127;
    simulate_vehicle(state, input, hangar_world, f, dt, &field);
    CHECK(!state.grounded);

    // Slow level approach with throttle back inside hangar LandingRegion DOES land smoothly.
    state.position = {0, 2.0, 0};
    state.velocity = {0, 0, -10};
    state.pitch = 0;
    for (int i = 0; i < 120; ++i) {
        simulate_vehicle(state, input, hangar_world, f, dt, &field);
    }
    CHECK(state.grounded);
    CHECK(state.velocity.length() < 1e-6);
}

void test_space_systems_and_scoring() {
    Battlefield field;
    field.name = "Space Assault Rules";
    field.score_limit = 50;
    field.warmup = 0;
    field.results = 5;
    field.min_players = 1;
    field.min_x = -2000;
    field.max_x = 2000;
    field.min_z = -2000;
    field.max_z = 2000;
    field.teams[0] = {"Empire", {marine_loadout()}};
    field.teams[1] = {"Rebellion", {marine_loadout()}};
    field.vehicles = {space_fighter(), space_turret()};

    CommandPost cp0{.name = "Imp", .position = {0, 0, 20}, .owner = 0, .capturable = false};
    cp0.spawn_points = {{0, 0, 20}};
    cp0.spawn_heights = true;
    CommandPost cp1{.name = "All", .position = {0, 0, -20}, .owner = 1, .capturable = false};
    cp1.spawn_points = {{0, 0, -20}};
    cp1.spawn_heights = true;
    field.posts = {cp0, cp1};

    for (int i = 0; i < 4; ++i) {
        Destructible d;
        d.name = "sys" + std::to_string(i);
        d.center = {double(i) * 20, 0, -100};
        d.half_size = {5, 5, 5};
        d.max_health = 100;
        field.destructibles.push_back(d);
    }
    field.shield_links.push_back({0, {1}});
    field.destroy_links.push_back({2, {3}});
    field.turret_mainframes.push_back({0, {"reb_turret"}});
    field.space_systems = {
        {0, 1, "shieldgen", 20, "Shields"},
        {1, 1, "bridge", 15, "Bridge"},
        {2, 1, "engines_int", 0, "Engine Console"},
        {3, 1, "engines", 15, "Main Engine"},
    };
    VehicleSpawn ts;
    ts.name = "reb_turret";
    ts.position = {50, 0, -50};
    ts.vehicle = {1, 1};
    ts.turret_team = 1;
    field.vehicle_spawns.push_back(ts);

    Conquest c(field);
    c.step(1.0 / 30.0, {}, 2);
    CHECK(c.state().phase == Phase::live);
    CHECK(c.destructible_shielded(1));
    CHECK(!c.damage_destructible(1, 500));  // blocked while shield generator stands
    CHECK(!c.turret_disabled("reb_turret"));
    CHECK(c.damage_destructible(0, 100));   // destroy shield generator -> unshields bridge & disables mainframe
    c.add_score(0, 20);
    CHECK(!c.destructible_shielded(1));
    CHECK(c.turret_disabled("reb_turret"));

    c.killed(1, true);   // Rebel flyer destroyed -> +2 points for Empire
    c.killed(1, false);  // Rebel soldier killed -> +1 point for Empire
    c.step(1.0 / 30.0, {}, 2);
    CHECK(c.state().scores[0] == 23);

    c.add_score(0, 27);  // hits 50 score_limit -> Empire wins
    c.step(1.0 / 30.0, {}, 2);
    CHECK(c.state().phase == Phase::ended && c.state().winner == 0);

    // Server & client networked verification: player 0 kills player 1 on foot (+1 score).
    SpaceRig rig(field);
    auto& p0 = rig.join("ImpSoldier");
    auto& p1 = rig.join("RebTarget");
    rig.step(0.2);
    p0.request_spawn(0, 0);
    p1.request_spawn(0, 1);
    rig.step(0.5, 0.2);
    CHECK(p0.spawned() && p1.spawned());
    CHECK(p0.match() && p0.match()->score_limit == 50);

    GameClient::Controls fire;
    fire.fire = true;
    fire.yaw = 0;  // ImpSoldier at z=200 aims -Z toward RebTarget at z=-200
    p0.set_input(fire);
    rig.step(1.5, 0.7);
    CHECK(p0.match()->scores[0] >= 1);
}

void test_turret_chair_and_mobile_transport() {
    Battlefield field;
    field.name = "Transport And Chairs";
    field.score_limit = 180;
    field.warmup = 0;
    field.results = 5;
    field.min_players = 1;
    field.min_x = -1000;
    field.max_x = 1000;
    field.min_z = -1000;
    field.max_z = 1000;
    field.min_fly_y = -500;
    field.max_fly_y = 500;
    field.teams[0] = {"Rebellion", {marine_loadout()}};
    field.teams[1] = {"Empire", {marine_loadout()}};
    field.vehicles = {space_fighter(true), space_turret()};

    DeathRegion hangar;
    hangar.shape = DeathRegion::Shape::box;
    hangar.center = {0, 0, 0};
    hangar.size = {80, 30, 80};
    field.landing_regions.push_back(hangar);

    CommandPost cp0{.name = "AllHangar", .position = {0, 0, 10}, .owner = 0, .capturable = false};
    cp0.spawn_points = {{0, 0, 0}};
    cp0.spawn_heights = true;
    CommandPost cp1{.name = "ImpHangar", .position = {0, 0, -500}, .owner = 1, .capturable = false};
    cp1.spawn_points = {{0, 0, -500}};
    cp1.spawn_heights = true;
    field.posts = {cp0, cp1};

    VehicleSpawn gunship;
    gunship.name = "gunship";
    gunship.position = {20, 0.5, 0};
    gunship.post = 0;
    gunship.vehicle = {0, -1};
    field.vehicle_spawns.push_back(gunship);

    VehicleSpawn turret;
    turret.name = "all_turret";
    turret.position = {0, 10, -200};
    turret.post = 0;
    turret.vehicle = {1, -1};
    turret.turret_team = 0;
    field.vehicle_spawns.push_back(turret);

    field.turret_chairs.push_back({.name = "chair1", .position = {0, 0, 0}, .yaw = 0, .team = 0});

    SpaceRig rig(field);
    auto& p0 = rig.join("ChairUser");      // Team 0 (Rebellion)
    auto& enemy = rig.join("EnemySpot");   // Team 1 (Empire)
    auto& p1 = rig.join("MobilePassenger"); // Team 0 (Rebellion)
    rig.step(0.2);
    p0.request_spawn(0, 0);
    enemy.request_spawn(0, 1);
    rig.step(0.5, 0.2);
    CHECK(p0.spawned());

    // Board remote hull turret via turret chair, then exit back to the chair inside the ship.
    GameClient::Controls use;
    use.use = true;
    p0.set_input(use);
    rig.step(0.15, 0.7);
    CHECK(p0.predicted().vehicle == 1);  // sitting in remote turret

    p0.set_input(GameClient::Controls{});
    rig.step(0.25, 0.85);
    p0.set_input(use);
    rig.step(0.15, 1.1);
    CHECK(p0.predicted().vehicle < 0);
    CHECK((p0.predicted().position - Vec3{0, 0, 0}).length() < 3.0);

    // Move near transport, board it, fly out so mobile_spawn becomes active, and spawn p1 onto it.
    p0.set_input(GameClient::Controls{});
    rig.step(0.3, 1.25);
    GameClient::Controls right;
    right.right = 1.0;
    right.yaw = 0;
    for (int i = 0; i < 240; ++i) {
        p0.set_input(right);
        rig.step(1.0 / 60.0, 1.55 + i / 60.0);
        if ((p0.predicted().position - Vec3{20, 0, 0}).length() < 4.5) break;
    }
    p0.set_input(use);
    rig.step(0.15, 5.6);
    CHECK(p0.predicted().vehicle == 0);

    GameClient::Controls fly;
    fly.forward = 1.0;
    p0.set_input(fly);
    rig.step(2.0, 5.75);
    CHECK(!p0.vehicles().empty() && (p0.vehicles()[0].flags & net::VehicleState::flag_mobile_spawn) != 0);

    p1.request_spawn(0, int(field.posts.size()));  // mobile transport post index
    rig.step(0.3, 7.75);
    CHECK(p1.spawned() && p1.alive());
}

void test_exported_space_assets() {
    const std::filesystem::path root = std::filesystem::current_path();
    std::filesystem::path game;
    for (auto dir = root; !dir.empty(); dir = dir.parent_path()) {
        if (std::filesystem::exists(dir / "game" / "battlefields")) {
            game = dir / "game";
            break;
        }
    }
    if (game.empty() || !std::filesystem::exists(game / "imported_assets" / "maps" / "spa1" / "spa1_assault.ini")) {
        return;  // exported BF2 assets absent on CI/clean tree
    }
    const HubConfig mos_eisley = load_hub(game / "hubs" / "mos_eisley.ini");
    CHECK(mos_eisley.battlefields.size() >= 19);

    for (const char* name : {"space_yavin.ini", "space_hoth.ini", "space_tatooine.ini", "space_kashyyyk.ini",
                             "space_mygeeto.ini", "space_felucia.ini"}) {
        const Battlefield f = load_battlefield(game / "battlefields" / name);
        CHECK(f.is_space());
        CHECK(f.score_limit == 180);
        CHECK(f.posts.size() == 2);
        CHECK(!f.landing_regions.empty());
        CHECK(!f.turret_chairs.empty());
        CHECK(!f.space_systems.empty());
        CHECK(!f.vehicles.empty());
    }
}

} // namespace
} // namespace swm::core

int main() {
    swm::core::test_space_flight_and_hangar_landing();
    swm::core::test_space_systems_and_scoring();
    swm::core::test_turret_chair_and_mobile_transport();
    swm::core::test_exported_space_assets();
    return swm::test::finish("space_test");
}
