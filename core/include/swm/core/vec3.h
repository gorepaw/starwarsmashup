#pragma once

#include <cmath>

namespace swm::core {

// World-space vector in game coordinates: right-handed, Y up, meters, the
// same axes as Godot. Galaxies is left-handed, so game x = -Galaxies x.
struct Vec3 {
    double x = 0;
    double y = 0;
    double z = 0;

    friend Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
    friend bool operator==(Vec3 a, Vec3 b) = default;
    Vec3& operator+=(Vec3 b) { return *this = *this + b; }

    double length() const { return std::sqrt(x * x + y * y + z * z); }
};

} // namespace swm::core
