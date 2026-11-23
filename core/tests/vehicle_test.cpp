// Stage 6 vehicles: a hover vehicle's and a walker's handling, a vehicle
// file, and a match's vehicles: given at a spawn to its post's holder, got
// into and driven (predicted by its driver), shot until it blows up with
// its driver, and given again after its spawn time.

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>

#include "swm/core/client.h"
#include "swm/core/combat.h"
#include "swm/core/heightfield.h"
#include "swm/core/net/link.h"
#include "swm/core/server.h"
#include "swm/core/vehicle.h"
#include "swm/core/world.h"
#include "test.h"

using namespace swm::core;
using swm::test::near;

namespace {

constexpr double dt = 1.0 / 30;

VehicleClass bike() {
    VehicleClass v;
    v.name = "test_bike";
    v.label = "Bike";
    v.max_health = 100;
    v.forward_speed = 28;
    v.reverse_speed = 6;
    v.strafe_speed = 3;
    v.boost_speed = 40;
    v.acceleration = 30;
    v.boost_acceleration = 100;
    v.deceleration = 60;
    v.traction = 35;
    v.turn_rate = 1.4;
    v.altitude = 0.85;
    v.gravity_scale = 4;
    v.bounds_min = {-0.5, 0, -1.5};
    v.bounds_max = {0.5, 1.2, 1.5};
    WeaponDef gun;
    gun.name = "bike_gun";
    gun.rounds_per_clip = 0;
    gun.heat_per_shot = 0.1;
    gun.heat_recover_rate = 0.5;
    gun.shot_delay = 0.15;
    gun.damage = 150;
    gun.vehicle_factor = 0.1;
    v.loadout.weapons = {gun, std::nullopt, std::nullopt};
    return v;
}

void test_hover() {
    Heightfield flat{101, 1.0, -50, -50, std::vector<float>(101 * 101, 2.0f)};
    World world(flat);
    const VehicleClass v = bike();
    MoveState state;
    state.position = {0, 2.85, 0};
    state.vehicle = 0;
    MoveInput input;
    input.forward = 127;
    for (int i = 0; i < 60; ++i) {
        simulate_vehicle(state, input, world, v, dt);
    }
    // Two seconds at 30 m/s^2: at its forward speed, along -z, over the ground at its altitude.
    CHECK(near(state.velocity.length(), 28, 1e-6) && state.velocity.z < 0);
    CHECK(near(state.position.y, 2.85, 1e-9) && state.grounded);
    // Boosting: faster, draining energy.
    input.buttons = MoveInput::sprint;
    for (int i = 0; i < 15; ++i) {
        simulate_vehicle(state, input, world, v, dt);
    }
    CHECK(state.velocity.length() > 35 && state.energy < 100);
    // Turning toward the look, at its turn rate (1.4 rad/s).
    input.buttons = 0;
    input.yaw = float(std::numbers::pi / 2);
    const float before = state.yaw;
    simulate_vehicle(state, input, world, v, 0.5);
    CHECK(near(state.yaw - before, 0.7, 1e-5));
    // Nothing pressed: it slows to a stop (Deceleration 60).
    input = MoveInput{};
    input.yaw = state.yaw;
    for (int i = 0; i < 30; ++i) {
        simulate_vehicle(state, input, world, v, dt);
    }
    CHECK(state.velocity.length() < 1e-9);

    // Its guns aim within their limits round its heading.
    MoveInput aim;
    aim.yaw = state.yaw + 1.0f;
    aim.pitch = 0.5f;
    const auto [yaw, pitch] = vehicle_aim(state, aim, v);
    CHECK(near(yaw, state.yaw + v.yaw_max, 1e-6) && near(pitch, v.pitch_max, 1e-6));
    // Its box: a bolt across its middle hits; one over it doesn't.
    CHECK(segment_hits_vehicle(state.position + Vec3{-5, 0.5, 0}, state.position + Vec3{5, 0.5, 0}, state.position,
                               state.yaw, v)
              .has_value());
    CHECK(!segment_hits_vehicle(state.position + Vec3{-5, 3, 0}, state.position + Vec3{5, 3, 0}, state.position,
                                state.yaw, v)
               .has_value());
    CHECK(near(distance_to_vehicle(state.position + Vec3{0, 4.2, 0}, state.position, state.yaw, v), 3, 1e-6));

    // Driven through simulate_character: its guns fire from it.
    MoveState driven = state;
    reset_loadout(driven, v.loadout, CharacterStats{});
    MoveInput fire;
    fire.sequence = 1;
    fire.yaw = driven.yaw;
    fire.buttons = MoveInput::fire;
    const std::vector<VehicleClass> classes{v};
    const auto shot = simulate_character(driven, fire, true, world, Loadout{}, CharacterStats{}, dt, classes);
    CHECK(shot.has_value() && (shot->origin - vehicle_muzzle(driven, v)).length() < 1e-9);
}

void test_walker() {
    Heightfield flat{101, 1.0, -50, -50, std::vector<float>(101 * 101, 0.0f)};
    World world(flat);
    VehicleClass v;
    v.kind = VehicleKind::walker;
    v.forward_speed = 5;
    v.reverse_speed = v.strafe_speed = 3.5;
    v.turn_rate = 1.5;
    v.acceleration = v.deceleration = v.traction = 10;
    v.bounds_min = {-2, 0, -2};
    v.bounds_max = {2, 9, 2};
    MoveState state;
    MoveInput input;
    input.forward = 127;
    for (int i = 0; i < 60; ++i) {
        simulate_vehicle(state, input, world, v, dt);
    }
    CHECK(near(state.velocity.length(), 5, 1e-6) && near(state.position.y, 0, 1e-9) && state.grounded);
}

void test_vehicle_file() {
    const auto dir = std::filesystem::temp_directory_path() / "swm_vehicle_test";
    std::filesystem::create_directories(dir / "test_bike");
    std::ofstream(dir / "test_bike" / "test_bike.ini") << R"([vehicle]
Name = test_bike
Kind = hover
Label = Speederbike
MaxHealth = 80.0
ForwardSpeed = 28.0
SetAltitude = .85
TurnRate = 1.4
YawLimits = -15.0 15.0
PitchLimits = -5.0 5.0
EyePointOffset = 0.0 1.0 0.0
TrackCenter = 0 2.5 -6
Model = test_bike.gltf
Bounds = -0.5 -0.2 -1.6 0.5 1.1 1.7

[weapon]
Name = test_bike_cannon
Projectile = bolt
RoundsPerClip = 0
HeatPerShot = 0.15
MaxDamage = 150
PersonScale = 1
VehicleScale = 0.1
)";
    const VehicleClass v = load_vehicle_class(dir / "test_bike" / "test_bike.ini");
    CHECK(v.label == "Speederbike" && v.max_health == 80 && near(v.altitude, 0.85));
    CHECK(near(v.yaw_max, 15 * std::numbers::pi / 180) && near(v.bounds_min.z, -1.6) && near(v.bounds_min.y, -0.2));
    CHECK(v.model == dir / "test_bike" / "test_bike.gltf");
    CHECK(v.loadout.weapons[0] && v.loadout.weapons[0]->name == "test_bike_cannon");
    CHECK(v.loadout.weapons[0] && near(v.loadout.weapons[0]->vehicle_factor, 0.1));

    // A walker: its pace, its run on energy (an AT-ST's BoostSpeed), its
    // steps' own pace and its camera's distance.
    std::filesystem::create_directories(dir / "test_walker");
    std::ofstream(dir / "test_walker" / "test_walker.ini") << R"([vehicle]
Name = test_walker
Kind = walker
MaxSpeed = 5.0
MaxStrafeSpeed = 3.5
BoostSpeed = 10
BoostAcceleration = 100.0
EnergyBoostDrain = 20
TrackCenter = 0.0 9.25 -3.0
TrackOffset = 0.0 0.0 13.0
StrideSpeed = 6.806
Bounds = -2.4 0 -2.5 2.4 8.5 3.6
)";
    const VehicleClass w = load_vehicle_class(dir / "test_walker" / "test_walker.ini");
    CHECK(w.kind == VehicleKind::walker && near(w.forward_speed, 5) && near(w.boost_speed, 10));
    CHECK(near(w.boost_acceleration, 20) && near(w.energy_boost_drain, 20));  // no faster than twice its own
    CHECK(near(w.stride_speed, 6.806) && near(w.track_offset.z, 13));
    Heightfield flat{101, 1.0, -50, -50, std::vector<float>(101 * 101, 0.0f)};
    World world(flat);
    MoveState state;
    MoveInput run;
    run.forward = 127;
    run.buttons = MoveInput::sprint;
    for (int i = 0; i < 60; ++i) {
        simulate_vehicle(state, run, world, w, dt);
    }
    CHECK(near(Vec3{state.velocity.x, 0, state.velocity.z}.length(), 10, 1e-6) && state.energy < 100);
    std::filesystem::remove_all(dir);
}

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
            for (auto& client : clients) client->update(now);
        }
    }
    void run(double seconds) {
        run(seconds, [] {});
    }
};

void test_match_vehicles() {
    Battlefield field;
    field.teams[0] = {"Empire", {Loadout{}}, "imp"};
    field.teams[1] = {"Rebellion", {Loadout{}}, "all"};
    field.min_x = field.min_z = -150;
    field.max_x = field.max_z = 150;
    field.posts = {{.name = "West", .position = {-60, 0, 0}, .owner = 0},
                   {.name = "East", .position = {60, 0, 0}, .owner = 1}};
    field.warmup = 100;
    field.vehicles = {bike()};
    VehicleSpawn spawn;
    spawn.name = "bikes";
    spawn.position = {-55, 0, 10};
    spawn.yaw = 0;
    spawn.post = 0;
    spawn.vehicle = {0, -1};  // the Empire's, when it holds West
    spawn.respawn_time = 3;
    field.vehicle_spawns = {spawn};
    Field f(field);
    GameClient& a = f.join("a");
    GameClient& b = f.join("b");
    f.run(0.3);
    a.request_spawn(0, 0);
    b.request_spawn(0, 1);
    f.run(0.5);
    CHECK(a.alive() && b.alive() && a.self().team == 0);
    // The Empire holds West: its bike is there, empty, hovering.
    CHECK(a.vehicles().size() == 1);
    const net::VehicleState bike_state = a.vehicles().empty() ? net::VehicleState{} : a.vehicles()[0];
    CHECK(bike_state.driver == 0 && bike_state.team == 0 && near(bike_state.position.y, 0.85, 1e-6));

    // A walks over and gets in.
    const auto walk_to = [&](GameClient& c, Vec3 to) {
        const Vec3 d = to - c.predicted().position;
        return GameClient::Controls{.forward = std::hypot(d.x, d.z) > 1.5 ? 1.0 : 0.0, .yaw = std::atan2(-d.x, -d.z)};
    };
    f.run(4, [&] { a.set_input(walk_to(a, bike_state.position)); });
    CHECK((a.predicted().position - bike_state.position).length() < 3);
    f.run(0.1, [&] { a.set_input({.use = true}); });
    f.run(0.3, [&] { a.set_input({}); });
    CHECK(a.predicted().vehicle == 0 && a.self().state.vehicle == 0);
    CHECK(!a.vehicles().empty() && a.vehicles()[0].driver == a.entity_id());
    // Driving: fast, and the prediction agrees with the server.
    const Vec3 start = a.predicted().position;
    const auto corrections = a.stats().corrections;
    f.run(2, [&] { a.set_input({.forward = 1, .yaw = 0}); });
    CHECK((a.predicted().position - start).length() > 30);
    CHECK(a.stats().corrections - corrections <= 1);

    // Out again, going fast: thrown clear (ejected), then on foot beside
    // it, holding its own gun.
    f.run(0.1, [&] { a.set_input({.use = true}); });
    f.run(0.1, [&] { a.set_input({}); });
    CHECK(a.predicted().vehicle == -1 && a.vehicles()[0].driver == 0);
    CHECK(a.predicted().position.y > 0.05 && !a.predicted().grounded);
    f.run(1.5, [&] { a.set_input({}); });
    CHECK(near(a.predicted().position.y, 0, 1e-6));
    // It coasted on a little: back to it, and in.
    f.run(3, [&] { a.set_input(walk_to(a, a.vehicles()[0].position)); });
    // Back in, and B shoots the bike: 60 a bolt times VehicleScale (1, the
    // default E-11's) against 100: two hits blow it up, killing A.
    f.run(0.1, [&] { a.set_input({.use = true}); });
    f.run(0.3, [&] { a.set_input({}); });
    CHECK(a.predicted().vehicle == 0);
    bool destroyed = false;
    f.run(6, [&] {
        GameClient::Controls controls;
        if (!b.vehicles().empty() && b.vehicles()[0].alive) {
            const Vec3 target = b.vehicles()[0].position + Vec3{0, 0.4, 0};
            const Vec3 d = target - (b.predicted().position + Vec3{0, eye_height, 0});
            controls.yaw = std::atan2(-d.x, -d.z);
            controls.pitch = std::atan2(d.y, std::hypot(d.x, d.z));
            controls.fire = true;
        }
        b.set_input(controls);
        for (const auto& v : b.vehicles()) destroyed |= !v.alive;
    });
    CHECK(destroyed);
    CHECK(!a.alive() && a.self().deaths == 1 && b.self().kills == 1);
    CHECK(a.predicted().vehicle == -1);
    // After its spawn time, another at home.
    f.run(4, [&] { b.set_input({}); });
    CHECK(a.vehicles().size() == 1 && a.vehicles()[0].alive && a.vehicles()[0].driver == 0);
}

// A gunner's seat: a teammate gets in behind the driver, rides where the
// vehicle goes, can't be joined by the other side, gets out with its own
// gun, and dies with the vehicle.
void test_gunner_seat() {
    VehicleClass tank = bike();
    tank.label = "Tank";
    VehicleSeat turret;
    turret.name = "TURRET1";
    turret.eye = {0, 2, 0};
    turret.loadout.weapons = {WeaponDef{}, std::nullopt, std::nullopt};
    turret.loadout.weapons[0]->name = "turret_laser";
    tank.seats = {turret};
    Battlefield field;
    field.teams[0] = {"Empire", {Loadout{}}, "imp"};
    field.teams[1] = {"Rebellion", {Loadout{}}, "all"};
    field.min_x = field.min_z = -150;
    field.max_x = field.max_z = 150;
    field.posts = {{.name = "West", .position = {-60, 0, 0}, .owner = 0},
                   {.name = "East", .position = {60, 0, 0}, .owner = 1}};
    field.warmup = 100;
    field.vehicles = {tank};
    VehicleSpawn spawn;
    spawn.name = "tank";
    spawn.position = {-55, 0, 10};
    spawn.post = 0;
    spawn.vehicle = {0, -1};
    spawn.respawn_time = 30;
    field.vehicle_spawns = {spawn};
    Field f(field);
    GameClient& a = f.join("a");  // the Empire
    GameClient& b = f.join("b");  // the Rebellion
    GameClient& c = f.join("c");  // the Empire
    f.run(0.3);
    a.request_spawn(0, 0);
    b.request_spawn(0, 1);
    c.request_spawn(0, 0);
    f.run(0.5);
    CHECK(a.self().team == 0 && b.self().team == 1 && c.self().team == 0);
    const Vec3 at = a.vehicles().empty() ? Vec3{} : a.vehicles()[0].position;
    const auto walk_to = [&](GameClient& who, Vec3 to) {
        const Vec3 d = to - who.predicted().position;
        return GameClient::Controls{.forward = std::hypot(d.x, d.z) > 1.5 ? 1.0 : 0.0, .yaw = std::atan2(-d.x, -d.z)};
    };
    f.run(4, [&] {
        a.set_input(walk_to(a, at));
        c.set_input(walk_to(c, at));
    });
    // A drives; C, beside it, takes the gun.
    f.run(0.1, [&] { a.set_input({.use = true}); });
    f.run(0.2, [&] { a.set_input({}); });
    f.run(0.1, [&] { c.set_input({.use = true}); });
    f.run(0.3, [&] { c.set_input({}); });
    CHECK(a.self().state.vehicle == 0 && a.self().state.seat == 0);
    CHECK(c.self().state.vehicle == 0 && c.self().state.seat == 1);
    CHECK(!a.vehicles().empty() && a.vehicles()[0].driver == a.entity_id() &&
          a.vehicles()[0].gunners == std::vector<uint32_t>{c.entity_id()});
    // Driven off, the gunner rides along.
    f.run(1.5, [&] { a.set_input({.forward = 1, .yaw = 0}); });
    f.run(0.5, [&] { a.set_input({}); });
    const Vec3 tank_at = a.vehicles().empty() ? Vec3{} : a.vehicles()[0].position;
    CHECK((tank_at - at).length() > 10);
    CHECK((c.self().state.position - tank_at).length() < 1.5);
    // The gunner out (it's slowed: on its feet) with its own gun, and back in.
    f.run(2, [&] { a.set_input({}); });
    f.run(0.1, [&] { c.set_input({.use = true}); });
    f.run(0.3, [&] { c.set_input({}); });
    CHECK(c.self().state.vehicle == -1 && c.self().state.seat == 0);
    CHECK(!a.vehicles().empty() && a.vehicles()[0].gunners == std::vector<uint32_t>{0});
    f.run(0.1, [&] { c.set_input({.use = true}); });
    f.run(0.3, [&] { c.set_input({}); });
    CHECK(c.self().state.seat == 1);
    // Destroyed: both aboard die.
    bool destroyed = false;
    f.run(8, [&] {
        GameClient::Controls controls;
        if (!b.vehicles().empty() && b.vehicles()[0].alive) {
            const Vec3 target = b.vehicles()[0].position + Vec3{0, 0.4, 0};
            const Vec3 d = target - (b.predicted().position + Vec3{0, eye_height, 0});
            controls.yaw = std::atan2(-d.x, -d.z);
            controls.pitch = std::atan2(d.y, std::hypot(d.x, d.z));
            controls.fire = true;
            controls.forward = std::hypot(d.x, d.z) > 30 ? 1.0 : 0.0;
        }
        b.set_input(controls);
        for (const auto& v : b.vehicles()) destroyed |= !v.alive;
    });
    CHECK(destroyed);
    CHECK(!a.alive() && !c.alive() && b.self().kills == 2);
}

} // namespace

int main() {
    test_hover();
    test_walker();
    test_vehicle_file();
    test_match_vehicles();
    test_gunner_seat();
    return swm::test::finish("vehicle_test");
}
