#include "swg/gltf.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace swm::swg {
namespace {

constexpr int component_float = 5126;
constexpr int component_u16 = 5123;
constexpr int component_u32 = 5125;
constexpr int target_vertices = 34962;
constexpr int target_indices = 34963;

std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

std::string int_list(const std::vector<int>& values) {
    std::string out = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        out += (i ? "," : "") + std::to_string(values[i]);
    }
    return out + "]";
}

} // namespace

int Gltf::add_accessor(const void* data, size_t bytes, int target, int component_type, size_t count,
                       std::string type) {
    while (buffer_.size() % 4 != 0) {
        buffer_.push_back(0);
    }
    Accessor accessor;
    accessor.offset = buffer_.size();
    accessor.length = bytes;
    accessor.target = target;
    accessor.component_type = component_type;
    accessor.count = count;
    accessor.type = std::move(type);
    const auto* begin = static_cast<const uint8_t*>(data);
    buffer_.insert(buffer_.end(), begin, begin + bytes);
    accessors_.push_back(std::move(accessor));
    return int(accessors_.size() - 1);
}

int Gltf::add_vec2(const std::vector<std::array<float, 2>>& values) {
    return add_accessor(values.data(), values.size() * 8, target_vertices, component_float, values.size(), "VEC2");
}

int Gltf::add_vec3(const std::vector<Vec3>& values, bool with_bounds) {
    const int index =
        add_accessor(values.data(), values.size() * 12, target_vertices, component_float, values.size(), "VEC3");
    if (with_bounds && !values.empty()) {
        Vec3 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        Vec3 hi{-lo.x, -lo.y, -lo.z};
        for (const Vec3& v : values) {
            lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
            hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
        }
        accessors_[size_t(index)].min = std::format("[{},{},{}]", lo.x, lo.y, lo.z);
        accessors_[size_t(index)].max = std::format("[{},{},{}]", hi.x, hi.y, hi.z);
    }
    return index;
}

int Gltf::add_vec4(const std::vector<std::array<float, 4>>& values) {
    return add_accessor(values.data(), values.size() * 16, target_vertices, component_float, values.size(), "VEC4");
}

int Gltf::add_joints(const std::vector<std::array<uint16_t, 4>>& values) {
    return add_accessor(values.data(), values.size() * 8, target_vertices, component_u16, values.size(), "VEC4");
}

int Gltf::add_indices(const std::vector<uint32_t>& values) {
    return add_accessor(values.data(), values.size() * 4, target_indices, component_u32, values.size(), "SCALAR");
}

int Gltf::add_matrices(const std::vector<Mat4>& values) {
    return add_accessor(values.data(), values.size() * 64, 0, component_float, values.size(), "MAT4");
}

void Gltf::write(const std::filesystem::path& path) const {
    std::filesystem::path bin_path = path;
    bin_path.replace_extension(".bin");

    std::string json = R"({"asset":{"version":"2.0","generator":"starwarsmashup swgtool"},"scene":0,)";
    json += std::format(R"("scenes":[{{"nodes":{}}}],)", int_list(scene_nodes));

    json += R"("nodes":[)";
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node& node = nodes[i];
        json += (i ? "," : "") + std::format(R"({{"name":{})", json_string(node.name));
        if (node.translation) {
            json += std::format(R"(,"translation":[{},{},{}])", node.translation->x, node.translation->y,
                                node.translation->z);
        }
        if (node.rotation) {  // glTF stores x, y, z, w
            json += std::format(R"(,"rotation":[{},{},{},{}])", node.rotation->x, node.rotation->y, node.rotation->z,
                                node.rotation->w);
        }
        if (node.mesh >= 0) {
            json += std::format(R"(,"mesh":{})", node.mesh);
        }
        if (node.skin >= 0) {
            json += std::format(R"(,"skin":{})", node.skin);
        }
        if (!node.children.empty()) {
            json += R"(,"children":)" + int_list(node.children);
        }
        json += "}";
    }
    json += "],";

    json += R"("meshes":[)";
    for (size_t i = 0; i < meshes.size(); ++i) {
        json += (i ? "," : "") + std::format(R"({{"name":{},"primitives":[)", json_string(meshes[i].name));
        for (size_t p = 0; p < meshes[i].primitives.size(); ++p) {
            const Primitive& primitive = meshes[i].primitives[p];
            json += p ? ",{\"attributes\":{" : "{\"attributes\":{";
            bool first = true;
            for (const auto& [name, accessor] : primitive.attributes) {
                json += std::format(R"({}"{}":{})", first ? "" : ",", name, accessor);
                first = false;
            }
            json += "}";
            if (primitive.indices >= 0) {
                json += std::format(R"(,"indices":{})", primitive.indices);
            }
            if (primitive.material >= 0) {
                json += std::format(R"(,"material":{})", primitive.material);
            }
            json += "}";
        }
        json += "]}";
    }
    json += "],";

    if (!skins.empty()) {
        json += R"("skins":[)";
        for (size_t i = 0; i < skins.size(); ++i) {
            json += (i ? "," : "") + std::format(R"({{"joints":{},"inverseBindMatrices":{},"skeleton":{}}})",
                                                 int_list(skins[i].joints), skins[i].inverse_bind_matrices,
                                                 skins[i].skeleton);
        }
        json += "],";
    }

    json += R"("materials":[)";
    for (size_t i = 0; i < materials.size(); ++i) {
        json += (i ? "," : "") + std::format(R"({{"name":{},"pbrMetallicRoughness":{{)", json_string(materials[i].name));
        if (materials[i].texture >= 0) {
            json += std::format(R"("baseColorTexture":{{"index":{}}},)", materials[i].texture);
        }
        json += R"("metallicFactor":0,"roughnessFactor":0.8}})";
    }
    json += "],";

    json += R"("images":[)";
    for (size_t i = 0; i < images.size(); ++i) {
        json += (i ? "," : "") + std::format(R"({{"uri":{}}})", json_string(images[i]));
    }
    json += R"(],"textures":[)";
    for (size_t i = 0; i < images.size(); ++i) {
        json += (i ? "," : "") + std::format(R"({{"source":{}}})", i);
    }
    json += "],";

    json += R"("accessors":[)";
    for (size_t i = 0; i < accessors_.size(); ++i) {
        const Accessor& a = accessors_[i];
        json += (i ? "," : "") + std::format(R"({{"bufferView":{},"componentType":{},"count":{},"type":"{}")", i,
                                             a.component_type, a.count, a.type);
        if (!a.min.empty()) {
            json += std::format(R"(,"min":{},"max":{})", a.min, a.max);
        }
        json += "}";
    }
    json += R"(],"bufferViews":[)";
    for (size_t i = 0; i < accessors_.size(); ++i) {
        const Accessor& a = accessors_[i];
        json += (i ? "," : "") + std::format(R"({{"buffer":0,"byteOffset":{},"byteLength":{})", a.offset, a.length);
        if (a.target) {
            json += std::format(R"(,"target":{})", a.target);
        }
        json += "}";
    }
    json += std::format(R"(],"buffers":[{{"uri":{},"byteLength":{}}}]}})", json_string(bin_path.filename().string()),
                        buffer_.size());

    std::ofstream json_out(path, std::ios::binary);
    json_out << json;
    std::ofstream bin_out(bin_path, std::ios::binary);
    bin_out.write(reinterpret_cast<const char*>(buffer_.data()), static_cast<std::streamsize>(buffer_.size()));
    if (!json_out || !bin_out) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

} // namespace swm::swg
