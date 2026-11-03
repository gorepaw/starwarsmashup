#pragma once

#include <string>
#include <vector>

#include "common/math.h"

namespace swm::swg {

// An animation for a Galaxies skeleton, sampled at a fixed rate (SWG space).
struct AnimationClip {
    std::string name;
    float fps = 30.0f;
    std::vector<std::vector<Quat>> rotations;  // [frame][joint]: local rotation
    std::vector<Vec3> root_translations;       // [frame]: local translation of joint 0
};

} // namespace swm::swg
