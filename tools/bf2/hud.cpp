#include "bf2/hud.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>

#include "bf2/odf.h"

namespace swm::bf2 {
namespace {

template <class T>
T read(std::span<const uint8_t> data, size_t offset) {
    if (offset + sizeof(T) > data.size()) {
        throw std::runtime_error("bf2 hud: read past the end of a chunk");
    }
    T value;
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

// Keywords of Battlefront II's .hud files, recovered by hashing candidate
// names against the munged layouts. The capitalization is a guess (hashes
// ignore case).
constexpr const char* keywords[] = {
    "Alpha", "Background", "Bitmap", "BlendMode", "Color", "EventAlpha", "EventColor", "EventDisable",
    "EventEnable", "EventMesh", "EventPosition", "EventRotation", "EventScale", "EventText", "EventValue", "Format",
    "Group", "Lighting", "Map", "Mesh", "Model3D", "Player", "Position", "Rect", "Rotation", "Scale", "Segment",
    "Target", "Text", "Value", "Viewport", "ZOrder",
    "AI", "AIText", "AngleEnd", "AngleStart", "AttackerOffScreen", "AttackerOnScreen", "AttackerOnScreenBehind",
    "Backdrop", "BackdropLarge", "BackdropSmall", "BackdropSpawn", "BarBitmap", "BitmapRect", "BitmapStyle",
    "Border", "ColorChange", "ColorChangeRate", "ColorPulseRate", "DisplayTime", "Empty", "EmptyText",
    "EventBitmap", "EventNameFilter", "EventNumber", "EventPlayerIndex", "EventPostHide", "EventPulseRate",
    "EventRefreshMarker", "EventRefreshPost", "EventRefreshTarget", "FadeInTime", "FadeOutTime", "FlagOffScreen",
    "FlagOnScreen", "FlagOnScreenBehind", "HintFlagOnScreen", "HintOffScreen", "HintOnScreen",
    "HintOnScreenBehind", "LockedOffScreen", "LockedOnScreen", "LockedOnScreenBehind", "MarkerLarge",
    "MarkerSmall", "MarkerSpawn", "Mask", "MaxScale", "MinScale", "MortarShapeCoord", "NameMesh", "NorthLarge",
    "NorthSmall", "NorthSpawn", "NumberColor", "NumberToTime", "ObjectiveList", "ObjectiveOffScreen",
    "ObjectiveOnScreen", "PlayerFOVLarge", "PlayerFOVSmall", "PlayerFOVSpawn", "PlayerText", "PositionLarge",
    "PositionSmall", "PositionSpawn", "PostLarge", "PostSelectLarge", "PostSelectSmall", "PostSelectSpawn",
    "PostSmall", "PostSpawn", "PostTextLarge", "PostTextSmall", "PostTextSpawn", "ReminderFlashingTotal",
    "ScaleTexture", "ScrollSpeed", "Self", "SelfText", "TargetLarge", "TargetSmall", "TargetSpawn",
    "TextAlignment", "TextBox", "TextFont", "TextScale", "TextStyle", "VehicleSeating",
};

const std::map<uint32_t, std::string_view>& keyword_table() {
    static const std::map<uint32_t, std::string_view> table = [] {
        std::map<uint32_t, std::string_view> out;
        for (const char* name : keywords) {
            out[odf_hash(name)] = name;
        }
        return out;
    }();
    return table;
}

HudNode read_record(std::span<const uint8_t> data) {
    HudNode node;
    node.hash = read<uint32_t>(data, 0);
    const std::string_view known = hud_keyword(node.hash);
    if (!known.empty()) {
        node.keyword = known;
    } else {
        char hex[9];
        std::snprintf(hex, sizeof hex, "%08x", node.hash);
        node.keyword = hex;
    }
    const uint8_t count = read<uint8_t>(data, 4);
    const std::span<const uint8_t> values = data.subspan(5);
    const size_t strings_at = size_t(count) * 4;  // the string block, if any
    for (size_t i = 0; i < count && (i + 1) * 4 <= values.size(); ++i) {
        const uint32_t bits = read<uint32_t>(values, i * 4);
        HudValue value;
        // A string's offset (no float stores those bit patterns: they're
        // denormals).
        if (bits >= strings_at && size_t(bits) + 4 < values.size() && bits < 0x00800000u) {
            const auto* start = reinterpret_cast<const char*>(values.data() + bits + 4);
            value.is_text = true;
            value.text.assign(start, strnlen(start, values.size() - bits - 4));
            if (!node.has_text) {
                node.text = value.text;
                node.has_text = true;
            }
        } else {
            value.number = double(read<float>(values, i * 4));
        }
        node.values.push_back(std::move(value));
    }
    return node;
}

} // namespace

std::string_view hud_keyword(uint32_t hash) {
    const auto& table = keyword_table();
    const auto it = table.find(hash);
    return it == table.end() ? std::string_view{} : it->second;
}

std::vector<HudNode> read_config_tree(const UcfbNode& chunk) {
    std::vector<HudNode> out;
    for (size_t i = 0; i < chunk.children.size(); ++i) {
        const UcfbNode& child = chunk.children[i];
        if (child.tag != "DATA") {
            continue;
        }
        HudNode node = read_record(child.data);
        if (i + 1 < chunk.children.size() && chunk.children[i + 1].tag == "SCOP") {
            node.children = read_config_tree(chunk.children[i + 1]);
            ++i;
        }
        out.push_back(std::move(node));
    }
    return out;
}

Font read_font(const UcfbNode& chunk) {
    Font font;
    font.name = chunk.name();
    const UcfbNode* head = chunk.child("HEAD");
    const UcfbNode* body = chunk.child("FBOD");
    if (head == nullptr || body == nullptr) {
        throw std::runtime_error("bf2 font " + font.name + ": no HEAD or FBOD");
    }
    const uint16_t count = read<uint16_t>(head->data, 0);
    font.line_height = read<uint8_t>(head->data, 3);
    if (body->data.size() < size_t(count) * 24) {
        throw std::runtime_error("bf2 font " + font.name + ": FBOD too short");
    }
    for (size_t i = 0; i < count; ++i) {
        const size_t at = i * 24;
        FontGlyph glyph;
        glyph.character = read<uint16_t>(body->data, at);
        glyph.page = read<uint8_t>(body->data, at + 2);
        glyph.advance = read<uint8_t>(body->data, at + 3);
        glyph.x_offset = read<int8_t>(body->data, at + 4);
        glyph.width = read<uint8_t>(body->data, at + 5);
        glyph.top = read<uint8_t>(body->data, at + 6);
        glyph.bottom = read<uint8_t>(body->data, at + 7);
        for (size_t k = 0; k < 4; ++k) {
            glyph.uv[k] = read<float>(body->data, at + 8 + k * 4);
        }
        font.glyphs.push_back(glyph);
    }
    if (const UcfbNode* textures = chunk.child("FTEX")) {
        for (const UcfbNode& page : textures->children) {
            if (page.tag == "tex_") {
                font.pages.push_back(&page);
            }
        }
    }
    return font;
}

std::vector<std::pair<uint32_t, std::string>> read_strings(const UcfbNode& locl) {
    std::vector<std::pair<uint32_t, std::string>> out;
    const UcfbNode* body = locl.child("BODY");
    if (body == nullptr) {
        return out;
    }
    const std::span<const uint8_t> data = body->data;
    size_t at = 0;
    while (at + 6 <= data.size()) {
        const uint32_t key = read<uint32_t>(data, at);
        const uint16_t size = read<uint16_t>(data, at + 4);
        if (size < 6 || at + size > data.size()) {
            break;
        }
        std::string text;
        for (size_t i = at + 6; i + 1 < at + size; i += 2) {
            const uint16_t c = read<uint16_t>(data, i);
            if (c == 0) {
                break;
            }
            // UTF-16 (the Basic Multilingual Plane) to UTF-8.
            if (c < 0x80) {
                text += char(c);
            } else if (c < 0x800) {
                text += char(0xc0 | (c >> 6));
                text += char(0x80 | (c & 0x3f));
            } else {
                text += char(0xe0 | (c >> 12));
                text += char(0x80 | ((c >> 6) & 0x3f));
                text += char(0x80 | (c & 0x3f));
            }
        }
        out.emplace_back(key, std::move(text));
        at += size;
    }
    return out;
}

} // namespace swm::bf2
