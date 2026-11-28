#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace swbfo::swg {

// A texture renderer (.trt, BTRT 0001): a texture Galaxies composites at
// runtime from layers, for a mesh part's shader (SkinnedMesh's
// TextureRenderer): a face's skin, brows, makeup, freckles, tattoos. Read
// from the files (not documented):
//
// - DEST: the texture's width and height (u32s), then its format.
// - SHTM: a shader per layer (NAME each): a copy (the layer replaces what's
//   under it) or an alpha blend (over it, by its alpha). Their pixel program
//   draws the layer's texture times the layer's texture factor, its alpha
//   the texture's.
// - TXTS: INFO: a count, then that many texture paths.
// - VBS: a vertex buffer per layer (VTXA 0003: INFO flags, vertex count;
//   DATA: a quad of x, y, z, u, v floats): where in the texture the layer
//   goes, x and y from 0 to 1 as the texture's u and v (the first row on
//   top), and the layer texture's coordinates there.
// - RCMS: the drawing order: SRSC INFO (shader, ...), RTFC (vertex buffer).
// - PCMS: what sets each layer's texture and factor: SSTC (shader u32, tag,
//   texture index u32: a fixed texture), SST1 (shader u32, tag, first
//   texture index u32, a style variable, count u32: the variable picks one
//   of the next `count` textures), STFP (shader u16, tag, palette, color
//   variable, a byte: 0 for a variable the whole body shares, as a
//   customizable shader's PAL).
struct TextureRendererLayer {
    std::string shader;                   // decides how it blends (effect_blend)
    std::array<float, 4> rect{0, 0, 1, 1};  // where: x0, y0, x1, y1 (0..1, as u, v)
    std::array<float, 4> uv{0, 0, 1, 1};    // the layer texture's u, v at (x0, y0) and (x1, y1)
    std::vector<std::string> textures;    // one, or the choices of `style_variable`
    std::string style_variable;           // empty for a fixed texture
    std::string palette;                  // empty: the factor stays white
    std::string color_variable;
    bool color_shared = false;
};
struct TextureRenderer {
    uint32_t width = 0, height = 0;
    std::vector<TextureRendererLayer> layers;  // in drawing order
};

// Throws std::runtime_error on malformed or unsupported data.
TextureRenderer parse_texture_renderer(std::span<const uint8_t> bytes);

} // namespace swbfo::swg
