#include "swbfo_info.h"

#include <string>

#include <godot_cpp/core/class_db.hpp>

#include "swbfo/core/build_info.h"

namespace swbfo::godot_bindings {

static_assert(sizeof(godot::real_t) == 8, "the GDExtension must be built with double precision (GODOTCPP_PRECISION)");

godot::String SwbfoInfo::get_core_version() const {
    const std::string version(core::version());
    return godot::String::utf8(version.c_str());
}

int64_t SwbfoInfo::get_real_size() const {
    return sizeof(godot::real_t);
}

void SwbfoInfo::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("get_core_version"), &SwbfoInfo::get_core_version);
    godot::ClassDB::bind_method(godot::D_METHOD("get_real_size"), &SwbfoInfo::get_real_size);
}

} // namespace swbfo::godot_bindings
