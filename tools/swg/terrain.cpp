#include "swg/terrain.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>

#include "swg/iff.h"

namespace swm::swg {
namespace {

constexpr double pi = 3.14159265358979323846;

double feathered(Feather feather, double t) {
    t = std::clamp(t, 0.0, 1.0);
    switch (feather) {
    case Feather::linear: return t;
    case Feather::ease_in: return t * t;
    case Feather::ease_out: return 1 - (1 - t) * (1 - t);
    case Feather::ease_in_out: return t * t * (3 - 2 * t);
    }
    return t;
}

Feather to_feather(int32_t value) {
    return value >= 0 && value <= 3 ? Feather(value) : Feather::linear;
}

// 1 inside [lo, hi], feathered to 0 at the edges over `fraction` of the range.
double range_amount(double value, double lo, double hi, Feather feather, double fraction) {
    if (value < lo || value > hi) {
        return 0;
    }
    const double width = (hi - lo) * fraction * 0.5;
    if (width <= 0) {
        return 1;
    }
    return feathered(feather, std::min(value - lo, hi - value) / width);
}

double distance_to_segment(double px, double pz, const std::array<double, 2>& a, const std::array<double, 2>& b) {
    const double dx = b[0] - a[0], dz = b[1] - a[1];
    const double length_squared = dx * dx + dz * dz;
    double t = length_squared > 0 ? ((px - a[0]) * dx + (pz - a[1]) * dz) / length_squared : 0;
    t = std::clamp(t, 0.0, 1.0);
    const double cx = a[0] + t * dx - px, cz = a[1] + t * dz - pz;
    return std::sqrt(cx * cx + cz * cz);
}

double bias_function(double b, double t) {
    return t <= 0 ? 0 : std::pow(t, std::log(std::max(b, 1e-6)) / std::log(0.5));
}

double gain_function(double g, double t) {
    return t < 0.5 ? bias_function(1 - g, 2 * t) / 2 : 1 - bias_function(1 - g, 2 - 2 * t) / 2;
}

// --- parsing --------------------------------------------------------------

// The version form inside a node: FORM <type> -> FORM <version>.
const IffNode& version_of(const IffNode& node) {
    for (const IffNode& child : node.children) {
        if (child.form) {
            return child;
        }
    }
    throw std::runtime_error("terrain: '" + node.tag + "' has no version form");
}

// IHDR -> (enabled, name)
std::pair<bool, std::string> read_header(const IffNode& version) {
    const IffNode* ihdr = version.child("IHDR");
    if (!ihdr) {
        return {true, {}};
    }
    IffReader reader(version_of(*ihdr).expect("DATA").data);
    const bool enabled = reader.i32() != 0;
    return {enabled, reader.cstring()};
}

// Parameter bytes: a DATA chunk, or a DATA form's PARM (or inner DATA) chunk.
std::span<const uint8_t> read_parameters(const IffNode& version) {
    for (const IffNode& child : version.children) {
        if (!child.form && child.tag == "DATA") {
            return child.data;
        }
        if (child.form && child.tag == "DATA") {
            if (const IffNode* parm = child.child("PARM")) {
                return parm->data;
            }
            if (const IffNode* data = child.child("DATA")) {
                return data->data;
            }
        }
    }
    return {};
}

std::vector<std::array<double, 2>> read_points(IffReader& reader) {
    const int32_t count = reader.i32();
    std::vector<std::array<double, 2>> points;
    for (int32_t i = 0; i < count; ++i) {
        const double x = reader.f32();
        points.push_back({x, double(reader.f32())});
    }
    return points;
}

bool parse_boundary(const IffNode& node, TerrainBoundary& out) {
    IffReader r(read_parameters(version_of(node)));
    if (node.tag == "BCIR") {
        out.kind = TerrainBoundary::Kind::circle;
        out.center_x = r.f32();
        out.center_z = r.f32();
        out.radius = r.f32();
    } else if (node.tag == "BREC") {
        out.kind = TerrainBoundary::Kind::rectangle;
        const double x0 = r.f32(), z0 = r.f32(), x1 = r.f32(), z1 = r.f32();
        out.min_x = std::min(x0, x1);
        out.max_x = std::max(x0, x1);
        out.min_z = std::min(z0, z1);
        out.max_z = std::max(z0, z1);
    } else if (node.tag == "BPOL") {
        out.kind = TerrainBoundary::Kind::polygon;
        out.points = read_points(r);
    } else if (node.tag == "BPLN") {
        out.kind = TerrainBoundary::Kind::polyline;
        out.points = read_points(r);
    } else {
        return false;
    }
    out.feather = to_feather(r.i32());
    out.feather_amount = r.f32();
    if (out.kind == TerrainBoundary::Kind::polyline) {
        out.width = r.f32();
    }
    return true;
}

bool parse_filter(const IffNode& node, TerrainFilter& out) {
    IffReader r(read_parameters(version_of(node)));
    if (node.tag == "FHGT" || node.tag == "FSLP") {
        out.kind = node.tag == "FHGT" ? TerrainFilter::Kind::height : TerrainFilter::Kind::slope;
        out.min = r.f32();
        out.max = r.f32();
        out.feather = to_feather(r.i32());
        out.feather_amount = r.f32();
    } else if (node.tag == "FFRA") {
        out.kind = TerrainFilter::Kind::fractal;
        out.fractal_id = r.i32();
        out.feather = to_feather(r.i32());
        out.feather_amount = r.f32();
        out.min = r.f32();
        out.max = r.f32();
        out.fractal_scale = r.f32();
    } else if (node.tag == "FDIR" || node.tag == "FSHD") {
        out.kind = TerrainFilter::Kind::ignored;  // needs aspect / shader maps; treated as passing
    } else {
        return false;
    }
    return true;
}

bool parse_height_affector(const IffNode& node, TerrainAffector& out) {
    IffReader r(read_parameters(version_of(node)));
    if (node.tag == "AHCN") {
        out.kind = TerrainAffector::Kind::height_constant;
        out.operation = r.i32();
        out.value = r.f32();
    } else if (node.tag == "AHFR") {
        out.kind = TerrainAffector::Kind::height_fractal;
        out.fractal_id = r.i32();
        out.operation = r.i32();
        out.value = r.f32();
    } else if (node.tag == "AHTR") {
        out.kind = TerrainAffector::Kind::height_terrace;
        out.terrace_fraction = r.f32();
        out.value = r.f32();
    } else {
        return false;
    }
    return true;
}

TerrainLayer parse_layer(const IffNode& layr) {
    const IffNode& version = version_of(layr);
    TerrainLayer layer;
    std::tie(layer.enabled, layer.name) = read_header(version);
    if (const IffNode* adta = version.child("ADTA")) {
        IffReader r(adta->data);
        layer.invert_boundaries = r.i32() != 0;
        layer.invert_filters = r.i32() != 0;
    }
    for (const IffNode& child : version.children) {
        if (!child.form || child.tag == "IHDR") {
            continue;
        }
        if (child.tag == "LAYR") {
            layer.children.push_back(parse_layer(child));
            layer.steps.push_back({.is_layer = true, .layer = layer.children.size() - 1});
            continue;
        }
        if (!read_header(version_of(child)).first) {
            continue;  // disabled
        }
        try {
            TerrainBoundary boundary;
            TerrainFilter filter;
            TerrainAffector affector;
            if (parse_boundary(child, boundary)) {
                layer.boundaries.push_back(std::move(boundary));
            } else if (parse_filter(child, filter)) {
                layer.filters.push_back(filter);
            } else if (parse_height_affector(child, affector)) {
                layer.steps.push_back({.affector = affector});
            }
        } catch (const std::runtime_error&) {
            // A node version with a shorter layout than expected; skip it.
        }
    }
    return layer;
}

Fractal parse_fractal(const IffNode& mfrc) {
    IffReader r(version_of(mfrc).expect("DATA").data);
    Fractal f;
    f.seed = r.i32();
    f.use_bias = r.i32() != 0;
    f.bias = r.f32();
    f.use_gain = r.i32() != 0;
    f.gain = r.f32();
    f.octaves = std::max(1, r.i32());
    f.octave_frequency = r.f32();
    f.octave_amplitude = r.f32();
    f.frequency_x = r.f32();
    f.frequency_z = r.f32();
    f.offset_x = r.f32();
    f.offset_z = r.f32();
    f.combination = r.i32();
    f.initialize();
    return f;
}

// --- evaluation -----------------------------------------------------------

struct Bounds {
    double min_x, min_z, max_x, max_z;
    bool overlaps(const Bounds& o) const {
        return min_x <= o.max_x && max_x >= o.min_x && min_z <= o.max_z && max_z >= o.min_z;
    }
};

Bounds bounds_of(const TerrainBoundary& b) {
    switch (b.kind) {
    case TerrainBoundary::Kind::circle:
        return {b.center_x - b.radius, b.center_z - b.radius, b.center_x + b.radius, b.center_z + b.radius};
    case TerrainBoundary::Kind::rectangle:
        return {b.min_x, b.min_z, b.max_x, b.max_z};
    default: {
        Bounds r{1e30, 1e30, -1e30, -1e30};
        for (const auto& p : b.points) {
            r = {std::min(r.min_x, p[0]), std::min(r.min_z, p[1]), std::max(r.max_x, p[0]), std::max(r.max_z, p[1])};
        }
        const double pad = b.kind == TerrainBoundary::Kind::polyline ? b.width : 0;
        return {r.min_x - pad, r.min_z - pad, r.max_x + pad, r.max_z + pad};
    }
    }
}

class Evaluator {
public:
    Evaluator(const ProceduralTerrain& terrain, HeightGrid& grid) : terrain_(terrain), grid_(grid) {
        const double far = grid.origin_x + grid.spacing * (grid.samples - 1);
        const double far_z = grid.origin_z + grid.spacing * (grid.samples - 1);
        region_ = {grid.origin_x, grid.origin_z, far, far_z};
    }

    void run() {
        const std::vector<float> everywhere(grid_.heights.size(), 1.0f);
        for (const TerrainLayer& layer : terrain_.layers) {
            process(layer, everywhere);
        }
    }

private:
    double world_x(int column) const { return grid_.origin_x + column * grid_.spacing; }
    double world_z(int row) const { return grid_.origin_z + row * grid_.spacing; }

    const Fractal* fractal(int32_t id) const {
        const auto it = terrain_.fractals.find(id);
        return it == terrain_.fractals.end() ? nullptr : &it->second;
    }

    double slope_degrees(int column, int row) const {
        const int n = grid_.samples;
        const int c0 = std::max(column - 1, 0), c1 = std::min(column + 1, n - 1);
        const int r0 = std::max(row - 1, 0), r1 = std::min(row + 1, n - 1);
        const double dx = (grid_.at(c1, row) - grid_.at(c0, row)) / ((c1 - c0) * grid_.spacing);
        const double dz = (grid_.at(column, r1) - grid_.at(column, r0)) / ((r1 - r0) * grid_.spacing);
        return std::atan(std::sqrt(dx * dx + dz * dz)) * 180.0 / pi;
    }

    void process(const TerrainLayer& layer, const std::vector<float>& parent) {
        if (!layer.enabled) {
            return;
        }
        // Skip layers whose boundaries don't touch the region at all.
        if (!layer.boundaries.empty() && !layer.invert_boundaries &&
            std::none_of(layer.boundaries.begin(), layer.boundaries.end(),
                         [&](const TerrainBoundary& b) { return bounds_of(b).overlaps(region_); })) {
            return;
        }

        const int n = grid_.samples;
        std::vector<float> amount(parent);
        for (int row = 0; row < n; ++row) {
            for (int column = 0; column < n; ++column) {
                const size_t i = size_t(row) * size_t(n) + size_t(column);
                if (amount[i] <= 0) {
                    continue;
                }
                const double x = world_x(column), z = world_z(row);
                if (!layer.boundaries.empty()) {
                    double b = 0;
                    for (const TerrainBoundary& boundary : layer.boundaries) {
                        b = std::max(b, boundary.amount(x, z));
                    }
                    amount[i] *= float(layer.invert_boundaries ? 1 - b : b);
                }
                if (!layer.filters.empty() && amount[i] > 0) {
                    double f = 1;
                    for (const TerrainFilter& filter : layer.filters) {
                        f = std::min(f, filter_amount(filter, column, row, x, z));
                    }
                    amount[i] *= float(layer.invert_filters ? 1 - f : f);
                }
            }
        }

        for (const TerrainLayer::Step& step : layer.steps) {
            if (step.is_layer) {
                process(layer.children[step.layer], amount);
            } else {
                apply(step.affector, amount);
            }
        }
    }

    double filter_amount(const TerrainFilter& filter, int column, int row, double x, double z) const {
        switch (filter.kind) {
        case TerrainFilter::Kind::height:
            return range_amount(grid_.at(column, row), filter.min, filter.max, filter.feather, filter.feather_amount);
        case TerrainFilter::Kind::slope:
            return range_amount(slope_degrees(column, row), filter.min, filter.max, filter.feather,
                                filter.feather_amount);
        case TerrainFilter::Kind::fractal: {
            const Fractal* f = fractal(filter.fractal_id);
            const double value = f ? f->sample(x, z) * filter.fractal_scale : 0;
            return range_amount(value, filter.min, filter.max, filter.feather, filter.feather_amount);
        }
        case TerrainFilter::Kind::ignored:
            return 1;
        }
        return 1;
    }

    void apply(const TerrainAffector& affector, const std::vector<float>& amount) {
        const int n = grid_.samples;
        const Fractal* f = affector.kind == TerrainAffector::Kind::height_fractal ? fractal(affector.fractal_id) : nullptr;
        for (int row = 0; row < n; ++row) {
            for (int column = 0; column < n; ++column) {
                const size_t i = size_t(row) * size_t(n) + size_t(column);
                if (amount[i] <= 0) {
                    continue;
                }
                const double h = grid_.heights[i];
                double target = h;
                if (affector.kind == TerrainAffector::Kind::height_terrace) {
                    const double step = affector.value;
                    if (step > 0) {
                        const double base = std::floor(h / step) * step;
                        const double t = (h - base) / step;
                        const double edge = std::max(affector.terrace_fraction, 1e-6);
                        target = base + step * std::clamp((t - (1 - edge)) / edge, 0.0, 1.0);
                    }
                } else {
                    const double value = affector.kind == TerrainAffector::Kind::height_fractal
                                             ? (f ? f->sample(world_x(column), world_z(row)) : 0) * affector.value
                                             : affector.value;
                    switch (affector.operation) {
                    case 0: target = value; break;
                    case 1: target = h + value; break;
                    case 2: target = h - value; break;
                    case 3: target = h * value; break;
                    default: break;
                    }
                }
                grid_.heights[i] = float(h + (target - h) * amount[i]);
            }
        }
    }

    const ProceduralTerrain& terrain_;
    HeightGrid& grid_;
    Bounds region_{};
};

} // namespace

void Fractal::initialize() {
    std::array<uint8_t, 256> values;
    std::iota(values.begin(), values.end(), uint8_t(0));
    std::mt19937 rng(static_cast<uint32_t>(seed));
    std::shuffle(values.begin(), values.end(), rng);
    for (size_t i = 0; i < 512; ++i) {
        permutation_[i] = values[i & 255];
    }
}

double Fractal::noise(double x, double z) const {
    const double fx = std::floor(x), fz = std::floor(z);
    const int xi = int(int64_t(fx) & 255), zi = int(int64_t(fz) & 255);
    const double dx = x - fx, dz = z - fz;
    auto fade = [](double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    auto grad = [](int hash, double gx, double gz) {
        switch (hash & 7) {
        case 0: return gx + gz;
        case 1: return gx - gz;
        case 2: return -gx + gz;
        case 3: return -gx - gz;
        case 4: return gx;
        case 5: return -gx;
        case 6: return gz;
        default: return -gz;
        }
    };
    const int aa = permutation_[size_t(permutation_[size_t(xi)] + zi)];
    const int ab = permutation_[size_t(permutation_[size_t(xi)] + zi + 1)];
    const int ba = permutation_[size_t(permutation_[size_t(xi + 1)] + zi)];
    const int bb = permutation_[size_t(permutation_[size_t(xi + 1)] + zi + 1)];
    const double u = fade(dx), v = fade(dz);
    const double x0 = grad(aa, dx, dz) + u * (grad(ba, dx - 1, dz) - grad(aa, dx, dz));
    const double x1 = grad(ab, dx, dz - 1) + u * (grad(bb, dx - 1, dz - 1) - grad(ab, dx, dz - 1));
    return x0 + v * (x1 - x0);
}

double Fractal::sample(double x, double z) const {
    const double sx = x * frequency_x + offset_x, sz = z * frequency_z + offset_z;
    double total = combination == 1 ? 1.0 : 0.0;
    double amplitude = 1, frequency = 1, max_amplitude = 0;
    for (int32_t o = 0; o < octaves; ++o) {
        const double n = std::clamp(noise(sx * frequency, sz * frequency), -1.0, 1.0);
        switch (combination) {
        case 1: total *= 1 + amplitude * n; break;
        case 2:
        case 4: total += amplitude * (1 - 2 * std::abs(n)); break;
        case 3:
        case 5: total += amplitude * (2 * std::abs(n) - 1); break;
        default: total += amplitude * n; break;
        }
        max_amplitude += amplitude;
        amplitude *= octave_amplitude;
        frequency *= octave_frequency;
    }
    double value = combination == 1 ? total * 0.5 : (total / std::max(max_amplitude, 1e-9) + 1) * 0.5;
    value = std::clamp(value, 0.0, 1.0);
    if (use_bias) {
        value = bias_function(bias, value);
    }
    if (use_gain) {
        value = gain_function(gain, value);
    }
    return value;
}

double TerrainBoundary::amount(double x, double z) const {
    switch (kind) {
    case Kind::circle: {
        const double d = std::hypot(x - center_x, z - center_z);
        if (d >= radius) {
            return 0;
        }
        const double feather_width = radius * feather_amount;
        return feather_width > 0 ? feathered(feather, (radius - d) / feather_width) : 1;
    }
    case Kind::rectangle: {
        if (x < min_x || x > max_x || z < min_z || z > max_z) {
            return 0;
        }
        const double edge = std::min({x - min_x, max_x - x, z - min_z, max_z - z});
        const double feather_width = feather_amount * std::min(max_x - min_x, max_z - min_z) * 0.5;
        return feather_width > 0 ? feathered(feather, edge / feather_width) : 1;
    }
    case Kind::polygon: {
        bool inside = false;
        double edge = 1e30;
        for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
            const auto& a = points[i];
            const auto& b = points[j];
            if ((a[1] > z) != (b[1] > z) && x < (b[0] - a[0]) * (z - a[1]) / (b[1] - a[1]) + a[0]) {
                inside = !inside;
            }
            edge = std::min(edge, distance_to_segment(x, z, a, b));
        }
        if (!inside) {
            return 0;
        }
        return feather_amount > 0 ? feathered(feather, edge / feather_amount) : 1;
    }
    case Kind::polyline: {
        double d = 1e30;
        for (size_t i = 1; i < points.size(); ++i) {
            d = std::min(d, distance_to_segment(x, z, points[i - 1], points[i]));
        }
        if (d >= width) {
            return 0;
        }
        const double feather_width = width * feather_amount;
        return feather_width > 0 ? feathered(feather, (width - d) / feather_width) : 1;
    }
    }
    return 0;
}

float HeightGrid::sample_world(double x, double z) const {
    const double gx = std::clamp((x - origin_x) / spacing, 0.0, double(samples - 1));
    const double gz = std::clamp((z - origin_z) / spacing, 0.0, double(samples - 1));
    const int c0 = std::min(int(gx), samples - 2), r0 = std::min(int(gz), samples - 2);
    const double tx = gx - c0, tz = gz - r0;
    const double top = at(c0, r0) + (at(c0 + 1, r0) - at(c0, r0)) * tx;
    const double bottom = at(c0, r0 + 1) + (at(c0 + 1, r0 + 1) - at(c0, r0 + 1)) * tx;
    return float(top + (bottom - top) * tz);
}

ProceduralTerrain parse_procedural_terrain(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "PTAT") {
        throw std::runtime_error("not a procedural terrain file (" + root.tag + ")");
    }
    const IffNode& version = version_of(root);
    const IffNode& generator = version_of(version.expect("TGEN"));

    ProceduralTerrain terrain;
    if (const IffNode* fractal_group = generator.child("MGRP")) {
        for (const IffNode* family : version_of(*fractal_group).children_with("MFAM")) {
            IffReader r(family->expect("DATA").data);
            terrain.fractals[r.i32()] = parse_fractal(family->expect("MFRC"));
        }
    }
    for (const IffNode* layer : generator.expect("LYRS").children_with("LAYR")) {
        terrain.layers.push_back(parse_layer(*layer));
    }
    return terrain;
}

HeightGrid bake_heights(const ProceduralTerrain& terrain, double center_x, double center_z, int samples, double spacing) {
    if (samples < 2) {
        throw std::runtime_error("terrain bake needs at least 2 samples per side");
    }
    HeightGrid grid;
    grid.samples = samples;
    grid.spacing = spacing;
    grid.origin_x = center_x - spacing * (samples - 1) * 0.5;
    grid.origin_z = center_z - spacing * (samples - 1) * 0.5;
    grid.heights.assign(size_t(samples) * size_t(samples), 0.0f);
    Evaluator(terrain, grid).run();
    return grid;
}

} // namespace swm::swg
