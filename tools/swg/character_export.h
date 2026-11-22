#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include "swg/animation_clip.h"
#include "swg/skeleton.h"
#include "swg/tre_library.h"

namespace swm::swg {

struct CharacterExport {
    std::string name;                      // output file stem
    std::vector<std::string> appearances;  // .sat paths; the first defines the skeleton
    std::vector<std::string> meshes;       // further meshes worn (.lmg or .mgn: clothes, hair)
    std::string weapon_lod;                // optional .lod path, attached to "hold_r"
    std::vector<AnimationClip> clips;      // optional, for the skeleton of appearances[0]
};

// The combined skeleton of a .sat (its root skeleton plus attached ones, such
// as a face skeleton on the head joint). Joint order matches the export.
Skeleton load_appearance_skeleton(TreLibrary& library, const std::string& sat_path);

// Writes <out_dir>/<name>.gltf, .bin, and one PNG per texture. Uses each
// mesh's highest level of detail. Throws std::runtime_error on failure.
//
// And <name>.colors.json: its customizable materials' colors, for whoever
// draws it to choose (a material's base color is its default MAIN color):
//   "palettes": {path: "rrggbb..." (each entry)},
//   "materials": {name: {"MAIN": factor, "HUEB": factor (two-color only)}}
// where a factor is {"variable", "shared", "palette", "index" (default)},
// or a fixed "rrggbb". A two-color material is the texture times MAIN
// blended to HUEB by the texture's alpha.
//
// A material whose texture a texture renderer composites (a face's: see
// TextureRenderer) has "layers" instead, in drawing order: {"rect", "uv",
// "blend" (else copied), "textures" (PNGs next to the model: the choices of
// "style", a style variable, or one), "color" (a factor; its default index
// the same variable's in the body's shaders, else 0)}.
//
// A garment's bare skin (an owner proxy shader, "body") is drawn with the
// body's own material.
void export_character(TreLibrary& library, const CharacterExport& request, const std::filesystem::path& out_dir,
                      std::ostream& log);

} // namespace swm::swg
