// Decodes a small synthetic animation bank (no game data).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bf2/animation.h"
#include "bf2/hash.h"

using namespace swm;
using namespace swm::bf2;

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

using Bytes = std::vector<uint8_t>;

template <class T>
void put(Bytes& out, T value) {
    uint8_t raw[sizeof(T)];
    std::memcpy(raw, &value, sizeof(T));
    out.insert(out.end(), raw, raw + sizeof(T));
}

void put_block(Bytes& out, const char* tag, const Bytes& payload) {
    out.insert(out.end(), tag, tag + 4);
    put<uint32_t>(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

void test_name_hash() {
    // From the human_0 animation skeleton in ingame.lvl.
    CHECK(name_hash("dummyroot") == 0x4446e9b8u);
    CHECK(name_hash("DummyRoot") == 0x4446e9b8u);  // case-insensitive
}

void test_bank() {
    // One 4-frame animation, one bone.
    // Rotation streams (x, y, z, w):
    //   x: 0, then hold 3            -> 0, 0, 0, 0
    //   y: 0, then hold 3            -> 0, 0, 0, 0
    //   z: 0, deltas +10, +20, then absolute 100 -> 0, 10, 30, 100
    //   w: 2047, then hold 3         -> 2047 x4
    // Translation: x stream 0, +1, +1, +1 with multiplier 0.5 and base (1, 2, 3).
    Bytes data;
    auto stream_at = [&](std::initializer_list<uint8_t> bytes) {
        const auto offset = static_cast<uint32_t>(data.size());
        data.insert(data.end(), bytes);
        return offset;
    };
    const uint32_t x = stream_at({0, 0, 0x80, 3});
    const uint32_t y = stream_at({0, 0, 0x80, 3});
    const uint32_t z = stream_at({0, 0, 10, 20, 0x81, 100, 0});
    const uint32_t w = stream_at({0xff, 0x07, 0x80, 3});
    const uint32_t tx = stream_at({0, 0, 1, 1, 1});
    const uint32_t ty = stream_at({0, 0, 0x80, 3});
    const uint32_t tz = stream_at({0, 0, 0x80, 3});

    Bytes info;
    put<uint32_t>(info, 0);
    put<uint32_t>(info, 0);
    put<uint32_t>(info, name_hash("test_anim"));
    put<uint16_t>(info, 4);  // frames
    put<uint16_t>(info, 1);  // bones

    Bytes joints;
    put<uint32_t>(joints, name_hash("bone_root"));
    for (const uint32_t offset : {x, y, z, w, tx, ty, tz}) {
        put<uint32_t>(joints, offset);
    }
    for (const float v : {1.0f, 2.0f, 3.0f, 0.5f}) {
        put<float>(joints, v);
    }

    Bytes bin(4, 0);
    std::memcpy(bin.data(), "SMNA", 4);
    for (const uint32_t v : {0u, 8u, 1u, static_cast<uint32_t>(data.size()), 1u}) {
        put<uint32_t>(bin, v);
    }
    put_block(bin, "MINA", info);
    put_block(bin, "TNJA", joints);
    put_block(bin, "TADA", data);

    const std::vector<Animation> animations = parse_animation_bank(bin);
    CHECK(animations.size() == 1);
    if (animations.size() != 1) {
        return;
    }
    const Animation& animation = animations[0];
    CHECK(animation.name_hash == name_hash("test_anim"));
    CHECK(animation.frame_count == 4);
    const AnimationTrack* track = animation.track(name_hash("bone_root"));
    CHECK(track != nullptr);
    if (!track) {
        return;
    }
    // Frame 0 is the identity; later frames rotate about Z (normalized).
    CHECK(near(track->rotations[0].w, 1) && near(track->rotations[0].z, 0));
    const float z3 = 100.0f / 2047.0f, w3 = 1.0f;
    const float length = std::sqrt(z3 * z3 + w3 * w3);
    CHECK(near(track->rotations[3].z, z3 / length) && near(track->rotations[3].w, w3 / length));
    CHECK(near(track->translations[0].x, 1.0f) && near(track->translations[3].x, 2.5f));
    CHECK(near(track->translations[3].y, 2.0f) && near(track->translations[3].z, 3.0f));
}

} // namespace

int main() {
    test_name_hash();
    test_bank();

    if (failures == 0) {
        std::printf("animation_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
