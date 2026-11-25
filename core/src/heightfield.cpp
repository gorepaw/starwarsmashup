#include "swm/core/heightfield.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
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

bool TerrainCut::contains(Vec3 point) const {
    for (const auto& [nx, ny, nz, d] : planes) {
        if (nx * point.x + ny * point.y + nz * point.z + d > 0) {
            return false;
        }
    }
    return !planes.empty();
}

bool TerrainHoles::at(double x, double z) const {
    const double column = std::floor((x - x0) / cell);
    const double row = std::floor((z - z0) / cell);
    if (cells.empty() || column < 0 || row < 0 || column >= width || row >= height) {
        return false;
    }
    return cells[size_t(row) * size_t(width) + size_t(column)] == 1;
}

namespace {

constexpr uint32_t holes_magic = 0x4C4F4848;  // "HHOL"
constexpr uint32_t holes_version = 1;

void write_terrain_holes(std::ostream& out, const TerrainHoles& holes) {
    const auto put = [&](const auto& value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
    put(holes_magic);
    put(holes_version);
    put(holes.x0);
    put(holes.z0);
    put(holes.cell);
    put(int32_t(holes.width));
    put(int32_t(holes.height));
    out.write(reinterpret_cast<const char*>(holes.cells.data()), std::streamsize(holes.cells.size()));
}

} // namespace

void save_terrain_holes(const TerrainHoles& holes, const std::filesystem::path& path) {
    save_terrain_hole_patches({holes}, path);
}

namespace {

// Reads one patch at the stream's position.
TerrainHoles read_terrain_holes(std::istream& in, const std::filesystem::path& path) {
    const auto get = [&](auto& value) { in.read(reinterpret_cast<char*>(&value), sizeof(value)); };
    uint32_t magic = 0, version = 0;
    int32_t width = 0, height = 0;
    TerrainHoles holes;
    get(magic);
    get(version);
    get(holes.x0);
    get(holes.z0);
    get(holes.cell);
    get(width);
    get(height);
    if (!in || magic != holes_magic || version != holes_version || width < 0 || height < 0 || holes.cell <= 0 ||
        int64_t(width) * height > (int64_t(1) << 28)) {
        throw std::runtime_error(path.string() + ": not a terrain holes file");
    }
    holes.width = width;
    holes.height = height;
    holes.cells.resize(size_t(width) * size_t(height));
    in.read(reinterpret_cast<char*>(holes.cells.data()), std::streamsize(holes.cells.size()));
    if (!in) {
        throw std::runtime_error(path.string() + ": truncated");
    }
    return holes;
}

} // namespace

TerrainHoles load_terrain_holes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    return read_terrain_holes(in, path);
}

std::vector<TerrainHoles> load_terrain_hole_patches(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::vector<TerrainHoles> patches;
    while (in.peek() != std::char_traits<char>::eof()) {
        patches.push_back(read_terrain_holes(in, path));
    }
    return patches;
}

void save_terrain_hole_patches(const std::vector<TerrainHoles>& patches, const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    for (const TerrainHoles& patch : patches) {
        write_terrain_holes(out, patch);
    }
    if (!out) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

bool Heightfield::cut_at(double x, double z) const {
    for (const TerrainHoles& patch : holes_) {
        if (patch.at(x, z)) {
            return true;
        }
    }
    if (cuts_.empty()) {
        return false;
    }
    const Vec3 surface{x, height_at(x, z), z};
    for (const TerrainCut& cut : cuts_) {
        const Vec3 d = surface - cut.center;
        if (d.x * d.x + d.z * d.z <= cut.radius * cut.radius && cut.contains(surface)) {
            return true;
        }
    }
    return false;
}

double Heightfield::lowest() const {
    return heights_.empty() ? 0.0 : double(*std::min_element(heights_.begin(), heights_.end()));
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
    Heightfield field{samples, json_number(json, "spacing"), json_number(json, "origin_x"),
                      json_number(json, "origin_z"), std::move(heights)};
    if (std::ifstream in(prefix.string() + ".cuts"); in) {
        std::vector<TerrainCut> cuts;
        for (std::string line; std::getline(in, line);) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            std::istringstream fields(line);
            TerrainCut cut;
            if (!(fields >> cut.center.x >> cut.center.y >> cut.center.z >> cut.radius)) {
                throw std::runtime_error(prefix.string() + ".cuts: bad line \"" + line + "\"");
            }
            for (std::array<double, 4> plane; fields >> plane[0] >> plane[1] >> plane[2] >> plane[3];) {
                cut.planes.push_back(plane);
            }
            // A little slack: the sphere bounds the volume, the surface may
            // touch its edge.
            cut.radius += 0.5;
            cuts.push_back(std::move(cut));
        }
        field.set_cuts(std::move(cuts));
    }
    if (const std::filesystem::path holes = prefix.string() + ".holes"; std::filesystem::exists(holes)) {
        field.set_hole_patches(load_terrain_hole_patches(holes));
    }
    return field;
}

} // namespace swm::core
