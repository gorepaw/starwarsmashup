#include "swg/iff.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace swm::swg {
namespace {

static_assert(std::endian::native == std::endian::little, "IFF payload reads assume a little-endian host");

constexpr size_t chunk_header_size = 8;
constexpr int max_depth = 64;

uint32_t read_u32_be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

std::string tag_at(const uint8_t* p) {
    return std::string(reinterpret_cast<const char*>(p), 4);
}

std::vector<IffNode> parse_chunks(std::span<const uint8_t> bytes, size_t base_offset, int depth) {
    if (depth > max_depth) {
        throw std::runtime_error("IFF nesting deeper than " + std::to_string(max_depth));
    }

    std::vector<IffNode> nodes;
    size_t pos = 0;
    while (pos < bytes.size()) {
        if (bytes.size() - pos < chunk_header_size) {
            throw std::runtime_error("IFF: truncated chunk header at offset " + std::to_string(base_offset + pos));
        }
        const std::string tag = tag_at(bytes.data() + pos);
        const uint32_t size = read_u32_be(bytes.data() + pos + 4);
        const size_t data_start = pos + chunk_header_size;
        if (size > bytes.size() - data_start) {
            throw std::runtime_error("IFF: chunk '" + tag + "' at offset " + std::to_string(base_offset + pos) +
                                     " overruns its parent (size " + std::to_string(size) + ")");
        }
        const std::span<const uint8_t> data = bytes.subspan(data_start, size);

        IffNode node;
        if (tag == "FORM") {
            if (size < 4) {
                throw std::runtime_error("IFF: FORM at offset " + std::to_string(base_offset + pos) +
                                         " is too small for a form name");
            }
            node.form = true;
            node.tag = tag_at(data.data());
            node.children = parse_chunks(data.subspan(4), base_offset + data_start + 4, depth + 1);
        } else {
            node.tag = tag;
            node.data = data;
        }
        nodes.push_back(std::move(node));
        pos = data_start + size;
    }
    return nodes;
}

} // namespace

const IffNode* IffNode::child(std::string_view child_tag) const {
    const auto it = std::find_if(children.begin(), children.end(),
                                 [&](const IffNode& node) { return node.tag == child_tag; });
    return it == children.end() ? nullptr : &*it;
}

const IffNode& IffNode::expect(std::string_view child_tag) const {
    if (const IffNode* node = child(child_tag)) {
        return *node;
    }
    throw std::runtime_error("IFF: '" + tag + "' has no '" + std::string(child_tag) + "' child");
}

std::vector<const IffNode*> IffNode::children_with(std::string_view child_tag) const {
    std::vector<const IffNode*> out;
    for (const IffNode& node : children) {
        if (node.tag == child_tag) {
            out.push_back(&node);
        }
    }
    return out;
}

std::vector<std::string> iff_strings(std::span<const uint8_t> data) {
    std::vector<std::string> out;
    auto begin = data.begin();
    while (begin != data.end()) {
        const auto end = std::find(begin, data.end(), uint8_t(0));
        out.emplace_back(begin, end);
        begin = end == data.end() ? end : end + 1;
    }
    return out;
}

std::vector<IffNode> parse_iff(std::span<const uint8_t> bytes) {
    return parse_chunks(bytes, 0, 0);
}

IffNode parse_iff_root(std::span<const uint8_t> bytes) {
    std::vector<IffNode> roots = parse_iff(bytes);
    if (roots.size() != 1 || !roots[0].form) {
        throw std::runtime_error("IFF: expected a single root FORM");
    }
    return std::move(roots[0]);
}

bool looks_like_iff(std::span<const uint8_t> bytes) {
    return bytes.size() >= 12 && std::memcmp(bytes.data(), "FORM", 4) == 0;
}

std::string IffReader::cstring() {
    const auto begin = data_.begin() + pos_;
    const auto end = std::find(begin, data_.end(), uint8_t(0));
    if (end == data_.end()) {
        throw std::runtime_error("IFF: unterminated string");
    }
    std::string out(begin, end);
    pos_ += out.size() + 1;
    return out;
}

void IffReader::require(size_t size) const {
    if (size > remaining()) {
        throw std::runtime_error("IFF: read of " + std::to_string(size) + " bytes past end of chunk");
    }
}

} // namespace swm::swg
