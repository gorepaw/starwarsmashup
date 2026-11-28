#include "swg/appearance_collision.h"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

#include "swg/iff.h"
#include "swg/manifest.h"
#include "swg/static_mesh.h"

namespace swbfo::swg {

namespace {

// A triangle facing outward in SWG space (counter-clockwise from outside,
// as SWG's own collision meshes are: verified on a building's roof), into
// glTF space: mirroring X reverses the turn, so the corners swap too.
void add_solid(std::vector<std::array<Vec3, 3>>& out, const Mat4& transform, Vec3 a, Vec3 b, Vec3 c) {
    out.push_back({to_gltf(transform_point(transform, a)), to_gltf(transform_point(transform, c)),
                   to_gltf(transform_point(transform, b))});
}

void add_box(std::vector<std::array<Vec3, 3>>& out, const Mat4& transform, Vec3 lo, Vec3 hi) {
    const Vec3 v[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                       {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    // Each face counter-clockwise seen from outside (right-handed).
    const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
    for (const auto& f : faces) {
        add_solid(out, transform, v[f[0]], v[f[1]], v[f[2]]);
        add_solid(out, transform, v[f[0]], v[f[2]], v[f[3]]);
    }
}

void add_cylinder(std::vector<std::array<Vec3, 3>>& out, const Mat4& transform, Vec3 base, float radius, float height) {
    constexpr int sides = 12;
    const Vec3 top{base.x, base.y + height, base.z};
    for (int i = 0; i < sides; ++i) {
        const float a0 = 2 * std::numbers::pi_v<float> * float(i) / sides;
        const float a1 = 2 * std::numbers::pi_v<float> * float(i + 1) / sides;
        const Vec3 p0{base.x + radius * std::cos(a0), base.y, base.z + radius * std::sin(a0)};
        const Vec3 p1{base.x + radius * std::cos(a1), base.y, base.z + radius * std::sin(a1)};
        const Vec3 q0{p0.x, top.y, p0.z}, q1{p1.x, top.y, p1.z};
        // Around +Y from +X toward +Z: outward is p0 -> q0 -> p1 (right-handed).
        add_solid(out, transform, p0, q0, q1);
        add_solid(out, transform, p0, q1, p1);
        add_solid(out, transform, top, q1, q0);
        add_solid(out, transform, base, p0, p1);
    }
}

Vec3 read_vec3(IffReader& r) {
    const float x = r.f32(), y = r.f32(), z = r.f32();
    return {x, y, z};
}

// The first form child that is a version (its name all digits).
const IffNode* version_of(const IffNode& node) {
    for (const IffNode& child : node.children) {
        if (child.form && child.tag.find_first_not_of("0123456789") == std::string::npos) {
            return &child;
        }
    }
    return nullptr;
}

} // namespace

void add_floor_collision(const FloorMesh& floor, const Mat4& transform, AppearanceCollision& out, float wall_height) {
    std::vector<Vec3> vertices;
    for (const Vec3& v : floor.vertices) {
        vertices.push_back(to_gltf(transform_point(transform, v)));
    }
    for (const FloorMesh::Triangle& t : floor.triangles) {
        const Vec3 a = vertices[t.corners[0]], b = vertices[t.corners[1]], c = vertices[t.corners[2]];
        // Facing up, whichever way the floor turns.
        if (cross(b - a, c - a).y >= 0) {
            out.floors.push_back({a, b, c});
        } else {
            out.floors.push_back({a, c, b});
        }
        for (size_t k = 0; k < 3; ++k) {
            if (t.neighbors[k] >= 0 || t.crossable[k]) {
                continue;
            }
            const Vec3 p = vertices[t.corners[k]], q = vertices[t.corners[(k + 1) % 3]];
            const Vec3 up{0, wall_height, 0};
            // Both ways: from the floor and from outside it.
            out.walls.push_back({p, q, q + up});
            out.walls.push_back({p, q + up, p + up});
            out.walls.push_back({p, q + up, q});
            out.walls.push_back({p, p + up, q + up});
        }
    }
}

AppearanceCollision AppearanceCollisions::load(const std::string& path) {
    AppearanceCollision out;
    collect(normalize_path(path), Mat4{}, 0, out);
    return out;
}

void AppearanceCollisions::collect(const std::string& path, const Mat4& transform, int depth, AppearanceCollision& out) {
    if (depth > 8) {
        throw std::runtime_error(path + ": appearances nested too deeply");
    }
    const std::vector<uint8_t> bytes = library_.read(path);
    const IffNode root = parse_iff_root(bytes);
    if (root.tag == "APT ") {
        for (const IffNode& version : root.children) {
            if (const IffNode* name = version.form ? version.child("NAME") : nullptr) {
                collect(normalize_path(IffReader(name->data).cstring()), transform, depth + 1, out);
                return;
            }
        }
        throw std::runtime_error(path + ": redirect has no NAME");
    }
    const IffNode* version = version_of(root);
    if (version == nullptr) {
        return;
    }
    if (root.tag == "CMPA") {
        for (const IffNode* part : version->children_with("PART")) {
            IffReader r(part->data);
            const std::string child = r.cstring();
            Mat4 placed;
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 4; ++column) {
                    placed.at(row, column) = r.f32();
                }
            }
            collect("appearance/" + child, transform * placed, depth + 1, out);
        }
    } else if (root.tag != "MESH" && root.tag != "DTLA") {
        ++skipped_["appearance " + root.tag];
        return;
    }
    const IffNode* appearance = version->child("APPR");
    const IffNode* appearance_version = appearance != nullptr ? version_of(*appearance) : nullptr;
    if (appearance_version == nullptr) {
        return;
    }
    // Its extents: the first bounds it, the second is what collides.
    int extents = 0;
    for (const IffNode& child : appearance_version->children) {
        if (!child.form) {
            continue;
        }
        if (child.tag == "FLOR") {
            const IffNode* data = child.child("DATA");
            if (data == nullptr) {
                continue;
            }
            IffReader r(data->data);
            if (r.u8() != 0) {
                const std::string floor_path = normalize_path(r.cstring());
                if (!floor_path.empty() && library_.contains(floor_path)) {
                    add_floor_collision(parse_floor(library_.read(floor_path)), transform, out);
                }
            }
            continue;
        }
        if (child.tag == "HPTS") {
            continue;
        }
        if (extents++ == 1) {
            // The extent that collides.
            struct Walk {
                AppearanceCollisions& self;
                AppearanceCollision& out;
                const Mat4& transform;
                const std::string& path;
                void extent(const IffNode& node) {
                    const IffNode* v = version_of(node);
                    if (node.tag == "NULL" || node.tag == "EXSP") {
                        return;  // nothing, or a sphere only bounding it
                    }
                    if (v == nullptr) {
                        ++self.skipped_[node.tag];
                        return;
                    }
                    if (node.tag == "EXBX") {
                        const IffNode* box = v->child("BOX ");
                        if (box == nullptr) {
                            ++self.skipped_["EXBX without BOX"];
                            return;
                        }
                        IffReader r(box->data);
                        const Vec3 hi = read_vec3(r);
                        const Vec3 lo = read_vec3(r);
                        add_box(out.solids, transform, lo, hi);
                    } else if (node.tag == "XCYL") {
                        const IffNode* cylinder = v->child("CYLN");
                        if (cylinder == nullptr) {
                            ++self.skipped_["XCYL without CYLN"];
                            return;
                        }
                        IffReader r(cylinder->data);
                        const Vec3 base = read_vec3(r);
                        const float radius = r.f32();
                        const float height = r.f32();
                        add_cylinder(out.solids, transform, base, radius, height);
                    } else if (node.tag == "CMSH") {
                        const IffNode* list = v->child("IDTL");
                        const IffNode* lv = list != nullptr ? version_of(*list) : nullptr;
                        const IffNode* vert = lv != nullptr ? lv->child("VERT") : nullptr;
                        const IffNode* indx = lv != nullptr ? lv->child("INDX") : nullptr;
                        if (vert == nullptr || indx == nullptr) {
                            ++self.skipped_["CMSH without IDTL"];
                            return;
                        }
                        std::vector<Vec3> points;
                        IffReader vr(vert->data);
                        while (vr.remaining() >= 12) {
                            points.push_back(read_vec3(vr));
                        }
                        IffReader ir(indx->data);
                        while (ir.remaining() >= 12) {
                            const uint32_t a = ir.u32(), b = ir.u32(), c = ir.u32();
                            if (a < points.size() && b < points.size() && c < points.size()) {
                                add_solid(out.solids, transform, points[a], points[b], points[c]);
                            }
                        }
                    } else if (node.tag == "CMPT" || node.tag == "CPST") {
                        // Several extents (a component extent holds a composite).
                        for (const IffNode& child : v->children) {
                            if (child.form) {
                                extent(child);
                            }
                        }
                    } else if (node.tag == "DTAL") {
                        // A detail extent: a composite whose first extent
                        // only bounds the detailed ones after it.
                        const IffNode* composite = v->child("CPST");
                        const IffNode* cv = composite != nullptr ? version_of(*composite) : nullptr;
                        if (cv == nullptr) {
                            ++self.skipped_["DTAL without CPST (e.g. " + path + ")"];
                            return;
                        }
                        bool first = true;
                        for (const IffNode& child : cv->children) {
                            if (child.form && !std::exchange(first, false)) {
                                extent(child);
                            }
                        }
                    } else {
                        ++self.skipped_[node.tag + " (e.g. " + path + ")"];
                    }
                }
            };
            Walk{*this, out, transform, path}.extent(child);
        }
    }
}

} // namespace swbfo::swg
