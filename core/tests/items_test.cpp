#include "swm/core/items.h"

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

    return swm::test::finish("items_test");
}
