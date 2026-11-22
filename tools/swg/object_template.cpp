#include "swg/object_template.h"

#include <stdexcept>
#include <vector>

#include "swg/iff.h"
#include "swg/manifest.h"

namespace swm::swg {

namespace {

// Every parameter chunk under `node`, depth first; and the first base
// template named (DERV).
void scan(const IffNode& node, std::vector<const IffNode*>& parameters, std::string& base) {
    for (const IffNode& child : node.children) {
        if (child.form) {
            if (child.tag == "DERV") {
                if (const IffNode* name = child.child("XXXX"); name != nullptr && base.empty()) {
                    base = IffReader(name->data).cstring();
                }
                continue;
            }
            scan(child, parameters, base);
        } else if (child.tag == "XXXX") {
            parameters.push_back(&child);
        }
    }
}

} // namespace

ObjectTemplates::Read ObjectTemplates::read(const std::string& path) {
    const std::vector<uint8_t> bytes = library_.read(path);
    const IffNode root = parse_iff_root(bytes);
    std::vector<const IffNode*> parameters;
    Read out;
    scan(root, parameters, out.base);
    for (const IffNode* parameter : parameters) {
        IffReader r(parameter->data);
        const std::string name = r.cstring();
        if (name != "appearanceFilename" && name != "portalLayoutFilename" && name != "clientDataFile" &&
            name != "collisionRadius") {
            continue;
        }
        if (r.remaining() == 0 || r.u8() == 0) {
            continue;  // not set here
        }
        if (name == "collisionRadius") {
            if (r.remaining() >= 5 && r.u8() == ' ') {
                out.values.collision_radius = r.f32();
                out.has_radius = true;
            }
            continue;
        }
        const std::string value = normalize_path(r.cstring());
        if (name == "appearanceFilename") {
            out.values.appearance = value;
            out.has_appearance = true;
        } else if (name == "clientDataFile") {
            out.values.client_data = value;
            out.has_client_data = true;
        } else {
            out.values.portal_layout = value;
            out.has_portal_layout = true;
        }
    }
    return out;
}

const SharedObjectTemplate& ObjectTemplates::shared(const std::string& path_in) {
    const std::string path = normalize_path(path_in);
    if (const auto it = cache_.find(path); it != cache_.end()) {
        return it->second;
    }
    Read r = read(path);
    SharedObjectTemplate values = r.values;
    // What this template leaves unset comes from its base, and so on.
    std::string base = r.base;
    for (int depth = 0;
         !base.empty() && (!r.has_appearance || !r.has_portal_layout || !r.has_client_data || !r.has_radius) &&
         depth < 16;
         ++depth) {
        if (!library_.contains(normalize_path(base))) {
            break;
        }
        const Read b = read(normalize_path(base));
        if (!r.has_appearance && b.has_appearance) {
            values.appearance = b.values.appearance;
            r.has_appearance = true;
        }
        if (!r.has_portal_layout && b.has_portal_layout) {
            values.portal_layout = b.values.portal_layout;
            r.has_portal_layout = true;
        }
        if (!r.has_client_data && b.has_client_data) {
            values.client_data = b.values.client_data;
            r.has_client_data = true;
        }
        if (!r.has_radius && b.has_radius) {
            values.collision_radius = b.values.collision_radius;
            r.has_radius = true;
        }
        base = b.base;
    }
    return cache_.emplace(path, values).first->second;
}

std::vector<std::string> client_data_wearables(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "CLDF") {
        throw std::runtime_error("not a client data file (" + root.tag + ")");
    }
    std::vector<std::string> wearables;
    for (const IffNode& version : root.children) {
        if (!version.form) {
            continue;
        }
        for (const IffNode* wear : version.children_with("WEAR")) {
            for (const IffNode* mesh : wear->children_with("MESH")) {  // a suit of armor: a mesh per piece
                wearables.push_back(normalize_path(IffReader(mesh->data).cstring()));
            }
        }
    }
    return wearables;
}

} // namespace swm::swg
