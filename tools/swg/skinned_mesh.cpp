#include "swg/skinned_mesh.h"

#include <stdexcept>

#include "swg/iff.h"
#include "swg/manifest.h"

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
    const IffNode* v = root.child("0004");
    if (!v) {
        v = root.child("0003");
    }
    const IffNode& version = v ? *v : root.expect("0004");

    IffReader info(version.expect("INFO").data);
    for (int i = 0; i < 9; ++i) {
        info.i32();  // counts, re-derived from the tables below
    }
    info.u16();  // occlusion zone name count (OZN's)
    const uint16_t combination_count = info.u16();
    const uint16_t occludes_count = info.u16();

    SkinnedMesh mesh;
    mesh.occlusion_layer = static_cast<int16_t>(info.u16());
    // OZN: the zones' names; FOZC: a count, then zone indices; OZC: per
    // combination a count, then zone indices; ZTO: zone indices (INFO's
    // count of them).
    if (const IffNode* ozn = version.child("OZN ")) {
        mesh.occlusion_zones = iff_strings(ozn->data);
    }
    const auto zone = [&](uint16_t index) {
        if (index >= mesh.occlusion_zones.size()) {
            throw std::runtime_error("SKMG: occlusion zone index out of range");
        }
        return mesh.occlusion_zones[index];
    };
    if (const IffNode* fozc = version.child("FOZC")) {
        IffReader r(fozc->data);
        const uint16_t count = r.u16();
        for (uint16_t i = 0; i < count; ++i) {
            mesh.fully_occluded.push_back(zone(r.u16()));
        }
    }
    if (const IffNode* ozc = version.child("OZC ")) {
        IffReader r(ozc->data);
        for (uint16_t c = 0; c < combination_count; ++c) {
            std::vector<std::string>& combination = mesh.zone_combinations.emplace_back();
            const uint16_t count = r.u16();
            for (uint16_t i = 0; i < count; ++i) {
                combination.push_back(zone(r.u16()));
            }
        }
    }
    if (const IffNode* zto = version.child("ZTO ")) {
        IffReader r(zto->data);
        for (uint16_t i = 0; i < occludes_count; ++i) {
            mesh.occludes.push_back(zone(r.u16()));
        }
    }
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

    // TRTS: INFO (renderer count, target count), then a TRT chunk each: the
    // renderer's path, a target count, and per target the part (PSDT index)
    // and the texture's tag (little-endian: "NIAM" for MAIN).
    if (const IffNode* renderers = version.child("TRTS")) {
        for (const IffNode* trt : renderers->children_with("TRT ")) {
            IffReader r(trt->data);
            SkinnedMesh::TextureRenderer renderer{.path = normalize_path(r.cstring())};
            const uint32_t targets = r.u32();
            for (uint32_t i = 0; i < targets; ++i) {
                const uint32_t part = r.u32();
                const uint32_t tag = r.u32();
                if (part >= mesh.parts.size()) {
                    throw std::runtime_error("SKMG: texture renderer targets a missing part");
                }
                const auto* t = reinterpret_cast<const char*>(&tag);
                renderer.targets.push_back({int(part), std::string{t[3], t[2], t[1], t[0]}});
            }
            mesh.texture_renderers.push_back(std::move(renderer));
        }
    }

    // BLTS: blend shapes (morph targets), each a `FORM BLT ` with INFO
    // (position delta count, normal delta count, name) and POSN (index + vec3).
    if (const IffNode* blts = version.child("BLTS")) {
        for (const IffNode* blt : blts->children_with("BLT ")) {
            const IffNode* info_node = blt->child("INFO");
            if (info_node == nullptr) {
                continue;
            }
            IffReader r(info_node->data);
            const uint32_t pos_count = r.u32();
            r.u32();  // normal delta count
            SkinnedMesh::BlendTarget target;
            target.name = r.cstring();
            if (const IffNode* posn = blt->child("POSN")) {
                IffReader pr(posn->data);
                for (uint32_t i = 0; i < pos_count && pr.remaining() >= 16; ++i) {
                    const uint32_t idx = pr.u32();
                    const float dx = pr.f32();
                    const float dy = pr.f32();
                    const float dz = pr.f32();
                    if (idx < mesh.positions.size()) {
                        target.position_deltas.push_back({idx, {dx, dy, dz}});
                    }
                }
            }
            if (!target.name.empty() && !target.position_deltas.empty()) {
                mesh.blend_targets.push_back(std::move(target));
            }
        }
    }
    return mesh;
}

} // namespace swm::swg
