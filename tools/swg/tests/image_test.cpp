// Decodes small synthetic DDS images (no game data).

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "swg/image.h"

using namespace swm::swg;

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

void put_u32(Bytes& out, size_t offset, uint32_t value) {
    std::memcpy(out.data() + offset, &value, 4);
}

Bytes dds_header(uint32_t width, uint32_t height, const char* fourcc) {
    Bytes header(128, 0);
    std::memcpy(header.data(), "DDS ", 4);
    put_u32(header, 4, 124);
    put_u32(header, 12, height);
    put_u32(header, 16, width);
    put_u32(header, 76, 32);
    put_u32(header, 80, 0x4);  // DDPF_FOURCC
    std::memcpy(header.data() + 84, fourcc, 4);
    return header;
}

const uint8_t* pixel(const ImageRgba& image, uint32_t x, uint32_t y) {
    return &image.pixels[(size_t(y) * image.width + x) * 4];
}

void test_dxt1() {
    // One 4x4 block: color0 pure red (0xF800), color1 pure blue (0x001F).
    // Row 0 indices: 0, 1, 2, 3 (2 = 2/3 red + 1/3 blue, 3 = 1/3 red + 2/3 blue).
    Bytes dds = dds_header(4, 4, "DXT1");
    const uint8_t block[8] = {0x00, 0xF8, 0x1F, 0x00, 0b11100100, 0, 0, 0};
    dds.insert(dds.end(), block, block + 8);

    const ImageRgba image = decode_dds(dds);
    CHECK(image.width == 4 && image.height == 4);
    CHECK(pixel(image, 0, 0)[0] == 255 && pixel(image, 0, 0)[2] == 0 && pixel(image, 0, 0)[3] == 255);
    CHECK(pixel(image, 1, 0)[0] == 0 && pixel(image, 1, 0)[2] == 255);
    CHECK(pixel(image, 2, 0)[0] == 170 && pixel(image, 2, 0)[2] == 85);
    CHECK(pixel(image, 3, 0)[0] == 85 && pixel(image, 3, 0)[2] == 170);
    CHECK(pixel(image, 0, 1)[0] == 255);  // index 0 elsewhere
}

void test_dxt5_alpha() {
    // Alpha endpoints 255 and 0, texel 0 index 0 (255), texel 1 index 1 (0).
    Bytes dds = dds_header(4, 4, "DXT5");
    const uint8_t block[16] = {255, 0, 0b00001000, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0};
    dds.insert(dds.end(), block, block + 16);

    const ImageRgba image = decode_dds(dds);
    CHECK(pixel(image, 0, 0)[3] == 255);
    CHECK(pixel(image, 1, 0)[3] == 0);
    CHECK(pixel(image, 0, 0)[0] == 255 && pixel(image, 0, 0)[1] == 255);  // white color endpoint
}

void test_png_signature() {
    const ImageRgba image{2, 1, {255, 0, 0, 255, 0, 255, 0, 255}};
    const auto path = std::filesystem::temp_directory_path() / "swm_image_test.png";
    write_png(path, image);
    std::ifstream in(path, std::ios::binary);
    char signature[8] = {};
    in.read(signature, 8);
    CHECK(std::memcmp(signature, "\x89PNG\r\n\x1a\n", 8) == 0);
    in.close();
    std::filesystem::remove(path);
}

} // namespace

int main() {
    test_dxt1();
    test_dxt5_alpha();
    test_png_signature();

    if (failures == 0) {
        std::printf("image_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
