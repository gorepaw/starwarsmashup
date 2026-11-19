#pragma once

#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "bf2/model.h"
#include "bf2/odf.h"
#include "bf2/ucfb.h"

namespace swm::bf2 {

// Models, skeletons, textures and object classes gathered from several
// munged files: a map's .lvl first, then ingame.lvl for the props every map
// shares (command posts, recharge droids). The first file holding a name
// wins. Names are case-insensitive, as in Battlefront.
class AssetLibrary {
public:
    // Parses and keeps a file's bytes. Throws std::runtime_error if it isn't
    // a ucfb tree.
    void add(std::vector<uint8_t> bytes);

    // A model by name, or through a "gmod" of that name (its first LOD).
    const UcfbNode* model(std::string_view name) const;
    const UcfbNode* skeleton(std::string_view model) const;
    const UcfbNode* texture(std::string_view name) const;
    // A model's collision mesh ("coll") and primitives ("prim"), or nullptr.
    const UcfbNode* collision_mesh(std::string_view model) const;
    const UcfbNode* collision_primitives(std::string_view model) const;
    const OdfLibrary& classes() const { return classes_; }

private:
    void index(const UcfbNode& node);

    std::deque<std::vector<uint8_t>> files_;
    std::deque<UcfbNode> roots_;
    std::map<std::string, const UcfbNode*> models_, skeletons_, textures_, meshes_, primitives_;  // lowercase names
    std::map<std::string, std::string> lod_names_;  // gmod -> its first LOD's model
    OdfLibrary classes_;
};

// What export_model wrote.
struct ModelExport {
    int segments = 0;  // segments written (one glTF primitive each)
    int skipped = 0;   // segments without readable vertices (skinned or compressed only)
    std::vector<std::string> missing_textures;
};

// Writes model `name` as `<model_dir>/<name>.gltf` (and .bin) in game space:
// x mirrored, triangles turned to face their normals. Each segment is a
// primitive with its own material: its diffuse texture, alpha-tested
// ("hardedged") or blended ("transparent"), double-sided, and its vertex
// colors if any; unlit when it glows or its colors are baked lighting. Diffuse textures are
// written once each, as `<texture_dir>/<name>.png` (lowercase), and listed in
// `textures_written`. Throws std::runtime_error if there's no such model.
ModelExport export_model(const AssetLibrary& library, std::string_view name, const std::filesystem::path& model_dir,
                         const std::filesystem::path& texture_dir, std::set<std::string>& textures_written);

// A model's collision, in its model space (Battlefront coordinates): its
// collision mesh, and its primitives as triangles (boxes, 12-sided
// cylinders, spheres). ASSUMPTION: the mesh stops soldiers and ordnance;
// a primitive stops what its name says (CollisionPrimitive::stops), and one
// stopping neither is left out.
struct CollisionTriangle {
    std::array<float, 3> a, b, c;
    bool soldiers = true;
    bool ordnance = true;
};
std::vector<CollisionTriangle> model_collision(const AssetLibrary& library, std::string_view model);

// Writes a texture as a PNG. False if the library doesn't have it or no
// stored format can be decoded.
bool export_texture(const AssetLibrary& library, std::string_view name, const std::filesystem::path& path);

// Lowercase, for file names and lookups.
std::string lowercase(std::string_view text);

} // namespace swm::bf2
