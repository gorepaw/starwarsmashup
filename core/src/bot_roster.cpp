#include "swm/core/bot_roster.h"

#include <algorithm>
#include <array>
#include <format>

namespace swm::core {
namespace {

uint64_t next_rng(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double rand_unit(uint64_t& state) {
    return double(next_rng(state) >> 11) / double(1ull << 53);
}

struct LoadoutRecipe {
    const char* name;
    const char* primary;
    const char* sidearm;
    const char* secondary;
    const char* shield;
    std::array<const char*, 5> armor;     // helmet, chest, biceps/bracers, leggings, boots
    std::array<const char*, 5> look;      // its cosmetic override: full_body, helmet, chest, leggings, boots
};

constexpr std::array<const char*, 24> rebel_names{
    "Kael Solusar", "Tala Vane",   "Corran Jex", "Ryn Talos",   "Bria Venn",  "Dax Ordo",
    "Nyx Krell",    "Olin Tark",   "Sera Vance", "Garik Vos",   "Maren Korr", "Tycho Bris",
    "Jek Porkin",   "Lina Faris",  "Shara Bey",  "Keyan Farl",  "Zev Senesc", "Horton Salm",
    "Nien Numb",    "Verris Tarn", "Kori Daan",  "Bode Talen",  "Arvel Cryn", "Kess Mora",
};

constexpr std::array<const char*, 24> empire_names{
    "TK-421",    "TK-808",    "TK-319",     "TK-614",    "Sgt. Vane",   "Cpl. Krann",
    "Lt. Darsk", "TK-118",    "TK-904",     "Scout-19",  "TK-552",      "Pvt. Delak",
    "TK-275",    "Sgt. Krenn","TK-731",     "Cpl. Hesk", "Lt. Brann",   "TK-490",
    "Scout-44",  "TK-663",    "Trooper Vox","TK-105",    "Sgt. Talor",  "TK-982",
};

constexpr std::array<const char*, 24> republic_names{
    "CT-1104",  "CT-4029", "CT-7567",  "CT-5555",  "CT-2104",  "ARC-318",
    "Jet-109",  "CT-6116", "Sgt. Kix", "Hvy-712",  "CT-3270",  "CT-8801",
    "ARC-501",  "CT-4409", "Jet-212",  "CT-9904",  "Cpl. Tup", "CT-782",
    "Hvy-404",  "CT-6482", "Cmdr-09",  "CT-1988",  "ARC-77",   "CT-5385",
};

constexpr std::array<const char*, 24> separatist_names{
    "B1-042",  "B1-218",  "B1-704",  "OOM-19",  "B2-104",  "B2-512",
    "IG-104",  "OOM-88",  "B1-391",  "B1-655",  "B2-227",  "IG-209",
    "B1-119",  "B1-840",  "B1-502",  "OOM-33",  "B2-891",  "B1-974",
    "IG-315",  "B1-067",  "B2-440",  "B1-308",  "OOM-71",  "B1-923",
};

const std::array<LoadoutRecipe, max_character_loadouts> rebel_recipes{{
    {"Rebel Rifleman", "wp_rifle_e11", "wp_pistol_dh17", "wp_grenade_c22", "psg_mark1",
     {"armor_ubese_helmet", "armor_ubese_shirt", "armor_ubese_gloves", "armor_ubese_leggings", "armor_ubese_boots"},
     {nullptr, nullptr, "cloth_jacket_rebel", "cloth_pants_cargo", "cloth_boots_leather"}},
    {"Alliance Commando", "wp_carbine_dh17", "wp_pistol_dl44", "wp_grenade_fragmentation", "psg_mark2_energy",
     {"armor_composite_helmet", "armor_composite_chest", "armor_composite_gloves", "armor_composite_leggings",
      "armor_composite_boots"},
     {nullptr, nullptr, "cloth_jacket_rebel", "cloth_pants_cargo", "cloth_boots_leather"}},
    {"Vanguard Breacher", "wp_rifle_sg82", "wp_melee_vibroblade", "wp_grenade_glop", "psg_mark2_kinetic",
     {nullptr, "armor_padded_chest", nullptr, "armor_padded_leggings", "armor_padded_boots"},
     {nullptr, nullptr, "cloth_shirt_tunic", "cloth_pants_cargo", "cloth_boots_leather"}},
    {"SpecForce Heavy", "wp_heavy_rocket_launcher", "wp_pistol_dh17", "wp_grenade_ion", "psg_mark3_rebel",
     {"armor_composite_helmet", "armor_padded_chest", nullptr, "armor_padded_leggings", "armor_padded_boots"},
     {nullptr, nullptr, "armor_composite_chest", "armor_composite_leggings", "armor_composite_boots"}},
    {"Alliance Marksman", "wp_rifle_laser", "wp_pistol_ion", "wp_grenade_c22", "psg_mark1",
     {"armor_ubese_helmet", "armor_ubese_shirt", "armor_ubese_bracer_l", "armor_ubese_leggings", "armor_ubese_boots"},
     {nullptr, nullptr, "cloth_jacket_rebel", "cloth_pants_cargo", "cloth_boots_leather"}},
}};

const std::array<LoadoutRecipe, max_character_loadouts> empire_recipes{{
    {"Imperial Stormtrooper", "wp_rifle_e11", "wp_pistol_scout_blaster", "wp_grenade_c22", "psg_mark1",
     {"armor_stormtrooper_helmet", "armor_stormtrooper_chest", "armor_stormtrooper_gloves",
      "armor_stormtrooper_leggings", "armor_stormtrooper_boots"},
     {"suit_imp_stormtrooper", nullptr, nullptr, nullptr, nullptr}},
    {"Imperial Heavy", "wp_rifle_t21", "wp_pistol_d18", "wp_grenade_fragmentation", "psg_mark3_imperial",
     {"armor_stormtrooper_helmet", "armor_stormtrooper_chest", "armor_stormtrooper_belt",
      "armor_stormtrooper_leggings", "armor_stormtrooper_boots"},
     {nullptr, nullptr, nullptr, nullptr, nullptr}},
    {"Imperial Scout", "wp_rifle_dlt20a", "wp_pistol_scout_blaster", "wp_grenade_glop", "psg_mark2_energy",
     {"armor_ubese_helmet", "armor_padded_chest", nullptr, "armor_padded_leggings", "armor_padded_boots"},
     {nullptr, "armor_stormtrooper_helmet", "cloth_shirt_tunic", "armor_stormtrooper_leggings",
      "armor_stormtrooper_boots"}},
    {"Imperial Rocket", "wp_heavy_rocket_launcher", "wp_pistol_scout_blaster", "wp_grenade_ion", "psg_mark2_kinetic",
     {"armor_stormtrooper_helmet", "armor_stormtrooper_chest", "armor_stormtrooper_bracers",
      "armor_stormtrooper_leggings", "armor_stormtrooper_boots"},
     {"suit_imp_stormtrooper", nullptr, nullptr, nullptr, nullptr}},
    {"Storm Commando", "wp_carbine_ee3", "wp_melee_stun_baton", "wp_grenade_c22", "psg_mark3_imperial",
     {"armor_stormtrooper_helmet", "armor_stormtrooper_chest", "armor_stormtrooper_biceps",
      "armor_stormtrooper_leggings", "armor_stormtrooper_boots"},
     {nullptr, nullptr, nullptr, nullptr, nullptr}},
}};

const std::array<LoadoutRecipe, max_character_loadouts> republic_recipes{{
    {"Clone Trooper", "wp_rifle_laser", "wp_pistol_d18", "wp_grenade_c22", "psg_mark1",
     {"armor_composite_helmet", "armor_composite_chest", "armor_composite_gloves", "armor_composite_leggings",
      "armor_composite_boots"},
     {"suit_rep_clone_p2", nullptr, nullptr, nullptr, nullptr}},
    {"Phase I Clone", "wp_rifle_e11", "wp_pistol_d18", "wp_grenade_ion", "psg_mark2_energy",
     {"armor_composite_helmet", "armor_composite_chest", "armor_composite_gloves", "armor_composite_leggings",
      "armor_composite_boots"},
     {"suit_rep_clone_p1", nullptr, nullptr, nullptr, nullptr}},
    {"ARC Trooper", "wp_carbine_ee3", "wp_pistol_ion", "wp_grenade_fragmentation", "psg_mark3_rebel",
     {"armor_composite_helmet", "armor_composite_chest", "armor_composite_gloves", "armor_composite_leggings",
      "armor_composite_boots"},
     {"suit_rep_arc_trooper", nullptr, nullptr, nullptr, nullptr}},
    {"Clone Heavy", "wp_heavy_rocket_launcher", "wp_pistol_d18", "wp_grenade_ion", "psg_mark2_kinetic",
     {"armor_composite_helmet", "armor_padded_chest", nullptr, "armor_padded_leggings", "armor_padded_boots"},
     {"suit_rep_heavy_trooper", nullptr, nullptr, nullptr, nullptr}},
    {"Jet Trooper", "wp_rifle_sg82", "wp_pistol_ion", "wp_grenade_glop", "psg_mark2_energy",
     {"armor_ubese_helmet", "armor_padded_chest", nullptr, "armor_padded_leggings", "armor_padded_boots"},
     {"suit_rep_jet_trooper", nullptr, nullptr, nullptr, nullptr}},
}};

const std::array<LoadoutRecipe, max_character_loadouts> separatist_recipes{{
    {"B1 Assault Droid", "wp_rifle_e11", "wp_pistol_d18", "wp_grenade_c22", "psg_mark1",
     {nullptr, "armor_padded_chest", nullptr, "armor_padded_leggings", "armor_padded_boots"},
     {"suit_cis_bdroid", nullptr, nullptr, nullptr, nullptr}},
    {"Super Battle Droid", "wp_carbine_laser", "wp_pistol_ion", "wp_grenade_fragmentation", "psg_mark2_kinetic",
     {"armor_composite_helmet", "armor_composite_chest", "armor_composite_gloves", "armor_composite_leggings",
      "armor_composite_boots"},
     {"suit_cis_sbdroid", nullptr, nullptr, nullptr, nullptr}},
    {"B1 Marksman Droid", "wp_rifle_dlt20a", "wp_pistol_d18", "wp_grenade_c22", "psg_mark1",
     {nullptr, "armor_ubese_shirt", nullptr, "armor_ubese_leggings", "armor_ubese_boots"},
     {"suit_cis_bdroid", nullptr, nullptr, nullptr, nullptr}},
    {"Heavy Rocket Droid", "wp_heavy_rocket_launcher", "wp_pistol_ion", "wp_grenade_ion", "psg_mark2_energy",
     {"armor_composite_helmet", "armor_padded_chest", nullptr, "armor_padded_leggings", "armor_padded_boots"},
     {"suit_cis_sbdroid", nullptr, nullptr, nullptr, nullptr}},
    {"MagnaGuard", "wp_melee_vibrolance", "wp_pistol_ion", "wp_grenade_glop", "psg_mark3_imperial",
     {"armor_stormtrooper_helmet", "armor_stormtrooper_chest", "armor_stormtrooper_gloves",
      "armor_stormtrooper_leggings", "armor_stormtrooper_boots"},
     {"suit_cis_magnaguard", nullptr, nullptr, nullptr, nullptr}},
}};

constexpr std::array<const char*, 5> armor_slot_keys{"helmet", "chest", "gloves", "leggings", "boots"};
constexpr std::array<const char*, 5> look_slot_keys{"full_body", "helmet", "chest", "leggings", "boots"};

std::string make_appearance(uint64_t& rng) {
    const double h = 0.35 + rand_unit(rng) * 0.40;
    const double m = 0.35 + rand_unit(rng) * 0.45;
    return std::format("{{\"height\":{:.2f},\"muscle\":{:.2f}}}", h, m);
}

std::string fallback_worn_for(std::string_view faction, size_t slot, Gender gender) {
    const char g = gender == Gender::female ? 'f' : 'm';
    if (faction == "republic") {
        static constexpr std::array<const char*, 5> rep_models{
            "override:characters/rep_inf_ep3trooper/rep_inf_ep3trooper.gltf",
            "override:characters/rep_inf_trooper/rep_inf_trooper.gltf",
            "override:characters/rep_inf_arctrooper/rep_inf_arctrooper.gltf",
            "override:characters/rep_inf_ep3heavytrooper/rep_inf_ep3heavytrooper.gltf",
            "override:characters/rep_inf_jettrooper/rep_inf_jettrooper.gltf",
        };
        return rep_models[slot % rep_models.size()];
    }
    if (faction == "separatists") {
        static constexpr std::array<const char*, 5> cis_models{
            "override:characters/cis_inf_bdroid/cis_inf_bdroid.gltf",
            "override:characters/cis_inf_sbdroid/cis_inf_sbdroid.gltf",
            "override:characters/cis_inf_bdroid/cis_inf_bdroid.gltf",
            "override:characters/cis_inf_sbdroid/cis_inf_sbdroid.gltf",
            "override:characters/cis_inf_magnaguard/cis_inf_magnaguard.gltf",
        };
        return cis_models[slot % cis_models.size()];
    }
    if (faction == "empire" && (slot == 0 || slot == 3)) {
        return "override:characters/stormtrooper/stormtrooper.gltf";
    }
    if (faction == "empire") {
        return std::format(
            "armor/stormtrooper/helmet_{0}.gltf,armor/stormtrooper/chest_{0}.gltf,"
            "armor/stormtrooper/leggings_{0}.gltf,armor/stormtrooper/boots_{0}.gltf",
            g);
    }
    return std::format("clothing/jacket_rebel_{0}.gltf,clothing/pants_cargo_{0}.gltf,clothing/boots_leather_{0}.gltf", g);
}

BotProfileEntry build_entry(std::string name, std::string faction, Species species, Gender gender,
                            const std::array<LoadoutRecipe, max_character_loadouts>& recipes,
                            const ItemCatalog* catalog, uint64_t& rng) {
    BotProfileEntry out;
    out.name = std::move(name);
    out.faction = std::move(faction);
    out.species = species;
    out.gender = gender;
    out.appearance = make_appearance(rng);

    // Each bot's gear centers on a grade of its own (mostly Standard and Fine),
    // each piece a little either side.
    const double base_tier = 0.38 + rand_unit(rng) * 0.48;
    out.loadouts.reserve(max_character_loadouts);
    out.worn_per_loadout.reserve(max_character_loadouts);

    for (size_t s = 0; s < max_character_loadouts; ++s) {
        const LoadoutRecipe& r = recipes[s];
        if (catalog == nullptr) {
            Loadout lo;
            lo.name = r.name;
            out.loadouts.push_back(std::move(lo));
            out.worn_per_loadout.push_back(fallback_worn_for(out.faction, s, gender));
            continue;
        }

        std::vector<ItemInstance> inv;
        uint64_t next_id = 1;
        const auto add_item = [&](const char* id) -> uint64_t {
            if (id == nullptr || catalog->find(id) == nullptr) return 0;
            const double tier = std::clamp(base_tier + (rand_unit(rng) - 0.5) * 0.14, 0.25, 0.95);
            const ItemGrade grade = tier < 0.4   ? ItemGrade::flawed
                                    : tier < 0.6 ? ItemGrade::standard
                                    : tier < 0.8 ? ItemGrade::fine
                                                 : ItemGrade::exceptional;
            inv.push_back({next_id, 1, id, grade, "", painted_color_json(*catalog->find(id), out.faction), 1});
            return next_id++;
        };

        CharacterLoadout cl;
        cl.slot_index = int(s);
        cl.name = r.name;
        cl.primary_id = add_item(r.primary);
        cl.sidearm_id = add_item(r.sidearm);
        cl.secondary_id = add_item(r.secondary);
        if (const uint64_t sh = add_item(r.shield)) {
            cl.gear_ids["deflector_shield"] = sh;
        }
        for (size_t k = 0; k < armor_slot_keys.size(); ++k) {
            if (const uint64_t id = add_item(r.armor[k])) {
                cl.gear_ids[armor_slot_keys[k]] = id;
            }
        }
        for (size_t k = 0; k < look_slot_keys.size(); ++k) {
            // Empire mixes full-body stormtrooper suit & modular armor looks across bots.
            if (out.faction == "empire" && std::string_view(look_slot_keys[k]) == "full_body" &&
                rand_unit(rng) < 0.45) {
                continue;
            }
            if (const uint64_t id = add_item(r.look[k])) {
                cl.appearance_ids[look_slot_keys[k]] = id;
            }
        }

        Loadout built = catalog->build_loadout(cl, inv, species);
        // Bots never exhaust standard or heavy ammo during BF2 conquest matches.
        built.unified_standard_ammo = -1;
        built.unified_heavy_ammo = -1;
        std::string worn = resolve_worn_models(*catalog, cl, inv, species, gender);
        if (worn.empty()) {
            worn = plain_clothes(*catalog, gender);
        }
        if (worn.empty()) {
            worn = fallback_worn_for(out.faction, s, gender);
        }
        out.loadouts.push_back(std::move(built));
        out.worn_per_loadout.push_back(std::move(worn));
    }
    return out;
}

} // namespace

std::vector<BotProfileEntry> build_stock_bot_roster(const ItemCatalog* catalog, uint32_t seed) {
    uint64_t rng = uint64_t(seed) * 0x9E3779B97F4A7C15ull + 1337;
    std::vector<BotProfileEntry> roster;
    roster.reserve(96);

    static constexpr std::array<Species, 9> rebel_species{
        Species::human, Species::twilek, Species::rodian, Species::zabrak, Species::bothan,
        Species::trandoshan, Species::wookiee, Species::moncal, Species::sullustan,
    };
    for (size_t i = 0; i < rebel_names.size(); ++i) {
        const Species sp = rebel_species[i % rebel_species.size()];
        const Gender gen = (i % 3 == 1) ? Gender::female : Gender::male;
        roster.push_back(build_entry(rebel_names[i], "rebellion", sp, gen, rebel_recipes, catalog, rng));
    }

    for (size_t i = 0; i < empire_names.size(); ++i) {
        const Species sp = (i % 6 == 5) ? Species::zabrak : Species::human;
        const Gender gen = (i % 4 == 2) ? Gender::female : Gender::male;
        roster.push_back(build_entry(empire_names[i], "empire", sp, gen, empire_recipes, catalog, rng));
    }

    for (size_t i = 0; i < republic_names.size(); ++i) {
        roster.push_back(
            build_entry(republic_names[i], "republic", Species::human, Gender::male, republic_recipes, catalog, rng));
    }

    for (size_t i = 0; i < separatist_names.size(); ++i) {
        roster.push_back(build_entry(separatist_names[i], "separatists", Species::human, Gender::male,
                                     separatist_recipes, catalog, rng));
    }
    return roster;
}

std::vector<const BotProfileEntry*> roster_for_faction(const std::vector<BotProfileEntry>& roster,
                                                       std::string_view faction) {
    const std::string norm = normalize_faction(faction);
    std::vector<const BotProfileEntry*> out;
    for (const BotProfileEntry& e : roster) {
        if (e.faction == norm) {
            out.push_back(&e);
        }
    }
    return out;
}

} // namespace swm::core
