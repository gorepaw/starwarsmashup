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
    CHECK(bob_items.size() == 9 && bob_los.size() == 1);
    CHECK(!bob_los.empty() && bob_los[0].gear_ids.count("ammo_standard") == 1 &&
          bob_los[0].appearance_ids.count("shirt") == 1 &&
          bob_los[0].appearance_ids.count("pants") == 1 && bob_los[0].appearance_ids.count("boots_cosmetic") == 1);
    const auto bob_after = *store.find(bob.id);
    CHECK(bob_after.credits == 500 && bob_after.faction_points == 100);

    // Reward calculation: always Gear-Matters rules (+15% FP & Credits).
    const BattleReward gear_win = compute_battle_reward(true, false, 4, 2);
    CHECK(gear_win.xp == 470 && gear_win.faction_points == 218 && gear_win.credits == 391);
    static_assert(max_character_loadouts == 5);

    // Worn models: appearance overrides armor visually; Wookiee skips armor chest
    const std::filesystem::path worn_ini = "swm_worn_test_catalog.ini";
    {
        std::ofstream out(worn_ini);
        out << "[item]\nId = armor_stormtrooper_chest\nKind = armor\nSlot = chest\nModel = armor/stormtrooper/chest_{g}.gltf\n"
            << "[item]\nId = cloth_jacket_rebel\nKind = clothing\nSlot = jacket\nModel = clothing/jacket_rebel_{g}.gltf\n"
            << "[item]\nId = speeder_test\nKind = vehicle\nVehicle = swg_speeder_bike\nSlot = mount\nModel = swg_vehicles/speeder/speeder.gltf\n";
    }
    ItemCatalog worn_cat;
    worn_cat.load(worn_ini);
    std::filesystem::remove(worn_ini);
    std::vector<ItemInstance> winv{{1, 1, "armor_stormtrooper_chest", 0.5, "", "{}"},
                                   {2, 1, "cloth_jacket_rebel", 0.5, "", "{}"},
                                   {3, 1, "speeder_test", 0.5, "", "{}"}};
    CharacterLoadout wcl;
    wcl.gear_ids["chest"] = 1;
    wcl.gear_ids["mount"] = 3;  // a mount isn't worn: it's summoned
    CHECK(resolve_worn_models(worn_cat, wcl, winv, Species::human, Gender::female) == "armor/stormtrooper/chest_f.gltf");
    CHECK(resolve_worn_models(worn_cat, wcl, winv, Species::wookiee, Gender::male).empty());
    wcl.appearance_ids["jacket"] = 2;
    CHECK(resolve_worn_models(worn_cat, wcl, winv, Species::human, Gender::male) == "clothing/jacket_rebel_m.gltf");

    // Reputation standing helpers + opposing faction calculation
    CHECK(normalize_faction("CIS") == "separatists" && normalize_faction("Imperial") == "empire");
    CHECK(opposing_faction("rebellion") == "empire" && opposing_faction("republic") == "separatists");
    CHECK(reputation_tier(-3500) == RepTier::hated && reputation_tier(-100) == RepTier::unfriendly);
    CHECK(reputation_tier(0) == RepTier::neutral && reputation_tier(3000) == RepTier::friendly);
    CHECK(reputation_tier(9000) == RepTier::honored && reputation_tier(21000) == RepTier::revered);
    CHECK(reputation_tier(42000) == RepTier::exalted);

    // Unified ammo: caps (999 standard / 99 heavy) and horizontal sidegrades
    const std::filesystem::path ammo_ini = "swm_ammo_test_catalog.ini";
    {
        std::ofstream out(ammo_ini);
        out << "[item]\nId = r1\nKind = weapon\nArchetype = rifle_auto\nSlot = primary\n"
            << "[item]\nId = h1\nKind = weapon\nArchetype = heavy_launcher\nSlot = primary\n"
            << "[item]\nId = a_burn\nKind = ammo\nAmmoClass = standard\nAmmoBatch = 100\n"
            << "DamageMult = 1.15\nRateMult = 0.87\nDamageType = laser\nBoltColor = red\n"
            << "[item]\nId = a_hvy\nKind = ammo\nAmmoClass = heavy\nAmmoBatch = 12\n";
    }
    ItemCatalog ammo_cat;
    ammo_cat.load(ammo_ini);
    std::filesystem::remove(ammo_ini);
    std::vector<ItemInstance> ainv{
        {1, 1, "r1", 0.5, "", "{}", 1},
        {2, 1, "a_burn", 0.5, "", "{}", 1500},
        {3, 1, "h1", 0.5, "", "{}", 1},
        {4, 1, "a_hvy", 0.5, "", "{}", 150},
    };
    CharacterLoadout acl;
    acl.primary_id = 1;
    acl.gear_ids["ammo_standard"] = 2;
    const Loadout std_lo = ammo_cat.build_loadout(acl, ainv, Species::human, false);
    CHECK(std_lo.unified_standard_ammo == max_standard_ammo);
    CHECK(std_lo.weapons[primary_slot] && std_lo.weapons[primary_slot]->ammo_class == AmmoClass::standard);
    CHECK(std::abs(std_lo.weapons[primary_slot]->damage - WeaponDef{}.damage * 1.15) < 1e-6);

    CharacterLoadout hcl;
    hcl.primary_id = 3;
    hcl.gear_ids["ammo_heavy"] = 4;
    const Loadout hvy_lo = ammo_cat.build_loadout(hcl, ainv, Species::human, false);
    CHECK(hvy_lo.unified_heavy_ammo == max_heavy_ammo);
    CHECK(hvy_lo.weapons[primary_slot] && hvy_lo.weapons[primary_slot]->ammo_class == AmmoClass::heavy);

    return swm::test::finish("items_test");
}
