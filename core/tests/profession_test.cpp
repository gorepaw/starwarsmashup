// Stage 7 professions: the shipped roster and its level curve, slotting,
// perks applied to a loadout, armor classes, and experience earned through a
// server (weapon kills, hits taken, quests) and saved with the character.

#include <algorithm>
#include <filesystem>
#include <vector>

#include "swm/core/client.h"
#include "swm/core/heightfield.h"
#include "swm/core/items.h"
#include "swm/core/net/link.h"
#include "swm/core/profession.h"
#include "swm/core/server.h"
#include "swm/core/sqlite_store.h"
#include "swm/core/world.h"
#include "test.h"

using namespace swm::core;
using swm::test::near;

namespace {

std::filesystem::path repo_root() {
    auto p = std::filesystem::current_path();
    while (!p.empty() && !std::filesystem::exists(p / "game" / "professions" / "professions.ini")) {
        auto parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return p;
}

double stat_after(const Loadout& lo, Stat stat) {
    double add = 0, mult = 1;
    for (const Gear& g : lo.gear) {
        for (const StatModifier& m : g.modifiers) {
            if (m.stat == stat) {
                add += m.add;
                mult *= m.multiply;
            }
        }
    }
    return stat == Stat::run_speed ? mult : add;
}

void test_roster() {
    ProfessionBook book;
    book.load(repo_root() / "game" / "professions" / "professions.ini");
    CHECK(book.all().size() == 31);
    const auto in_wave = [&](const char* wave) {
        return std::count_if(book.all().begin(), book.all().end(), [&](const ProfessionDef& d) { return d.wave == wave; });
    };
    CHECK(in_wave("7A") == 17);
    CHECK(in_wave("7B") == 10);
    CHECK(in_wave("7C") == 4);
    for (const ProfessionDef& def : book.all()) {
        // Major unlocks at 1, 25, 50, 75 and 100.
        std::vector<int> levels;
        for (const ProfessionMilestone& m : def.milestones) levels.push_back(m.level);
        CHECK((levels == std::vector<int>{1, 25, 50, 75, 100}));
        CHECK(!def.name.empty() && !def.description.empty());
    }
    // All 4 armors and 5 weapon lines (including Melee) are in play.
    for (const char* id : {"unarmored", "light_armor", "medium_armor", "heavy_armor", "pistols", "carbines", "rifles",
                           "heavy_weapons", "melee"}) {
        CHECK(can_specialize(book, id));
    }
    CHECK(!can_specialize(book, "bounty_hunter"));  // Wave 7B specialist not yet in play
    CHECK(!can_specialize(book, "weaponsmith"));    // nor crafting
    CHECK(!can_specialize(book, "nobody"));
    for (const auto& a : {"pistol", "carbine_burst", "scatter", "rifle_auto", "sniper", "heavy_launcher", "grenade",
                          "melee"}) {
        CHECK(book.for_archetype(a) != nullptr);
    }
    CHECK(book.for_archetype("pistol") && book.for_archetype("pistol")->id == "pistols");
    CHECK(book.for_armor("none") && book.for_armor("none")->id == "unarmored");
    CHECK(book.for_armor("heavy") && book.for_armor("heavy")->id == "heavy_armor");

    // The curve: 100 to level 2, 15 more each level after; 82,665 to 100.
    CHECK(book.xp_for_level(1) == 0);
    CHECK(book.xp_for_level(2) == 100);
    CHECK(book.xp_for_level(3) == 215);
    CHECK(book.xp_to_next(2) == 115);
    CHECK(book.xp_for_level(100) == 82665);
    CHECK(book.level_for(0) == 1);
    CHECK(book.level_for(99) == 1);
    CHECK(book.level_for(100) == 2);
    CHECK(book.level_for(82664) == 99);
    CHECK(book.level_for(82665) == 100);
    CHECK(book.level_for(10'000'000) == 100);
    CHECK(book.xp_to_next(100) == 0);

    // Slots: known, slottable, once each, three at most.
    const std::vector<std::string> asked{"rifles", "bounty_hunter", "rifles", "bogus", "light_armor", "pistols", "carbines"};
    CHECK((clean_specializations(book, asked) == std::vector<std::string>{"rifles", "light_armor", "pistols"}));
}

void test_perks() {
    ProfessionBook book;
    book.load(repo_root() / "game" / "professions" / "professions.ini");
    Loadout lo;
    WeaponDef pistol;
    pistol.archetype = "pistol";
    pistol.yaw_spread = 1.0;
    pistol.reload_time = 2.0;
    WeaponDef rifle;
    rifle.archetype = "rifle_auto";
    rifle.yaw_spread = 1.0;
    lo.weapons = {rifle, pistol, std::nullopt};
    lo.armor_class = "light";
    const std::vector<ProfessionProgress> progress{{"pistols", book.xp_for_level(25)}, {"light_armor", 0},
                                                   {"heavy_armor", book.xp_for_level(100)}};
    // Pistols at 25 (two ranks, Sidearm Certification, Steady Hand), light
    // armor at 1, heavy armor's not worn: nothing from it.
    const std::vector<std::string> slots{"pistols", "light_armor", "heavy_armor"};
    apply_specializations(lo, book, slots, progress);
    CHECK(near(lo.weapons[1]->yaw_spread, 0.99 * 0.99 * 0.95));
    CHECK(near(lo.weapons[1]->reload_time, 2.0 * 0.95));
    CHECK(near(lo.weapons[0]->yaw_spread, 1.0));  // the rifle isn't a pistol
    CHECK(lo.gear.size() == 1 && lo.gear[0].slot == "specialization");
    CHECK(near(stat_after(lo, Stat::mitigation_electro), 0.02));
    CHECK(near(stat_after(lo, Stat::mitigation_explosive), 0.0));
    // Nothing slotted, nothing applied.
    Loadout plain;
    apply_specializations(plain, book, {}, progress);
    CHECK(plain.gear.empty());

    // Routed experience: the slotted, else the weapon's and the armor's.
    CHECK((routed_professions(book, lo, slots) == slots));
    CHECK((routed_professions(book, lo, {}) == std::vector<std::string>{"rifles", "light_armor"}));

    // Armor classes: the most pieces, the heavier on a tie.
    CHECK(loadout_armor_class(std::vector<std::string>{}) == "none");
    CHECK(loadout_armor_class(std::vector<std::string>{"light", "medium"}) == "medium");
    CHECK(loadout_armor_class(std::vector<std::string>{"light", "light", "heavy"}) == "light");
    CHECK(loadout_armor_class(std::vector<std::string>{"medium", "heavy", "heavy", "light"}) == "heavy");

    // The catalog's armor says its class; a loadout of it is that class.
    ItemCatalog catalog;
    catalog.load(repo_root() / "game" / "items" / "swg_catalog.ini");
    for (const ItemDef& def : catalog.all()) {
        if (def.kind == ItemKind::armor) {
            CHECK(!def.armor_class.empty());
        }
    }
    CHECK(catalog.find("armor_ubese_shirt") && catalog.find("armor_ubese_shirt")->armor_class == "light");
    CHECK(catalog.find("armor_composite_chest") && catalog.find("armor_composite_chest")->armor_class == "heavy");
    std::vector<ItemInstance> inventory{{1, 7, "armor_ubese_shirt"}, {2, 7, "armor_ubese_boots"},
                                        {3, 7, "armor_padded_chest"}, {4, 7, "wp_pistol_dh17"}};
    CharacterLoadout cl;
    cl.sidearm_id = 4;
    cl.gear_ids = {{"chest", 1}, {"boots", 2}};
    const Loadout built = catalog.build_loadout(cl, inventory, Species::human);
    CHECK(built.armor_class == "light");
    CHECK(built.weapons[1] && built.weapons[1]->archetype == "pistol");
    CHECK(catalog.build_loadout(CharacterLoadout{}, inventory, Species::human).armor_class == "none");
}

void test_server_professions() {
    const auto root = repo_root();
    const auto db_path = std::filesystem::temp_directory_path() / "swm_profession_test.sqlite";
    std::filesystem::remove(db_path);
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    ProfessionBook book;
    book.load(root / "game" / "professions" / "professions.ini");
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
        config.professions = &book;
        config.pvp_enabled = false;
        config.spawn = {0, 0, 0};
        GameServer server(srv_link, world, config, &store);
        server.set_looks({"object/mobile/shared_dressed_commoner_tatooine_rodian_male_01.iff"});
        const uint32_t rat = server.add_npc("Womp Rat", {5, 0, 5}, 0, 0);
        GameClient client(cli_link, world, ClientConfig{.server = server_ep, .name = "Trainee"});
        double now = 0;
        const auto run = [&](double seconds) {
            for (const double end = now + seconds; now < end; now += 1.0 / 60.0) {
                server.update(now);
                client.update(now);
            }
        };
        run(0.5);
        CHECK(client.status() == GameClient::Status::connected);
        const uint32_t me = client.entity_id();

        // A creature killed with the starter E-11 in hand trains Rifles.
        server.award_xp(me, 150);
        run(0.3);
        const auto* mine = server.professions_of(me);
        CHECK(mine && profession_xp(*mine, "rifles") == 150);
        CHECK(client.professions().professions.size() == 1 &&
              client.professions().professions[0].id == "rifles" && client.professions().professions[0].xp == 150);
        CHECK(client.player_status().notice.find("Rifles reached level 2") != std::string::npos);

        // Hits taken in no armor train Unarmored: a point for each 10 damage.
        server.npc_melee(rat, me, 55, DamageType::melee);
        server.npc_melee(rat, me, 55, DamageType::melee);
        run(0.2);
        CHECK(mine && profession_xp(*mine, "unarmored") == 11);

        // Slots: only slottable professions are kept; the perks come with them.
        CharacterLoadout lo = client.char_loadouts().at(0);
        lo.specializations = {"rifles", "bounty_hunter", "bogus", "rifles"};
        lo.companion = "a rancor";
        client.save_loadout(lo);
        run(0.3);
        CHECK(client.char_loadouts().size() == 1 &&
              client.char_loadouts()[0].specializations == std::vector<std::string>{"rifles"});
        CHECK(client.char_loadouts().size() == 1 && client.char_loadouts()[0].companion.empty());
        const auto& resolved = client.custom_loadouts();
        CHECK(!resolved.empty() && resolved[0].weapons[0] && resolved[0].weapons[0]->archetype == "rifle_auto");
        const double slotted_spread = resolved.empty() || !resolved[0].weapons[0] ? 0 : resolved[0].weapons[0]->yaw_spread;
        const WeaponDef e11 = catalog.resolve_weapon(*catalog.find("wp_rifle_e11"));
        CHECK(near(slotted_spread, e11.yaw_spread * 0.97, 1e-4));  // Rifle Certification, level 2

        // Quest and mission experience goes to the slotted professions.
        server.award_profession_xp("Trainee", {{"pistols", 40}}, 60);
        run(0.3);
        CHECK(mine && profession_xp(*mine, "pistols") == 40);
        CHECK(mine && profession_xp(*mine, "rifles") == 210);
        client.disconnect();
        run(0.1);
    }
    {
        SqliteCharacterStore store(db_path);
        const auto saved = store.find_by_name("Trainee");
        CHECK(saved.has_value());
        const auto progress = saved ? store.list_professions(saved->id) : std::vector<ProfessionProgress>{};
        CHECK(profession_xp(progress, "rifles") == 210);
        CHECK(profession_xp(progress, "unarmored") == 11);
        CHECK(profession_xp(progress, "pistols") == 40);
        const auto loadouts = saved ? store.list_loadouts(saved->id) : std::vector<CharacterLoadout>{};
        CHECK(loadouts.size() == 1 && loadouts[0].specializations == std::vector<std::string>{"rifles"});

        // Rewarded while away (on a battle's server): saved for when they're back.
        net::LoopbackHub links;
        net::LoopbackLink srv_link{links, {1, 1}};
        ServerConfig config;
        config.catalog = &catalog;
        config.professions = &book;
        GameServer server(srv_link, world, config, &store);
        server.award_profession_xp("Trainee", {{"carbines", 25}, {"bounty_hunter", 99}}, 30);
        const auto later = saved ? store.list_professions(saved->id) : std::vector<ProfessionProgress>{};
        CHECK(profession_xp(later, "carbines") == 25);
        CHECK(profession_xp(later, "bounty_hunter") == 0);  // not in the game yet
        CHECK(profession_xp(later, "rifles") == 240);
    }
    std::filesystem::remove(db_path);
}

} // namespace

int main() {
    try {
        test_roster();
        test_perks();
        test_server_professions();
    } catch (const std::exception& error) {
        std::printf("FAIL: %s\n", error.what());
        return 1;
    }
    return swm::test::finish("profession_test");
}
