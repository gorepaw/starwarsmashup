// Reads a small synthetic Battlefront map (no game data): terrain patches,
// world layers in a sublevel, regions and paths.

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <cstring>
#include <string>
#include <vector>

#include "bf2/level.h"
#include "bf2/odf.h"
#include "bf2/ucfb.h"

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

template <typename T>
void put(Bytes& out, T value) {
    uint8_t raw[sizeof(T)];
    std::memcpy(raw, &value, sizeof(T));
    out.insert(out.end(), raw, raw + sizeof(T));
}

Bytes text(const std::string& s) {
    Bytes out(s.begin(), s.end());
    out.push_back(0);
    return out;
}

Bytes chunk(const char* tag, const Bytes& payload) {
    Bytes out(tag, tag + 4);
    put(out, uint32_t(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }
    return out;
}

Bytes join(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const Bytes& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

// A config record with numbers, or with a string.
Bytes record(const char* name, std::initializer_list<float> numbers) {
    Bytes out;
    put(out, odf_hash(name));
    put(out, uint8_t(numbers.size()));
    for (const float n : numbers) {
        put(out, n);
    }
    return chunk("DATA", out);
}

// A string first, then numbers: the string's value is its offset, past the
// values.
Bytes record(const char* name, const std::string& value, std::initializer_list<float> numbers = {}) {
    Bytes out;
    put(out, odf_hash(name));
    put(out, uint8_t(1 + numbers.size()));
    put(out, uint32_t(4 * (1 + numbers.size())));
    for (const float n : numbers) {
        put(out, n);
    }
    put(out, uint32_t(value.size() + 1));
    const Bytes t = text(value);
    out.insert(out.end(), t.begin(), t.end());
    return chunk("DATA", out);
}

// Water: a 4x4 grid of 8 m cells centred on the map, one layer ("pond", 1.5 m
// high). Cells (0, 0), (3, 0) and (2, 3) are wet; the last has the high bit
// set, as some maps' cells do.
Bytes water() {
    Bytes info;
    put(info, uint32_t(4));
    put(info, uint32_t(2));
    for (const float v : {8.0f, 16.0f, 16.0f}) {
        put(info, v);
    }
    put(info, uint32_t(1));
    Bytes layer = text("pond");
    put(layer, uint32_t(10));
    for (const float v : {1.5f, 0.1f, 0.2f, 2.0f, 3.0f}) {
        put(layer, v);
    }
    for (const uint8_t c : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4)}) {
        layer.push_back(c);
    }
    Bytes cells(8, 0);
    cells[0] = 0x10;  // (0, 0) wet, (1, 0) dry
    cells[1] = 0x01;  // (2, 0) dry, (3, 0) wet
    cells[7] = 0x90;  // (2, 3) wet, (3, 3) dry
    return chunk("WATR", join({chunk("INFO", info), chunk("LAYR", layer), chunk("WMAP", cells)}));
}

// A 16x16-cell terrain of 2x2 patches (8 cells each), 2 m cells: height x + z
// at each grid point (in cells). Patch (1,1) misses one grid point. Patch
// (0,0) uses layers mud (weight 200) and grass (55), lit (30, 20, 10); the
// others grass only, unlit.
Bytes terrain() {
    Bytes info;
    put(info, 2.0f);   // unit
    put(info, 0.01f);  // height scale
    put(info, 0.0f);
    put(info, 32.0f);
    put(info, uint16_t(16));  // cells
    put(info, uint16_t(8));   // patch cells
    put(info, uint32_t(0));
    put(info, uint32_t(0));
    Bytes patches = chunk("COMN", {});
    for (int pz = 0; pz < 2; ++pz) {
        for (int px = 0; px < 2; ++px) {
            const bool first = px == 0 && pz == 0;
            Bytes vertices, compressed;
            uint32_t count = 0;
            // A compressed vertex: int16 position and w (third layer's
            // weight), normal and w (second's), color and alpha (first's).
            const auto compress = [&](uint8_t first_weight, uint8_t second_weight) {
                const uint8_t v[16] = {0, 0, 0, 0, 0, 0, 0, 0, 127, 255, 127, second_weight, 0, 0, 0, first_weight};
                compressed.insert(compressed.end(), v, v + 16);
            };
            for (int z = 0; z <= 8; ++z) {
                for (int x = 0; x <= 8; ++x) {
                    if (px == 1 && pz == 1 && x == 4 && z == 4) {
                        continue;  // a hole
                    }
                    put(vertices, float(x * 2));
                    put(vertices, float(px * 8 + x + pz * 8 + z));
                    put(vertices, float(z * 2));
                    for (int pad = 0; pad < 3; ++pad) {
                        put(vertices, 0.0f);  // normal
                    }
                    for (const uint8_t b : first ? std::initializer_list<uint8_t>{10, 20, 30, 0}
                                                 : std::initializer_list<uint8_t>{255, 255, 255, 0}) {
                        vertices.push_back(b);  // baked light, B G R A
                    }
                    first ? compress(200, 55) : compress(255, 0);
                    ++count;
                }
            }
            // An off-grid vertex along a cut: ignored.
            for (const float v : {1.0f, 99.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f}) {
                put(vertices, v);
            }
            compress(0, 0);
            ++count;
            const auto buffer = [&](uint32_t stride, const Bytes& data) {
                Bytes vbuf;
                put(vbuf, count);
                put(vbuf, stride);
                put(vbuf, uint32_t(0));
                vbuf.insert(vbuf.end(), data.begin(), data.end());
                return chunk("VBUF", vbuf);
            };
            Bytes layers(8, 0);
            layers[0] = first ? 2 : 1;  // layers used: mud and grass, or grass
            layers[1] = first ? 1 : 0;
            patches = join({patches, chunk("PTCH", join({chunk("INFO", layers), buffer(16, compressed),
                                                         buffer(28, vertices)}))});
        }
    }
    Bytes scales;
    for (const float v : {8.0f, 16.0f, 1.0f, 1.0f}) {
        put(scales, v);
    }
    return chunk("tern", join({chunk("NAME", text("test")), chunk("INFO", info),
                               chunk("LTEX", join({text("grass"), text("mud"), text(""), text("")})),
                               chunk("DTLX", text("detail")), chunk("SCAL", scales), chunk("PCHS", patches),
                               water()}));
}

Bytes xfrm(float x, float y, float z) {
    Bytes out;
    for (const float v : {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, x, y, z}) {
        put(out, v);
    }
    return chunk("XFRM", out);
}

Bytes prop(const char* name, const std::string& value) {
    Bytes out;
    put(out, odf_hash(name));
    const Bytes t = text(value);
    out.insert(out.end(), t.begin(), t.end());
    return chunk("PROP", out);
}

// The conquest layer, in a sublevel: a command post, its capture region,
// and (in the sublevel's path group) its spawn path.
Bytes sublevel(uint32_t hash) {
    const Bytes post = chunk(
        "inst", join({chunk("INFO", join({chunk("TYPE", text("com_bldg_controlzone")), chunk("NAME", text("cp1")),
                                          xfrm(10, 1, 20)})),
                      prop("Team", "2"), prop("CaptureRegion", "cp1_capture"), prop("SpawnPath", "cp1_spawn")}));
    Bytes size;
    for (const float v : {8.0f, 8.0f, 8.0f}) {
        put(size, v);
    }
    const Bytes region = chunk("regn", join({chunk("INFO", join({chunk("TYPE", text("cylinder")),
                                                                 chunk("NAME", text("cp1_capture")), xfrm(11, 1, 21),
                                                                 chunk("SIZE", size)}))}));
    const Bytes world = chunk("wrld", join({chunk("NAME", text("test_conquest")), region, post}));
    Bytes body;
    put(body, hash);
    put(body, uint32_t(world.size()));
    body.insert(body.end(), world.begin(), world.end());
    return chunk("lvl_", body);
}

Bytes paths(uint32_t group, const std::string& name, float z) {
    Bytes name_hash;
    put(name_hash, group);
    const Bytes node = join({record("Node", {}),
                             chunk("SCOP", join({record("Position", {5.0f, 1.0f, z}), record("Rotation", {1, 0, 0, 0})}))});
    const Bytes nodes = join({record("Nodes", {1.0f}), chunk("SCOP", node)});
    return chunk("path", join({chunk("NAME", name_hash), record("Version", {10.0f}), record("PathCount", {1.0f}),
                               record("Path", name), chunk("SCOP", nodes)}));
}

// A group's boundaries: a Boundary record, then a scope of Path records,
// each its path's name hash, stored where a number would be.
Bytes boundaries(uint32_t group, std::initializer_list<const char*> names) {
    Bytes name_hash;
    put(name_hash, group);
    Bytes scope;
    for (const char* name : names) {
        Bytes out;
        put(out, odf_hash("Path"));
        put(out, uint8_t(1));
        put(out, odf_hash(name));
        const Bytes data = chunk("DATA", out);
        scope.insert(scope.end(), data.begin(), data.end());
    }
    return chunk("bnd_", join({chunk("NAME", name_hash), record("Boundary", {}), chunk("SCOP", scope)}));
}

// The sky: fog and view ranges in an info scope, the dome's texture and
// model. The lights: a sun, a lamp (z stored the other way around, like
// paths), and the global lights naming the sun.
Bytes sky_and_lights() {
    const Bytes sky = chunk(
        "sky_", join({chunk("NAME", Bytes(4, 0)), record("SkyInfo", {}),
                      chunk("SCOP", join({record("FogColor", {95, 111, 126}), record("NearSceneRange", {20, 30, 60, 80})})),
                      record("DomeInfo", {}),
                      chunk("SCOP", join({record("Texture", "test_sky"), record("DomeModel", {}),
                                          chunk("SCOP", record("Geometry", "test_dome"))}))}));
    const Bytes lights = chunk(
        "lght",
        join({chunk("NAME", Bytes(4, 0)), record("Light", "sun", {1}),
              chunk("SCOP", join({record("Rotation", {0.5f, 0.5f, -0.5f, 0.5f}), record("Type", {1}),
                                  record("Color", {0.4f, 0.5f, 0.6f})})),
              record("Light", "lamp", {1}),
              chunk("SCOP", join({record("Position", {1, 2, 3}), record("Type", {2}), record("Range", {5})})),
              record("GlobalLights", {}),
              chunk("SCOP", join({record("Light1", "sun"), record("Light2", "back"), record("Top", {101, 114, 127}),
                                  record("Bottom", {70, 90, 115})}))}));
    return join({sky, lights});
}

// The world's effects (water and a fog cloud), and a particle effect, which
// isn't one of them.
Bytes effects() {
    const Bytes world = chunk(
        "fx__", join({chunk("NAME", Bytes(4, 0)), record("Effect", "Water"),
                      chunk("SCOP", join({record("MainTexture", "test_water"), record("Velocity", {0.01f, 0.02f}),
                                          record("NormalMapTextures", "test_normal_", {16, 8})})),
                      record("Effect", "FogCloud"), chunk("SCOP", record("Range", {60, 100}))}));
    const Bytes particles = chunk("fx__", join({chunk("NAME", Bytes(4, 0)), record("ParticleEmitter", "Spark"),
                                                chunk("SCOP", record("Range", {1, 2}))}));
    return join({world, particles});
}

// An AI barrier for soldiers and hover vehicles at (10, 0, 20), 4 m along
// its local x and 2 m along z, turned so that local x points along +z.
Bytes barrier() {
    Bytes transform;
    for (const float v : {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 10.0f, 0.0f, 20.0f}) {
        put(transform, v);
    }
    Bytes size;
    for (const float v : {2.0f, 0.0f, 1.0f}) {
        put(size, v);
    }
    Bytes flags;
    put(flags, uint32_t(3));
    return chunk("BARR", chunk("INFO", join({chunk("NAME", text("barrier1")), chunk("XFRM", transform),
                                             chunk("SIZE", size), chunk("FLAG", flags)})));
}

void test_level() {
    const uint32_t conquest = 0x12345678;
    const Bytes file = chunk(
        "ucfb", join({terrain(),
                      chunk("wrld", join({chunk("NAME", text("test")),
                                          chunk("inst", chunk("INFO", join({chunk("TYPE", text("test_prop_tree")),
                                                                            chunk("NAME", text("tree1")),
                                                                            xfrm(-3, 2, 4)}))),
                                          barrier()})),
                      sublevel(conquest), sky_and_lights(), effects(),
                      // The same path name in another group, then in the conquest one.
                      paths(0x0badf00d, "cp1_spawn", -99), paths(conquest, "cp1_spawn", -30),
                      boundaries(0x0badf00d, {"warning1", "Warning2"})}));
    const Level level = read_level(parse_ucfb(file));

    const Terrain& t = level.terrain;
    CHECK(t.name == "test" && t.cells == 16 && t.unit == 2 && t.origin_x() == -16 && t.origin_z() == -14);
    CHECK(t.texture_layers.size() == 2 && t.texture_layers[1] == "mud");
    CHECK(t.heights.size() == 17 * 17 && t.missing == 1);
    CHECK(t.heights[5 * 17 + 3] == 8 && t.heights[16 * 17 + 16] == 32);
    CHECK(t.heights[12 * 17 + 12] == 24);  // the hole, filled from its neighbours

    // Texture layers: slots (trailing empties dropped), tiling, weights per
    // point (a patch's first-listed layer weighs its color alpha, the second
    // its normal w), baked light.
    CHECK(t.texture_layers.size() == 2 && t.texture_scales == (std::vector<float>{8, 16}));
    CHECK(t.detail_texture == "detail" && t.layer_weights.size() == 2 && t.light.size() == 17 * 17);
    CHECK(t.layer_weights[1][3 * 17 + 5] == 200 && t.layer_weights[0][3 * 17 + 5] == 55);  // patch (0,0)
    CHECK(t.layer_weights[0][12 * 17 + 13] == 255 && t.layer_weights[1][12 * 17 + 13] == 0);  // patch (1,1)
    CHECK(t.layer_weights[0][12 * 17 + 12] == 255);  // the hole: first layer
    CHECK(t.light[3 * 17 + 5] == (std::array<uint8_t, 3>{30, 20, 10}));
    CHECK(t.light[12 * 17 + 13] == (std::array<uint8_t, 3>{255, 255, 255}));

    const LevelSky& sky = level.sky;
    CHECK(sky.fog_color == (std::array<float, 3>{95, 111, 126}) && sky.near_scene_range.size() == 4);
    CHECK(sky.dome_texture == "test_sky" && sky.dome_models == (std::vector<std::string>{"test_dome"}));
    CHECK(sky.sun == "sun" && sky.back == "back" && sky.ambient_top[2] == 127 && sky.ambient_bottom[0] == 70);
    CHECK(level.lights.size() == 2 && level.lights[0].name == "sun" && level.lights[0].type == 1);
    CHECK(level.lights[0].rotation[2] == -0.5f && level.lights[0].color[1] == 0.5f);
    CHECK(level.lights[1].name == "lamp" && level.lights[1].type == 2 && level.lights[1].range == 5);
    CHECK(level.lights[1].position == (std::array<float, 3>{1, 2, -3}));

    CHECK(level.layers.size() == 2 && level.layers[1] == "test_conquest");
    CHECK(level.objects.size() == 2 && level.regions.size() == 1);
    const WorldItem& tree = level.objects[0];
    CHECK(tree.layer == "test" && tree.type == "test_prop_tree" && tree.position[0] == -3 && tree.rotation[4] == 1);
    const WorldItem& post = level.objects[1];
    CHECK(post.get("Team") == "2" && post.get("CaptureRegion") == "cp1_capture" && post.get("Missing").empty());
    const WorldItem* capture = level.region("cp1_capture");
    CHECK(capture && capture->type == "cylinder" && capture->size[0] == 8 && capture->position[2] == 21);

    CHECK(level.barriers.size() == 1);
    if (!level.barriers.empty()) {
        const LevelBarrier& b = level.barriers[0];
        CHECK(b.layer == "test" && b.name == "barrier1" && b.flags == 3 && b.half_size[0] == 2 && b.half_size[1] == 1);
        const auto corners = b.corners();
        CHECK(corners[0] == (std::array<float, 2>{9, 22}) && corners[2] == (std::array<float, 2>{11, 18}));
    }

    const LevelWater& w = level.water;
    CHECK(w.cells == 4 && w.cell_size == 8 && w.origin == (std::array<float, 2>{-16, -16}));
    CHECK(w.layers.size() == 1 && w.map.size() == 16);
    if (w.layers.size() == 1) {
        const WaterLayer& pond = w.layers[0];
        CHECK(pond.name == "pond" && pond.height == 1.5f && pond.velocity[1] == 0.2f && pond.repeat[1] == 3.0f);
        CHECK(pond.color == (std::array<uint8_t, 4>{1, 2, 3, 4}));
        CHECK(w.at(0, 0) == &pond && w.at(3, 0) == &pond && w.at(2, 3) == &pond);
        CHECK(!w.at(1, 0) && !w.at(2, 0) && !w.at(3, 3) && !w.at(1, 1) && !w.at(-1, 0) && !w.at(0, 4));
    }

    CHECK(level.effects.size() == 2);
    const LevelEffect* water_effect = level.effect("water");
    CHECK(water_effect && water_effect->name == "Water" && level.effect("FogCloud") && !level.effect("Spark"));
    if (water_effect) {
        const ConfigRecord* normals = water_effect->get("NormalMapTextures");
        CHECK(normals && normals->text == "test_normal_" && normals->numbers == (std::vector<float>{16, 8}));
        CHECK(water_effect->get("Velocity") && water_effect->get("Velocity")->numbers[1] == 0.02f);
        CHECK(!water_effect->get("Range"));
    }

    // The conquest layer's own spawn path, with z turned to world coordinates.
    const LevelPath* spawn = level.path("cp1_spawn", "test_conquest");
    CHECK(spawn && spawn->group == conquest && spawn->nodes.size() == 1);
    CHECK(spawn && spawn->nodes[0].position[0] == 5 && spawn->nodes[0].position[2] == 30);
    CHECK(level.path("cp1_spawn") != nullptr && level.path("nope") == nullptr);
    // A post names its spawn path in any case (Felucia's "cp1spawn" is its
    // "CP1Spawn").
    CHECK(level.path_in("CP1_Spawn", conquest) == spawn && level.path_in("", conquest) == nullptr);

    // The paths the boundaries list, by group.
    CHECK(level.boundary_paths.size() == 2);
    if (level.boundary_paths.size() == 2) {
        CHECK(level.boundary_paths[0] == (std::pair<uint32_t, uint32_t>{0x0badf00d, odf_hash("warning1")}));
        CHECK(level.boundary_paths[1].second == odf_hash("warning2"));
    }
}

} // namespace

// Aboard a ship (Tantive IV): a terrain chunk with only a name, no grid.
void test_no_terrain() {
    const Bytes file = chunk("ucfb", join({chunk("tern", chunk("NAME", text("tan1"))),
                                           chunk("wrld", chunk("NAME", text("tan1")))}));
    const Level level = read_level(parse_ucfb(file));
    CHECK(level.terrain.name == "tan1" && level.terrain.cells == 0 && level.terrain.heights.empty());
    // No terrain chunk at all is still an error.
    bool threw = false;
    try {
        read_level(parse_ucfb(chunk("ucfb", chunk("wrld", chunk("NAME", text("x"))))));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

int main() {
    test_level();
    test_no_terrain();
    if (failures == 0) {
        std::printf("level_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
