// Reads a small synthetic Battlefront map (no game data): terrain patches,
// world layers in a sublevel, regions and paths.

#include <cmath>
#include <cstdio>
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

Bytes record(const char* name, const std::string& value) {
    Bytes out;
    put(out, odf_hash(name));
    put(out, uint8_t(1));
    put(out, uint32_t(4));
    put(out, uint32_t(value.size() + 1));
    const Bytes t = text(value);
    out.insert(out.end(), t.begin(), t.end());
    return chunk("DATA", out);
}

// A 16x16-cell terrain of 2x2 patches (8 cells each), 2 m cells: height x + z
// at each grid point (in cells). Patch (1,1) misses one grid point.
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
            Bytes vertices;
            uint32_t count = 0;
            for (int z = 0; z <= 8; ++z) {
                for (int x = 0; x <= 8; ++x) {
                    if (px == 1 && pz == 1 && x == 4 && z == 4) {
                        continue;  // a hole
                    }
                    put(vertices, float(x * 2));
                    put(vertices, float(px * 8 + x + pz * 8 + z));
                    put(vertices, float(z * 2));
                    for (int pad = 0; pad < 4; ++pad) {
                        put(vertices, 0.0f);  // normal, color
                    }
                    ++count;
                }
            }
            // An off-grid vertex along a cut: ignored.
            for (const float v : {1.0f, 99.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f}) {
                put(vertices, v);
            }
            ++count;
            Bytes vbuf;
            put(vbuf, count);
            put(vbuf, uint32_t(28));
            put(vbuf, uint32_t(0));
            vbuf.insert(vbuf.end(), vertices.begin(), vertices.end());
            patches = join({patches, chunk("PTCH", join({chunk("INFO", Bytes(8, 0)), chunk("VBUF", vbuf)}))});
        }
    }
    return chunk("tern", join({chunk("NAME", text("test")), chunk("INFO", info),
                               chunk("LTEX", join({text("grass"), text("mud")})), chunk("PCHS", patches)}));
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

void test_level() {
    const uint32_t conquest = 0x12345678;
    const Bytes file = chunk(
        "ucfb", join({terrain(),
                      chunk("wrld", join({chunk("NAME", text("test")),
                                          chunk("inst", chunk("INFO", join({chunk("TYPE", text("test_prop_tree")),
                                                                            chunk("NAME", text("tree1")),
                                                                            xfrm(-3, 2, 4)})))})),
                      sublevel(conquest),
                      // The same path name in another group, then in the conquest one.
                      paths(0x0badf00d, "cp1_spawn", -99), paths(conquest, "cp1_spawn", -30)}));
    const Level level = read_level(parse_ucfb(file));

    const Terrain& t = level.terrain;
    CHECK(t.name == "test" && t.cells == 16 && t.unit == 2 && t.origin() == -16);
    CHECK(t.texture_layers.size() == 2 && t.texture_layers[1] == "mud");
    CHECK(t.heights.size() == 17 * 17 && t.missing == 1);
    CHECK(t.heights[5 * 17 + 3] == 8 && t.heights[16 * 17 + 16] == 32);
    CHECK(t.heights[12 * 17 + 12] == 24);  // the hole, filled from its neighbours

    CHECK(level.layers.size() == 2 && level.layers[1] == "test_conquest");
    CHECK(level.objects.size() == 2 && level.regions.size() == 1);
    const WorldItem& tree = level.objects[0];
    CHECK(tree.layer == "test" && tree.type == "test_prop_tree" && tree.position[0] == -3 && tree.rotation[4] == 1);
    const WorldItem& post = level.objects[1];
    CHECK(post.get("Team") == "2" && post.get("CaptureRegion") == "cp1_capture" && post.get("Missing").empty());
    const WorldItem* capture = level.region("cp1_capture");
    CHECK(capture && capture->type == "cylinder" && capture->size[0] == 8 && capture->position[2] == 21);

    // The conquest layer's own spawn path, with z turned to world coordinates.
    const LevelPath* spawn = level.path("cp1_spawn", "test_conquest");
    CHECK(spawn && spawn->group == conquest && spawn->nodes.size() == 1);
    CHECK(spawn && spawn->nodes[0].position[0] == 5 && spawn->nodes[0].position[2] == 30);
    CHECK(level.path("cp1_spawn") != nullptr && level.path("nope") == nullptr);
}

} // namespace

int main() {
    test_level();
    if (failures == 0) {
        std::printf("level_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
