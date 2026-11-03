#include "bf2/ucfb.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace swm::bf2 {
namespace {

constexpr size_t chunk_header_size = 8;
constexpr int max_depth = 64;

uint32_t read_u32(const uint8_t* p) {
    uint32_t value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

bool is_tag_char(uint8_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ' ';
}

bool is_tag(const uint8_t* p) {
    return std::all_of(p, p + 4, is_tag_char);
}

size_t align4(size_t value) {
    return (value + 3) & ~size_t(3);
}

// True if `bytes` is exactly a sequence of well-formed chunks.
bool is_chunk_sequence(std::span<const uint8_t> bytes) {
    if (bytes.size() < chunk_header_size) {
        return false;
    }
    size_t pos = 0;
    while (pos < bytes.size()) {
        if (bytes.size() - pos < chunk_header_size || !is_tag(bytes.data() + pos)) {
            return false;
        }
        const uint32_t size = read_u32(bytes.data() + pos + 4);
        if (size > bytes.size() - pos - chunk_header_size) {
            return false;
        }
        // The last chunk may omit its padding.
        pos = std::min(bytes.size(), align4(pos + chunk_header_size + size));
    }
    return true;
}

std::vector<UcfbNode> parse_children(std::span<const uint8_t> bytes, int depth);

UcfbNode make_node(std::string tag, std::span<const uint8_t> data, int depth) {
    if (depth > max_depth) {
        throw std::runtime_error("ucfb: nesting deeper than " + std::to_string(max_depth));
    }
    UcfbNode node;
    node.tag = std::move(tag);
    node.data = data;
    if (node.tag == "lvl_") {
        if (data.size() < 8) {
            throw std::runtime_error("ucfb: lvl_ chunk too small for its header");
        }
        node.name_hash = read_u32(data.data());
        const uint32_t inner_size = read_u32(data.data() + 4);
        if (inner_size > data.size() - 8) {
            throw std::runtime_error("ucfb: lvl_ inner size overruns the chunk");
        }
        node.data = data.subspan(8, inner_size);
        node.children = parse_children(node.data, depth + 1);
    } else if (is_chunk_sequence(data)) {
        node.children = parse_children(data, depth + 1);
    }
    return node;
}

std::vector<UcfbNode> parse_children(std::span<const uint8_t> bytes, int depth) {
    std::vector<UcfbNode> nodes;
    size_t pos = 0;
    while (pos + chunk_header_size <= bytes.size()) {
        std::string tag(reinterpret_cast<const char*>(bytes.data() + pos), 4);
        const uint32_t size = read_u32(bytes.data() + pos + 4);
        if (size > bytes.size() - pos - chunk_header_size) {
            throw std::runtime_error("ucfb: chunk '" + tag + "' overruns its parent");
        }
        nodes.push_back(make_node(std::move(tag), bytes.subspan(pos + chunk_header_size, size), depth));
        pos = align4(pos + chunk_header_size + size);
    }
    return nodes;
}

} // namespace

const UcfbNode* UcfbNode::child(std::string_view child_tag) const {
    const auto it = std::find_if(children.begin(), children.end(),
                                 [&](const UcfbNode& node) { return node.tag == child_tag; });
    return it == children.end() ? nullptr : &*it;
}

std::string UcfbNode::name() const {
    const UcfbNode* name_node = child("NAME");
    if (!name_node) {
        return {};
    }
    const auto begin = name_node->data.begin();
    return std::string(begin, std::find(begin, name_node->data.end(), uint8_t(0)));
}

UcfbNode parse_ucfb(std::span<const uint8_t> bytes) {
    if (bytes.size() < chunk_header_size || std::memcmp(bytes.data(), "ucfb", 4) != 0) {
        throw std::runtime_error("not a ucfb file");
    }
    const uint32_t size = read_u32(bytes.data() + 4);
    if (size > bytes.size() - chunk_header_size) {
        throw std::runtime_error("ucfb: root size overruns the file");
    }
    UcfbNode root;
    root.tag = "ucfb";
    root.data = bytes.subspan(chunk_header_size, size);
    root.children = parse_children(root.data, 1);
    return root;
}

} // namespace swm::bf2
