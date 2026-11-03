#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>

namespace swm::godot_bindings {

// Build information from core/, exposed to Godot.
class SwmInfo : public godot::RefCounted {
    GDCLASS(SwmInfo, godot::RefCounted)

public:
    godot::String get_core_version() const;

    // Size of Godot's real_t in this extension: 8 in a double-precision build.
    int64_t get_real_size() const;

protected:
    static void _bind_methods();
};

} // namespace swm::godot_bindings
