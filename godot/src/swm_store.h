#pragma once

#include <memory>
#include <string>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "swm/core/items.h"
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
    godot::Dictionary grant_item(int64_t char_id, const godot::String& def_id, double quality);
    bool remove_item(int64_t item_id);
    godot::Array list_loadouts(int64_t char_id);
    godot::Error save_loadout(int64_t char_id, int64_t slot_index, const godot::Dictionary& loadout);
    godot::Error ensure_starter_kit(int64_t char_id);
    godot::Dictionary buy_item(int64_t char_id, const godot::String& def_id, bool use_faction_points);
    godot::Dictionary add_rewards(int64_t char_id, int64_t xp, int64_t faction_points, int64_t credits);
    godot::Error set_active_loadout(int64_t char_id, int64_t slot_index);

    static godot::Array get_species_list();

protected:
    static void _bind_methods();

private:
    std::unique_ptr<core::SqliteCharacterStore> store_;
    core::ItemCatalog catalog_;
    std::string last_error_;
};

} // namespace swm::godot_bindings
