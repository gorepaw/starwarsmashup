#include "swg/appearance.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "swg/iff.h"
#include "swg/manifest.h"

namespace swbfo::swg {
namespace {

// Finds the SSHT form inside a shader file: SSHT itself, or CSHD/<version>/SSHT.
const IffNode* find_ssht(const IffNode& node) {
    if (node.form && node.tag == "SSHT") {
        return &node;
    }
    for (const IffNode& child : node.children) {
        if (const IffNode* found = find_ssht(child)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

SkeletalAppearance parse_skeletal_appearance(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "SMAT") {
        throw std::runtime_error("not a skeletal appearance (" + root.tag + ")");
    }
    const IffNode& version = root.expect("0003");

    SkeletalAppearance appearance;
    appearance.mesh_generators = iff_strings(version.expect("MSGN").data);
    const std::vector<std::string> skeleton_info = iff_strings(version.expect("SKTI").data);
    for (size_t i = 0; i + 1 < skeleton_info.size(); i += 2) {
        appearance.skeletons.push_back({skeleton_info[i], skeleton_info[i + 1]});
    }
    return appearance;
}

std::vector<std::string> parse_mesh_lod_list(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "MLOD") {
        throw std::runtime_error("not a mesh LOD list (" + root.tag + ")");
    }
    std::vector<std::string> paths;
    for (const IffNode* name : root.expect("0000").children_with("NAME")) {
        paths.push_back(iff_strings(name->data).at(0));
    }
    return paths;
}

std::string shader_main_texture(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag == "SWTS") {
        // A switching texture shader (screens, signs): its frames' textures,
        // each a TEXT chunk (the texture's tag, then its path). The first
        // frame's main texture stands for it.
        for (const IffNode& version : root.children) {
            for (const IffNode* text : version.children_with("TEXT")) {
                if (text->data.size() > 4 && std::memcmp(text->data.data(), "NIAM", 4) == 0) {
                    return normalize_path(iff_strings(text->data.subspan(4)).at(0));
                }
            }
        }
        return {};
    }
    const IffNode* ssht = find_ssht(root);
    if (!ssht) {
        return {};
    }
    for (const IffNode& version : ssht->children) {
        const IffNode* txms = version.child("TXMS");
        if (!txms) {
            continue;
        }
        for (const IffNode* txm : txms->children_with("TXM ")) {
            for (const IffNode& txm_version : txm->children) {
                const IffNode* data = txm_version.child("DATA");
                const IffNode* name = txm_version.child("NAME");
                // Texture tags are stored as little-endian uint32s: "MAIN" reads as "NIAM".
                if (data && name && data->data.size() >= 4 && std::memcmp(data->data.data(), "NIAM", 4) == 0) {
                    return normalize_path(iff_strings(name->data).at(0));
                }
            }
        }
    }
    return {};
}

namespace {

// Texture factor tags are stored as little-endian uint32s: "MAIN" reads as
// "NIAM". Returns the tag as written in the shader's source ("MAIN").
std::string factor_tag(std::span<const uint8_t> bytes) {
    if (bytes.size() < 4) {
        throw std::runtime_error("truncated texture factor tag");
    }
    return {char(bytes[3]), char(bytes[2]), char(bytes[1]), char(bytes[0])};
}

const IffNode* find_form(const IffNode& node, std::string_view tag) {
    if (node.form && node.tag == tag) {
        return &node;
    }
    for (const IffNode& child : node.children) {
        if (const IffNode* found = find_form(child, tag)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

bool ShaderCustomization::two_colors() const {
    return std::any_of(colors.begin(), colors.end(), [](const PaletteColor& c) { return c.factor == "HUEB"; }) ||
           std::any_of(factor_defaults.begin(), factor_defaults.end(), [](const auto& d) { return d.first == "HUEB"; });
}

ShaderCustomization parse_shader_customization(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    ShaderCustomization result;
    if (root.tag == "OPST") {
        // FORM OPST / 0000 / INFO: the proxied shader's name.
        for (const IffNode& version : root.children) {
            if (const IffNode* info = version.form ? version.child("INFO") : nullptr) {
                result.owner_proxy = IffReader(info->data).cstring();
            }
        }
        return result;
    }
    if (const IffNode* names = find_form(root, "TFNS")) {
        // TFNS / 0000: (tag, 0xAARRGGBB) pairs.
        for (const IffNode& version : names->children) {
            for (size_t at = 0; at + 8 <= version.data.size(); at += 8) {
                IffReader r(version.data.subspan(at + 4, 4));
                result.factor_defaults.emplace_back(factor_tag(version.data.subspan(at, 4)), r.u32());
            }
        }
    }
    if (root.tag != "CSHD") {
        return result;
    }
    // CSHD / <version> / TFAC / PAL: variable name, the shared byte, the
    // factor's tag, the palette's path, the default index.
    for (const IffNode& version : root.children) {
        // TXTR / DATA: a count (u16), then that many texture paths; TXTR /
        // CUST / TX1D: the texture's tag, the first and the count of its
        // choices among them (u16s), the variable's name, a private byte, the
        // default index (u16).
        if (const IffNode* swaps = version.form ? version.child("TXTR") : nullptr) {
            std::vector<std::string> paths;
            if (const IffNode* data = swaps->child("DATA")) {
                IffReader r(data->data);
                const uint16_t count = r.u16();
                for (uint16_t i = 0; i < count; ++i) paths.push_back(normalize_path(r.cstring()));
            }
            if (const IffNode* cust = swaps->child("CUST")) {
                for (const IffNode* tx : cust->children_with("TX1D")) {
                    IffReader r(tx->data);
                    const uint32_t tag = r.u32();
                    TextureSwap swap;
                    swap.factor = factor_tag(std::span(reinterpret_cast<const uint8_t*>(&tag), 4));
                    const uint16_t first = r.u16();
                    const uint16_t count = r.u16();
                    swap.variable = r.cstring();
                    r.u8();
                    swap.default_index = r.u16();
                    for (size_t i = first; i < size_t(first) + count && i < paths.size(); ++i) {
                        swap.textures.push_back(paths[i]);
                    }
                    if (!swap.textures.empty()) result.textures.push_back(std::move(swap));
                }
            }
        }
        const IffNode* factors = version.form ? version.child("TFAC") : nullptr;
        if (!factors) {
            continue;
        }
        for (const IffNode* pal : factors->children_with("PAL ")) {
            IffReader r(pal->data);
            PaletteColor color;
            color.variable = r.cstring();
            color.shared = r.u8() == 0;
            const uint32_t tag = r.u32();
            color.factor = factor_tag(std::span(reinterpret_cast<const uint8_t*>(&tag), 4));
            color.palette = normalize_path(r.cstring());
            color.default_index = r.i32();
            result.colors.push_back(std::move(color));
        }
    }
    return result;
}

std::string shader_effect(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    const IffNode* ssht = find_ssht(root);
    if (!ssht) {
        return {};
    }
    for (const IffNode& version : ssht->children) {
        if (const IffNode* name = version.form ? version.child("NAME") : nullptr) {
            return normalize_path(IffReader(name->data).cstring());
        }
    }
    return {};
}

EffectBlend effect_blend(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    const IffNode* effect = find_form(root, "EFCT");
    if (!effect) {
        throw std::runtime_error("no effect in " + root.tag);
    }
    const IffNode* pass = find_form(*effect, "PASS");
    if (!pass) {
        return EffectBlend::opaque;
    }
    for (const IffNode& version : pass->children) {
        const IffNode* data = version.form ? version.child("DATA") : nullptr;
        if (!data || data->data.size() < 18) {
            continue;
        }
        const auto& d = data->data;
        if (d[7] != 0) {
            return d[10] == 1 ? EffectBlend::additive : EffectBlend::alpha;  // onto what's behind as it is
        }
        // A pixel program that clips: PPSH / <version> / DATA, a byte then
        // the program's path.
        if (const IffNode* program = find_form(version, "PPSH")) {
            for (const IffNode& program_version : program->children) {
                const IffNode* program_data = program_version.form ? program_version.child("DATA") : nullptr;
                if (program_data && program_data->data.size() > 1) {
                    const std::string path = IffReader(program_data->data.subspan(1)).cstring();
                    if (path.find("punchout") != std::string::npos) {
                        return EffectBlend::cutout;
                    }
                }
            }
        }
        return EffectBlend::opaque;
    }
    return EffectBlend::opaque;
}

std::vector<std::array<uint8_t, 3>> parse_palette(std::span<const uint8_t> bytes) {
    // "RIFF" <size> "PAL " "data" <size> <u16 version> <u16 count> then
    // count entries of r, g, b, flags.
    if (bytes.size() < 24 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "PAL ", 4) != 0 ||
        std::memcmp(bytes.data() + 12, "data", 4) != 0) {
        throw std::runtime_error("not a RIFF palette");
    }
    const size_t count = size_t(bytes[22]) | size_t(bytes[23]) << 8;
    if (bytes.size() < 24 + 4 * count) {
        throw std::runtime_error("truncated palette");
    }
    std::vector<std::array<uint8_t, 3>> colors(count);
    for (size_t i = 0; i < count; ++i) {
        colors[i] = {bytes[24 + 4 * i], bytes[25 + 4 * i], bytes[26 + 4 * i]};
    }
    return colors;
}

} // namespace swbfo::swg
