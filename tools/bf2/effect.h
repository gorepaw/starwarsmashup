#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "bf2/ucfb.h"

namespace swm::bf2 {

// Battlefront II's fx__ particle effect chunks (in ingame.lvl and side/*.lvl).
//
// Each fx__ chunk holds a NAME (a 4-byte hash of the effect's name using
// FNV-1a with `c | 0x20`, which differs from odf_hash on underscores) and a
// DATA/SCOP config tree of one or more ParticleEmitters (sometimes nested).
// Each ParticleEmitter carries emission counts/delays, a Spawner (initial
// shape, velocity, size, RGBA or HSVA color, rotation), a Transformer
// (lifetime, acceleration, chained Size and Color keyframes via Next/Reach/
// Move), and a Geometry block (BlendMode, Type: PARTICLE/SPARK/EMITTER,
// SparkLength, Texture).

// 32-bit FNV-1a over `c | 0x20`, matching Battlefront II's fx__ NAME chunks.
uint32_t effect_name_hash(std::string_view name);

struct EffectEmitter {
    std::string name;
    int max_particles = 1;                  // -1: continuous (trail); > 0: one-shot burst
    double start_delay = 0;                 // seconds
    std::array<double, 2> burst_delay{0.05, 0.05};
    std::array<double, 2> burst_count{1, 1};
    // Spawner
    std::string shape = "circle";           // "circle" or "spread"
    std::array<double, 3> pos_min{0, 0, 0};
    std::array<double, 3> pos_max{0, 0, 0};
    std::array<double, 3> offset_min{0, 0, 0};
    std::array<double, 3> offset_max{0, 0, 0};
    std::array<double, 2> pos_scale{0, 0};
    std::array<double, 2> vel_scale{0, 0};
    double inherit_velocity = 0;
    std::array<double, 2> size{0.5, 0.5};   // initial particle diameter (m)
    std::array<double, 4> color_min{1, 1, 1, 1};  // RGBA 0..1
    std::array<double, 4> color_max{1, 1, 1, 1};  // RGBA 0..1
    std::array<double, 2> start_rotation{0, 360};       // degrees
    std::array<double, 2> rotation_velocity{0, 0};      // degrees/s
    // Transformer
    double lifetime = 1.0;                  // seconds
    std::array<double, 3> acceleration{0, 0, 0};  // m/s^2 (gravity)
    bool damped = false;                    // Position.Scale == 0 (velocity decays to 0)
    // Normalized time (0..1) -> size multiplier (starts at [0, 1]).
    std::vector<std::array<double, 2>> size_curve;
    // Normalized time (0..1) -> RGBA (0..1).
    std::vector<std::array<double, 5>> color_curve;
    // Geometry
    std::string blend = "alpha";            // "alpha" or "add"
    std::string type = "particle";          // "particle" or "spark"
    double spark_length = 0;                // seconds of velocity stretch for sparks
    std::string texture;                    // lowercase texture name
};

// Reads a Battlefront II `fx__` chunk into its drawable particle emitters
// (flattening nested emitters and skipping container-only "EMITTER" shells).
std::vector<EffectEmitter> read_effect(const UcfbNode& fx_chunk);

// Writes the emitters as JSON for game/rendering/bf2_effects.gd.
void write_effect_json(std::ostream& out, const std::vector<EffectEmitter>& emitters);

} // namespace swm::bf2
