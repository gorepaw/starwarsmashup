#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include "common/math.h"
#include "swg/animation_clip.h"
#include "swg/skeleton.h"
#include "swg/tre_library.h"

namespace swbfo::swg {

struct CharacterExport {
    std::string name;                      // output file stem
    std::vector<std::string> appearances;  // .sat paths; the first defines the skeleton
    std::vector<std::string> meshes;       // further meshes worn (.lmg or .mgn: clothes, hair)
    std::string weapon_lod;                // optional .lod path, attached to "hold_r"
    std::vector<AnimationClip> clips;      // optional, for the skeleton of appearances[0]
    bool export_appearance_meshes = true;  // false for standalone attachments (e.g. hair)
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
//
// What's worn hides what it covers (Galaxies' occlusion zones): exported
// together, a mesh's covered triangles are left out. For the client to do the
// same with things it puts on later, <name>.occlusion.json (if its meshes
// have zones): {"meshes": [{"node" (the mesh's node name), "layer",
// "occludes" (the zones it covers), "fully" (all covered, it's hidden whole),
// "combinations" (lists of zones), "surfaces" (per primitive, each triangle's
// combination, -1 for none)}]}. A triangle whose combination's zones are all
// covered by meshes of a higher layer isn't drawn.
//
// Returns the bounds of its meshes in their bind pose (glTF space).
struct CharacterBounds {
    Vec3 min{1e30f, 1e30f, 1e30f}, max{-1e30f, -1e30f, -1e30f};
};
CharacterBounds export_character(TreLibrary& library, const CharacterExport& request,
                                 const std::filesystem::path& out_dir, std::ostream& log);

} // namespace swbfo::swg
