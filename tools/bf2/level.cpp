#include "bf2/level.h"

#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>

#include "bf2/odf.h"

namespace swm::bf2 {

namespace {

template <typename T>
T read(std::span<const uint8_t> data, size_t offset) {
    if (offset + sizeof(T) > data.size()) {
        throw std::runtime_error("bf2 level: read past the end of a chunk");
    }
    T value;
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

std::string text(std::span<const uint8_t> data) {
    const auto* begin = reinterpret_cast<const char*>(data.data());
    return std::string(begin, strnlen(begin, data.size()));
}

std::string child_text(const UcfbNode& node, std::string_view tag) {
    const UcfbNode* child = node.child(tag);
    return child ? text(child->data) : std::string();
}

// Terrain ("tern"): INFO is the grid unit, height scale, min and max height
// (floats), then the grid size and the patch size in cells (uint16s). Patches
// ("PCHS"/"PTCH", row-major from the -X,-Z corner) hold vertex buffers; the
// one with a 28-byte stride has float positions local to the patch.
Terrain read_terrain(const UcfbNode& tern) {
    Terrain terrain;
    terrain.name = tern.name();
    const UcfbNode* info = tern.child("INFO");
    const UcfbNode* patches = tern.child("PCHS");
    if (info == nullptr || patches == nullptr) {
        throw std::runtime_error("bf2 level: terrain without INFO or PCHS");
    }
    terrain.unit = read<float>(info->data, 0);
    terrain.cells = read<uint16_t>(info->data, 16);
    const int patch_cells = read<uint16_t>(info->data, 18);
    if (terrain.cells <= 0 || patch_cells <= 0 || terrain.cells % patch_cells != 0 || terrain.unit <= 0) {
        throw std::runtime_error("bf2 level: bad terrain size");
    }
    if (const UcfbNode* layers = tern.child("LTEX")) {
        for (size_t at = 0; at < layers->data.size();) {
            const std::string name = text(layers->data.subspan(at));
            at += name.size() + 1;
            if (!name.empty()) {
                terrain.texture_layers.push_back(name);
            }
        }
    }

    const int points = terrain.cells + 1;
    const int per_row = terrain.cells / patch_cells;
    terrain.heights.assign(size_t(points) * points, NAN);
    int index = 0;
    for (const UcfbNode& patch : patches->children) {
        if (patch.tag != "PTCH") {
            continue;
        }
        const int px = index % per_row, pz = index / per_row;
        ++index;
        for (const UcfbNode& buffer : patch.children) {
            if (buffer.tag != "VBUF" || read<uint32_t>(buffer.data, 4) != 28) {
                continue;
            }
            const uint32_t count = read<uint32_t>(buffer.data, 0);
            for (uint32_t i = 0; i < count; ++i) {
                const size_t at = 12 + size_t(i) * 28;
                const float x = read<float>(buffer.data, at) / float(terrain.unit);
                const float y = read<float>(buffer.data, at + 4);
                const float z = read<float>(buffer.data, at + 8) / float(terrain.unit);
                // Vertices off the grid (along cuts) are skipped.
                if (std::abs(x - std::round(x)) > 1e-3f || std::abs(z - std::round(z)) > 1e-3f) {
                    continue;
                }
                const int gx = px * patch_cells + int(std::lround(x));
                const int gz = pz * patch_cells + int(std::lround(z));
                if (gx >= 0 && gx < points && gz >= 0 && gz < points) {
                    terrain.heights[size_t(gz) * points + gx] = y;
                }
            }
            break;
        }
    }
    // Holes: take the mean of known neighbours (repeating outward).
    for (float& h : terrain.heights) {
        terrain.missing += std::isnan(h) ? 1 : 0;
    }
    for (int pass = 0; pass < points && terrain.missing > 0; ++pass) {
        std::vector<float> next = terrain.heights;
        for (int z = 0; z < points; ++z) {
            for (int x = 0; x < points; ++x) {
                if (!std::isnan(terrain.heights[size_t(z) * points + x])) {
                    continue;
                }
                double sum = 0;
                int known = 0;
                for (const auto [dx, dz] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
                    const int nx = x + dx, nz = z + dz;
                    if (nx >= 0 && nx < points && nz >= 0 && nz < points &&
                        !std::isnan(terrain.heights[size_t(nz) * points + nx])) {
                        sum += terrain.heights[size_t(nz) * points + nx];
                        ++known;
                    }
                }
                if (known > 0) {
                    next[size_t(z) * points + x] = float(sum / known);
                }
            }
        }
        terrain.heights = std::move(next);
        bool done = true;
        for (const float h : terrain.heights) {
            done = done && !std::isnan(h);
        }
        if (done) {
            break;
        }
    }
    for (float& h : terrain.heights) {
        h = std::isnan(h) ? 0.0f : h;
    }
    return terrain;
}

// An "inst" or "regn": INFO {TYPE, NAME, XFRM (3x3 rotation, then position),
// SIZE (regions)}, then PROP chunks (a name hash and a string).
WorldItem read_item(const UcfbNode& node, const std::string& layer) {
    WorldItem item;
    item.layer = layer;
    const UcfbNode* info = node.child("INFO");
    if (info == nullptr) {
        throw std::runtime_error("bf2 level: " + node.tag + " without INFO");
    }
    item.type = child_text(*info, "TYPE");
    item.name = child_text(*info, "NAME");
    if (const UcfbNode* xfrm = info->child("XFRM")) {
        for (size_t i = 0; i < 9; ++i) {
            item.rotation[i] = read<float>(xfrm->data, i * 4);
        }
        for (size_t i = 0; i < 3; ++i) {
            item.position[i] = read<float>(xfrm->data, 36 + i * 4);
        }
    }
    if (const UcfbNode* size = info->child("SIZE")) {
        for (size_t i = 0; i < 3; ++i) {
            item.size[i] = read<float>(size->data, i * 4);
        }
    }
    for (const UcfbNode& child : node.children) {
        if (child.tag == "PROP" && child.data.size() >= 4) {
            item.properties.emplace_back(read<uint32_t>(child.data, 0), text(child.data.subspan(4)));
        }
    }
    return item;
}

// A "path" chunk holds a layer's paths as config records: Path = <name>,
// then a scope whose "Nodes" scope has one "Node" scope per point
// (Position, Rotation, ...). Positions are converted to the world layers'
// coordinates.
void read_paths(const UcfbNode& chunk, std::vector<LevelPath>& paths) {
    const UcfbNode* name = chunk.child("NAME");
    const uint32_t group = name && name->data.size() >= 4 ? read<uint32_t>(name->data, 0) : 0;
    const uint32_t path_hash = odf_hash("Path");
    const uint32_t nodes_hash = odf_hash("Nodes");
    const uint32_t position_hash = odf_hash("Position");
    const uint32_t rotation_hash = odf_hash("Rotation");
    for (size_t i = 0; i < chunk.children.size(); ++i) {
        const UcfbNode& node = chunk.children[i];
        if (node.tag != "DATA" || i + 1 >= chunk.children.size() || chunk.children[i + 1].tag != "SCOP") {
            continue;
        }
        const ConfigRecord record = read_config_record(node.data);
        if (record.name != path_hash) {
            continue;
        }
        LevelPath path{record.text, group, {}};
        const UcfbNode& scope = chunk.children[i + 1];
        for (size_t j = 0; j + 1 < scope.children.size(); ++j) {
            if (scope.children[j].tag != "DATA" || read_config_record(scope.children[j].data).name != nodes_hash) {
                continue;
            }
            for (const UcfbNode& node_scope : scope.children[j + 1].children) {
                if (node_scope.tag != "SCOP") {
                    continue;
                }
                PathNode point;
                for (const UcfbNode& field : node_scope.children) {
                    if (field.tag != "DATA") {
                        continue;
                    }
                    const ConfigRecord value = read_config_record(field.data);
                    if (value.name == position_hash && value.numbers.size() >= 3) {
                        // Paths store z the other way around from the world
                        // layers (found by matching command posts' spawn
                        // paths to their posts).
                        point.position = {value.numbers[0], value.numbers[1], -value.numbers[2]};
                    } else if (value.name == rotation_hash && value.numbers.size() >= 4) {
                        point.rotation = {value.numbers[0], value.numbers[1], value.numbers[2], value.numbers[3]};
                    }
                }
                path.nodes.push_back(point);
            }
            break;
        }
        paths.push_back(std::move(path));
    }
}

// `group` is the enclosing sublevel's name hash (0 at the top: a base
// layer's paths are grouped under its own name's hash).
void read_tree(const UcfbNode& node, Level& level, uint32_t group) {
    for (const UcfbNode& child : node.children) {
        if (child.tag == "lvl_") {
            read_tree(child, level, child.name_hash);
        } else if (child.tag == "tern" && level.terrain.cells == 0) {
            level.terrain = read_terrain(child);
        } else if (child.tag == "wrld") {
            const std::string layer = child.name();
            level.layers.push_back(layer);
            level.layer_groups[layer] = group != 0 ? group : odf_hash(layer);
            for (const UcfbNode& item : child.children) {
                if (item.tag == "inst") {
                    level.objects.push_back(read_item(item, layer));
                } else if (item.tag == "regn") {
                    level.regions.push_back(read_item(item, layer));
                }
            }
        } else if (child.tag == "path") {
            read_paths(child, level.paths);
        }
    }
}

} // namespace

std::string WorldItem::get(std::string_view property) const {
    const uint32_t hash = odf_hash(property);
    std::string value;
    for (const auto& [key, text_value] : properties) {
        if (key == hash) {
            value = text_value;
        }
    }
    return value;
}

const WorldItem* Level::region(std::string_view name) const {
    for (const WorldItem& r : regions) {
        if (r.name == name) {
            return &r;
        }
    }
    return nullptr;
}

const LevelPath* Level::path(std::string_view name, std::string_view layer) const {
    const auto group = layer_groups.find(std::string(layer));
    const LevelPath* any = nullptr;
    for (const LevelPath& p : paths) {
        if (p.name == name) {
            if (group != layer_groups.end() && p.group == group->second) {
                return &p;
            }
            any = any ? any : &p;
        }
    }
    return any;
}

ConfigRecord read_config_record(std::span<const uint8_t> data) {
    ConfigRecord record;
    record.name = read<uint32_t>(data, 0);
    const uint8_t count = read<uint8_t>(data, 4);
    const std::span<const uint8_t> values = data.subspan(5);
    // A string: offset 4 (to the text), then its length, then the text.
    if (count == 1 && values.size() > 8 && read<uint32_t>(values, 0) == 4) {
        const uint32_t length = read<uint32_t>(values, 4);
        if (8 + size_t(length) <= values.size()) {
            record.text = text(values.subspan(8, length));
            return record;
        }
    }
    for (size_t i = 0; i < count && (i + 1) * 4 <= values.size(); ++i) {
        record.numbers.push_back(read<float>(values, i * 4));
    }
    return record;
}

Level read_level(const UcfbNode& root) {
    Level level;
    read_tree(root, level, 0);
    if (level.terrain.cells == 0) {
        throw std::runtime_error("bf2 level: no terrain (tern) chunk");
    }
    return level;
}

} // namespace swm::bf2
