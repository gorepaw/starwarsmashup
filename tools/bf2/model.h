#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bf2/ucfb.h"
#include "swg/image.h"

namespace swm::bf2 {

// Munged models ("modl"), their skeletons ("skel"), textures ("tex_") and
// collision ("coll", "prim"). Coordinates are Battlefront's, as stored
// (left-handed; game x = -x).
//
// Transforms (XFRM, here and in world layers) are a 3x3 rotation stored as
// its three columns, i.e. the local axes, then a position: p' = R p + t with
// R's columns as stored. (Found by fitting collision boxes to their models'
// geometry, the crashed X-wing to the ground and the cave to the terrain's
// cut holes; reading them as rows fits worse each time.)

// Material flags (MTRL), as Battlefront II's mod tools name them.
enum MaterialFlag : uint32_t {
    material_normal = 1u << 0,
    material_hardedged = 1u << 1,    // alpha-tested cutouts
    material_transparent = 1u << 2,  // alpha-blended
    material_glossmap = 1u << 3,
    material_glow = 1u << 4,
    material_bumpmap = 1u << 5,
    material_additive = 1u << 6,
    material_specular = 1u << 7,
    material_env_map = 1u << 8,
    material_vertex_lighting = 1u << 9,  // vertex colors are baked lighting
    material_doublesided = 1u << 16,
    material_scrolling = 1u << 24,
    material_energy = 1u << 25,
    material_animated = 1u << 26,
};

// A part of a model with one material: an indexed triangle list.
struct ModelSegment {
    std::string bone;             // the skeleton bone it hangs from (BNAM)
    uint32_t flags = 0;           // MaterialFlag bits
    std::array<uint8_t, 4> diffuse{255, 255, 255, 255};  // RGBA
    std::string render_type;      // RTYP, e.g. "Normal"
    std::array<std::string, 4> textures;  // diffuse, bump/normal map, detail, environment ("" if none)
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 2>> uvs;
    std::vector<std::array<uint8_t, 4>> colors;  // RGBA; empty if the vertices have none
    // A skinned segment's (a walker's legs): each vertex's bone, an index into
    // `bone_map`, which indexes the skeleton's bones; such a vertex is stored
    // in its bone's space. Empty for a rigid segment.
    std::vector<uint8_t> vertex_bones;
    std::vector<uint8_t> bone_map;  // BMAP
    bool baked_lighting = false;  // the colors are static lighting, not paint
    // Triangles from the stored strips (degenerates dropped), each wound so
    // its front faces the stored normals in Battlefront's coordinates when
    // read with the usual strip parity.
    std::vector<uint32_t> triangles;
};

struct Model {
    std::string name;
    std::vector<ModelSegment> segments;
};

// A 3x3 rotation as stored (three columns) and a position.
struct Transform {
    std::array<float, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<float, 3> position{};

    std::array<float, 3> apply(const std::array<float, 3>& p) const;
    Transform then(const Transform& outer) const;  // this, then `outer`
};

struct ModelBone {
    std::string name;
    std::string parent;  // "" for a root
    std::array<float, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};  // 3x3, columns as stored
    std::array<float, 3> position{};
};

struct Skeleton {
    std::string model;  // the model it belongs to
    std::vector<ModelBone> bones;

    // A bone's transform to model space (parents applied); identity for an
    // unknown bone.
    Transform model_transform(const std::string& bone) const;
};

// A model's collision mesh ("coll"): vertices, and an AABB tree whose leaves
// are convex polygons (fanned into triangles here). Its vertices are in the
// space of `bone`.
struct CollisionMesh {
    std::string model;
    std::string bone;
    std::vector<std::array<float, 3>> vertices;
    std::vector<std::array<uint32_t, 3>> triangles;
};

// Collision primitives ("prim"), each on a bone of its model. Names say what
// they stop: "p_-so-trunk" stops soldiers (s) and ordnance (o); other letters
// are vehicles (v), buildings (b) and terrain (t). A name without flags stops
// everything.
struct CollisionPrimitive {
    enum Shape : uint32_t { sphere = 1, cylinder = 2, box = 4 };  // ASSUMPTION: sphere (not seen yet)
    std::string name;
    std::string parent;  // bone
    uint32_t shape = 0;
    Transform transform;  // in the parent bone's space
    // box: half extents; cylinder: radius, full height (along local y,
    // centered); sphere: radius.
    std::array<float, 3> size{};

    bool stops(char kind) const;  // kind: 's', 'v', 'b', 'o' or 't'
    // The flags in its name, if it has any.
    std::optional<std::string> flags() const;
};

struct CollisionPrimitives {
    std::string model;
    std::vector<CollisionPrimitive> primitives;
};

CollisionMesh read_collision_mesh(const UcfbNode& coll);
CollisionPrimitives read_collision_primitives(const UcfbNode& prim);

// Reads a "modl" chunk: segments with uncompressed vertex buffers (a
// segment's compressed copy is ignored). Throws std::runtime_error on
// malformed data.
Model read_model(const UcfbNode& modl);

// Reads a "skel" chunk.
Skeleton read_skeleton(const UcfbNode& skel);

// Decodes a "tex_" chunk's top mip level from the best format stored
// (D3D formats: DXT1/3/5, 32-bit, 16-bit, luminance), or nothing if none is
// supported (e.g. bump maps).
std::optional<swg::ImageRgba> read_texture(const UcfbNode& tex);

} // namespace swm::bf2
