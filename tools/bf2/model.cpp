#include "bf2/model.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace swm::bf2 {

namespace {

template <typename T>
T read(std::span<const uint8_t> data, size_t offset) {
    if (offset + sizeof(T) > data.size()) {
        throw std::runtime_error("bf2 model: read past the end of a chunk");
    }
    T value;
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

std::string text(std::span<const uint8_t> data) {
    const auto* begin = reinterpret_cast<const char*>(data.data());
    return std::string(begin, strnlen(begin, data.size()));
}

// A D3DCOLOR (stored B, G, R, A) as RGBA.
std::array<uint8_t, 4> d3d_color(std::span<const uint8_t> data, size_t offset) {
    const auto bgra = read<std::array<uint8_t, 4>>(data, offset);
    return {bgra[2], bgra[1], bgra[0], bgra[3]};
}

// Vertex buffer flags (VBUF).
constexpr uint32_t vertex_position = 1u << 1;
constexpr uint32_t vertex_bone_indices = 1u << 2;
constexpr uint32_t vertex_bone_weights = 1u << 3;
constexpr uint32_t vertex_normal = 1u << 5;
constexpr uint32_t vertex_tangents = 1u << 6;
constexpr uint32_t vertex_color = 1u << 7;
constexpr uint32_t vertex_static_lighting = 1u << 8;
constexpr uint32_t vertex_texcoords = 1u << 9;
constexpr uint32_t vertex_compressed = 0xf000;  // compressed position, bone info, normal or texcoords

// Reads an uncompressed vertex buffer into `segment`: position, bone
// indices and weights (skipped: skinned vertices are read in their bind pose,
// in model space), normal, tangents (skipped), color or static lighting,
// texcoords. False for compressed buffers or an unexpected stride.
bool read_vertices(std::span<const uint8_t> vbuf, ModelSegment& segment) {
    const uint32_t count = read<uint32_t>(vbuf, 0);
    const uint32_t stride = read<uint32_t>(vbuf, 4);
    const uint32_t flags = read<uint32_t>(vbuf, 8);
    if ((flags & vertex_compressed) || !(flags & vertex_position)) {
        return false;
    }
    const bool normal = flags & vertex_normal, tangents = flags & vertex_tangents;
    const bool color = flags & (vertex_color | vertex_static_lighting), texcoords = flags & vertex_texcoords;
    // Bone indices: 4 bytes; weights: ASSUMPTION 2 floats (no map seen yet).
    const uint32_t bones = (flags & vertex_bone_indices ? 4 : 0) + (flags & vertex_bone_weights ? 8 : 0);
    const uint32_t expected =
        12 + bones + (normal ? 12 : 0) + (tangents ? 24 : 0) + (color ? 4 : 0) + (texcoords ? 8 : 0);
    if (stride != expected || 12 + size_t(count) * stride > vbuf.size()) {
        return false;
    }
    segment.positions.resize(count);
    segment.normals.assign(count, {0, 1, 0});
    segment.uvs.assign(count, {0, 0});
    segment.colors.assign(color ? count : 0, {255, 255, 255, 255});
    segment.baked_lighting = (flags & vertex_static_lighting) && !(flags & vertex_color);
    for (uint32_t i = 0; i < count; ++i) {
        size_t at = 12 + size_t(i) * stride;
        segment.positions[i] = read<std::array<float, 3>>(vbuf, at);
        at += 12 + bones;
        if (normal) {
            segment.normals[i] = read<std::array<float, 3>>(vbuf, at);
            at += 12;
        }
        at += tangents ? 24 : 0;
        if (color) {
            segment.colors[i] = d3d_color(vbuf, at);
            if (!(flags & vertex_color)) {
                segment.colors[i][3] = 255;  // baked lighting has no alpha
            }
            at += 4;
        }
        if (texcoords) {
            segment.uvs[i] = read<std::array<float, 2>>(vbuf, at);
        }
    }
    return true;
}

// Triangle strips (Direct3D topology 5) or lists (4) to a list.
std::vector<uint32_t> read_triangles(std::span<const uint8_t> ibuf, uint32_t topology, size_t vertices) {
    const uint32_t count = read<uint32_t>(ibuf, 0);
    std::vector<uint32_t> indices(count);
    for (uint32_t i = 0; i < count; ++i) {
        indices[i] = read<uint16_t>(ibuf, 4 + size_t(i) * 2);
        if (indices[i] >= vertices) {
            throw std::runtime_error("bf2 model: index past the vertices");
        }
    }
    if (topology == 4) {
        indices.resize(indices.size() / 3 * 3);
        return indices;
    }
    if (topology != 5) {
        throw std::runtime_error("bf2 model: unsupported primitive topology " + std::to_string(topology));
    }
    std::vector<uint32_t> triangles;
    for (size_t k = 0; k + 2 < indices.size(); ++k) {
        uint32_t a = indices[k], b = indices[k + 1];
        const uint32_t c = indices[k + 2];
        if (a == b || b == c || a == c) {
            continue;  // degenerate: joins strips
        }
        if (k % 2 == 1) {
            std::swap(a, b);
        }
        triangles.insert(triangles.end(), {a, b, c});
    }
    return triangles;
}

ModelSegment read_segment(const UcfbNode& segm) {
    ModelSegment segment;
    const UcfbNode* info = segm.child("INFO");
    if (info == nullptr) {
        throw std::runtime_error("bf2 model: segment without INFO");
    }
    const uint32_t topology = read<uint32_t>(info->data, 0);
    if (const UcfbNode* material = segm.child("MTRL")) {
        segment.flags = read<uint32_t>(material->data, 0);
        segment.diffuse = d3d_color(material->data, 4);
    }
    if (const UcfbNode* type = segm.child("RTYP")) {
        segment.render_type = text(type->data);
    }
    if (const UcfbNode* bone = segm.child("BNAM")) {
        segment.bone = text(bone->data);
    }
    const UcfbNode* indices = nullptr;
    for (const UcfbNode& child : segm.children) {
        if (child.tag == "TNAM") {
            const uint32_t slot = read<uint32_t>(child.data, 0);
            if (slot < segment.textures.size()) {
                segment.textures[slot] = text(child.data.subspan(4));
            }
        } else if (child.tag == "IBUF") {
            indices = &child;
        } else if (child.tag == "VBUF" && segment.positions.empty()) {
            read_vertices(child.data, segment);
        }
    }
    if (indices != nullptr && !segment.positions.empty()) {
        segment.triangles = read_triangles(indices->data, topology, segment.positions.size());
    }
    return segment;
}

// Formats in order of preference (D3DFORMAT values; DXTn as FourCC).
constexpr uint32_t fourcc(const char (&code)[5]) {
    return uint32_t(code[0]) | uint32_t(code[1]) << 8 | uint32_t(code[2]) << 16 | uint32_t(code[3]) << 24;
}
constexpr uint32_t format_a8r8g8b8 = 21, format_x8r8g8b8 = 22, format_r5g6b5 = 23, format_a1r5g5b5 = 25,
                   format_a4r4g4b4 = 26, format_a8 = 28, format_l8 = 50, format_a8l8 = 51;
constexpr uint32_t preferred_formats[] = {format_a8r8g8b8, format_x8r8g8b8, fourcc("DXT5"), fourcc("DXT3"),
                                          fourcc("DXT1"),  format_a4r4g4b4, format_a1r5g5b5, format_r5g6b5,
                                          format_a8l8,     format_l8,       format_a8};

uint8_t expand(uint32_t value, int bits) {
    return uint8_t(value * 255 / ((1u << bits) - 1));
}

swg::ImageRgba decode_pixels(uint32_t format, uint32_t width, uint32_t height, std::span<const uint8_t> body) {
    if (format == fourcc("DXT1") || format == fourcc("DXT3") || format == fourcc("DXT5")) {
        return swg::decode_dxt(body, width, height, format == fourcc("DXT1") ? 1 : format == fourcc("DXT3") ? 3 : 5);
    }
    const size_t pixels = size_t(width) * height;
    const size_t bytes = format == format_a8r8g8b8 || format == format_x8r8g8b8 ? 4
                         : format == format_l8 || format == format_a8           ? 1
                                                                                : 2;
    if (body.size() < pixels * bytes) {
        throw std::runtime_error("bf2 texture: truncated pixels");
    }
    swg::ImageRgba image{width, height, std::vector<uint8_t>(pixels * 4)};
    for (size_t i = 0; i < pixels; ++i) {
        uint8_t* out = &image.pixels[i * 4];
        const uint8_t* in = body.data() + i * bytes;
        const uint32_t v = bytes == 2 ? uint32_t(in[0] | in[1] << 8) : 0;
        switch (format) {
        case format_a8r8g8b8:
        case format_x8r8g8b8:
            out[0] = in[2], out[1] = in[1], out[2] = in[0], out[3] = format == format_x8r8g8b8 ? 255 : in[3];
            break;
        case format_r5g6b5:
            out[0] = expand(v >> 11, 5), out[1] = expand((v >> 5) & 63, 6), out[2] = expand(v & 31, 5), out[3] = 255;
            break;
        case format_a1r5g5b5:
            out[0] = expand((v >> 10) & 31, 5), out[1] = expand((v >> 5) & 31, 5), out[2] = expand(v & 31, 5);
            out[3] = (v >> 15) ? 255 : 0;
            break;
        case format_a4r4g4b4:
            out[0] = expand((v >> 8) & 15, 4), out[1] = expand((v >> 4) & 15, 4), out[2] = expand(v & 15, 4);
            out[3] = expand(v >> 12, 4);
            break;
        case format_a8:
            out[0] = out[1] = out[2] = 255, out[3] = in[0];
            break;
        case format_l8:
            out[0] = out[1] = out[2] = in[0], out[3] = 255;
            break;
        default:  // A8L8
            out[0] = out[1] = out[2] = in[0], out[3] = in[1];
            break;
        }
    }
    return image;
}

} // namespace

void Skeleton::model_transform(const std::string& bone, std::array<float, 9>& rotation,
                               std::array<float, 3>& position) const {
    rotation = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    position = {0, 0, 0};
    std::string name = bone;
    // Walk up to the root, applying each parent's transform: p' = R p + t.
    for (size_t guard = 0; guard <= bones.size() && !name.empty(); ++guard) {
        const auto it = std::find_if(bones.begin(), bones.end(), [&](const ModelBone& b) { return b.name == name; });
        if (it == bones.end()) {
            break;
        }
        const auto& r = it->rotation;
        std::array<float, 9> combined{};
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                for (int k = 0; k < 3; ++k) {
                    combined[row * 3 + col] += r[row * 3 + k] * rotation[k * 3 + col];
                }
            }
        }
        std::array<float, 3> moved{};
        for (int row = 0; row < 3; ++row) {
            moved[row] = r[row * 3] * position[0] + r[row * 3 + 1] * position[1] + r[row * 3 + 2] * position[2] +
                         it->position[row];
        }
        rotation = combined;
        position = moved;
        name = it->parent;
    }
}

Model read_model(const UcfbNode& modl) {
    Model model;
    model.name = modl.name();
    for (const UcfbNode& child : modl.children) {
        if (child.tag == "segm") {
            model.segments.push_back(read_segment(child));
        }
    }
    return model;
}

// "skel": INFO is the model's name and the bone count; NAME and PRNT are the
// bones' names and parents, null-separated; XFRM a 3x3 rotation and a
// position per bone.
Skeleton read_skeleton(const UcfbNode& skel) {
    Skeleton skeleton;
    const UcfbNode* info = skel.child("INFO");
    const UcfbNode* names = skel.child("NAME");
    const UcfbNode* parents = skel.child("PRNT");
    const UcfbNode* transforms = skel.child("XFRM");
    if (info == nullptr || names == nullptr) {
        throw std::runtime_error("bf2 model: skeleton without INFO or NAME");
    }
    skeleton.model = text(info->data);
    const size_t count = read<uint32_t>(info->data, skeleton.model.size() + 1);
    size_t name_at = 0, parent_at = 0;
    for (size_t i = 0; i < count; ++i) {
        ModelBone bone;
        if (name_at < names->data.size()) {
            bone.name = text(names->data.subspan(name_at));
            name_at += bone.name.size() + 1;
        }
        if (parents != nullptr && parent_at < parents->data.size()) {
            bone.parent = text(parents->data.subspan(parent_at));
            parent_at += bone.parent.size() + 1;
        }
        if (transforms != nullptr && (i + 1) * 48 <= transforms->data.size()) {
            bone.rotation = read<std::array<float, 9>>(transforms->data, i * 48);
            bone.position = read<std::array<float, 3>>(transforms->data, i * 48 + 36);
        }
        skeleton.bones.push_back(bone);
    }
    return skeleton;
}

// "tex_": INFO lists the stored formats; each FMT_ has INFO (format, width,
// height, depth, mip count, type) and a FACE of LVL_ mip levels (INFO: level,
// size; BODY: the pixels).
std::optional<swg::ImageRgba> read_texture(const UcfbNode& tex) {
    for (const uint32_t wanted : preferred_formats) {
        for (const UcfbNode& format : tex.children) {
            const UcfbNode* info = format.tag == "FMT_" ? format.child("INFO") : nullptr;
            if (info == nullptr || read<uint32_t>(info->data, 0) != wanted) {
                continue;
            }
            const uint32_t width = read<uint16_t>(info->data, 4);
            const uint32_t height = read<uint16_t>(info->data, 6);
            const UcfbNode* face = format.child("FACE");
            if (face == nullptr) {
                continue;
            }
            for (const UcfbNode& level : face->children) {
                const UcfbNode* level_info = level.child("INFO");
                const UcfbNode* body = level.child("BODY");
                if (level.tag == "LVL_" && level_info && body && read<uint32_t>(level_info->data, 0) == 0) {
                    return decode_pixels(wanted, width, height, body->data);
                }
            }
        }
    }
    return std::nullopt;
}

} // namespace swm::bf2
