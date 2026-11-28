// Stage 5 Milestone 3: Loot tables, corpse looting, junk dealer selling, gear
// vendor buying, supply usage, and SQLite credit/item persistence.

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "swbfo/core/client.h"
#include "swbfo/core/heightfield.h"
#include "swbfo/core/ini.h"
#include "swbfo/core/items.h"
#include "swbfo/core/loot.h"
#include "swbfo/core/net/link.h"
#include "swbfo/core/quest.h"
#include "swbfo/core/server.h"
#include "swbfo/core/sqlite_store.h"
#include "swbfo/core/wildlife.h"
#include "swbfo/core/world.h"
#include "test.h"

using namespace swbfo::core;

namespace {

std::filesystem::path repo_root() {
    auto p = std::filesystem::current_path();
    while (!p.empty() && !std::filesystem::exists(p / "game" / "items" / "swg_catalog.ini")) {
        auto parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return p;
}

void test_catalog_junk_and_supplies() {
    const auto root = repo_root();
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");

    const ItemDef* hide = catalog.find("junk_womp_rat_hide");
    CHECK(hide != nullptr);
    CHECK(hide->kind == ItemKind::junk);
    CHECK(hide->sell_value == 22);

    // The vendors' Bacta Stimpack is tactical gear, like a Doctor's medpack.
    const ItemDef* stim = catalog.find("supply_bacta_stim");
    CHECK(stim != nullptr);
    CHECK(stim->kind == ItemKind::weapon);
    CHECK(stim->slot == "secondary");
    CHECK(stim->cost_credits == 45);
    CHECK(stim->sell_value == 15);
    CHECK(stim->heal_amount == 100.0);
    CHECK(stim->carried == 2);

    const ItemDef* rations = catalog.find("supply_field_rations");
    CHECK(rations != nullptr && rations->kind == ItemKind::supply && rations->cost_credits == 25);

    const ItemDef* rifle = catalog.find("wp_carbine_ee3");
    CHECK(rifle != nullptr && rifle->cost_credits == 400 && rifle->sell_value == 100);
}

void test_tatooine_loot_tables() {
    const auto root = repo_root();
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");

    LootTableSet tables;
    tables.load(root / "game" / "hubs" / "tatooine_loot.ini");
    CHECK(tables.size() >= 19);

    const WorldSpawns spawns = load_world_spawns(root / "game" / "hubs" / "tatooine_spawns.ini");
    for (const CreatureArchetype& c : spawns.creatures) {
        CHECK(!c.loot_table.empty());
        const LootTable* lt = tables.find(c.loot_table);
        CHECK(lt != nullptr);
        for (const LootEntry& e : lt->junk_pool) CHECK(catalog.find(e.item_id) != nullptr);
        for (const LootEntry& e : lt->gear_pool) CHECK(catalog.find(e.item_id) != nullptr);
    }
    for (const BanditArchetype& b : spawns.bandits) {
        CHECK(!b.loot_table.empty());
        const LootTable* lt = tables.find(b.loot_table);
        CHECK(lt != nullptr);
        for (const LootEntry& e : lt->junk_pool) CHECK(catalog.find(e.item_id) != nullptr);
        for (const LootEntry& e : lt->gear_pool) CHECK(catalog.find(e.item_id) != nullptr);
    }

    // Roll 50 times on a bandit table and verify bounds, and grades it gives weight.
    uint64_t rng = 0x123456789ABCDEF0ull;
    const LootTable* tusken = tables.find("loot_tusken_raider");
    CHECK(tusken != nullptr);
    int gear_drops = 0;
    for (int i = 0; i < 50; ++i) {
        const RolledLoot r = tables.roll("loot_tusken_raider", rng);
        CHECK(r.credits >= tusken->credits_min && r.credits <= tusken->credits_max);
        CHECK(int(r.items.size()) >= tusken->junk_rolls_min);
        for (const RolledLootItem& it : r.items) {
            const ItemDef* def = catalog.find(it.def_id);
            CHECK(def != nullptr);
            if (def->kind != ItemKind::junk) {
                ++gear_drops;
                CHECK(it.grade != ItemGrade::legendary && tusken->gear_grade_weights[size_t(it.grade)] > 0);
            } else {
                CHECK(it.grade == ItemGrade::standard);
            }
        }
    }
    CHECK(gear_drops > 0);
}

// A table's gear drops at Mk tiers between GearMarkMin and GearMarkMax.
void test_loot_marks() {
    const std::filesystem::path ini = "swbfo_loot_marks_test.ini";
    {
        std::ofstream out(ini);
        out << "[loot_table]\nId = marked\nJunkMin = 0\nGearChance = 1\nGearMarkMin = 3\nGearMarkMax = 5\n"
            << "Gear = wp_rifle_e11\n"
            << "[loot_table]\nId = mk7\nJunkMin = 0\nGearChance = 1\nGearMarkMin = 7\nGearMarkMax = 7\n"
            << "Gear = wp_rifle_e11\n"
            << "[loot_table]\nId = plain\nJunkMin = 0\nGearChance = 1\nGearMarkMax = 14\nGear = wp_rifle_e11\n"
            << "[loot_table]\nId = fine\nJunkMin = 0\nGearChance = 1\nGearGrades = 0 0 1 0\nGear = wp_rifle_e11\n"
            << "[loot_table]\nId = split\nJunkMin = 0\nGearChance = 1\nGearGrades = 1 0 0 1\nGear = wp_rifle_e11\n";
    }
    LootTableSet tables;
    tables.load(ini);
    std::filesystem::remove(ini);
    CHECK(tables.find("plain")->gear_mark_max == max_mark);
    uint64_t rng = 99;
    std::array<int, max_mark + 1> seen{};
    for (int i = 0; i < 200; ++i) {
        const RolledLoot r = tables.roll("marked", rng);
        CHECK(r.items.size() == 1 && r.items[0].mark >= 3 && r.items[0].mark <= 5);
        ++seen[size_t(r.items[0].mark)];
        CHECK(tables.roll("mk7", rng).items[0].mark == 7);
    }
    CHECK(seen[3] > 0 && seen[4] > 0 && seen[5] > 0);

    // GearGrades weighs Flawed, Standard, Fine, Exceptional.
    std::array<int, item_grade_count> grades{};
    for (int i = 0; i < 200; ++i) {
        CHECK(tables.roll("fine", rng).items[0].grade == ItemGrade::fine);
        ++grades[size_t(tables.roll("split", rng).items[0].grade)];
    }
    CHECK(grades[0] > 50 && grades[3] > 50 && grades[0] + grades[3] == 200);
    {
        std::ofstream out(ini);
        out << "[loot_table]\nId = bad\nGearGrades = 1 2\n";
    }
    bool threw = false;
    try {
        LootTableSet bad;
        bad.load(ini);
    } catch (const std::exception&) {
        threw = true;
    }
    std::filesystem::remove(ini);
    CHECK(threw);
}

// Every item a hub's data names is in the catalog: an unknown id drops loot with
// no definition, and arms a bandit with the fallback blaster instead.
void test_hub_data_names_known_items() {
    const auto root = repo_root();
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");

    const auto known = [&](const std::string& id, const std::string& file) {
        const bool found = catalog.find(id) != nullptr;
        if (!found) std::fprintf(stderr, "%s names an unknown item: %s\n", file.c_str(), id.c_str());
        return found;
    };
    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root / "game" / "hubs")) {
        const std::string name = entry.path().filename().string();
        if (name.ends_with("_loot.ini")) {
            LootTableSet tables;
            tables.load(entry.path());
            const Ini ini = Ini::load(entry.path());
            for (const Ini::Section& s : ini.sections()) {
                const LootTable* t = tables.find(s.get("Id").value_or(""));
                if (t == nullptr) continue;
                for (const LootEntry& e : t->junk_pool) CHECK(known(e.item_id, name));
                for (const LootEntry& e : t->gear_pool) CHECK(known(e.item_id, name));
            }
        } else if (name.ends_with("_spawns.ini")) {
            for (const BanditArchetype& b : load_world_spawns(entry.path()).bandits) CHECK(known(b.weapon_id, name));
        } else if (name.ends_with("_quests.ini")) {
            QuestBook quests;
            quests.load(entry.path());
            for (const QuestDef& q : quests.quests()) {
                for (const std::string& id : q.reward.items) CHECK(known(id, name));
                for (const QuestStep& step : q.steps) {
                    if (step.kind == QuestStepKind::collect || step.kind == QuestStepKind::deliver) {
                        CHECK(known(step.target, name));
                    }
                }
            }
        } else {
            continue;
        }
        ++files;
    }
    CHECK(files >= 30);
    CHECK(BanditArchetype{}.weapon_id == "wp_rifle_e11" && catalog.find(BanditArchetype{}.weapon_id) != nullptr);
}

void test_corpse_loot_vendor_and_credit_persistence() {
    const auto root = repo_root();
    const auto db_path = std::filesystem::temp_directory_path() / "swbfo_loot_test.sqlite";
    std::filesystem::remove(db_path);

    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");

    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};

    {
        SqliteCharacterStore store(db_path);
        net::LoopbackHub links;
        net::Endpoint server_ep{1, 1}, client_ep{2, 1};
        net::LoopbackLink srv_link{links, server_ep};
        net::LoopbackLink cli_link{links, client_ep};

        ServerConfig scfg;
        scfg.catalog = &catalog;
        scfg.pvp_enabled = false;
        scfg.spawn = {0, 0, 0};

        GameServer server(srv_link, world, scfg, &store);
        server.set_vendors({
            {"Mos Eisley Junk Dealer", "junk_dealer", {12, 0, 0}, 4.0},
            {"Mos Eisley Gear & Supplies", "gear_vendor", {20, 0, 0}, 4.0},
        });

        GameServer::NpcCombatSpec mob_spec;
        mob_spec.actor_kind = 2;
        mob_spec.max_health = 50;
        const uint32_t mob_id = server.add_combat_npc("Lesser Womp Rat", {4, 0, 0}, 0.0, 1, mob_spec);

        server.on_npc_killed = [&](uint32_t npc_entity, uint32_t killer_entity) {
            if (npc_entity == mob_id && killer_entity != 0) {
                server.award_credits(killer_entity, 35);
                server.set_corpse_loot(npc_entity, killer_entity, "Lesser Womp Rat", {4, 0, 0}, {
                    {0, "junk_womp_rat_hide"},
                    {1, "junk_broken_blaster"},
                });
            }
        };

        GameClient client(cli_link, world, ClientConfig{.server = server_ep, .name = "LootHunter"});

        double now = 0.0;
        const auto step_both = [&](double seconds) {
            for (const double end = now + seconds; now < end; now += 1.0 / 60.0) {
                server.update(now);
                client.update(now);
            }
        };

        step_both(0.5);
        CHECK(client.status() == GameClient::Status::connected && client.spawned());
        const int64_t start_credits = client.player_status().credits;
        const size_t start_items = client.items().size();
        CHECK(client.vendors().size() == 2);

        // Shoot & kill the womp rat standing at (4, 0, 0)
        const Vec3 pos = client.predicted().position;
        const float aim_yaw = float(std::atan2(-(4.0 - pos.x), -(0.0 - pos.z)));
        client.set_input({.yaw = aim_yaw, .pitch = 0.0, .fire = true});
        step_both(0.6);
        client.set_input({.yaw = aim_yaw});
        step_both(0.2);

        // Credits were auto-looted and corpse loot is replicated
        CHECK(client.player_status().credits == start_credits + 35);
        CHECK(client.corpses().size() == 1);
        CHECK(client.corpses()[0].items.size() == 2);

        // Walk near corpse (4, 0, 0) and loot slot 0, then loot all
        client.set_input({.forward = 1.0, .yaw = aim_yaw});
        step_both(0.8);
        client.set_input({});
        client.take_loot(mob_id, 0);
        step_both(0.25);
        CHECK(client.items().size() == start_items + 1);
        CHECK(client.corpses().size() == 1 && client.corpses()[0].items.size() == 1);

        client.take_loot(mob_id, -1);
        step_both(0.25);
        CHECK(client.items().size() == start_items + 2);
        CHECK(client.corpses().empty());
        CHECK(server.corpse_loot_count() == 0);

        // Walk to Junk Dealer at (12, 0, 0) and sell all junk (+22 +32 = +54 Cr)
        const float east_yaw = float(std::atan2(-1.0, 0.0));
        client.set_input({.forward = 1.0, .yaw = east_yaw});
        for (int i = 0; i < 180 && client.predicted().position.x < 11.0; ++i) {
            step_both(1.0 / 60.0);
        }
        client.set_input({});
        step_both(0.1);
        client.sell_all_junk();
        step_both(0.25);
        CHECK(client.player_status().credits == start_credits + 35 + 22 + 32);
        CHECK(client.items().size() == start_items);

        // Walk to Gear & Supplies Vendor at (20, 0, 0) and buy Field Rations (25 Cr)
        client.set_input({.forward = 1.0, .yaw = east_yaw});
        for (int i = 0; i < 180 && client.predicted().position.x < 19.0; ++i) {
            step_both(1.0 / 60.0);
        }
        client.set_input({});
        step_both(0.1);
        client.buy_item("supply_field_rations", false);
        step_both(0.25);
        CHECK(client.player_status().credits == start_credits + 35 + 54 - 25);
        CHECK(client.items().size() == start_items + 1);

        // Damage player via NPC melee, then eat the rations
        server.npc_melee(mob_id, client.entity_id(), 100.0, DamageType::melee);
        step_both(0.15);
        const double hurt_hp = client.health();
        CHECK(hurt_hp < client.character_stats().max_health);

        uint64_t stim_id = 0;
        for (const ItemInstance& inst : client.items()) {
            if (inst.def_id == "supply_field_rations") {
                stim_id = inst.id;
                break;
            }
        }
        CHECK(stim_id != 0);
        // Just hit: it waits for the fight to be over (food heals out of combat).
        client.use_item(stim_id);
        step_both(0.25);
        CHECK(client.items().size() == start_items + 1);
        step_both(out_of_combat_delay + 0.5);
        const double rested_hp = client.health();
        client.use_item(stim_id);
        step_both(0.25);
        CHECK(client.health() > rested_hp || client.health() >= client.character_stats().max_health);
        CHECK(rested_hp >= hurt_hp);
        CHECK(client.items().size() == start_items);

        client.disconnect();
        step_both(0.1);
    }

    // Reopen SQLite store and verify credits persisted
    {
        SqliteCharacterStore store2(db_path);
        const auto saved = store2.find_by_name("LootHunter");
        CHECK(saved.has_value());
        CHECK(saved->credits == 500 + 35 + 54 - 25);
    }
    std::filesystem::remove(db_path);
}

} // namespace

int main() {
    test_catalog_junk_and_supplies();
    test_tatooine_loot_tables();
    test_loot_marks();
    test_hub_data_names_known_items();
    test_corpse_loot_vendor_and_credit_persistence();
    return swbfo::test::finish("loot_test");
}
