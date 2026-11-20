#pragma once

#include <array>
#include <filesystem>
#include <vector>

#include "swm/core/vec3.h"

namespace swm::core {

// Terrain heights on a square grid, as baked by `swgtool bake-terrain`. The
// grid is laid out in Galaxies coordinates: rows along +Z, columns along
// Galaxies +X (game -X). Heights between samples follow the same two
// triangles per cell as the rendered mesh and the collision built from it,
// so characters stand exactly on what's drawn.
// A hole in the terrain (Battlefront's terrain cutters: a bunker's door
// into a hill): a convex volume, game space. A point is inside when
// n . p + d <= 0 for every plane.
struct TerrainCut {
    Vec3 center;  // its bounding sphere
    double radius = 0;
    std::vector<std::array<double, 4>> planes;  // (nx, ny, nz, d)

    bool contains(Vec3 point) const;
};

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
    // Whether the terrain is cut away at a game-space position: its surface
    // there is inside a cut. There, it neither holds anyone up nor stops
    // anything.
    bool cut_at(double x, double z) const;
    const std::vector<TerrainCut>& cuts() const { return cuts_; }
    void set_cuts(std::vector<TerrainCut> cuts) { cuts_ = std::move(cuts); }
    // The lowest height in the grid.
    double lowest() const;

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
    std::vector<TerrainCut> cuts_;
};

// Loads `<prefix>.r32` and the grid layout from `<prefix>.json`, as written by
// `swgtool bake-terrain` (or `bf2tool export-map`), and its cuts from
// `<prefix>.cuts` if there is one (a line per cut: center x y z, radius, then
// each plane's nx ny nz d; game space). Throws std::runtime_error on failure.
Heightfield load_bake(const std::filesystem::path& prefix);

} // namespace swm::core
