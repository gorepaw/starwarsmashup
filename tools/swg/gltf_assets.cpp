#include "swg/gltf_assets.h"

#include <ostream>
#include <stdexcept>

#include "swg/appearance.h"
#include "swg/iff.h"
#include "swg/image.h"
#include "swg/manifest.h"

namespace swm::swg {

int GltfAssets::material_for(const std::string& shader_path) {
    if (const auto it = materials_.find(shader_path); it != materials_.end()) {
        return it->second;
    }
    Gltf::Material material{.name = std::filesystem::path(shader_path).stem().string()};
    try {
        const std::string texture = shader_main_texture(library_.read(shader_path));
        if (!texture.empty()) {
            material.texture = image_for(texture);
        }
    } catch (const std::exception& e) {
        log_ << "  warning: " << shader_path << ": " << e.what() << "\n";
    }
    gltf_.materials.push_back(std::move(material));
    materials_[shader_path] = int(gltf_.materials.size() - 1);
    return materials_[shader_path];
}

int GltfAssets::image_for(const std::string& texture_path) {
    if (const auto it = images_.find(texture_path); it != images_.end()) {
        return it->second;
    }
    const std::string file_name = std::filesystem::path(texture_path).stem().string() + ".png";
    write_png(out_dir_ / file_name, decode_dds(library_.read(texture_path)));
    gltf_.images.push_back(file_name);
    images_[texture_path] = int(gltf_.images.size() - 1);
    return images_[texture_path];
}

int GltfAssets::add_static_mesh(const StaticMesh& mesh, std::string name) {
    Gltf::Mesh gltf_mesh{.name = std::move(name)};
    for (const StaticMesh::Part& part : mesh.parts) {
        if (part.indices.empty()) {
            continue;
        }
        std::vector<Vec3> positions, normals;
        for (const Vec3& p : part.positions) {
            positions.push_back(to_gltf(p));
        }
        for (const Vec3& n : part.normals) {
            normals.push_back(to_gltf(n));
        }
        Gltf::Primitive primitive;
        primitive.attributes["POSITION"] = gltf_.add_vec3(positions, true);
        if (!normals.empty()) {
            primitive.attributes["NORMAL"] = gltf_.add_vec3(normals, false);
        }
        if (!part.uv.empty()) {
            primitive.attributes["TEXCOORD_0"] = gltf_.add_vec2(part.uv);
        }
        std::vector<uint32_t> indices;
        for (size_t i = 0; i + 2 < part.indices.size(); i += 3) {
            append_triangle(indices, part.indices[i], part.indices[i + 1], part.indices[i + 2]);
        }
        primitive.indices = gltf_.add_indices(indices);
        primitive.material = material_for(part.shader);
        gltf_mesh.primitives.push_back(std::move(primitive));
    }
    gltf_.meshes.push_back(std::move(gltf_mesh));
    return int(gltf_.meshes.size() - 1);
}

namespace {

// Moves a mesh's parts into `out`, transformed by `transform`.
void append_transformed(StaticMesh&& mesh, const Mat4& transform, StaticMesh& out) {
    for (StaticMesh::Part& part : mesh.parts) {
        for (Vec3& p : part.positions) {
            p = transform_point(transform, p);
        }
        for (Vec3& n : part.normals) {
            n = normalized(transform_point(transform, n) - transform_point(transform, Vec3{}));
        }
        out.parts.push_back(std::move(part));
    }
}

StaticMesh load_appearance(TreLibrary& library, const std::string& path, int depth) {
    if (depth > 8) {
        throw std::runtime_error(path + ": appearances nested too deeply");
    }
    const std::vector<uint8_t> bytes = library.read(path);
    const IffNode root = parse_iff_root(bytes);
    if (root.tag == "MESH") {
        return parse_static_mesh(bytes);
    }
    if (root.tag == "APT ") {
        // Redirect: FORM APT -> 0000 -> NAME (full path of another appearance).
        for (const IffNode& version : root.children) {
            if (const IffNode* name = version.form ? version.child("NAME") : nullptr) {
                return load_appearance(library, normalize_path(IffReader(name->data).cstring()), depth + 1);
            }
        }
        throw std::runtime_error(path + ": redirect has no NAME");
    }
    if (root.tag == "DTLA") {
        return load_appearance(library, "appearance/" + lod_highest_detail(bytes), depth + 1);
    }
    if (root.tag == "CMPA") {
        // Component appearance: PART chunks, each a child appearance path
        // (relative to appearance/) and a 3x4 row-major transform.
        StaticMesh combined;
        for (const IffNode& version : root.children) {
            if (!version.form) {
                continue;
            }
            for (const IffNode* part : version.children_with("PART")) {
                IffReader r(part->data);
                const std::string child = r.cstring();
                Mat4 transform;
                for (int row = 0; row < 3; ++row) {
                    for (int column = 0; column < 4; ++column) {
                        transform.at(row, column) = r.f32();
                    }
                }
                append_transformed(load_appearance(library, "appearance/" + child, depth + 1), transform, combined);
            }
        }
        return combined;
    }
    throw std::runtime_error(path + ": unsupported appearance type " + root.tag);
}

} // namespace

StaticMesh load_static_appearance(TreLibrary& library, const std::string& path) {
    return load_appearance(library, normalize_path(path), 0);
}

} // namespace swm::swg
