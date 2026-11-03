// Parses small synthetic IFF buffers (no game data).

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "swg/iff.h"

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
        out.push_back(static_cast<uint8_t>(size >> shift)); // big-endian
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

template <class T>
void put(Bytes& out, T value) {
    uint8_t raw[sizeof(T)];
    std::memcpy(raw, &value, sizeof(T)); // little-endian host
    out.insert(out.end(), raw, raw + sizeof(T));
}

bool throws(const Bytes& bytes) {
    try {
        parse_iff(bytes);
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

void test_tree_and_reader() {
    Bytes info;
    put<uint32_t>(info, 7);
    put<float>(info, 1.5f);
    info.insert(info.end(), {'h', 'i', 0});
    const Bytes file = form("TEST", {form("0001", {chunk("INFO", info)}), chunk("DATA", {1, 2, 3})});

    CHECK(looks_like_iff(file));
    const std::vector<IffNode> roots = parse_iff(file);
    CHECK(roots.size() == 1);
    if (roots.size() != 1) {
        return;
    }
    const IffNode& root = roots[0];
    CHECK(root.form && root.tag == "TEST");
    CHECK(root.children.size() == 2);

    const IffNode* version = root.child("0001");
    CHECK(version && version->form);
    const IffNode* info_node = version ? version->child("INFO") : nullptr;
    CHECK(info_node && !info_node->form && info_node->data.size() == info.size());
    if (info_node) {
        IffReader reader(info_node->data);
        CHECK(reader.u32() == 7);
        CHECK(reader.f32() == 1.5f);
        CHECK(reader.cstring() == "hi");
        CHECK(reader.remaining() == 0);
        bool overran = false;
        try {
            reader.u8();
        } catch (const std::runtime_error&) {
            overran = true;
        }
        CHECK(overran);
    }

    const IffNode* data = root.child("DATA");
    CHECK(data && data->data.size() == 3 && data->data[2] == 3);
    CHECK(root.child("NONE") == nullptr);
}

void test_rejects_malformed() {
    Bytes overrun = chunk("DATA", {1, 2, 3, 4});
    overrun[7] = 9; // claims 9 bytes, has 4
    CHECK(throws(overrun));

    Bytes truncated_header = form("TEST", {chunk("DATA", {1})});
    truncated_header.push_back('X'); // stray bytes after the last chunk
    CHECK(throws(truncated_header));

    CHECK(throws(chunk("FORM", {'A', 'B'}))); // FORM too small for a name
    CHECK(!looks_like_iff(chunk("DATA", {1, 2, 3, 4})));
}

} // namespace

int main() {
    test_tree_and_reader();
    test_rejects_malformed();

    if (failures == 0) {
        std::printf("iff_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
