#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace swm::swg {

// SWG IFF: a tree of chunks. Each chunk is a 4-character tag, a big-endian
// uint32 size, then `size` bytes. A FORM chunk's data starts with a 4-character
// form name followed by child chunks. There is no padding between chunks.
// Payload data inside chunks is little-endian (see IffReader).
struct IffNode {
    std::string tag;                // chunk tag; for FORMs, the form name
    bool form = false;
    std::span<const uint8_t> data;  // chunk payload; empty for FORMs
    std::vector<IffNode> children;  // FORMs only

    // First direct child with this tag (form name for FORMs), or nullptr.
    const IffNode* child(std::string_view child_tag) const;

    // Like child(), but throws std::runtime_error if missing.
    const IffNode& expect(std::string_view child_tag) const;

    // All direct children with this tag.
    std::vector<const IffNode*> children_with(std::string_view child_tag) const;
};

// Splits a payload of null-terminated strings.
std::vector<std::string> iff_strings(std::span<const uint8_t> data);

// Parses a file that must contain exactly one root FORM; returns it by value
// (its spans still view `bytes`).
IffNode parse_iff_root(std::span<const uint8_t> bytes);

// Parses a buffer of top-level chunks. Nodes view `bytes`, which must outlive
// them. Throws std::runtime_error on malformed data.
std::vector<IffNode> parse_iff(std::span<const uint8_t> bytes);

// True if the buffer starts like an IFF file (a FORM chunk).
bool looks_like_iff(std::span<const uint8_t> bytes);

// Sequential little-endian reads from a chunk payload. Throws
// std::runtime_error on reading past the end.
class IffReader {
public:
    explicit IffReader(std::span<const uint8_t> data) : data_(data) {}

    uint8_t u8() { return read<uint8_t>(); }
    uint16_t u16() { return read<uint16_t>(); }
    uint32_t u32() { return read<uint32_t>(); }
    int32_t i32() { return read<int32_t>(); }
    float f32() { return read<float>(); }
    std::string cstring();  // null-terminated

    size_t remaining() const { return data_.size() - pos_; }

private:
    template <class T>
    T read() {
        require(sizeof(T));
        T value;
        std::memcpy(&value, data_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return value;
    }
    void require(size_t size) const;

    std::span<const uint8_t> data_;
    size_t pos_ = 0;
};

} // namespace swm::swg
