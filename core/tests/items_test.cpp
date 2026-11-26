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

    // Mk tiers: names, levels, and the power curve (x2 at Mk X).
    CHECK(mark_name(0).empty() && mark_name(4) == "Mk IV" && mark_name(10) == "Mk X" && mark_name(12) == "Mk X");
    CHECK(mark_level(0) == 1 && mark_level(1) == 10 && mark_level(9) == 90 && mark_level(10) == 100);
    CHECK(item_display_name(*e11, 4) == "E-11 Blaster Carbine Mk IV" &&
          item_display_name(*e11, 0) == "E-11 Blaster Carbine");
    CHECK(near(catalog.resolve_weapon(*e11, 0.5, true, 10).damage, base_w.damage * 2));
    CHECK(near(catalog.resolve_weapon(*e11, 0.5, true, 5).damage, base_w.damage * 1.5));
    CHECK(near(catalog.resolve_weapon(*e11, 1.0, true, 10).damage, base_w.damage * 2 * 1.08));
    {
        double total = 0;
        for (const std::string_view s : {"deflector_shield", "helmet", "chest", "biceps", "bracers", "gloves", "belt",
                                         "leggings", "boots"}) {
            total += defense_mark_weight(s);
        }
        CHECK(near(total, 1.0) && defense_mark_weight("shirt") == 0);

        std::vector<ItemInstance> marked = inv;
        for (ItemInstance& i : marked) i.mark = 10;
        const Loadout hum10_lo = catalog.build_loadout(cl, marked, Species::human, true);
        const CharacterStats hum10 = stats_for(hum10_lo, Species::human);
        // Chest and Deflector Shield worn (0.2 + 0.3 of the weight) at Mk X: x1.5.
        CHECK(near(hum10.max_health, 450.0 * 1.5));
        CHECK(near(hum10.mitigation_by_type[size_t(DamageType::laser)], 0.12));  // marks don't touch mitigation
        CHECK(near(hum10_lo.weapons[primary_slot]->damage, hum_lo.weapons[primary_slot]->damage * 2));
        // A Wookiee wears only the Deflector Shield: its Mk X is the whole curve.
        const CharacterStats wke10 =
            stats_for(catalog.build_loadout(cl, marked, Species::wookiee, true), Species::wookiee);
        CHECK(near(wke10.max_health, wke_stats.max_health * 2));
        // Unmarked: no change.
        CHECK(catalog.build_loadout(cl, inv, Species::human, true).gear.size() == 2);

        // A Deflector Shield's mark grows its capacity and recharge.
        ItemDef psg;
        psg.kind = ItemKind::deflector_shield;
        psg.slot = "deflector_shield";
        psg.modifiers = {{Stat::shield_capacity, 100}, {Stat::shield_recharge, 20}, {Stat::mitigation, 0.1}};
        const Gear g = catalog.resolve_gear(psg, 0.5, true, 10);
        CHECK(near(g.modifiers[0].add, 200) && near(g.modifiers[1].add, 40) && near(g.modifiers[2].add, 0.1));
    }

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
            << "OnHit = burn\n"
            << "[item]\nId = a_hvy\nKind = ammo\nAmmoClass = heavy\nAmmoBatch = 12\n"
            << "[hit_effect]\nId = burn\nName = Burning\nChance = 0.5\nDuration = 3\nDamagePerSecond = 12\n"
            << "DamageType = laser\nModifier = run_speed * 0.9\n"
            << "[item]\nId = r2\nKind = weapon\nArchetype = rifle_auto\nSlot = primary\nVelocityMult = 2\n"
            << "HeadshotMult = 1.5\nCritChance = 0.1\nCritMult = 2.5\nVehicleMult = 0.5\nShieldMult = 1.5\n"
            << "SwapMult = 0.5\nRecoil = 0.8\nAimSpeedMult = 1.25\n";
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
    // The ammo's effect is the gun's firing it.
    CHECK(ammo_cat.hit_effect("burn") != nullptr && ammo_cat.hit_effects().size() == 1);
    const auto& burn = std_lo.weapons[primary_slot]->hit_effect;
    CHECK(burn && burn->id == "burn" && burn->name == "Burning" && burn->chance == 0.5 && burn->duration == 3 &&
          burn->damage_per_second == 12 && burn->damage_type == DamageType::laser && burn->modifiers.size() == 1);
    // Without the ammo, none.
    CharacterLoadout bare;
    bare.primary_id = 1;
    CHECK(!ammo_cat.build_loadout(bare, ainv, Species::human, false).weapons[primary_slot]->hit_effect);

    // A weapon's handling keys, over the archetype's.
    const WeaponDef base = ammo_cat.resolve_weapon(*ammo_cat.find("r1"), 0.5, false);
    const WeaponDef tuned = ammo_cat.resolve_weapon(*ammo_cat.find("r2"), 0.5, false);
    CHECK(std::abs(tuned.bolt_speed - base.bolt_speed * 2) < 1e-6);
    CHECK(std::abs(tuned.bolt_speed * tuned.bolt_life - base.bolt_speed * base.bolt_life) < 1e-6);  // as far
    CHECK(std::abs(tuned.headshot_multiplier - base.headshot_multiplier * 1.5) < 1e-6);
    CHECK(std::abs(tuned.crit_chance - 0.1) < 1e-9 && std::abs(tuned.crit_multiplier - 2.5) < 1e-9);
    CHECK(std::abs(tuned.vehicle_factor - base.vehicle_factor * 0.5) < 1e-6);
    CHECK(std::abs(tuned.shield_factor - 1.5) < 1e-9 && std::abs(tuned.swap_time - base.swap_time * 0.5) < 1e-9);
    CHECK(std::abs(tuned.recoil - 0.8) < 1e-9 && std::abs(tuned.aim_speed - 1.25) < 1e-9);
    CHECK(base.recoil == 0 && base.aim_speed == 1 && !base.hit_effect);

    // An OnHit naming no [hit_effect] is an error.
    {
        std::ofstream out(ammo_ini);
        out << "[item]\nId = a_bad\nKind = ammo\nOnHit = nothing\n";
    }
    bool threw = false;
    try {
        ItemCatalog bad;
        bad.load(ammo_ini);
    } catch (const std::exception&) {
        threw = true;
    }
    std::filesystem::remove(ammo_ini);
    CHECK(threw);

    CharacterLoadout hcl;
    hcl.primary_id = 3;
    hcl.gear_ids["ammo_heavy"] = 4;
    const Loadout hvy_lo = ammo_cat.build_loadout(hcl, ainv, Species::human, false);
    CHECK(hvy_lo.unified_heavy_ammo == max_heavy_ammo);
    CHECK(hvy_lo.weapons[primary_slot] && hvy_lo.weapons[primary_slot]->ammo_class == AmmoClass::heavy);

    return swm::test::finish("items_test");
}
