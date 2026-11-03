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
    std::string weapon_lod;                // optional .lod path, attached to "hold_r"
    std::vector<AnimationClip> clips;      // optional, for the skeleton of appearances[0]
};

// The combined skeleton of a .sat (its root skeleton plus attached ones, such
// as a face skeleton on the head joint). Joint order matches the export.
Skeleton load_appearance_skeleton(TreLibrary& library, const std::string& sat_path);

// Writes <out_dir>/<name>.gltf, .bin, and one PNG per texture. Uses each
// mesh's highest level of detail. Throws std::runtime_error on failure.
void export_character(TreLibrary& library, const CharacterExport& request, const std::filesystem::path& out_dir,
                      std::ostream& log);

} // namespace swm::swg
