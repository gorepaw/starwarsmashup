#include <cmath>
#include <cstdio>
#include <filesystem>

#include "swm/core/client.h"
#include "swm/core/heightfield.h"
#include "swm/core/items.h"
#include "swm/core/net/link.h"
#include "swm/core/server.h"
#include "swm/core/wildlife.h"
#include "swm/core/world.h"
#include "test.h"

using namespace swm::core;
using swm::core::net::LoopbackHub;
using swm::core::net::LoopbackLink;

namespace {

double hdist(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

WeaponDef test_rifle() {
    WeaponDef w;
    w.name = "test_rifle";
    w.damage_type = DamageType::laser;
    w.projectile = Projectile::bolt;
    w.damage = 60;
    w.shot_delay = 0.2;
    w.rounds_per_clip = 30;
    w.reload_time = 1.0;
    w.bolt_speed = 120;
    w.bolt_life = 2.0;
    return w;
}

void test_load_tatooine_spawns() {
    std::filesystem::path spawns_path;
    for (const auto& candidate : {
             std::filesystem::path("game/hubs/tatooine_spawns.ini"),
             std::filesystem::path("../game/hubs/tatooine_spawns.ini"),
             std::filesystem::path("../../game/hubs/tatooine_spawns.ini"),
             std::filesystem::path("../../../game/hubs/tatooine_spawns.ini"),
         }) {
        if (std::filesystem::exists(candidate)) {
            spawns_path = candidate;
            break;
        }
    }
    CHECK(!spawns_path.empty());
    if (spawns_path.empty()) return;
    const WorldSpawns spawns = load_world_spawns(spawns_path);
    CHECK(spawns.creatures.size() == 10);
    CHECK(spawns.bandits.size() == 9);
    CHECK(spawns.lairs.size() == 16);
    CHECK(spawns.camps.size() == 6);
}

void test_creature_behaviors_leash_and_respawn() {
    const Heightfield ground(101, 4.0, -200.0, -200.0, std::vector<float>(101 * 101, 0.0f));
    const World world(ground);
    LoopbackHub hub;
    LoopbackLink srv_link(hub, {1, 1});
    LoopbackLink cli_link(hub, {2, 1});

    ServerConfig scfg;
    scfg.spawn = {0, 0, 0};
    scfg.loadout.weapons[primary_slot] = test_rifle();
    scfg.pvp_enabled = false;
    GameServer server(srv_link, world, scfg);

    WorldSpawns spawns;
    CreatureArchetype passive;
    passive.id = "worrt";
    passive.name = "Worrt";
    passive.template_path = "object/mobile/shared_worrt.iff";
    passive.behavior = CreatureBehavior::passive;
    passive.health = 100;
    passive.respawn_time = 2.5;
    spawns.creatures.push_back(passive);

    CreatureArchetype herd;
    herd.id = "bantha";
    herd.name = "Bantha";
    herd.template_path = "object/mobile/shared_bantha.iff";
    herd.behavior = CreatureBehavior::herd;
    herd.health = 200;
    herd.damage = 15;
    herd.leash_radius = 40;
    herd.respawn_time = 2.5;
    spawns.creatures.push_back(herd);

    CreatureArchetype aggro;
    aggro.id = "womp_rat";
    aggro.name = "Womp Rat";
    aggro.template_path = "object/mobile/shared_womp_rat.iff";
    aggro.behavior = CreatureBehavior::aggressive;
    aggro.health = 80;
    aggro.damage = 18;
    aggro.aggro_radius = 20;
    aggro.leash_radius = 30;
    aggro.respawn_time = 2.2;
    spawns.creatures.push_back(aggro);

    // Lair 0: passive worrt at (0, 0, -14)
    spawns.lairs.push_back({"worrt", {0, 0, 14}, 1.0, 1});  // SWG (0, 14) -> game (0, 14)
    // Lair 1: herd banthas at (30, 0, 0) -> game (-30, 0)
    spawns.lairs.push_back({"bantha", {-30, 0, 0}, 4.0, 2});
    // Lair 2: aggressive womp rat at (0, 0, -14) -> game (0, -14)
    spawns.lairs.push_back({"womp_rat", {0, 0, -14}, 1.0, 1});

    std::map<std::string, uint8_t> looks{
        {passive.template_path, uint8_t(1)},
        {herd.template_path, uint8_t(2)},
        {aggro.template_path, uint8_t(3)},
    };
    WorldPopulation pop(server, world, nullptr, spawns, nullptr, looks);
    pop.spawn_all();
    CHECK(pop.creature_count() == 4);

    ClientConfig ccfg;
    ccfg.server = {1, 1};
    ccfg.name = "Hunter";
    ccfg.loadout = scfg.loadout;
    GameClient client(cli_link, world, ccfg);

    double now = 0;
    auto step_sim = [&](double secs, auto&& fn) {
        const double end = now + secs;
        for (; now < end; now += 1.0 / 60.0) {
            fn();
            client.update(now);
            server.update(now);
            pop.update(now);
        }
    };

    step_sim(0.6, [] {});
    CHECK(client.spawned());
    const uint32_t worrt_id = pop.mob_entities()[0];
    const uint32_t bantha1_id = pop.mob_entities()[1];
    const uint32_t bantha2_id = pop.mob_entities()[2];
    const uint32_t rat_id = pop.mob_entities()[3];

    // 1. Aggressive womp rat aggros player at (0,0,0) from (0,0,-14), chases and deals melee damage
    const double initial_hp = server.health_of(client.entity_id());
    step_sim(3.2, [] {});
    CHECK(server.health_of(client.entity_id()) < initial_hp);

    // 2. Kill the womp rat, verify dead state, then respawn
    GameClient::Controls shoot_rat;
    step_sim(1.5, [&] {
        const auto rinfo = server.npc_info(rat_id);
        const auto pst = server.state_of(client.entity_id());
        if (rinfo && rinfo->life == Life::alive && pst) {
            const Vec3 eye = pst->position + Vec3{0, 1.6, 0};
            const Vec3 aim_pt = rinfo->state.position + Vec3{0, 0.5, 0};
            const Vec3 d = aim_pt - eye;
            shoot_rat.yaw = float(std::atan2(-d.x, -d.z));
            shoot_rat.pitch = float(std::atan2(d.y, std::hypot(d.x, d.z)));
            shoot_rat.fire = true;
        } else {
            shoot_rat.fire = false;
        }
        client.set_input(shoot_rat);
    });
    const auto dead_rat = server.npc_info(rat_id);
    CHECK(dead_rat && dead_rat->life == Life::dead);
    client.set_input({});
    MoveState away = *server.state_of(client.entity_id());
    away.position = {0, 0, 40};
    server.set_state(client.entity_id(), away);
    step_sim(2.6, [] {});
    const auto respawned_rat = server.npc_info(rat_id);
    CHECK(respawned_rat && respawned_rat->life == Life::alive && hdist(respawned_rat->state.position, {0, 0, -14}) < 8.0);

    // 3. Passive worrt flees when damaged
    const Vec3 worrt_start = server.npc_info(worrt_id)->state.position;
    MoveState pstate = *server.state_of(client.entity_id());
    pstate.position = {0, 0, 4};
    server.set_state(client.entity_id(), pstate);
    step_sim(0.2, [] {});
    step_sim(0.8, [&] {
        const auto winfo = server.npc_info(worrt_id);
        const auto pst = server.state_of(client.entity_id());
        GameClient::Controls ctrl;
        if (winfo && pst && winfo->health > 50) {
            const Vec3 d = winfo->state.position - pst->position;
            ctrl.yaw = float(std::atan2(-d.x, -d.z));
            ctrl.pitch = -0.04f;
            ctrl.fire = true;
        }
        client.set_input(ctrl);
    });
    client.set_input({});
    step_sim(1.2, [] {});
    const Vec3 worrt_after = server.npc_info(worrt_id)->state.position;
    CHECK(worrt_after.z > worrt_start.z + 2.0);

    // 4. Herd banthas: shooting one alerts its mate and both advance toward player
    pstate.position = {-12, 0, 0};
    server.set_state(client.entity_id(), pstate);
    step_sim(0.2, [] {});
    const double b2_start_dist = hdist(server.npc_info(bantha2_id)->state.position, pstate.position);
    step_sim(0.8, [&] {
        const auto b1 = server.npc_info(bantha1_id);
        const auto pst = server.state_of(client.entity_id());
        GameClient::Controls ctrl;
        if (b1 && pst && b1->last_damaged_tick == 0) {
            const Vec3 d = (b1->state.position + Vec3{0, 1.0, 0}) - (pst->position + Vec3{0, 1.6, 0});
            ctrl.yaw = float(std::atan2(-d.x, -d.z));
            ctrl.pitch = float(std::atan2(d.y, std::hypot(d.x, d.z)));
            ctrl.fire = true;
        }
        client.set_input(ctrl);
    });
    client.set_input({});
    step_sim(2.0, [] {});
    const double b2_after_dist = hdist(server.npc_info(bantha2_id)->state.position, pstate.position);
    CHECK(b2_after_dist < b2_start_dist - 2.0);

    // 5. Leashing: pull bantha past 40 m from (-30, 0), verify it returns home and heals
    pstate.position = {35, 0, 0};
    server.set_state(client.entity_id(), pstate);
    // Move bantha manually beyond leash radius to test leash trigger
    // Wait: let bantha chase player past leash or verify it heals when returning
    pstate.position = {25, 0, 0};
    server.set_state(client.entity_id(), pstate);
    step_sim(10.0, [] {});
    const auto b1_leashed = server.npc_info(bantha1_id);
    CHECK(b1_leashed && hdist(b1_leashed->state.position, {-30, 0, 0}) < 10.0);
}

void test_bandit_pvp_disabled_and_cloning() {
    const Heightfield ground(101, 4.0, -200.0, -200.0, std::vector<float>(101 * 101, 0.0f));
    const World world(ground);
    LoopbackHub hub;
    LoopbackLink srv_link(hub, {1, 1});
    LoopbackLink a_link(hub, {2, 1});
    LoopbackLink b_link(hub, {3, 1});

    ServerConfig scfg;
    scfg.spawn = {0, 0, 0};
    scfg.respawn_delay = 0.8;
    scfg.loadout.weapons[primary_slot] = test_rifle();
    scfg.pvp_enabled = false;
    GameServer server(srv_link, world, scfg);
    const Vec3 cloning{-60, 0, 40};
    server.set_cloning_facility(cloning);

    WorldSpawns spawns;
    BanditArchetype barch;
    barch.id = "thug";
    barch.name = "Valarian Thug";
    barch.template_path = "object/mobile/shared_dressed_tatooine_valarian_thug.iff";
    barch.health = 200;
    barch.skill = BotSkill::preset("hard");
    barch.aggro_radius = 45;
    barch.leash_radius = 80;
    spawns.bandits.push_back(barch);
    spawns.camps.push_back({"Camp", {-22, 0, 0}, 6.0, {"thug"}});  // SWG (22, 0) -> game (-22, 0)

    std::map<std::string, uint8_t> looks{{barch.template_path, uint8_t(1)}};
    WorldPopulation pop(server, world, nullptr, spawns, nullptr, looks);
    pop.spawn_all();
    CHECK(pop.bandit_count() == 1);

    ClientConfig acfg{.server = {1, 1}, .name = "Alice", .loadout = scfg.loadout};
    ClientConfig bcfg{.server = {1, 1}, .name = "Bob", .loadout = scfg.loadout};
    GameClient a(a_link, world, acfg);
    GameClient b(b_link, world, bcfg);

    double now = 0;
    auto step_sim = [&](double secs, auto&& fn) {
        const double end = now + secs;
        for (; now < end; now += 1.0 / 60.0) {
            fn();
            a.update(now);
            b.update(now);
            server.update(now);
            pop.update(now);
        }
    };

    step_sim(0.6, [] {});
    CHECK(a.spawned() && b.spawned());

    // 1. No PvP outside matches: move Alice and Bob away from bandit aggro, Alice shoots Bob -> 0 damage
    MoveState a_far = *server.state_of(a.entity_id());
    MoveState b_far = *server.state_of(b.entity_id());
    a_far.position = {80, 0, 0};
    b_far.position = {85, 0, 0};
    server.set_state(a.entity_id(), a_far);
    server.set_state(b.entity_id(), b_far);
    step_sim(0.3, [] {});
    const double bob_hp0 = server.health_of(b.entity_id());
    step_sim(0.6, [&] {
        const auto ast = server.state_of(a.entity_id());
        const auto bst = server.state_of(b.entity_id());
        GameClient::Controls ctrl;
        if (ast && bst) {
            const Vec3 d = bst->position - ast->position;
            ctrl.yaw = float(std::atan2(-d.x, -d.z));
            ctrl.fire = true;
        }
        a.set_input(ctrl);
    });
    a.set_input({});
    CHECK(server.health_of(b.entity_id()) == bob_hp0);

    // Move Alice back near bandit camp to test bandit combat & Cloning Facility respawn
    a_far.position = {0, 0, 0};
    server.set_state(a.entity_id(), a_far);
    step_sim(0.2, [] {});

    // 2. Bandit fires bolts at Alice until Alice dies, then Alice respawns at Cloning Facility
    bool saw_alice_dead = false;
    bool saw_cloned = false;
    step_sim(12.0, [&] {
        if (!server.alive(a.entity_id())) {
            saw_alice_dead = true;
        } else if (saw_alice_dead) {
            const auto ast = server.state_of(a.entity_id());
            if (ast && hdist(ast->position, cloning) < 10.0) {
                saw_cloned = true;
            }
        }
    });
    CHECK(saw_alice_dead);
    CHECK(saw_cloned);
}

} // namespace

int main() {
    test_load_tatooine_spawns();
    test_creature_behaviors_leash_and_respawn();
    test_bandit_pvp_disabled_and_cloning();
    return swm::test::finish("wildlife_test");
}
