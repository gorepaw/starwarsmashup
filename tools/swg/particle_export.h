#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

#include "swg/tre_library.h"

namespace swm::swg {

// Writes a particle effect (.prt) as <out_dir>/<stem>.json, and its
// textures as PNGs beside it, for the client to draw (game/rendering/
// swg_particles.gd); returns the JSON's file name. Its emitters, each:
//   {"name", "rate" (particles a second), "speed" (m/s), "lifetime": [low,
//    high] (s), "gravity" (times the world's), "radius" (m), "spread"
//    (degrees), "color": [[t, r, g, b]...], "alpha", "speed_scale",
//    "size": [[t, value]...] (over a particle's life), "texture" (PNG),
//    "frames", "grid", "fps" (a sprite sheet), "blend" ("alpha" or "add")}
// (see ParticleEmitter for what's assumed). Throws std::runtime_error if it
// can't be read.
std::string export_particle_effect(TreLibrary& library, const std::string& prt_path,
                                   const std::filesystem::path& out_dir, std::ostream& log);

} // namespace swm::swg
