#pragma once

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "swbfo/core/vec3.h"

namespace swbfo::core {

// Points on the ground plane (x, z) filed in a uniform grid, for "what's
// near here" questions: which players an NPC is near, which NPCs a client
// sees. Planets are 16 km across, so the grid is sparse (a hash of cells).
// Rebuilt each tick: clear() keeps the cells' storage.
class PointGrid {
public:
    explicit PointGrid(double cell_size = 64) : cell_(cell_size > 1 ? cell_size : 1) {}

    void clear() {
        for (auto& [_, points] : cells_) {
            points.clear();
        }
        count_ = 0;
        // Cells left empty for a long while (players moved on) are dropped now and then.
        if (++clears_ % 512 == 0) {
            std::erase_if(cells_, [](const auto& cell) { return cell.second.empty(); });
        }
    }

    void insert(uint32_t id, Vec3 position) {
        cells_[key(cell_of(position.x), cell_of(position.z))].push_back({id, position});
        ++count_;
    }

    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    double cell_size() const { return cell_; }

    // Calls f(id, position, squared horizontal distance) for each point
    // within `radius` of `center`, horizontally.
    template <class F>
    void for_each_near(Vec3 center, double radius, F&& f) const {
        if (count_ == 0 || radius < 0) {
            return;
        }
        const double r2 = radius * radius;
        const int x0 = cell_of(center.x - radius), x1 = cell_of(center.x + radius);
        const int z0 = cell_of(center.z - radius), z1 = cell_of(center.z + radius);
        for (int cx = x0; cx <= x1; ++cx) {
            for (int cz = z0; cz <= z1; ++cz) {
                const auto it = cells_.find(key(cx, cz));
                if (it == cells_.end()) {
                    continue;
                }
                for (const auto& [id, p] : it->second) {
                    const double dx = p.x - center.x, dz = p.z - center.z;
                    const double d2 = dx * dx + dz * dz;
                    if (d2 <= r2) {
                        f(id, p, d2);
                    }
                }
            }
        }
    }

    // Whether any point is within `radius` of `center`, horizontally.
    bool any_near(Vec3 center, double radius) const {
        if (count_ == 0 || radius < 0) {
            return false;
        }
        const double r2 = radius * radius;
        const int x0 = cell_of(center.x - radius), x1 = cell_of(center.x + radius);
        const int z0 = cell_of(center.z - radius), z1 = cell_of(center.z + radius);
        for (int cx = x0; cx <= x1; ++cx) {
            for (int cz = z0; cz <= z1; ++cz) {
                const auto it = cells_.find(key(cx, cz));
                if (it == cells_.end()) {
                    continue;
                }
                for (const auto& [id, p] : it->second) {
                    const double dx = p.x - center.x, dz = p.z - center.z;
                    if (dx * dx + dz * dz <= r2) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

private:
    int cell_of(double v) const { return int(std::floor(v / cell_)); }
    static int64_t key(int cx, int cz) { return (int64_t(cx) << 32) ^ int64_t(uint32_t(cz)); }

    double cell_;
    std::unordered_map<int64_t, std::vector<std::pair<uint32_t, Vec3>>> cells_;
    size_t count_ = 0;
    uint64_t clears_ = 0;
};

} // namespace swbfo::core
