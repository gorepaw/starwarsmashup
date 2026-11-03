// Terrain boundaries, fractals, and layer evaluation on synthetic data.

#include <cmath>
#include <cstdio>

#include "swg/terrain.h"

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

bool near(double a, double b, double tolerance = 1e-6) {
    return std::fabs(a - b) < tolerance;
}

void test_circle() {
    TerrainBoundary circle;
    circle.kind = TerrainBoundary::Kind::circle;
    circle.radius = 100;
    circle.feather_amount = 0.5;  // feather over the outer 50 m
    CHECK(near(circle.amount(0, 0), 1));
    CHECK(near(circle.amount(40, 0), 1));
    CHECK(near(circle.amount(75, 0), 0.5));  // halfway through the feather, linear
    CHECK(near(circle.amount(100, 0), 0));
    CHECK(near(circle.amount(0, 150), 0));
}

void test_polygon() {
    TerrainBoundary square;
    square.kind = TerrainBoundary::Kind::polygon;
    square.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    square.feather_amount = 10;  // meters
    CHECK(near(square.amount(50, 50), 1));
    CHECK(near(square.amount(5, 50), 0.5));
    CHECK(near(square.amount(-5, 50), 0));
}

void test_fractal() {
    Fractal f;
    f.seed = 1234;
    f.octaves = 3;
    f.frequency_x = f.frequency_z = 0.01f;
    f.initialize();
    double lo = 1, hi = 0;
    for (int i = 0; i < 2000; ++i) {
        const double v = f.sample(i * 7.3, i * -3.1);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    CHECK(lo >= 0 && hi <= 1 && hi - lo > 0.2);  // in range, and not constant
    Fractal same = f;
    same.initialize();
    CHECK(near(f.sample(123.4, 567.8), same.sample(123.4, 567.8)));  // deterministic
}

void test_layers() {
    // Replace with 10 everywhere, then add 5 inside a circle.
    ProceduralTerrain terrain;
    TerrainLayer base;
    base.steps.push_back({.affector = {.kind = TerrainAffector::Kind::height_constant, .operation = 0, .value = 10}});
    TerrainLayer bump;
    TerrainBoundary circle;
    circle.kind = TerrainBoundary::Kind::circle;
    circle.radius = 20;
    bump.boundaries.push_back(circle);
    bump.steps.push_back({.affector = {.kind = TerrainAffector::Kind::height_constant, .operation = 1, .value = 5}});
    terrain.layers = {base, bump};

    const HeightGrid grid = bake_heights(terrain, 0, 0, 101, 1.0);  // -50..50 m
    CHECK(near(grid.sample_world(0, 0), 15));
    CHECK(near(grid.sample_world(40, 40), 10));

    // A disabled layer does nothing.
    terrain.layers[1].enabled = false;
    CHECK(near(bake_heights(terrain, 0, 0, 101, 1.0).sample_world(0, 0), 10));
}

} // namespace

int main() {
    test_circle();
    test_polygon();
    test_fractal();
    test_layers();

    if (failures == 0) {
        std::printf("terrain_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
