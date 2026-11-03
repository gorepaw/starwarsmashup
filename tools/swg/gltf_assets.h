#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "swg/gltf.h"
#include "swg/static_mesh.h"
#include "swg/tre_library.h"

namespace swm::swg {

// After mirroring X into glTF space, SWG's triangles face inward (verified by
// rendering), so each triangle's winding is reversed.
inline void append_triangle(std::vector<uint32_t>& indices, uint32_t a, uint32_t b, uint32_t c) {
    indices.insert(indices.end(), {a, c, b});
}

// Adds SWG meshes and shaders to a glTF document. A shader becomes a material
// whose main texture is decoded to a PNG in `out_dir`, next to the .gltf.
class GltfAssets {
public:
    GltfAssets(TreLibrary& library, Gltf& gltf, std::filesystem::path out_dir, std::ostream& log)
        : library_(library), gltf_(gltf), out_dir_(std::move(out_dir)), log_(log) {}

    int material_for(const std::string& shader_path);              // cached per shader
    int add_static_mesh(const StaticMesh& mesh, std::string name);  // returns the mesh index

private:
    int image_for(const std::string& texture_path);

    TreLibrary& library_;
    Gltf& gltf_;
    std::filesystem::path out_dir_;
    std::ostream& log_;
    std::map<std::string, int> materials_;
    std::map<std::string, int> images_;
};

// The highest-detail static mesh of an appearance: a mesh (MESH), a level of
// detail list (DTLA), a component appearance (CMPA) whose parts are merged, or
// a redirect (APT) to one of those.
// Throws std::runtime_error for other appearance types.
StaticMesh load_static_appearance(TreLibrary& library, const std::string& path);

} // namespace swm::swg
