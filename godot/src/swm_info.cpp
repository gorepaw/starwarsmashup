#include "swm_info.h"

#include <string>

#include <godot_cpp/core/class_db.hpp>

#include "swm/core/build_info.h"

namespace swm::godot_bindings {

static_assert(sizeof(godot::real_t) == 8, "the GDExtension must be built with double precision (GODOTCPP_PRECISION)");

godot::String SwmInfo::get_core_version() const {
    const std::string version(core::version());
    return godot::String::utf8(version.c_str());
}

int64_t SwmInfo::get_real_size() const {
    return sizeof(godot::real_t);
}

void SwmInfo::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("get_core_version"), &SwmInfo::get_core_version);
    godot::ClassDB::bind_method(godot::D_METHOD("get_real_size"), &SwmInfo::get_real_size);
}

} // namespace swm::godot_bindings
