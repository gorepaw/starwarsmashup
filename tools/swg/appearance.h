#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace swm::swg {

// A skeletal appearance template (.sat, SMAT 0003).
struct SkeletalAppearance {
    struct SkeletonRef {
        std::string path;       // .skt
        std::string attach_to;  // joint in the previous skeletons, empty for the root skeleton
    };
    std::vector<std::string> mesh_generators;  // .lmg (or .mgn) paths
    std::vector<SkeletonRef> skeletons;
};

SkeletalAppearance parse_skeletal_appearance(std::span<const uint8_t> bytes);

// Mesh paths of a .lmg file (MLOD 0000), most detailed first.
std::vector<std::string> parse_mesh_lod_list(std::span<const uint8_t> bytes);

// The MAIN texture of a shader (.sht: SSHT, or CSHD wrapping one; for a
// switching texture shader, SWTS, its first frame's), with normalized
// slashes. Empty if the shader has none.
std::string shader_main_texture(std::span<const uint8_t> bytes);

// A customizable shader's colors (CSHD's TFAC): texture factors, each taken
// from a palette by a customization variable. Galaxies' pixel programs
// multiply the main texture by the MAIN factor; a "color2" effect's by MAIN
// and HUEB blended by the texture's alpha (0: MAIN, 1: HUEB).
struct PaletteColor {
    std::string variable;  // e.g. "index_color_1", "index_color_skin"
    // ASSUMPTION (inferred, not documented): the byte after the variable's
    // name is 0 for skin variables, which every part of a body shares, and 1
    // for the rest (a worn item's own colors). Read here as "shared".
    bool shared = false;
    std::string factor;   // "MAIN" or "HUEB"
    std::string palette;  // palette/....pal
    int default_index = 0;
};
struct ShaderCustomization {
    std::vector<PaletteColor> colors;
    // The shader's texture factors' values when no palette sets them (TFNS:
    // factor -> 0xAARRGGBB), e.g. HUEB on a color2 shader with only MAIN.
    std::vector<std::pair<std::string, uint32_t>> factor_defaults;
    // An owner proxy shader (OPST): stands for its wearer's shader by name
    // ("body": a garment's bare skin drawn as the wearer's body).
    std::string owner_proxy;

    // Whether the shader blends two colors (it has a HUEB factor).
    bool two_colors() const;
};
ShaderCustomization parse_shader_customization(std::span<const uint8_t> bytes);

// The effect (.eft) a shader draws with (SSHT's NAME; a CSHD's inner SSHT's),
// with normalized slashes. Empty if none (e.g. a switching texture shader).
std::string shader_effect(std::span<const uint8_t> bytes);

// How an effect (.eft: EFCT) blends what it draws, by its first pass (its
// first implementation's): the pass's DATA, whose leading bytes are the same
// in every version seen (0008, 0009). Read from comparing effects, not
// documented: byte 5 writes depth (0 for transparent effects), 7 blends,
// 9 and 10 the blend's factors (4, 5: by the texture's alpha, as alpha
// blending; 1, 1: added), 11 alpha tests, 17 the color write mask (7: not
// alpha). A cutout clips in its pixel program instead ("punchout": clip
// alpha under 0.5), its pass as an opaque one's.
// Also takes a shader with its effect inside it (SSHT / EFCT: a texture
// renderer's shaders).
enum class EffectBlend { opaque, alpha, additive, cutout };
EffectBlend effect_blend(std::span<const uint8_t> bytes);

// A palette (.pal: a RIFF "PAL " file, as Windows' logical palettes): its
// colors, RGB.
std::vector<std::array<uint8_t, 3>> parse_palette(std::span<const uint8_t> bytes);

} // namespace swm::swg
