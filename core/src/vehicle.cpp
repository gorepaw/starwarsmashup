#include "swm/core/vehicle.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <stdexcept>

#include "swm/core/conquest.h"
#include "swm/core/ini.h"
#include "swm/core/world.h"

namespace swm::core {
namespace {

constexpr double degrees = std::numbers::pi / 180;

Vec3 read_vec3(const Ini::Section& s, const char* key, Vec3 fallback) {
    const auto text = s.get(key);
    if (!text) return fallback;
    std::istringstream in(*text);
    Vec3 v = fallback;
    in >> v.x >> v.y >> v.z;
    return v;
}

std::pair<double, double> read_pair(const Ini::Section& s, const char* key, std::pair<double, double> fallback) {
    const auto text = s.get(key);
    if (!text) return fallback;
    std::istringstream in(*text);
    double a = fallback.first, b = fallback.second;
    in >> a >> b;
    return {std::min(a, b), std::max(a, b)};
}

double wrap_angle(double a) {
    a = std::fmod(a + std::numbers::pi, 2 * std::numbers::pi);
    if (a < 0) a += 2 * std::numbers::pi;
    return a - std::numbers::pi;
}

// The model faces +z; a vehicle at yaw 0 faces -z (as everything with a yaw
// does), so its model is turned by yaw + pi.
Vec3 to_local(Vec3 point, Vec3 position, double yaw) {
    const double t = -(yaw + std::numbers::pi);
    const double c = std::cos(t), s = std::sin(t);
    const Vec3 d = point - position;
    return {d.x * c + d.z * s, d.y, -d.x * s + d.z * c};
}

} // namespace

double VehicleClass::radius() const {
    return std::max(0.3, std::min(bounds_max.x - bounds_min.x, bounds_max.z - bounds_min.z) / 2);
}

VehicleClass load_vehicle_class(const std::filesystem::path& path) {
    const Ini ini = Ini::load(path);
    const std::string where = path.string();
    const Ini::Section* main = ini.section("vehicle");
    if (main == nullptr) {
        throw std::runtime_error(where + ": no [vehicle] section");
    }
    const Ini::Section& s = *main;
    VehicleClass v;
    v.name = s.get("Name").value_or(path.stem().string());
    v.label = s.get("Label").value_or(v.name);
    v.model = path.parent_path() / s.get("Model").value_or(v.name + ".gltf");
    const std::string kind = s.get("Kind").value_or("hover");
    if (kind != "hover" && kind != "walker" && kind != "turret" && kind != "flyer") {
        throw std::runtime_error(where + ": Kind is hover, walker, turret or flyer, not " + kind);
    }
    v.kind = kind == "walker"   ? VehicleKind::walker
             : kind == "turret" ? VehicleKind::turret
             : kind == "flyer"  ? VehicleKind::flyer
                                : VehicleKind::hover;
    v.max_health = std::max(1.0, s.number("MaxHealth", v.max_health));
    if (v.kind == VehicleKind::turret) {
        v.forward_speed = v.reverse_speed = v.strafe_speed = 0;
        v.turn_rate = s.number("MaxTurnSpeed", 2);
    } else if (v.kind == VehicleKind::flyer) {
        v.max_speed = v.forward_speed = s.number("MaxSpeed", v.max_speed);
        v.min_speed = std::min(v.max_speed, s.number("MinSpeed", v.min_speed));
        v.boost_speed = s.number("BoostSpeed", 0);
        v.acceleration = std::max(5.0, (v.max_speed - v.min_speed) / 2);  // ASSUMPTION: two seconds over its range
        v.boost_acceleration = std::max(v.acceleration, (v.boost_speed - v.max_speed) / 2);
        v.turn_rate = s.number("TurnRate", 0.8);
        v.pitch_rate = s.number("PitchRate", v.pitch_rate);
        v.energy_restore = s.number("EnergyAutoRestore", v.energy_restore);
        v.energy_boost_drain = s.number("EnergyBoostDrain", v.energy_boost_drain);
        v.takeoff_height = s.number("TakeoffHeight", v.takeoff_height);
        v.takeoff_time = std::max(0.1, s.number("TakeoffTime", v.takeoff_time));
        v.landed_height = s.number("LandedHeight", v.landed_height);
        v.landing_speed = s.number("LandingSpeed", v.landing_speed);
    } else if (v.kind == VehicleKind::walker) {
        v.forward_speed = s.number("MaxSpeed", 5);
        v.reverse_speed = v.strafe_speed = s.number("MaxStrafeSpeed", 3);
        v.turn_rate = s.number("MaxTurnSpeed", 1.5);
        // ASSUMPTION: a walker gets to speed in about half a second.
        v.acceleration = v.deceleration = v.traction = v.forward_speed * 2;
        // Its run (the AT-ST's BoostSpeed), on energy.
        v.boost_speed = s.number("BoostSpeed", 0);
        v.boost_acceleration = std::min(s.number("BoostAcceleration", v.acceleration), v.acceleration * 2);
        v.energy_restore = s.number("EnergyAutoRestore", v.energy_restore);
        v.energy_boost_drain = s.number("EnergyBoostDrain", v.energy_boost_drain);
    } else {
        v.forward_speed = s.number("ForwardSpeed", v.forward_speed);
        v.reverse_speed = s.number("ReverseSpeed", v.reverse_speed);
        v.strafe_speed = s.number("StrafeSpeed", v.strafe_speed);
        v.boost_speed = s.number("BoostSpeed", 0);
        v.acceleration = s.number("Acceleration", v.acceleration);
        v.boost_acceleration = s.number("BoostAcceleration", v.acceleration);
        v.deceleration = s.number("Deceleration", v.deceleration);
        v.traction = s.number("Traction", v.traction);
        v.energy_restore = s.number("EnergyAutoRestore", v.energy_restore);
        v.energy_boost_drain = s.number("EnergyBoostDrain", v.energy_boost_drain);
        v.turn_rate = s.number("TurnRate", v.turn_rate);
        v.altitude = s.number("SetAltitude", v.altitude);
        v.gravity_scale = s.number("GravityScale", v.gravity_scale);
    }
    if (const auto bounds = s.get("Bounds")) {
        std::istringstream in(*bounds);
        Vec3 lo, hi;
        if (in >> lo.x >> lo.y >> lo.z >> hi.x >> hi.y >> hi.z) {
            v.bounds_min = lo;
            v.bounds_max = {hi.x, std::max(hi.y, lo.y + 0.5), hi.z};
        }
    }
    // A walker steps over a third of its height (ASSUMPTION); a hover
    // vehicle glides over what's under its altitude and a bit.
    v.step_up = v.kind == VehicleKind::walker ? std::max(0.6, v.bounds_max.y / 3) : std::max(0.6, v.altitude + 0.4);
    v.eye = read_vec3(s, "EyePointOffset", v.eye);
    if (v.eye.y <= 0) {
        // None (an AT-ST's is zero): from high on its body, ahead of its
        // middle (ASSUMPTION: where a walker's head and guns are).
        v.eye = {0, v.bounds_max.y * 0.75, v.bounds_max.z * 0.5};
    }
    const auto [yaw_lo, yaw_hi] = read_pair(s, "YawLimits", {-15, 15});
    const auto [pitch_lo, pitch_hi] = read_pair(s, "PitchLimits", {-5, 5});
    v.yaw_min = yaw_lo * degrees, v.yaw_max = yaw_hi * degrees;
    v.pitch_min = pitch_lo * degrees, v.pitch_max = pitch_hi * degrees;
    v.track_center = read_vec3(s, "TrackCenter", v.track_center);
    v.track_offset = read_vec3(s, "TrackOffset", v.track_offset);
    v.explosion_damage = s.number("ExplosionDamage", 0);
    v.explosion_inner = s.number("ExplosionRadiusInner", 0);
    v.explosion_outer = std::max(v.explosion_inner, s.number("ExplosionRadiusOuter", 0));
    v.explosion_effect = s.get("ExplosionEffect").value_or("");
    v.engine_sound = s.get("EngineSound").value_or("");
    v.stride_speed = s.number("StrideSpeed", 0);
    v.rider_seat = read_vec3(s, "RiderSeat", v.rider_seat);
    v.wake_effect = s.get("WakeEffect").value_or("");
    v.contrail_effect = s.get("ContrailEffect").value_or("");
    const std::string vtype = s.get("VehicleType").value_or("");
    v.is_transport = vtype.size() == 9 &&
                     std::equal(vtype.begin(), vtype.end(), "transport",
                                [](char a, char b) { return std::tolower(uint8_t(a)) == b; });
    for (const auto& [key, val] : s.values) {
        if (key.size() == 10 &&
            std::equal(key.begin(), key.end(), "spawnpoint",
                       [](char a, char b) { return std::tolower(uint8_t(a)) == b; })) {
            Vec3 pt;
            std::istringstream sp_in(val);
            if (sp_in >> pt.x >> pt.y >> pt.z) {
                v.spawn_points.push_back(pt);
            }
        }
    }
    v.step_sound = s.get("StepSound").value_or("");
    v.stomp_effect = s.get("StompEffect").value_or("");
    v.rider_pose = s.get("RiderPose").value_or("");
    for (const Ini::Section& section : ini.sections()) {
        if (section.name != "damage") continue;
        VehicleClass::DamageEffect d;
        d.effect = section.get("Effect").value_or("");
        d.start = section.number("Start", 100);
        d.stop = section.number("Stop", 0);
        d.at = read_vec3(section, "At", {});
        if (!d.effect.empty()) v.damage_effects.push_back(d);
    }
    v.map_texture = s.get("MapTexture").value_or("");
    v.health_texture = s.get("HealthTexture").value_or("");
    v.loadout.name = v.name;
    v.loadout.weapons = {};
    for (const Ini::Section& section : ini.sections()) {
        if (section.name != "seat") continue;
        VehicleSeat seat;
        seat.name = section.get("Name").value_or("seat");
        seat.loadout.name = v.name + "_" + seat.name;
        seat.loadout.weapons = {};
        seat.eye = read_vec3(section, "EyePointOffset", v.eye);
        if (seat.eye.y <= 0) seat.eye = v.eye;
        const auto [syaw_lo, syaw_hi] = read_pair(section, "YawLimits", {-180, 180});
        const auto [spitch_lo, spitch_hi] = read_pair(section, "PitchLimits", {-30, 30});
        seat.yaw_min = syaw_lo * degrees, seat.yaw_max = syaw_hi * degrees;
        seat.turret_node = section.get("TurretNode").value_or("");
        seat.pitch_node = section.get("PitchNode").value_or("");
        seat.pitch_min = spitch_lo * degrees, seat.pitch_max = spitch_hi * degrees;
        if (const auto tow = section.get("TowCable")) {
            std::istringstream fields(*tow);
            fields >> seat.tow_lock_range >> seat.tow_lock_time;
            seat.tow_lock_range = std::max(0.0, seat.tow_lock_range);
        }
        v.seats.push_back(std::move(seat));
    }
    std::vector<int> slots(v.seats.size() + 1, 0);
    for (size_t i = 0; i < ini.sections().size(); ++i) {
        if (ini.sections()[i].name != "weapon") continue;
        const int seat = int(ini.sections()[i].number("Seat", 0));
        if (seat < 0 || seat > int(v.seats.size()) || slots[size_t(seat)] >= hand_slots) continue;
        Loadout& loadout = seat == 0 ? v.loadout : v.seats[size_t(seat - 1)].loadout;
        loadout.weapons[size_t(slots[size_t(seat)]++)] = weapon_from_section(ini, i, where, v.name + "_weapon");
    }
    return v;
}

// A flyer. Landed (grounded) it sits at LandedHeight and, given throttle,
// lifts to TakeoffHeight in TakeoffTime and flies off at MinSpeed. Flying,
// its nose turns and pitches toward the look (TurnRate, PitchRate) and it
// goes along it at a speed between MinSpeed and MaxSpeed (forward speeds
// up, back slows; BoostSpeed on energy), never into the ground: low, slow
// and level with the throttle back, it settles and lands (ASSUMPTION).
// Hitting the ground or a wall stops it (the server reckons the damage).
std::vector<VehicleClass> load_vehicle_classes(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    if (std::filesystem::is_directory(dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ini") files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    std::vector<VehicleClass> out;
    for (const auto& file : files) {
        out.push_back(load_vehicle_class(file));
    }
    return out;
}

void simulate_flyer(MoveState& state, const MoveState& before, const MoveInput& input, const World& world,
                    const VehicleClass& vehicle, double dt, const Battlefield* field) {
    const double f = input.forward / 127.0;
    const bool space = field != nullptr && field->is_space();
    const bool in_hangar = space && field->in_landing_region(before.position);
    const double ground = world.ground_at(state.position.x, state.position.z, state.position.y, 1.5).height;
    const double speed = state.velocity.length();
    if (state.grounded) {
        state.pitch = 0;
        state.roll = 0;
        state.velocity = {};
        if (!space || in_hangar || ground > field->min_fly_y) {
            state.position.y = ground + vehicle.landed_height;
        }
        if (f > 0.1) {
            // Lifting off: up to its takeoff height, then away.
            state.grounded = false;
            state.velocity = {0, (vehicle.takeoff_height - vehicle.landed_height) / vehicle.takeoff_time, 0};
        }
        return;
    }
    // Visual roll/bank into turns.
    const double yaw_delta = wrap_angle(double(state.yaw) - double(before.yaw));
    const double yaw_vel = dt > 1e-6 ? yaw_delta / dt : 0.0;
    const double target_bank = std::clamp(-yaw_vel * 0.45 - (input.right / 127.0) * 0.35, -0.75, 0.75);
    state.roll = float(double(before.roll) + std::clamp(target_bank - double(before.roll), -2.5 * dt, 2.5 * dt));

    // Nose toward the look (deeper pitch range in 3D space).
    const double max_pitch = space ? 1.4 : 1.0;
    const double pitch_to = std::clamp(double(input.pitch), -max_pitch, max_pitch);
    const double pitch = double(before.pitch) + std::clamp(pitch_to - double(before.pitch),
                                                           -vehicle.pitch_rate * dt, vehicle.pitch_rate * dt);
    state.pitch = float(pitch);
    const bool boosting = vehicle.boost_speed > 0 && input.held(MoveInput::sprint) && state.energy > 0;
    state.sprinting = boosting;
    state.energy = boosting ? std::max(0.0, state.energy - vehicle.energy_boost_drain * dt)
                            : std::min(100.0, state.energy + vehicle.energy_restore * dt);
    const double wished = boosting ? vehicle.boost_speed
                          : f > 0.1 ? vehicle.max_speed
                          : f < -0.1 ? vehicle.min_speed
                                     : std::clamp(speed, vehicle.min_speed, vehicle.max_speed);
    const double rate = boosting ? vehicle.boost_acceleration : vehicle.acceleration;
    const bool lifting = speed < vehicle.min_speed * 0.5 && state.velocity.y > 0;  // just off the ground
    double next = lifting ? vehicle.min_speed
                          : speed + std::clamp(wished - speed, -rate * dt, rate * dt);
    const double s = std::sin(double(state.yaw)), c = std::cos(double(state.yaw));
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    Vec3 velocity{-s * cp * next, sp * next, -c * cp * next};
    Vec3 position = before.position + velocity * dt;

    if (!space) {
        const double floor = world.ground_at(position.x, position.z, position.y, 0.5).height;
        const double low = floor + vehicle.takeoff_height;
        // Low (skimming), slow and level with the throttle back: it lands.
        if (position.y < low + 0.5 && f < -0.1 && next <= vehicle.min_speed + 1 && std::abs(pitch) < 0.2) {
            position.y = floor + vehicle.landed_height;
            state.position = world.clamp(position);
            state.velocity = {};
            state.grounded = true;
            state.pitch = 0;
            state.roll = 0;
            return;
        }
        if (position.y < low) {
            // Not into the ground: it skims along over it.
            position.y = low;
            velocity.y = std::max(0.0, velocity.y);
            state.pitch = float(std::max(0.0, pitch));
        }
    } else {
        position.y = std::clamp(position.y, field->min_fly_y, field->max_fly_y);
        if (field->in_landing_region(position)) {
            const double floor = world.ground_at(position.x, position.z, position.y + 2.0, 1.5).height;
            const double low = floor + vehicle.takeoff_height;
            if (position.y - floor <= 35.0) {
                if (position.y < low + 4.0 && f < -0.1 && next <= vehicle.min_speed + 2.0 && std::abs(pitch) < 0.45) {
                    position.y = floor + vehicle.landed_height;
                    state.position = world.clamp(position);
                    state.velocity = {};
                    state.grounded = true;
                    state.pitch = 0;
                    state.roll = 0;
                    return;
                }
                if (position.y < low) {
                    position.y = low;
                    velocity.y = std::max(0.0, velocity.y);
                    state.pitch = float(std::max(0.0, pitch));
                }
            }
        }
    }

    // Check 3D solid ship hull / structure hits in space before horizontal wall resolution.
    if (space) {
        const Vec3 delta = position - before.position;
        if (delta.length() > 1e-4) {
            if (const auto hit = world.raycast_hit(before.position + Vec3{0, 0.5, 0}, position + Vec3{0, 0.5, 0})) {
                const double approach = velocity.x * hit->normal.x + velocity.y * hit->normal.y +
                                        velocity.z * hit->normal.z;
                position = before.position + delta * std::max(0.0, hit->t - 0.05) + hit->normal * 0.5;
                if (approach < -next * 0.55) {
                    velocity = {};
                } else if (approach < 0) {
                    velocity = velocity - hit->normal * approach;
                }
            }
        }
    }
    const Vec3 moved = world.resolve_walls(before.position, position, vehicle.radius(), vehicle.height(),
                                           vehicle.step_up, velocity);
    if ((moved - position).length() > 0.05) {
        velocity = {};  // ran into something: stopped (the server reckons the damage)
        next = 0;
    }
    state.position = world.clamp(moved);
    state.velocity = velocity;
    (void)next;
}

void simulate_vehicle(MoveState& state, const MoveInput& input, const World& world, const VehicleClass& vehicle,
                      double dt, const Battlefield* field) {
    const MoveState before = state;
    // Turn toward where the pilot looks, at most turn_rate.
    const double turn = std::clamp(wrap_angle(double(input.yaw) - double(state.yaw)), -vehicle.turn_rate * dt,
                                   vehicle.turn_rate * dt);
    state.yaw = float(wrap_angle(double(state.yaw) + turn));
    state.pitch = input.pitch;
    state.crouched = false;
    state.roll = 0;
    if (vehicle.kind == VehicleKind::turret) {
        state.velocity = {};
        state.grounded = true;
        state.sprinting = false;
        return;  // it stays where it stands
    }
    if (vehicle.kind == VehicleKind::flyer) {
        simulate_flyer(state, before, input, world, vehicle, dt, field);
        return;
    }
    const double s = std::sin(double(state.yaw)), c = std::cos(double(state.yaw));
    const Vec3 forward{-s, 0, -c};
    const Vec3 right{c, 0, -s};
    const double f = input.forward / 127.0, r = input.right / 127.0;

    // Boost (sprint) drains energy; energy comes back otherwise.
    const bool boosting = vehicle.boost_speed > 0 && input.held(MoveInput::sprint) && f > 0 && state.energy > 0;
    state.sprinting = boosting;
    state.energy = boosting ? std::max(0.0, state.energy - vehicle.energy_boost_drain * dt)
                            : std::min(100.0, state.energy + vehicle.energy_restore * dt);

    // Along its heading: toward the wished speed at its acceleration (its
    // deceleration with nothing pressed); sideways: toward the strafe,
    // its traction taking out any slide.
    double along = state.velocity.x * forward.x + state.velocity.z * forward.z;
    double side = state.velocity.x * right.x + state.velocity.z * right.z;
    const double wish_along = f >= 0 ? f * (boosting ? vehicle.boost_speed : vehicle.forward_speed)
                                     : f * vehicle.reverse_speed;
    const double wish_side = r * vehicle.strafe_speed;
    const auto toward = [&](double value, double target, double rate) {
        const double step = rate * dt;
        return std::abs(target - value) <= step ? target : value + (target > value ? step : -step);
    };
    const double accel = boosting ? vehicle.boost_acceleration : vehicle.acceleration;
    along = toward(along, wish_along, f == 0 || wish_along * along < 0 ? vehicle.deceleration : accel);
    side = toward(side, wish_side, r == 0 ? vehicle.traction : std::max(vehicle.traction, accel));
    Vec3 velocity = forward * along + right * side;
    velocity.y = state.velocity.y;

    if (vehicle.kind == VehicleKind::walker) {
        // On the ground as a soldier is: steps, slopes, falls.
        MoveParams params;
        params.radius = vehicle.radius();
        params.height = vehicle.height();
        params.step_up = vehicle.step_up;
        params.snap_down = vehicle.step_up;
        params.headroom = vehicle.height();
        Vec3 position = world.clamp(before.position + Vec3{velocity.x, 0, velocity.z} * dt);
        position = world.resolve_walls(before.position, position, params.radius, params.height, params.step_up,
                                       velocity);
        const World::Ground ground = world.support_at(position.x, position.z, before.position.y, params.step_up);
        if (state.grounded && position.y - ground.height <= params.snap_down) {
            position.y = ground.height;
            velocity.y = 0;
        } else {
            velocity.y -= 9.8 * dt;
            position.y += velocity.y * dt;
            state.grounded = false;
        }
        if (position.y <= ground.height) {
            position.y = ground.height;
            velocity.y = 0;
            state.grounded = true;
        }
        state.position = position;
        state.velocity = velocity;
        return;
    }

    // Hovering: its altitude over the ground (or a floor) under it; off a
    // ledge, it falls (GravityScale times gravity) until it's back over it.
    Vec3 position = world.clamp(before.position + Vec3{velocity.x, 0, velocity.z} * dt);
    position = world.resolve_walls(before.position, position, vehicle.radius(), vehicle.height(), vehicle.step_up,
                                   velocity);
    const double feet = before.position.y - vehicle.altitude;
    World::Ground ground = world.support_at(position.x, position.z, feet, vehicle.step_up);
    // Ground too steep to climb (ASSUMPTION: as a soldier's, 55 degrees):
    // it stays put across it.
    if (!ground.on_floor && ground.height > feet + 0.05 && ground.normal.y < std::cos(55 * degrees)) {
        position.x = before.position.x;
        position.z = before.position.z;
        velocity.x = velocity.z = 0;
        ground = world.support_at(position.x, position.z, feet, vehicle.step_up);
    }
    const double ride = ground.height + vehicle.altitude;
    if (state.grounded && before.position.y - ride <= vehicle.step_up) {
        position.y = ride;  // following the ground, up or down
        velocity.y = 0;
    } else {
        velocity.y -= 9.8 * vehicle.gravity_scale * dt;  // off a ledge
        position.y = before.position.y + velocity.y * dt;
        state.grounded = false;
        if (position.y <= ride) {
            position.y = ride;
            velocity.y = 0;
            state.grounded = true;
        }
    }
    state.position = position;
    state.velocity = velocity;
}

std::pair<double, double> vehicle_aim(const MoveState& state, const MoveInput& input, const VehicleClass& vehicle) {
    const double yaw = double(state.yaw) +
                       std::clamp(wrap_angle(double(input.yaw) - double(state.yaw)), vehicle.yaw_min, vehicle.yaw_max);
    const double pitch = std::clamp(double(input.pitch), vehicle.pitch_min, vehicle.pitch_max);
    return {yaw, pitch};
}

Vec3 vehicle_to_world(Vec3 position, double yaw, Vec3 local) {
    const double t = yaw + std::numbers::pi;
    const double c = std::cos(t), s = std::sin(t);
    return position + Vec3{local.x * c + local.z * s, local.y, -local.x * s + local.z * c};
}

Vec3 vehicle_muzzle(const MoveState& state, const VehicleClass& vehicle) {
    return vehicle_to_world(state.position, state.yaw, vehicle.eye);
}

const VehicleSeat* vehicle_seat(const VehicleClass& vehicle, int seat) {
    return seat >= 1 && size_t(seat) <= vehicle.seats.size() ? &vehicle.seats[size_t(seat - 1)] : nullptr;
}

std::pair<double, double> seat_aim(const MoveState& state, const MoveInput& input, const VehicleSeat& seat) {
    const double yaw = double(state.yaw) +
                       std::clamp(wrap_angle(double(input.yaw) - double(state.yaw)), seat.yaw_min, seat.yaw_max);
    const double pitch = std::clamp(double(input.pitch), seat.pitch_min, seat.pitch_max);
    return {yaw, pitch};
}

Vec3 seat_muzzle(const MoveState& state, const VehicleSeat& seat) {
    return vehicle_to_world(state.position, state.yaw, seat.eye);
}

std::optional<double> segment_hits_vehicle(Vec3 a, Vec3 b, Vec3 position, double yaw, const VehicleClass& vehicle) {
    // Slab test in the vehicle's own space.
    const Vec3 la = to_local(a, position, yaw), lb = to_local(b, position, yaw);
    const Vec3 d = lb - la;
    double t0 = 0, t1 = 1;
    const double lo[3] = {vehicle.bounds_min.x, vehicle.bounds_min.y, vehicle.bounds_min.z};
    const double hi[3] = {vehicle.bounds_max.x, vehicle.bounds_max.y, vehicle.bounds_max.z};
    const double o[3] = {la.x, la.y, la.z};
    const double v[3] = {d.x, d.y, d.z};
    for (int k = 0; k < 3; ++k) {
        if (std::abs(v[k]) < 1e-12) {
            if (o[k] < lo[k] || o[k] > hi[k]) return std::nullopt;
            continue;
        }
        double near = (lo[k] - o[k]) / v[k], far = (hi[k] - o[k]) / v[k];
        if (near > far) std::swap(near, far);
        t0 = std::max(t0, near);
        t1 = std::min(t1, far);
        if (t0 > t1) return std::nullopt;
    }
    return t0;
}

double distance_to_vehicle(Vec3 point, Vec3 position, double yaw, const VehicleClass& vehicle) {
    const Vec3 p = to_local(point, position, yaw);
    const double dx = std::max({vehicle.bounds_min.x - p.x, 0.0, p.x - vehicle.bounds_max.x});
    const double dy = std::max({vehicle.bounds_min.y - p.y, 0.0, p.y - vehicle.bounds_max.y});
    const double dz = std::max({vehicle.bounds_min.z - p.z, 0.0, p.z - vehicle.bounds_max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool towable(const VehicleClass& vehicle) {
    return vehicle.kind == VehicleKind::walker && vehicle.name.find("atat") != std::string::npos;
}

} // namespace swm::core
