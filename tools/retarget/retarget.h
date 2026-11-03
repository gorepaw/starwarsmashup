#pragma once

#include <string>
#include <vector>

#include "bf2/animation.h"
#include "swg/animation_clip.h"
#include "swg/skeleton.h"

namespace swm::retarget {

// One Galaxies joint driven by one Battlefront bone. The aim joints define
// each bone's direction in its rest pose (from the joint toward the aim
// joint); rest poses are aligned by rotating those directions onto each
// other. Without aim joints, the nearest mapped ancestor's alignment is used.
struct BoneMapping {
    std::string swg_joint;
    std::string bf2_bone;
    std::string swg_aim;
    std::string bf2_aim;
};

// Galaxies humanoid skeleton (all_b.skt) <- Battlefront human skeleton.
std::vector<BoneMapping> humanoid_mapping();

// Retargets a Battlefront animation onto a Galaxies skeleton.
//
// For each mapped joint, the Battlefront bone's world-space rotation away from
// its rest pose is applied to the Galaxies joint's rest pose, after aligning
// the two rest poses' bone directions. The weapon joint ("hold_r") copies the
// world rotation of Battlefront's weapon hardpoint ("hp_weapons"). Other
// unmapped joints (twist bones, fingers, face) keep their rest pose relative
// to their parent. Root motion
// is removed (the clip plays in place); the hip's vertical motion is kept,
// scaled by the ratio of hip heights.
//
// Battlefront's data is mirrored in X relative to Galaxies (verified from the
// rest pose: its right hand is at -X, Galaxies' at +X, both facing +Z), so the
// source is mirrored first. Throws std::runtime_error if a mapped joint or
// bone is missing.
swg::AnimationClip retarget_animation(const swg::Skeleton& target, const bf2::AnimationSkeleton& source_skeleton,
                                      const bf2::Animation& animation, const std::vector<BoneMapping>& mapping,
                                      std::string name);

} // namespace swm::retarget
