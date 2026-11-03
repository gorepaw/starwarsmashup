#include "swg/world_snapshot.h"

#include <stdexcept>

#include "swg/iff.h"

namespace swm::swg {
namespace {

// NODE -> 0000 -> DATA (52 bytes): id, parent id, template index, cell index,
// rotation (w, x, y, z), position, radius, portal layout CRC. Child NODEs follow.
void collect(const IffNode& node, std::vector<SnapshotNode>& out) {
    for (const IffNode* child : node.children_with("NODE")) {
        for (const IffNode& version : child->children) {
            if (!version.form) {
                continue;
            }
            if (const IffNode* data = version.child("DATA")) {
                IffReader r(data->data);
                SnapshotNode n;
                n.id = r.i32();
                n.parent_id = r.i32();
                n.template_index = r.i32();
                n.cell_index = r.i32();
                n.rotation.w = r.f32();
                n.rotation.x = r.f32();
                n.rotation.y = r.f32();
                n.rotation.z = r.f32();
                n.position = {r.f32(), r.f32(), r.f32()};
                n.radius = r.f32();
                out.push_back(n);
            }
            collect(version, out);
        }
    }
}

} // namespace

std::vector<SnapshotNode> parse_world_snapshot(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "WSNP") {
        throw std::runtime_error("not a world snapshot (" + root.tag + ")");
    }
    const IffNode& version = root.expect("0001");
    std::vector<SnapshotNode> nodes;
    collect(version.expect("NODS"), nodes);

    if (const IffNode* names = version.child("OTNL")) {
        IffReader r(names->data);
        const int32_t count = r.i32();
        std::vector<std::string> templates;
        for (int32_t i = 0; i < count; ++i) {
            templates.push_back(r.cstring());
        }
        for (SnapshotNode& n : nodes) {
            if (n.template_index >= 0 && size_t(n.template_index) < templates.size()) {
                n.template_name = templates[size_t(n.template_index)];
            }
        }
    }
    return nodes;
}

} // namespace swm::swg
