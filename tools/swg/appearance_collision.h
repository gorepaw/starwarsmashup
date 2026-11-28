#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "common/math.h"
#include "swg/portal_layout.h"
#include "swg/tre_library.h"

namespace swbfo::swg {

// What stops characters at a static appearance, in glTF space (SWG's with X
// mirrored: as the appearance's glTF is drawn).
struct AppearanceCollision {
    // Triangles facing outward (counter-clockwise from outside): its
    // collision extent, a mesh (CMSH) or boxes (EXBX) and cylinders (XCYL),
    // alone or several (CMPT).
    std::vector<std::array<Vec3, 3>> solids;
    // Its floors (FLOR, a .flr), walked on: triangles facing up, and walls
    // rising from their edges that can't be crossed, facing both ways.
    std::vector<std::array<Vec3, 3>> floors;
    std::vector<std::array<Vec3, 3>> walls;
};

// The floors and walls of a floor (.flr), in glTF space, with `transform`
// (SWG space) applied first. Walls are `wall_height` high.
void add_floor_collision(const FloorMesh& floor, const Mat4& transform, AppearanceCollision& out,
                         float wall_height = 3);

// Reads appearances' collision: a mesh's (MESH), a level-of-detail list's
// (DTLA), a component appearance's (CMPA: its own and its parts', each
// moved into place) or a redirect's (APT). An appearance's collision extent
// is the second extent of its APPR form (the first bounds it); its floor
// is the FLOR's. Kinds of extent it doesn't read are counted in
// `skipped` (by form name). Throws std::runtime_error if a file can't be
// read.
class AppearanceCollisions {
public:
    explicit AppearanceCollisions(TreLibrary& library) : library_(library) {}
    AppearanceCollision load(const std::string& path);
    const std::map<std::string, int>& skipped() const { return skipped_; }

private:
    void collect(const std::string& path, const Mat4& transform, int depth, AppearanceCollision& out);

    TreLibrary& library_;
    std::map<std::string, int> skipped_;
};

} // namespace swbfo::swg
