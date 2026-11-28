#include "swg/gltf.h"
#include "swg/skinned_mesh.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace swbfo::swg;

void push_u32(std::vector<uint8_t>& out, uint32_t v) {
    const size_t at = out.size();
    out.resize(at + 4);
    std::memcpy(out.data() + at, &v, 4);
}

void push_f32(std::vector<uint8_t>& out, float v) {
    const size_t at = out.size();
    out.resize(at + 4);
    std::memcpy(out.data() + at, &v, 4);
}

void push_cstr(std::vector<uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
}

std::vector<uint8_t> make_chunk(const char* tag, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    out.insert(out.end(), tag, tag + 4);
    const uint32_t sz = uint32_t(payload.size());
    out.push_back(uint8_t((sz >> 24) & 0xff));
    out.push_back(uint8_t((sz >> 16) & 0xff));
    out.push_back(uint8_t((sz >> 8) & 0xff));
    out.push_back(uint8_t(sz & 0xff));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<uint8_t> make_form(const char* form_name, const std::vector<uint8_t>& children) {
    std::vector<uint8_t> body;
    body.insert(body.end(), form_name, form_name + 4);
    body.insert(body.end(), children.begin(), children.end());
    return make_chunk("FORM", body);
}

void append(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

void test_blts_parse() {
    std::vector<uint8_t> v0004;

    // INFO: 9 i32s + 3 u16s + 1 i16 = 44 bytes
    std::vector<uint8_t> info(44, 0);
    append(v0004, make_chunk("INFO", info));

    std::vector<uint8_t> sktm;
    push_cstr(sktm, "appearance/skeleton/all_b.skt");
    append(v0004, make_chunk("SKTM", sktm));

    std::vector<uint8_t> xfnm;
    push_cstr(xfnm, "root");
    append(v0004, make_chunk("XFNM", xfnm));

    std::vector<uint8_t> posn;
    for (int i = 0; i < 3; ++i) {
        push_f32(posn, float(i));
        push_f32(posn, 0.0f);
        push_f32(posn, 0.0f);
    }
    append(v0004, make_chunk("POSN", posn));

    std::vector<uint8_t> twhd;
    std::vector<uint8_t> twdt;
    for (int i = 0; i < 3; ++i) {
        push_u32(twhd, 1);
        push_u32(twdt, 0);
        push_f32(twdt, 1.0f);
    }
    append(v0004, make_chunk("TWHD", twhd));
    append(v0004, make_chunk("TWDT", twdt));

    // BLTS -> FORM BLT  (blend_muscle touching position 1)
    std::vector<uint8_t> blt_info;
    push_u32(blt_info, 1);
    push_u32(blt_info, 0);
    push_cstr(blt_info, "blend_muscle");
    std::vector<uint8_t> blt_posn;
    push_u32(blt_posn, 1);
    push_f32(blt_posn, 0.1f);
    push_f32(blt_posn, 0.2f);
    push_f32(blt_posn, -0.3f);
    std::vector<uint8_t> blt_body;
    append(blt_body, make_chunk("INFO", blt_info));
    append(blt_body, make_chunk("POSN", blt_posn));
    append(v0004, make_form("BLTS", make_form("BLT ", blt_body)));

    // Minimal PSDT
    std::vector<uint8_t> psdt;
    std::vector<uint8_t> name;
    push_cstr(name, "shader/hum_m_body.sht");
    append(psdt, make_chunk("NAME", name));
    std::vector<uint8_t> pidx;
    push_u32(pidx, 3);
    push_u32(pidx, 0);
    push_u32(pidx, 1);
    push_u32(pidx, 2);
    append(psdt, make_chunk("PIDX", pidx));
    std::vector<uint8_t> itl;
    push_u32(itl, 1);
    push_u32(itl, 0);
    push_u32(itl, 1);
    push_u32(itl, 2);
    append(psdt, make_form("PRIM", make_chunk("ITL ", itl)));
    append(v0004, make_form("PSDT", psdt));

    const std::vector<uint8_t> skmg = make_form("SKMG", make_form("0004", v0004));
    const SkinnedMesh parsed = parse_skinned_mesh(skmg);
    assert(parsed.blend_targets.size() == 1);
    assert(parsed.blend_targets[0].name == "blend_muscle");
    assert(parsed.blend_targets[0].position_deltas.size() == 1);
    assert(parsed.blend_targets[0].position_deltas[0].first == 1);
}

void test_gltf_morph_write() {
    Gltf gltf;
    Gltf::Primitive prim;
    prim.attributes["POSITION"] = gltf.add_vec3({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, true);
    prim.targets.push_back({{"POSITION", gltf.add_vec3({{0, 0, 0}, {0.1f, 0.2f, 0}, {0, 0, 0}}, true, true)}});
    prim.indices = gltf.add_indices({0, 1, 2});
    Gltf::Mesh mesh;
    mesh.name = "test";
    mesh.target_names = {"blend_muscle"};
    mesh.primitives.push_back(prim);
    gltf.meshes.push_back(mesh);
    gltf.nodes.push_back({.name = "root", .mesh = 0});
    gltf.scene_nodes = {0};

    const auto path = std::filesystem::path("swbfo_blend_shape_test.gltf");
    gltf.write(path);
    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), {});
    in.close();
    std::filesystem::remove(path);
    std::filesystem::remove("swbfo_blend_shape_test.bin");
    assert(content.find("\"targets\":[{\"POSITION\":") != std::string::npos);
    assert(content.find("\"targetNames\":[\"blend_muscle\"]") != std::string::npos);
}

} // namespace

int main() {
    test_blts_parse();
    test_gltf_morph_write();
    return 0;
}
