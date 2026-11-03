#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace swm::swg {

// Streaming SHA-256 (FIPS 180-4).
class Sha256 {
public:
    using Digest = std::array<uint8_t, 32>;

    Sha256();

    void update(const void* data, size_t size);
    Digest finish();

    static std::string to_hex(const Digest& digest);
    static std::string hex_of(const void* data, size_t size);

private:
    void process_block(const uint8_t* block);

    std::array<uint32_t, 8> state_;
    std::array<uint8_t, 64> buffer_{};
    size_t buffer_len_ = 0;
    uint64_t total_bytes_ = 0;
};

} // namespace swm::swg
