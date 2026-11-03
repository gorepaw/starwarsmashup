#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "bf2/ucfb.h"
#include "common/math.h"

namespace swm::bf2 {

// The skeleton an animation bank was authored for (zaf_ chunk). Rest pose is
// in Battlefront space (left-handed, Y up, characters face +Z).
struct AnimationJoint {
    uint32_t hash = 0;  // name_hash() of the bone name
    uint32_t parent_hash = 0;  // 0 for the root
    Quat rotation;      // rest, relative to parent
    Vec3 translation;   // rest, relative to parent
};

struct AnimationSkeleton {
    std::vector<AnimationJoint> joints;  // parents precede children

    int find(uint32_t hash) const;  // -1 if missing
};

struct AnimationTrack {
    uint32_t bone_hash = 0;
    std::vector<Quat> rotations;     // per frame, local (absolute, not relative to rest)
    std::vector<Vec3> translations;  // per frame, local
};

struct Animation {
    uint32_t name_hash = 0;
    uint32_t frame_count = 0;
    std::vector<AnimationTrack> tracks;

    const AnimationTrack* track(uint32_t bone_hash) const;  // nullptr if the bone isn't animated
};

// Battlefront II animations play at 30 frames per second.
constexpr float animation_fps = 30.0f;

// Parses the BIN_ payload of a zaf_ chunk.
AnimationSkeleton parse_animation_skeleton(std::span<const uint8_t> bin);

// Parses and decodes the BIN_ payload of a zaa_ chunk.
//
// Each bone track stores one stream per quaternion component (x, y, z, w,
// scaled by 2047) and, if animated, one per translation axis (value = base +
// multiplier * stream). A stream is an int16 start value followed by int8
// deltas; the byte 0x80 means "hold the value for the next <uint8> frames"
// and 0x81 means "the next value is the following int16". This decodes all
// 15,016 streams in the human_0..4 banks exactly.
std::vector<Animation> parse_animation_bank(std::span<const uint8_t> bin);

// Name of a known human skeleton bone, or the hash in hex.
std::string bone_label(uint32_t hash);

// First chunk with `tag` whose NAME is `name`, anywhere under `root`; nullptr if missing.
const UcfbNode* find_named_chunk(const UcfbNode& root, std::string_view tag, std::string_view name);

} // namespace swm::bf2
