#pragma once

#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "bf2/animation.h"
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
    // Adds only a matching `lvl_` sublevel (e.g. a planet sky inside spa_sky.lvl).
    void add_sublevel(std::vector<uint8_t> bytes, uint32_t sublevel_hash);

    // A model by name, or through a "gmod" of that name (its first LOD).
    const UcfbNode* model(std::string_view name) const;
    const UcfbNode* skeleton(std::string_view model) const;
    const UcfbNode* texture(std::string_view name) const;
    // A model's collision mesh ("coll") and primitives ("prim"), or nullptr.
    const UcfbNode* collision_mesh(std::string_view model) const;
    const UcfbNode* collision_primitives(std::string_view model) const;
    // An animation bank's clips ("zaa_" by name, e.g. a door's
    // AnimationName), or nullptr.
    const UcfbNode* animation_bank(std::string_view name) const;
    const OdfLibrary& classes() const { return classes_; }

private:
    void index(const UcfbNode& node);

    std::deque<std::vector<uint8_t>> files_;
    std::deque<UcfbNode> roots_;
    std::map<std::string, const UcfbNode*> models_, skeletons_, textures_, meshes_, primitives_, banks_;  // lowercase names
    std::map<std::string, std::string> lod_names_;  // gmod -> its first LOD's model
    OdfLibrary classes_;
};

// What export_model wrote.
struct ModelExport {
    int segments = 0;  // segments written (one glTF primitive each)
    int skipped = 0;   // segments without readable vertices (skinned or compressed only)
    std::vector<std::string> missing_textures;
    // The box round what was written, in game space (meaningful without
    // clips: with them, segments are in their bones' spaces).
    std::array<float, 3> min{1e9f, 1e9f, 1e9f};
    std::array<float, 3> max{-1e9f, -1e9f, -1e9f};
};

// A clip to write with a model (export_model), under its name.
struct ModelClip {
    std::string name;
    Animation animation;
};

// Writes model `name` as `<model_dir>/<name>.gltf` (and .bin) in game space:
// x mirrored, triangles turned to face their normals. Each segment is a
// primitive with its own material: its diffuse texture, alpha-tested
// ("hardedged") or blended ("transparent"), double-sided, and its vertex
// colors if any; unlit when it glows or its colors are baked lighting. Diffuse textures are
// written once each, as `<texture_dir>/<name>.png` (lowercase), and listed in
// `textures_written`. With `clips` (a door's opening), each skeleton bone is
// a node of its own holding the segments that hang from it, and each clip is
// a glTF animation moving those nodes; without, one mesh in model space.
// Throws std::runtime_error if there's no such model.
ModelExport export_model(const AssetLibrary& library, std::string_view name, const std::filesystem::path& model_dir,
                         const std::filesystem::path& texture_dir, std::set<std::string>& textures_written,
                         const std::vector<ModelClip>& clips = {},
                         const std::map<std::string, std::string>& bone_rename = {},
                         std::string_view out_name = {});

// A model's collision, in its model space (Battlefront coordinates): its
// collision mesh, and its primitives as triangles (boxes, 12-sided
// cylinders, spheres). ASSUMPTION: the mesh stops soldiers, ordnance and
// vehicles; a primitive stops what its name says (CollisionPrimitive::stops),
// and one stopping none of them is left out.
struct CollisionTriangle {
    std::array<float, 3> a, b, c;
    bool soldiers = true;
    bool ordnance = true;
    bool vehicles = true;
};
// What soldiers, ordnance and vehicles meet, as a class lists it
// (SoldierCollision, OrdnanceCollision, VehicleCollision: "collisionmesh" or
// primitives' names, lowercase; "CLEAR" names nothing); a kind its class
// doesn't list meets the mesh and the primitives whose flags stop it.
// Naboo's buildings list their boxes for vehicles only, and the mesh for
// soldiers and ordnance: floors inside the boxes are walked on. A capital
// ship's hangar shield is VehicleCollision CLEAR: starfighters fly through.
struct CollisionUse {
    std::optional<std::vector<std::string>> soldiers, ordnance, vehicles;
};
CollisionUse collision_use(const OdfClass& resolved);
std::vector<CollisionTriangle> model_collision(const AssetLibrary& library, std::string_view model,
                                               const CollisionUse& use = {});

// Writes a texture as a PNG. False if the library doesn't have it or no
// stored format can be decoded.
bool export_texture(const AssetLibrary& library, std::string_view name, const std::filesystem::path& path);

// Lowercase, for file names and lookups.
std::string lowercase(std::string_view text);

} // namespace swm::bf2
