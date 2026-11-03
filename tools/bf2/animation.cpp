#include "bf2/animation.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "bf2/hash.h"

namespace swm::bf2 {
namespace {

constexpr float rotation_scale = 1.0f / 2047.0f;
constexpr int8_t hold_marker = -128;      // 0x80 <uint8 count>
constexpr int8_t absolute_marker = -127;  // 0x81 <int16 value>

// Bounds-checked little-endian reads from a byte span.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> data, size_t pos = 0) : data_(data), pos_(pos) {}

    template <class T>
    T read() {
        if (pos_ + sizeof(T) > data_.size()) {
            throw std::runtime_error("animation data: read past end");
        }
        T value;
        std::memcpy(&value, data_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return value;
    }

    size_t pos() const { return pos_; }

private:
    std::span<const uint8_t> data_;
    size_t pos_;
};

// Animation data blocks: byte-reversed 4-character tag, uint32 size, data.
struct Block {
    std::string tag;  // as stored, e.g. "TNJA"
    std::span<const uint8_t> data;
};

std::vector<Block> read_blocks(std::span<const uint8_t> bytes) {
    std::vector<Block> blocks;
    size_t pos = 0;
    while (pos + 8 <= bytes.size()) {
        uint32_t size;
        std::memcpy(&size, bytes.data() + pos + 4, 4);
        if (size > bytes.size() - pos - 8) {
            throw std::runtime_error("animation data: block overruns its parent");
        }
        blocks.push_back({std::string(reinterpret_cast<const char*>(bytes.data() + pos), 4), bytes.subspan(pos + 8, size)});
        pos += 8 + size;
    }
    return blocks;
}

const Block& expect_block(const std::vector<Block>& blocks, std::string_view tag) {
    const auto it = std::find_if(blocks.begin(), blocks.end(), [&](const Block& b) { return b.tag == tag; });
    if (it == blocks.end()) {
        throw std::runtime_error("animation data: missing " + std::string(tag) + " block");
    }
    return *it;
}

std::vector<int16_t> decode_stream(std::span<const uint8_t> data, size_t offset, uint32_t frame_count) {
    Reader reader(data, offset);
    int16_t value = reader.read<int16_t>();
    std::vector<int16_t> values{value};
    while (values.size() < frame_count) {
        const int8_t code = reader.read<int8_t>();
        if (code == hold_marker) {
            const uint8_t count = reader.read<uint8_t>();
            values.insert(values.end(), count, value);
        } else if (code == absolute_marker) {
            value = reader.read<int16_t>();
            values.push_back(value);
        } else {
            value = static_cast<int16_t>(value + code);
            values.push_back(value);
        }
    }
    values.resize(frame_count);
    return values;
}

} // namespace

int AnimationSkeleton::find(uint32_t hash) const {
    for (size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].hash == hash) {
            return int(i);
        }
    }
    return -1;
}

const AnimationTrack* Animation::track(uint32_t bone_hash) const {
    for (const AnimationTrack& t : tracks) {
        if (t.bone_hash == bone_hash) {
            return &t;
        }
    }
    return nullptr;
}

AnimationSkeleton parse_animation_skeleton(std::span<const uint8_t> bin) {
    const std::vector<Block> top = read_blocks(bin);
    const std::vector<Block> zaff = read_blocks(expect_block(top, "FFAZ").data);
    const std::vector<Block> skeleton_blocks = read_blocks(expect_block(zaff, "LEKS").data);

    AnimationSkeleton skeleton;
    for (const Block& block : skeleton_blocks) {
        if (block.tag != "TNOJ") {
            continue;
        }
        // hash, rotation (x, y, z, w), translation, parent, first child, sibling
        Reader reader(block.data);
        AnimationJoint joint;
        joint.hash = reader.read<uint32_t>();
        const float qx = reader.read<float>(), qy = reader.read<float>(), qz = reader.read<float>();
        joint.rotation = normalized(Quat{reader.read<float>(), qx, qy, qz});
        joint.translation = {reader.read<float>(), reader.read<float>(), reader.read<float>()};
        joint.parent_hash = reader.read<uint32_t>();
        skeleton.joints.push_back(joint);
    }

    // Order parents before children.
    std::vector<AnimationJoint> ordered;
    std::vector<bool> placed(skeleton.joints.size(), false);
    auto is_placed = [&](uint32_t hash) {
        return hash == 0 || std::any_of(ordered.begin(), ordered.end(), [&](const AnimationJoint& j) { return j.hash == hash; });
    };
    while (ordered.size() < skeleton.joints.size()) {
        const size_t before = ordered.size();
        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            if (!placed[i] && is_placed(skeleton.joints[i].parent_hash)) {
                ordered.push_back(skeleton.joints[i]);
                placed[i] = true;
            }
        }
        if (ordered.size() == before) {
            throw std::runtime_error("animation skeleton: joint with a missing parent");
        }
    }
    skeleton.joints = std::move(ordered);
    return skeleton;
}

std::vector<Animation> parse_animation_bank(std::span<const uint8_t> bin) {
    // "SMNA" header: tag, unknown, unknown, track count, data size, animation count.
    Reader header(bin);
    if (bin.size() < 24 || std::memcmp(bin.data(), "SMNA", 4) != 0) {
        throw std::runtime_error("not an animation bank");
    }
    header.read<uint32_t>();
    header.read<uint32_t>();
    header.read<uint32_t>();
    const uint32_t track_count = header.read<uint32_t>();
    header.read<uint32_t>();  // data size
    const uint32_t animation_count = header.read<uint32_t>();

    const std::vector<Block> blocks = read_blocks(bin.subspan(24));
    const std::span<const uint8_t> info = expect_block(blocks, "MINA").data;
    const std::span<const uint8_t> joints = expect_block(blocks, "TNJA").data;
    const std::span<const uint8_t> data = expect_block(blocks, "TADA").data;
    if (info.size() < size_t(animation_count) * 16 || joints.size() < size_t(track_count) * 48) {
        throw std::runtime_error("animation bank: tables smaller than their counts");
    }

    std::vector<Animation> animations;
    size_t track_index = 0;
    for (uint32_t a = 0; a < animation_count; ++a) {
        // flags, unknown, name hash, frame count (uint16), bone count (uint16)
        Reader entry(info, size_t(a) * 16);
        entry.read<uint32_t>();
        entry.read<uint32_t>();
        Animation animation;
        animation.name_hash = entry.read<uint32_t>();
        animation.frame_count = entry.read<uint16_t>();
        const uint16_t bone_count = entry.read<uint16_t>();
        if (track_index + bone_count > track_count) {
            throw std::runtime_error("animation bank: more tracks than the header declares");
        }

        for (uint16_t b = 0; b < bone_count; ++b, ++track_index) {
            // bone hash, 4 rotation offsets, 3 translation offsets, translation base (3), multiplier
            Reader t(joints, track_index * 48);
            AnimationTrack track;
            track.bone_hash = t.read<uint32_t>();
            uint32_t rotation_offsets[4], translation_offsets[3];
            for (uint32_t& o : rotation_offsets) {
                o = t.read<uint32_t>();
            }
            for (uint32_t& o : translation_offsets) {
                o = t.read<uint32_t>();
            }
            const Vec3 base{t.read<float>(), t.read<float>(), t.read<float>()};
            const float multiplier = t.read<float>();

            std::vector<int16_t> rotation[4];
            for (int c = 0; c < 4; ++c) {
                rotation[c] = decode_stream(data, rotation_offsets[c], animation.frame_count);
            }
            std::vector<int16_t> translation[3];
            if (multiplier != 0.0f) {
                for (int c = 0; c < 3; ++c) {
                    translation[c] = decode_stream(data, translation_offsets[c], animation.frame_count);
                }
            }
            for (uint32_t f = 0; f < animation.frame_count; ++f) {
                track.rotations.push_back(normalized(Quat{rotation[3][f] * rotation_scale, rotation[0][f] * rotation_scale,
                                                          rotation[1][f] * rotation_scale, rotation[2][f] * rotation_scale}));
                track.translations.push_back(multiplier != 0.0f ? base + Vec3{float(translation[0][f]), float(translation[1][f]),
                                                                              float(translation[2][f])} * multiplier
                                                                : base);
            }
            animation.tracks.push_back(std::move(track));
        }
        animations.push_back(std::move(animation));
    }
    return animations;
}

std::string bone_label(uint32_t hash) {
    static constexpr const char* known[] = {
        "dummyroot", "bone_root", "bone_pelvis", "bone_a_spine", "bone_b_spine", "bone_ribcage", "bone_neck",
        "bone_head", "bone_l_clavicle", "bone_l_upperarm", "bone_l_forearm", "bone_l_hand", "bone_r_clavicle",
        "bone_r_upperarm", "bone_r_forearm", "bone_r_hand", "bone_l_thigh", "bone_l_calf", "bone_l_foot",
        "bone_l_toe", "bone_r_thigh", "bone_r_calf", "bone_r_foot", "bone_r_toe", "hp_weapons",
    };
    for (const char* name : known) {
        if (name_hash(name) == hash) {
            return name;
        }
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08x", hash);
    return buffer;
}

const UcfbNode* find_named_chunk(const UcfbNode& root, std::string_view tag, std::string_view name) {
    if (root.tag == tag && root.name() == name) {
        return &root;
    }
    for (const UcfbNode& child : root.children) {
        if (const UcfbNode* found = find_named_chunk(child, tag, name)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace swm::bf2
