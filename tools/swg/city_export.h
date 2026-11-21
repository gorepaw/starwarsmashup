#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

#include "swg/tre_library.h"

namespace swm::swg {

struct CityExport {
    std::string snapshot;             // the planet's world snapshot (.ws)
    double center_x = 0, center_z = 0;  // Galaxies x z
    double radius = 0;                // meters around the center
    std::filesystem::path bake;       // the terrain bake's prefix the city stands on
    // A hub file (game/hubs/) whose war terminals ([recruiter] with a
    // Template, an object template, and Yaw in degrees) stand in the city
    // too, on the terrain. Optional.
    std::filesystem::path hub;
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
//   and "recruiter": true for the hub's war terminals.
// - <bake>.collision: what they stop (core::MapCollision, which the world
//   on that bake loads): each model's collision extent (its collision mesh,
//   boxes and cylinders) and its floors, with walls where floors can't be
//   left. A building's: its outside's extent, and every cell's floor.
//
// Objects inside buildings (furniture) and objects without a static
// appearance (particles, sounds, creatures) aren't exported yet; the log
// counts them. Throws std::runtime_error if the snapshot can't be read.
void export_city(TreLibrary& library, const CityExport& city, std::ostream& log);

} // namespace swm::swg
