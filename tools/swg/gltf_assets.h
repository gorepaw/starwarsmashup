#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "swg/appearance.h"
#include "swg/gltf.h"
#include "swg/static_mesh.h"
#include "swg/tre_library.h"

namespace swm::swg {

// After mirroring X into glTF space, SWG's triangles face inward (verified by
// rendering), so each triangle's winding is reversed.
inline void append_triangle(std::vector<uint32_t>& indices, uint32_t a, uint32_t b, uint32_t c) {
    indices.insert(indices.end(), {a, c, b});
}

// Adds SWG meshes and shaders to a glTF document. A shader becomes a material
// whose main texture is decoded to a PNG in `out_dir`, next to the .gltf.
//
// A shader's effect sets how its material blends (see effect_blend): alpha
// blending as glTF's BLEND, a cutout as MASK (at 0.5). Added light (glowing
// panels) has no glTF mode: drawn opaque.
//
// A customizable shader's colors (see ShaderCustomization) are either baked
// at their defaults (Colors::bake: into the material's base color; a
// two-color shader's into a copy of its texture, <material>.hue.png), or
// described for whoever draws it to choose (Colors::describe: the texture is
// kept, the base color is the default MAIN color, and colors() lists them).
class GltfAssets {
public:
    enum class Colors { bake, describe };

    GltfAssets(TreLibrary& library, Gltf& gltf, std::filesystem::path out_dir, std::ostream& log,
               Colors colors = Colors::bake)
        : library_(library), gltf_(gltf), out_dir_(std::move(out_dir)), log_(log), colors_mode_(colors) {}

    int material_for(const std::string& shader_path);              // cached per shader
    int add_static_mesh(const StaticMesh& mesh, std::string name);  // returns the mesh index

    // The shader an owner proxy shader (OPST) by this name stands for, e.g.
    // "body": the wearer's body's shader.
    void set_owner_shader(const std::string& name, const std::string& shader_path) {
        owner_shaders_[name] = shader_path;
    }

    // Colors::describe: each customizable material's colors, by its name.
    const std::map<std::string, ShaderCustomization>& colors() const { return colors_; }

    // A texture decoded to a PNG next to the glTF (once); its file's name.
    std::string texture_file(const std::string& texture_path);

    // A palette's colors (cached). Throws std::runtime_error.
    const std::vector<std::array<uint8_t, 3>>& palette(const std::string& path);

    // A factor's (MAIN, HUEB) default color: its palette's default entry,
    // else the shader's default for it, else white.
    std::array<uint8_t, 3> default_color(const ShaderCustomization& customization, const std::string& factor);

private:
    int image_for(const std::string& texture_path);
    int hued_image_for(const std::string& texture_path, const std::string& name, std::array<uint8_t, 3> main,
                       std::array<uint8_t, 3> hueb, bool keep_alpha);
    EffectBlend blend_of(const std::string& effect_path);  // cached; opaque if unreadable

    TreLibrary& library_;
    Gltf& gltf_;
    std::filesystem::path out_dir_;
    std::ostream& log_;
    Colors colors_mode_;
    std::map<std::string, int> materials_;
    std::map<std::string, int> images_;
    std::map<std::string, std::string> owner_shaders_;
    std::map<std::string, ShaderCustomization> colors_;
    std::map<std::string, std::vector<std::array<uint8_t, 3>>> palettes_;
    std::map<std::string, EffectBlend> blends_;
    std::set<std::string> texture_files_;
};

// The highest-detail static mesh of an appearance: a mesh (MESH), a level of
// detail list (DTLA), a component appearance (CMPA) whose parts are merged, or
// a redirect (APT) to one of those.
// Throws std::runtime_error for other appearance types.
StaticMesh load_static_appearance(TreLibrary& library, const std::string& path);

} // namespace swm::swg
