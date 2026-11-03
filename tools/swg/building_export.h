#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

#include "swg/tre_library.h"

namespace swm::swg {

// Exports a portalized building (.pob) to <out_dir>/<name>.gltf, .bin, one PNG
// per texture, and <name>.json.
//
// glTF: a root node <name> with one child per cell, named after the cell.
// Each cell node holds its render mesh "<cell>_mesh" and, if the cell has a
// floor, two collision meshes without materials: "<cell>_floor" (walkable
// triangles) and "<cell>_walls" (3 m quads rising from the floor's
// uncrossable boundary edges; doorways and exits to open ground stay open).
//
// JSON sidecar (glTF coordinates): portals (doorway polygons), and per cell
// its portal links (target cell, door style) and lights (ambient, parallel,
// point; color, position, the direction the light travels, attenuation).
//
// Not handled yet: doors (door styles are listed, but no door objects are
// placed) and collision meshes other than floors. Throws std::runtime_error
// on failure.
void export_building(TreLibrary& library, const std::string& pob_path, const std::string& name,
                     const std::filesystem::path& out_dir, std::ostream& log);

} // namespace swm::swg
