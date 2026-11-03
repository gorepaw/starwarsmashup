#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "swg/math.h"

namespace swm::swg {

// Minimal glTF 2.0 writer: one .gltf (JSON) plus one .bin buffer. Images are
// referenced by URI and written by the caller.
class Gltf {
public:
    struct Node {
        std::string name;
        std::optional<Vec3> translation;
        std::optional<Quat> rotation;
        int mesh = -1;
        int skin = -1;
        std::vector<int> children;
    };
    struct Primitive {
        std::map<std::string, int> attributes;  // attribute name -> accessor
        int indices = -1;
        int material = -1;
    };
    struct Mesh {
        std::string name;
        std::vector<Primitive> primitives;
    };
    struct Skin {
        std::vector<int> joints;
        int inverse_bind_matrices = -1;
        int skeleton = -1;
    };
    struct Material {
        std::string name;
        int texture = -1;  // index into images (one texture per image)
    };

    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<Skin> skins;
    std::vector<Material> materials;
    std::vector<std::string> images;  // URIs
    std::vector<int> scene_nodes;

    int add_vec2(const std::vector<std::array<float, 2>>& values);
    int add_vec3(const std::vector<Vec3>& values, bool with_bounds);
    int add_vec4(const std::vector<std::array<float, 4>>& values);
    int add_joints(const std::vector<std::array<uint16_t, 4>>& values);
    int add_indices(const std::vector<uint32_t>& values);
    int add_matrices(const std::vector<Mat4>& values);

    // Writes `path` and the buffer next to it (same stem, .bin).
    void write(const std::filesystem::path& path) const;

private:
    struct Accessor {
        size_t offset = 0;
        size_t length = 0;
        int target = 0;  // 34962 vertex data, 34963 indices, 0 none
        int component_type = 0;
        size_t count = 0;
        std::string type;
        std::string min, max;  // JSON arrays, POSITION only
    };

    int add_accessor(const void* data, size_t bytes, int target, int component_type, size_t count, std::string type);

    std::vector<uint8_t> buffer_;
    std::vector<Accessor> accessors_;
};

} // namespace swm::swg
