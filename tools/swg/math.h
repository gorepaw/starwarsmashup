#pragma once

#include <array>
#include <cmath>

namespace swm::swg {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

// Unit quaternion, w first (as stored in SWG files).
struct Quat {
    float w = 1, x = 0, y = 0, z = 0;
};

inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

inline Quat normalized(Quat q) {
    const float length = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return length > 0 ? Quat{q.w / length, q.x / length, q.y / length, q.z / length} : Quat{};
}

// 4x4 matrix, column-major (glTF order): element (row r, column c) is m[c * 4 + r].
struct Mat4 {
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    float& at(int row, int column) { return m[size_t(column) * 4 + size_t(row)]; }
    float at(int row, int column) const { return m[size_t(column) * 4 + size_t(row)]; }

    static Mat4 from_rotation_translation(Quat q, Vec3 t) {
        Mat4 r;
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        r.at(0, 0) = 1 - 2 * (yy + zz);
        r.at(0, 1) = 2 * (xy - wz);
        r.at(0, 2) = 2 * (xz + wy);
        r.at(1, 0) = 2 * (xy + wz);
        r.at(1, 1) = 1 - 2 * (xx + zz);
        r.at(1, 2) = 2 * (yz - wx);
        r.at(2, 0) = 2 * (xz - wy);
        r.at(2, 1) = 2 * (yz + wx);
        r.at(2, 2) = 1 - 2 * (xx + yy);
        r.at(0, 3) = t.x;
        r.at(1, 3) = t.y;
        r.at(2, 3) = t.z;
        return r;
    }

    // Inverse of a rotation + translation matrix.
    Mat4 inverse_rigid() const {
        Mat4 r;
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                r.at(row, column) = at(column, row);
            }
        }
        for (int row = 0; row < 3; ++row) {
            r.at(row, 3) = -(r.at(row, 0) * at(0, 3) + r.at(row, 1) * at(1, 3) + r.at(row, 2) * at(2, 3));
        }
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            float sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += a.at(row, k) * b.at(k, column);
            }
            r.at(row, column) = sum;
        }
    }
    return r;
}

inline Vec3 transform_point(const Mat4& m, Vec3 p) {
    return {m.at(0, 0) * p.x + m.at(0, 1) * p.y + m.at(0, 2) * p.z + m.at(0, 3),
            m.at(1, 0) * p.x + m.at(1, 1) * p.y + m.at(1, 2) * p.z + m.at(1, 3),
            m.at(2, 0) * p.x + m.at(2, 1) * p.y + m.at(2, 2) * p.z + m.at(2, 3)};
}

// SWG is left-handed; glTF is right-handed. Mirroring X converts between them
// and keeps the model facing +Z (glTF's front).
inline Vec3 to_gltf(Vec3 v) {
    return {-v.x, v.y, v.z};
}
inline Quat to_gltf(Quat q) {
    return {q.w, q.x, -q.y, -q.z};
}
inline Mat4 to_gltf(const Mat4& m) {
    Mat4 r = m;
    for (int i = 0; i < 4; ++i) {
        r.at(0, i) = -r.at(0, i);
        r.at(i, 0) = -r.at(i, 0);
    }
    return r;
}

} // namespace swm::swg
