// Reads a small synthetic munged model, skeleton and textures (no game data),
// and writes the model as glTF.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "bf2/model.h"
#include "bf2/model_export.h"
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

Bytes words(std::initializer_list<uint32_t> values) {
    Bytes out;
    for (const uint32_t v : values) {
        put(out, v);
    }
    return out;
}

// A quad as a 4-index strip: (0,1,2) then (2,1,3). Vertices: position,
// normal (+Y), static lighting (D3D color), texcoords; preceded by a
// compressed copy that must be ignored.
Bytes model() {
    Bytes vertices = words({4, 36, 0x322});  // count, stride, position | normal | static lighting | texcoords
    const float corners[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    for (const auto& c : corners) {
        for (const float v : {c[0] + 2, 0.5f, c[1], 0.0f, 1.0f, 0.0f}) {
            put(vertices, v);
        }
        for (const uint8_t b : {uint8_t(10), uint8_t(20), uint8_t(30), uint8_t(0)}) {  // B, G, R, A
            vertices.push_back(b);
        }
        put(vertices, c[0]);
        put(vertices, c[1]);
    }
    Bytes indices = words({4});
    for (const uint16_t i : {uint16_t(0), uint16_t(1), uint16_t(2), uint16_t(3)}) {
        put(indices, i);
    }
    Bytes material = words({0x10002});                              // hardedged | doublesided
    for (const uint8_t b : {uint8_t(255), uint8_t(128), uint8_t(64), uint8_t(255)}) {  // diffuse B, G, R, A
        material.push_back(b);
    }
    material.insert(material.end(), 16, 0);
    material.push_back(0);
    Bytes tnam = words({0});
    const Bytes leaf = text("Leaf");
    tnam.insert(tnam.end(), leaf.begin(), leaf.end());
    const Bytes segment = join({chunk("INFO", words({5, 4, 2})), chunk("MTRL", material), chunk("RTYP", text("Normal")),
                                chunk("TNAM", tnam), chunk("IBUF", indices),
                                chunk("VBUF", join({words({4, 16, 0x1000 | 0x22}), Bytes(64, 0)})),
                                chunk("VBUF", vertices), chunk("BNAM", text("child"))});
    return chunk("modl", join({chunk("NAME", text("test_prop")), chunk("segm", segment)}));
}

// root at the origin; child at (2, 2, 3) turned 90 degrees about y (its x
// axis is the root's z: the stored columns); tip at (1, 0, 0) from child.
Bytes skeleton() {
    Bytes transforms;
    for (const float v : {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,  //
                          0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 2.0f, 2.0f, 3.0f,  //
                          1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f}) {
        put(transforms, v);
    }
    return chunk("skel", join({chunk("INFO", join({text("test_prop"), words({3})})),
                               chunk("NAME", join({text("root"), text("child"), text("tip")})),
                               chunk("PRNT", join({text(""), text("root"), text("child")})),
                               chunk("XFRM", transforms)}));
}

// A 2x2 texture stored as a bump map (unsupported), DXT1 and A8R8G8B8: the
// last is preferred.
Bytes texture() {
    const auto format = [](uint32_t d3d, const Bytes& body) {
        Bytes info = words({d3d});
        put(info, uint16_t(2));
        put(info, uint16_t(2));
        put(info, uint16_t(1));
        put(info, uint16_t(1));
        put(info, uint32_t(1));
        return chunk("FMT_", join({chunk("INFO", info),
                                   chunk("FACE", chunk("LVL_", join({chunk("INFO", words({0, uint32_t(body.size())})),
                                                                     chunk("BODY", body)})))}));
    };
    Bytes argb;
    for (int i = 0; i < 4; ++i) {
        for (const uint8_t b : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(i * 80)}) {  // B, G, R, A
            argb.push_back(b);
        }
    }
    const uint32_t dxt1 = 0x31545844;
    return chunk("tex_", join({chunk("NAME", text("leaf")), chunk("INFO", words({3, 60, dxt1, 21})),
                               format(60, Bytes(8, 0)), format(dxt1, Bytes(8, 0)), format(21, argb)}));
}

// A collision mesh of a quad and a triangle (leaves of 4 and 3 indices) on
// bone "root", and three primitives: a box on "child", a soldier-and-ordnance
// cylinder, and a vehicles-only box (left out of model_collision).
Bytes collision() {
    Bytes positions;
    for (const float v : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f}) {
        put(positions, v);
    }
    const auto leaf = [](std::initializer_list<uint16_t> indices) {
        Bytes out{uint8_t(indices.size()), 0, 0, 0, 255, 255, 255};
        for (const uint16_t i : indices) {
            put(out, i);
        }
        return chunk("LEAF", out);
    };
    const Bytes coll = chunk(
        "coll", join({chunk("NAME", text("test_prop")), chunk("NODE", text("root")), chunk("INFO", words({5, 1, 2, 7})),
                      chunk("POSI", positions),
                      chunk("TREE", join({chunk("NODE", Bytes(6, 0)), leaf({0, 1, 2, 3}), leaf({3, 2, 4})}))}));
    const auto primitive = [](const char* name, const char* parent, uint32_t shape, float x, float y, float z) {
        Bytes transform;
        for (const float v : {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f}) {
            put(transform, v);
        }
        Bytes data = words({shape});
        put(data, x);
        put(data, y);
        put(data, z);
        return join({chunk("NAME", text(name)), chunk("PRNT", text(parent)), chunk("XFRM", transform),
                     chunk("DATA", data)});
    };
    const Bytes prim = chunk("prim", join({chunk("INFO", join({text("test_prop"), words({3})})),
                                           primitive("p_crate", "child", 4, 0.5f, 0.5f, 0.5f),
                                           primitive("p_-so-trunk", "root", 2, 0.3f, 4, 0),
                                           primitive("p_-v-wall", "root", 4, 1, 1, 1)}));
    // A prop with primitives only: a crate stopping everything, and a ramp
    // for vehicles (flags without the leading dash).
    const Bytes crate = chunk("prim", join({chunk("INFO", join({text("test_crate"), words({2})})),
                                            primitive("p_crate", "root", 4, 0.5f, 0.5f, 0.5f),
                                            primitive("p_v-ramp", "root", 4, 2, 1, 2)}));
    return join({coll, prim, crate});
}

void test_collision() {
    const Bytes file = chunk("ucfb", join({skeleton(), model(), collision()}));
    const UcfbNode root = parse_ucfb(file);

    const CollisionMesh mesh = read_collision_mesh(*root.child("coll"));
    CHECK(mesh.model == "test_prop" && mesh.bone == "root" && mesh.vertices.size() == 5);
    CHECK(mesh.triangles.size() == 3);  // the quad fanned into two, and the triangle
    CHECK(mesh.triangles[1] == (std::array<uint32_t, 3>{0, 2, 3}) && mesh.triangles[2] == (std::array<uint32_t, 3>{3, 2, 4}));

    const CollisionPrimitives prims = read_collision_primitives(*root.child("prim"));
    CHECK(prims.model == "test_prop" && prims.primitives.size() == 3);
    const CollisionPrimitive& crate = prims.primitives[0];
    CHECK(crate.parent == "child" && crate.shape == CollisionPrimitive::box && crate.size[0] == 0.5f);
    CHECK(crate.stops('s') && crate.stops('o') && crate.stops('v'));  // no flags: everything
    CHECK(prims.primitives[1].stops('s') && prims.primitives[1].stops('o') && !prims.primitives[1].stops('v'));
    CHECK(!prims.primitives[2].stops('s') && !prims.primitives[2].stops('o'));
    CHECK(!crate.flags() && prims.primitives[1].flags() == "so");
    CollisionPrimitive named;
    for (const char* none : {"p_set_piece", "p_vehicle", "p_ollision", "p_-", "p_cube1"}) {
        named.name = none;
        CHECK(!named.flags());
    }
    named.name = "p_so-collision";  // Geonosis's hangar
    CHECK(named.flags() == "so" && named.stops('s') && !named.stops('v'));

    AssetLibrary library;
    library.add(file);
    CHECK(library.collision_mesh("TEST_PROP") && library.collision_primitives("test_prop"));
    const std::vector<CollisionTriangle> triangles = model_collision(library, "test_prop");
    CHECK(triangles.size() == 3 + 12 + 48);  // mesh, crate, 12-sided cylinder (sides and caps)
    // A class listing what its soldiers and ordnance meet (Naboo's
    // buildings: the mesh; their boxes are for vehicles).
    OdfClass building;
    building.properties = {{odf_hash("VehicleCollision"), "p_crate"},
                           {odf_hash("SoldierCollision"), "CollisionMesh"},
                           {odf_hash("OrdnanceCollision"), "CollisionMesh"},
                           {odf_hash("OrdnanceCollision"), "p_-so-trunk"}};
    const CollisionUse use = collision_use(building);
    CHECK(use.soldiers == std::vector<std::string>{"collisionmesh"});
    CHECK(use.ordnance == (std::vector<std::string>{"collisionmesh", "p_-so-trunk"}));
    const std::vector<CollisionTriangle> listed = model_collision(library, "test_prop", use);
    CHECK(listed.size() == 3 + 48);  // the mesh and the trunk; not the crate
    CHECK(listed[0].soldiers && listed[0].ordnance && !listed[3].soldiers && listed[3].ordnance);
    // Without a mesh, the crate stops everything: 1 m up from the root.
    const std::vector<CollisionTriangle> alone = model_collision(library, "test_crate");
    CHECK(alone.size() == 12);
    float low = 1e9f, high = -1e9f;
    for (const auto& t : alone) {
        for (const auto& p : {t.a, t.b, t.c}) {
            low = std::min(low, p[1]);
            high = std::max(high, p[1]);
        }
        CHECK(t.soldiers && t.ordnance);
    }
    CHECK(low == 0.5f && high == 1.5f);
    // The trunk is 4 m tall, centered 1 m up: -1..3.
    low = 1e9f;
    for (size_t i = 15; i < triangles.size(); ++i) {
        low = std::min({low, triangles[i].a[1], triangles[i].b[1], triangles[i].c[1]});
        CHECK(triangles[i].soldiers && triangles[i].ordnance);
    }
    CHECK(low == -1.0f);
}

void test_model() {
    const Bytes file = chunk("ucfb", join({texture(), skeleton(), model()}));
    const UcfbNode root = parse_ucfb(file);

    const Model m = read_model(*root.child("modl"));
    CHECK(m.name == "test_prop" && m.segments.size() == 1);
    const ModelSegment& s = m.segments[0];
    CHECK(s.bone == "child" && s.render_type == "Normal" && s.textures[0] == "Leaf" && s.textures[1].empty());
    CHECK(s.flags == (material_hardedged | material_doublesided));
    CHECK(s.diffuse == (std::array<uint8_t, 4>{64, 128, 255, 255}));
    CHECK(s.positions.size() == 4 && s.positions[3] == (std::array<float, 3>{3, 0.5f, 1}));
    CHECK(s.uvs[1] == (std::array<float, 2>{1, 0}) && s.normals[2][1] == 1);
    CHECK(s.colors.size() == 4 && s.colors[0] == (std::array<uint8_t, 4>{30, 20, 10, 255}));  // RGB, opaque
    CHECK(s.baked_lighting);
    CHECK(s.triangles == (std::vector<uint32_t>{0, 1, 2, 2, 1, 3}));

    const Skeleton skel = read_skeleton(*root.child("skel"));
    CHECK(skel.model == "test_prop" && skel.bones.size() == 3 && skel.bones[2].parent == "child");
    const Transform tip = skel.model_transform("tip");
    CHECK(tip.position == (std::array<float, 3>{2, 2, 4}) && tip.rotation[2] == 1 && tip.rotation[6] == -1);
    CHECK(tip.apply({0, 0, 1}) == (std::array<float, 3>{1, 2, 4}));
    const Transform none = skel.model_transform("nope");
    CHECK(none.position == (std::array<float, 3>{0, 0, 0}) && none.rotation[0] == 1);

    const auto image = read_texture(*root.child("tex_"));
    CHECK(image && image->width == 2 && image->height == 2);
    CHECK(image && image->pixels[0] == 3 && image->pixels[2] == 1 && image->pixels[3 * 4 + 3] == 240);

    // Export: names are case-insensitive; the texture goes next to the
    // model folder.
    AssetLibrary library;
    library.add(file);
    CHECK(library.model("TEST_PROP") != nullptr && library.texture("LEAF") != nullptr);
    CHECK(library.skeleton("test_prop") != nullptr && library.model("missing") == nullptr);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "swm_bf2_model_test";
    std::filesystem::remove_all(dir);
    std::set<std::string> written;
    const ModelExport result = export_model(library, "Test_Prop", dir / "models", dir / "textures", written);
    CHECK(result.segments == 1 && result.skipped == 0 && result.missing_textures.empty());
    CHECK(written.contains("leaf") && std::filesystem::exists(dir / "textures" / "leaf.png"));
    std::ifstream in(dir / "models" / "test_prop.gltf");
    std::stringstream gltf;
    gltf << in.rdbuf();
    in.close();
    const std::string json = gltf.str();
    CHECK(json.find(R"("uri":"../textures/leaf.png")") != std::string::npos);
    CHECK(json.find(R"("alphaMode":"MASK")") != std::string::npos);
    CHECK(json.find(R"("doubleSided":true)") != std::string::npos);
    CHECK(json.find("COLOR_0") != std::string::npos);
    CHECK(json.find("KHR_materials_unlit") != std::string::npos);  // baked lighting
    // The quad sits at Battlefront x 2..3, z 0..1, through "child" (turned
    // 90 degrees about y, at (2, 2, 3)): model x = 2 - z = 1..2 (mirrored to
    // -2..-1), z = 3 + x = 5..6.
    CHECK(json.find(R"("min":[-2,2.5,5],"max":[-1,2.5,6])") != std::string::npos);
    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    try {
        test_model();
        test_collision();
    } catch (const std::exception& error) {
        std::printf("FAIL: %s\n", error.what());
        ++failures;
    }
    if (failures == 0) {
        std::printf("model_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
