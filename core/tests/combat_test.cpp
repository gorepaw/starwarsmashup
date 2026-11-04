// Weapons (fire rate, clips, heat, switching, spread), soldier files, bolt
// hits, and server-side combat with lag compensation over a simulated network.

#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>

#include "swm/core/client.h"
#include "swm/core/combat.h"
#include "swm/core/net/link.h"
#include "swm/core/server.h"
#include "swm/core/world.h"
#include "test.h"

using namespace swm::core;
using swm::test::near;

namespace {

constexpr double dt = 1.0 / 30;

// Holds `buttons` for `ticks` ticks; returns how many shots came out.
int hold(MoveState& state, const SoldierDef& soldier, uint8_t buttons, int ticks, uint32_t& sequence, int weapon = 0) {
    int shots = 0;
    for (int i = 0; i < ticks; ++i) {
        MoveInput input;
        input.sequence = ++sequence;
        input.buttons = buttons;
        input.weapon = uint8_t(weapon);
        shots += simulate_weapons(state, input, soldier, dt) ? 1 : 0;
    }
    return shots;
}

void test_soldier_file() {
    const auto path = std::filesystem::temp_directory_path() / "swm_combat_test_soldier.ini";
    std::ofstream(path) << "[soldier]\nName = imp_inf_rifleman\nMaxHealth = 300\nMaxSpeed = 6.5\n"
                           "JumpHeight = 1.25\nControlSpeed = stand  1.00 1.00 1.00\n"
                           "ControlSpeed = sprint 1.40 0.60 0.35\n\n"
                           "[weapon]\nName = rifle\nShotDelay = 0.25\nRoundsPerClip = 30\nMaxDamage = 50.0\n"
                           "PersonScale = 0.5\nVelocity = 250\n\n"
                           "[weapon]\nName = pistol\nRoundsPerClip = 0\nHeatPerShot = 0.2\n";
    const SoldierDef soldier = load_soldier(path);
    std::filesystem::remove(path);
    CHECK(soldier.name == "imp_inf_rifleman" && soldier.max_health == 300 && soldier.move.run_speed == 6.5);
    CHECK(near(soldier.move.jump_speed, std::sqrt(2 * 9.8 * 1.25)));
    CHECK(soldier.move.sprint_factor == 1.4 && soldier.move.sprint_strafe == 0.6);
    CHECK(soldier.weapons.size() == 2 && soldier.weapons[0].name == "rifle" && soldier.weapons[0].damage == 25);
    CHECK(soldier.weapons[0].rounds_per_clip == 30 && soldier.weapons[0].bolt_speed == 250);
    CHECK(soldier.weapons[1].rounds_per_clip == 0 && soldier.weapons[1].heat_per_shot == 0.2);
}

void test_rifle() {
    SoldierDef soldier;  // E-11: a shot every 0.166 s, 50-round clips
    soldier.weapons[0].rounds_per_clip = 12;
    MoveState state;
    reset_loadout(state, soldier);
    uint32_t sequence = 0;
    // Five ticks a shot at 30 Hz: six shots in a second.
    CHECK(hold(state, soldier, MoveInput::fire, 30, sequence) == 6);
    CHECK(state.combat.weapons[0].rounds == 6);
    // Emptying the clip, then pulling the trigger again, reloads.
    CHECK(hold(state, soldier, MoveInput::fire, 35, sequence) == 6 && state.combat.reload > 0);
    // The reload started 4 ticks ago and takes 1.5 s (45 ticks).
    CHECK(hold(state, soldier, MoveInput::fire, 40, sequence) == 0);
    CHECK(hold(state, soldier, 0, 3, sequence) == 0 && state.combat.weapons[0].rounds == 12);
    // Reloading by hand, from a partial clip.
    hold(state, soldier, MoveInput::fire, 1, sequence);
    hold(state, soldier, 0, 10, sequence);
    hold(state, soldier, MoveInput::reload, 1, sequence);
    CHECK(state.combat.reload > 0);
    hold(state, soldier, 0, 46, sequence);
    CHECK(state.combat.weapons[0].rounds == 12 && state.combat.reload == 0);
}

void test_heat() {
    SoldierDef soldier;
    WeaponDef pistol;  // Battlefront's com_weap_inf_pistol: no clip, overheats
    pistol.rounds_per_clip = 0;
    pistol.shot_delay = 0.2;
    pistol.heat_per_shot = 0.175;
    pistol.heat_recover_rate = 0.3;
    pistol.heat_threshold = 0.25;
    soldier.weapons = {pistol};
    MoveState state;
    reset_loadout(state, soldier);
    uint32_t sequence = 0;
    int shots = 0;
    int ticks = 0;
    while (!state.combat.weapons[0].overheated && ticks < 300) {
        shots += hold(state, soldier, MoveInput::fire, 1, sequence);
        ++ticks;
    }
    CHECK(state.combat.weapons[0].overheated && shots >= 7 && shots <= 10);
    // Overheated: no shots until it cools to the threshold (0.75 at 0.3/s: 2.5 s).
    CHECK(hold(state, soldier, MoveInput::fire, 70, sequence) == 0);
    CHECK(hold(state, soldier, MoveInput::fire, 10, sequence) >= 1 && !state.combat.weapons[0].overheated);
}

void test_switch_and_spread() {
    SoldierDef soldier;
    WeaponDef pistol;
    pistol.rounds_per_clip = 0;
    soldier.weapons.push_back(pistol);
    MoveState state;
    reset_loadout(state, soldier);
    uint32_t sequence = 0;
    // Switching takes 0.4 s (12 ticks) before the next shot.
    CHECK(hold(state, soldier, MoveInput::fire, 12, sequence, 1) == 0 && state.combat.weapon == 1);
    CHECK(hold(state, soldier, MoveInput::fire, 2, sequence, 1) == 1);

    // Spread is deterministic per input, and within the cone.
    MoveState a, b;
    reset_loadout(a, soldier);
    reset_loadout(b, soldier);
    MoveInput input;
    input.sequence = 77;
    input.buttons = MoveInput::fire;
    input.yaw = 0.5f;
    const auto shot_a = simulate_weapons(a, input, soldier, dt);
    const auto shot_b = simulate_weapons(b, input, soldier, dt);
    CHECK(shot_a && shot_b && shot_a->direction == shot_b->direction);
    const Vec3 aim = aim_direction(0.5, 0);
    const double off = std::acos(std::min(1.0, aim.x * shot_a->direction.x + aim.y * shot_a->direction.y +
                                                   aim.z * shot_a->direction.z));
    CHECK(off < 0.33 * std::numbers::pi / 180);  // first shot: base spread only (0.225 degrees per axis)
    input.sequence = 78;
    MoveState c;
    reset_loadout(c, soldier);
    CHECK(!(simulate_weapons(c, input, soldier, dt)->direction == shot_a->direction));
}

void test_cylinder() {
    const Vec3 feet{0, 0, 0};
    CHECK(near(*segment_hits_cylinder({-10, 1, 0}, {10, 1, 0}, feet, 0.5, 1.8), 9.5 / 20));
    CHECK(!segment_hits_cylinder({-10, 2.5, 0}, {10, 2.5, 0}, feet, 0.5, 1.8));  // over the head
    CHECK(!segment_hits_cylinder({-10, 1, 0.6}, {10, 1, 0.6}, feet, 0.5, 1.8));  // beside
    CHECK(!segment_hits_cylinder({-10, 1, 0}, {-5, 1, 0}, feet, 0.5, 1.8));      // short of it
    CHECK(near(*segment_hits_cylinder({0, 5, 0}, {0, -5, 0}, feet, 0.5, 1.8), 3.2 / 10));  // from above
}

// A server and two clients on flat ground, 12 m apart along x.
struct Arena {
    double now = 0;
    Heightfield ground{101, 1.0, -50, -50, std::vector<float>(101 * 101, 0.0f)};
    World world{ground};
    net::LoopbackHub hub;
    net::LoopbackLink server_link{hub, {1, 1}};
    net::LoopbackLink a_raw{hub, {2, 1}};
    net::LoopbackLink b_link{hub, {3, 1}};
    net::ImpairedLink a_link;
    GameServer server;
    GameClient a{a_link, world, ClientConfig{.server = {1, 1}, .name = "a"}};
    GameClient b{b_link, world, ClientConfig{.server = {1, 1}, .name = "b"}};

    Arena(double latency, double max_rewind, BuildingCollision building = {})
        : a_link(a_raw, {.latency = latency, .seed = 5}, [this] { return now; }),
          server(server_link, world, config(max_rewind)) {
        world.add_building(building, {});
    }

    static ServerConfig config(double max_rewind) {
        ServerConfig c;
        c.spawn = {0, 0, 0};
        c.spawn_spacing = 12;
        c.max_rewind = max_rewind;
        return c;
    }

    template <typename Steer>
    void run(double seconds, Steer steer) {
        for (const double end = now + seconds; now < end; now += 1.0 / 120) {
            steer();
            server.update(now);
            a.update(now);
            b.update(now);
        }
    }

    // A aims at B's chest where A sees B, and fires.
    GameClient::Controls a_aims_at_b(bool fire) {
        GameClient::Controls controls;
        controls.fire = fire;
        const auto remotes = a.remotes();
        if (remotes.empty()) {
            return controls;
        }
        const Vec3 eye = a.predicted().position + Vec3{0, eye_height, 0};
        const Vec3 to = remotes[0].state.position + Vec3{0, 1.0, 0} - eye;
        controls.yaw = std::atan2(-to.x, -to.z);
        controls.pitch = std::atan2(to.y, std::hypot(to.x, to.z));
        return controls;
    }
};

void test_kill_and_respawn() {
    Arena arena(0, 0.5);
    arena.run(0.5, [] {});
    CHECK(arena.a.spawned() && arena.b.spawned() && arena.a.health() == 400);
    const uint32_t b_id = arena.b.entity_id();
    const Vec3 b_spawn = arena.b.predicted().position;
    bool killed = false;
    int hits = 0;
    arena.run(3.0, [&] {
        arena.a.set_input(arena.a_aims_at_b(arena.server.alive(b_id)));
        for (const auto& event : arena.a.take_events()) {
            hits += event.kind == net::CombatEvent::hit && event.victim == b_id;
            killed |= event.kind == net::CombatEvent::kill && event.victim == b_id &&
                      event.attacker == arena.a.entity_id();
        }
    });
    // 400 health at 60 a bolt: seven hits.
    CHECK(killed && hits == 7 && !arena.server.alive(b_id) && !arena.b.alive() && arena.b.health() == 0);
    CHECK(arena.a.stats().corrections == 0);  // A's ammo and cooldowns were predicted exactly
    CHECK(arena.a.take_shots().size() >= 7);
    // B walks away while dead: the dead stand still.
    const Vec3 dead_at = arena.b.predicted().position;
    arena.run(1.0, [&] { arena.b.set_input({.forward = 1}); });
    CHECK(arena.b.predicted().position == dead_at);
    // Respawn 5 s after dying, at full health, at its spawn point.
    arena.run(4.5, [&] { arena.b.set_input({}); });
    CHECK(arena.server.alive(b_id) && arena.b.alive() && arena.b.health() == 400);
    CHECK(near(arena.b.predicted().position.x, b_spawn.x) && near(arena.b.presented().position.x, b_spawn.x, 0.01));
}

// B strafes across A's view while A, 100 ms away from the server each way,
// shoots where it sees B. With rewinding, those shots land.
int strafing_hits(double max_rewind) {
    Arena arena(0.1, max_rewind);
    arena.run(1.0, [] {});
    const uint32_t b_id = arena.b.entity_id();
    int hits = 0;
    arena.run(2.0, [&] {
        arena.a.set_input(arena.a_aims_at_b(true));
        arena.b.set_input({.right = 1, .yaw = float(-std::numbers::pi / 2)});  // facing +X, strafing toward -Z
        for (const auto& event : arena.a.take_events()) {
            hits += event.kind == net::CombatEvent::hit && event.victim == b_id;
        }
    });
    return hits;
}

void test_lag_compensation() {
    const int rewound = strafing_hits(0.5);
    const int raw = strafing_hits(0);
    CHECK(rewound >= 6 && raw <= 1);
    std::printf("lag compensation: %d hits with rewinding, %d without\n", rewound, raw);
}

void test_wall_blocks() {
    BuildingCollision wall;
    wall.walls.push_back({{6, 0, -5}, {6, 0, 5}, 3});  // between A (x 0) and B (x 12)
    Arena arena(0, 0.5, wall);
    arena.run(0.5, [] {});
    arena.run(2.0, [&] { arena.a.set_input(arena.a_aims_at_b(true)); });
    CHECK(arena.a.take_shots().size() >= 10 && arena.server.health_of(arena.b.entity_id()) == 400);
}

} // namespace

int main() {
    test_soldier_file();
    test_rifle();
    test_heat();
    test_switch_and_spread();
    test_cylinder();
    test_kill_and_respawn();
    test_lag_compensation();
    test_wall_blocks();
    return swm::test::finish("combat_test");
}
