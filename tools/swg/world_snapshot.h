#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "common/math.h"

namespace swm::swg {

// A world snapshot (.ws, WSNP 0001): the statically placed objects of a
// planet. Objects inside buildings are nested under their building's node.
struct SnapshotNode {
    int32_t id = 0;
    int32_t parent_id = 0;  // 0 for objects placed in the world
    int32_t template_index = 0;
    int32_t cell_index = 0;
    Quat rotation;
    Vec3 position;  // world space for top-level objects, else relative to the cell
    float radius = 0;
    std::string template_name;
};

std::vector<SnapshotNode> parse_world_snapshot(std::span<const uint8_t> bytes);

} // namespace swm::swg
