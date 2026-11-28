// Stage 7 gathering, Material Vault, SQLite v6, and Unified Workbench crafting tests.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
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
    CHECK(book.schematics().size() > 300);  // the hand-written ones, and the families' (test_families)
    // (Plain ammo is never spent, so only effect ammo is crafted.)
    CHECK(book.find_schematic("sch_ammo_tibanna_standard") == nullptr);
    const SchematicDef* burn_ammo_sch = book.find_schematic("sch_ammo_tibanna_burning");
    CHECK(burn_ammo_sch != nullptr && burn_ammo_sch->output_count == 100);

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
    CHECK(preview.material_score > 0.0 && preview.material_score <= 1.0);
    CHECK(std::abs(preview.material_mult - 6.85) < 1e-9);  // 1 + 9 x 65%
    {
        const ExperimentOdds& o = preview.odds;
        CHECK(std::abs(o.flawed + o.standard + o.fine + o.exceptional - 1.0) < 1e-9);
    }
    CHECK(CraftingBook::roll_craft_grade(preview, 0.0) == ItemGrade::exceptional);
    CHECK(CraftingBook::roll_craft_grade(preview, 0.999999) == ItemGrade::flawed);

    // Grade odds: a fresh crafter on average materials makes mostly Standard;
    // better materials, level, modules and focus each move them up; a master
    // with everything makes mostly Exceptional.
    {
        const ExperimentOdds fresh = CraftingBook::grade_odds(0.5, 1, 0, 0);
        CHECK(fresh.standard > fresh.flawed && fresh.standard > fresh.fine && fresh.exceptional < 0.05);
        const auto better = [&](const ExperimentOdds& o) { return o.exceptional > fresh.exceptional && o.flawed < fresh.flawed; };
        CHECK(better(CraftingBook::grade_odds(0.9, 1, 0, 0)));
        CHECK(better(CraftingBook::grade_odds(0.5, 60, 0, 0)));
        CHECK(better(CraftingBook::grade_odds(0.5, 1, 2, 0)));
        CHECK(better(CraftingBook::grade_odds(0.5, 1, 0, 100)));
        // A crafting perk's Experimentation moves them up; Masterwork doubles Exceptional.
        CHECK(better(CraftingBook::grade_odds(0.5, 1, 0, 0, 0.15)));
        const ExperimentOdds plain = CraftingBook::grade_odds(0.7, 50, 1, 50);
        const ExperimentOdds masterwork = CraftingBook::grade_odds(0.7, 50, 1, 50, 0.0, 2.0);
        CHECK(std::abs(masterwork.exceptional - 2.0 * plain.exceptional) < 1e-9);
        CHECK(std::abs(masterwork.flawed + masterwork.standard + masterwork.fine + masterwork.exceptional - 1.0) < 1e-9);
        const ExperimentOdds master = CraftingBook::grade_odds(0.9, 100, 2, 100);
        CHECK(master.exceptional > 0.6 && master.flawed < 0.01);
    }
    // Focus costs materials: up to 10x, on top of the mark's.
    CHECK(craft_material_amount(4, 0, 0) == 4 && craft_material_amount(4, 0, 100) == 40);
    CHECK(craft_material_amount(4, 2, 50) == 33);  // 6 at Mk II, x5.5
    CHECK(std::abs(focus_material_mult(150) - 10.0) < 1e-9);

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

        // Nothing is free (docs/PERKS.md): without Surveying learned, no extracting.
        client.gather_extract(ResourceCategory::mineral);
        run(0.5);
        CHECK(client.crafting().materials.empty());
        CHECK(client.player_status().notice.find("Learn Surveying") != std::string::npos);
        // The licences, from trainers: a level-1 perk each, free.
        server.set_vendors({{"Survey Trainer", "trainer", {0, 0, 0}, 4, "surveying,salvaging"},
                            {"Weaponsmith Trainer", "trainer", {0, 0, 0}, 4, "weaponsmith"}});
        for (const char* id : {"survey_quick_extraction", "salvage_tinkerers_eye", "wsmith_weapon_experimentation"}) {
            client.service(net::ServiceRequest::learn_perk, id, 0, 0);
            run(0.3);
        }

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
        client.craft_item("sch_wp_pistol_d18", {min_id, gas_id}, {}, 0, 3);
        run(0.4);
        CHECK(client.items().size() == inv_before);
        // ...but at a Weapon & Droid one: 3x D-18 Blaster Pistols (one to salvage).
        // Full focus would take 10x the materials, more than the vault holds.
        server.set_crafting(&crafting, {{"Weapon & Droid Crafting Station", {3, 0, 3}, 4.0, "weapon"}});
        client.craft_item("sch_wp_pistol_d18", {min_id, gas_id}, {}, 100, 3);
        run(0.4);
        CHECK(client.items().size() == inv_before);
        client.craft_item("sch_wp_pistol_d18", {min_id, gas_id}, {}, 0, 3);
        run(0.4);
        CHECK(client.items().size() == inv_before + 3);
        CHECK(profession_xp(client.professions().professions, "weaponsmith") >= 225);

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
        CHECK(client.items().size() == inv_before + 2);
        CHECK(profession_xp(client.professions().professions, "salvaging") > 0);

        // Delete the preset.
        client.delete_recipe_preset("D-18 Batch");
        run(0.3);
        CHECK(client.crafting().presets.empty());
    }
    std::filesystem::remove(db_path);
}

// docs/CRAFTING.md, Schematics: the families make everything a crafter makes,
// and nothing a quartermaster keeps.
void test_families() {
    const auto root = repo_root();
    CraftingBook book;
    book.load_resources(root / "game" / "crafting" / "resources.ini");
    book.load_schematics(root / "game" / "crafting" / "schematics.ini");
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    const auto made = [&](std::string_view item) {
        return std::any_of(book.schematics().begin(), book.schematics().end(),
                           [&](const SchematicDef& s) { return s.output_item == item; });
    };
    // Every neutral weapon but the trap (the scout's, later) and the vendors'
    // and loot's medpacks ("supply_", a Doctor makes their own); every
    // Tailor's piece; every vehicle and Deflector Shield.
    for (const ItemDef& def : catalog.all()) {
        if (def.kind == ItemKind::weapon && def.faction == "neutral" && def.id != "wp_trap_laser" &&
            !def.id.starts_with("supply_")) {
            CHECK(made(def.id));
        }
        if (def.kind == ItemKind::clothing && def.faction == "neutral") CHECK(made(def.id));
        if (def.kind == ItemKind::vehicle || (def.kind == ItemKind::deflector_shield && def.faction == "neutral")) {
            CHECK(made(def.id));
        }
        if (def.kind == ItemKind::bodysuit) CHECK(!made(def.id));  // (the quartermasters')
    }
    CHECK(!made("wp_trap_laser"));
    // The crafted armor sets, not the faction issue or the Katarn.
    CHECK(made("armor_padded_gloves") && made("armor_ubese_helmet") && made("armor_mandalorian_boots"));
    CHECK(!made("armor_katarn_chest") && !made("armor_marine_chest") && !made("armor_stormtrooper_chest"));

    // A family's schematic: its group, level and recipe; a piece costs its
    // slot's share of a chest's.
    const SchematicDef* chest = book.find_schematic("sch_armor_padded_chest");
    const SchematicDef* gloves = book.find_schematic("sch_armor_padded_gloves");
    CHECK(chest && gloves && chest->group == "Medium Armor" && chest->profession == "armorsmith");
    CHECK(chest->required_level == 1 && chest->enhancement_slots == 2 && chest->marks);
    CHECK(chest->slots.size() == 3 && chest->slots[0].amount == 7 && gloves->slots[0].amount == 2);
    const SchematicDef* shirt = book.find_schematic("sch_cloth_shirt_s04");
    CHECK(shirt && shirt->group == "Shirts" && shirt->profession == "tailor" && !shirt->marks);
    const SchematicDef* speeder = book.find_schematic("sch_vehicle_speeder_bike");
    CHECK(speeder && speeder->profession == "droid_engineer" && !speeder->marks);
    CHECK(book.find_schematic("sch_mount_bantha")->profession == "bio_engineer");
    // A hand-written one keeps its own id and recipe, with a group.
    const SchematicDef* d18 = book.find_schematic("sch_wp_pistol_d18");
    CHECK(d18 && d18->group == "Pistols" && d18->marks && book.find_schematic("sch_wp_pistol_d18") == d18);

    // Mk tiers: up to the crafter's level's, more materials the higher.
    CHECK(craft_max_mark(1) == 0 && craft_max_mark(19) == 1 && craft_max_mark(100) == max_mark);
    CHECK(mark_material_amount(4, 0) == 4 && mark_material_amount(4, 2) == 6 && mark_material_amount(5, 10) == 18);
    CHECK(mark_craft_xp(100, 10) == 250);
    std::vector<MaterialStack> some;
    for (const ResourceDef& r : book.resources()) add_material(some, r.id, 5);
    CHECK(!book.auto_fill(*d18, some, AutoFillMode::best_stat, 0)[0].empty());
    CHECK(book.auto_fill(*d18, some, AutoFillMode::best_stat, 2)[0].empty());  // 6 mineral a pistol at Mk II

    // Dyes: a model's customization values.
    CHECK(dye_values_ok("") && dye_values_ok("index_color_1=12") && dye_values_ok("index_color_1=0&/shared_owner/x=255"));
    CHECK(!dye_values_ok("index_color_1") && !dye_values_ok("a=256") && !dye_values_ok("A=1") && !dye_values_ok("a=1&"));
    CHECK(!dye_values_ok("a=1&b=1&c=1&d=1&e=1&f=1&g=1") && !dye_values_ok("a=\"1"));
}

// docs/CRAFTING.md, Levels and Blueprints: weapons and armor aren't gated by
// level (medicine is, medpacks too); a schematic or some of a family can wait
// behind a blueprint.
void test_blueprints() {
    const auto root = repo_root();
    CraftingBook real;
    real.load_resources(root / "game" / "crafting" / "resources.ini");
    real.load_schematics(root / "game" / "crafting" / "schematics.ini");
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    // Every weaponsmith's and armorsmith's weapon, armor and shield is a
    // blueprint but the basics: one in each crafting group, the same that
    // gear vendors sell. Each blueprint is taught by exactly one city
    // quartermaster's blueprint item (docs/CRAFTING.md).
    std::map<std::string, int> taught_by;
    for (const ItemDef& def : catalog.all()) {
        if (def.town.empty()) continue;
        CHECK(def.kind == ItemKind::supply && def.cost_credits > 0 && !gear_vendor_sells(def));
        for (const std::string& id : blueprint_teaches(def)) ++taught_by[id];
    }
    std::map<std::string, int> basics_in;  // crafting group -> its basics (an armor set's pieces count once)
    std::set<std::string> basic_sets;
    for (const SchematicDef& s : real.schematics()) {
        const ItemDef* out = catalog.find(s.output_item);
        const bool medicine = out != nullptr && out->archetype == WeaponArchetype::medpack;
        if (out != nullptr && !medicine && (out->kind == ItemKind::weapon || out->kind == ItemKind::armor ||
                                            out->kind == ItemKind::deflector_shield)) {
            CHECK(s.required_level == 1);
        }
        const bool smith = s.profession == "weaponsmith" || s.profession == "armorsmith";
        if (out == nullptr || !smith || !is_smith_gear(*out)) {
            CHECK(!s.blueprint);
            continue;
        }
        CHECK(s.blueprint == !out->basic);
        CHECK(gear_vendor_sells(*out) == (out->basic && out->cost_credits > 0));
        if (s.blueprint) {
            if (taught_by[s.id] != 1) std::printf("%s is taught by %d blueprints\n", s.id.c_str(), taught_by[s.id]);
            CHECK(taught_by[s.id] == 1);
        } else if (out->kind == ItemKind::armor) {
            const std::string set = out->id.substr(0, out->id.rfind('_'));
            if (basic_sets.insert(set).second) ++basics_in[s.group];
        } else {
            ++basics_in[s.group];
        }
    }
    for (const char* group : {"Pistols", "Carbines", "Rifles", "Scatterguns", "Sniper Rifles", "Melee Weapons",
                              "Two-Handed Melee", "Beams & Flamers", "Heavy Weapons", "Grenades & Mines", "Light Armor",
                              "Medium Armor", "Heavy Armor", "Deflector Shields"}) {
        if (basics_in[group] != 1) std::printf("%s has %d basics\n", group, basics_in[group]);
        CHECK(basics_in[group] == 1);
    }
    // What no crafter makes stays a gear vendor's (the Katarn: docs/ARMOR.md, Paint).
    const ItemDef* katarn = catalog.find("armor_katarn_chest");
    CHECK(katarn != nullptr && !katarn->blueprint_taught && gear_vendor_sells(*katarn));
    // A whole set from one blueprint.
    const ItemDef* ubese = catalog.find("bp_armor_ubese");
    CHECK(ubese != nullptr && ubese->town == "dantooine_agro_outpost" && blueprint_teaches(*ubese).size() >= 6 &&
          ubese->cost_credits == city_blueprint_cost(ubese->required_rank, true));

    CraftingBook book;
    book.load_resources(root / "game" / "crafting" / "resources.ini");
    book.parse_schematics(Ini::parse("[schematic]\nId = sch_secret\nName = Secret\nOutput = wp_pistol_dh17\n"
                                     "Blueprint = yes\nSlot = mineral 2\n"
                                     "[schematic]\nId = sch_open\nName = Open\nOutput = wp_pistol_d18\nSlot = mineral 2\n"
                                     "[family]\nGroup = Carbines\nKind = weapon\nArchetype = carbine_burst\n"
                                     "BlueprintMatch = wp_rifle_e11*\nSlot = mineral 4\n"),
                          "test", &catalog);
    CHECK(book.find_schematic("sch_secret") && book.find_schematic("sch_secret")->blueprint);
    CHECK(book.find_schematic("sch_open") && !book.find_schematic("sch_open")->blueprint);
    size_t hidden = 0, open = 0;
    for (const SchematicDef& s : book.schematics()) {
        if (s.group != "Carbines") continue;  // (the E-11 is a carbine)
        CHECK(s.blueprint == s.output_item.starts_with("wp_rifle_e11"));
        (s.blueprint ? hidden : open) += 1;
    }
    CHECK(hidden >= 1 && open >= 1);

    // A blueprint item: a supply that Teaches.
    std::filesystem::path cat_file = std::filesystem::temp_directory_path() / "swm_blueprint_catalog.ini";
    {
        std::FILE* f = std::fopen(cat_file.string().c_str(), "wb");
        std::fputs("[item]\nId = bp_secret\nName = Blueprint: Secret\nKind = supply\nTeaches = sch_secret\n", f);
        std::fclose(f);
    }
    ItemCatalog small;
    small.load(cat_file);
    CHECK(small.find("bp_secret") && small.find("bp_secret")->teaches == "sch_secret");
    std::filesystem::remove(cat_file);
}

// docs/CRAFTING.md, Modules: each tunes what it's crafted into within its
// budget, something better for something worse.
void test_modules() {
    const auto root = repo_root();
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    const ItemDef& dh17 = *catalog.find("wp_pistol_dh17");
    const ItemDef& barrel = *catalog.find("mod_precision_barrel");
    const ItemDef& plating = *catalog.find("mod_armor_plating");
    CHECK(module_fits(barrel, dh17) && !module_fits(plating, dh17));
    CHECK(!module_fits(*catalog.find("mod_weighted_blade"), dh17));
    CHECK(module_fits(*catalog.find("mod_weighted_blade"), *catalog.find("wp_melee_vibroblade")));
    CHECK(module_fits(plating, *catalog.find("armor_padded_chest")));
    CHECK(module_fits(*catalog.find("mod_capacitor_bank"), *catalog.find("psg_mark1")));

    // An owned copy's fields: kept apart, a Smuggler's slice among them.
    std::string json = with_item_json_field("{\"slice\":\"overcharge\"}", "mods", "mod_precision_barrel");
    json = with_item_json_field(json, "dye", "index_color_1=3");
    CHECK(item_json_field(json, "slice") == "overcharge" && item_json_field(json, "dye") == "index_color_1=3");
    CHECK(with_item_json_field(json, "dye", "") == R"({"slice":"overcharge","mods":"mod_precision_barrel"})");
    CHECK(with_item_json_field("{}", "mods", "") == "{}");

    // A weapon takes its modules' multipliers; one that doesn't fit does nothing.
    ItemInstance inst{1, 1, "wp_pistol_dh17"};
    inst.color_json = with_item_json_field("{}", "mods", "mod_precision_barrel,mod_armor_plating");
    CHECK(item_modules(inst).size() == 2);
    const ItemDef tuned = catalog.tuned(dh17, inst);
    CHECK(std::abs(tuned.spread_mult - dh17.spread_mult * 0.85) < 1e-9);
    CHECK(std::abs(tuned.rate_mult - dh17.rate_mult * 0.94) < 1e-9 && tuned.damage_mult == dh17.damage_mult);

    // Within the sidegrade budget: no module (or pair) scores a weapon higher.
    std::vector<std::string> weapon_mods;
    for (const ItemDef& d : catalog.all()) {
        if (d.kind == ItemKind::enhancement && (d.module_for == "ranged" || d.module_for == "melee")) {
            weapon_mods.push_back(d.id);
        }
    }
    CHECK(weapon_mods.size() >= 4);
    for (const ItemDef& w : catalog.all()) {
        if (w.kind != ItemKind::weapon) continue;
        for (const std::string& a : weapon_mods) {
            for (const std::string& b : weapon_mods) {
                ItemInstance two{1, 1, w.id};
                two.color_json = with_item_json_field("{}", "mods", a + "," + b);
                CHECK(sidegrade_score(catalog.tuned(w, two)) <= sidegrade_score(w) * (1 + 1e-9));
            }
        }
    }

    // Armor: a quarter of the piece's typed resistance moves to the module's
    // spread; the class budget stays whole.
    const ItemDef& padded = *catalog.find("armor_padded_chest");  // ballistic and explosive, half each
    const auto typed = [](const ItemDef& d, Stat stat) {
        double sum = 0;
        for (const StatModifier& m : d.modifiers) {
            if (m.stat == stat) sum += m.add;
        }
        return sum;
    };
    const auto typed_total = [&](const ItemDef& d) {
        double sum = 0;
        for (size_t t = size_t(Stat::mitigation_ballistic); t <= size_t(Stat::mitigation_melee); ++t) sum += typed(d, Stat(t));
        return sum;
    };
    ItemInstance plated{2, 1, "armor_padded_chest"};
    plated.color_json = with_item_json_field("{}", "mods", "mod_armor_plating");
    const ItemDef padded_plated = catalog.tuned(padded, plated);
    CHECK(std::abs(typed_total(padded_plated) - typed_total(padded)) < 1e-9);
    CHECK(typed(padded_plated, Stat::mitigation_melee) > 0 && typed(padded, Stat::mitigation_melee) == 0);
    CHECK(std::abs(typed(padded_plated, Stat::mitigation_explosive) - typed(padded, Stat::mitigation_explosive) * 0.75) < 1e-9);
    CHECK(std::abs(typed(padded_plated, Stat::mitigation) - typed(padded, Stat::mitigation)) < 1e-9);
    CHECK(std::abs(typed(padded_plated, Stat::max_health) - typed(padded, Stat::max_health)) < 1e-9);

    // A shield: its module's Modifier lines.
    ItemInstance banked{3, 1, "psg_mark1"};
    banked.color_json = with_item_json_field("{}", "mods", "mod_capacitor_bank");
    const ItemDef& psg = *catalog.find("psg_mark1");
    CHECK(catalog.tuned(psg, banked).modifiers.size() == psg.modifiers.size() + 2);

    // The loadout carries it: the tuned pistol fires tighter.
    CharacterLoadout lo;
    lo.sidearm_id = 1;
    ItemInstance plain{1, 1, "wp_pistol_dh17"};
    const Loadout before = catalog.build_loadout(lo, std::vector<ItemInstance>{plain}, Species::human);
    const Loadout after = catalog.build_loadout(lo, std::vector<ItemInstance>{inst}, Species::human);
    CHECK(after.weapons[sidearm_slot] && before.weapons[sidearm_slot]);
    CHECK(after.weapons[sidearm_slot]->yaw_spread < before.weapons[sidearm_slot]->yaw_spread);

    // A dress (a Galaxies robe in the dress slots) covers the legs: what's worn there isn't drawn.
    CharacterLoadout dressed;
    dressed.gear_ids = {{"chest", 10}, {"leggings", 11}};
    const std::vector<ItemInstance> wardrobe{{10, 1, "cloth_dress_s11"}, {11, 1, "cloth_pants_s04"}};
    const std::string drawn = resolve_worn_models(catalog, dressed, wardrobe, Species::human, Gender::male);
    CHECK(drawn.find("dress_s11_m") != std::string::npos && drawn.find("pants_s04") == std::string::npos);
    // A dyed piece carries its dye after its paint.
    std::vector<ItemInstance> dyed{{10, 1, "cloth_shirt_s04"}};
    dyed[0].color_json = R"({"dye":"index_color_1=7"})";
    CharacterLoadout shirt;
    shirt.gear_ids = {{"chest", 10}};
    CHECK(resolve_worn_models(catalog, shirt, dyed, Species::human, Gender::female) ==
          "clothing/shirt_s04_f.gltf?index_color_1=7");
}

// At the workbench: a mark up to the crafter's level's, modules crafted in,
// and a dye.
void test_marks_modules_and_dyes() {
    const auto root = repo_root();
    const auto db_path = std::filesystem::temp_directory_path() / "swm_crafting_marks_test.sqlite";
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
        // A level-20 weaponsmith (licensed: Thrifty Gunsmith, a level-1 perk,
        // 20% fewer materials), with plenty in the vault, a module, the wrong
        // module and a shirt.
        const CharacterRecord smith = store.create("Smith", {0, 0, 0}, 0.0f);
        const std::vector<ProfessionProgress> progress{{"weaponsmith", professions.xp_for_level(20)}};
        store.save_professions(smith.id, progress);
        store.set_extra(smith.id, "perks:weaponsmith", "wsmith_thrifty_gunsmith");
        store.set_extra(smith.id, "perks:tailor", "tailor_fine_stitching");
        std::vector<MaterialStack> vault;
        for (const ResourceDef& r : crafting.resources()) vault.push_back({r.id, 200});
        store.save_materials(smith.id, vault);
        const ItemInstance barrel = store.grant_item({0, smith.id, "mod_precision_barrel"});
        const ItemInstance plating = store.grant_item({0, smith.id, "mod_armor_plating"});
        const ItemInstance shirt = store.grant_item({0, smith.id, "cloth_shirt_s04"});
        // The DH-17 is a blueprint (Coronet's quartermaster sells it).
        const ItemInstance dh17_blueprint = store.grant_item({0, smith.id, "bp_wp_pistol_dh17"});
        store.save_reputation(smith.id, {{town_reputation_key("coronet"), 3000}});  // Friendly with Coronet

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
        GameServer server(srv_link, world, config, &store);
        server.set_looks({"object/mobile/shared_dressed_commoner_tatooine_rodian_male_01.iff"});
        server.set_crafting(&crafting, {{"Crafting Station", {0, 0, 0}, 6.0, "general"}});
        GameClient client(cli_link, world, ClientConfig{.server = server_ep, .name = "Smith"});
        double now = 0.0;
        const auto run = [&](double seconds) {
            for (const double end = now + seconds; now < end; now += 1.0 / 60.0) {
                server.update(now);
                client.update(now);
            }
        };
        const auto owned = [&](std::string_view def_id) {
            std::vector<ItemInstance> out;
            for (const ItemInstance& inst : client.items()) {
                if (inst.def_id == def_id) out.push_back(inst);
            }
            return out;
        };
        run(0.5);
        CHECK(client.status() == GameClient::Status::connected);
        CHECK(client.items().size() == 4);

        const SchematicDef* sch = crafting.find_schematic("sch_wp_pistol_dh17");
        CHECK(sch != nullptr && sch->marks && sch->blueprint);
        const std::vector<std::string> rids = crafting.auto_fill(*sch, vault, AutoFillMode::best_stat, 2);
        // Refused until its blueprint is learned; using the blueprint teaches
        // it and spends it.
        client.craft_item(sch->id, rids, {}, 0, 1, 2);
        run(0.4);
        CHECK(owned("wp_pistol_dh17").empty());
        client.use_item(dh17_blueprint.id);
        run(0.4);
        CHECK(owned("bp_wp_pistol_dh17").empty());
        CHECK(std::find(client.crafting().blueprints.begin(), client.crafting().blueprints.end(),
                        "sch_wp_pistol_dh17") != client.crafting().blueprints.end());

        // Coronet's quartermaster sells Coronet's blueprints for credits, each
        // at its standing; nobody else's.
        server.set_quartermasters({{"Coronet Quartermaster", "neutral", {0, 0, 0}, 6, "coronet", "Coronet"}});
        server.award_credits(client.entity_id(), 10000);
        run(0.3);
        const int64_t cr = client.player_status().credits;
        const ItemDef& corsec = *catalog.find("bp_wp_pistol_scout_corsec");  // Friendly
        const ItemDef& tantel = *catalog.find("bp_armor_tantel");           // Honored
        CHECK(corsec.town == "coronet" && corsec.required_rank == RepTier::friendly);
        CHECK(tantel.town == "coronet" && tantel.required_rank == RepTier::honored);
        client.buy_item(tantel.id, false);
        client.buy_item("bp_wp_pistol_dl44_metal", false);  // Mos Eisley's
        run(0.4);
        CHECK(owned(tantel.id).empty() && owned("bp_wp_pistol_dl44_metal").empty());
        CHECK(client.player_status().credits == cr);
        client.buy_item(corsec.id, false);
        run(0.4);
        CHECK(owned(corsec.id).size() == 1 && client.player_status().credits == cr - corsec.cost_credits);
        server.set_quartermasters({});
        const int64_t mineral_before = material_amount(client.crafting().materials, rids[0]);

        // Mk III takes level 30.
        client.craft_item(sch->id, rids, {}, 0, 1, 3);
        run(0.4);
        CHECK(owned("wp_pistol_dh17").empty());
        CHECK(client.player_status().notice.find("level 30") != std::string::npos);
        // Armor plating doesn't fit a pistol.
        client.craft_item(sch->id, rids, {plating.id}, 0, 1, 2);
        run(0.4);
        CHECK(owned("wp_pistol_dh17").empty() && owned("mod_armor_plating").size() == 1);
        // Mk II with the barrel: tuned, the barrel used, half again the
        // materials (less Thrifty's 20%).
        client.craft_item(sch->id, rids, {barrel.id}, 0, 1, 2);
        run(0.4);
        const auto pistols = owned("wp_pistol_dh17");
        CHECK(pistols.size() == 1 && pistols[0].mark == 2);
        CHECK(item_modules(pistols[0]) == std::vector<std::string>{"mod_precision_barrel"});
        CHECK(owned("mod_precision_barrel").empty());
        const int64_t thrifty = std::max<int64_t>(
            1, int64_t(std::ceil(double(mark_material_amount(sch->slots[0].amount, 2)) * 0.8 - 1e-6)));
        CHECK(thrifty < mark_material_amount(sch->slots[0].amount, 2));
        CHECK(material_amount(client.crafting().materials, rids[0]) == mineral_before - thrifty);
        // By default, the highest it may make: Mk II at level 20.
        client.craft_item(sch->id, rids, {}, 0, 1);
        run(0.4);
        CHECK(owned("wp_pistol_dh17").size() == 2 && owned("wp_pistol_dh17").back().mark == 2);

        // The Tailor dyes the shirt (a chemical's two units); a bad dye is refused.
        int64_t chemical = 0;
        for (const MaterialStack& st : client.crafting().materials) {
            if (crafting.find_resource(st.resource_id)->category == ResourceCategory::chemical) chemical += st.amount;
        }
        client.dye_item(shirt.id, "index_color_1=12&index_color_2=3");
        run(0.4);
        CHECK(item_json_field(owned("cloth_shirt_s04")[0].color_json, "dye") == "index_color_1=12&index_color_2=3");
        int64_t chemical_after = 0;
        for (const MaterialStack& st : client.crafting().materials) {
            if (crafting.find_resource(st.resource_id)->category == ResourceCategory::chemical) chemical_after += st.amount;
        }
        CHECK(chemical_after == chemical - dye_cost);
        client.dye_item(shirt.id, "index_color_1=999");
        run(0.4);
        CHECK(item_json_field(owned("cloth_shirt_s04")[0].color_json, "dye") == "index_color_1=12&index_color_2=3");
        client.dye_item(shirt.id, "");
        run(0.4);
        CHECK(owned("cloth_shirt_s04")[0].color_json == "{}");
        // Not a weapon.
        client.dye_item(pistols[0].id, "index_color_1=1");
        run(0.4);
        CHECK(item_json_field(owned("wp_pistol_dh17")[0].color_json, "dye").empty());
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
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // (a crash keeps what's printed)
    test_extraction_rules();
    test_crafting_book();
    test_sqlite_v6_materials_and_presets();
    test_server_client_crafting_loopback();
    test_families();
    test_blueprints();
    test_modules();
    test_marks_modules_and_dyes();
    return swm::test::finish("crafting_test");
}
