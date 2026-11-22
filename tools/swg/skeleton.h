#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/math.h"

namespace swm::swg {

struct Joint {
    std::string name;
    int parent = -1;   // index into Skeleton::joints, -1 for roots; parents precede children
    Vec3 translation;  // bind pose, relative to parent (SWG space)
    Quat rotation;     // bind pose, relative to parent (SWG space)
    // The skeleton's pre- and post-multiply rotations: rotation is
    // post * bind * pre, and an animation's rotation takes bind's place.
    Quat pre, post;
};

struct Skeleton {
    std::vector<Joint> joints;

    // Case-insensitive; -1 if missing.
    int find(std::string_view name) const;

    // Appends `other`, parenting its roots to the joint named `attach_to`
    // (or leaving them roots if `attach_to` is empty). Throws if missing.
    void attach(const Skeleton& other, std::string_view attach_to);

    // Bind-pose model-space transform of every joint (SWG space).
    std::vector<Mat4> global_transforms() const;
};

// Parses a .skt file: SKTM 0002, or SLOD (uses the highest-detail SKTM).
//
// Each joint's local rotation is post * bind * pre (RPST * BPRO * RPRE). That
// order fits the human meshes' vertex weights best (mean 1.6 cm; the reverse
// order is 6 cm off). Galaxies' own animations confirm it: with their
// rotations in bind's place, the human walks, runs and sits as it should.
Skeleton parse_skeleton(std::span<const uint8_t> bytes);

} // namespace swm::swg
