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

    const TerrainGrid grid = bake_terrain(terrain, 0, 0, 101, 1.0);  // -50..50 m
    CHECK(near(grid.sample_world(0, 0), 15));
    CHECK(near(grid.sample_world(40, 40), 10));

    // A disabled layer does nothing.
    terrain.layers[1].enabled = false;
    CHECK(near(bake_terrain(terrain, 0, 0, 101, 1.0).sample_world(0, 0), 10));
}

TerrainBoundary circle_at(double x, double radius, double feather) {
    TerrainBoundary circle;
    circle.kind = TerrainBoundary::Kind::circle;
    circle.center_x = x;
    circle.radius = radius;
    circle.feather_amount = feather;
    return circle;
}

void test_shaders() {
    ProceduralTerrain terrain;
    terrain.shader_families = {{.id = 1, .name = "sand"}, {.id = 2, .name = "rock"}, {.id = 3, .name = "road"}};
    // Sand everywhere.
    TerrainLayer base;
    base.steps.push_back({.affector = {.kind = TerrainAffector::Kind::shader_constant, .shader_family = 1}});
    // Rock in a circle feathered over its outer half; the default clamp (1)
    // keeps it to where the layer is fully on.
    TerrainLayer rock;
    rock.boundaries.push_back(circle_at(0, 20, 0.5));
    rock.steps.push_back({.affector = {.kind = TerrainAffector::Kind::shader_constant, .shader_family = 2}});
    // A clamp override of 0 spreads a texture over the whole feathered area.
    TerrainLayer wide;
    wide.boundaries.push_back(circle_at(40, 10, 0.5));
    wide.steps.push_back({.affector = {.kind = TerrainAffector::Kind::shader_constant,
                                       .shader_family = 2,
                                       .use_feather_clamp = true,
                                       .feather_clamp = 0}});
    // Replace rock with road, but only where rock is (shader filter + replace).
    TerrainLayer replace;
    TerrainFilter on_rock;
    on_rock.kind = TerrainFilter::Kind::shader;
    on_rock.shader_family = 2;
    replace.filters.push_back(on_rock);
    replace.boundaries.push_back(circle_at(0, 5, 0));
    replace.steps.push_back(
        {.affector = {.kind = TerrainAffector::Kind::shader_replace, .shader_family = 3, .replaced_family = 2}});
    terrain.layers = {base, rock, wide, replace};

    const TerrainGrid grid = bake_terrain(terrain, 20, 0, 121, 1.0);  // x -40..80
    const auto shader_at = [&](double x) { return grid.shaders[grid.index(int(std::lround(x - grid.origin_x)), 60)]; };
    CHECK(shader_at(-30) == 1);  // outside everything
    CHECK(shader_at(8) == 2);    // fully inside the rock circle
    CHECK(shader_at(15) == 1);   // rock's feathered edge: amount < 1
    CHECK(shader_at(48) == 2);   // the clamp-0 circle's feathered edge still counts
    CHECK(shader_at(2) == 3);    // replaced inside the small circle
}

void test_road() {
    ProceduralTerrain terrain;
    terrain.shader_families = {{.id = 7, .name = "road"}};
    TerrainLayer base;
    base.steps.push_back({.affector = {.kind = TerrainAffector::Kind::height_constant, .operation = 0, .value = 10}});
    TerrainLayer road_layer;
    TerrainAffector road{.kind = TerrainAffector::Kind::road};
    road.road.segments = {{{-50, 4, 0}, {50, 4, 0}}};  // along X at height 4
    road.road.width = 8;
    road.road.shader_family = 7;
    road.road.height_feather_amount = 0.5;  // outer half of each side blends
    road_layer.steps.push_back({.affector = road});
    terrain.layers = {base, road_layer};

    const TerrainGrid grid = bake_terrain(terrain, 0, 0, 41, 1.0);  // -20..20
    CHECK(near(grid.sample_world(0, 0), 4));     // on the road
    CHECK(near(grid.sample_world(0, 1), 4));     // inside the unfeathered core (2 m)
    CHECK(near(grid.sample_world(0, 3), 7));     // halfway through the 2 m feather: weight 0.5
    CHECK(near(grid.sample_world(0, 10), 10));   // off the road
    CHECK(grid.shaders[grid.index(20, 20)] == 7);
    CHECK(grid.shaders[grid.index(20, 30)] == -1);
}

} // namespace

int main() {
    test_circle();
    test_polygon();
    test_fractal();
    test_layers();
    test_shaders();
    test_road();

    if (failures == 0) {
        std::printf("terrain_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
