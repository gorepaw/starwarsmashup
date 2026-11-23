#include "swm_terrain.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace swm::godot_bindings {

void SwmTerrain::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("load_bake", "prefix"), &SwmTerrain::load_bake);
    godot::ClassDB::bind_method(godot::D_METHOD("get_samples"), &SwmTerrain::get_samples);
    godot::ClassDB::bind_method(godot::D_METHOD("get_spacing"), &SwmTerrain::get_spacing);
    godot::ClassDB::bind_method(godot::D_METHOD("get_origin"), &SwmTerrain::get_origin);
    godot::ClassDB::bind_method(godot::D_METHOD("get_min_height"), &SwmTerrain::get_min_height);
    godot::ClassDB::bind_method(godot::D_METHOD("get_max_height"), &SwmTerrain::get_max_height);
    godot::ClassDB::bind_method(godot::D_METHOD("height_at", "game_x", "game_z"), &SwmTerrain::height_at);
    godot::ClassDB::bind_method(godot::D_METHOD("build_chunk", "col0", "row0", "cells", "step", "skirt_depth"),
                                &SwmTerrain::build_chunk);
}

godot::Error SwmTerrain::load_bake(const godot::String& prefix) {
    try {
        terrain_ = core::load_bake(prefix.utf8().get_data());
        min_height_ = terrain_.lowest();
        max_height_ = min_height_;
        const auto& heights = terrain_.heights();
        if (!heights.empty()) {
            const auto [lo, hi] = std::minmax_element(heights.begin(), heights.end());
            min_height_ = *lo;
            max_height_ = *hi;
        }
        return godot::OK;
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("SwmTerrain::load_bake: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
}

int64_t SwmTerrain::get_samples() const {
    return terrain_.samples();
}

double SwmTerrain::get_spacing() const {
    return terrain_.spacing();
}

godot::Vector2 SwmTerrain::get_origin() const {
    return godot::Vector2(float(terrain_.origin_x()), float(terrain_.origin_z()));
}

double SwmTerrain::get_min_height() const {
    return min_height_;
}

double SwmTerrain::get_max_height() const {
    return max_height_;
}

double SwmTerrain::height_at(double game_x, double game_z) const {
    return terrain_.height_at(game_x, game_z);
}

godot::Ref<godot::ArrayMesh> SwmTerrain::build_chunk(int64_t col0, int64_t row0, int64_t cells, int64_t step,
                                                     double skirt_depth) const {
    godot::Ref<godot::ArrayMesh> mesh;
    const int samples = terrain_.samples();
    if (samples < 2 || cells <= 0 || step <= 0) {
        return mesh;
    }
    const int c0 = std::clamp(int(col0), 0, samples - 2);
    const int r0 = std::clamp(int(row0), 0, samples - 2);
    const int c1 = std::min(samples - 1, c0 + int(cells));
    const int r1 = std::min(samples - 1, r0 + int(cells));
    const int s = int(step);
    const int span_c = c1 - c0;
    const int span_r = r1 - r0;
    if (span_c <= 0 || span_r <= 0) {
        return mesh;
    }
    const int count_c = (span_c + s - 1) / s + 1;
    const int count_r = (span_r + s - 1) / s + 1;
    const auto& heights = terrain_.heights();
    const double spacing = terrain_.spacing();
    const double ox = terrain_.origin_x();
    const double oz = terrain_.origin_z();

    auto sample_h = [&](int c, int r) -> float {
        const int cc = std::clamp(c, 0, samples - 1);
        const int rr = std::clamp(r, 0, samples - 1);
        return heights[size_t(rr) * size_t(samples) + size_t(cc)];
    };

    auto sample_n = [&](int c, int r) -> godot::Vector3 {
        const int cl = std::max(0, c - s);
        const int cr = std::min(samples - 1, c + s);
        const int rd = std::max(0, r - s);
        const int ru = std::min(samples - 1, r + s);
        const double dx = double(cr - cl) * spacing;
        const double dz = double(ru - rd) * spacing;
        // Game x is mirrored (-SWG x), so dH/d(game_x) = -(h(cr) - h(cl)) / dx.
        const double dh_dx = dx > 0.0 ? -(double(sample_h(cr, r)) - double(sample_h(cl, r))) / dx : 0.0;
        const double dh_dz = dz > 0.0 ? (double(sample_h(c, ru)) - double(sample_h(c, rd))) / dz : 0.0;
        return godot::Vector3(float(-dh_dx), 1.0f, float(-dh_dz)).normalized();
    };

    const int main_verts = count_c * count_r;
    const int skirt_verts = skirt_depth > 0.0 ? 2 * (count_c + count_r) : 0;
    godot::PackedVector3Array vertices;
    godot::PackedVector3Array normals;
    vertices.resize(main_verts + skirt_verts);
    normals.resize(main_verts + skirt_verts);
    godot::Vector3* v_ptr = vertices.ptrw();
    godot::Vector3* n_ptr = normals.ptrw();

    for (int rr = 0; rr < count_r; ++rr) {
        const int r = std::min(r1, r0 + rr * s);
        const double gz = oz + r * spacing;
        for (int cc = 0; cc < count_c; ++cc) {
            const int c = std::min(c1, c0 + cc * s);
            const double gx = -(ox + c * spacing);
            const int idx = rr * count_c + cc;
            v_ptr[idx] = godot::Vector3(float(gx), sample_h(c, r), float(gz));
            n_ptr[idx] = sample_n(c, r);
        }
    }

    const int main_tris = (count_c - 1) * (count_r - 1) * 2;
    const int skirt_tris = skirt_depth > 0.0 ? 2 * ((count_c - 1) * 2 + (count_r - 1) * 2) : 0;
    godot::PackedInt32Array indices;
    indices.resize((main_tris + skirt_tris) * 3);
    int32_t* i_ptr = indices.ptrw();
    int write = 0;

    for (int rr = 0; rr < count_r - 1; ++rr) {
        for (int cc = 0; cc < count_c - 1; ++cc) {
            const int32_t a = rr * count_c + cc;
            const int32_t b = a + 1;
            const int32_t c = a + count_c;
            const int32_t d = c + 1;
            i_ptr[write++] = a;
            i_ptr[write++] = c;
            i_ptr[write++] = b;
            i_ptr[write++] = b;
            i_ptr[write++] = c;
            i_ptr[write++] = d;
        }
    }

    if (skirt_depth > 0.0) {
        int sv = main_verts;
        auto add_skirt_edge = [&](int start, int stride, int count, bool flip) {
            const int base = sv;
            for (int k = 0; k < count; ++k) {
                const int src = start + k * stride;
                godot::Vector3 p = v_ptr[src];
                p.y -= float(skirt_depth);
                v_ptr[sv] = p;
                n_ptr[sv] = n_ptr[src];
                ++sv;
            }
            for (int k = 0; k < count - 1; ++k) {
                const int32_t top0 = start + k * stride;
                const int32_t top1 = start + (k + 1) * stride;
                const int32_t bot0 = base + k;
                const int32_t bot1 = base + k + 1;
                if (flip) {
                    i_ptr[write++] = top0;
                    i_ptr[write++] = top1;
                    i_ptr[write++] = bot0;
                    i_ptr[write++] = top1;
                    i_ptr[write++] = bot1;
                    i_ptr[write++] = bot0;
                } else {
                    i_ptr[write++] = top0;
                    i_ptr[write++] = bot0;
                    i_ptr[write++] = top1;
                    i_ptr[write++] = top1;
                    i_ptr[write++] = bot0;
                    i_ptr[write++] = bot1;
                }
            }
        };
        // South (rr = 0), North (rr = count_r - 1), West (cc = 0), East (cc = count_c - 1)
        add_skirt_edge(0, 1, count_c, false);
        add_skirt_edge((count_r - 1) * count_c, 1, count_c, true);
        add_skirt_edge(0, count_c, count_r, true);
        add_skirt_edge(count_c - 1, count_c, count_r, false);
    }

    godot::Array arrays;
    arrays.resize(godot::Mesh::ARRAY_MAX);
    arrays[godot::Mesh::ARRAY_VERTEX] = vertices;
    arrays[godot::Mesh::ARRAY_NORMAL] = normals;
    arrays[godot::Mesh::ARRAY_INDEX] = indices;

    mesh.instantiate();
    mesh->add_surface_from_arrays(godot::Mesh::PRIMITIVE_TRIANGLES, arrays);
    return mesh;
}

} // namespace swm::godot_bindings
