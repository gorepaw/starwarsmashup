// Object templates and appearances' collision, read from a small synthetic
// TRE archive (no game data): what export-city places and what it collides.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "swg/appearance.h"
#include "swg/appearance_collision.h"
#include "swg/datatable.h"
#include "swg/object_template.h"
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
              });
    {
        TreLibrary library(dir);
        ObjectTemplates templates(library);
        CHECK(templates.shared("object/shared_derived.iff").appearance == crate);
        CHECK(templates.shared("object/shared_derived.iff").portal_layout.empty());
        CHECK(templates.shared("object/shared_house.iff").portal_layout == pob);
        CHECK(templates.shared("object/shared_house.iff").appearance == crate);

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

int main() {
    test_switching_shader();
    test_shader_colors();
    test_datatable();
    test_templates_and_collision();
    test_floor_collision();
    if (failures == 0) {
        std::printf("city_export_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
