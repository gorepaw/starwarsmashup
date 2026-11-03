#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace swm::swg {

// A skeletal appearance template (.sat, SMAT 0003).
struct SkeletalAppearance {
    struct SkeletonRef {
        std::string path;       // .skt
        std::string attach_to;  // joint in the previous skeletons, empty for the root skeleton
    };
    std::vector<std::string> mesh_generators;  // .lmg (or .mgn) paths
    std::vector<SkeletonRef> skeletons;
};

SkeletalAppearance parse_skeletal_appearance(std::span<const uint8_t> bytes);

// Mesh paths of a .lmg file (MLOD 0000), most detailed first.
std::vector<std::string> parse_mesh_lod_list(std::span<const uint8_t> bytes);

// The MAIN texture of a shader (.sht: SSHT, or CSHD wrapping one), with
// normalized slashes. Empty if the shader has none.
std::string shader_main_texture(std::span<const uint8_t> bytes);

} // namespace swm::swg
