#include "swm_store.h"

#include <exception>

#include <godot_cpp/core/class_db.hpp>

#include "swm/core/species.h"

namespace swm::godot_bindings {

namespace {

std::string to_std(const godot::String& s) {
    return s.utf8().get_data();
}

godot::Dictionary to_dict(const core::CharacterRecord& r) {
    godot::Dictionary out;
    out["id"] = int64_t(r.id);
    out["name"] = godot::String::utf8(r.name.c_str());
    out["species"] = godot::String::utf8(r.species.c_str());
    out["gender"] = godot::String::utf8(r.gender.c_str());
    out["location"] = godot::String::utf8(r.location.c_str());
    out["appearance"] = godot::String::utf8(r.appearance.c_str());
    out["position"] = godot::Vector3(real_t(r.position.x), real_t(r.position.y), real_t(r.position.z));
    out["yaw"] = double(r.yaw);
    return out;
}

} // namespace

godot::Error SwmCharacterStore::open(const godot::String& path) {
    last_error_.clear();
    store_.reset();
    try {
        store_ = std::make_unique<core::SqliteCharacterStore>(to_std(path));
        return godot::OK;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return godot::ERR_CANT_OPEN;
    }
}

void SwmCharacterStore::close() {
    store_.reset();
}

godot::Array SwmCharacterStore::list_characters() {
    godot::Array out;
    if (!store_) {
        return out;
    }
    try {
        for (const core::CharacterRecord& r : store_->list_all()) {
            out.push_back(to_dict(r));
        }
    } catch (const std::exception& e) {
        last_error_ = e.what();
    }
    return out;
}

godot::Dictionary SwmCharacterStore::create_character(const godot::String& name, const godot::String& species,
                                                      const godot::String& gender, const godot::String& appearance_json,
                                                      const godot::String& location, const godot::Vector3& position,
                                                      double yaw) {
    last_error_.clear();
    if (!store_) {
        last_error_ = "database not open";
        return {};
    }
    try {
        core::CharacterRecord req;
        req.name = to_std(name);
        req.species = to_std(species);
        req.gender = to_std(gender);
        req.appearance = to_std(appearance_json);
        req.location = to_std(location);
        req.position = {position.x, position.y, position.z};
        req.yaw = float(yaw);
        return to_dict(store_->create_character(req));
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return {};
    }
}

bool SwmCharacterStore::delete_character(int64_t id) {
    last_error_.clear();
    if (!store_) {
        last_error_ = "database not open";
        return false;
    }
    try {
        return store_->remove(core::PersistentId(id));
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

godot::String SwmCharacterStore::get_last_error() const {
    return godot::String::utf8(last_error_.c_str());
}

godot::Array SwmCharacterStore::get_species_list() {
    godot::Array out;
    for (const core::SpeciesDef& sp : core::all_species()) {
        godot::Dictionary d;
        d["id"] = godot::String::utf8(sp.id.c_str());
        d["code"] = godot::String::utf8(sp.code.c_str());
        d["name"] = godot::String::utf8(sp.display_name.c_str());
        d["male_height"] = sp.male_body.height;
        d["female_height"] = sp.female_body.height;
        d["male_eye_height"] = sp.male_body.eye_height;
        d["female_eye_height"] = sp.female_body.eye_height;
        d["scale_min"] = sp.male_body.scale_min;
        d["scale_max"] = sp.male_body.scale_max;
        godot::PackedStringArray blocked;
        for (const std::string& slot : sp.blocked_slots) {
            blocked.push_back(godot::String::utf8(slot.c_str()));
        }
        d["blocked_slots"] = blocked;
        d["traits"] = godot::String::utf8(sp.traits_summary.c_str());
        out.push_back(d);
    }
    return out;
}

void SwmCharacterStore::_bind_methods() {
    using godot::ClassDB;
    using godot::D_METHOD;
    ClassDB::bind_method(D_METHOD("open", "path"), &SwmCharacterStore::open);
    ClassDB::bind_method(D_METHOD("close"), &SwmCharacterStore::close);
    ClassDB::bind_method(D_METHOD("list_characters"), &SwmCharacterStore::list_characters);
    ClassDB::bind_method(
        D_METHOD("create_character", "name", "species", "gender", "appearance_json", "location", "position", "yaw"),
        &SwmCharacterStore::create_character);
    ClassDB::bind_method(D_METHOD("delete_character", "id"), &SwmCharacterStore::delete_character);
    ClassDB::bind_method(D_METHOD("get_last_error"), &SwmCharacterStore::get_last_error);
    ClassDB::bind_static_method("SwmCharacterStore", D_METHOD("get_species_list"),
                                &SwmCharacterStore::get_species_list);
}

} // namespace swm::godot_bindings
