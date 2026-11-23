#pragma once

#include <memory>
#include <string>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

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

    static godot::Array get_species_list();

protected:
    static void _bind_methods();

private:
    std::unique_ptr<core::SqliteCharacterStore> store_;
    std::string last_error_;
};

} // namespace swm::godot_bindings
