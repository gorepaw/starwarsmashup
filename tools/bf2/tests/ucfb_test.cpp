// Parses small synthetic ucfb buffers (no game data).

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

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

void put_u32(Bytes& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

Bytes chunk(const char* tag, const Bytes& payload, bool pad = true) {
    Bytes out(tag, tag + 4);
    put_u32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    while (pad && out.size() % 4 != 0) {
        out.push_back(0);
    }
    return out;
}

Bytes concat(const std::vector<Bytes>& parts) {
    Bytes out;
    for (const Bytes& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

Bytes text(const std::string& s) {
    Bytes out(s.begin(), s.end());
    out.push_back(0);
    return out;
}

void test_tree() {
    const Bytes model = chunk("modl", concat({chunk("NAME", text("rep_inf_trooper")), chunk("VBUF", {1, 2, 3, 4, 5})}));
    Bytes sublevel_payload;
    put_u32(sublevel_payload, 0xdeadbeef);  // name hash
    const Bytes sublevel_children = model;
    put_u32(sublevel_payload, static_cast<uint32_t>(sublevel_children.size()));
    sublevel_payload.insert(sublevel_payload.end(), sublevel_children.begin(), sublevel_children.end());
    const Bytes file = chunk("ucfb", concat({chunk("lvl_", sublevel_payload), chunk("tex_", concat({chunk("NAME", text("tx"))}))}));

    const UcfbNode root = parse_ucfb(file);
    CHECK(root.tag == "ucfb");
    CHECK(root.children.size() == 2);
    if (root.children.size() != 2) {
        return;
    }
    const UcfbNode& sublevel = root.children[0];
    CHECK(sublevel.tag == "lvl_" && sublevel.name_hash == 0xdeadbeef);
    const UcfbNode* modl = sublevel.child("modl");
    CHECK(modl && modl->name() == "rep_inf_trooper");
    const UcfbNode* vbuf = modl ? modl->child("VBUF") : nullptr;
    CHECK(vbuf && vbuf->children.empty() && vbuf->data.size() == 5);  // raw data, not children
    CHECK(root.children[1].name() == "tx");
}

void test_unpadded_last_chunk() {
    // The final chunk of a container may omit its padding.
    const Bytes file = chunk("ucfb", chunk("NAME", text("ab"), false));
    const UcfbNode root = parse_ucfb(file);
    CHECK(root.children.size() == 1 && root.name() == "ab");
}

void test_rejects_non_ucfb() {
    bool threw = false;
    try {
        parse_ucfb(chunk("RIFF", {1, 2, 3, 4}));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main() {
    test_tree();
    test_unpadded_last_chunk();
    test_rejects_non_ucfb();

    if (failures == 0) {
        std::printf("ucfb_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
