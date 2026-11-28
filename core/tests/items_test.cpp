#include "swm/core/items.h"
#include "swm/core/combat.h"
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
            << "ArmorClass = medium\n"
            << "Slot = chest\n"
            << "Resist = laser:1\n"
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

    // Grades: -8% Flawed, +8% Fine, +16% Exceptional (Legendary +16% for now).
    const WeaponDef base_w = catalog.resolve_weapon(*e11);
    CHECK(near(catalog.resolve_weapon(*e11, ItemGrade::flawed).damage, base_w.damage * 0.92));
    CHECK(near(catalog.resolve_weapon(*e11, ItemGrade::fine).damage, base_w.damage * 1.08));
    CHECK(near(catalog.resolve_weapon(*e11, ItemGrade::exceptional).damage, base_w.damage * 1.16));
    CHECK(near(catalog.resolve_weapon(*e11, ItemGrade::legendary).damage, base_w.damage * 1.16));
    CHECK(catalog.resolve_weapon(*e11, ItemGrade::exceptional).heat_per_shot <= base_w.heat_per_shot);
    CHECK(item_grade_name(ItemGrade::exceptional) == "Exceptional" && item_grade_from_name("fine") == ItemGrade::fine &&
          !item_grade_from_name("rare"));
    CHECK(grade_from_quality(0.1) == ItemGrade::flawed && grade_from_quality(0.5) == ItemGrade::standard &&
          grade_from_quality(0.9) == ItemGrade::fine);
    CHECK(saved_grade("3") == ItemGrade::exceptional && saved_grade("0.9000") == ItemGrade::fine &&
          saved_grade("") == ItemGrade::standard && saved_grade("9") == ItemGrade::legendary);
    CHECK(item_takes_grade(*e11) && shown_grade(*e11, ItemGrade::fine) == ItemGrade::fine);
    {
        ItemDef junk;
        junk.kind = ItemKind::junk;
        CHECK(!item_takes_grade(junk) && !shown_grade(junk, ItemGrade::fine));  // white
        junk.grade = ItemGrade::legendary;
        CHECK(shown_grade(junk, ItemGrade::standard) == ItemGrade::legendary);
        ItemDef relic = *e11;
        relic.grade = ItemGrade::legendary;  // its def's grade, whatever the copy's
        CHECK(near(catalog.resolve_weapon(relic, ItemGrade::flawed).damage, base_w.damage * 1.16));
    }

    // Build loadout for Human vs Wookiee: every species wears every slot (a
    // Wookiee's chest armor only doesn't show).
    const std::vector<ItemInstance> inv{
        {1, 10, "wp_rifle_e11", ItemGrade::standard, "", "{}"},
        {2, 10, "wp_pistol_dl44", ItemGrade::standard, "", "{}"},
        {3, 10, "armor_stormtrooper_chest", ItemGrade::standard, "", "{}"},
        {4, 10, "psg_mark1", ItemGrade::standard, "", "{}"},
    };
    CharacterLoadout cl;
    cl.primary_id = 1;
    cl.sidearm_id = 2;
    cl.gear_ids["chest"] = 3;
    cl.gear_ids["deflector_shield"] = 4;

    const Loadout hum_lo = catalog.build_loadout(cl, inv, Species::human);
    CHECK(hum_lo.weapons[primary_slot] && hum_lo.weapons[sidearm_slot]);
    CHECK(hum_lo.gear.size() == 2);

    const Loadout wke_lo = catalog.build_loadout(cl, inv, Species::wookiee);
    CHECK(wke_lo.gear.size() == 2);

    const CharacterStats hum_stats = stats_for(hum_lo, Species::human);
    const CharacterStats wke_stats = stats_for(wke_lo, Species::wookiee);
    // The chest: its 2/7 share of the medium budget (100 health, 12% typed,
    // all of it laser), with the shield's 12% laser.
    const double chest_hp = 400.0 + 100.0 * 2 / 7, chest_laser = 0.12 + 0.12 * 2 / 7;
    CHECK(near(hum_stats.max_health, chest_hp));
    CHECK(near(wke_stats.max_health, chest_hp + 40));  // and the Wookiee's own 40
    CHECK(near(hum_stats.mitigation_by_type[size_t(DamageType::laser)], chest_laser));
    CHECK(near(wke_stats.mitigation_by_type[size_t(DamageType::laser)], chest_laser));
    CHECK(near(hum_stats.mitigation, 0.20 * 2 / 7));

    // Mk tiers: names, levels, and the power curve (x2 at Mk X).
    CHECK(mark_name(0).empty() && mark_name(4) == "Mk IV" && mark_name(10) == "Mk X" && mark_name(12) == "Mk X");
    CHECK(mark_level(0) == 1 && mark_level(1) == 10 && mark_level(9) == 90 && mark_level(10) == 100);
    CHECK(item_display_name(*e11, 4) == "E-11 Blaster Carbine Mk IV" &&
          item_display_name(*e11, 0) == "E-11 Blaster Carbine");
    CHECK(near(catalog.resolve_weapon(*e11, ItemGrade::standard, 10).damage, base_w.damage * 2));
    CHECK(near(catalog.resolve_weapon(*e11, ItemGrade::standard, 5).damage, base_w.damage * 1.5));
    CHECK(near(catalog.resolve_weapon(*e11, ItemGrade::fine, 10).damage, base_w.damage * 2 * 1.08));
    {
        double total = 0;
        for (const std::string_view s : {"deflector_shield", "helmet", "chest", "biceps", "bracers", "gloves", "belt",
                                         "leggings", "boots"}) {
            total += defense_mark_weight(s);
        }
        CHECK(near(total, 1.0) && defense_mark_weight("shirt") == 0);

        std::vector<ItemInstance> marked = inv;
        for (ItemInstance& i : marked) i.mark = 10;
        const Loadout hum10_lo = catalog.build_loadout(cl, marked, Species::human);
        const CharacterStats hum10 = stats_for(hum10_lo, Species::human);
        // Chest and Deflector Shield worn (0.2 + 0.3 of the weight) at Mk X: x1.5.
        CHECK(near(hum10.max_health, chest_hp * 1.5));
        CHECK(near(hum10.mitigation_by_type[size_t(DamageType::laser)], chest_laser));  // marks don't touch mitigation
        CHECK(near(hum10_lo.weapons[primary_slot]->damage, hum_lo.weapons[primary_slot]->damage * 2));
        // A Wookiee's marks count the same as anyone's.
        const CharacterStats wke10 =
            stats_for(catalog.build_loadout(cl, marked, Species::wookiee), Species::wookiee);
        CHECK(near(wke10.max_health, wke_stats.max_health * 1.5));
        // Unmarked: no change.
        CHECK(catalog.build_loadout(cl, inv, Species::human).gear.size() == 2);

        // A Deflector Shield's mark grows its capacity and recharge.
        ItemDef psg;
        psg.kind = ItemKind::deflector_shield;
        psg.slot = "deflector_shield";
        psg.modifiers = {{Stat::shield_capacity, 100}, {Stat::shield_recharge, 20}, {Stat::mitigation, 0.1}};
        const Gear g = catalog.resolve_gear(psg, ItemGrade::standard, 10);
        CHECK(near(g.modifiers[0].add, 200) && near(g.modifiers[1].add, 40) && near(g.modifiers[2].add, 0.1));
    }

    // Starter kit + resolve_worn_models (appearance overrides & slots a Wookiee doesn't show)
    MemoryCharacterStore store;
    const auto bob = store.create_character({0, "Bob", {0, 0, 0}, 0, "human", "male"});
    ensure_starter_kit(store, bob.id);
    const auto bob_items = store.list_items(bob.id);
    const auto bob_los = store.list_loadouts(bob.id);
    // (No ammo: plain ammo isn't counted, so an empty ammo slot fires as well.)
    CHECK(bob_items.size() == 8 && bob_los.size() == 1);
    CHECK(!bob_los.empty() && bob_los[0].gear_ids.count("ammo_standard") == 0 &&
          bob_los[0].gear_ids.count("chest") == 1 && bob_los[0].gear_ids.count("leggings") == 1 &&
          bob_los[0].gear_ids.count("boots") == 1 && bob_los[0].appearance_ids.empty());
    const auto bob_after = *store.find(bob.id);
    CHECK(bob_after.credits == 500 && bob_after.faction_points == 100);

    // Reward calculation: always Gear-Matters rules (+15% FP & Credits).
    const BattleReward gear_win = compute_battle_reward(true, false, 4, 2);
    CHECK(gear_win.xp == 470 && gear_win.faction_points == 218 && gear_win.credits == 391);
    // Leaving early keeps the kills' and captures' share, not the outcome's.
    const BattleReward left = compute_left_battle_reward(4, 2);
    CHECK(left.xp == 220 && left.faction_points == 103 && left.credits == 161);
    static_assert(max_character_loadouts == 5);

    // Worn models: appearance overrides armor visually; a Wookiee's chest armor doesn't show
    const std::filesystem::path worn_ini = "swm_worn_test_catalog.ini";
    {
        std::ofstream out(worn_ini);
        out << "[item]\nId = armor_stormtrooper_chest\nKind = armor\nSlot = chest\nModel = armor/stormtrooper/chest_{g}.gltf\n"
            << "[item]\nId = cloth_jacket_rebel\nKind = clothing\nSlot = chest\nModel = clothing/jacket_rebel_{g}.gltf\n"
            << "[item]\nId = speeder_test\nKind = vehicle\nVehicle = swg_speeder_bike\nSlot = mount\nModel = swg_vehicles/speeder/speeder.gltf\n"
            << "[item]\nId = helm\nKind = armor\nSlot = helmet\nModel = armor/h_{g}.gltf\n"
            << "[item]\nId = suit\nKind = bodysuit\nModel = characters/suit/suit.gltf\n";
    }
    ItemCatalog worn_cat;
    worn_cat.load(worn_ini);
    std::filesystem::remove(worn_ini);
    std::vector<ItemInstance> winv{{1, 1, "armor_stormtrooper_chest", ItemGrade::standard, "", "{}"},
                                   {2, 1, "cloth_jacket_rebel", ItemGrade::standard, "", "{}"},
                                   {3, 1, "speeder_test", ItemGrade::standard, "", "{}"},
                                   {4, 1, "helm", ItemGrade::standard, "", "{}"},
                                   {5, 1, "suit", ItemGrade::standard, "", "{}"}};
    CharacterLoadout wcl;
    wcl.gear_ids["chest"] = 1;
    wcl.gear_ids["mount"] = 3;  // a mount isn't worn: it's summoned
    CHECK(resolve_worn_models(worn_cat, wcl, winv, Species::human, Gender::female) == "armor/stormtrooper/chest_f.gltf");
    CHECK(resolve_worn_models(worn_cat, wcl, winv, Species::wookiee, Gender::male).empty());
    wcl.appearance_ids["chest"] = 2;
    CHECK(resolve_worn_models(worn_cat, wcl, winv, Species::human, Gender::male) == "clothing/jacket_rebel_m.gltf");
    wcl.appearance_ids["boots"] = 4;  // only where it fits
    wcl.appearance_ids["helmet"] = 4;
    CHECK(resolve_worn_models(worn_cat, wcl, winv, Species::human, Gender::male) ==
          "clothing/jacket_rebel_m.gltf,armor/h_m.gltf");
    // A bodysuit is a look only: in the gear it's nothing; in the override
    // it shows in place of everything.
    CHECK(worn_cat.find("suit")->slot == "full_body");
    CharacterLoadout scl;
    scl.gear_ids = {{"chest", 1}, {"full_body", 5}};
    CHECK(resolve_worn_models(worn_cat, scl, winv, Species::human, Gender::male) == "armor/stormtrooper/chest_m.gltf");
    scl.appearance_ids = {{"helmet", 4}, {"full_body", 5}};
    CHECK(resolve_worn_models(worn_cat, scl, winv, Species::human, Gender::male) == "override:characters/suit/suit.gltf");

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
        {1, 1, "r1", ItemGrade::standard, "", "{}", 1},
        {2, 1, "a_burn", ItemGrade::standard, "", "{}", 2500},
        {3, 1, "h1", ItemGrade::standard, "", "{}", 1},
        {4, 1, "a_hvy", ItemGrade::standard, "", "{}", 250},
    };
    CharacterLoadout acl;
    acl.primary_id = 1;
    acl.gear_ids["ammo_standard"] = 2;
    const Loadout std_lo = ammo_cat.build_loadout(acl, ainv, Species::human);
    // (A stack is carried up to the most any character can carry: a perk's
    // capacity lets one grow past the standard, and it's never cut back.)
    CHECK(std_lo.unified_standard_ammo == int(max_standard_ammo * max_ammo_capacity));
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
    CHECK(!ammo_cat.build_loadout(bare, ainv, Species::human).weapons[primary_slot]->hit_effect);

    // A weapon's handling keys, over the archetype's.
    const WeaponDef base = ammo_cat.resolve_weapon(*ammo_cat.find("r1"));
    const WeaponDef tuned = ammo_cat.resolve_weapon(*ammo_cat.find("r2"));
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
    const Loadout hvy_lo = ammo_cat.build_loadout(hcl, ainv, Species::human);
    // Plain warheads aren't counted: the launcher carries its own load (-1).
    CHECK(ammo_cat.find("a_hvy") && is_generic_ammo(*ammo_cat.find("a_hvy")));
    CHECK(!is_generic_ammo(*ammo_cat.find("a_burn")));
    CHECK(hvy_lo.unified_heavy_ammo == -1 && hvy_lo.unified_standard_ammo == -1);
    CHECK(hvy_lo.weapons[primary_slot] && hvy_lo.weapons[primary_slot]->ammo_class == AmmoClass::heavy);
    CHECK(ammo_cat.build_loadout(bare, ainv, Species::human).unified_standard_ammo == -1);
    // Out in the world those guns never run dry; a match keeps their load.
    {
        Loadout world = hvy_lo;
        const int match_load = hvy_lo.weapons[primary_slot]->ammo;
        unlimited_world_ammo(world);
        CHECK(match_load > 0 && world.weapons[primary_slot]->ammo == 0);
        Loadout counted = std_lo;  // an effect ammo's stack is still spent
        const int counted_load = counted.weapons[primary_slot]->ammo;
        unlimited_world_ammo(counted);
        CHECK(counted.weapons[primary_slot]->ammo == counted_load);
    }
    // In a match, an effect stack run dry: from the next respawn its guns are
    // the plain ones, with their own load, and the stack counts as spent.
    {
        Loadout match = std_lo;
        match.plain_weapons = ammo_cat.build_loadout(bare, ainv, Species::human).weapons;
        CHECK(fall_back_to_plain_ammo(match, AmmoClass::standard));
        CHECK(!fall_back_to_plain_ammo(match, AmmoClass::standard));  // (once)
        CHECK(match.unified_standard_ammo == -1 && match.ammo_spent[size_t(AmmoClass::standard)]);
        CHECK(!match.weapons[primary_slot]->hit_effect &&
              std::abs(match.weapons[primary_slot]->damage - WeaponDef{}.damage) < 1e-6);
        MoveState fresh;
        reset_loadout(fresh, match, CharacterStats{});
        CHECK(remaining_unified_ammo(fresh.combat, match, AmmoClass::standard) == 0);  // what's reported
        CHECK(fresh.combat.weapons[primary_slot].rounds == match.weapons[primary_slot]->rounds_per_clip);
        CHECK(!fall_back_to_plain_ammo(match, AmmoClass::heavy));  // (no heavy effect ammo)
    }

    // The sidegrade budget: the archetype scores 1; trading damage for rate
    // of fire breaks even; every weapon and ammo in the real catalog is within
    // tolerance of its budget (a species weapon's is bigger).
    {
        ItemDef plain;
        CHECK(near(sidegrade_score(plain), 1.0));
        ItemDef trade;
        trade.damage_mult = 1.25;
        trade.rate_mult = 0.8;
        CHECK(near(sidegrade_score(trade), 1.0));
        trade.modifiers = {{Stat::run_speed, 0, 0.9}};
        CHECK(sidegrade_score(trade) < 0.9);
        trade.species = {Species::wookiee};
        CHECK(near(sidegrade_budget(trade), 1.0 + species_weapon_bonus));
        CHECK(species_can_use(trade, Species::wookiee) && !species_can_use(trade, Species::human) &&
              species_can_use(plain, Species::human));

        std::filesystem::path root = std::filesystem::current_path();
        while (!root.empty() && !std::filesystem::exists(root / "game" / "items" / "swg_catalog.ini") &&
               root.parent_path() != root) {
            root = root.parent_path();
        }
        ItemCatalog real;
        real.load(root / "game" / "items" / "swg_catalog.ini");
        for (const ItemDef& def : real.all()) {
            if (def.kind != ItemKind::weapon && def.kind != ItemKind::ammo) continue;
            const double score = sidegrade_score(def);
            const bool ok = std::abs(score - sidegrade_budget(def)) <= sidegrade_tolerance;
            std::printf("%-28s %-15s %.3f / %.2f%s\n", def.id.c_str(), weapon_archetype_info(def.archetype).id.c_str(),
                        score, sidegrade_budget(def), ok ? "" : "  OVER/UNDER");
            CHECK(ok);
        }

        // Armor (docs/ARMOR.md): every piece is its slot's share of its
        // class's budget, its typed resistance spread by its Resist, with at
        // most one small perk; the slots' shares make a whole kit.
        {
            double shares = 0;
            for (const std::string_view s : worn_slots) shares += armor_slot_share(s);
            CHECK(near(shares, 1.0) && armor_slot_share(full_body_slot) == 0 &&
                  armor_slot_share("deflector_shield") == 0);
            CHECK(armor_budget("light").mitigation < armor_budget("medium").mitigation &&
                  armor_budget("medium").mitigation < armor_budget("heavy").mitigation &&
                  armor_budget("light").run_speed > armor_budget("heavy").run_speed && armor_budget("none").health == 0);
        }
        for (const ItemDef& def : real.all()) {
            if (def.kind == ItemKind::clothing) {
                CHECK(is_worn_slot(def.slot) && def.modifiers.empty());
                continue;
            }
            if (def.kind == ItemKind::bodysuit) {  // the one look-only item
                CHECK(def.slot == full_body_slot && def.modifiers.empty() && !def.model_path.empty());
                continue;
            }
            if (def.kind != ItemKind::armor) continue;
            double resist = 0;
            for (const auto& [type, share] : def.resist) resist += share;
            bool perk_ok = def.perks.size() <= 1;
            for (const StatModifier& p : def.perks) {
                perk_ok = perk_ok && (p.add == 0 ? std::abs(p.multiply - 1) <= 0.05 + 1e-9
                                                 : p.stat == Stat::max_energy && p.add <= 20);
            }
            std::printf("%-28s %-7s %-9s resist %.2f perks %zu%s\n", def.id.c_str(), def.armor_class.c_str(),
                        def.slot.c_str(), resist, def.perks.size(), near(resist, 1.0) && perk_ok ? "" : "  BAD");
            CHECK(is_worn_slot(def.slot) && near(resist, 1.0) && perk_ok);
        }
        {
            // A whole Stormtrooper kit is the medium budget.
            std::vector<ItemInstance> kit;
            CharacterLoadout kit_lo;
            for (const std::string_view s : worn_slots) {
                const std::string id = "armor_stormtrooper_" + std::string(s);
                CHECK(real.find(id) != nullptr);
                kit.push_back({kit.size() + 1, 1, id});
                kit_lo.gear_ids[std::string(s)] = kit.back().id;
            }
            const Loadout full = real.build_loadout(kit_lo, kit, Species::human);
            const CharacterStats st = stats_for(full, Species::human);
            const ArmorBudget& mb = armor_budget("medium");
            CHECK(full.armor_class == "medium");
            CHECK(near(st.mitigation, mb.mitigation) && near(st.mitigation_by_type[size_t(DamageType::laser)], mb.typed));
            CHECK(near(st.max_health, CharacterStats{}.max_health + mb.health));
            CHECK(near(st.move.run_speed, MoveParams{}.run_speed * mb.run_speed));

            // A bodysuit gives nothing, wherever it's put (and changes
            // nothing about the kit). In the cosmetic override, nothing counts.
            kit.push_back({100, 1, "suit_imp_stormtrooper"});
            kit_lo.gear_ids["full_body"] = 100;
            const Loadout suited = real.build_loadout(kit_lo, kit, Species::human);
            CHECK(suited.armor_class == "medium" &&
                  near(stats_for(suited, Species::human).mitigation, mb.mitigation) &&
                  near(stats_for(suited, Species::human).max_health, st.max_health));
            CharacterLoadout look_lo;
            look_lo.appearance_ids = kit_lo.gear_ids;
            const CharacterStats looks = stats_for(real.build_loadout(look_lo, kit, Species::human), Species::human);
            CHECK(looks.mitigation == 0 && near(looks.max_health, CharacterStats{}.max_health));

            // Clothes are worn (unarmored); a loadout mostly in them is too.
            std::vector<ItemInstance> mixed{{1, 1, "cloth_shirt_tunic"}, {2, 1, "cloth_pants_cargo"},
                                            {3, 1, "armor_composite_helmet"}};
            CharacterLoadout mixed_lo;
            mixed_lo.gear_ids = {{"chest", 1}, {"leggings", 2}, {"helmet", 3}};
            CHECK(real.build_loadout(mixed_lo, mixed, Species::human).armor_class == "none");
            mixed_lo.gear_ids.erase("leggings");
            CHECK(real.build_loadout(mixed_lo, mixed, Species::human).armor_class == "heavy");  // a tie: the heavier

            // What goes where: worn things in either panel, the rest in the gear.
            const ItemDef* helm = real.find("armor_ubese_helmet");
            const ItemDef* tunic = real.find("cloth_shirt_tunic");
            const ItemDef* e11r = real.find("wp_rifle_e11");
            CHECK(item_fits_slot(*helm, "helmet", false) && item_fits_slot(*helm, "helmet", true) &&
                  !item_fits_slot(*helm, "chest", true));
            CHECK(item_fits_slot(*tunic, "chest", false) && item_fits_slot(*tunic, "chest", true));
            CHECK(item_fits_slot(*e11r, "primary", false) && !item_fits_slot(*e11r, "primary", true) &&
                  !item_fits_slot(*e11r, "sidearm", false));
            const ItemDef* b1 = real.find("suit_cis_bdroid");
            CHECK(b1 && b1->kind == ItemKind::bodysuit && item_fits_slot(*b1, "full_body", true) &&
                  !item_fits_slot(*b1, "full_body", false) && !item_fits_slot(*helm, "full_body", true));
        }

        // Species armor (docs/ARMOR.md): a Wookiee or an Ithorian shows its
        // own set's piece of the class of what it wears; nothing for clothing,
        // or where its species has no piece (a Wookiee's helmet).
        {
            size_t wookiee = 0, ithorian = 0;
            for (const SpeciesArmorLook& l : real.species_looks()) {
                (l.species == Species::wookiee ? wookiee : ithorian) += 1;
            }
            CHECK(wookiee == 9 && ithorian == 21);
            const std::vector<ItemInstance> worn{{1, 1, "armor_composite_chest"}, {2, 1, "armor_ubese_helmet"},
                                                 {3, 1, "cloth_pants_cargo"}, {4, 1, "armor_stormtrooper_bracers"}};
            CharacterLoadout wl;
            wl.gear_ids = {{"chest", 1}, {"helmet", 2}, {"leggings", 3}, {"bracers", 4}};
            CHECK(resolve_worn_models(real, wl, worn, Species::wookiee, Gender::male) ==
                  "armor/kashyyykian_black_mountain/bracer_l_m.gltf,armor/kashyyykian_black_mountain/bracer_r_m.gltf,"
                  "armor/kashyyykian_hunting/chest_m.gltf");
            CHECK(resolve_worn_models(real, wl, worn, Species::ithorian, Gender::female) ==
                  "armor/ithorian_guardian/bracer_l_f.gltf,armor/ithorian_guardian/bracer_r_f.gltf,"
                  "armor/ithorian_defender/chest_f.gltf,armor/ithorian_sentinel/helmet_f.gltf");
            // The cosmetic override's piece decides, as it shows in place of the gear's.
            CharacterLoadout ol = wl;
            ol.appearance_ids = {{"chest", 2}};  // (a helmet: doesn't fit the chest, so no change)
            CHECK(resolve_worn_models(real, ol, worn, Species::wookiee, Gender::male) ==
                  resolve_worn_models(real, wl, worn, Species::wookiee, Gender::male));
            ol.appearance_ids = {{"chest", 5}};
            std::vector<ItemInstance> both = worn;
            both.push_back({5, 1, "armor_ubese_shirt"});
            CHECK(resolve_worn_models(real, ol, both, Species::wookiee, Gender::male).ends_with(
                "armor/kashyyykian_ceremonial/chest_m.gltf"));
        }

        // Paint (docs/ARMOR.md): a Katarn piece is drawn in its owner's
        // scheme, from its color_json (a quartermaster's faction's), else
        // its neutral one; an item without schemes takes none.
        {
            const ItemDef* chest = real.find("armor_katarn_chest");
            CHECK(chest && chest->paints.size() == 3);
            CHECK(painted_color_json(*chest, "Imperial") == R"({"paint":"empire"})" &&
                  painted_color_json(*chest, "republic") == "{}" &&
                  painted_color_json(*real.find("armor_ubese_helmet"), "empire") == "{}");
            std::vector<ItemInstance> kit{{1, 1, "armor_katarn_chest", ItemGrade::standard, "", painted_color_json(*chest, "empire")},
                                          {2, 1, "armor_katarn_chest"}};
            CHECK(item_paint(kit[0]) == "empire" && item_paint(kit[1]).empty());
            CharacterLoadout pl;
            pl.gear_ids["chest"] = 1;
            CHECK(resolve_worn_models(real, pl, kit, Species::human, Gender::male) ==
                  "armor/katarn/chest_m.gltf?index_color_1=1&index_color_2=5&index_texture_1=0");
            pl.gear_ids["chest"] = 2;
            CHECK(resolve_worn_models(real, pl, kit, Species::human, Gender::male) ==
                  "armor/katarn/chest_m.gltf?index_color_1=0&index_color_2=4&index_texture_1=0");
            const std::vector<ItemInstance> arms{{1, 1, "armor_katarn_biceps", ItemGrade::standard, "", R"({"paint":"rebellion"})"}};
            CharacterLoadout al;
            al.gear_ids["biceps"] = 1;
            CHECK(resolve_worn_models(real, al, arms, Species::human, Gender::female) ==
                  "armor/katarn/bicep_l_f.gltf?index_color_1=0&index_color_2=6&index_texture_1=1,"
                  "armor/katarn/bicep_r_f.gltf?index_color_1=0&index_color_2=6&index_texture_1=1");
        }

        // A species weapon works only in its species' hands.
        ItemCatalog sp_cat;
        {
            std::ofstream out(ini_path);
            out << "[item]\nId = bow\nKind = weapon\nArchetype = rifle_auto\nSpecies = wookiee\n";
        }
        sp_cat.load(ini_path);
        std::filesystem::remove(ini_path);
        CHECK(sp_cat.find("bow")->species == std::vector<Species>{Species::wookiee});
        const std::vector<ItemInstance> bow_inv{{1, 1, "bow", ItemGrade::standard, "", "{}"}};
        CharacterLoadout bow_lo;
        bow_lo.primary_id = 1;
        CHECK(sp_cat.build_loadout(bow_lo, bow_inv, Species::wookiee).weapons[primary_slot]->name == "bow");
        CHECK(sp_cat.build_loadout(bow_lo, bow_inv, Species::human).weapons[primary_slot]->name != "bow");

        // Sidearms (docs/WEAPONS.md): the scatter pistol is a pistol-sized
        // spread on heat; one-handed melee is carried as a sidearm.
        const ItemDef* sp = real.find("wp_pistol_scatter");
        CHECK(sp && sp->slot == "sidearm" && sp->anim_set == "pistol");
        if (sp) {
            const WeaponDef w = real.resolve_weapon(*sp);
            CHECK(w.shots_per_salvo > 1 && w.rounds_per_clip == 0 && w.heat_per_shot > 0);
            CHECK(w.archetype == "scatter_pistol" && w.ammo_class == AmmoClass::standard);
        }
        for (const char* id : {"wp_melee_vibroblade", "wp_melee_sword", "wp_melee_vibroknuckler"}) {
            const ItemDef* m = real.find(id);
            CHECK(m && m->archetype == WeaponArchetype::melee && m->slot == "sidearm");
        }

        // Two-handed melee is a primary, and outweighs one-handed: more
        // damage a second, more reach, more stagger.
        const ItemDef* lance = real.find("wp_melee_vibrolance");
        CHECK(lance && lance->archetype == WeaponArchetype::melee_2h && lance->slot == "primary");
        CHECK(is_melee_archetype(WeaponArchetype::melee_2h) && !is_melee_archetype(WeaponArchetype::pistol));
        ItemDef one, two;
        one.archetype = WeaponArchetype::melee;
        two.archetype = WeaponArchetype::melee_2h;
        const WeaponDef w1 = real.resolve_weapon(one), w2 = real.resolve_weapon(two);
        CHECK(w2.damage / w2.shot_delay > 1.15 * (w1.damage / w1.shot_delay));
        CHECK(w2.melee_range > w1.melee_range && w2.melee_stagger > w1.melee_stagger);
        CHECK(w2.projectile == Projectile::melee && lance && lance->anim_set == "melee_2h");
    }

    return swm::test::finish("items_test");
}
