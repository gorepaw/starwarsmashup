#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace swm::bf2 {

// Battlefront II munged files (.lvl and friends) are "ucfb" chunk trees. Each
// chunk is a 4-character tag, a little-endian uint32 size, then `size` bytes,
// padded to a 4-byte boundary. Files start with a single "ucfb" root.
//
// The format doesn't mark which chunks contain children, so the parser treats
// a chunk's data as children when it parses cleanly as a chunk sequence that
// fills the data exactly, with every tag made of [A-Za-z0-9_ ]. "lvl_" chunks
// are special: their data starts with an 8-byte header (name hash, size).
struct UcfbNode {
    std::string tag;
    std::span<const uint8_t> data;  // whole payload (for "lvl_", after the 8-byte header)
    uint32_t name_hash = 0;         // "lvl_" only
    std::vector<UcfbNode> children; // empty if the payload isn't a chunk sequence

    // First direct child with this tag, or nullptr.
    const UcfbNode* child(std::string_view child_tag) const;

    // Contents of the first NAME child as a string, or empty.
    std::string name() const;
};

// Parses a whole file. Nodes view `bytes`, which must outlive them. Throws
// std::runtime_error if the file isn't a ucfb tree.
UcfbNode parse_ucfb(std::span<const uint8_t> bytes);

// Depth-first visit of `node` and all descendants.
template <class Visitor>
void visit_ucfb(const UcfbNode& node, Visitor&& visitor, int depth = 0) {
    visitor(node, depth);
    for (const UcfbNode& child : node.children) {
        visit_ucfb(child, visitor, depth + 1);
    }
}

} // namespace swm::bf2
