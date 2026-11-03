#include "swg/appearance.h"

#include <cstring>
#include <stdexcept>

#include "swg/iff.h"
#include "swg/manifest.h"

namespace swm::swg {
namespace {

// Finds the SSHT form inside a shader file: SSHT itself, or CSHD/<version>/SSHT.
const IffNode* find_ssht(const IffNode& node) {
    if (node.form && node.tag == "SSHT") {
        return &node;
    }
    for (const IffNode& child : node.children) {
        if (const IffNode* found = find_ssht(child)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

SkeletalAppearance parse_skeletal_appearance(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "SMAT") {
        throw std::runtime_error("not a skeletal appearance (" + root.tag + ")");
    }
    const IffNode& version = root.expect("0003");

    SkeletalAppearance appearance;
    appearance.mesh_generators = iff_strings(version.expect("MSGN").data);
    const std::vector<std::string> skeleton_info = iff_strings(version.expect("SKTI").data);
    for (size_t i = 0; i + 1 < skeleton_info.size(); i += 2) {
        appearance.skeletons.push_back({skeleton_info[i], skeleton_info[i + 1]});
    }
    return appearance;
}

std::vector<std::string> parse_mesh_lod_list(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "MLOD") {
        throw std::runtime_error("not a mesh LOD list (" + root.tag + ")");
    }
    std::vector<std::string> paths;
    for (const IffNode* name : root.expect("0000").children_with("NAME")) {
        paths.push_back(iff_strings(name->data).at(0));
    }
    return paths;
}

std::string shader_main_texture(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    const IffNode* ssht = find_ssht(root);
    if (!ssht) {
        return {};
    }
    for (const IffNode& version : ssht->children) {
        const IffNode* txms = version.child("TXMS");
        if (!txms) {
            continue;
        }
        for (const IffNode* txm : txms->children_with("TXM ")) {
            for (const IffNode& txm_version : txm->children) {
                const IffNode* data = txm_version.child("DATA");
                const IffNode* name = txm_version.child("NAME");
                // Texture tags are stored as little-endian uint32s: "MAIN" reads as "NIAM".
                if (data && name && data->data.size() >= 4 && std::memcmp(data->data.data(), "NIAM", 4) == 0) {
                    return normalize_path(iff_strings(name->data).at(0));
                }
            }
        }
    }
    return {};
}

} // namespace swm::swg
