#include "bf2/level.h"

#include <algorithm>
#include <cctype>
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
    if (info == nullptr && patches == nullptr) {
        return terrain;  // none: a map aboard a ship (Tantive IV's), cells 0
    }
    if (info == nullptr || patches == nullptr) {
        throw std::runtime_error("bf2 level: terrain without INFO or PCHS");
    }
    terrain.unit = read<float>(info->data, 0);
    terrain.cells = read<uint16_t>(info->data, 16);
    const int patch_cells = read<uint16_t>(info->data, 18);
    if (terrain.cells <= 0 || patch_cells <= 0 || terrain.cells % patch_cells != 0 || terrain.unit <= 0) {
        throw std::runtime_error("bf2 level: bad terrain size");
    }
    // Cutters: INFO is the plane count, then the bounding sphere's radius and
    // center; PLNS the planes.
    if (const UcfbNode* cuts = tern.child("CUTS")) {
        for (const UcfbNode& cutter : cuts->children) {
            const UcfbNode* cut_info = cutter.child("INFO");
            const UcfbNode* planes = cutter.child("PLNS");
            if (cutter.tag != "CUTR" || cut_info == nullptr || planes == nullptr || cut_info->data.size() < 20) {
                continue;
            }
            TerrainCut cut;
            const uint32_t count = read<uint32_t>(cut_info->data, 0);
            cut.radius = read<float>(cut_info->data, 4);
            cut.center = {read<float>(cut_info->data, 8), read<float>(cut_info->data, 12), read<float>(cut_info->data, 16)};
            for (uint32_t i = 0; i < count && (i + 1) * 16 <= planes->data.size(); ++i) {
                cut.planes.push_back({read<float>(planes->data, i * 16), read<float>(planes->data, i * 16 + 4),
                                      read<float>(planes->data, i * 16 + 8), read<float>(planes->data, i * 16 + 12)});
            }
            terrain.cuts.push_back(std::move(cut));
        }
    }
    if (const UcfbNode* layers = tern.child("LTEX")) {
        for (size_t at = 0; at < layers->data.size();) {
            const std::string name = text(layers->data.subspan(at));
            at += name.size() + 1;
            terrain.texture_layers.push_back(name);
        }
        while (!terrain.texture_layers.empty() && terrain.texture_layers.back().empty()) {
            terrain.texture_layers.pop_back();
        }
    }
    if (const UcfbNode* scales = tern.child("SCAL")) {
        for (size_t i = 0; i < terrain.texture_layers.size() && (i + 1) * 4 <= scales->data.size(); ++i) {
            terrain.texture_scales.push_back(read<float>(scales->data, i * 4));
        }
    }
    terrain.texture_scales.resize(terrain.texture_layers.size(), 1.0f);
    if (const UcfbNode* detail = tern.child("DTLX")) {
        terrain.detail_texture = text(detail->data);
    }

    const int points = terrain.cells + 1;
    const int per_row = terrain.cells / patch_cells;
    const size_t point_count = size_t(points) * points;
    terrain.heights.assign(point_count, NAN);
    terrain.layer_weights.assign(terrain.texture_layers.size(), std::vector<uint8_t>(point_count, 0));
    terrain.light.assign(point_count, {255, 255, 255});
    std::vector<bool> weighted(point_count, false);
    int index = 0;
    for (const UcfbNode& patch : patches->children) {
        if (patch.tag != "PTCH") {
            continue;
        }
        const int px = index % per_row, pz = index / per_row;
        ++index;
        // INFO: the number of layers used, then their slots.
        std::vector<size_t> slots;
        if (const UcfbNode* patch_info = patch.child("INFO"); patch_info && !patch_info->data.empty()) {
            const size_t used = std::min<size_t>(patch_info->data[0], 3);
            for (size_t i = 0; i < used && 1 + i < patch_info->data.size(); ++i) {
                slots.push_back(patch_info->data[1 + i]);
            }
        }
        // The geometry buffer (28-byte vertices: float position local to the
        // patch, float normal, D3D color = baked light) and its compressed
        // copy (16 bytes: int16 position and w, normal, color), whose spare
        // bytes hold the layers' weights: color alpha for the first layer,
        // normal w for the second, position w for the third. (Found by
        // matching shared edge vertices between patches.)
        const UcfbNode* geometry = nullptr;
        const UcfbNode* compressed = nullptr;
        for (const UcfbNode& buffer : patch.children) {
            if (buffer.tag == "VBUF" && read<uint32_t>(buffer.data, 4) == 28 && !geometry) {
                geometry = &buffer;
            } else if (buffer.tag == "VBUF" && read<uint32_t>(buffer.data, 4) == 16 && !compressed) {
                compressed = &buffer;
            }
        }
        if (geometry == nullptr) {
            continue;
        }
        const uint32_t count = read<uint32_t>(geometry->data, 0);
        if (compressed != nullptr && read<uint32_t>(compressed->data, 0) != count) {
            compressed = nullptr;
        }
        for (uint32_t i = 0; i < count; ++i) {
            const size_t at = 12 + size_t(i) * 28;
            const float x = read<float>(geometry->data, at) / float(terrain.unit);
            const float y = read<float>(geometry->data, at + 4);
            const float z = read<float>(geometry->data, at + 8) / float(terrain.unit);
            // Vertices off the grid (along cuts) are skipped.
            if (std::abs(x - std::round(x)) > 1e-3f || std::abs(z - std::round(z)) > 1e-3f) {
                continue;
            }
            const int gx = px * patch_cells + int(std::lround(x));
            const int gz = pz * patch_cells + int(std::lround(z));
            if (gx < 0 || gx >= points || gz < 0 || gz >= points) {
                continue;
            }
            const size_t point = size_t(gz) * points + size_t(gx);
            terrain.heights[point] = y;
            terrain.light[point] = {read<uint8_t>(geometry->data, at + 26), read<uint8_t>(geometry->data, at + 25),
                                    read<uint8_t>(geometry->data, at + 24)};
            if (compressed != nullptr && !slots.empty() && !weighted[point]) {
                const size_t c = 12 + size_t(i) * 16;
                const uint8_t weights[3] = {read<uint8_t>(compressed->data, c + 15),
                                            read<uint8_t>(compressed->data, c + 11),
                                            read<uint8_t>(compressed->data, c + 6)};
                for (size_t slot = 0; slot < slots.size(); ++slot) {
                    if (slots[slot] < terrain.layer_weights.size()) {
                        terrain.layer_weights[slots[slot]][point] = weights[slot];
                    }
                }
                weighted[point] = true;
            }
        }
    }
    // Points no patch weighted get the first layer.
    if (!terrain.layer_weights.empty()) {
        for (size_t point = 0; point < point_count; ++point) {
            if (!weighted[point]) {
                terrain.layer_weights[0][point] = 255;
            }
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

// A "BARR": INFO {NAME, XFRM, SIZE (half extents x, y, z), FLAG}.
LevelBarrier read_barrier(const UcfbNode& node, const std::string& layer) {
    LevelBarrier barrier;
    barrier.layer = layer;
    const UcfbNode* info = node.child("INFO");
    const UcfbNode* xfrm = info ? info->child("XFRM") : nullptr;
    const UcfbNode* size = info ? info->child("SIZE") : nullptr;
    if (xfrm == nullptr || size == nullptr || xfrm->data.size() < 48 || size->data.size() < 12) {
        throw std::runtime_error("bf2 level: malformed BARR");
    }
    barrier.name = child_text(*info, "NAME");
    for (size_t i = 0; i < 9; ++i) {
        barrier.rotation[i] = read<float>(xfrm->data, i * 4);
    }
    for (size_t i = 0; i < 3; ++i) {
        barrier.position[i] = read<float>(xfrm->data, 36 + i * 4);
    }
    barrier.half_size = {read<float>(size->data, 0), read<float>(size->data, 8)};
    if (const UcfbNode* flag = info->child("FLAG"); flag && flag->data.size() >= 4) {
        barrier.flags = read<uint32_t>(flag->data, 0);
    }
    return barrier;
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

// A "bnd_" chunk lists a group's boundaries as config records: Boundary,
// then a scope of "Path" records, each the hash of a path's name (stored
// in a number's place).
void read_boundaries(const UcfbNode& chunk, std::vector<std::pair<uint32_t, uint32_t>>& boundary_paths) {
    const UcfbNode* name = chunk.child("NAME");
    const uint32_t group = name && name->data.size() >= 4 ? read<uint32_t>(name->data, 0) : 0;
    const uint32_t path_hash = odf_hash("Path");
    for (const UcfbNode& scope : chunk.children) {
        if (scope.tag != "SCOP") {
            continue;
        }
        for (const UcfbNode& field : scope.children) {
            if (field.tag != "DATA") {
                continue;
            }
            const ConfigRecord record = read_config_record(field.data);
            if (record.name == path_hash && record.numbers.size() == 1) {
                // The bits as stored (past the name and count), not as a float.
                boundary_paths.push_back({group, read<uint32_t>(field.data, 5)});
            }
        }
    }
}

// Calls `visit(record, scope)` for each record among a config chunk's (or
// scope's) children; `scope` is the SCOP right after it, or nullptr.
template <class Visitor>
void for_each_record(const UcfbNode& node, Visitor&& visit) {
    for (size_t i = 0; i < node.children.size(); ++i) {
        if (node.children[i].tag != "DATA") {
            continue;
        }
        const UcfbNode* scope =
            i + 1 < node.children.size() && node.children[i + 1].tag == "SCOP" ? &node.children[i + 1] : nullptr;
        visit(read_config_record(node.children[i].data), scope);
    }
}

std::array<float, 3> first3(const std::vector<float>& numbers) {
    std::array<float, 3> out{};
    for (size_t i = 0; i < 3 && i < numbers.size(); ++i) {
        out[i] = numbers[i];
    }
    return out;
}

// "sky_": a sky-info scope (fog color, view ranges) and DomeInfo (the dome's
// texture, and DomeModel scopes naming its geometry).
void read_sky(const UcfbNode& chunk, LevelSky& sky) {
    const uint32_t fog_color = odf_hash("FogColor"), near_range = odf_hash("NearSceneRange");
    const uint32_t dome_info = odf_hash("DomeInfo"), texture = odf_hash("Texture");
    const uint32_t dome_model = odf_hash("DomeModel"), geometry = odf_hash("Geometry");
    for_each_record(chunk, [&](const ConfigRecord& section, const UcfbNode* scope) {
        if (scope == nullptr) {
            return;
        }
        for_each_record(*scope, [&](const ConfigRecord& record, const UcfbNode* inner) {
            if (record.name == fog_color) {
                sky.fog_color = first3(record.numbers);
            } else if (record.name == near_range) {
                sky.near_scene_range = record.numbers;
            } else if (section.name == dome_info && record.name == texture) {
                sky.dome_texture = record.text;
            } else if (section.name == dome_info && record.name == dome_model && inner != nullptr) {
                for_each_record(*inner, [&](const ConfigRecord& model, const UcfbNode*) {
                    if (model.name == geometry && !model.text.empty()) {
                        sky.dome_models.push_back(model.text);
                    }
                });
            }
        });
    });
}

// "lght": Light("name", ...) scopes (Type, Rotation, Position, Color, Range)
// and GlobalLights (the sun's name, the ambient colors).
void read_lights(const UcfbNode& chunk, Level& level) {
    const uint32_t light = odf_hash("Light"), global = odf_hash("GlobalLights");
    const uint32_t type = odf_hash("Type"), rotation = odf_hash("Rotation"), position = odf_hash("Position");
    const uint32_t color = odf_hash("Color"), range = odf_hash("Range"), baked = odf_hash("Static");
    const uint32_t light1 = odf_hash("Light1"), top = odf_hash("Top"), bottom = odf_hash("Bottom");
    for_each_record(chunk, [&](const ConfigRecord& section, const UcfbNode* scope) {
        if (scope == nullptr) {
            return;
        }
        if (section.name == light) {
            LevelLight l;
            l.name = section.text;
            for_each_record(*scope, [&](const ConfigRecord& r, const UcfbNode*) {
                if (r.name == type && !r.numbers.empty()) {
                    l.type = int(r.numbers[0]);
                } else if (r.name == rotation && r.numbers.size() >= 4) {
                    l.rotation = {r.numbers[0], r.numbers[1], r.numbers[2], r.numbers[3]};
                } else if (r.name == position) {
                    // Like paths, lights store z the other way around (the
                    // lamps' lights sit on the lamp props with z negated).
                    l.position = first3(r.numbers);
                    l.position[2] = -l.position[2];
                } else if (r.name == color) {
                    l.color = first3(r.numbers);
                } else if (r.name == range && !r.numbers.empty()) {
                    l.range = r.numbers[0];
                } else if (r.name == baked) {
                    l.baked = true;
                }
            });
            level.lights.push_back(l);
        } else if (section.name == global) {
            for_each_record(*scope, [&](const ConfigRecord& r, const UcfbNode*) {
                if (r.name == light1) {
                    level.sky.sun = r.text;
                } else if (r.name == top) {
                    level.sky.ambient_top = first3(r.numbers);
                } else if (r.name == bottom) {
                    level.sky.ambient_bottom = first3(r.numbers);
                }
            });
        }
    });
}

// "WATR" (in the terrain): INFO {cells per side, an unknown 2, cell size,
// half the grid's extent along x and z, an unknown 1}; a LAYR per layer {name,
// an unknown 10 on every map, height, U and V velocity, U and V repeat, RGBA
// color}; WMAP, 4 bits per cell, two cells a byte, the first in the high
// nibble. (The grid's size, centring and nibble order were found by matching
// Dagobah's water cells to its swamp pools; heights match other maps' water,
// e.g. Yavin's -72.5 over terrain from -74.5.) ASSUMPTION: a cell's low three
// bits are its layer number (nibbles of 9 occur on maps with one layer).
LevelWater read_water(const UcfbNode& watr) {
    LevelWater water;
    const UcfbNode* info = watr.child("INFO");
    const UcfbNode* cells = watr.child("WMAP");
    if (info == nullptr || cells == nullptr) {
        return water;
    }
    const int count = int(read<uint32_t>(info->data, 0));
    const float size = read<float>(info->data, 8);
    if (count <= 0 || count > 4096 || !(size > 0) || cells->data.size() < size_t(count) * size_t(count) / 2) {
        throw std::runtime_error("bf2 level: bad water map");
    }
    water.cells = count;
    water.cell_size = size;
    water.origin = {-read<float>(info->data, 12), -read<float>(info->data, 16)};
    for (const UcfbNode& layer : watr.children) {
        if (layer.tag != "LAYR") {
            continue;
        }
        WaterLayer out;
        out.name = text(layer.data);
        const size_t at = out.name.size() + 1;
        out.height = read<float>(layer.data, at + 4);
        out.velocity = {read<float>(layer.data, at + 8), read<float>(layer.data, at + 12)};
        out.repeat = {read<float>(layer.data, at + 16), read<float>(layer.data, at + 20)};
        for (size_t i = 0; i < 4; ++i) {
            out.color[i] = read<uint8_t>(layer.data, at + 24 + i);
        }
        water.layers.push_back(out);
    }
    water.map.resize(size_t(count) * size_t(count));
    for (size_t i = 0; i < water.map.size(); ++i) {
        const uint8_t pair = cells->data[i / 2];
        water.map[i] = uint8_t(((i % 2 == 0) ? pair >> 4 : pair) & 7);
    }
    return water;
}

// "fx__": Effect("name") records, each followed by a scope of properties.
// (Particle effects, also "fx__", are named by other records and skipped.)
void read_effects(const UcfbNode& chunk, std::vector<LevelEffect>& effects) {
    const uint32_t effect = odf_hash("Effect");
    for_each_record(chunk, [&](const ConfigRecord& record, const UcfbNode* scope) {
        if (record.name != effect || scope == nullptr) {
            return;
        }
        LevelEffect out{record.text, {}};
        for_each_record(*scope, [&](const ConfigRecord& property, const UcfbNode*) {
            out.properties.push_back(property);
        });
        effects.push_back(std::move(out));
    });
}

// `group` is the enclosing sublevel's name hash (0 at the top: a base
// layer's paths are grouped under its own name's hash).
void read_tree(const UcfbNode& node, Level& level, uint32_t group) {
    for (const UcfbNode& child : node.children) {
        if (child.tag == "lvl_") {
            level.sublevels.push_back({child.name_hash, {}});
            read_tree(child, level, child.name_hash);
        } else if (child.tag == "tern" && level.terrain.cells == 0) {
            level.terrain = read_terrain(child);
            if (const UcfbNode* watr = child.child("WATR")) {
                level.water = read_water(*watr);
            }
        } else if (child.tag == "fx__") {
            read_effects(child, level.effects);
        } else if (child.tag == "wrld") {
            const std::string layer = child.name();
            level.layers.push_back(layer);
            level.layer_groups[layer] = group != 0 ? group : odf_hash(layer);
            if (group != 0 && !level.sublevels.empty() && level.sublevels.back().first == group) {
                level.sublevels.back().second.push_back(layer);
            }
            for (const UcfbNode& item : child.children) {
                if (item.tag == "inst") {
                    level.objects.push_back(read_item(item, layer));
                    level.objects.back().sublevel = group;
                } else if (item.tag == "regn") {
                    level.regions.push_back(read_item(item, layer));
                    level.regions.back().sublevel = group;
                } else if (item.tag == "BARR") {
                    level.barriers.push_back(read_barrier(item, layer));
                    level.barriers.back().sublevel = group;
                }
            }
        } else if (child.tag == "path") {
            read_paths(child, level.paths);
        } else if (child.tag == "bnd_") {
            read_boundaries(child, level.boundary_paths);
        } else if (child.tag == "sky_") {
            read_sky(child, level.sky);
        } else if (child.tag == "lght") {
            read_lights(child, level);
        }
    }
}

} // namespace

std::array<std::array<float, 2>, 4> LevelBarrier::corners() const {
    // The local x axis is the rotation's first column, z its third.
    const auto& r = rotation;
    std::array<std::array<float, 2>, 4> out{};
    const float signs[4][2] = {{1, 1}, {1, -1}, {-1, -1}, {-1, 1}};
    for (size_t i = 0; i < 4; ++i) {
        const float a = signs[i][0] * half_size[0], b = signs[i][1] * half_size[1];
        out[i] = {position[0] + a * r[0] + b * r[6], position[2] + a * r[2] + b * r[8]};
    }
    return out;
}

const WaterLayer* LevelWater::at(int column, int row) const {
    if (column < 0 || row < 0 || column >= cells || row >= cells) {
        return nullptr;
    }
    const uint8_t layer = map[size_t(row) * size_t(cells) + size_t(column)];
    if (layer == 0 || layers.empty()) {
        return nullptr;
    }
    return &layers[std::min<size_t>(layer, layers.size()) - 1];
}

const ConfigRecord* LevelEffect::get(std::string_view property) const {
    const uint32_t hash = odf_hash(property);
    for (const ConfigRecord& record : properties) {
        if (record.name == hash) {
            return &record;
        }
    }
    return nullptr;
}

const LevelEffect* Level::effect(std::string_view name) const {
    for (const LevelEffect& e : effects) {
        if (e.name.size() == name.size() &&
            std::equal(e.name.begin(), e.name.end(), name.begin(),
                       [](char a, char b) { return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b)); })) {
            return &e;
        }
    }
    return nullptr;
}

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

const WorldItem* Level::region(std::string_view name, uint32_t sublevel) const {
    const auto same = [](std::string_view a, std::string_view b) {
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(x) == std::tolower(y); });
    };
    const WorldItem* any = nullptr;
    for (const WorldItem& r : regions) {
        if (!name.empty() && (same(r.name, name) || same(r.get("Name"), name))) {
            if (r.sublevel == sublevel) {
                return &r;
            }
            any = any ? any : &r;
        }
    }
    return any;
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

const LevelPath* Level::path_in(std::string_view name, uint32_t group) const {
    const LevelPath* any = nullptr;
    const uint32_t hash = odf_hash(name);  // ignores case
    for (const LevelPath& p : paths) {
        if (!name.empty() && odf_hash(p.name) == hash) {
            if (p.group == group) {
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
    // A string first: its offset is just past the values, where its length
    // and text are. (No float stores those bit patterns: they're denormals.)
    size_t first_number = 0;
    const size_t string_at = size_t(count) * 4;
    if (count >= 1 && values.size() > string_at + 4 && read<uint32_t>(values, 0) == string_at) {
        const uint32_t length = read<uint32_t>(values, string_at);
        if (string_at + 4 + size_t(length) <= values.size()) {
            record.text = text(values.subspan(string_at + 4, length));
            first_number = 1;
        }
    }
    for (size_t i = first_number; i < count && (i + 1) * 4 <= values.size(); ++i) {
        record.numbers.push_back(read<float>(values, i * 4));
    }
    return record;
}

Level read_level(const UcfbNode& root) {
    Level level;
    read_tree(root, level, 0);
    if (level.terrain.cells == 0 && level.terrain.name.empty()) {  // a ship's has a name, no grid
        throw std::runtime_error("bf2 level: no terrain (tern) chunk");
    }
    return level;
}

} // namespace swm::bf2
