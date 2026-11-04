#pragma once

#include <filesystem>
#include <vector>

#include "swm/core/vec3.h"

namespace swm::core {

// Terrain heights on a square grid, as baked by `swgtool bake-terrain`. The
// grid is laid out in Galaxies coordinates: rows along +Z, columns along
// Galaxies +X (game -X). Heights between samples follow the same two
// triangles per cell as the rendered mesh and the collision built from it,
// so characters stand exactly on what's drawn.
class Heightfield {
public:
    Heightfield() = default;
    // `origin_x`/`origin_z` are the Galaxies coordinates of sample (0, 0).
    Heightfield(int samples, double spacing, double origin_x, double origin_z, std::vector<float> heights);

    bool empty() const { return heights_.empty(); }
    int samples() const { return samples_; }
    double spacing() const { return spacing_; }

    // Height at a game-space position. Positions outside the grid use the
    // nearest edge.
    double height_at(double x, double z) const;
    // Upward unit normal of the triangle under a game-space position.
    Vec3 normal_at(double x, double z) const;
    // Clamps a game-space position's x and z to the grid.
    Vec3 clamp(Vec3 position) const;

private:
    struct Triangle {
        Vec3 a, b, c;  // game space
    };
    Triangle triangle_at(double x, double z) const;
    Vec3 point(int column, int row) const;

    int samples_ = 0;
    double spacing_ = 1;
    double origin_x_ = 0;
    double origin_z_ = 0;
    std::vector<float> heights_;
};

// Loads `<prefix>.r32` and the grid layout from `<prefix>.json`, as written by
// `swgtool bake-terrain`. Throws std::runtime_error on failure.
Heightfield load_bake(const std::filesystem::path& prefix);

} // namespace swm::core
