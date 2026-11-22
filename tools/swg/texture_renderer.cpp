#include "swg/texture_renderer.h"

#include <algorithm>
#include <stdexcept>

#include "swg/iff.h"
#include "swg/manifest.h"

namespace swm::swg {
namespace {

std::string tag_of(uint32_t tag) {
    const auto* t = reinterpret_cast<const char*>(&tag);
    return {t[3], t[2], t[1], t[0]};
}

// A layer's quad: its extent in the texture and the layer texture's
// coordinates at its corners.
void read_quad(const IffNode& vtxa, TextureRendererLayer& layer) {
    for (const IffNode& version : vtxa.children) {
        const IffNode* info = version.form ? version.child("INFO") : nullptr;
        const IffNode* data = version.form ? version.child("DATA") : nullptr;
        if (!info || !data) {
            continue;
        }
        IffReader header(info->data);
        header.u32();  // flags: position, one 2D texture coordinate set
        const uint32_t count = header.u32();
        if (count == 0 || data->data.size() != count * 5 * sizeof(float)) {
            throw std::runtime_error("texture renderer: unsupported vertex layout");
        }
        IffReader r(data->data);
        std::vector<std::array<float, 4>> vertices(count);  // x, y, u, v
        for (auto& v : vertices) {
            v[0] = r.f32();
            v[1] = r.f32();
            r.f32();  // z
            v[2] = r.f32();
            v[3] = r.f32();
        }
        const auto [min_x, max_x] = std::minmax_element(vertices.begin(), vertices.end(),
                                                        [](const auto& a, const auto& b) { return a[0] < b[0]; });
        const auto [min_y, max_y] = std::minmax_element(vertices.begin(), vertices.end(),
                                                        [](const auto& a, const auto& b) { return a[1] < b[1]; });
        layer.rect = {(*min_x)[0], (*min_y)[1], (*max_x)[0], (*max_y)[1]};
        layer.uv = {(*min_x)[2], (*min_y)[3], (*max_x)[2], (*max_y)[3]};
        return;
    }
    throw std::runtime_error("texture renderer: a vertex buffer without vertices");
}

} // namespace

TextureRenderer parse_texture_renderer(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "BTRT") {
        throw std::runtime_error("not a texture renderer (" + root.tag + ")");
    }
    const IffNode& version = root.expect("0001");
    TextureRenderer renderer;
    {
        IffReader dest(version.expect("DEST").data);
        renderer.width = dest.u32();
        renderer.height = dest.u32();
    }

    std::vector<std::string> shaders;
    for (const IffNode* name : version.expect("SHTM").children_with("NAME")) {
        shaders.push_back(normalize_path(IffReader(name->data).cstring()));
    }
    std::vector<std::string> textures;
    {
        IffReader r(version.expect("TXTS").expect("INFO").data);
        const uint32_t count = r.u32();
        for (uint32_t i = 0; i < count; ++i) {
            textures.push_back(normalize_path(r.cstring()));
        }
    }
    std::vector<TextureRendererLayer> quads;
    for (const IffNode* vtxa : version.expect("VBS ").children_with("VTXA")) {
        read_quad(*vtxa, quads.emplace_back());
    }

    // The layers, one per shader, set by the commands.
    std::vector<TextureRendererLayer> by_shader(shaders.size());
    for (size_t i = 0; i < shaders.size(); ++i) {
        by_shader[i].shader = shaders[i];
    }
    const auto layer = [&](uint32_t shader) -> TextureRendererLayer& {
        if (shader >= by_shader.size()) {
            throw std::runtime_error("texture renderer: a command for a missing shader");
        }
        return by_shader[shader];
    };
    const auto texture = [&](uint32_t index) {
        if (index >= textures.size()) {
            throw std::runtime_error("texture renderer: a missing texture");
        }
        return textures[index];
    };
    for (const IffNode* command : version.expect("PCMS").children_with("PCMD")) {
        for (const IffNode& operation : command->children) {  // TOPS
            for (const IffNode& op : operation.children) {
                if (op.form || op.tag == "INFO") {
                    continue;
                }
                IffReader r(op.data);
                if (op.tag == "SSTC") {
                    TextureRendererLayer& l = layer(r.u32());
                    r.u32();  // the shader's texture tag (MAIN)
                    l.textures = {texture(r.u32())};
                } else if (op.tag == "SST1") {
                    TextureRendererLayer& l = layer(r.u32());
                    r.u32();
                    const uint32_t first = r.u32();
                    l.style_variable = r.cstring();
                    const uint32_t count = r.u32();
                    l.textures.clear();
                    for (uint32_t i = 0; i < count; ++i) {
                        l.textures.push_back(texture(first + i));
                    }
                } else if (op.tag == "STFP") {
                    TextureRendererLayer& l = layer(r.u16());
                    r.u32();  // the shader's texture factor (MAIN)
                    l.palette = normalize_path(r.cstring());
                    l.color_variable = r.cstring();
                    l.color_shared = r.u8() == 0;
                } else {
                    throw std::runtime_error("texture renderer: unsupported command " + op.tag);
                }
            }
        }
    }

    // In drawing order: each render command's shader and vertex buffer.
    for (const IffNode* command : version.expect("RCMS").children_with("SRSC")) {
        const uint32_t shader = IffReader(command->expect("INFO").data).u32();
        const uint32_t buffer = IffReader(command->expect("RTFC").data).u32();
        if (buffer >= quads.size()) {
            throw std::runtime_error("texture renderer: a missing vertex buffer");
        }
        TextureRendererLayer drawn = layer(shader);
        drawn.rect = quads[buffer].rect;
        drawn.uv = quads[buffer].uv;
        renderer.layers.push_back(std::move(drawn));
    }
    return renderer;
}

} // namespace swm::swg
