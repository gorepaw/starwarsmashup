#include "swm/core/items.h"
#include "swm/core/persistence.h"

#include <filesystem>
#include <fstream>

#include "test.h"

using namespace swm::core;
using swm::test::near;

int main() {
    const std::filesystem::path ini_path = "swm_items_test_catalog.ini";
    {
        std::ofstream out(ini_path);
        out << "[item]\n"
            << "Id = wp_rifle_e11\n"
            << "Name = E-11 Blaster Carbine\n"
            << "Kind = weapon\n"
            << "Archetype = rifle_auto\n"
            << "DamageMult = 1.0\n"
            << "RateMult = 1.1\n"
            << "\n"
            << "[item]\n"
            << "Id = wp_pistol_dl44\n"
            << "Name = DL-44 Heavy Pistol\n"
            << "Kind = weapon\n"
            << "Archetype = pistol\n"
            << "Slot = sidearm\n"
            << "DamageMult = 1.2\n"
            << "RateMult = 0.85\n"
            << "\n"
            << "[item]\n"
            << "Id = armor_stormtrooper_chest\n"
            << "Name = Stormtrooper Chestplate\n"
            << "Kind = armor\n"
            << "Slot = chest\n"
            << "Modifier = max_health + 50\n"
            << "Modifier = mitigation + 0.10\n"
            << "\n"
            << "[item]\n"
            << "Id = psg_mark1\n"
            << "Name = Personal Shield Generator Mk I\n"
            << "Kind = deflector_shield\n"
            << "Slot = deflector_shield\n"
            << "Modifier = mitigation_laser + 0.12\n";
    }

    ItemCatalog catalog;
    catalog.load(ini_path);
    std::filesystem::remove(ini_path);

    CHECK(catalog.all().size() == 4);
    const ItemDef* e11 = catalog.find("wp_rifle_e11");
    CHECK(e11 != nullptr && e11->anim_set == "rifle" && e11->slot == "primary");

    // Standard queue flattens quality; Gear queue scales within +-8% band.
    const WeaponDef std_w = catalog.resolve_weapon(*e11, 1.0, false);
    const WeaponDef base_w = catalog.resolve_weapon(*e11, 0.5, true);
    const WeaponDef high_w = catalog.resolve_weapon(*e11, 1.0, true);
    CHECK(near(std_w.damage, base_w.damage));
    CHECK(high_w.damage > base_w.damage && near(high_w.damage, base_w.damage * 1.08));

    // Build loadout for Human vs Wookiee: Wookiee skips chest armor, keeps deflector shield.
    const std::vector<ItemInstance> inv{
        {1, 10, "wp_rifle_e11", 0.5, "", "{}"},
        {2, 10, "wp_pistol_dl44", 0.5, "", "{}"},
        {3, 10, "armor_stormtrooper_chest", 0.5, "", "{}"},
        {4, 10, "psg_mark1", 0.5, "", "{}"},
    };
    CharacterLoadout cl;
    cl.primary_id = 1;
    cl.sidearm_id = 2;
    cl.gear_ids["chest"] = 3;
    cl.gear_ids["deflector_shield"] = 4;

    const Loadout hum_lo = catalog.build_loadout(cl, inv, Species::human, true);
    CHECK(hum_lo.weapons[primary_slot] && hum_lo.weapons[sidearm_slot]);
    CHECK(hum_lo.gear.size() == 2);

    const Loadout wke_lo = catalog.build_loadout(cl, inv, Species::wookiee, true);
    CHECK(wke_lo.gear.size() == 1 && wke_lo.gear[0].slot == "deflector_shield");

    const CharacterStats hum_stats = stats_for(hum_lo, Species::human);
    const CharacterStats wke_stats = stats_for(wke_lo, Species::wookiee);
    CHECK(near(hum_stats.max_health, 450.0));
    CHECK(near(hum_stats.mitigation_by_type[size_t(DamageType::laser)], 0.12));
    CHECK(near(wke_stats.mitigation_by_type[size_t(DamageType::laser)], 0.12));

    // Starter kit + resolve_worn_models (appearance overrides & Wookiee slot blocking)
    MemoryCharacterStore store;
    const auto bob = store.create_character({0, "Bob", {0, 0, 0}, 0, "human", "male"});
    ensure_starter_kit(store, bob.id);
    const auto bob_items = store.list_items(bob.id);
    const auto bob_los = store.list_loadouts(bob.id);
    CHECK(bob_items.size() == 7 && bob_los.size() == 1);
    const auto bob_after = *store.find(bob.id);
    CHECK(bob_after.credits == 500 && bob_after.faction_points == 100);

    // Reward calculation: Standard vs Gear
    const BattleReward std_win = compute_battle_reward(true, false, 4, 2, QueueMode::standard);
    const BattleReward gear_win = compute_battle_reward(true, false, 4, 2, QueueMode::gear);
    CHECK(std_win.xp == 470 && std_win.faction_points == 190 && std_win.credits == 340);
    CHECK(gear_win.faction_points > std_win.faction_points && gear_win.credits > std_win.credits);

    // Worn models: appearance overrides armor visually; Wookiee skips armor chest
    const std::filesystem::path worn_ini = "swm_worn_test_catalog.ini";
    {
        std::ofstream out(worn_ini);
        out << "[item]\nId = armor_stormtrooper_chest\nKind = armor\nSlot = chest\nModel = armor/stormtrooper/chest_{g}.gltf\n"
            << "[item]\nId = cloth_jacket_rebel\nKind = clothing\nSlot = jacket\nModel = clothing/jacket_rebel_{g}.gltf\n";
    }
    ItemCatalog worn_cat;
    worn_cat.load(worn_ini);
    std::filesystem::remove(worn_ini);
    std::vector<ItemInstance> winv{{1, 1, "armor_stormtrooper_chest", 0.5, "", "{}"},
                                   {2, 1, "cloth_jacket_rebel", 0.5, "", "{}"}};
    CharacterLoadout wcl;
    wcl.gear_ids["chest"] = 1;
    CHECK(resolve_worn_models(worn_cat, wcl, winv, Species::human, Gender::female) == "armor/stormtrooper/chest_f.gltf");
    CHECK(resolve_worn_models(worn_cat, wcl, winv, Species::wookiee, Gender::male).empty());
    wcl.appearance_ids["jacket"] = 2;
    CHECK(resolve_worn_models(worn_cat, wcl, winv, Species::human, Gender::male) == "clothing/jacket_rebel_m.gltf");

    return swm::test::finish("items_test");
}
