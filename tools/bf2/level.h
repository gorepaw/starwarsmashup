#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "bf2/ucfb.h"

namespace swm::bf2 {

// A Battlefront II map (`<map>.lvl`, e.g. dag/dag1.lvl): terrain, world
// layers of object instances and regions, and paths. Sublevels ("lvl_"
// chunks) hold the game modes' layers (conquest, CTF, ...) and era props;
// they're read too. Coordinates are Battlefront's (left-handed, Y up, like
// Galaxies): game x = -x.

// A terrain cutter ("CUTR" in the terrain's "CUTS"): a convex volume where
// the terrain isn't (a bunker's door into a hill, a pit): a point is inside
// when n . p + d <= 0 for every plane (n, d). World-layer coordinates.
struct TerrainCut {
    std::array<float, 3> center{};  // its bounding sphere
    float radius = 0;
    std::vector<std::array<float, 4>> planes;  // (nx, ny, nz, d)
};

// The terrain: a square grid of heights. Munged terrain is stored as 8x8-cell
// patches of vertices; heights are read from the vertices on grid points.
// Each patch also lists the texture layers it uses (up to three), and a
// compressed copy of its vertices carries their weights.
struct Terrain {
    std::string name;
    int cells = 0;              // per side
    double unit = 1;            // meters per cell
    std::vector<float> heights; // (cells + 1)^2, rows along +Z, x fastest
    int missing = 0;            // grid points no patch vertex covered (holes); filled from neighbours
    // Texture layers by slot ("" for an unused slot before a used one), with
    // their tiling (SCAL; ASSUMPTION: meters per texture repeat).
    std::vector<std::string> texture_layers;
    std::vector<float> texture_scales;
    std::string detail_texture;  // DTLX, tiled over everything
    // Per grid point (same order as heights): each layer's weight (0..255,
    // summing to 255) and the baked lighting color (RGB).
    std::vector<std::vector<uint8_t>> layer_weights;  // [layer][point]
    std::vector<std::array<uint8_t, 3>> light;
    std::vector<TerrainCut> cuts;

    // Coordinates of grid point (0, 0): the grid is centred on the origin.
    double origin() const { return -cells * unit / 2; }
};

// One record of a munged config ("DATA" chunk): a name hash, a value count,
// then 4-byte values: floats, except that a string's value is the offset (from
// the first value) of its length and text, stored after the values. Only a
// first value can be a string here; the numbers after it are kept.
struct ConfigRecord {
    uint32_t name = 0;
    std::vector<float> numbers;
    std::string text;
};
ConfigRecord read_config_record(std::span<const uint8_t> data);

// A world layer's object or region, with its instance properties.
struct WorldItem {
    std::string layer;
    uint32_t sublevel = 0;  // the enclosing sublevel's name hash, or 0: a base layer's
    std::string type;   // object class (e.g. "dag1_prop_grandtree"), or region shape ("box", "sphere", "cylinder")
    std::string name;
    std::array<float, 9> rotation{};  // 3x3, its columns as stored (see bf2/model.h)
    std::array<float, 3> position{};
    std::array<float, 3> size{};      // regions only
    std::vector<std::pair<uint32_t, std::string>> properties;  // name hash, value

    // Last value of a property by name, or empty.
    std::string get(std::string_view property) const;
};

// An AI barrier ("BARR" in a world layer): a rectangle on the ground the
// AI keeps out of, for the classes in `flags` (1 soldiers, 2 hover, 4 small,
// 8 medium, 16 huge, 32 flyers).
struct LevelBarrier {
    std::string layer;
    uint32_t sublevel = 0;  // as WorldItem's
    std::string name;
    std::array<float, 9> rotation{};  // 3x3, its columns as stored
    std::array<float, 3> position{};
    std::array<float, 2> half_size{};  // along its local x and z
    uint32_t flags = 0;

    // Corners in order, in world-layer coordinates (x, z).
    std::array<std::array<float, 2>, 4> corners() const;
};

struct PathNode {
    std::array<float, 3> position{};  // in world-layer coordinates
    std::array<float, 4> rotation{};  // as stored (w, x, y, z; not converted)
};

// Paths are grouped by the sublevel (or base layer) they belong to: game
// modes reuse names like "cp1_spawn".
struct LevelPath {
    std::string name;
    uint32_t group = 0;  // the sublevel's name hash, or the base layer's name hash
    std::vector<PathNode> nodes;
};

// A light from the map's lighting ("lght"); its position is converted to
// world-layer coordinates.
struct LevelLight {
    std::string name;
    int type = 0;  // 1 directional, 2 omni, 3 spot
    std::array<float, 4> rotation{1, 0, 0, 0};  // as stored (w, x, y, z), in the paths' frame (z negated)
    std::array<float, 3> position{};
    std::array<float, 3> color{1, 1, 1};        // 0..1
    float range = 0;
    // Baked into the terrain's and props' lighting ("Static"). Otherwise the
    // engine lights them with it as it draws (Endor's sun, not Dagobah's).
    bool baked = false;
};

// The sky ("sky_") and global lighting. Colors are 0..255, as stored.
struct LevelSky {
    std::array<float, 3> fog_color{};
    std::vector<float> near_scene_range;  // as stored: (20, 30, 60, 80) on Dagobah
    std::string dome_texture;
    std::vector<std::string> dome_models;
    std::array<float, 3> ambient_top{}, ambient_bottom{};  // hemisphere ambient light
    std::string sun;  // the global light's name (GlobalLights' Light1)
};

// Water ("WATR" in the terrain): a grid of square cells centred on the map,
// each naming a water layer or none, and the layers' flat surfaces. The grid
// may reach past the terrain (Dagobah's: 128 cells of 16 m over a 1024 m
// terrain).
struct WaterLayer {
    std::string name;
    float height = 0;                    // the surface's y
    std::array<float, 2> velocity{};     // texture scrolling (u, v)
    std::array<float, 2> repeat{1, 1};   // texture repeats (u, v)
    std::array<uint8_t, 4> color{};      // RGBA
};

struct LevelWater {
    int cells = 0;           // per side
    float cell_size = 0;     // meters
    std::array<float, 2> origin{};  // (x, z) of the grid's -X,-Z corner, in world-layer coordinates
    std::vector<WaterLayer> layers;
    std::vector<uint8_t> map;  // cells^2, rows along +Z, x fastest: a layer number (1-based), 0 for none

    // The layer of cell (column, row), or nullptr.
    const WaterLayer* at(int column, int row) const;
};

// A map-wide effect from the world's effects ("fx__": Effect("Water"),
// Effect("FogCloud"), ...): its properties as stored.
struct LevelEffect {
    std::string name;
    std::vector<ConfigRecord> properties;

    const ConfigRecord* get(std::string_view property) const;
};

struct Level {
    Terrain terrain;
    LevelWater water;
    std::vector<LevelEffect> effects;
    LevelSky sky;
    std::vector<LevelLight> lights;
    std::vector<WorldItem> objects;
    std::vector<WorldItem> regions;
    std::vector<LevelBarrier> barriers;
    std::vector<LevelPath> paths;
    std::vector<std::string> layers;  // world layer names, in file order
    std::map<std::string, uint32_t> layer_groups;  // layer -> its paths' group
    // Each sublevel's layers, by the sublevel's name hash, in file order. A
    // layer can be in several (a game mode's posts shared by two modes).
    std::vector<std::pair<uint32_t, std::vector<std::string>>> sublevels;

    // A region by the name objects refer to it by (a capture region's,
    // a script's death region's): its ID or its Name property, ignoring
    // case. Prefers one in `sublevel` (a hash, or 0: a base layer).
    const WorldItem* region(std::string_view name, uint32_t sublevel = 0) const;
    const LevelEffect* effect(std::string_view name) const;  // by name, ignoring case
    // A path by name, preferring the one belonging with `layer`.
    const LevelPath* path(std::string_view name, std::string_view layer = {}) const;
    // A path by name, preferring the one in group `group` (a sublevel's
    // hash, or a base layer's name hash).
    const LevelPath* path_in(std::string_view name, uint32_t group) const;
};

// Reads a map file's tree. Throws std::runtime_error on malformed data.
Level read_level(const UcfbNode& root);

} // namespace swm::bf2
