#include "swg/skinned_mesh.h"

#include <stdexcept>

#include "swg/iff.h"

namespace swm::swg {
namespace {

std::vector<Vec3> read_vec3s(std::span<const uint8_t> data) {
    IffReader reader(data);
    std::vector<Vec3> out(data.size() / 12);
    for (Vec3& v : out) {
        v = {reader.f32(), reader.f32(), reader.f32()};
    }
    return out;
}

SkinnedMesh::Part parse_part(const IffNode& psdt) {
    SkinnedMesh::Part part;
    part.shader = iff_strings(psdt.expect("NAME").data).at(0);

    IffReader pidx(psdt.expect("PIDX").data);
    const uint32_t vertex_count = pidx.u32();
    part.position_index.resize(vertex_count);
    for (uint32_t& index : part.position_index) {
        index = pidx.u32();
    }

    if (const IffNode* nidx_node = psdt.child("NIDX")) {
        IffReader nidx(nidx_node->data);
        part.normal_index.resize(vertex_count);
        for (uint32_t& index : part.normal_index) {
            index = nidx.u32();
        }
    }

    if (const IffNode* txci = psdt.child("TXCI")) {
        IffReader info(txci->data);
        const uint32_t set_count = info.u32();
        if (set_count > 0) {
            const uint32_t dimension = info.u32();
            const IffNode& tcsd = psdt.expect("TCSF").expect("TCSD");
            IffReader coords(tcsd.data);
            part.uv.resize(vertex_count);
            for (auto& uv : part.uv) {
                uv[0] = coords.f32();
                uv[1] = dimension > 1 ? coords.f32() : 0.0f;
                for (uint32_t extra = 2; extra < dimension; ++extra) {
                    coords.f32();
                }
            }
        }
    }

    const IffNode& prim = psdt.expect("PRIM");
    for (const IffNode& list : prim.children) {
        if (list.tag == "ITL ") {
            IffReader reader(list.data);
            const uint32_t count = reader.u32();
            for (uint32_t i = 0; i < count; ++i) {
                part.triangles.push_back({reader.u32(), reader.u32(), reader.u32()});
            }
        } else if (list.tag == "OITL") {
            IffReader reader(list.data);
            const uint32_t count = reader.u32();
            for (uint32_t i = 0; i < count; ++i) {
                part.triangle_occlusion.push_back(static_cast<int16_t>(reader.u16()));
                part.triangles.push_back({reader.u32(), reader.u32(), reader.u32()});
            }
        }
    }
    for (const auto& triangle : part.triangles) {
        for (const uint32_t index : triangle) {
            if (index >= vertex_count) {
                throw std::runtime_error("SKMG: triangle index out of range in " + part.shader);
            }
        }
    }
    return part;
}

} // namespace

SkinnedMesh parse_skinned_mesh(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "SKMG") {
        throw std::runtime_error("not a skinned mesh (" + root.tag + ")");
    }
    const IffNode& version = root.expect("0004");

    IffReader info(version.expect("INFO").data);
    for (int i = 0; i < 9; ++i) {
        info.i32();  // counts, re-derived from the tables below
    }
    info.u16();  // occlusion zone name count
    info.u16();  // occlusion zone combination count
    info.u16();  // zones-this-occludes count

    SkinnedMesh mesh;
    mesh.occlusion_layer = static_cast<int16_t>(info.u16());
    mesh.skeletons = iff_strings(version.expect("SKTM").data);
    mesh.bones = iff_strings(version.expect("XFNM").data);
    mesh.positions = read_vec3s(version.expect("POSN").data);
    if (const IffNode* norm = version.child("NORM")) {
        mesh.normals = read_vec3s(norm->data);
    }

    IffReader counts(version.expect("TWHD").data);
    IffReader weights(version.expect("TWDT").data);
    mesh.weights.resize(mesh.positions.size());
    for (auto& position_weights : mesh.weights) {
        const uint32_t count = counts.u32();
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t bone = weights.u32();
            const float weight = weights.f32();
            if (bone >= mesh.bones.size()) {
                throw std::runtime_error("SKMG: weight references a missing bone");
            }
            position_weights.emplace_back(bone, weight);
        }
    }

    for (const IffNode* psdt : version.children_with("PSDT")) {
        mesh.parts.push_back(parse_part(*psdt));
        for (const uint32_t index : mesh.parts.back().position_index) {
            if (index >= mesh.positions.size()) {
                throw std::runtime_error("SKMG: position index out of range");
            }
        }
        for (const uint32_t index : mesh.parts.back().normal_index) {
            if (index >= mesh.normals.size()) {
                throw std::runtime_error("SKMG: normal index out of range");
            }
        }
    }
    return mesh;
}

} // namespace swm::swg
