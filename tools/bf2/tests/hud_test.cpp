// Reads a small synthetic HUD layout, font and string table (no game data).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bf2/hud.h"
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

// A config record: values are floats, or strings (stored after the values,
// in one block: its length, then each string NUL-terminated; a string's value
// is its offset from the first value, 4 bytes before the string).
struct Value {
    bool is_text;
    float number;
    std::string text;
};
Bytes record(const char* keyword, const std::vector<Value>& values) {
    Bytes out;
    put(out, odf_hash(keyword));
    put(out, uint8_t(values.size()));
    Bytes strings;
    std::vector<uint32_t> offsets;
    size_t at = values.size() * 4;  // the block's start
    for (const Value& v : values) {
        if (v.is_text) {
            offsets.push_back(uint32_t(at + strings.size()));
            strings.insert(strings.end(), v.text.begin(), v.text.end());
            strings.push_back(0);
        }
    }
    size_t s = 0;
    for (const Value& v : values) {
        if (v.is_text) {
            put(out, offsets[s++]);
        } else {
            put(out, v.number);
        }
    }
    if (!strings.empty()) {
        put(out, uint32_t(strings.size()));
        out.insert(out.end(), strings.begin(), strings.end());
    }
    return chunk("DATA", out);
}
Value num(float f) { return {false, f, ""}; }
Value str(const std::string& s) { return {true, 0, s}; }

void layout() {
    const Bytes hud = chunk(
        "hud_", join({record("Group", {str("player1healthbar")}),
                      chunk("SCOP", join({record("Position", {num(0.01f), num(0.95f), num(0), str("Viewport")}),
                                          record("BitmapRect", {num(0.2f), num(0.015f), str("Left"), str("Top"),
                                                                str("Viewport")}),
                                          record("EventEnable", {str("player1.health")})})),
                      record("ZOrder", {num(3)})}));
    const Bytes file = chunk("ucfb", hud);
    const UcfbNode root = parse_ucfb(file);
    const std::vector<HudNode> tree = read_config_tree(root.children.at(0));
    CHECK(tree.size() == 2);
    const HudNode& group = tree.at(0);
    CHECK(group.keyword == "Group" && group.has_text && group.text == "player1healthbar");
    CHECK(group.children.size() == 3);
    const HudNode& position = group.children.at(0);
    CHECK(position.keyword == "Position" && position.values.size() == 4);
    CHECK(!position.values[0].is_text && std::abs(position.values[1].number - 0.95) < 1e-6);
    CHECK(position.values[3].is_text && position.values[3].text == "Viewport");
    const HudNode& rect = group.children.at(1);
    CHECK(rect.keyword == "BitmapRect" && rect.values.size() == 5);
    CHECK(rect.values[2].text == "Left" && rect.values[3].text == "Top" && rect.values[4].text == "Viewport");
    CHECK(group.children.at(2).text == "player1.health");
    CHECK(tree.at(1).keyword == "ZOrder" && !tree.at(1).has_text && tree.at(1).values.at(0).number == 3);
    // An unknown keyword keeps its hash.
    const Bytes other = chunk("ucfb", chunk("hud_", record("NotAHudKeyword", {num(1)})));
    const std::vector<HudNode> unknown = read_config_tree(parse_ucfb(other).children.at(0));
    char hex[9];
    std::snprintf(hex, sizeof hex, "%08x", odf_hash("NotAHudKeyword"));
    CHECK(unknown.size() == 1 && unknown[0].keyword == hex);
}

void font() {
    Bytes head;
    put(head, uint16_t(2));  // glyphs
    put(head, uint8_t(1));   // pages
    put(head, uint8_t(22));  // line height
    put(head, uint16_t(0));
    Bytes body;
    for (const auto& [c, advance, x, w, top, bottom] :
         std::vector<std::array<int, 6>>{{'A', 12, 0, 12, 6, 17}, {'!', 5, 1, 4, 6, 17}}) {
        put(body, uint16_t(c));
        put(body, uint8_t(0));
        put(body, uint8_t(advance));
        put(body, int8_t(x));
        put(body, uint8_t(w));
        put(body, uint8_t(top));
        put(body, uint8_t(bottom));
        for (const float uv : {0.1f, 0.2f, 0.3f, 0.4f}) {
            put(body, uv);
        }
    }
    const Bytes name(std::string("gamefont_test").begin(), std::string("gamefont_test").end());
    Bytes named = name;
    named.push_back(0);
    const Bytes file = chunk("ucfb", chunk("font", join({chunk("NAME", named), chunk("HEAD", head),
                                                         chunk("FBOD", body)})));
    const UcfbNode root = parse_ucfb(file);
    const Font f = read_font(root.children.at(0));
    CHECK(f.name == "gamefont_test" && f.line_height == 22 && f.glyphs.size() == 2);
    CHECK(f.glyphs[0].character == 'A' && f.glyphs[0].advance == 12 && f.glyphs[0].width == 12);
    CHECK(f.glyphs[1].character == '!' && f.glyphs[1].x_offset == 1 && f.glyphs[1].top == 6 &&
          f.glyphs[1].bottom == 17);
    CHECK(std::abs(f.glyphs[1].uv[3] - 0.4f) < 1e-6f);
}

void strings() {
    Bytes body;
    const auto entry = [&](const char* key, const std::u16string& value) {
        put(body, odf_hash(key));
        const size_t size = 6 + (value.size() + 1) * 2;
        const size_t padded = (size + 3) / 4 * 4;
        put(body, uint16_t(padded));
        for (const char16_t c : value) {
            put(body, uint16_t(c));
        }
        for (size_t i = size - 2; i < padded; i += 2) {
            put(body, uint16_t(0));
        }
    };
    entry("common.victoryin", u"Victory In");
    entry("common.test", u"Café");
    Bytes named(std::string("english").begin(), std::string("english").end());
    named.push_back(0);
    const Bytes file = chunk("ucfb", chunk("Locl", join({chunk("NAME", named), chunk("BODY", body)})));
    const auto read = read_strings(parse_ucfb(file).children.at(0));
    CHECK(read.size() == 2);
    CHECK(read[0].first == odf_hash("common.victoryin") && read[0].second == "Victory In");
    CHECK(read[1].second == "Caf\xc3\xa9");  // UTF-8
}

} // namespace

int main() {
    layout();
    font();
    strings();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("hud_test: all passed\n");
    return 0;
}
