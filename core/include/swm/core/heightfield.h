#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
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

// Where the terrain is taken away on a fine grid over part of it (game x
// and z), as a building's rooms are: in Galaxies a room is a space of its
// own with no terrain in it, and some floors lie under the ground (the
// cantina's main room, a meter down). Written by `swgtool export-city` next
// to the bake (<prefix>.holes).
struct TerrainHoles {
    double x0 = 0, z0 = 0;  // game x and z of cell (0, 0)'s corner
    double cell = 0.25;     // meters
    int width = 0, height = 0;  // cells along +x and +z
    // Rows along +z: 0 where the terrain is, 1 where it's taken away (not
    // there to stand on, nor drawn), 2 where it's hidden: drawn away only,
    // still there to stand on (over a room's floor where no one walks: along
    // its walls' foot, a bar's footprint).
    std::vector<uint8_t> cells;

    bool empty() const { return cells.empty(); }
    // Whether the terrain is taken away there (a hidden cell's is still there).
    bool at(double x, double z) const;
    void set(int column, int row) { cells[size_t(row) * size_t(width) + size_t(column)] = 1; }
    void hide(int column, int row) {
        uint8_t& c = cells[size_t(row) * size_t(width) + size_t(column)];
        c = c == 0 ? 2 : c;
    }
};

void save_terrain_holes(const TerrainHoles& holes, const std::filesystem::path& path);
// Throws std::runtime_error on a missing or malformed file. A file of
// several patches (one per town: save_terrain_hole_patches) gives its first.
TerrainHoles load_terrain_holes(const std::filesystem::path& path);
// A planet's towns are kilometers apart, so their rooms' holes are a patch
// each, written one after another (each as save_terrain_holes writes one).
void save_terrain_hole_patches(const std::vector<TerrainHoles>& patches, const std::filesystem::path& path);
std::vector<TerrainHoles> load_terrain_hole_patches(const std::filesystem::path& path);

// Where water stands (a planet's lakes, rivers and sea: swgtool writes a
// terrain's water tables to <prefix>.water): a surface height over an
// outline, in game space. Water fills it wherever the ground is lower.
struct WaterTable {
    std::string name;
    double height = 0;
    std::vector<std::array<double, 2>> outline;  // x z
    double min_x = 0, min_z = 0, max_x = 0, max_z = 0;

    bool contains(double x, double z) const;
};
std::vector<WaterTable> load_water_tables(const std::filesystem::path& path);

class Heightfield {
public:
    Heightfield() = default;
    // `origin_x`/`origin_z` are the Galaxies coordinates of sample (0, 0).
    Heightfield(int samples, double spacing, double origin_x, double origin_z, std::vector<float> heights);

    bool empty() const { return heights_.empty(); }
    int samples() const { return samples_; }
    double spacing() const { return spacing_; }
    double origin_x() const { return origin_x_; }
    double origin_z() const { return origin_z_; }
    const std::vector<float>& heights() const { return heights_; }

    // Height at a game-space position. Positions outside the grid use the
    // nearest edge.
    double height_at(double x, double z) const;
    // Upward unit normal of the triangle under a game-space position.
    Vec3 normal_at(double x, double z) const;
    // Clamps a game-space position's x and z to the grid.
    Vec3 clamp(Vec3 position) const;
    // Whether the terrain is cut away at a game-space position: its surface
    // there is inside a cut, or it's in a hole. There, it neither holds
    // anyone up nor stops anything.
    bool cut_at(double x, double z) const;
    // Whether a building's hole (TerrainHoles, not a cut) is at a position.
    bool hole_at(double x, double z) const;
    const std::vector<TerrainCut>& cuts() const { return cuts_; }
    void set_cuts(std::vector<TerrainCut> cuts) { cuts_ = std::move(cuts); }
    // The first patch of holes (empty if none).
    const TerrainHoles& holes() const { return holes_.empty() ? no_holes_ : holes_.front(); }
    const std::vector<TerrainHoles>& hole_patches() const { return holes_; }
    void set_holes(TerrainHoles holes) { holes_ = {std::move(holes)}; }
    void set_hole_patches(std::vector<TerrainHoles> patches) { holes_ = std::move(patches); }
    // The lowest height in the grid.
    double lowest() const;

    const std::vector<WaterTable>& water() const { return water_; }
    void set_water(std::vector<WaterTable> tables) { water_ = std::move(tables); }
    // The water's surface at a game-space position, if water stands there
    // over the terrain: the highest table holding the point (Theed's river
    // at 3 m lies inside Naboo's ocean at -210), if the terrain is lower.
    std::optional<double> water_at(double x, double z) const;

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
    std::vector<TerrainHoles> holes_;
    std::vector<WaterTable> water_;
    inline static const TerrainHoles no_holes_{};
};

// Loads `<prefix>.r32` and the grid layout from `<prefix>.json`, as written by
// `swgtool bake-terrain` (or `bf2tool export-map`), and its cuts from
// `<prefix>.cuts` if there is one (a line per cut: center x y z, radius, then
// each plane's nx ny nz d; game space), its holes from `<prefix>.holes`
// and its water from `<prefix>.water` if there are any. Throws std::runtime_error on failure.
Heightfield load_bake(const std::filesystem::path& prefix);

} // namespace swm::core
