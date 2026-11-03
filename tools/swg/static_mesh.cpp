#include "swg/static_mesh.h"

#include <stdexcept>

#include "swg/iff.h"

namespace swm::swg {
namespace {

// Vertex format flags (VTXA INFO):
//   bit 0 position (3 floats), bit 2 normal (3 floats), bit 3 color (uint32),
//   bits 8-11 texture coordinate set count, then 2 bits per set from bit 12
//   giving its dimension minus one.
constexpr uint32_t has_position = 1u << 0;
constexpr uint32_t has_normal = 1u << 2;
constexpr uint32_t has_color = 1u << 3;
constexpr uint32_t known_flag_bits = 0xff0d;  // the bits above; checked against the stride

uint32_t texture_set_count(uint32_t flags) {
    return (flags >> 8) & 0xf;
}

uint32_t texture_set_dimension(uint32_t flags, uint32_t set) {
    return ((flags >> (12 + 2 * set)) & 3) + 1;
}

Hardpoint parse_hardpoint(const IffNode& hpnt) {
    IffReader reader(hpnt.data);
    Hardpoint hardpoint;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            hardpoint.transform.at(row, column) = reader.f32();
        }
    }
    hardpoint.name = reader.cstring();
    return hardpoint;
}

void parse_vertices(const IffNode& vtxa, StaticMesh::Part& part) {
    const IffNode& version = vtxa.expect("0003");
    IffReader info(version.expect("INFO").data);
    const uint32_t flags = info.u32();
    const uint32_t count = info.u32();
    if (!(flags & has_position)) {
        throw std::runtime_error("MESH: vertex format without positions");
    }

    const uint32_t sets = texture_set_count(flags);
    uint32_t stride = 12 + ((flags & has_normal) ? 12 : 0) + ((flags & has_color) ? 4 : 0);
    for (uint32_t set = 0; set < sets; ++set) {
        stride += 4 * texture_set_dimension(flags, set);
    }
    const auto& data = version.expect("DATA").data;
    if ((flags & ~known_flag_bits & 0xff) != 0 || data.size() != size_t(stride) * count) {
        throw std::runtime_error("MESH: unsupported vertex format flags " + std::to_string(flags));
    }

    IffReader reader(data);
    for (uint32_t i = 0; i < count; ++i) {
        part.positions.push_back({reader.f32(), reader.f32(), reader.f32()});
        if (flags & has_normal) {
            part.normals.push_back({reader.f32(), reader.f32(), reader.f32()});
        }
        if (flags & has_color) {
            reader.u32();
        }
        for (uint32_t set = 0; set < sets; ++set) {
            const uint32_t dimension = texture_set_dimension(flags, set);
            float coords[4] = {};
            for (uint32_t d = 0; d < dimension; ++d) {
                coords[d] = reader.f32();
            }
            if (set == 0) {
                part.uv.push_back({coords[0], coords[1]});
            }
        }
    }
}

void parse_indices(const IffNode& indx, StaticMesh::Part& part) {
    IffReader reader(indx.data);
    const uint32_t count = reader.u32();
    const size_t remaining = reader.remaining();
    if (remaining == size_t(count) * 2) {
        for (uint32_t i = 0; i < count; ++i) {
            part.indices.push_back(reader.u16());
        }
    } else if (remaining == size_t(count) * 4) {
        for (uint32_t i = 0; i < count; ++i) {
            part.indices.push_back(reader.u32());
        }
    } else {
        throw std::runtime_error("MESH: index buffer size doesn't match its count");
    }
}

} // namespace

StaticMesh parse_static_mesh(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "MESH") {
        throw std::runtime_error("not a static mesh (" + root.tag + ")");
    }
    const IffNode* version = root.child("0005");
    version = version ? version : &root.expect("0004");

    StaticMesh mesh;
    if (const IffNode* appr = version->child("APPR")) {
        for (const IffNode& appr_version : appr->children) {
            if (const IffNode* hpts = appr_version.child("HPTS")) {
                for (const IffNode* hpnt : hpts->children_with("HPNT")) {
                    mesh.hardpoints.push_back(parse_hardpoint(*hpnt));
                }
            }
        }
    }

    const IffNode& sps = version->expect("SPS ").expect("0001");
    for (const IffNode& shader_node : sps.children) {
        if (!shader_node.form) {
            continue;  // CNT
        }
        StaticMesh::Part part;
        part.shader = iff_strings(shader_node.expect("NAME").data).at(0);
        for (const IffNode& primitive : shader_node.children) {
            if (!primitive.form) {
                continue;  // NAME, INFO
            }
            parse_vertices(primitive.expect("VTXA"), part);
            if (const IffNode* indx = primitive.child("INDX")) {
                parse_indices(*indx, part);
            } else {
                for (uint32_t i = 0; i < part.positions.size(); ++i) {
                    part.indices.push_back(i);
                }
            }
        }
        for (const uint32_t index : part.indices) {
            if (index >= part.positions.size()) {
                throw std::runtime_error("MESH: index out of range in " + part.shader);
            }
        }
        mesh.parts.push_back(std::move(part));
    }
    return mesh;
}

std::string lod_highest_detail(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "DTLA") {
        throw std::runtime_error("not a LOD file (" + root.tag + ")");
    }
    std::string best;
    int32_t best_index = -1;
    for (const IffNode& version : root.children) {
        const IffNode* data = version.child("DATA");
        if (!data) {
            continue;
        }
        // CHLD: int32 LOD index, path. The highest index is the most detailed (the "_l0" mesh).
        for (const IffNode* chld : data->children_with("CHLD")) {
            IffReader reader(chld->data);
            const int32_t index = reader.i32();
            if (index > best_index) {
                best_index = index;
                best = reader.cstring();
            }
        }
    }
    if (best.empty()) {
        throw std::runtime_error("LOD file lists no meshes");
    }
    return best;
}

} // namespace swm::swg
