#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace swm::swg {

// SWG procedural terrain (.trn, PTAT 0014): a tree of layers. Each layer has
// boundaries (where it applies, with feathered edges), filters (conditions on
// height, slope, or a fractal), and affectors (what it does), applied in file
// order together with nested sub-layers.
//
// Only height generation is implemented; shader, flora, color, and
// environment affectors are parsed but ignored.

enum class Feather { linear = 0, ease_in = 1, ease_out = 2, ease_in_out = 3 };

// A fractal family (MFRC 0001): multi-octave noise mapped to [0, 1].
//
// APPROXIMATE: the parameters are decoded from the file, but the underlying
// noise function is a standard seeded gradient noise, not SWG's own, so
// fine detail differs from the original game. Everything built from
// boundaries, filters, and constant/terrace affectors is unaffected.
struct Fractal {
    int32_t seed = 0;
    bool use_bias = false;
    float bias = 0.5f;
    bool use_gain = false;
    float gain = 0.5f;
    int32_t octaves = 1;
    float octave_frequency = 2.0f;  // frequency multiplier per octave
    float octave_amplitude = 0.5f;  // amplitude multiplier per octave
    float frequency_x = 1, frequency_z = 1;
    float offset_x = 0, offset_z = 0;
    int32_t combination = 0;  // 0 add, 1 multiply, 2 crest, 3 turbulence, 4 crest clamp, 5 turbulence clamp

    void initialize();  // builds the permutation table from the seed
    double sample(double x, double z) const;

private:
    double noise(double x, double z) const;  // gradient noise, roughly [-1, 1]
    std::array<uint8_t, 512> permutation_{};
};

struct TerrainBoundary {
    enum class Kind { circle, rectangle, polygon, polyline } kind = Kind::circle;
    std::vector<std::array<double, 2>> points;  // polygon/polyline (x, z)
    double center_x = 0, center_z = 0, radius = 0;
    double min_x = 0, min_z = 0, max_x = 0, max_z = 0;
    double width = 0;  // polyline
    Feather feather = Feather::linear;
    double feather_amount = 0;  // circle/rectangle/polyline: fraction of size; polygon: meters

    double amount(double x, double z) const;  // 0 outside, 1 inside, feathered between
};

struct TerrainFilter {
    enum class Kind { height, slope, fractal, ignored } kind = Kind::ignored;
    double min = 0, max = 0;
    Feather feather = Feather::linear;
    double feather_amount = 0;  // fraction of the [min, max] range
    int32_t fractal_id = 0;
    double fractal_scale = 1;
};

struct TerrainAffector {
    enum class Kind { height_constant, height_fractal, height_terrace } kind = Kind::height_constant;
    // 0 replace, 1 add, 2 subtract, 3 multiply. Inferred from Tatooine's layer
    // names: op 0 is used to flatten ("START-flat" 195, "Dune_Seas" 0), op 1 to
    // raise ("Xelric Raise" 200, "Top_Mesa" 70), op 2 to cut ("Ebe_subtract"
    // 100, "Craters" 3). Op 3 is unused on Tatooine; multiply is a guess.
    int32_t operation = 0;
    double value = 0;       // constant height, fractal scale, or terrace step
    int32_t fractal_id = 0;
    double terrace_fraction = 0;
};

struct TerrainLayer {
    std::string name;
    bool enabled = true;
    bool invert_boundaries = false;
    bool invert_filters = false;
    std::vector<TerrainBoundary> boundaries;
    std::vector<TerrainFilter> filters;
    // File order: an affector, or (affector empty) the next child layer.
    struct Step {
        bool is_layer = false;
        TerrainAffector affector;
        size_t layer = 0;  // index into children
    };
    std::vector<Step> steps;
    std::vector<TerrainLayer> children;
};

struct ProceduralTerrain {
    double map_size = 16384;
    std::map<int32_t, Fractal> fractals;
    std::vector<TerrainLayer> layers;
};

ProceduralTerrain parse_procedural_terrain(std::span<const uint8_t> bytes);

// Heights on a square grid centered at (center_x, center_z), `samples` per
// side, `spacing` meters apart. Row-major, rows along +Z.
struct HeightGrid {
    double origin_x = 0, origin_z = 0;  // world position of sample (0, 0)
    double spacing = 1;
    int samples = 0;
    std::vector<float> heights;

    float at(int column, int row) const { return heights[size_t(row) * size_t(samples) + size_t(column)]; }
    float sample_world(double x, double z) const;  // bilinear; clamps to the grid
};

HeightGrid bake_heights(const ProceduralTerrain& terrain, double center_x, double center_z, int samples, double spacing);

} // namespace swm::swg
