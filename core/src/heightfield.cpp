#include "swm/core/heightfield.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace swm::core {

Heightfield::Heightfield(int samples, double spacing, double origin_x, double origin_z, std::vector<float> heights)
    : samples_(samples), spacing_(spacing), origin_x_(origin_x), origin_z_(origin_z), heights_(std::move(heights)) {
    if (samples < 2 || spacing <= 0 || heights_.size() != size_t(samples) * size_t(samples)) {
        throw std::runtime_error("heightfield: bad grid layout");
    }
}

Vec3 Heightfield::point(int column, int row) const {
    return {-(origin_x_ + column * spacing_), heights_[size_t(row) * samples_ + column], origin_z_ + row * spacing_};
}

Heightfield::Triangle Heightfield::triangle_at(double x, double z) const {
    // Grid coordinates; game x runs opposite to the columns.
    const double gc = std::clamp((-x - origin_x_) / spacing_, 0.0, double(samples_ - 1));
    const double gr = std::clamp((z - origin_z_) / spacing_, 0.0, double(samples_ - 1));
    const int column = std::min(int(gc), samples_ - 2);
    const int row = std::min(int(gr), samples_ - 2);
    const Vec3 a = point(column, row);
    const Vec3 b = point(column + 1, row);
    const Vec3 c = point(column, row + 1);
    const Vec3 d = point(column + 1, row + 1);
    // Cells split along the b-c diagonal, like the rendered mesh.
    if ((gc - column) + (gr - row) <= 1) {
        return {a, b, c};
    }
    return {d, c, b};
}

double Heightfield::height_at(double x, double z) const {
    if (empty()) {
        return 0;
    }
    const Triangle t = triangle_at(x, z);
    // t.a is the right-angle corner; t.b differs from it in x, t.c in z.
    const double u = (x - t.a.x) / (t.b.x - t.a.x);
    const double v = (z - t.a.z) / (t.c.z - t.a.z);
    return t.a.y + (t.b.y - t.a.y) * std::clamp(u, 0.0, 1.0) + (t.c.y - t.a.y) * std::clamp(v, 0.0, 1.0);
}

Vec3 Heightfield::normal_at(double x, double z) const {
    if (empty()) {
        return {0, 1, 0};
    }
    const Triangle t = triangle_at(x, z);
    const Vec3 e1 = t.b - t.a;
    const Vec3 e2 = t.c - t.a;
    Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
    if (n.y < 0) {
        n = n * -1;
    }
    return n * (1 / n.length());
}

Vec3 Heightfield::clamp(Vec3 position) const {
    if (empty()) {
        return position;
    }
    const double extent = (samples_ - 1) * spacing_;
    position.x = std::clamp(position.x, -(origin_x_ + extent), -origin_x_);
    position.z = std::clamp(position.z, origin_z_, origin_z_ + extent);
    return position;
}

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Reads a top-level number from the bake's JSON. Only for our own bake files,
// whose layout keys are numbers that appear before any nested object.
double json_number(const std::string& json, const std::string& key) {
    const size_t at = json.find("\"" + key + "\"");
    const size_t colon = at == std::string::npos ? at : json.find(':', at);
    if (colon == std::string::npos) {
        throw std::runtime_error("bake metadata has no " + key);
    }
    return std::stod(json.substr(colon + 1, 32));
}

} // namespace

Heightfield load_bake(const std::filesystem::path& prefix) {
    const std::string json = read_file(prefix.string() + ".json");
    const std::string raw = read_file(prefix.string() + ".r32");
    const int samples = int(json_number(json, "samples"));
    std::vector<float> heights(raw.size() / sizeof(float));
    std::memcpy(heights.data(), raw.data(), heights.size() * sizeof(float));
    return {samples, json_number(json, "spacing"), json_number(json, "origin_x"), json_number(json, "origin_z"),
            std::move(heights)};
}

} // namespace swm::core
