#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace swm::swg {

struct ImageRgba {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;  // RGBA8, rows top to bottom
};

// Decodes the top mip level of a .dds: DXT1, DXT3, DXT5, or uncompressed
// 32-bit with channel masks. Throws std::runtime_error otherwise.
ImageRgba decode_dds(std::span<const uint8_t> bytes);

// Decodes DXT1, DXT3 or DXT5 blocks (`dxt` = 1, 3 or 5) of a width x height
// image. Throws std::runtime_error if `data` is too short.
ImageRgba decode_dxt(std::span<const uint8_t> data, uint32_t width, uint32_t height, int dxt);

// Throws std::runtime_error on I/O failure.
void write_png(const std::filesystem::path& path, const ImageRgba& image);

} // namespace swm::swg
