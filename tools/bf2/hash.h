#pragma once

#include <cctype>
#include <cstdint>
#include <string_view>

namespace swm::bf2 {

// Name hash used for bones and animations in munged animation data: CRC-32
// computed most-significant-bit first (polynomial 0x04C11DB7, initial value
// and final XOR 0xFFFFFFFF) over the lowercase name. Identified by matching
// the human animation skeleton's joint hashes against bone names (25 of 31).
inline uint32_t name_hash(std::string_view name) {
    uint32_t crc = 0xFFFFFFFF;
    for (const char c : name) {
        crc ^= uint32_t(uint8_t(std::tolower(static_cast<unsigned char>(c)))) << 24;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80000000) ? (crc << 1) ^ 0x04C11DB7 : crc << 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

} // namespace swm::bf2
