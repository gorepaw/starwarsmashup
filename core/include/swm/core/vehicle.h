#pragma once

#include <filesystem>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "swm/core/loadout.h"
#include "swm/core/movement.h"
#include "swm/core/vec3.h"

namespace swm::core {

class World;

// How a vehicle moves: hovering over the ground (speeder bikes, hover
// tanks) or walking on it (AT-STs).
// A turret stays where it stands and turns to its gunner's look; a flyer
// (the snowspeeder) takes off and flies where its pilot looks.
enum class VehicleKind : uint8_t { hover, walker, turret, flyer };

// A Battlefront vehicle (written by `bf2tool export-vehicle` from its class):
// its handling, body, health and the pilot's weapons.
// A gunner's seat (a later FlyerSection or WalkerSection: the hover tank's
// TURRET1): where its guns fire from and how far they turn from the
// vehicle's heading, as the pilot's do, and its guns.
struct VehicleSeat {
    std::string name;
    Vec3 eye{0, 1, 0};
    double yaw_min = -std::numbers::pi, yaw_max = std::numbers::pi;
    double pitch_min = -0.5, pitch_max = 0.5;
    Loadout loadout;
    // Its turret on the model (lowercase node names; "" none): the node that
    // turns with the gunner's aim, and the one that tilts with it.
    std::string turret_node, pitch_node;
};

struct VehicleClass {
    std::string name;   // its Battlefront class, e.g. "imp_hover_speederbike"
    std::string label;  // e.g. "Speederbike"
    std::filesystem::path model;  // its glTF, next to its file
    VehicleKind kind = VehicleKind::hover;
    double max_health = 1000;
    // Hover: speeds (ForwardSpeed, ReverseSpeed, StrafeSpeed; BoostSpeed
    // while boosting, which drains energy: EnergyBoostDrain a second,
    // EnergyAutoRestore back), how fast it gets there (Acceleration,
    // BoostAcceleration), slows with nothing pressed (Deceleration) and
    // stops sliding sideways (Traction), all m/s and m/s^2. Walkers:
    // MaxSpeed forward, MaxStrafeSpeed back and sideways.
    double forward_speed = 20;
    double reverse_speed = 6;
    double strafe_speed = 3;
    double boost_speed = 0;
    double acceleration = 20;
    double boost_acceleration = 0;
    double deceleration = 20;
    double traction = 20;
    double energy_restore = 15;
    double energy_boost_drain = 20;
    // Radians a second it turns toward where its pilot looks (TurnRate;
    // a walker's MaxTurnSpeed). ASSUMPTION: TurnRate is radians a second.
    double turn_rate = 1.5;
    double altitude = 1;  // SetAltitude: how high a hover vehicle rides over the ground, m
    double gravity_scale = 1;  // GravityScale: how fast it falls off a ledge
    // Its body (the box round its collision, its model's space: +z
    // forward), for walls and hits.
    Vec3 bounds_min{-0.6, 0, -1.2};
    Vec3 bounds_max{0.6, 1.4, 1.2};
    double step_up = 0.6;  // m: the highest ledge it goes up
    // Where its guns fire from (EyePointOffset over its middle) and how far
    // they turn from straight ahead (YawLimits, PitchLimits: degrees as the
    // file has them, radians here).
    Vec3 eye{0, 1, 0};
    double yaw_min = -0.26, yaw_max = 0.26;
    double pitch_min = -0.1, pitch_max = 0.1;
    // The third-person camera: TrackCenter, the point it looks over, behind
    // and above (its model's space).
    Vec3 track_center{0, 2.5, -6};
    Vec3 track_offset;  // TrackOffset: the chase camera further back (z) from TrackCenter
    // The pilot's weapons, as a loadout's primary and sidearm.
    Loadout loadout;
    // Gunners' seats ([seat]s, their [weapon]s marked Seat = 1, 2, ...).
    std::vector<VehicleSeat> seats;
    // Destroyed: its explosion (whoever's near takes it), and its look.
    double explosion_damage = 0, explosion_inner = 0, explosion_outer = 0;
    std::string explosion_effect;
    std::string engine_sound;
    double stride_speed = 0;  // a walker's steps' own pace (m/s), to play them at its speed
    // Where a rider on an open vehicle (a speeder bike) has their feet,
    // sitting, in its model's space (RiderSeat; ASSUMPTION for Battlefront's).
    Vec3 rider_seat{0, -0.5, -0.25};
    // A flyer: its speeds in the air (MinSpeed .. MaxSpeed, BoostSpeed on
    // energy), how fast it pitches (PitchRate, radians a second), and its
    // takeoff and landing: off the ground to TakeoffHeight in TakeoffTime,
    // down to LandedHeight from below LandingSpeed.
    double min_speed = 15, max_speed = 55;
    double pitch_rate = 1.2;
    double takeoff_height = 2.5, takeoff_time = 1, landed_height = 0.5, landing_speed = 5;
    std::string map_texture, health_texture;
    // How it looks hurt and moving: each damage effect while its health is
    // between start and stop percent, at `at` (its model's space; [damage]s),
    // and a hover vehicle's wake (WakeEffect).
    struct DamageEffect {
        std::string effect;
        double start = 100, stop = 0;
        Vec3 at;
    };
    std::vector<DamageEffect> damage_effects;
    std::string wake_effect;

    double radius() const;  // for walls: half its width
    double height() const { return bounds_max.y; }
};

// Reads a vehicle file. Throws std::runtime_error if it can't be read.
// Every class file (*.ini) in `dir`, by file name (the world's vehicles:
// game/vehicles/). Server and clients load the same, so indices agree.
std::vector<VehicleClass> load_vehicle_classes(const std::filesystem::path& dir);
VehicleClass load_vehicle_class(const std::filesystem::path& path);

// One tick of a vehicle under its pilot's input: it turns toward the
// input's yaw, speeds along its heading, hovers over the ground or walks on
// it. Its state's yaw is its heading; pitch is the input's. Like
// simulate_move, deterministic.
void simulate_vehicle(MoveState& state, const MoveInput& input, const World& world, const VehicleClass& vehicle,
                      double dt);

// Where its guns aim for an input: the input's yaw and pitch, within their
// limits round its heading.
std::pair<double, double> vehicle_aim(const MoveState& state, const MoveInput& input, const VehicleClass& vehicle);
// Where its guns fire from, in the world.
Vec3 vehicle_muzzle(const MoveState& state, const VehicleClass& vehicle);
// A gunner's: `state` is the gunner's (riding: the vehicle's place and
// heading), `seat` 1 for the first gunner's seat. Out of range: the pilot's.
const VehicleSeat* vehicle_seat(const VehicleClass& vehicle, int seat);
std::pair<double, double> seat_aim(const MoveState& state, const MoveInput& input, const VehicleSeat& seat);
Vec3 seat_muzzle(const MoveState& state, const VehicleSeat& seat);
// A point in the vehicle's model space (+z forward), in the world, for a
// vehicle at `position` facing `yaw`.
Vec3 vehicle_to_world(Vec3 position, double yaw, Vec3 local);
// Where along the segment a..b (0..1) it first enters the vehicle's box,
// if it does.
std::optional<double> segment_hits_vehicle(Vec3 a, Vec3 b, Vec3 position, double yaw, const VehicleClass& vehicle);
// How far a point is from the vehicle's box (0 inside).
double distance_to_vehicle(Vec3 point, Vec3 position, double yaw, const VehicleClass& vehicle);

} // namespace swm::core
