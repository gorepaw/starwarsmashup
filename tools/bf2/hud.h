#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bf2/ucfb.h"

namespace swm::bf2 {

// Battlefront II's HUD layouts ("hud_" chunks in ingame.lvl, munged from its
// .hud files) and fonts ("font" chunks in core.lvl).
//
// A layout is a tree of config records, the same encoding as world effects
// (fx__): each DATA is a keyword (FNV-1a hash of its lowercase name, see
// odf_hash), an optional string and numbers; a SCOP after it holds its
// children. Elements (Group, Bitmap, BarBitmap, Text, Model3D, Map...) name
// themselves with the string and hold properties (Position, Scale, Color,
// EventEnable...). Keyword names were recovered by hashing candidate words
// (hud_keyword); a few remain unknown and are kept as their hash.

// A value: a float or a string. A record's values are 4 bytes each; a
// string's is its offset from the first value to a block just past them (a
// uint32 block length, then the strings, each NUL-terminated), the string
// starting 4 bytes after that offset. E.g. Rect(0.09, 0.11, "Left", "Top",
// "Viewport"): a size and how it's aligned.
struct HudValue {
    bool is_text = false;
    std::string text;
    double number = 0;
};

struct HudNode {
    uint32_t hash = 0;
    std::string keyword;  // its name if known, else the hash as 8 hex digits
    std::string text;     // the first string value ("" if none): an element's name
    bool has_text = false;
    std::vector<HudValue> values;
    std::vector<HudNode> children;
};

// Reads a config chunk's records (its DATA and SCOP children) as a tree.
std::vector<HudNode> read_config_tree(const UcfbNode& chunk);

// A HUD keyword's name, or empty if it isn't known.
std::string_view hud_keyword(uint32_t hash);

// A bitmap font: glyphs cut from one or more texture pages.
//
// HEAD: uint16 glyph count, uint8 page count, uint8 line height (pixels).
// FBOD: per glyph, 24 bytes: uint16 character (UTF-16), uint8 page, uint8
// advance, int8 x offset, uint8 width, uint8 top, uint8 bottom (pixels; top
// and bottom are rows below the line's top), then floats u0, u1, v0, v1 on
// its page (its width x (bottom - top) pixels). FTEX: the pages, "tex_"
// chunks.
struct FontGlyph {
    uint16_t character = 0;
    uint8_t page = 0;
    uint8_t advance = 0;
    int8_t x_offset = 0;
    uint8_t width = 0;
    uint8_t top = 0;
    uint8_t bottom = 0;
    std::array<float, 4> uv{};  // u0, u1, v0, v1
};

struct Font {
    std::string name;
    int line_height = 0;
    std::vector<const UcfbNode*> pages;  // tex_ chunks, viewing the file
    std::vector<FontGlyph> glyphs;
};

// Reads a "font" chunk. Throws std::runtime_error on malformed data.
Font read_font(const UcfbNode& font);

// One language's strings ("Locl" chunks in core.lvl, named by language):
// BODY is a run of entries, each a uint32 key (odf_hash of the dotted key,
// e.g. "common.victoryin"), a uint16 entry size in bytes (this header
// included) and a NUL-terminated UTF-16 string. Returned as UTF-8, by key.
std::vector<std::pair<uint32_t, std::string>> read_strings(const UcfbNode& locl);

} // namespace swm::bf2
