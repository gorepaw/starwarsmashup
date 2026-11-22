#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "common/math.h"
#include "swg/animation_clip.h"
#include "swg/skeleton.h"

namespace swm::swg {

// A Galaxies keyframe animation (.ans): KFAT 0003 (rotations as floats) or
// CKAT 0001 (rotations compressed into 32 bits). Per joint, by name: its
// rotation keys (one, if it holds still) and its translation's keys per
// axis, all in SWG space.
//
// Worked out from the files (see the .cpp): a joint's rotation takes the
// place of its bind rotation, between its skeleton's pre- and
// post-multiply rotations; translations are offsets from its bind
// translation (all but the root's are zero).
struct KeyframeAnimation {
    struct Track {
        std::string joint;
        std::vector<std::pair<int, Quat>> rotation;                    // (frame, rotation), by frame
        std::array<std::vector<std::pair<int, float>>, 3> translation;  // per axis
    };
    float fps = 30;
    int frames = 0;
    std::vector<Track> tracks;
};

// Throws std::runtime_error on unsupported or malformed data.
KeyframeAnimation parse_keyframe_animation(std::span<const uint8_t> bytes);

// A CKAT compressed rotation: three format bytes (x, y, z) and a 32-bit code.
Quat decompress_rotation(const std::array<uint8_t, 3>& format, uint32_t code);

// Samples an animation for a skeleton at its own frame rate: each joint's
// local rotation (post * key * pre; the bind rotation where the animation
// has no track for it) and the root's translation.
AnimationClip sample_animation(const KeyframeAnimation& animation, const Skeleton& skeleton, const std::string& name);

} // namespace swm::swg
