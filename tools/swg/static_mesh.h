#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "common/math.h"

namespace swm::swg {

struct Hardpoint {
    std::string name;
    Mat4 transform;  // relative to the mesh (SWG space)
};

// A static mesh (.msh, MESH 0004/0005): one part per shader.
struct StaticMesh {
    struct Part {
        std::string shader;  // .sht path
        std::vector<Vec3> positions;
        std::vector<Vec3> normals;               // empty if the vertex format has none
        std::vector<std::array<float, 2>> uv;    // empty if no texture coordinates
        std::vector<uint32_t> indices;           // triangle list
    };

    std::vector<Part> parts;
    std::vector<Hardpoint> hardpoints;
};

// Throws std::runtime_error on unsupported or malformed data.
StaticMesh parse_static_mesh(std::span<const uint8_t> bytes);

// Path of the highest-detail mesh in a .lod file (DTLA), relative to the
// "appearance/" directory as stored (e.g. "mesh/foo_l0.msh").
std::string lod_highest_detail(std::span<const uint8_t> bytes);

} // namespace swm::swg
