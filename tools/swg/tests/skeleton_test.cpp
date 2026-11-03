// Builds small synthetic skeletons (no game data) and checks parsing and math.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "swg/skeleton.h"

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
    uint8_t raw[sizeof(T)];
    std::memcpy(raw, &value, sizeof(T));
    out.insert(out.end(), raw, raw + sizeof(T));
}

void put_quat(Bytes& out, Quat q) {
    put(out, q.w);
    put(out, q.x);
    put(out, q.y);
    put(out, q.z);
}

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-5f;
}

// 90 degrees about Y.
const Quat quarter_turn_y{std::sqrt(0.5f), 0, std::sqrt(0.5f), 0};

// root at the origin; "hand" one unit along X from root. root's rotation is
// split across RPRE / BPRO / RPST to exercise the composition order.
Bytes two_joint_skeleton(Quat pre, Quat bind, Quat post) {
    Bytes info, names, parents, rpre, rpst, bptr, bpro, jror;
    put<uint32_t>(info, 2);
    for (const char* name : {"root", "hand"}) {
        names.insert(names.end(), name, name + std::strlen(name) + 1);
    }
    put<int32_t>(parents, -1);
    put<int32_t>(parents, 0);
    put_quat(rpre, pre);
    put_quat(rpre, {});
    put_quat(rpst, post);
    put_quat(rpst, {});
    for (float v : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f}) {
        put(bptr, v);
    }
    put_quat(bpro, bind);
    put_quat(bpro, {});
    put<int32_t>(jror, 0);
    put<int32_t>(jror, 0);
    return form("SKTM", {form("0002", {chunk("INFO", info), chunk("NAME", names), chunk("PRNT", parents),
                                       chunk("RPRE", rpre), chunk("RPST", rpst), chunk("BPTR", bptr),
                                       chunk("BPRO", bpro), chunk("JROR", jror)})});
}

void test_parse_and_globals() {
    // A quarter turn about Y moves the child at +X to -Z.
    const Skeleton skeleton = parse_skeleton(two_joint_skeleton({}, quarter_turn_y, {}));
    CHECK(skeleton.joints.size() == 2);
    CHECK(skeleton.find("HAND") == 1);  // case-insensitive
    CHECK(skeleton.find("missing") == -1);
    const std::vector<Mat4> globals = skeleton.global_transforms();
    const Vec3 hand = transform_point(globals[1], {});
    CHECK(near(hand.x, 0) && near(hand.y, 0) && near(hand.z, -1));
}

void test_rotation_order() {
    // local rotation = post * bind * pre: with pre = 90 deg about Y, bind =
    // identity, and post = 90 deg about X, expect exactly post * pre.
    const Quat quarter_turn_x{std::sqrt(0.5f), std::sqrt(0.5f), 0, 0};
    const Skeleton skeleton = parse_skeleton(two_joint_skeleton(quarter_turn_y, {}, quarter_turn_x));
    const Quat expected = quarter_turn_x * quarter_turn_y;
    const Quat actual = skeleton.joints[0].rotation;
    CHECK(near(actual.w, expected.w) && near(actual.x, expected.x) && near(actual.y, expected.y) &&
          near(actual.z, expected.z));
}

void test_attach() {
    Skeleton body = parse_skeleton(two_joint_skeleton({}, {}, {}));
    const Skeleton face = parse_skeleton(two_joint_skeleton({}, {}, {}));
    body.attach(face, "hand");
    CHECK(body.joints.size() == 4);
    CHECK(body.joints[2].parent == 1);  // face root hangs off "hand"
    CHECK(body.joints[3].parent == 2);
    const Vec3 face_child = transform_point(body.global_transforms()[3], {});
    CHECK(near(face_child.x, 2));  // hand at x=1, plus one more unit
}

void test_gltf_mirror() {
    // Mirroring X: points flip X; a rotation about Y reverses direction.
    const Vec3 p = to_gltf(Vec3{1, 2, 3});
    CHECK(near(p.x, -1) && near(p.y, 2) && near(p.z, 3));
    const Quat q = to_gltf(quarter_turn_y);
    const Vec3 rotated = transform_point(Mat4::from_rotation_translation(q, {}), to_gltf(Vec3{1, 0, 0}));
    const Vec3 expected = to_gltf(transform_point(Mat4::from_rotation_translation(quarter_turn_y, {}), {1, 0, 0}));
    CHECK(near(rotated.x, expected.x) && near(rotated.y, expected.y) && near(rotated.z, expected.z));
}

} // namespace

int main() {
    test_parse_and_globals();
    test_rotation_order();
    test_attach();
    test_gltf_mirror();

    if (failures == 0) {
        std::printf("skeleton_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
