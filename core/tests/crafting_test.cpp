// Stage 7 gathering, Material Vault, SQLite v6, and Unified Workbench crafting tests.

#include <algorithm>
#include <filesystem>
#include <vector>

#include "swm/core/client.h"
#include "swm/core/crafting.h"
#include "swm/core/heightfield.h"
#include "swm/core/items.h"
#include "swm/core/net/link.h"
#include "swm/core/profession.h"
#include "swm/core/server.h"
#include "swm/core/sqlite_store.h"
#include "swm/core/world.h"
#include "test.h"

using namespace swm::core;

namespace {

std::filesystem::path repo_root() {
    auto p = std::filesystem::current_path();
    while (!p.empty() && !std::filesystem::exists(p / "game" / "crafting" / "resources.ini")) {
        auto parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return p;
}

void test_crafting_book() {
    const auto root = repo_root();
    CraftingBook book;
    book.load_resources(root / "game" / "crafting" / "resources.ini");
    book.load_schematics(root / "game" / "crafting" / "schematics.ini");
    CHECK(book.resources().size() == 16);
    CHECK(book.schematics().size() == 31);  // with Stage 12's pharmaceuticals and droid repair kits
    CHECK(book.work_orders().size() == 5);
    const SchematicDef* std_ammo_sch = book.find_schematic("sch_ammo_tibanna_standard");
    CHECK(std_ammo_sch != nullptr && std_ammo_sch->output_count == 100);

    CHECK(resource_category_label(ResourceCategory::mineral) == "Mineral");
    CHECK(resource_stat_label(ResourceStat::conductivity) == "Conductivity");

    const ScanResult scan = book.scan_terrain(ResourceCategory::mineral, -3520.0, -4790.0, 60.0, 17);
    CHECK(!scan.best_resource_id.empty());
    CHECK(!scan.best_resource_name.empty());
    CHECK(scan.concentration > 0.0 && scan.concentration <= 1.0);
    CHECK(scan.grid.size() == 17 * 17);

    const SchematicDef* pistol = book.find_schematic("sch_wp_pistol_d18");
    CHECK(pistol != nullptr);
    std::vector<MaterialStack> vault;
    for (const ResourceDef& r : book.resources()) {
        add_material(vault, r.id, 50);
    }
    const auto best_q = book.auto_fill(*pistol, vault, AutoFillMode::best_quality);
    const auto best_s = book.auto_fill(*pistol, vault, AutoFillMode::best_stat);
    const auto cheap = book.auto_fill(*pistol, vault, AutoFillMode::cheapest);
    CHECK(best_q.size() == 2 && !best_q[0].empty() && !best_q[1].empty());
    CHECK(best_s.size() == 2 && !best_s[0].empty() && !best_s[1].empty());
    CHECK(cheap.size() == 2 && !cheap[0].empty() && !cheap[1].empty());

    const CraftPreview preview = book.preview_craft(*pistol, best_s, 1, 25, 65);
    CHECK(preview.base_quality > 0.0 && preview.base_quality <= 1.0);
    CHECK(preview.max_quality >= preview.base_quality);
    const auto [great_q, great_label] = CraftingBook::roll_craft_quality(preview, 0.01);
    const auto [flaw_q, flaw_label] = CraftingBook::roll_craft_quality(preview, 0.99);
    CHECK(great_q >= flaw_q);
    CHECK(great_label == "Great Success");
    CHECK(flaw_label == "Minor Flaw");

    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    const ItemDef* d18 = catalog.find("wp_pistol_d18");
    CHECK(d18 != nullptr);
    const auto salvage = book.salvage_yield(*d18, 10);
    CHECK(!salvage.materials.empty());
    CHECK(salvage.xp > 0);
}

void test_sqlite_v6_materials_and_presets() {
    const auto db_path = std::filesystem::temp_directory_path() / "swm_crafting_v6_test.sqlite";
    std::filesystem::remove(db_path);
    {
        SqliteCharacterStore store(db_path);
        const auto ch = store.create("Crafter", {0, 0, 0}, 0.0f);
        std::vector<MaterialStack> mats{{"res_durasteel", 42}, {"res_tibanna_gas", 15}, {"res_empty", 0}};
        store.save_materials(ch.id, mats);
        const auto loaded_mats = store.list_materials(ch.id);
        CHECK(loaded_mats.size() == 2);
        CHECK(material_amount(loaded_mats, "res_durasteel") == 42);
        CHECK(material_amount(loaded_mats, "res_tibanna_gas") == 15);

        RecipePreset preset;
        preset.name = "Militia D-18";
        preset.schematic_id = "sch_wp_pistol_d18";
        preset.resource_ids = {"res_durasteel", "res_tibanna_gas"};
        preset.enhancement_defs = {"mod_precision_barrel"};
        preset.exp_quality_pct = 75;
        store.save_preset(ch.id, preset);

        const auto presets = store.list_presets(ch.id);
        CHECK(presets.size() == 1);
        CHECK(presets[0] == preset);

        CHECK(store.delete_preset(ch.id, "Militia D-18"));
        CHECK(store.list_presets(ch.id).empty());
    }
    std::filesystem::remove(db_path);
}

void test_server_client_crafting_loopback() {
    const auto root = repo_root();
    const auto db_path = std::filesystem::temp_directory_path() / "swm_crafting_srv_test.sqlite";
    std::filesystem::remove(db_path);

    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    ProfessionBook professions;
    professions.load(root / "game" / "professions" / "professions.ini");
    CraftingBook crafting;
    crafting.load_resources(root / "game" / "crafting" / "resources.ini");
    crafting.load_schematics(root / "game" / "crafting" / "schematics.ini");

    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    {
        SqliteCharacterStore store(db_path);
        net::LoopbackHub links;
        net::Endpoint server_ep{1, 1}, client_ep{2, 1};
        net::LoopbackLink srv_link{links, server_ep};
        net::LoopbackLink cli_link{links, client_ep};

        ServerConfig config;
        config.catalog = &catalog;
        config.professions = &professions;
        config.crafting = &crafting;
        config.pvp_enabled = false;
        config.spawn = {0, 0, 0};
        config.extract_time = 0.1;  // (the rules of extraction have their own test)
        config.extract_spacing = 0;

        GameServer server(srv_link, world, config, &store);
        server.set_looks({"object/mobile/shared_dressed_commoner_tatooine_rodian_male_01.iff"});
        // At first only an Armor & Clothing station in reach (a weapon one far off).
        server.set_crafting(&crafting, {{"Armor & Clothing Crafting Station", {3, 0, 3}, 4.0, "clothing"},
                                        {"Weapon & Droid Crafting Station", {80, 0, 80}, 4.0, "weapon"}});

        GameClient client(cli_link, world, ClientConfig{.server = server_ep, .name = "Artisan"});
        double now = 0.0;
        const auto run = [&](double seconds) {
            for (const double end = now + seconds; now < end; now += 1.0 / 60.0) {
                server.update(now);
                client.update(now);
            }
        };
        run(0.5);
        CHECK(client.status() == GameClient::Status::connected);
        CHECK(client.workbenches().size() == 2);
        CHECK(client.workbenches()[0].name == "Armor & Clothing Crafting Station");
        CHECK(client.workbenches()[0].kind == "clothing");
        // The stations' professions.
        CHECK(station_makes("weapon", "weaponsmith") && station_makes("weapon", "droid_engineer"));
        CHECK(!station_makes("weapon", "tailor") && !station_makes("clothing", "weaponsmith"));
        CHECK(station_makes("clothing", "armorsmith") && station_makes("food", "doctor"));
        CHECK(station_makes("structure", "architect") && station_makes("general", "chef"));
        CHECK(station_name(station_for("chef")) == "Food & Chemical Crafting Station");

        // Extract mineral + gas twice so we have plenty for a batch of 2 + 1 spare D-18 pistols.
        for (int i = 0; i < 3; ++i) {
            run(1.6);
            client.gather_extract(ResourceCategory::mineral);
            run(1.6);
            client.gather_extract(ResourceCategory::gas);
        }
        run(0.3);

        const auto& mats = client.crafting().materials;
        CHECK(!mats.empty());
        CHECK(profession_xp(client.professions().professions, "surveying") > 0);

        // Find extracted mineral & gas IDs in vault.
        std::string min_id, gas_id;
        for (const MaterialStack& st : mats) {
            const ResourceDef* r = crafting.find_resource(st.resource_id);
            if (r && r->category == ResourceCategory::mineral && st.amount >= 12) min_id = r->id;
            if (r && r->category == ResourceCategory::gas && st.amount >= 6) gas_id = r->id;
        }
        CHECK(!min_id.empty() && !gas_id.empty());

        // Save and verify a RecipePreset.
        client.save_recipe_preset("D-18 Batch", "sch_wp_pistol_d18", {min_id, gas_id}, {}, 70);
        run(0.3);
        CHECK(client.crafting().presets.size() == 1);
        CHECK(client.crafting().presets[0].name == "D-18 Batch");

        // A pistol isn't made at an Armor & Clothing station...
        const size_t inv_before = client.items().size();
        client.craft_item("sch_wp_pistol_d18", {min_id, gas_id}, {}, 70, 3);
        run(0.4);
        CHECK(client.items().size() == inv_before);
        // ...but at a Weapon & Droid one: 3x D-18 Blaster Pistols (2 for wo_pistols_militia, 1 to salvage).
        server.set_crafting(&crafting, {{"Weapon & Droid Crafting Station", {3, 0, 3}, 4.0, "weapon"}});
        client.craft_item("sch_wp_pistol_d18", {min_id, gas_id}, {}, 70, 3);
        run(0.4);
        CHECK(client.items().size() == inv_before + 3);
        CHECK(profession_xp(client.professions().professions, "weaponsmith") >= 225);

        // Fulfill Hub Work Order (needs 2x wp_pistol_d18).
        const int64_t credits_before = client.player_status().credits;
        client.fulfill_work_order("wo_pistols_militia");
        run(0.4);
        CHECK(client.items().size() == inv_before + 1);
        CHECK(client.player_status().credits > credits_before);

        // Salvage the remaining crafted D-18 pistol.
        uint64_t spare_id = 0;
        for (const ItemInstance& inst : client.items()) {
            if (inst.def_id == "wp_pistol_d18") {
                spare_id = inst.id;
                break;
            }
        }
        CHECK(spare_id != 0);
        client.salvage_item(spare_id);
        run(0.4);
        CHECK(client.items().size() == inv_before);
        CHECK(profession_xp(client.professions().professions, "salvaging") > 0);

        // Delete the preset.
        client.delete_recipe_preset("D-18 Batch");
        run(0.3);
        CHECK(client.crafting().presets.empty());
    }
    std::filesystem::remove(db_path);
}

} // namespace

// Extracting takes a few seconds standing still, and the next one wants new ground.
void test_extraction_rules() {
    const auto root = repo_root();
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    ProfessionBook professions;
    professions.load(root / "game" / "professions" / "professions.ini");
    CraftingBook crafting;
    crafting.load_resources(root / "game" / "crafting" / "resources.ini");
    crafting.load_schematics(root / "game" / "crafting" / "schematics.ini");
    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1}, client_ep{2, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink cli_link{links, client_ep};
    ServerConfig config;
    config.catalog = &catalog;
    config.professions = &professions;
    config.crafting = &crafting;
    config.pvp_enabled = false;
    config.spawn = {0, 0, 0};
    GameServer server(srv_link, world, config);
    server.set_crafting(&crafting, {});
    GameClient client(cli_link, world, ClientConfig{.server = server_ep, .name = "Digger"});
    double now = 0.0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 60.0) {
            server.update(now);
            client.update(now);
        }
    };
    const auto total = [&] {
        int64_t sum = 0;
        for (const MaterialStack& st : client.crafting().materials) sum += st.amount;
        return sum;
    };
    run(0.5);
    CHECK(client.status() == GameClient::Status::connected);
    CHECK(client.player_status().extract_progress < 0);

    // Under way, it takes its time; then it pays.
    client.gather_extract(ResourceCategory::mineral);
    run(2.0);
    CHECK(total() == 0);
    CHECK(client.player_status().extract_progress > 0.1f && client.player_status().extract_progress < 0.9f);
    run(5.0);
    CHECK(total() > 0);
    CHECK(client.player_status().extract_progress < 0);

    // The same ground gives no more.
    const int64_t first = total();
    client.gather_extract(ResourceCategory::mineral);
    run(8.0);
    CHECK(total() == first);

    // Moving breaks one off.
    client.set_input({.forward = 1.0});
    run(7.0);
    client.set_input({});
    run(0.5);
    client.gather_extract(ResourceCategory::mineral);
    run(1.0);
    CHECK(client.player_status().extract_progress > 0.0f);
    client.set_input({.forward = 1.0});
    run(1.0);
    client.set_input({});
    run(8.0);
    CHECK(total() == first);

    BuildingCollision pad;
    pad.floors.push_back({{-2, 0, -2}, {2, 0, -2}, {2, 0, 2}});
    world.add_building(pad, {.position = {0, 0, 0}});
    CHECK(world.near_structure({10, 0, 10}, 40.0));
    CHECK(!world.near_structure({100, 0, 100}, 40.0));
}

int main() {
    test_extraction_rules();
    test_crafting_book();
    test_sqlite_v6_materials_and_presets();
    test_server_client_crafting_loopback();
    return swm::test::finish("crafting_test");
}
