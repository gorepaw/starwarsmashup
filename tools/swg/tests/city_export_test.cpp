// Object templates and appearances' collision, read from a small synthetic
// TRE archive (no game data): what export-city places and what it collides.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "swg/appearance.h"
#include "swg/appearance_collision.h"
#include "swg/city_export.h"
#include "swg/datatable.h"
#include "swg/gltf.h"
#include "swg/object_template.h"
#include "swg/texture_renderer.h"
#include "swg/tre_archive.h"
#include "swg/tre_library.h"

using namespace swm;
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

using Bytes = std::vector<uint8_t>;

Bytes chunk(const char* tag, const Bytes& payload) {
    Bytes out(tag, tag + 4);
    const auto size = static_cast<uint32_t>(payload.size());
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>(size >> shift));
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

Bytes form(const char* name, const std::vector<Bytes>& children) {
    Bytes payload(name, name + 4);
    for (const Bytes& child : children) {
        payload.insert(payload.end(), child.begin(), child.end());
    }
    return chunk("FORM", payload);
}

Bytes text(const std::string& s) {
    Bytes out(s.begin(), s.end());
    out.push_back(0);
    return out;
}

Bytes floats(const std::vector<float>& values) {
    Bytes out(values.size() * 4);
    std::memcpy(out.data(), values.data(), out.size());
    return out;
}

// A template parameter: its name, whether it's set here, and its value.
Bytes parameter(const std::string& name, const std::string* value) {
    Bytes out = text(name);
    out.push_back(value != nullptr ? 1 : 0);
    const Bytes v = text(value != nullptr ? *value : "");
    out.insert(out.end(), v.begin(), v.end());
    return chunk("XXXX", out);
}

// A shared static template (STAT > SHOT), deriving from `base` if given.
Bytes object_template(const std::string& base, const std::string* appearance, const std::string* portal_layout) {
    std::vector<Bytes> shot;
    if (!base.empty()) {
        shot.push_back(form("DERV", {chunk("XXXX", text(base))}));
    }
    shot.push_back(form("0007", {parameter("objectName", nullptr), parameter("appearanceFilename", appearance),
                                 parameter("portalLayoutFilename", portal_layout)}));
    std::vector<Bytes> stat;
    if (!base.empty()) {
        stat.push_back(form("DERV", {chunk("XXXX", text(base))}));
    }
    stat.push_back(form("0000", {}));
    stat.push_back(form("SHOT", shot));
    return form("STAT", stat);
}

// A shared creature template (SCOT, its own parameters and then a SHOT's),
// setting its collision radius or not.
Bytes creature_template(const std::string& base, const float* radius) {
    Bytes value = text("collisionRadius");
    value.push_back(radius != nullptr ? 1 : 0);
    value.push_back(' ');
    if (radius != nullptr) {
        const Bytes f = floats({*radius});
        value.insert(value.end(), f.begin(), f.end());
    }
    std::vector<Bytes> scot;
    if (!base.empty()) {
        scot.push_back(form("DERV", {chunk("XXXX", text(base))}));
    }
    scot.push_back(form("0013", {chunk("XXXX", value)}));
    scot.push_back(form("STOT", {form("0007", {}), form("SHOT", {form("0009", {parameter("objectName", nullptr)})})}));
    return form("SCOT", scot);
}

Bytes box_extent(float lo, float hi) {
    return form("EXBX", {form("0001", {form("EXSP", {form("0001", {chunk("SPHR", floats({0, 0, 0, hi}))})}),
                                       chunk("BOX ", floats({hi, hi, hi, lo, lo, lo}))})});
}

// A mesh whose APPR bounds it with one box and collides with `collision`.
Bytes mesh_with(const Bytes& collision) {
    return form("MESH", {form("0005", {form("APPR", {form("0003", {box_extent(-5, 5), collision, form("HPTS", {})})})})});
}

void put_u32(Bytes& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

void write_tre(const std::filesystem::path& path, const std::vector<std::pair<std::string, Bytes>>& files) {
    Bytes data, toc, names;
    uint32_t offset = 36;
    for (const auto& [name, contents] : files) {
        put_u32(toc, 0);
        put_u32(toc, static_cast<uint32_t>(contents.size()));
        put_u32(toc, offset);
        put_u32(toc, tre_compressor_none);
        put_u32(toc, 0);
        put_u32(toc, static_cast<uint32_t>(names.size()));
        names.insert(names.end(), name.begin(), name.end());
        names.push_back(0);
        data.insert(data.end(), contents.begin(), contents.end());
        offset += static_cast<uint32_t>(contents.size());
    }
    Bytes out{'E', 'E', 'R', 'T', '5', '0', '0', '0'};
    put_u32(out, static_cast<uint32_t>(files.size()));
    put_u32(out, offset);
    put_u32(out, tre_compressor_none);
    put_u32(out, static_cast<uint32_t>(toc.size()));
    put_u32(out, tre_compressor_none);
    put_u32(out, static_cast<uint32_t>(names.size()));
    put_u32(out, static_cast<uint32_t>(names.size()));
    out.insert(out.end(), data.begin(), data.end());
    out.insert(out.end(), toc.begin(), toc.end());
    out.insert(out.end(), names.begin(), names.end());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(out.data()),
                                                static_cast<std::streamsize>(out.size()));
}

double signed_volume(const std::vector<std::array<Vec3, 3>>& triangles) {
    double volume = 0;
    for (const auto& t : triangles) {
        volume += dot(t[0], cross(t[1], t[2])) / 6.0;
    }
    return volume;
}

void test_templates_and_collision() {
    const std::string crate = "appearance/crate.msh", pob = "appearance/house.pob";
    const float person_radius = 0.5f, beast_radius = 1.7f;
    const auto dir = std::filesystem::temp_directory_path() / "swm_city_export_test";
    std::filesystem::create_directories(dir);
    write_tre(dir / "test.tre",
              {
                  {"object/base/shared_base.iff", object_template("", &crate, nullptr)},
                  // Sets nothing itself: its base's appearance applies.
                  {"object/shared_derived.iff", object_template("object/base/shared_base.iff", nullptr, nullptr)},
                  {"object/shared_house.iff", object_template("object/base/shared_base.iff", nullptr, &pob)},
                  {"appearance/crate.apt", form("APT ", {form("0000", {chunk("NAME", text(crate))})})},
                  {crate, mesh_with(box_extent(-1, 1))},
                  // Two boxes, in a component extent holding a composite.
                  {"appearance/pair.msh",
                   mesh_with(form("CMPT", {form("0000", {form("CPST", {form("0000", {box_extent(0, 1), box_extent(2, 3)})})})}))},
                  // A detail extent: its first box only bounds the second.
                  {"appearance/detail.msh",
                   mesh_with(form("DTAL", {form("0000", {form("CPST", {form("0000", {box_extent(-9, 9), box_extent(0, 2)})})})}))},
                  {"appearance/none.msh", mesh_with(form("NULL", {}))},
                  {"object/creature/shared_base_creature.iff", creature_template("", &person_radius)},
                  {"object/mobile/shared_beast.iff", creature_template("object/creature/shared_base_creature.iff", &beast_radius)},
                  {"object/mobile/shared_person.iff", creature_template("object/creature/shared_base_creature.iff", nullptr)},
              });
    {
        TreLibrary library(dir);
        ObjectTemplates templates(library);
        CHECK(templates.shared("object/shared_derived.iff").appearance == crate);
        CHECK(templates.shared("object/shared_derived.iff").portal_layout.empty());
        CHECK(templates.shared("object/shared_house.iff").portal_layout == pob);
        CHECK(templates.shared("object/shared_house.iff").appearance == crate);
        // A creature's collision radius, its own or its base's.
        CHECK(templates.shared("object/mobile/shared_beast.iff").collision_radius == beast_radius);
        CHECK(templates.shared("object/mobile/shared_person.iff").collision_radius == person_radius);
        CHECK(templates.shared("object/shared_house.iff").collision_radius == 0);

        AppearanceCollisions collisions(library);
        // A 2 m box through the redirect, facing outward: 8 m3.
        const AppearanceCollision box = collisions.load("appearance/crate.apt");
        CHECK(box.solids.size() == 12 && std::abs(signed_volume(box.solids) - 8) < 1e-3);
        // Mirrored into glTF space: x from -1 to 1 still, the top facing up.
        bool top_up = false;
        for (const auto& t : box.solids) {
            top_up |= t[0].y == 1 && t[1].y == 1 && t[2].y == 1 && cross(t[1] - t[0], t[2] - t[0]).y > 0;
        }
        CHECK(top_up);
        CHECK(std::abs(signed_volume(collisions.load("appearance/pair.msh").solids) - 2) < 1e-3);
        const AppearanceCollision detail = collisions.load("appearance/detail.msh");
        CHECK(detail.solids.size() == 12 && std::abs(signed_volume(detail.solids) - 8) < 1e-3);
        CHECK(collisions.load("appearance/none.msh").solids.empty());
        CHECK(collisions.skipped().empty());
    }
    std::filesystem::remove_all(dir);
}

void test_floor_collision() {
    // One triangle, wound clockwise from above in SWG space; its first edge
    // a doorway, the others walls.
    FloorMesh floor;
    floor.vertices = {{0, 0, 0}, {4, 0, 0}, {0, 0, 4}};
    FloorMesh::Triangle t;
    t.corners = {0, 1, 2};
    t.neighbors = {-1, -1, -1};
    t.crossable = {true, false, false};
    t.portal = {-1, -1, -1};
    floor.triangles = {t};
    AppearanceCollision out;
    add_floor_collision(floor, Mat4{}, out, 3);
    CHECK(out.floors.size() == 1);
    const auto& f = out.floors[0];
    CHECK(cross(f[1] - f[0], f[2] - f[0]).y > 0);  // facing up
    // Two walls, each facing both ways: 2 quads of 2 triangles each.
    CHECK(out.walls.size() == 8);
    for (const auto& w : out.walls) {
        CHECK(std::max({w[0].y, w[1].y, w[2].y}) == 3);
    }
}

// A switching texture shader (a screen's frames): its first frame's main texture.
void test_switching_shader() {
    Bytes first(4);
    std::memcpy(first.data(), "NIAM", 4);
    const Bytes path = text("texture/screen_1.dds");
    first.insert(first.end(), path.begin(), path.end());
    const Bytes shader = form("SWTS", {form("0000", {chunk("NAME", text("shader/screen.sht")), chunk("TEXT", first)})});
    CHECK(shader_main_texture(shader) == "texture/screen_1.dds");
}

// A customizable shader: a skin color shared by the body, a garment's own
// second color, the shader's default for a factor no palette sets; an owner
// proxy shader; a palette.
void test_shader_colors() {
    const auto pal = [](const std::string& variable, uint8_t flag, const char* tag, const std::string& palette,
                        int32_t index) {
        Bytes out = text(variable);
        out.push_back(flag);
        out.insert(out.end(), tag, tag + 4);
        const Bytes path = text(palette);
        out.insert(out.end(), path.begin(), path.end());
        const auto* b = reinterpret_cast<const uint8_t*>(&index);
        out.insert(out.end(), b, b + 4);
        return chunk("PAL ", out);
    };
    // TFNS: BLCK opaque black, HUEB white (tags and colors little-endian).
    const Bytes names{'K', 'C', 'L', 'B', 0, 0, 0, 0xff, 'B', 'E', 'U', 'H', 0xff, 0xff, 0xff, 0xff};
    const Bytes shader = form(
        "CSHD",
        {form("0001", {form("SSHT", {form("0000", {form("TFNS", {chunk("0000", names)})})}),
                       form("TFAC", {pal("index_color_skin", 0, "NIAM", "palette\\skin.pal", 3),
                                     pal("index_color_1", 1, "BEUH", "palette/cloth.pal", 124)})})});
    const ShaderCustomization colors = parse_shader_customization(shader);
    CHECK(colors.colors.size() == 2);
    CHECK(colors.colors[0].variable == "index_color_skin" && colors.colors[0].shared &&
          colors.colors[0].factor == "MAIN" && colors.colors[0].palette == "palette/skin.pal" &&
          colors.colors[0].default_index == 3);
    CHECK(colors.colors[1].variable == "index_color_1" && !colors.colors[1].shared &&
          colors.colors[1].factor == "HUEB" && colors.colors[1].default_index == 124);
    CHECK(colors.factor_defaults.size() == 2);
    CHECK((colors.factor_defaults[0] == std::pair<std::string, uint32_t>("BLCK", 0xff000000)));
    CHECK((colors.factor_defaults[1] == std::pair<std::string, uint32_t>("HUEB", 0xffffffff)));
    CHECK(colors.two_colors());
    CHECK(colors.owner_proxy.empty());

    const ShaderCustomization proxy = parse_shader_customization(form("OPST", {form("0000", {chunk("INFO", text("body"))})}));
    CHECK(proxy.owner_proxy == "body" && proxy.colors.empty() && !proxy.two_colors());

    Bytes palette{'R', 'I', 'F', 'F', 16, 0, 0, 0, 'P', 'A', 'L', ' ', 'd', 'a', 't', 'a', 8, 0, 0, 0, 0, 3, 2, 0,
                  10, 20, 30, 0, 200, 100, 50, 0};
    const auto entries = parse_palette(palette);
    CHECK(entries.size() == 2);
    CHECK(entries.size() == 2 && entries[0] == (std::array<uint8_t, 3>{10, 20, 30}) &&
          entries[1] == (std::array<uint8_t, 3>{200, 100, 50}));
    palette.resize(28);
    bool threw = false;
    try {
        parse_palette(palette);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

// How an effect blends, by its first pass's DATA (and a clipping pixel
// program), and a shader's effect.
void test_effect_blend() {
    const auto effect = [](uint8_t depth_write, uint8_t blend, uint8_t source, uint8_t destination,
                           const std::string& program) {
        Bytes data(56, 0);
        data[5] = depth_write, data[7] = blend, data[9] = source, data[10] = destination, data[17] = 0x0f;
        Bytes program_data{2};
        const Bytes path = text(program);
        program_data.insert(program_data.end(), path.begin(), path.end());
        return form("EFCT", {form("0001", {chunk("DATA", {6, 0}),
                                           form("IMPL", {form("0005", {form("PASS", {form("0009", {
                                               chunk("DATA", data),
                                               form("PPSH", {form("0001", {chunk("DATA", program_data)})})})})})})})});
    };
    CHECK(effect_blend(effect(1, 0, 4, 5, "pixel_program/a_simple.psh")) == EffectBlend::opaque);
    CHECK(effect_blend(effect(0, 1, 4, 5, "pixel_program/a_alpha.psh")) == EffectBlend::alpha);
    CHECK(effect_blend(effect(0, 1, 1, 1, "pixel_program/a_emisadd.psh")) == EffectBlend::additive);
    CHECK(effect_blend(effect(1, 0, 4, 5, "pixel_program/a_punchout_ps20.psh")) == EffectBlend::cutout);

    const Bytes shader = form("CSHD", {form("0001", {form("SSHT", {form("0000", {
                                                         chunk("NAME", text("effect\\h_alpha.eft"))})})})});
    CHECK(shader_effect(shader) == "effect/h_alpha.eft");

    // An additive material, which glTF has no mode for: blended, marked in
    // its extras, unlit.
    Gltf gltf;
    gltf.materials.push_back({.name = "glow", .alpha_mode = "BLEND", .unlit = true, .additive = true});
    gltf.materials.push_back({.name = "plain"});
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "swm_additive_test.gltf";
    gltf.write(path);
    std::stringstream json;
    json << std::ifstream(path).rdbuf();
    const std::string text_json = json.str();
    CHECK(text_json.find(R"("alphaMode":"BLEND","extensions":{"KHR_materials_unlit":{}},"extras":{"blend":"add"}})") !=
          std::string::npos);
    CHECK(text_json.find(R"("extras")") == text_json.rfind(R"("extras")"));  // the plain one has none
    std::filesystem::remove(path);
    std::filesystem::path bin = path;
    std::filesystem::remove(bin.replace_extension(".bin"));
}

// A texture renderer: two layers (a skin copied whole, brows blended into a
// corner, drawn first-shader-last to check the drawing order), their
// textures, styles and colors.
void test_texture_renderer() {
    const auto u32 = [](Bytes& out, uint32_t v) {
        const auto* b = reinterpret_cast<const uint8_t*>(&v);
        out.insert(out.end(), b, b + 4);
    };
    const auto f32s = [](Bytes& out, std::initializer_list<float> values) {
        for (const float v : values) {
            const auto* b = reinterpret_cast<const uint8_t*>(&v);
            out.insert(out.end(), b, b + 4);
        }
    };
    const auto append = [](Bytes& out, const Bytes& more) { out.insert(out.end(), more.begin(), more.end()); };
    const auto quad = [&](float x0, float y0, float x1, float y1) {
        Bytes info, data;
        u32(info, 0x1101);
        u32(info, 4);
        f32s(data, {x0, y0, 3, 0, 0, x1, y0, 3, 1, 0, x1, y1, 3, 1, 1, x0, y1, 3, 0, 1});
        return form("VTXA", {form("0003", {chunk("INFO", info), chunk("DATA", data)})});
    };
    const auto command = [&](const char* tag, const Bytes& payload) {
        Bytes one;
        u32(one, 1);
        return form("PCMD", {form("TOPS", {chunk("INFO", one), chunk(tag, payload)})});
    };
    const auto render = [&](uint32_t shader, uint32_t buffer) {
        Bytes info, rtfc;
        u32(info, shader);
        u32(info, 1);
        u32(rtfc, buffer);
        return form("SRSC", {chunk("INFO", info), chunk("RTFC", rtfc)});
    };

    Bytes dest, textures, sstc, sst1, stfp;
    u32(dest, 128);
    u32(dest, 256);
    u32(textures, 3);
    append(textures, text("texture\\face.dds"));
    append(textures, text("texture\\brow_s01.dds"));
    append(textures, text("texture\\brow_s02.dds"));
    u32(sstc, 1);  // shader 1 (the skin): texture 0
    append(sstc, {'N', 'I', 'A', 'M'});
    u32(sstc, 0);
    u32(sst1, 0);  // shader 0 (the brows): one of textures 1 and 2
    append(sst1, {'N', 'I', 'A', 'M'});
    u32(sst1, 1);
    append(sst1, text("index_style_eyebrow"));
    u32(sst1, 2);
    stfp = {1, 0, 'N', 'I', 'A', 'M'};  // shader 1's color: skin, shared
    append(stfp, text("palette/skin.pal"));
    append(stfp, text("index_color_skin"));
    stfp.push_back(0);

    const Bytes trt = form(
        "BTRT", {form("0001", {chunk("DEST", dest),
                               form("SHTM", {chunk("NAME", text("shader\\texren_alphablend.sht")),
                                             chunk("NAME", text("shader\\texren_copy.sht"))}),
                               form("TXTS", {chunk("INFO", textures)}),
                               form("VBS ", {quad(0, 0, 1, 1), quad(0.5f, 0.25f, 0.75f, 0.5f)}),
                               form("RCMS", {render(1, 0), render(0, 1)}),
                               form("PCMS", {command("SSTC", sstc), command("SST1", sst1), command("STFP", stfp)})})});
    const TextureRenderer renderer = parse_texture_renderer(trt);
    CHECK(renderer.width == 128 && renderer.height == 256);
    CHECK(renderer.layers.size() == 2);
    if (renderer.layers.size() == 2) {
        const TextureRendererLayer& skin = renderer.layers[0];
        CHECK(skin.shader == "shader/texren_copy.sht");
        CHECK(skin.textures == std::vector<std::string>{"texture/face.dds"} && skin.style_variable.empty());
        CHECK(skin.palette == "palette/skin.pal" && skin.color_variable == "index_color_skin" && skin.color_shared);
        CHECK((skin.rect == std::array<float, 4>{0, 0, 1, 1}));
        const TextureRendererLayer& brows = renderer.layers[1];
        CHECK(brows.shader == "shader/texren_alphablend.sht");
        CHECK((brows.textures == std::vector<std::string>{"texture/brow_s01.dds", "texture/brow_s02.dds"}));
        CHECK(brows.style_variable == "index_style_eyebrow" && brows.palette.empty());
        CHECK((brows.rect == std::array<float, 4>{0.5f, 0.25f, 0.75f, 0.5f}));
        CHECK((brows.uv == std::array<float, 4>{0, 0, 1, 1}));
    }
}

// A datatable: typed columns, rows of strings, numbers and booleans.
void test_datatable() {
    Bytes columns;
    const auto put_i32 = [](Bytes& out, int32_t v) {
        const auto* b = reinterpret_cast<const uint8_t*>(&v);
        out.insert(out.end(), b, b + 4);
    };
    put_i32(columns, 3);
    for (const char* name : {"doorStyleName", "doorMoveX", "isForceField"}) {
        const Bytes t = text(name);
        columns.insert(columns.end(), t.begin(), t.end());
    }
    Bytes types;
    for (const char* type : {"s", "f", "b"}) {
        const Bytes t = text(type);
        types.insert(types.end(), t.begin(), t.end());
    }
    Bytes rows;
    put_i32(rows, 2);
    for (const auto& [name, move, force] : {std::tuple{"cantina_door", -2.1f, 0}, std::tuple{"shield", 0.0f, 1}}) {
        const Bytes t = text(name);
        rows.insert(rows.end(), t.begin(), t.end());
        const Bytes f = floats({move});
        rows.insert(rows.end(), f.begin(), f.end());
        put_i32(rows, force);
    }
    const DataTable table = parse_datatable(
        form("DTII", {form("0001", {chunk("COLS", columns), chunk("TYPE", types), chunk("ROWS", rows)})}));
    CHECK(table.columns.size() == 3 && table.rows.size() == 2);
    CHECK(table.text(0, table.column("doorStyleName")) == "cantina_door");
    CHECK(std::abs(table.number(0, table.column("doorMoveX")) + 2.1f) < 1e-6f);
    CHECK(table.number(1, table.column("isForceField")) == 1 && table.column("nothing") == -1);
}

} // namespace

// The terrain in a room: a walked floor with a diagonal edge (x + z = 2.2),
// and a drawn one beside it (x 3..4). Taken away where a cell's center is
// on the walked floor; hidden (still stood on) where the terrain stands
// above a floor (it would show through) over the cell's center (the drawn
// one's) or part of it; not where it's below (the floor covers it).
void test_room_terrain_holes() {
    const std::vector<std::array<Vec3, 3>> walked{{Vec3{0.1f, 0, 0.1f}, Vec3{2.1f, 0, 0.1f}, Vec3{0.1f, 0, 2.1f}}};
    const std::vector<std::array<Vec3, 3>> drawn{{Vec3{3, 0, 0}, Vec3{4, 0, 0}, Vec3{3, 0, 1}}};
    const core::TerrainHoles above = room_terrain_holes(walked, drawn, [](double, double) { return 1.0; });
    const core::TerrainHoles below = room_terrain_holes(walked, drawn, [](double, double) { return -1.0; });
    const auto cell = [](const core::TerrainHoles& holes, double x, double z) {
        return holes.cells[size_t(std::floor((z - holes.z0) / holes.cell)) * size_t(holes.width) +
                           size_t(std::floor((x - holes.x0) / holes.cell))];
    };
    CHECK(above.cell == 0.25);
    CHECK(above.at(1.0, 0.5) && below.at(1.0, 0.5));  // its center on the walked floor: taken away
    CHECK(cell(above, 1.875, 0.375) == 2 && !above.at(1.875, 0.375));  // a corner under its edge: hidden
    CHECK(cell(below, 1.875, 0.375) == 0);                             // unless the terrain's under it
    CHECK(cell(above, 2.125, 0.375) == 0 && cell(below, 2.125, 0.375) == 0);  // beside the edge
    CHECK(cell(above, -0.125, 1.0) == 0);                                     // beside a straight edge
    CHECK(cell(above, 3.375, 0.375) == 2 && cell(below, 3.375, 0.375) == 0);  // on the drawn floor
    CHECK(!above.at(3.375, 0.375));
    CHECK(room_terrain_holes({}, {}, [](double, double) { return 0.0; }).empty());
}

int main() {
    test_room_terrain_holes();
    test_switching_shader();
    test_shader_colors();
    test_effect_blend();
    test_texture_renderer();
    test_datatable();
    test_templates_and_collision();
    test_floor_collision();
    if (failures == 0) {
        std::printf("city_export_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
