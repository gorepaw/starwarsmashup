#pragma once

#include <memory>
#include <string>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "swm/core/crafting.h"
#include "swm/core/items.h"
#include "swm/core/profession.h"
#include "swm/core/sqlite_store.h"

namespace swm::godot_bindings {

// Exposes core::SqliteCharacterStore and core::all_species() to the Godot
// front-end menus (Load Character, Character Creator).
class SwmCharacterStore : public godot::RefCounted {
    GDCLASS(SwmCharacterStore, godot::RefCounted)

public:
    godot::Error open(const godot::String& path);
    void close();

    godot::Array list_characters();
    godot::Dictionary create_character(const godot::String& name, const godot::String& species,
                                       const godot::String& gender, const godot::String& appearance_json,
                                       const godot::String& location, const godot::Vector3& position, double yaw);
    bool delete_character(int64_t id);
    godot::String get_last_error() const;

    godot::Error load_catalog(const godot::String& catalog_ini, const godot::String& bf2_items_dir);
    godot::Array get_catalog() const;
    godot::Array list_items(int64_t char_id);
    godot::Dictionary get_reputation(int64_t char_id);
    godot::Dictionary grant_item(int64_t char_id, const godot::String& def_id, double quality);
    bool remove_item(int64_t item_id);
    godot::Array list_loadouts(int64_t char_id);
    godot::Error save_loadout(int64_t char_id, int64_t slot_index, const godot::Dictionary& loadout);
    godot::Error ensure_starter_kit(int64_t char_id);
    godot::Dictionary buy_item(int64_t char_id, const godot::String& def_id, bool use_faction_points);
    godot::Dictionary add_rewards(int64_t char_id, int64_t xp, int64_t faction_points, int64_t credits);
    godot::Error set_active_loadout(int64_t char_id, int64_t slot_index);

    godot::Dictionary resolve_item_stats(const godot::String& def_id, double quality = 0.5,
                                           bool apply_quality = true, int64_t mark = 0) const;
    // An item's name at a mark ("E-11 Blaster Rifle Mk IV"), and what the
    // mark asks for: {level, profession (its name; "" for the character's
    // highest), health (worn gear: the max health it adds, as a share)};
    // level 1 and "" for Mk 0.
    godot::String item_display_name(const godot::String& def_id, int64_t mark) const;
    godot::Dictionary mark_requirement(const godot::String& def_id, int64_t mark) const;
    godot::Dictionary compute_loadout_stats(const godot::String& species, const godot::Dictionary& loadout,
                                              const godot::Array& items, bool apply_quality = true) const;
    // What a loadout shows on this species and gender: core resolve_worn_models
    // (its gear, its cosmetic override, a bodysuit, a species' own armor).
    godot::String worn_models(const godot::String& species, const godot::String& gender,
                              const godot::Dictionary& loadout, const godot::Array& items) const;

    // The profession roster, for the planner: load it, then its professions
    // (each with its milestones or perks and what they do, in words), and where
    // an amount of experience stands on the curve: {level, into, needed}.
    godot::Error load_professions(const godot::String& path);
    godot::Array get_professions() const;
    godot::Dictionary profession_level(int64_t xp) const;
    int64_t get_perk_cap() const { return core::perk_cap; }

    // Gathering & crafting data + deterministic scanner/preview helpers.
    godot::Error load_crafting(const godot::String& resources_path, const godot::String& schematics_path);
    godot::Array get_resources() const;
    godot::Array get_schematics() const;
    godot::Array get_work_orders() const;
    godot::Dictionary scan_terrain(const godot::String& category, double x, double z, double radius = 60.0,
                                   int64_t grid_n = 17) const;
    godot::PackedStringArray auto_fill_schematic(const godot::String& schematic_id, const godot::Array& materials,
                                                 const godot::String& mode) const;
    godot::Dictionary preview_craft(const godot::String& schematic_id, const godot::PackedStringArray& resource_ids,
                                    int64_t enhancement_count, int64_t crafter_level,
                                    int64_t exp_quality_pct) const;

    static godot::Array get_species_list();

protected:
    static void _bind_methods();

private:
    std::unique_ptr<core::SqliteCharacterStore> store_;
    core::ItemCatalog catalog_;
    core::ProfessionBook professions_;
    core::CraftingBook crafting_;
    std::string last_error_;
};

} // namespace swm::godot_bindings
