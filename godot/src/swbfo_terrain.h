#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "swbfo/core/heightfield.h"

namespace swbfo::godot_bindings {

class SwbfoTerrain : public godot::RefCounted {
    GDCLASS(SwbfoTerrain, godot::RefCounted)

public:
    godot::Error load_bake(const godot::String& prefix);
    int64_t get_samples() const;
    double get_spacing() const;
    godot::Vector2 get_origin() const;
    double get_min_height() const;
    double get_max_height() const;
    double height_at(double game_x, double game_z) const;
    godot::Ref<godot::ArrayMesh> build_chunk(int64_t col0, int64_t row0, int64_t cells, int64_t step,
                                             double skirt_depth) const;

protected:
    static void _bind_methods();

private:
    core::Heightfield terrain_;
    double min_height_ = 0.0;
    double max_height_ = 0.0;
};

} // namespace swbfo::godot_bindings
