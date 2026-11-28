#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "common/math.h"

namespace swbfo::swg {

// A skinned mesh generator (.mgn, SKMG 0004). Positions and normals are shared
// tables; each shader part indexes into them per vertex.
struct SkinnedMesh {
    struct Part {
        std::string shader;                       // .sht path
        std::vector<uint32_t> position_index;     // per vertex
        std::vector<uint32_t> normal_index;       // per vertex
        std::vector<std::array<float, 2>> uv;     // per vertex, first texture coordinate set
        std::vector<std::array<uint32_t, 3>> triangles;
        std::vector<int16_t> triangle_occlusion;  // per triangle (OITL), empty for ITL
    };

    std::vector<std::string> skeletons;  // skeleton template paths
    std::vector<std::string> bones;      // transform names, referenced by weights
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::vector<std::pair<uint32_t, float>>> weights;  // per position: (bone, weight)
    std::vector<Part> parts;
    // Occlusion (OZN, FOZC, OZC, ZTO): what worn things hide of what's under
    // them. A mesh's zones by name (its own table: "chest", "l_arm"...);
    // each triangle's OITL entry is a combination (a list of zones, indices
    // into zone_combinations). Worn over it (a mesh of a higher layer
    // occluding them), a triangle whose zones are all covered isn't drawn,
    // nor the whole mesh once all its fully_occluded zones are.
    int16_t occlusion_layer = 0;
    std::vector<std::string> occlusion_zones;
    std::vector<std::string> fully_occluded;               // FOZC, by name
    std::vector<std::vector<std::string>> zone_combinations;  // OZC, by name
    std::vector<std::string> occludes;                     // ZTO, by name

    // Textures composited at runtime (TRTS): a texture renderer (.trt) whose
    // result replaces a part's shader's texture (by tag, e.g. MAIN). Faces:
    // skin, brows, makeup, freckles, a Zabrak's tattoos.
    struct TextureRenderer {
        std::string path;  // texturerenderer/....trt
        std::vector<std::pair<int, std::string>> targets;  // (part, texture tag)
    };
    std::vector<TextureRenderer> texture_renderers;

    struct BlendTarget {
        std::string name;
        std::vector<std::pair<uint32_t, Vec3>> position_deltas;  // (position_index, delta)
    };
    std::vector<BlendTarget> blend_targets;
};

// Throws std::runtime_error on unsupported or malformed data.
SkinnedMesh parse_skinned_mesh(std::span<const uint8_t> bytes);

} // namespace swbfo::swg
