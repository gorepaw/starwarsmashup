#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace swm::core::net {

static_assert(std::endian::native == std::endian::little, "packets are little-endian; add byte swapping");

// Appends little-endian values to a packet.
class ByteWriter {
public:
    template <typename T>
        requires std::is_arithmetic_v<T>
    void put(T value) {
        const size_t at = data_.size();
        data_.resize(at + sizeof(T));
        std::memcpy(data_.data() + at, &value, sizeof(T));
    }
    void put_string(const std::string& value);  // u8 length + bytes, at most 255

    const std::vector<uint8_t>& data() const { return data_; }

private:
    std::vector<uint8_t> data_;
};

// Reads little-endian values from a packet. Throws PacketError past the end.
class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> data) : data_(data) {}

    template <typename T>
        requires std::is_arithmetic_v<T>
    T get() {
        need(sizeof(T));
        T value;
        std::memcpy(&value, data_.data() + at_, sizeof(T));
        at_ += sizeof(T);
        return value;
    }
    std::string get_string();

    bool at_end() const { return at_ == data_.size(); }

private:
    void need(size_t bytes) const;

    std::span<const uint8_t> data_;
    size_t at_ = 0;
};

struct PacketError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace swm::core::net
