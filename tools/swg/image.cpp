#include "swg/image.h"

#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

#include <zlib.h>

namespace swm::swg {
namespace {

constexpr size_t dds_header_size = 128;
constexpr uint32_t pixel_format_fourcc = 0x4;
constexpr uint32_t pixel_format_rgb = 0x40;

uint32_t read_u32(const uint8_t* p) {
    uint32_t value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

uint16_t read_u16(const uint8_t* p) {
    uint16_t value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

std::array<uint8_t, 4> rgb565(uint16_t c) {
    const uint8_t r = uint8_t((c >> 11) & 31), g = uint8_t((c >> 5) & 63), b = uint8_t(c & 31);
    return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 2) | (g >> 4)), uint8_t((b << 3) | (b >> 2)), 255};
}

// Decodes a 4x4 DXT color block into `out` (16 RGBA texels). DXT3/5 always
// use the four-color mode.
void decode_color_block(const uint8_t* block, bool allow_transparent, uint8_t out[16][4]) {
    const uint16_t c0 = read_u16(block), c1 = read_u16(block + 2);
    std::array<std::array<uint8_t, 4>, 4> palette{rgb565(c0), rgb565(c1)};
    if (c0 > c1 || !allow_transparent) {
        for (int ch = 0; ch < 3; ++ch) {
            palette[2][ch] = uint8_t((2 * palette[0][ch] + palette[1][ch]) / 3);
            palette[3][ch] = uint8_t((palette[0][ch] + 2 * palette[1][ch]) / 3);
        }
        palette[2][3] = palette[3][3] = 255;
    } else {
        for (int ch = 0; ch < 3; ++ch) {
            palette[2][ch] = uint8_t((palette[0][ch] + palette[1][ch]) / 2);
            palette[3][ch] = 0;
        }
        palette[2][3] = 255;
        palette[3][3] = 0;
    }
    const uint32_t indices = read_u32(block + 4);
    for (int i = 0; i < 16; ++i) {
        std::memcpy(out[i], palette[(indices >> (2 * i)) & 3].data(), 4);
    }
}

void decode_dxt5_alpha(const uint8_t* block, uint8_t out[16][4]) {
    const uint8_t a0 = block[0], a1 = block[1];
    uint8_t alpha[8] = {a0, a1};
    if (a0 > a1) {
        for (int i = 1; i < 7; ++i) {
            alpha[i + 1] = uint8_t(((7 - i) * a0 + i * a1) / 7);
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            alpha[i + 1] = uint8_t(((5 - i) * a0 + i * a1) / 5);
        }
        alpha[6] = 0;
        alpha[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= uint64_t(block[2 + i]) << (8 * i);
    }
    for (int i = 0; i < 16; ++i) {
        out[i][3] = alpha[(bits >> (3 * i)) & 7];
    }
}

void decode_dxt3_alpha(const uint8_t* block, uint8_t out[16][4]) {
    for (int i = 0; i < 16; ++i) {
        const uint8_t nibble = (block[i / 2] >> (4 * (i % 2))) & 0xf;
        out[i][3] = uint8_t(nibble * 17);
    }
}

ImageRgba decode_dxt(std::span<const uint8_t> data, uint32_t width, uint32_t height, int dxt) {
    const size_t block_size = dxt == 1 ? 8 : 16;
    const uint32_t blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
    if (data.size() < size_t(blocks_x) * blocks_y * block_size) {
        throw std::runtime_error("DDS: truncated DXT data");
    }
    ImageRgba image{width, height, std::vector<uint8_t>(size_t(width) * height * 4)};
    const uint8_t* block = data.data();
    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx, block += block_size) {
            uint8_t texels[16][4];
            decode_color_block(dxt == 1 ? block : block + 8, dxt == 1, texels);
            if (dxt == 3) {
                decode_dxt3_alpha(block, texels);
            } else if (dxt == 5) {
                decode_dxt5_alpha(block, texels);
            }
            for (uint32_t i = 0; i < 16; ++i) {
                const uint32_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
                if (x < width && y < height) {
                    std::memcpy(&image.pixels[(size_t(y) * width + x) * 4], texels[i], 4);
                }
            }
        }
    }
    return image;
}

uint8_t extract_channel(uint32_t pixel, uint32_t mask) {
    if (mask == 0) {
        return 255;
    }
    const uint32_t value = (pixel & mask) >> std::countr_zero(mask);
    const uint32_t max = mask >> std::countr_zero(mask);
    return uint8_t(value * 255 / max);
}

void append_png_chunk(std::string& out, const char* type, const std::string& payload) {
    auto put_u32_be = [&](uint32_t v) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            out.push_back(char(v >> shift));
        }
    };
    put_u32_be(static_cast<uint32_t>(payload.size()));
    const size_t type_start = out.size();
    out.append(type, 4);
    out += payload;
    put_u32_be(static_cast<uint32_t>(
        crc32(0, reinterpret_cast<const Bytef*>(out.data() + type_start), static_cast<uInt>(4 + payload.size()))));
}

} // namespace

ImageRgba decode_dds(std::span<const uint8_t> bytes) {
    if (bytes.size() < dds_header_size || std::memcmp(bytes.data(), "DDS ", 4) != 0) {
        throw std::runtime_error("not a DDS file");
    }
    const uint32_t height = read_u32(bytes.data() + 12);
    const uint32_t width = read_u32(bytes.data() + 16);
    const uint32_t format_flags = read_u32(bytes.data() + 80);
    const std::span<const uint8_t> data = bytes.subspan(dds_header_size);

    if (format_flags & pixel_format_fourcc) {
        const std::string fourcc(reinterpret_cast<const char*>(bytes.data() + 84), 4);
        if (fourcc == "DXT1") {
            return decode_dxt(data, width, height, 1);
        }
        if (fourcc == "DXT2" || fourcc == "DXT3") {
            return decode_dxt(data, width, height, 3);
        }
        if (fourcc == "DXT4" || fourcc == "DXT5") {
            return decode_dxt(data, width, height, 5);
        }
        throw std::runtime_error("DDS: unsupported format " + fourcc);
    }
    const uint32_t bit_count = read_u32(bytes.data() + 88);
    if (!(format_flags & pixel_format_rgb) || bit_count != 32) {
        throw std::runtime_error("DDS: unsupported uncompressed format");
    }
    const uint32_t masks[4] = {read_u32(bytes.data() + 92), read_u32(bytes.data() + 96), read_u32(bytes.data() + 100),
                               read_u32(bytes.data() + 104)};
    if (data.size() < size_t(width) * height * 4) {
        throw std::runtime_error("DDS: truncated pixel data");
    }
    ImageRgba image{width, height, std::vector<uint8_t>(size_t(width) * height * 4)};
    for (size_t i = 0; i < size_t(width) * height; ++i) {
        const uint32_t pixel = read_u32(data.data() + i * 4);
        for (int ch = 0; ch < 4; ++ch) {
            image.pixels[i * 4 + ch] = extract_channel(pixel, masks[ch]);
        }
    }
    return image;
}

void write_png(const std::filesystem::path& path, const ImageRgba& image) {
    // Filter type 0 (none) on every row.
    std::string raw;
    raw.reserve((size_t(image.width) * 4 + 1) * image.height);
    for (uint32_t y = 0; y < image.height; ++y) {
        raw.push_back(0);
        raw.append(reinterpret_cast<const char*>(&image.pixels[size_t(y) * image.width * 4]), size_t(image.width) * 4);
    }
    uLongf compressed_size = compressBound(static_cast<uLong>(raw.size()));
    std::string compressed(compressed_size, '\0');
    if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &compressed_size,
                  reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), 6) != Z_OK) {
        throw std::runtime_error("PNG: compression failed");
    }
    compressed.resize(compressed_size);

    std::string header;
    for (const uint32_t v : {image.width, image.height}) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            header.push_back(char(v >> shift));
        }
    }
    header += std::string("\x08\x06\x00\x00\x00", 5);  // 8-bit RGBA, deflate, no filter, no interlace

    std::string png("\x89PNG\r\n\x1a\n", 8);
    append_png_chunk(png, "IHDR", header);
    append_png_chunk(png, "IDAT", compressed);
    append_png_chunk(png, "IEND", "");

    std::ofstream out(path, std::ios::binary);
    out.write(png.data(), static_cast<std::streamsize>(png.size()));
    if (!out) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

} // namespace swm::swg
