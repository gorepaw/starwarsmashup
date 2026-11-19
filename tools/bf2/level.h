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

// The terrain: a square grid of heights. Munged terrain is stored as 8x8-cell
// patches of vertices; heights are read from the vertices on grid points.
struct Terrain {
    std::string name;
    int cells = 0;              // per side
    double unit = 1;            // meters per cell
    std::vector<float> heights; // (cells + 1)^2, rows along +Z, x fastest
    int missing = 0;            // grid points no patch vertex covered (holes); filled from neighbours
    std::vector<std::string> texture_layers;

    // Coordinates of grid point (0, 0): the grid is centred on the origin.
    double origin() const { return -cells * unit / 2; }
};

// A world layer's object or region, with its instance properties.
struct WorldItem {
    std::string layer;
    std::string type;   // object class (e.g. "dag1_prop_grandtree"), or region shape ("box", "sphere", "cylinder")
    std::string name;
    std::array<float, 9> rotation{};  // 3x3, rows as stored
    std::array<float, 3> position{};
    std::array<float, 3> size{};      // regions only
    std::vector<std::pair<uint32_t, std::string>> properties;  // name hash, value

    // Last value of a property by name, or empty.
    std::string get(std::string_view property) const;
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

struct Level {
    Terrain terrain;
    std::vector<WorldItem> objects;
    std::vector<WorldItem> regions;
    std::vector<LevelPath> paths;
    std::vector<std::string> layers;  // world layer names, in file order
    std::map<std::string, uint32_t> layer_groups;  // layer -> its paths' group

    const WorldItem* region(std::string_view name) const;
    // A path by name, preferring the one belonging with `layer`.
    const LevelPath* path(std::string_view name, std::string_view layer = {}) const;
};

// Reads a map file's tree. Throws std::runtime_error on malformed data.
Level read_level(const UcfbNode& root);

// One record of a munged config ("DATA" chunk): a name hash, then values.
// Strings are stored inline; numbers as floats.
struct ConfigRecord {
    uint32_t name = 0;
    std::vector<float> numbers;
    std::string text;
};
ConfigRecord read_config_record(std::span<const uint8_t> data);

} // namespace swm::bf2
