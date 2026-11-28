#include "swg/keyframe_animation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

#include "swg/iff.h"

// How the two formats were worked out, from the files alone:
//
// KFAT 0003: INFO is the frame rate (f32), then frames, joints, animated
// rotations, static rotations, animated translations and static
// translations (i32 each). XFRM holds a joint per XFIN: its name, whether
// its rotation is animated (u8), the rotation's index (i32: into the
// animated channels, AROT, or the static ones, SROT), a translation mask
// (i32: 0x08 x, 0x10 y, 0x20 z animated) and the x, y and z translations'
// indices (i32: into ATRN's channels where animated, else STRN). A QCHN is
// a key count (i32) and keys (frame i32, w x y z f32); a CHNL a key count
// and keys (frame i32, value f32). Static rotations are w x y z, static
// translations single floats. Identity is (1, 0, 0, 0).
//
// CKAT 0001: the same with smaller numbers: INFO's counts are u16, XFIN's
// rotation index and translation indices u16 and its mask a u8, a QCHN's
// count and frames u16 and a CHNL's too. A rotation is compressed: three
// format bytes, one per component (x, y, z), shared by a channel's keys,
// and a 32-bit code per key: x in bits 21-31, y in 10-20, z in 0-9. See
// decompress_rotation.
namespace swbfo::swg {

namespace {

const IffNode* version_of(const IffNode& node) {
    for (const IffNode& child : node.children) {
        if (child.form && child.tag.find_first_not_of("0123456789") == std::string::npos) {
            return &child;
        }
    }
    return nullptr;
}

bool same_name(const std::string& a, const std::string& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); });
}

// One component: a format byte of L ones, a zero and a (7 - L)-bit offset v,
// and a code field of a sign bit and a magnitude. The component is
// (v - center) * step + (sign) magnitude / range * step, where center is
// the middle of v's range and step grows with L: v places a channel's keys
// coarsely (within +-1), the codes within a step either side of it.
//
// Established from the files: the sign-and-magnitude code (keys move
// smoothly through a channel's middle, where the sign flips); for L = 1, a
// step of 1/32 (matches a plain clip's rotations to 0.0005, and an identity
// rotation coded either side of its middle); for L = 2 to 4, 1/16, 1/8 and
// 1/4 (best fits to the standing idle's pose at the end of clips that end
// standing). ASSUMPTION: L = 5 and 6, 1/4 and 1/2 (fewest keys to fit by;
// with the doubling continued, some keys would lie outside the unit sphere;
// with these, none of 12,085 do).
double component(uint8_t format, uint32_t code, int bits) {
    int level = 0;
    while (level < 7 && (format & (0x80 >> level))) {
        ++level;
    }
    static constexpr double steps[8] = {1.0 / 64, 1.0 / 32, 1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 4, 1.0 / 2, 1.0 / 2};
    const int offset_bits = std::max(0, 7 - level);
    const int v = format & ((1 << offset_bits) - 1);
    const double center = ((1 << offset_bits) - 1) / 2.0;
    const uint32_t sign_bit = 1u << (bits - 1);
    const double magnitude = double(code & (sign_bit - 1)) / double(sign_bit);
    const double step = steps[level];
    return (v - center) * step + ((code & sign_bit) ? -magnitude : magnitude) * step;
}

} // namespace

Quat decompress_rotation(const std::array<uint8_t, 3>& format, uint32_t code) {
    const double x = component(format[0], (code >> 21) & 0x7ff, 11);
    const double y = component(format[1], (code >> 10) & 0x7ff, 11);
    const double z = component(format[2], code & 0x3ff, 10);
    const double w = std::sqrt(std::max(0.0, 1 - x * x - y * y - z * z));
    return normalized(Quat{float(w), float(x), float(y), float(z)});
}

KeyframeAnimation parse_keyframe_animation(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    const bool compressed = root.tag == "CKAT";
    if (root.tag != "KFAT" && !compressed) {
        throw std::runtime_error("not a keyframe animation (" + root.tag + ")");
    }
    const IffNode* version = version_of(root);
    if (version == nullptr || version->tag != (compressed ? "0001" : "0003")) {
        throw std::runtime_error(root.tag + ": unsupported version");
    }
    KeyframeAnimation out;
    IffReader info(version->expect("INFO").data);
    out.fps = info.f32();
    out.frames = compressed ? info.u16() : info.i32();

    // Animated and static rotations and translations.
    std::vector<std::vector<std::pair<int, Quat>>> rotations;
    if (const IffNode* arot = version->child("AROT")) {
        for (const IffNode* channel : arot->children_with("QCHN")) {
            IffReader r(channel->data);
            std::vector<std::pair<int, Quat>> keys;
            if (compressed) {
                const uint16_t count = r.u16();
                const std::array<uint8_t, 3> format{r.u8(), r.u8(), r.u8()};
                for (uint16_t i = 0; i < count; ++i) {
                    const int frame = r.u16();
                    keys.push_back({frame, decompress_rotation(format, r.u32())});
                }
            } else {
                const int32_t count = r.i32();
                for (int32_t i = 0; i < count; ++i) {
                    const int frame = r.i32();
                    Quat q;
                    q.w = r.f32(), q.x = r.f32(), q.y = r.f32(), q.z = r.f32();
                    keys.push_back({frame, normalized(q)});
                }
            }
            rotations.push_back(std::move(keys));
        }
    }
    std::vector<Quat> static_rotations;
    if (const IffNode* srot = version->child("SROT")) {
        IffReader r(srot->data);
        while (r.remaining() >= (compressed ? 7u : 16u)) {
            if (compressed) {
                const std::array<uint8_t, 3> format{r.u8(), r.u8(), r.u8()};
                static_rotations.push_back(decompress_rotation(format, r.u32()));
            } else {
                Quat q;
                q.w = r.f32(), q.x = r.f32(), q.y = r.f32(), q.z = r.f32();
                static_rotations.push_back(normalized(q));
            }
        }
    }
    std::vector<std::vector<std::pair<int, float>>> translations;
    if (const IffNode* atrn = version->child("ATRN")) {
        for (const IffNode* channel : atrn->children_with("CHNL")) {
            IffReader r(channel->data);
            const int count = compressed ? r.u16() : r.i32();
            std::vector<std::pair<int, float>> keys;
            for (int i = 0; i < count; ++i) {
                const int frame = compressed ? r.u16() : r.i32();
                keys.push_back({frame, r.f32()});
            }
            translations.push_back(std::move(keys));
        }
    }
    std::vector<float> static_translations;
    if (const IffNode* strn = version->child("STRN")) {
        IffReader r(strn->data);
        while (r.remaining() >= 4) {
            static_translations.push_back(r.f32());
        }
    }

    for (const IffNode* xfin : version->expect("XFRM").children_with("XFIN")) {
        IffReader r(xfin->data);
        KeyframeAnimation::Track track;
        track.joint = r.cstring();
        const bool animated = r.u8() != 0;
        const size_t rotation = compressed ? r.u16() : size_t(r.i32());
        const uint32_t mask = compressed ? r.u8() : r.u32();
        size_t axis_index[3];
        for (size_t& index : axis_index) {
            index = compressed ? r.u16() : size_t(r.i32());
        }
        if (animated && rotation < rotations.size()) {
            track.rotation = rotations[rotation];
        } else if (!animated && rotation < static_rotations.size()) {
            track.rotation = {{0, static_rotations[rotation]}};
        }
        for (int axis = 0; axis < 3; ++axis) {
            const bool moving = mask & (0x08u << axis);
            if (moving && axis_index[axis] < translations.size()) {
                track.translation[size_t(axis)] = translations[axis_index[axis]];
            } else if (!moving && axis_index[axis] < static_translations.size()) {
                track.translation[size_t(axis)] = {{0, static_translations[axis_index[axis]]}};
            }
        }
        out.tracks.push_back(std::move(track));
    }
    return out;
}

namespace {

Quat slerp(Quat a, Quat b, float t) {
    float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (d < 0) {
        b = {-b.w, -b.x, -b.y, -b.z};
        d = -d;
    }
    if (d > 0.9995f) {
        return normalized(Quat{a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                               a.z + (b.z - a.z) * t});
    }
    const float angle = std::acos(d);
    const float sa = std::sin((1 - t) * angle) / std::sin(angle), sb = std::sin(t * angle) / std::sin(angle);
    return {a.w * sa + b.w * sb, a.x * sa + b.x * sb, a.y * sa + b.y * sb, a.z * sa + b.z * sb};
}

template <class T, class Mix>
T at_frame(const std::vector<std::pair<int, T>>& keys, float frame, T fallback, Mix mix) {
    if (keys.empty()) {
        return fallback;
    }
    if (frame <= float(keys.front().first)) {
        return keys.front().second;
    }
    for (size_t i = 1; i < keys.size(); ++i) {
        if (frame <= float(keys[i].first)) {
            const float span = float(keys[i].first - keys[i - 1].first);
            const float t = span > 0 ? (frame - float(keys[i - 1].first)) / span : 0;
            return mix(keys[i - 1].second, keys[i].second, t);
        }
    }
    return keys.back().second;
}

} // namespace

AnimationClip sample_animation(const KeyframeAnimation& animation, const Skeleton& skeleton, const std::string& name) {
    AnimationClip clip;
    clip.name = name;
    clip.fps = animation.fps;
    std::vector<const KeyframeAnimation::Track*> tracks(skeleton.joints.size(), nullptr);
    for (size_t j = 0; j < skeleton.joints.size(); ++j) {
        for (const KeyframeAnimation::Track& track : animation.tracks) {
            if (same_name(track.joint, skeleton.joints[j].name)) {
                tracks[j] = &track;
            }
        }
    }
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    for (int frame = 0; frame < std::max(1, animation.frames); ++frame) {
        std::vector<Quat> rotations;
        for (size_t j = 0; j < skeleton.joints.size(); ++j) {
            const Joint& joint = skeleton.joints[j];
            if (tracks[j] == nullptr || tracks[j]->rotation.empty()) {
                rotations.push_back(joint.rotation);
                continue;
            }
            const Quat key = at_frame(tracks[j]->rotation, float(frame), Quat{}, slerp);
            rotations.push_back(normalized(joint.post * key * joint.pre));
        }
        clip.rotations.push_back(std::move(rotations));
        Vec3 root = skeleton.joints.empty() ? Vec3{} : skeleton.joints[0].translation;
        if (!skeleton.joints.empty() && tracks[0] != nullptr) {
            root.x += at_frame(tracks[0]->translation[0], float(frame), 0.0f, lerp);
            root.y += at_frame(tracks[0]->translation[1], float(frame), 0.0f, lerp);
            root.z += at_frame(tracks[0]->translation[2], float(frame), 0.0f, lerp);
        }
        clip.root_translations.push_back(root);
    }
    return clip;
}

} // namespace swbfo::swg
