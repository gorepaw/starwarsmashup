// Professions: the shipped roster and its level curve, trainer perks and
// milestones under the cap (docs/PERKS.md), perks applied to a loadout, armor
// classes, and experience earned through a server (weapon kills, hits taken,
// quests) and saved with the character.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <vector>

#include "swm/core/client.h"
#include "swm/core/combat.h"
#include "swm/core/net/protocol.h"
#include "swm/core/persistence.h"
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
    CHECK(book.all().size() == 29);  // Image Designer is gone (an NPC one day), and Combat Medic (into Doctor)
    CHECK(book.find("image_designer") == nullptr && book.find("combat_medic") == nullptr);
    const auto in_wave = [&](const char* wave) {
        return std::count_if(book.all().begin(), book.all().end(), [&](const ProfessionDef& d) { return d.wave == wave; });
    };
    CHECK(in_wave("7A") == 17);
    CHECK(in_wave("7B") == 8);
    CHECK(in_wave("7C") == 4);
    int perks = 0, waiting = 0, triggered = 0;
    for (const ProfessionDef& def : book.all()) {
        CHECK(def.available);  // all of them are in play since Stage 12
        CHECK(!def.name.empty() && !def.description.empty());
        const bool combat = def.category == ProfessionCategory::weapon || def.category == ProfessionCategory::armor;
        // Every profession teaches perks, two at each tier (5, or 1 for a
        // licence, then 25/45/65/85): milestones are gone.
        if (combat) CHECK(def.chooses() && def.first_perk_level == 5 && def.pool == PerkPool::combat);
        CHECK(def.chooses());
        if (def.chooses()) {
            CHECK(def.perks.size() == 10);
            const auto tiers = def.perk_tiers();
            for (size_t i = 0; i < def.perks.size(); ++i) {
                const ProfessionPerkDef& perk = def.perks[i];
                CHECK(perk.level == tiers[i / 2] && perk.choice == (i % 2 == 0 ? 'A' : 'B'));
                CHECK(!perk.name.empty() && !perk.description.empty());
                if (!combat) {
                    // An advanced, gathering or crafting profession's: an
                    // ability, a flag, a modifier or a bonus.
                    CHECK(!perk.abilities.empty() || !perk.grants.empty() || !perk.perk.empty() ||
                          !perk.bonuses.empty());
                    continue;
                }
                // One that works changes something, always or on its trigger;
                // one that waits (for a new mechanic) changes nothing yet.
                if (perk.needs.empty()) {
                    CHECK(!perk.perk.empty() || perk.trigger.has_value());
                } else {
                    CHECK(perk.perk.empty() && !perk.trigger && perk.needs == "mechanic");
                }
                triggered += perk.trigger ? 1 : 0;
                ++perks;
                waiting += perk.needs.empty() ? 0 : 1;
            }
        }
    }
    CHECK(perks == 90);
    CHECK(triggered == 31);
    // The advanced trees so far, and their pools.
    const ProfessionDef* sl = book.find("squad_leader");
    const ProfessionDef* doc = book.find("doctor");
    const ProfessionDef* ent = book.find("entertainer");
    CHECK(sl && sl->chooses() && sl->first_perk_level == 1 && sl->pool == PerkPool::combat);
    CHECK(doc && doc->chooses() && doc->first_perk_level == 1 && doc->pool == PerkPool::combat);
    CHECK(ent && ent->chooses() && ent->first_perk_level == 1 && ent->pool == PerkPool::noncombat);
    CHECK(book.find("surveying") && book.find("surveying")->pool == PerkPool::noncombat);
    CHECK(book.find("merchant") && book.find("merchant")->pool == PerkPool::noncombat);
    CHECK(book.find("ranger") && book.find("ranger")->pool == PerkPool::combat);
    CHECK(waiting == 0);  // every perk works (step 3 built the last of the mechanics)
    for (const auto& a : {"pistol", "carbine_burst", "scatter", "rifle_auto", "sniper", "heavy_launcher", "melee"}) {
        CHECK(book.for_archetype(a) != nullptr);
    }
    // Tacticals level no profession (their perks come with the advanced trees).
    for (const auto& a : {"grenade", "placed", "utility"}) {
        CHECK(book.for_archetype(a) == nullptr);
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

}

// Trainers: perks to choose, milestones in order, ten in all, giving up free
// and learning again free (docs/PERKS.md).
void test_training() {
    ProfessionBook book;
    book.load(repo_root() / "game" / "professions" / "professions.ini");
    std::vector<ProfessionProgress> me{{"rifles", book.xp_for_level(30)}, {"pistols", book.xp_for_level(4)}};

    // A perk needs its level, and credits.
    CHECK(!learn_perk(book, me, "pistols_quick_draw", 10'000).ok);       // Pistols 4: not yet
    CHECK(!learn_perk(book, me, "rifles_long_barrel", 100).ok);          // 500 Cr
    CHECK(!learn_perk(book, me, "nothing", 10'000).ok);
    TrainResult r = learn_perk(book, me, "rifles_long_barrel", 1000);
    CHECK(r.ok && r.cost == perk_cost(5) && r.cost == 500);
    CHECK(!learn_perk(book, me, "rifles_long_barrel", 1000).ok);         // known already
    // A or B: the other at its level must be given up first.
    CHECK(!learn_perk(book, me, "rifles_high_velocity", 1000).ok);
    r = forget(book, me, "rifles_long_barrel");
    CHECK(r.ok && r.cost == 0);
    CHECK(learn_perk(book, me, "rifles_high_velocity", 1000).ok);
    CHECK(forget(book, me, "rifles_high_velocity").ok);
    // Paid for once: free again.
    r = learn_perk(book, me, "rifles_long_barrel", 0);
    CHECK(r.ok && r.cost == 0);
    CHECK(!forget(book, me, "rifles_high_velocity").ok);  // not known
    CHECK(learn_perk(book, me, "rifles_braced", 10'000).ok);  // level 25, waits for triggers: learnable all the same
    CHECK(!learn_perk(book, me, "rifles_spotter", 10'000).ok);  // level 45
    CHECK(perks_learned(book, me, PerkPool::combat) == 2);

    // A licence: the level-1 perk, free; fighting needs none.
    me.push_back({"droid_operations", book.xp_for_level(60)});
    CHECK(!licensed(book, me, "droid_operations"));
    CHECK(licensed(book, me, "rifles") && licensed(book, me, "heavy_armor"));
    CHECK(learn_perk(book, me, "do_perk_overcharge", 10'000).ok);  // learnable, but no licence yet
    CHECK(!licensed(book, me, "droid_operations"));
    r = learn_perk(book, me, "do_perk_repair", 0);
    CHECK(r.ok && r.cost == 0);
    CHECK(licensed(book, me, "droid_operations"));
    CHECK(learn_perk(book, me, "do_perk_frame", 2500).ok);
    CHECK(!learn_perk(book, me, "do_perk_sensor", 10'000).ok);  // Droid Operations 60: not 65
    CHECK(!forget(book, me, "droid_operations").ok);            // a perk is given up by name
    CHECK(perks_learned(book, me, PerkPool::combat) == 5);

    // Ten at most in a pool: Rifles, Droid Operations and Pilot all fight.
    me.push_back({"pilot", book.xp_for_level(100)});
    for (const char* perk : {"pl_perk_hotshot", "pl_perk_evasive", "pl_perk_reactor", "pl_perk_flare", "pl_perk_ace"}) {
        CHECK(learn_perk(book, me, perk, 100'000).ok);
    }
    CHECK(perks_learned(book, me, PerkPool::combat) == 10);
    r = learn_perk(book, me, "rifles_spotter", 100'000);
    CHECK(!r.ok);
    me[0].xp = book.xp_for_level(100);
    r = learn_perk(book, me, "rifles_spotter", 100'000);
    CHECK(!r.ok && r.message.find("give one up") != std::string::npos);
    CHECK(!learn_perk(book, me, "do_perk_reboot", 100'000).ok);  // (60 still)
    // ...but the non-combat ten are their own: an Entertainer's licence still fits.
    me.push_back({"entertainer", book.xp_for_level(30)});
    CHECK(!licensed(book, me, "entertainer"));
    r = learn_perk(book, me, "ent_perk_dancer", 0);
    CHECK(r.ok && r.cost == 0);  // a level-1 perk is free
    CHECK(licensed(book, me, "entertainer") && has_grant(book, me, "entertainer", "dancer"));
    CHECK(!has_grant(book, me, "entertainer", "musician"));
    CHECK(perks_learned(book, me, PerkPool::noncombat) == 1 && perks_learned(book, me, PerkPool::combat) == 10);
    CHECK(forget(book, me, "pl_perk_ace").ok);  // (a perk is given up by name)
    CHECK(learn_perk(book, me, "rifles_spotter", 100'000).ok);
    CHECK(perks_learned(book, me, PerkPool::combat) == 10);
    // Giving up the licence perk takes the licence.
    CHECK(forget(book, me, "do_perk_repair").ok);
    CHECK(!licensed(book, me, "droid_operations") && !forget(book, me, "do_perk_repair").ok);

    // Gathering and crafting teach perks from level 1: either level-1 perk is
    // the licence, and their Bonus lines add up by name.
    {
        const ProfessionDef* survey = book.find("surveying");
        const ProfessionDef* smith = book.find("weaponsmith");
        CHECK(survey && survey->chooses() && survey->first_perk_level == 1 && survey->pool == PerkPool::noncombat);
        CHECK(smith && smith->chooses() && smith->first_perk_level == 1 && smith->pool == PerkPool::noncombat);
        // (Those not yet given their own perks: a Bonus each.)
        for (const char* id : {"salvaging", "weaponsmith", "armorsmith", "tailor", "chef", "architect"}) {
            const ProfessionDef* def = book.find(id);
            CHECK(def && def->perks.size() == 10);
            for (const ProfessionPerkDef& perk : def->perks) CHECK(perk.bonuses.size() == 1);
        }
        std::vector<ProfessionProgress> gatherer{{"surveying", book.xp_for_level(90)}};
        CHECK(!licensed(book, gatherer, "surveying"));
        CHECK(learn_perk(book, gatherer, "survey_rich_veins", 100'000).ok);  // learnable, but no licence
        CHECK(!licensed(book, gatherer, "surveying"));
        const TrainResult lic = learn_perk(book, gatherer, "survey_prospectors_scanner", 0);
        CHECK(lic.ok && lic.cost == 0 && licensed(book, gatherer, "surveying"));
        CHECK(learn_perk(book, gatherer, "survey_twin_seams", 100'000).ok);
        CHECK(std::abs(perk_bonus(book, gatherer, "surveying", "yield") - 0.15) < 1e-9);
        CHECK(std::abs(perk_bonus(book, gatherer, "surveying", "double_haul") - 0.15) < 1e-9);
        CHECK(has_grant(book, gatherer, "surveying", "rich_veins"));
        CHECK(perk_bonus(book, gatherer, "foraging", "yield") == 0.0);
        CHECK(perks_learned(book, gatherer, PerkPool::noncombat) == 3 && perks_learned(book, gatherer, PerkPool::combat) == 0);
        const std::vector<std::string> ids{"wsmith_weapon_experimentation", "wsmith_master_experimenter"};
        CHECK(std::abs(perk_bonus(book, std::span<const std::string>(ids), "grade") - 0.3) < 1e-9);
        // Doctor's first tier is its licence too.
        const ProfessionDef* doc = book.find("doctor");
        CHECK(doc && doc->first_perk_level == 1);
        std::vector<ProfessionProgress> medic{{"doctor", book.xp_for_level(10)}};
        CHECK(!licensed(book, medic, "doctor"));
        CHECK(learn_perk(book, medic, "doc_perk_field_dressing", 0).ok && licensed(book, medic, "doctor"));
    }
    // An advanced profession of perks: a level-1 perk is its licence (Squad
    // Leader, Doctor); its later tiers wait for the level.
    std::vector<ProfessionProgress> sl{{"squad_leader", book.xp_for_level(30)}, {"doctor", 0}};
    CHECK(!licensed(book, sl, "squad_leader") && !licensed(book, sl, "doctor"));
    CHECK(!learn_perk(book, sl, "doc_perk_cleanse", 10'000).ok);  // Doctor 1: not yet 25
    CHECK(learn_perk(book, sl, "sl_perk_steady_rally", 0).ok);
    CHECK(licensed(book, sl, "squad_leader"));
    CHECK(!learn_perk(book, sl, "sl_perk_rally", 0).ok);  // the other kind of rally
    CHECK(learn_perk(book, sl, "sl_perk_shields_up", 500).ok);
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
    const auto known = [](const char* id, int64_t xp, std::vector<std::string> perks) {
        ProfessionProgress p{id, xp};
        p.perks = std::move(perks);
        return p;
    };
    // Pistols 25 with Quick Draw and Snap Shot (which waits for triggers),
    // Light Armor 5 with Insulated Weave, Heavy Armor's Blast Padding (not
    // worn: nothing from it), and no slots: what's learned is what applies.
    const double swap = pistol.swap_time;
    const std::vector<ProfessionProgress> progress{
        known("pistols", book.xp_for_level(25), {"pistols_quick_draw", "pistols_snap_shot"}),
        known("light_armor", book.xp_for_level(5), {"light_insulated_weave"}),
        known("heavy_armor", book.xp_for_level(100), {"heavy_blast_padding"})};
    apply_profession_perks(lo, book, progress);
    CHECK(near(lo.weapons[1]->swap_time, swap * 0.6));
    CHECK(near(lo.weapons[1]->yaw_spread, 1.0));        // no ranks, and Snap Shot does nothing yet
    CHECK(near(lo.weapons[1]->reload_time, 2.0));       // no certification
    CHECK(near(lo.weapons[0]->swap_time, swap));        // the rifle isn't a pistol
    CHECK(lo.gear.size() == 1 && lo.gear[0].slot == "specialization");
    CHECK(near(stat_after(lo, Stat::mitigation_electro), 0.05));
    CHECK(near(stat_after(lo, Stat::mitigation_explosive), 0.0));
    // A perk for some of its profession's weapons: Tight Choke tightens
    // scatter guns, not burst carbines.
    {
        Loadout guns;
        WeaponDef scatter;
        scatter.archetype = "scatter";
        scatter.yaw_spread = 1.0;
        WeaponDef burst;
        burst.archetype = "carbine_burst";
        burst.yaw_spread = 1.0;
        guns.weapons = {scatter, burst, std::nullopt};
        const std::vector<ProfessionProgress> carbines{known("carbines", book.xp_for_level(5), {"carbines_tight_choke"})};
        apply_profession_perks(guns, book, carbines);
        CHECK(near(guns.weapons[0]->yaw_spread, 0.85) && near(guns.weapons[1]->yaw_spread, 1.0));
    }
    // Nothing learned, nothing applied; a level lost (it can't be, but a
    // perk above the level) doesn't apply.
    Loadout plain;
    apply_profession_perks(plain, book, {});
    CHECK(plain.gear.empty());
    Loadout under;
    under.armor_class = "light";
    const std::vector<ProfessionProgress> low{known("light_armor", book.xp_for_level(4), {"light_insulated_weave"})};
    apply_profession_perks(under, book, low);
    CHECK(under.gear.empty());

    // Routed experience: the weapon's and the armor's, slotted or not.
    CHECK((routed_professions(book, lo) == std::vector<std::string>{"rifles", "light_armor"}));
    // The combat level: the higher of the two.
    std::vector<ProfessionProgress> levels{{"rifles", book.xp_for_level(12)}, {"light_armor", book.xp_for_level(30)}};
    CHECK(combat_level(book, lo, levels) == 30);
    levels = {{"rifles", book.xp_for_level(12)}};
    CHECK(combat_level(book, lo, levels) == 12);
    CHECK(combat_level(book, lo, {}) == 1);

    // A marked item asks for its own profession's level: a rifle Rifles', light
    // armor Light Armor's; a Deflector Shield the character's highest.
    {
        ItemDef rifle_item;
        rifle_item.kind = ItemKind::weapon;
        rifle_item.archetype = WeaponArchetype::rifle_auto;
        ItemDef light_item;
        light_item.kind = ItemKind::armor;
        light_item.armor_class = "light";
        ItemDef shield_item;
        shield_item.kind = ItemKind::deflector_shield;
        const std::vector<ProfessionProgress> mine{{"rifles", book.xp_for_level(42)},
                                                   {"light_armor", book.xp_for_level(9)},
                                                   {"pistols", book.xp_for_level(61)}};
        CHECK(mark_profession(book, rifle_item) && mark_profession(book, rifle_item)->id == "rifles");
        CHECK(mark_profession(book, shield_item) == nullptr);
        CHECK(level_for_item(book, rifle_item, mine) == 42 && level_for_item(book, shield_item, mine) == 61);
        CHECK(can_use_mark(book, rifle_item, 4, mine) && !can_use_mark(book, rifle_item, 5, mine));
        CHECK(can_use_mark(book, light_item, 0, mine) && !can_use_mark(book, light_item, 1, mine));
        CHECK(can_use_mark(book, shield_item, 6, mine) && !can_use_mark(book, shield_item, 7, mine));
        CHECK(can_use_mark(book, shield_item, 0, {}) && !can_use_mark(book, shield_item, 1, {}));
    }

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

// Triggered perks (docs/PERKS.md, Triggers) in the shared simulation: what a
// loadout carries, stances, a roll and an overheat starting timers, and
// active perks' statistics; and on the wire.
void test_triggers() {
    ProfessionBook book;
    book.load(repo_root() / "game" / "professions" / "professions.ini");
    const auto known = [](const char* id, int64_t xp, std::vector<std::string> perks) {
        ProfessionProgress p{id, xp};
        p.perks = std::move(perks);
        return p;
    };
    const int64_t master = book.xp_for_level(100);
    WeaponDef rifle;
    rifle.archetype = "rifle_auto";
    rifle.yaw_spread = rifle.pitch_spread = 1.0;
    rifle.spread_per_shot = 0.5;
    rifle.heat_per_shot = 0.1;
    rifle.rounds_per_clip = 0;
    WeaponDef pistol;
    pistol.archetype = "pistol";
    pistol.shot_delay = 0.3;
    pistol.yaw_spread = pistol.pitch_spread = 0.5;
    Loadout lo;
    lo.weapons = {rifle, pistol, std::nullopt};
    lo.armor_class = "light";
    const std::vector<ProfessionProgress> progress{
        known("rifles", master, {"rifles_braced", "rifles_rhythm", "rifles_overwatch", "rifles_one_shot"}),
        known("pistols", master, {"pistols_snap_shot", "pistols_fan_the_hammer", "pistols_execution", "pistols_deadeye"}),
        known("light_armor", master, {"light_pathfinder"}),
        known("heavy_armor", master, {"heavy_anchor"}),      // not worn
        known("carbines", master, {"carbines_overclock"})};  // none carried
    apply_profession_perks(lo, book, progress);
    const auto index = [&](std::string_view id) -> int {
        for (size_t i = 0; i < lo.triggered.size(); ++i) {
            if (lo.triggered[i].id == id) return int(i);
        }
        return -1;
    };
    // One Shot is a sniper's (an automatic rifle is carried); Anchor heavy
    // armor's; Overclock a carbine's.
    CHECK(lo.triggered.size() == 8);
    CHECK(index("rifles_one_shot") < 0 && index("heavy_anchor") < 0 && index("carbines_overclock") < 0);
    const int braced = index("rifles_braced"), overwatch = index("rifles_overwatch"), snap = index("pistols_snap_shot");
    const int fan = index("pistols_fan_the_hammer"), pathfinder = index("light_pathfinder");
    CHECK(braced >= 0 && overwatch >= 0 && snap >= 0 && fan >= 0 && pathfinder >= 0);
    CHECK(lo.triggered[size_t(braced)].slots == 1 && lo.triggered[size_t(snap)].slots == 2);
    CHECK(lo.triggered[size_t(pathfinder)].slots == 0);  // armor: every weapon
    // Triggered perks' modifiers aren't always on: nothing in the gear for Pathfinder.
    CHECK(stats_for(lo).move.run_speed == CharacterStats{}.move.run_speed);

    // Stances: Braced crouched (the rifle only), Overwatch after 2 s crouched
    // and still, Snap Shot after a second's pause.
    MoveState st;
    CHECK(near(perk_handling(lo, st, 0).spread, 1.0));
    st.crouched = true;
    CHECK(near(perk_handling(lo, st, 0).spread, 0.78) && near(perk_handling(lo, st, 1).spread, 1.0));
    CHECK(near(perk_handling(lo, st, 0).heat, 1.0));
    st.still = 2.5f;
    CHECK(near(perk_handling(lo, st, 0).heat, 0.7) && near(perk_handling(lo, st, 0).bloom, 0.0));
    st = {};
    CHECK(near(perk_handling(lo, st, 1).spread, 1.0));
    st.combat.idle = 1.5f;
    CHECK(near(perk_handling(lo, st, 1).spread, 0.0));

    Heightfield ground{101, 2.0, -100, -100, std::vector<float>(101 * 101, 0.0f)};
    World world{ground};
    const CharacterStats base;
    uint32_t seq = 0;
    const auto tick = [&](MoveState& s, MoveInput in) {
        in.sequence = ++seq;
        return simulate_character(s, in, true, world, lo, base, 1.0 / 30.0);
    };
    // Crouched and still: the simulation counts it.
    MoveState sim;
    reset_loadout(sim, lo, base);
    MoveInput crouch;
    crouch.buttons = MoveInput::crouch;
    tick(sim, crouch);
    for (int i = 0; i < 70; ++i) tick(sim, MoveInput{});
    CHECK(sim.crouched && sim.still > 2.0f && perk_active(lo, sim, size_t(overwatch)));
    // A paused pistol fires its first shot true; the next has its spread again.
    reset_loadout(sim, lo, base);
    MoveInput fire;
    fire.buttons = MoveInput::fire;
    fire.weapon = 1;
    MoveInput hold_pistol;
    hold_pistol.weapon = 1;
    for (int i = 0; i < 45; ++i) tick(sim, hold_pistol);
    CHECK(sim.combat.weapon == 1 && sim.combat.idle >= 1.0f);
    const auto first = tick(sim, fire);
    CHECK(first.has_value() && first->directions.size() == 1);
    CHECK(first && near(first->directions[0].x, aim_direction(0, 0).x, 1e-9) &&
          near(first->directions[0].y, aim_direction(0, 0).y, 1e-9));
    CHECK(sim.combat.idle == 0.0f && !perk_active(lo, sim, size_t(snap)));

    // A roll starts Fan the Hammer and Pathfinder: faster shots, faster feet.
    reset_loadout(sim, lo, base);
    MoveInput run;
    run.forward = 127;
    for (int i = 0; i < 20; ++i) tick(sim, run);
    MoveInput roll = run;
    roll.buttons = MoveInput::crouch;
    tick(sim, roll);
    CHECK(sim.rolling());
    CHECK(sim.perk_time[size_t(fan)] > 3.5f && sim.perk_time[size_t(pathfinder)] > 2.5f);
    CHECK(near(perk_handling(lo, sim, 1).speed, 0.77) && near(perk_handling(lo, sim, 0).speed, 1.0));
    CHECK(near(perk_stats(base, lo, sim).move.run_speed, base.move.run_speed * 1.2));
    for (int i = 0; i < 120; ++i) tick(sim, MoveInput{});
    CHECK(!perk_active(lo, sim, size_t(fan)) && near(perk_stats(base, lo, sim).move.run_speed, base.move.run_speed));

    // Overclock: an overheat vents at once, then not again for 20 s.
    {
        WeaponDef carbine;
        carbine.archetype = "carbine_burst";
        carbine.rounds_per_clip = 0;
        carbine.heat_per_shot = 0.6;
        carbine.heat_recover_rate = 0;
        carbine.heat_threshold = 0.2;
        carbine.shot_delay = 0.1;
        Loadout hot;
        hot.weapons = {carbine, std::nullopt, std::nullopt};
        apply_profession_perks(hot, book, std::vector<ProfessionProgress>{known("carbines", master, {"carbines_overclock"})});
        CHECK(hot.triggered.size() == 1 && hot.triggered[0].timer_is_cooldown());
        MoveState h;
        reset_loadout(h, hot, base);
        MoveInput shoot;
        shoot.buttons = MoveInput::fire;
        for (int i = 0; i < 9; ++i) {
            shoot.sequence = ++seq;
            simulate_weapons(h, shoot, hot, 1.0 / 30.0);
        }
        CHECK(!h.combat.weapons[0].overheated && h.perk_time[0] > 19.0f);  // vented on the second shot
        for (int i = 0; i < 9; ++i) {
            shoot.sequence = ++seq;
            simulate_weapons(h, shoot, hot, 1.0 / 30.0);
        }
        CHECK(h.combat.weapons[0].overheated);  // cooling down: no vent
    }

    // On the wire: the timers with the character's state, the perks with its loadouts.
    {
        net::Snapshot snap_msg;
        net::EntityState self{7, sim};
        self.state.perk_time[2] = 1.25f;
        self.state.still = 0.5f;
        self.state.combat.idle = 3.0f;
        snap_msg.entities.push_back(self);
        const auto decoded = std::get<net::Snapshot>(net::decode(net::encode(snap_msg)));
        CHECK(decoded.entities.size() == 1 && decoded.entities[0].state == self.state);
        net::InventorySync sync;
        sync.resolved_loadouts = {lo};
        const auto inv = std::get<net::InventorySync>(net::decode(net::encode(sync)));
        CHECK(inv.resolved_loadouts.size() == 1 && inv.resolved_loadouts[0].triggered.size() == lo.triggered.size());
        if (inv.resolved_loadouts.size() == 1 && inv.resolved_loadouts[0].triggered.size() == lo.triggered.size()) {
            for (size_t i = 0; i < lo.triggered.size(); ++i) {
                const TriggeredPerk& a = lo.triggered[i];
                const TriggeredPerk& b = inv.resolved_loadouts[0].triggered[i];
                CHECK(a.id == b.id && a.trigger == b.trigger && a.slots == b.slots && a.weapons == b.weapons &&
                      near(a.duration, b.duration, 1e-6) && a.stats.size() == b.stats.size());
            }
        }
    }
}

// Triggered perks on a server: a while unhurt (held), a shield broken, a
// killing blow, and kills in a chain with a carbine.
void test_server_triggers() {
    const auto root = repo_root();
    const auto db_path = std::filesystem::temp_directory_path() / "swm_perk_trigger_test.sqlite";
    std::filesystem::remove(db_path);
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    ProfessionBook book;
    book.load(root / "game" / "professions" / "professions.ini");
    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    SqliteCharacterStore store(db_path);
    // A veteran: unarmored (the starter clothes) with Untouchable and Ghost
    // Step, a carbine with Chain Breach, and Second Chance (medium armor's:
    // not worn, so it won't save them).
    const CharacterRecord vet = store.create("Veteran", {0, 0, 0}, 0);
    ensure_starter_kit(store, vet.id);
    const int64_t master = book.xp_for_level(100);
    const std::vector<ProfessionProgress> levels{{"unarmored", master}, {"carbines", master}, {"medium_armor", master}};
    store.save_professions(vet.id, levels);
    store.set_extra(vet.id, "perks:unarmored", "unarmored_untouchable,unarmored_ghost_step");
    store.set_extra(vet.id, "perks:carbines", "carbines_chain_breach");
    store.set_extra(vet.id, "perks:medium_armor", "medium_second_chance");
    const ItemInstance carbine = store.grant_item({0, vet.id, "wp_carbine_dh17", ItemGrade::standard, "", "{}"});
    std::vector<CharacterLoadout> los = store.list_loadouts(vet.id);
    CHECK(!los.empty());
    if (los.empty()) return;
    los[0].primary_id = carbine.id;
    store.save_loadout(vet.id, los[0]);

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
    server.set_looks({"object/mobile/shared_womp_rat.iff"});
    GameServer::NpcCombatSpec frail;
    frail.actor_kind = 2;
    frail.passive = true;
    frail.max_health = 1;
    const uint32_t rat = server.add_combat_npc("Womp Rat", {30, 0, 30}, 0, 0, frail);
    GameClient client(cli_link, world, ClientConfig{.server = server_ep, .name = "Veteran"});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            client.update(now);
        }
    };
    run(0.6);
    CHECK(client.status() == GameClient::Status::connected);
    const uint32_t me = client.entity_id();
    const auto& lo = client.custom_loadouts();
    CHECK(!lo.empty() && lo[0].triggered.size() == 3);  // Second Chance isn't this armor's
    const auto index = [&](std::string_view id) -> int {
        if (lo.empty()) return -1;
        for (size_t i = 0; i < lo[0].triggered.size(); ++i) {
            if (lo[0].triggered[i].id == id) return int(i);
        }
        return -1;
    };
    const int untouchable = index("unarmored_untouchable"), ghost = index("unarmored_ghost_step");
    const int chain = index("carbines_chain_breach");
    CHECK(untouchable >= 0 && ghost >= 0 && chain >= 0);
    if (untouchable < 0 || ghost < 0 || chain < 0) return;
    const auto timer = [&](int i) {
        const MoveState* s = server.state_of(me);
        return s ? s->perk_time[size_t(i)] : -1.0f;
    };

    // Untouchable: four seconds unhurt, and it holds (on the client too) until a hit.
    run(4.5);
    CHECK(timer(untouchable) > 0);
    CHECK(client.predicted().perk_time[size_t(untouchable)] > 0);
    const double shield = server.shield_of(me);
    CHECK(shield > 0);
    server.npc_melee(rat, me, 10, DamageType::laser);
    run(0.05);
    CHECK(timer(untouchable) == 0);
    CHECK(timer(ghost) == 0);  // the shield held
    // A shield broken: Ghost Step for 2 s.
    server.npc_melee(rat, me, shield + 20, DamageType::laser);
    run(0.05);
    CHECK(server.shield_of(me) == 0 && timer(ghost) > 1.8f);
    // A killing blow: Second Chance isn't worn, so it doesn't save them.
    // (Below, after the chain.)

    // Chain Breach: the second of two kills within 5 s restores 15% of the shield.
    const double capacity = shield;  // (it was full)
    CHECK(capacity > 0);
    const uint32_t first = server.add_combat_npc("Womp Rat", {0, 0, -6}, 0, 0, frail);
    const uint32_t second = server.add_combat_npc("Womp Rat", {0, 0, -9}, 0, 0, frail);
    GameClient::Controls aim;
    aim.yaw = 0;  // facing -Z
    aim.pitch = -0.05;
    aim.fire = true;
    double shield_after_first = -1;
    const auto npc_alive = [&](uint32_t id) { const auto info = server.npc_info(id); return info && info->life == Life::alive; };
    for (int i = 0; i < 90 && npc_alive(second); ++i) {
        client.set_input(aim);
        run(1.0 / 30.0);
        if (shield_after_first < 0 && !npc_alive(first)) {
            shield_after_first = server.shield_of(me);
        }
    }
    client.set_input({});
    CHECK(!npc_alive(first) && !npc_alive(second));
    CHECK(shield_after_first >= 0 && near(server.shield_of(me), shield_after_first + 0.15 * capacity, 1.0));

    // A killing blow without Second Chance worn: dead.
    server.npc_melee(rat, me, 5000, DamageType::laser);
    run(0.05);
    CHECK(!server.alive(me));
    client.disconnect();
    run(0.1);

    // A guard in padded (medium) armor: Adaptive Plating and Second Chance.
    const CharacterRecord guard = store.create("Guard", {0, 0, 0}, 0);
    ensure_starter_kit(store, guard.id);
    const std::vector<ProfessionProgress> guard_levels{{"medium_armor", master}};
    store.save_professions(guard.id, guard_levels);
    store.set_extra(guard.id, "perks:medium_armor", "medium_adaptive_plating,medium_second_chance");
    std::vector<CharacterLoadout> guard_los = store.list_loadouts(guard.id);
    CHECK(!guard_los.empty());
    if (guard_los.empty()) return;
    for (const char* piece : {"chest", "leggings", "boots", "helmet"}) {
        const ItemInstance item = store.grant_item({0, guard.id, std::string("armor_padded_") + piece, ItemGrade::standard, "", "{}"});
        guard_los[0].gear_ids[piece] = item.id;
    }
    store.save_loadout(guard.id, guard_los[0]);
    net::LoopbackLink guard_link{links, {3, 1}};
    GameClient guard_client(guard_link, world, ClientConfig{.server = server_ep, .name = "Guard"});
    const auto run_guard = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            guard_client.update(now);
        }
    };
    run_guard(0.6);
    CHECK(guard_client.status() == GameClient::Status::connected);
    const uint32_t g = guard_client.entity_id();
    CHECK(!guard_client.custom_loadouts().empty() && guard_client.custom_loadouts()[0].armor_class == "medium" &&
          guard_client.custom_loadouts()[0].triggered.size() == 2);
    const auto taken = [&](double raw) {
        const double before = server.health_of(g) + server.shield_of(g);
        server.npc_melee(rat, g, raw, DamageType::laser);
        return before - (server.health_of(g) + server.shield_of(g));
    };
    // Adaptive Plating: struck by lasers, 10% more resistance to them for 6 s.
    const double first_hit = taken(40);
    const double second_hit = taken(40);
    CHECK(first_hit > 0 && second_hit < first_hit * 0.95);
    // Second Chance: once a life, a killing blow leaves them at 1 health.
    server.npc_melee(rat, g, 5000, DamageType::laser);
    run_guard(0.05);
    CHECK(server.alive(g) && near(server.health_of(g), 1.0, 1e-9));
    run_guard(1.5);
    server.npc_melee(rat, g, 5000, DamageType::laser);
    run_guard(0.05);
    CHECK(!server.alive(g));
    guard_client.disconnect();
    run_guard(0.1);
}

// Step 3's mechanics (docs/PERKS.md) without a server: the data, the weapon
// fields, the wire, and the shared simulation's (a slide, a second roll,
// firing while rolling, a reload held up by sprinting, holstered cooling and
// reloading, a block's cost and parry window, ammunition carried).
void test_mechanics() {
    const auto root = repo_root();
    ProfessionBook book;
    book.load(root / "game" / "professions" / "professions.ini");
    const auto perk_of = [&](const char* profession, std::string_view id) -> const ProfessionPerkDef* {
        const ProfessionDef* def = book.find(profession);
        if (def == nullptr) return nullptr;
        for (const ProfessionPerkDef& p : def->perks) {
            if (p.id == id) return &p;
        }
        return nullptr;
    };
    const ProfessionPerkDef* piercing = perk_of("rifles", "rifles_piercing_round");
    CHECK(piercing != nullptr && piercing->archetypes == std::vector<std::string>{"sniper"} &&
          piercing->perk.weapons.size() == 1 && piercing->perk.weapons[0].field == WeaponModifier::pierce &&
          piercing->perk.weapons[0].multiply == 1);
    const ProfessionPerkDef* stun = perk_of("pistols", "pistols_stun_rounds");
    CHECK(stun != nullptr && stun->trigger && stun->trigger->chance == 0.15 && stun->trigger->target.size() == 2);
    const ProfessionPerkDef* spotter = perk_of("rifles", "rifles_spotter");
    CHECK(spotter != nullptr && spotter->trigger && spotter->trigger->aimed && spotter->trigger->mark == 4);

    // The weapon fields.
    WeaponDef w;
    w.explosion_inner = 2;
    w.explosion_outer = 5;
    w.push_inner = 1;
    w.push_outer = 4;
    w.explosion_damage = 100;
    w.hit_effect = HitEffect{"burning", "Burning", 1, 4, 10, DamageType::bio, {}};
    w.modifiers = {{Stat::run_speed, 0, 0.8}, {Stat::sprint_factor, -0.2, 1}, {Stat::mitigation_laser, 0.1, 1}};
    for (const WeaponModifier& m : std::vector<WeaponModifier>{
             {WeaponModifier::blast, 1.2}, {WeaponModifier::blast_damage, 0.9}, {WeaponModifier::effect, 1.5},
             {WeaponModifier::carry, 0.5}, {WeaponModifier::ricochet, 1}, {WeaponModifier::guard, 0.6667},
             {WeaponModifier::sprint_reload, 1}, {WeaponModifier::body, 0.9}}) {
        apply_weapon_modifier(w, m);
    }
    CHECK(near(w.explosion_outer, 6.0, 1e-9) && near(w.push_inner, 1.2, 1e-9) && near(w.explosion_damage, 90, 1e-9));
    CHECK(near(w.hit_effect->duration, 6, 1e-9));
    // Heavy Lifter: its movement penalties halved, nothing else.
    CHECK(near(w.modifiers[0].multiply, 0.9, 1e-9) && near(w.modifiers[1].add, -0.1, 1e-9) &&
          near(w.modifiers[2].add, 0.1, 1e-9));
    CHECK(w.ricochets == 1 && w.reload_sprinting && near(w.block_leak, 0.3, 1e-3));
    // Headhunter's body shots.
    CHECK(near(hit_damage(w, 100, false, 1.0), 90, 1e-9) && near(hit_damage(w, 100, true, 1.0), 200, 1e-9));

    // On the wire: the weapon's perk fields, the state's, the input's aim.
    {
        net::InventorySync sync;
        Loadout wired;
        WeaponDef sent;
        sent.pierces = 2;
        sent.ricochets = 1;
        sent.deflect = 10;
        sent.riposte = true;
        sent.unblockable = true;
        sent.parry_window = 1.6;
        sent.body_multiplier = 0.9;
        sent.holster_cooling = 2;
        wired.weapons[0] = sent;
        sync.resolved_loadouts = {wired};
        const auto got = std::get<net::InventorySync>(net::decode(net::encode(sync)));
        CHECK(got.resolved_loadouts.size() == 1 && got.resolved_loadouts[0].weapons[0].has_value());
        if (got.resolved_loadouts.size() == 1 && got.resolved_loadouts[0].weapons[0]) {
            const WeaponDef& g = *got.resolved_loadouts[0].weapons[0];
            CHECK(g.pierces == 2 && g.ricochets == 1 && g.deflect == 10 && g.riposte && g.unblockable && !g.fire_rolling &&
                  g.parry_window == 1.6 && g.body_multiplier == 0.9 && g.holster_cooling == 2);
        }
        net::InputBatch batch;
        MoveInput aimed;
        aimed.weapon = 1;
        aimed.aiming = true;
        batch.inputs = {aimed};
        const auto inputs = std::get<net::InputBatch>(net::decode(net::encode(batch)));
        CHECK(inputs.inputs.size() == 1 && inputs.inputs[0].weapon == 1 && inputs.inputs[0].aiming);
    }

    // The simulation.
    Heightfield ground{101, 2.0, -100, -100, std::vector<float>(101 * 101, 0.0f)};
    World world{ground};
    constexpr double dt = 1.0 / 30.0;
    WeaponDef pistol;
    pistol.archetype = "pistol";
    pistol.rounds_per_clip = 10;
    pistol.ammo = 4;
    pistol.reload_time = 1.0;
    pistol.shot_delay = 0.1;
    pistol.yaw_spread = pistol.pitch_spread = pistol.spread_per_shot = 0;
    pistol.swap_time = 0.1;
    WeaponDef rifle = pistol;
    rifle.heat_recover_rate = 0.5;
    rifle.holster_cooling = 2;
    Loadout lo;
    lo.weapons[primary_slot] = pistol;
    lo.weapons[sidearm_slot] = rifle;
    const CharacterStats stats;
    MoveState st;
    reset_loadout(st, lo, stats);
    uint32_t seq = 0;
    const auto step = [&](MoveInput in, int ticks, const CharacterStats& with) {
        int shots = 0;
        for (int i = 0; i < ticks; ++i) {
            in.sequence = ++seq;
            if (simulate_character(st, in, true, world, lo, with, dt)) ++shots;
        }
        return shots;
    };
    MoveInput run;
    run.forward = 127;
    const auto with = [&](uint8_t buttons, uint8_t weapon = 0) {
        MoveInput in = run;
        in.buttons = buttons;
        in.weapon = weapon;
        return in;
    };

    // Slide: crouching out of a sprint slides, low and fast, still shooting.
    CharacterStats slider = stats;
    slider.move.slide_time = 0.8;
    step(with(MoveInput::sprint), 10, slider);
    CHECK(st.sprinting);
    step(with(MoveInput::sprint | MoveInput::crouch), 1, slider);
    CHECK(st.slide > 0 && st.crouched && !st.rolling() && std::hypot(st.velocity.x, st.velocity.z) > 9.0);
    CHECK(step(with(MoveInput::sprint | MoveInput::crouch | MoveInput::fire), 1, slider) == 1);
    step(run, 30, slider);
    CHECK(st.slide == 0);
    // Without it, the same is a roll.
    step(with(MoveInput::sprint), 10, stats);
    step(with(MoveInput::sprint | MoveInput::crouch), 1, stats);
    CHECK(st.rolling() && st.slide == 0);
    step(run, 30, stats);

    // Double Roll: a roll can follow a roll, once, in its second half.
    CharacterStats roller = stats;
    roller.move.roll_chain = 1;
    st.energy = 100;
    step(with(MoveInput::crouch), 1, roller);
    CHECK(st.rolling() && st.rolls == 1);
    step(run, 12, roller);
    CHECK(st.rolling() && st.roll < 0.375f);
    step(with(MoveInput::crouch), 1, roller);
    CHECK(st.rolling() && st.rolls == 2 && st.roll > 0.7f);
    step(run, 12, roller);
    step(with(MoveInput::crouch), 1, roller);
    CHECK(st.rolls == 2 && st.roll < 0.375f);  // only the one
    step(run, 30, roller);
    CHECK(!st.rolling() && st.rolls == 0);
    st.energy = 100;
    step(with(MoveInput::crouch), 1, stats);
    step(run, 12, stats);
    step(with(MoveInput::crouch), 1, stats);
    CHECK(st.rolls == 1 && st.roll < 0.375f);  // without it: none
    step(run, 30, stats);

    // Roll Shot: only a weapon that may fires while rolling.
    st.energy = 100;
    step(with(MoveInput::crouch), 1, stats);
    CHECK(st.rolling() && step(with(MoveInput::fire), 5, stats) == 0);
    step(run, 30, stats);
    lo.weapons[primary_slot]->fire_rolling = true;
    st.energy = 100;
    step(with(MoveInput::crouch), 1, stats);
    CHECK(st.rolling() && step(with(MoveInput::fire), 5, stats) >= 1);
    step(run, 30, stats);
    lo.weapons[primary_slot]->fire_rolling = false;

    // Sprinting holds a reload where it is (Combat Reload: it carries on).
    st.energy = 100;
    step(with(MoveInput::sprint | MoveInput::reload), 1, stats);
    const double started = st.combat.reload;
    CHECK(started > 0 && st.sprinting);
    step(with(MoveInput::sprint), 10, stats);
    CHECK(st.sprinting && near(st.combat.reload, started, 1e-9));
    lo.weapons[primary_slot]->reload_sprinting = true;
    step(with(MoveInput::sprint), 10, stats);
    CHECK(st.combat.reload < started - 0.3);
    lo.weapons[primary_slot]->reload_sprinting = false;
    step(run, 40, stats);
    CHECK(st.combat.reload == 0 && st.combat.weapons[primary_slot].rounds == 10);

    // Cool Holster: holstered, it cools twice as fast.
    st.combat.weapons[sidearm_slot].heat = 0.8f;
    step(run, 6, stats);
    CHECK(near(st.combat.weapons[sidearm_slot].heat, 0.8 - 0.5 * 2 * 0.2, 1e-4));

    // Tactical Swap: holstered, it reloads itself; brought back first, it
    // finishes in the hand.
    lo.weapons[primary_slot]->holster_reload = true;
    step(with(MoveInput::fire), 6, stats);
    CHECK(st.combat.weapons[primary_slot].rounds < 10);
    step(with(0, sidearm_slot), 1, stats);
    CHECK(st.combat.weapon == sidearm_slot && near(st.combat.weapons[primary_slot].reload, 1.0, 1e-4));
    step(with(0, sidearm_slot), 33, stats);
    CHECK(st.combat.weapons[primary_slot].rounds == 10 && st.combat.weapons[primary_slot].reload == 0);
    step(run, 6, stats);
    step(with(MoveInput::fire), 3, stats);
    CHECK(st.combat.weapon == primary_slot && st.combat.weapons[primary_slot].rounds < 10);
    step(with(0, sidearm_slot), 15, stats);
    step(run, 1, stats);
    CHECK(st.combat.weapon == primary_slot && st.combat.reload > 0.4 && st.combat.reload < 0.6 &&
          st.combat.weapons[primary_slot].reload == 0);

    // Iron Guard and Riposte: a block's cost and parry window are the held
    // melee weapon's.
    WeaponDef staff;
    staff.projectile = Projectile::melee;
    staff.archetype = "melee";
    staff.block_cost = 0.5;
    staff.parry_window = 1.6;
    Loadout melee;
    melee.weapons[primary_slot] = staff;
    MoveState guard;
    reset_loadout(guard, melee, stats);
    MoveInput block;
    block.buttons = MoveInput::block;
    simulate_character(guard, block, true, world, melee, stats, dt);
    CHECK(guard.blocking && near(guard.parry_window, 0.4, 1e-5));
    const double rested = guard.energy;
    simulate_character(guard, block, true, world, melee, stats, dt);
    CHECK(near(rested - guard.energy, 8.0 * 0.5 * dt, 1e-6));

    // Ammo Webbing: a quarter more carried.
    CharacterStats webbed = stats;
    webbed.ammo_capacity = 1.25;
    MoveState full;
    reset_loadout(full, lo, webbed);
    CHECK(full.combat.weapons[primary_slot].reserve == 40);  // 4 clips of 10, a quarter more, less the one loaded
    reset_loadout(full, lo, stats);
    CHECK(full.combat.weapons[primary_slot].reserve == 30);
}

// Step 3's mechanics on a live server: bolts that go through bodies,
// ricochet off the ground and burst near enemies; hits from behind; one's
// own blast; a block that turns bolts aside; a combo's stagger and a
// knockback; a shoulder charge; an overcharged shield; a blast that refills
// it; Spotter's mark; Low Profile; Quiet Step.
void test_server_mechanics() {
    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1}, client_ep{2, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink cli_link{links, client_ep};
    ServerConfig config;
    config.pvp_enabled = false;
    config.spawn = {0, 0, 0};
    GameServer server(srv_link, world, config);
    server.set_looks({"object/mobile/shared_womp_rat.iff"});
    GameClient client(cli_link, world, ClientConfig{.server = server_ep, .name = "Tester"});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            client.update(now);
        }
    };
    run(0.6);
    CHECK(client.status() == GameClient::Status::connected);
    const uint32_t me = client.entity_id();
    const auto health = [&](uint32_t npc) {
        const auto info = server.npc_info(npc);
        return info ? info->health : -1.0;
    };
    GameServer::NpcCombatSpec dummy;
    dummy.actor_kind = 2;
    dummy.passive = true;
    dummy.max_health = 100;
    // Where a target stands `distance` m from the player along a yaw (0: -Z).
    const auto along = [&](double yaw, double distance, double aside = 0) {
        const Vec3 at = server.state_of(me)->position;
        return Vec3{at.x - std::sin(yaw) * distance + std::cos(yaw) * aside, at.y,
                    at.z - std::cos(yaw) * distance - std::sin(yaw) * aside};
    };
    // One shot at a yaw and pitch, then a pause.
    const auto fire_once = [&](double yaw, double pitch, bool aimed = false) {
        GameClient::Controls c;
        c.yaw = yaw;
        c.pitch = pitch;
        c.fire = true;
        c.aim = aimed;
        client.set_input(c);
        run(2.0 / 30.0);
        GameClient::Controls rest;
        rest.yaw = yaw;
        client.set_input(rest);
        run(1.0);  // (the gun's shot delay)
    };
    WeaponDef gun;
    gun.archetype = "rifle_auto";
    gun.damage = 30;
    gun.shot_delay = 1.0;
    gun.yaw_spread = gun.pitch_spread = gun.spread_per_shot = 0;
    gun.bolt_gravity = 0;
    const auto arm = [&](const WeaponDef& weapon, std::vector<StatModifier> perks = {},
                         std::vector<TriggeredPerk> triggered = {}) {
        Loadout lo;
        lo.name = "Test";
        lo.weapons[primary_slot] = weapon;
        if (!perks.empty()) lo.gear.push_back({"perks", "", std::move(perks)});
        lo.triggered = std::move(triggered);
        server.set_player_loadouts(me, {lo});
        run(0.2);
    };

    // Piercing Round: through the first body into the second.
    {
        WeaponDef piercing = gun;
        piercing.pierces = 1;
        arm(piercing);
        const double yaw = 0.5;
        const uint32_t a = server.add_combat_npc("Front", along(yaw, 6), 0, 0, dummy);
        const uint32_t b = server.add_combat_npc("Back", along(yaw, 9), 0, 0, dummy);
        fire_once(yaw, -0.05);
        CHECK(near(health(a), 70, 1e-6) && near(health(b), 70, 1e-6));
        arm(gun);
        fire_once(yaw, -0.05);
        CHECK(near(health(a), 40, 1e-6) && near(health(b), 70, 1e-6));  // without it, only the first
    }
    // Flanker: harder from behind (a creature faces -Z unless steered).
    {
        WeaponDef flanking = gun;
        flanking.flank_multiplier = 1.5;
        arm(flanking);
        const double yaw = -0.4;
        const uint32_t back = server.add_combat_npc("Turned", along(yaw, 6), 0, 0, dummy);
        fire_once(yaw, -0.05);
        CHECK(near(health(back), 55, 1e-6));
        MoveInput face;
        face.yaw = float(std::numbers::pi);
        server.steer_npc(back, face);
        run(0.1);
        fire_once(yaw, -0.05);
        CHECK(near(health(back), 25, 1e-6));  // from the front: the usual
    }
    // Ricochet: off the ground into what's beyond.
    {
        const double yaw = 1.2;
        const uint32_t past = server.add_combat_npc("Past", along(yaw, 8), 0, 0, dummy);
        arm(gun);
        fire_once(yaw, -0.3);
        CHECK(near(health(past), 100, 1e-6));  // into the ground
        WeaponDef bouncing = gun;
        bouncing.ricochets = 1;
        arm(bouncing);
        fire_once(yaw, -0.3);
        CHECK(near(health(past), 70, 1e-6));
    }
    // Proximity Fuse: a rocket bursts passing near an enemy.
    {
        WeaponDef rocket = gun;
        rocket.archetype = "heavy_launcher";
        rocket.projectile = Projectile::missile;
        rocket.damage = 100;
        rocket.explosion_damage = 50;
        rocket.explosion_inner = 0.5;
        rocket.explosion_outer = 2.0;
        rocket.bolt_speed = 60;
        rocket.bolt_life = 1.0;
        const double yaw = -1.2;
        const uint32_t near_miss = server.add_combat_npc("Near Miss", along(yaw, 10, 1.2), 0, 0, dummy);
        arm(rocket);
        fire_once(yaw, 0);
        CHECK(near(health(near_miss), 100, 1e-6));  // it flies by
        rocket.proximity_fuse = 1.5;
        arm(rocket);
        fire_once(yaw, 0);
        CHECK(health(near_miss) < 80 && health(near_miss) > 0);

        // Danger Close: its own blast, 60% less.
        rocket.proximity_fuse = 0;
        rocket.explosion_damage = 100;
        rocket.explosion_inner = 2.0;
        rocket.explosion_outer = 4.0;
        arm(rocket);
        const double whole = server.health_of(me);
        fire_once(0, -1.2);
        const double taken = whole - server.health_of(me);
        CHECK(near(taken, 100, 1.0));
        arm(rocket, {{Stat::self_blast, 0, 0.4}});
        const double before = server.health_of(me);
        fire_once(0, -1.2);
        CHECK(near(before - server.health_of(me), 40, 1.0));
    }
    // Teras Kasi Master: the third blow in a row staggers twice as long
    // (creatures only stagger then); Sweeping Blows' knockback.
    {
        WeaponDef staff;
        staff.archetype = "melee";
        staff.projectile = Projectile::melee;
        staff.damage = 1;
        staff.shot_delay = 0.5;
        staff.melee_lunge = 0;
        staff.combo_stagger = 2;
        arm(staff);
        GameServer::NpcCombatSpec tough = dummy;
        tough.max_health = 1000;
        const double yaw = 2.4;
        const uint32_t sparring = server.add_combat_npc("Sparring", along(yaw, 1.8), 0, 0, tough);
        GameClient::Controls swing;
        swing.yaw = yaw;
        swing.fire = true;
        float most = 0;
        int blows = 0;
        double last = health(sparring);
        float before_third = 0;
        for (int i = 0; i < 50; ++i) {
            client.set_input(swing);
            run(1.0 / 30.0);
            if (health(sparring) < last) {
                ++blows;
                last = health(sparring);
            }
            const auto info = server.npc_info(sparring);
            const float stagger = info ? info->state.stagger : 0.0f;
            if (blows < 3) before_third = std::max(before_third, stagger);
            most = std::max(most, stagger);
        }
        GameClient::Controls rest;
        rest.yaw = yaw;
        client.set_input(rest);
        run(0.6);
        CHECK(blows >= 3 && before_third == 0 && most > 0.6f);
        staff.knockback = 6;
        arm(staff);
        const Vec3 from = server.npc_info(sparring)->state.position;
        client.set_input(swing);
        run(2.0 / 30.0);
        client.set_input(rest);
        run(0.5);
        const Vec3 to = server.npc_info(sparring)->state.position;
        CHECK(std::hypot(to.x - from.x, to.z - from.z) > 0.5);
    }
    // Shoulder Charge: sprinting into a creature staggers it.
    {
        const double yaw = -2.4;
        const uint32_t charged = server.add_combat_npc("Charged", along(yaw, 3), 0, 0, dummy);
        arm(gun, {{Stat::charge, 0.8, 1}});
        GameClient::Controls dash;
        dash.yaw = yaw;
        dash.forward = 1;
        dash.sprint = true;
        float most = 0;
        for (int i = 0; i < 15; ++i) {
            client.set_input(dash);
            run(1.0 / 30.0);
            const auto info = server.npc_info(charged);
            most = std::max(most, info ? info->state.stagger : 0.0f);
        }
        GameClient::Controls rest;
        rest.yaw = yaw;
        client.set_input(rest);
        run(0.5);
        CHECK(most > 0.7f);
    }
    // Deflect: a block turns an outlaw's bolts aside from the front, for stamina.
    {
        WeaponDef staff;
        staff.archetype = "melee";
        staff.projectile = Projectile::melee;
        staff.deflect = 10;
        arm(staff);
        GameServer::NpcCombatSpec outlaw;
        outlaw.actor_kind = 3;
        outlaw.max_health = 1000;
        WeaponDef blaster = gun;
        blaster.damage = 20;
        blaster.shot_delay = 0.5;
        outlaw.loadout.weapons[primary_slot] = blaster;
        const Vec3 me_at = server.state_of(me)->position;
        const uint32_t shooter = server.add_combat_npc("Outlaw", me_at + Vec3{0, 0, 8}, 0, 0, outlaw);
        GameClient::Controls guard;
        guard.yaw = std::numbers::pi;  // facing +Z, at the outlaw
        guard.block = true;
        client.set_input(guard);
        run(0.4);
        const double whole = server.health_of(me);
        MoveInput shoot;
        shoot.yaw = 0;  // at the player
        shoot.buttons = MoveInput::fire;
        server.steer_npc(shooter, shoot);
        double least_energy = 1e9;
        for (int i = 0; i < 36; ++i) {
            client.set_input(guard);
            run(1.0 / 30.0);
            least_energy = std::min(least_energy, server.state_of(me)->energy);
        }
        server.steer_npc(shooter, MoveInput{});
        CHECK(near(server.health_of(me), whole, 1e-6));
        CHECK(least_energy < 100 - 8.0 * 1.2 - 15);  // a block's drain, and at least two bolts turned aside
        client.set_input({});
        run(0.6);
        server.remove_npc(shooter);
    }
    // Overcharge: a full recharge goes 20% over, then drains back.
    {
        arm(gun, {{Stat::shield_capacity, 100, 1}, {Stat::shield_recharge, 50, 1}, {Stat::shield_delay, 0, 0.2},
                  {Stat::shield_overcharge, 0.2, 1}});
        const uint32_t brute = server.add_combat_npc("Brute", along(3.0, 30), 0, 0, dummy);
        run(8);  // (it starts empty: recharged, over, and back)
        CHECK(near(server.shield_of(me), 100, 1e-6));
        server.npc_melee(brute, me, 50, DamageType::laser);
        double most = 0;
        for (int i = 0; i < 90; ++i) {
            run(1.0 / 30.0);
            most = std::max(most, server.shield_of(me));
        }
        CHECK(most > 119 && most <= 120 + 1e-6);
        run(6.5);
        CHECK(near(server.shield_of(me), 100, 1e-6));
        // Reactive Plating: a quarter of a blast refills it.
        arm(gun, {{Stat::shield_capacity, 100, 1}, {Stat::blast_shield, 0.25, 1}});
        run(0.1);
        server.npc_melee(brute, me, 200, DamageType::explosive);
        CHECK(near(server.shield_of(me), 50, 1e-6));
    }
    // Spotter: a hit from a shot aimed down the sights marks the target.
    {
        TriggeredPerk spot;
        spot.id = "rifles_spotter";
        spot.name = "Spotter";
        spot.trigger = PerkTrigger::hit;
        spot.aimed = true;
        spot.mark = 4;
        spot.slots = 1;
        arm(gun, {}, {spot});
        const double yaw = -0.8;
        const uint32_t spotted = server.add_combat_npc("Spotted", along(yaw, 6), 0, 0, dummy);
        const auto marked = [&] {
            const net::FieldState field = server.field_state_for(me);
            return std::any_of(field.marks.begin(), field.marks.end(),
                               [&](const net::FieldMark& m) { return m.entity == spotted; });
        };
        fire_once(yaw, -0.05);
        CHECK(health(spotted) < 100 && !marked());  // from the hip: no
        client.take_events();
        fire_once(yaw, -0.05, true);
        CHECK(marked());
        // The HUD's: the perk's name with the loadout, and the server's word that it fired.
        CHECK(!client.custom_loadouts().empty() && client.custom_loadouts()[0].triggered.size() == 1 &&
              client.custom_loadouts()[0].triggered[0].name == "Spotter");
        const auto events = client.take_events();
        CHECK(std::any_of(events.begin(), events.end(), [&](const net::CombatEvent& e) {
            return e.kind == net::CombatEvent::perk && e.attacker == me && e.victim == 0;
        }));
    }
    // Low Profile: snapshots say its shots don't show on radar. Quiet Step:
    // creatures notice it from closer.
    arm(gun, {{Stat::radar_quiet, 1, 1}, {Stat::notice, 0, 0.6}});
    CHECK(server.describe(me) && server.describe(me)->quiet && near(server.notice_of(me), 0.6, 1e-9));
    arm(gun);
    CHECK(server.describe(me) && !server.describe(me)->quiet && server.notice_of(me) == 1);
    client.disconnect();
    run(0.1);
}

// The melee perks between two players: a block's leak and cost (Iron Guard),
// a blow through a block (Sweeping Blows), a parry's riposte (Riposte),
// stagger shrugged off (Steady Stance, Juggernaut), and a slow (Juggernaut).
void test_server_melee() {
    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink a_link{links, {2, 1}};
    net::LoopbackLink b_link{links, {3, 1}};
    ServerConfig config;
    config.pvp_enabled = true;
    config.spawn = {0, 0, 0};
    GameServer server(srv_link, world, config);
    GameClient attacker(a_link, world, ClientConfig{.server = server_ep, .name = "Attacker"});
    GameClient defender(b_link, world, ClientConfig{.server = server_ep, .name = "Defender"});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            attacker.update(now);
            defender.update(now);
        }
    };
    run(0.6);
    CHECK(attacker.status() == GameClient::Status::connected && defender.status() == GameClient::Status::connected);
    const uint32_t a = attacker.entity_id(), b = defender.entity_id();
    WeaponDef staff;
    staff.archetype = "melee";
    staff.projectile = Projectile::melee;
    staff.damage = 100;
    staff.shot_delay = 0.5;
    staff.melee_lunge = 0;
    const auto arm = [&](uint32_t who, const WeaponDef& weapon, std::vector<StatModifier> perks = {}) {
        Loadout lo;
        lo.name = "Melee";
        lo.weapons[primary_slot] = weapon;
        if (!perks.empty()) lo.gear.push_back({"perks", "", std::move(perks)});
        server.set_player_loadouts(who, {lo});
    };
    // Face to face, 1.5 m apart, fresh.
    const auto square_off = [&] {
        for (const uint32_t who : {a, b}) {
            MoveState st = *server.state_of(who);
            st.position = who == a ? Vec3{0, 0, 0} : Vec3{0, 0, -1.5};
            st.velocity = {};
            st.yaw = who == a ? 0.0f : float(std::numbers::pi);
            st.stagger = 0;
            st.energy = 100;
            server.set_state(who, st);
        }
        run(0.2);
    };
    GameClient::Controls a_rest, a_swing, b_rest, b_guard;
    a_swing.fire = true;
    b_rest.yaw = b_guard.yaw = std::numbers::pi;
    b_guard.block = true;
    const auto swing = [&] {
        attacker.set_input(a_swing);
        run(2.0 / 30.0);
        attacker.set_input(a_rest);
        run(0.5);
    };
    const auto lost = [&](uint32_t who, auto&& what) {
        const double before = server.health_of(who);
        what();
        return before - server.health_of(who);
    };

    // Iron Guard: a block lets 30% through, for half the stamina.
    WeaponDef guard = staff;
    guard.block_leak = 0.3;
    guard.block_cost = 0.5;
    guard.riposte = true;
    arm(a, staff);
    arm(b, guard);
    square_off();
    defender.set_input(b_guard);
    run(0.5);  // (past the parry window)
    const double energy_before = server.state_of(b)->energy;
    CHECK(near(lost(b, swing), 30, 1e-6));
    // A swing's 15 stamina, halved, and the block's own drain (4/s) over the swing's 0.57 s.
    CHECK(near(energy_before - server.state_of(b)->energy, 7.5 + 4.0 * (2.0 / 30.0 + 0.5), 0.3));
    // Sweeping Blows: through the block.
    WeaponDef sweeping = staff;
    sweeping.unblockable = true;
    sweeping.knockback = 6;
    arm(a, sweeping);
    square_off();
    defender.set_input(b_guard);
    run(0.5);
    CHECK(near(lost(b, swing), 100, 1e-6));
    defender.set_input(b_rest);
    run(0.5);

    // Riposte: a parry staggers the attacker, and the parrier's next blow crits.
    arm(a, staff);
    arm(b, guard);
    square_off();
    defender.set_input(b_guard);
    run(2.0 / 30.0);  // (inside the parry window)
    CHECK(server.state_of(b)->parry_window > 0);
    float parried = 0;
    for (int i = 0; i < 5; ++i) {  // (the swing lands a tick or two on)
        attacker.set_input(i < 2 ? a_swing : a_rest);
        run(1.0 / 30.0);
        parried = std::max(parried, server.state_of(a)->stagger);
    }
    CHECK(parried > 0.4f);
    GameClient::Controls b_strike = b_rest;
    b_strike.fire = true;
    defender.set_input(b_strike);
    const double a_before = server.health_of(a);
    run(2.0 / 30.0);
    defender.set_input(b_rest);
    run(0.3);
    CHECK(near(a_before - server.health_of(a), 100 * staff.crit_multiplier, 1e-6));

    // Steady Stance and Juggernaut: a blow staggers less, or not at all.
    const auto stagger_from_blow = [&](std::vector<StatModifier> perks) {
        arm(b, staff, std::move(perks));
        arm(a, staff);
        square_off();
        defender.set_input(b_rest);
        attacker.set_input(a_swing);
        run(2.0 / 30.0);
        attacker.set_input(a_rest);
        const float st = server.state_of(b)->stagger;
        run(0.6);
        return st;
    };
    CHECK(stagger_from_blow({}) > 0.25f);
    const float steady = stagger_from_blow({{Stat::stagger_resist, 0.5, 1}});
    CHECK(steady > 0.1f && steady < 0.2f);
    CHECK(stagger_from_blow({{Stat::stagger_resist, 1, 1}}) == 0);

    // Juggernaut: a slow (a blow's hit effect) doesn't slow it.
    WeaponDef slowing = staff;
    slowing.damage = 1;
    slowing.hit_effect = HitEffect{"slowed", "Slowed", 1, 5, 0, DamageType::melee, {{Stat::run_speed, 0, 0.5}}};
    const auto run_after_slow = [&](std::vector<StatModifier> perks) {
        arm(b, staff, std::move(perks));
        arm(a, slowing);
        square_off();
        swing();
        const Vec3 from = server.state_of(b)->position;
        GameClient::Controls go = b_rest;
        go.forward = 1;
        defender.set_input(go);
        run(0.5);
        defender.set_input(b_rest);
        const Vec3 to = server.state_of(b)->position;
        run(5.5);  // (the slow wears off)
        return std::hypot(to.x - from.x, to.z - from.z);
    };
    CHECK(run_after_slow({}) < 2.2);
    CHECK(run_after_slow({{Stat::slow_resist, 1, 1}}) > 2.9);
    attacker.disconnect();
    defender.disconnect();
    run(0.1);
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

        // A creature killed with the starter E-11 in hand trains Carbines.
        server.award_xp(me, 150);
        run(0.3);
        const auto* mine = server.professions_of(me);
        CHECK(mine && profession_xp(*mine, "carbines") == 150);
        CHECK(client.professions().professions.size() == 1 &&
              client.professions().professions[0].id == "carbines" && client.professions().professions[0].xp == 150);
        CHECK(client.player_status().notice.find("Carbines reached level 2") != std::string::npos);

        // Hits taken in no armor train Unarmored: a point for each 10 damage.
        server.npc_melee(rat, me, 55, DamageType::melee);
        server.npc_melee(rat, me, 55, DamageType::melee);
        run(0.2);
        CHECK(mine && profession_xp(*mine, "unarmored") == 11);

        // The Companion Slot keeps only a companion from their datapad.
        CharacterLoadout lo = client.char_loadouts().at(0);
        lo.companion = "a rancor";
        client.save_loadout(lo);
        run(0.3);
        CHECK(client.char_loadouts().size() == 1 && client.char_loadouts()[0].companion.empty());
        const auto& resolved = client.custom_loadouts();
        CHECK(!resolved.empty() && resolved[0].weapons[0] && resolved[0].weapons[0]->archetype == "carbine_burst");
        const WeaponDef e11 = catalog.resolve_weapon(*catalog.find("wp_rifle_e11"));
        const auto bloom_now = [&] {
            return client.custom_loadouts().empty() || !client.custom_loadouts()[0].weapons[0]
                       ? -1.0 : client.custom_loadouts()[0].weapons[0]->spread_per_shot;
        };
        CHECK(e11.spread_per_shot > 0 && near(bloom_now(), e11.spread_per_shot, 1e-6));  // nothing learned yet

        // A trainer teaches a perk, for its level and credits; it counts from then.
        server.set_vendors({{"Carbine Trainer", "trainer", {0, 0, 0}, 4, "carbines"}});
        client.service(net::ServiceRequest::learn_perk, "carbines_burst_control", 0, 0);  // Carbines 2: not yet
        run(0.3);
        CHECK(client.professions().professions[0].perks.empty());
        server.award_xp(me, 400);  // Carbines 5
        server.award_credits(me, 600);
        run(0.3);
        const int64_t credits = client.player_status().credits;
        CHECK(credits >= 500);
        client.service(net::ServiceRequest::learn_perk, "pistols_quick_draw", 0, 0);  // not this trainer's
        client.service(net::ServiceRequest::learn_perk, "carbines_burst_control", 0, 0);
        run(0.4);
        const auto carbines_progress = [&]() -> ProfessionProgress {
            for (const ProfessionProgress& p : client.professions().professions) {
                if (p.id == "carbines") return p;
            }
            return {};
        };
        CHECK((carbines_progress().perks == std::vector<std::string>{"carbines_burst_control"}));
        CHECK(client.player_status().credits == credits - 500);
        CHECK(near(bloom_now(), e11.spread_per_shot * 0.65, 1e-6));  // Burst Control
        // Given up for free, and learned again for free.
        client.service(net::ServiceRequest::forget, "carbines_burst_control", 0, 0);
        run(0.4);
        CHECK(carbines_progress().perks.empty() && near(bloom_now(), e11.spread_per_shot, 1e-6));
        client.service(net::ServiceRequest::learn_perk, "carbines_burst_control", 0, 0);
        run(0.4);
        CHECK(carbines_progress().perks.size() == 1 && client.player_status().credits == credits - 500);

        // Not licensed: no gathering, and none of its experience.
        server.award_profession_xp("Trainee", {{"foraging", 50}}, 0);
        run(0.2);
        CHECK(mine && profession_xp(*mine, "foraging") == 0);

        // Quest and mission experience goes to the carried weapon's and the worn armor's professions.
        server.award_profession_xp("Trainee", {{"pistols", 40}}, 60);
        run(0.3);
        CHECK(mine && profession_xp(*mine, "pistols") == 40);
        CHECK(mine && profession_xp(*mine, "carbines") == 580);
        CHECK(mine && profession_xp(*mine, "unarmored") == 41);
        client.disconnect();
        run(0.1);
    }
    {
        SqliteCharacterStore store(db_path);
        const auto saved = store.find_by_name("Trainee");
        CHECK(saved.has_value());
        const auto progress = saved ? store.list_professions(saved->id) : std::vector<ProfessionProgress>{};
        CHECK(profession_xp(progress, "carbines") == 580);
        CHECK(saved && store.get_extra(saved->id, "perks:carbines") == "carbines_burst_control");
        CHECK(saved && store.get_extra(saved->id, "paid_perks:carbines") == "carbines_burst_control");
        CHECK(profession_xp(progress, "unarmored") == 41);
        CHECK(profession_xp(progress, "pistols") == 40);

        // Rewarded while away (on a battle's server): saved for when they're back.
        net::LoopbackHub links;
        net::LoopbackLink srv_link{links, {1, 1}};
        ServerConfig config;
        config.catalog = &catalog;
        config.professions = &book;
        GameServer server(srv_link, world, config, &store);
        server.award_profession_xp("Trainee", {{"carbines", 25}, {"bounty_hunter", 99}}, 30);
        const auto later = saved ? store.list_professions(saved->id) : std::vector<ProfessionProgress>{};
        CHECK(profession_xp(later, "carbines") == 620);
        CHECK(profession_xp(later, "bounty_hunter") == 99);  // in the game since Stage 12
        CHECK(profession_xp(later, "rifles") == 0);
        CHECK(profession_xp(later, "unarmored") == 56);
    }
    std::filesystem::remove(db_path);
}

} // namespace

int main() {
    try {
        test_roster();
        test_training();
        test_perks();
        test_triggers();
        test_server_triggers();
        test_mechanics();
        test_server_mechanics();
        test_server_melee();
        test_server_professions();
    } catch (const std::exception& error) {
        std::printf("FAIL: %s\n", error.what());
        return 1;
    }
    return swm::test::finish("profession_test");
}
