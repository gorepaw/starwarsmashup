#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

#include "common/math.h"
#include "swg/character_export.h"
#include "swg/tre_library.h"
#include "swbfo/core/heightfield.h"

namespace swbfo::swg {

// A town on a planet: the snapshot's objects within `radius` of its center.
struct CityArea {
    std::string name;
    double center_x = 0, center_z = 0;  // Galaxies x z
    double radius = 0;                  // meters
    // A site out in the country (the Legacy's caves and bunkers, docs/
    // LEGACY.md) rather than a town: its rooms deep under the terrain (more
    // than site_room_depth) keep the terrain over them, as Galaxies drew it
    // (there's no hole in the hillside over a cave); a room nearer the
    // surface, as an entrance, is opened as a town's are.
    bool site = false;
};

inline constexpr double site_room_depth = 6;  // meters of terrain over a site's floor that stays

struct CityExport {
    std::string snapshot;             // the planet's world snapshot (.ws)
    double center_x = 0, center_z = 0;  // Galaxies x z
    double radius = 0;                // meters around the center
    // Or several towns on the same bake (a planet's), when not empty: each
    // object within any of them, and each town's rooms a terrain holes patch
    // of its own (core::save_terrain_hole_patches), as they're kilometers apart.
    std::vector<CityArea> areas;
    std::filesystem::path bake;       // the terrain bake's prefix the city stands on
    // A hub file (game/hubs/) whose war terminals ([recruiter] with a
    // Template, an object template, and Yaw in degrees) stand in the city
    // too, on the terrain, as do its [place]s (any object template the
    // snapshot lacks, a bunker or a cave for a Legacy site: Template,
    // Position = x z, Yaw, optional Height), and whose people ([npc] and [crowd] templates)
    // are exported to <bake>_city/people/ (a glTF each, named by the
    // template's file without "shared_", with clips walk, idle and sit).
    // Optional.
    std::filesystem::path hub;
    std::vector<std::filesystem::path> more_hubs;  // the other towns' files, likewise
};

// Exports the objects a world snapshot places around a point (a city's
// buildings, walls, props and terminals), next to a terrain bake:
//
// - <bake>_city/: a glTF per model (with its textures as PNGs). Buildings
//   (a portal layout) are exported as export_building does, every cell and
//   its lights; the rest as their appearance's most detailed mesh.
// - <bake>.objects.json: where each object stands, in game space: as
//   bf2tool's map objects ({model, position, basis (row-major)}), with
//   "building": true for buildings, drawn with their cells' own lights,
//   "recruiter": true for the hub's war terminals, and for the furniture in
//   a building's cells, "inside" (the building's index in the list) and
//   "cell" (its cell there, whose lights light it). A building's doors (a
//   door style named on a doorway, from datatables/appearance/door_style)
//   are listed after it too, with "door": {slide (game space), open and
//   close (seconds), radius (meters: anyone that near opens it)}.
// - <bake>.collision: what they stop (core::MapCollision, which the world
//   on that bake loads): each model's collision extent (its collision mesh,
//   boxes and cylinders) and its floors, with walls where floors can't be
//   left. A building's: its outside's extent, and every cell's floor.
// - <bake>.holes: where its rooms are (every cell's floor but the
//   outside's), the terrain taken away or hidden (core::TerrainHoles,
//   room_terrain_holes), as in Galaxies a room has no terrain in it.
//
// Creatures (a skeletal appearance) are exported as the hub's people are,
// into <bake>_city/people/, listed with "creature": true, and stop
// characters and shots as an upright prism: their template's collision
// radius and height, else their meshes' bounds in their bind pose.
// Particle effects are described for the client (export_particle_effect,
// into <bake>_city/particles/), listed with "particles": true; sound
// markers aren't. Throws std::runtime_error if the snapshot can't be read.
void export_city(TreLibrary& library, const CityExport& city, std::ostream& log);

using HubClipProvider = std::function<std::vector<AnimationClip>(const Skeleton&)>;
void export_hub_people(TreLibrary& library, const CityExport& city, std::ostream& log,
                       const HubClipProvider& bandit_clips = {});

// The terrain in rooms (their floors, game space), as Galaxies draws none
// there. Taken away where a cell's center is over a walked floor (so the
// holes stay inside the rooms, under their walls). Hidden (drawn away, still
// stood on: see TerrainHoles) where the terrain (its height at a game x z)
// stands above a floor over the cell's center (a drawn floor's: it reaches
// the walls where the walked one stops short of them) or over part of the
// cell: a room under the street, as the cantina's, showed the terrain along
// its walls' foot. Terrain under a floor stays (the floor covers it).
// `keep_deeper` (meters, 0: never): terrain standing more than that over a
// floor stays too (a site's cave: CityArea::site).
core::TerrainHoles room_terrain_holes(const std::vector<std::array<Vec3, 3>>& walked,
                                      const std::vector<std::array<Vec3, 3>>& drawn,
                                      const std::function<double(double, double)>& terrain_height,
                                      double keep_deeper = 0);

} // namespace swbfo::swg
