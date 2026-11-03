#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include "swg/tre_library.h"

namespace swm::swg {

struct CharacterExport {
    std::string name;                      // output file stem
    std::vector<std::string> appearances;  // .sat paths; the first defines the skeleton
    std::string weapon_lod;                // optional .lod path, attached to "hold_r"
};

// Writes <out_dir>/<name>.gltf, .bin, and one PNG per texture. Uses each
// mesh's highest level of detail. Throws std::runtime_error on failure.
void export_character(TreLibrary& library, const CharacterExport& request, const std::filesystem::path& out_dir,
                      std::ostream& log);

} // namespace swm::swg
