// Galaxies keyframe animations: compressed rotations, and a small synthetic
// CKAT clip (no game data) read and sampled for a skeleton.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "swg/keyframe_animation.h"

using namespace swm;
using namespace swm::swg;

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

bool near(double a, double b, double tolerance = 1e-4) {
    return std::abs(a - b) < tolerance;
}

uint32_t code(uint32_t x, uint32_t y, uint32_t z) {
    return (x << 21) | (y << 10) | z;
}

void test_decompress() {
    // Identity: format 0x9f (a step of 1/32, its offset just under the
    // middle), each code half a step up from it.
    const Quat identity = decompress_rotation({0x9f, 0x9f, 0x9f}, 0x3fe7fcff);
    CHECK(near(identity.w, 1, 1e-3) && near(identity.x, 0, 1e-3) && near(identity.y, 0, 1e-3) && near(identity.z, 0, 1e-3));
    CHECK(identity.x == decompress_rotation({0x9f, 0x9f, 0x9f}, code(511, 511, 255)).x);
    // ...or from just over the middle (0xa0), half a step down (sign bit set).
    const Quat other = decompress_rotation({0xa0, 0xa0, 0xa0}, code(1024 + 511, 1024 + 511, 512 + 255));
    CHECK(near(other.x, 0, 1e-3) && near(other.y, 0, 1e-3) && near(other.z, 0, 1e-3));
    // A thumb's y: offset half a step up, plus 525/1024 of a step.
    const Quat thumb = decompress_rotation({0x9f, 0xa0, 0x9f}, code(511, 525, 255));
    CHECK(near(thumb.y, 0.5 / 32 + 525.0 / 1024 / 32, 1e-4));
    // Coarser formats (more leading ones), bigger steps: 0xf4 is L = 4, a
    // step of 1/4, offset 4 of 0..7 (half a step up).
    const Quat elbow = decompress_rotation({0xf4, 0x9f, 0x9f}, code(512, 511, 255));
    CHECK(near(elbow.x, 0.5 / 4 + 0.5 / 4, 1e-3));
    // Always a unit rotation, w from the rest.
    CHECK(near(elbow.w * elbow.w + elbow.x * elbow.x + elbow.y * elbow.y + elbow.z * elbow.z, 1, 1e-4));
}

using Bytes = std::vector<uint8_t>;

Bytes chunk(const char* tag, const Bytes& payload) {
    Bytes out(tag, tag + 4);
    const auto size = static_cast<uint32_t>(payload.size());
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>(size >> shift));
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

Bytes form(const char* name, const std::vector<Bytes>& children) {
    Bytes payload(name, name + 4);
    for (const Bytes& child : children) {
        payload.insert(payload.end(), child.begin(), child.end());
    }
    return chunk("FORM", payload);
}

template <class T>
void put(Bytes& out, T value) {
    const auto* b = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), b, b + sizeof(T));
}

void test_clip() {
    // Two joints: "root" turning (animated rotation, y moving), "head" still.
    Bytes info;
    put(info, 30.0f);
    for (uint16_t v : {uint16_t(11), uint16_t(2), uint16_t(1), uint16_t(1), uint16_t(1), uint16_t(5)}) {
        put(info, v);
    }
    const auto xfin = [](const char* name, uint8_t animated, uint16_t rotation, uint8_t mask, uint16_t x, uint16_t y,
                         uint16_t z) {
        Bytes b(name, name + std::strlen(name) + 1);
        b.push_back(animated);
        put(b, rotation);
        b.push_back(mask);
        put(b, x);
        put(b, y);
        put(b, z);
        return chunk("XFIN", b);
    };
    // The root's rotation, x = -0.25 at both keys: format 0xf3 (L = 4, a step
    // of 1/4, offset 3 of 0..7: half a step down) and a code half a step down.
    Bytes qchn;
    put(qchn, uint16_t(2));
    for (uint8_t f : {uint8_t(0xf3), uint8_t(0x9f), uint8_t(0x9f)}) {
        qchn.push_back(f);
    }
    put(qchn, uint16_t(0));
    put(qchn, code(1024 + 512, 511, 255));
    put(qchn, uint16_t(10));
    put(qchn, code(1024 + 512, 511, 255));
    Bytes srot;
    for (uint8_t f : {uint8_t(0x9f), uint8_t(0x9f), uint8_t(0x9f)}) {
        srot.push_back(f);
    }
    put(srot, uint32_t(0x3fe7fcff));
    Bytes chnl;
    put(chnl, uint16_t(2));
    put(chnl, uint16_t(0));
    put(chnl, 0.0f);
    put(chnl, uint16_t(10));
    put(chnl, -0.1f);
    Bytes strn;
    for (int i = 0; i < 5; ++i) {
        put(strn, 0.0f);
    }
    const Bytes file = form("CKAT", {form("0001", {chunk("INFO", info),
                                                   form("XFRM", {xfin("root", 1, 0, 0x10, 0, 0, 1),
                                                                 xfin("head", 0, 0, 0, 2, 3, 4)}),
                                                   form("AROT", {chunk("QCHN", qchn)}), chunk("SROT", srot),
                                                   form("ATRN", {chunk("CHNL", chnl)}), chunk("STRN", strn)})});
    const KeyframeAnimation animation = parse_keyframe_animation(file);
    CHECK(animation.frames == 11 && animation.tracks.size() == 2 && animation.tracks[0].joint == "root");
    CHECK(animation.tracks[0].rotation.size() == 2 && animation.tracks[1].rotation.size() == 1);
    CHECK(near(animation.tracks[0].rotation[1].second.x, -0.25, 1e-3));
    CHECK(animation.tracks[0].translation[1].size() == 2 && animation.tracks[0].translation[0].size() == 1);

    Skeleton skeleton;
    skeleton.joints = {{.name = "root", .parent = -1, .translation = {0, 1, 0}},
                       {.name = "head", .parent = 0, .translation = {0, 0.5f, 0}}};
    const AnimationClip clip = sample_animation(animation, skeleton, "turn");
    CHECK(clip.rotations.size() == 11 && clip.root_translations.size() == 11);
    // The root bobs down 0.1 m from its bind translation, halfway at frame 5.
    CHECK(near(clip.root_translations[5].y, 0.95) && near(clip.root_translations[10].y, 0.9));
    CHECK(near(clip.rotations[10][1].w, 1, 1e-3));  // the head holds still
}

} // namespace

int main() {
    test_decompress();
    test_clip();
    if (failures == 0) {
        std::printf("keyframe_animation_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
