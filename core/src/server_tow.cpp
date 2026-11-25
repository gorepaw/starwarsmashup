// Stage 11: the snowspeeder's tow cable. Its rear gunner (a seat with
// TowCable: Battlefront's towcableweapon) harpoons an enemy AT-AT within the
// weapon's lock range while the snowspeeder flies; the pilot then circles
// the walker, winding the cable round its legs. Wound tow_wraps_to_trip
// times, the walker trips and falls (destroyed, credited to the gunner).
// The cable snaps if the snowspeeder strays past tow_max_length, lands or is
// destroyed, if the walker falls some other way, or after tow_max_time.

#include <cmath>
#include <format>
#include <numbers>

#include "swm/core/server.h"

namespace swm::core {

namespace {

double bearing(Vec3 from, Vec3 to) {
    return std::atan2(to.x - from.x, to.z - from.z);
}

// The shortest signed turn from a to b (radians).
double turn(double a, double b) {
    double d = b - a;
    while (d > std::numbers::pi) d -= 2 * std::numbers::pi;
    while (d < -std::numbers::pi) d += 2 * std::numbers::pi;
    return d;
}

} // namespace

void GameServer::fire_tow_cable(Client& gunner, const VehicleSeat& seat) {
    Vehicle* speeder = vehicle_gunned_by(gunner.entity);
    if (speeder == nullptr || speeder->towing != 0 || speeder->state.grounded) {
        return;  // one cable at a time, and only in the air
    }
    if (conquest_ && conquest_->state().phase == Phase::ended) {
        return;
    }
    const auto classes = vehicle_classes();
    Vehicle* target = nullptr;
    double best = seat.tow_lock_range;
    for (Vehicle& v : vehicles_) {
        if (v.dead_for >= 0 || size_t(v.cls) >= classes.size() || !towable(classes[size_t(v.cls)]) ||
            allied(v.team, speeder->team)) {
            continue;
        }
        const Vec3 d = v.state.position - speeder->state.position;
        const double distance = d.length();
        if (distance <= best) {
            best = distance;
            target = &v;
        }
    }
    if (target == nullptr) {
        notify(gunner.entity, "Tow cable: no AT-AT in range");
        return;
    }
    speeder->towing = target->id;
    speeder->tow_gunner = gunner.entity;
    speeder->tow_bearing = bearing(target->state.position, speeder->state.position);
    speeder->tow_wound = 0;
    speeder->tow_time = 0;
    notify(gunner.entity, "Tow cable attached: circle its legs!");
    if (speeder->driver != 0) {
        notify(speeder->driver, "Tow cable attached: circle the AT-AT!");
    }
    say(std::format("{} harpooned an AT-AT", gunner.name));
}

void GameServer::update_tow_cables(double dt) {
    const auto classes = vehicle_classes();
    for (Vehicle& speeder : vehicles_) {
        if (speeder.towing == 0) {
            continue;
        }
        Vehicle* walker = nullptr;
        for (Vehicle& v : vehicles_) {
            if (v.id == speeder.towing) walker = &v;
        }
        const auto release = [&](const char* why) {
            if (why != nullptr && speeder.tow_gunner != 0) {
                notify(speeder.tow_gunner, why);
            }
            speeder.towing = speeder.tow_gunner = 0;
            speeder.tow_wound = speeder.tow_time = 0;
        };
        if (speeder.dead_for >= 0) {
            release(nullptr);
            continue;
        }
        if (walker == nullptr || walker->dead_for >= 0) {
            release("Tow cable released");
            continue;
        }
        speeder.tow_time += dt;
        const double distance = (walker->state.position - speeder.state.position).length();
        if (speeder.state.grounded || distance > tow_max_length || speeder.tow_time > tow_max_time) {
            release("Tow cable snapped");
            continue;
        }
        const double now = bearing(walker->state.position, speeder.state.position);
        speeder.tow_wound += turn(speeder.tow_bearing, now);
        speeder.tow_bearing = now;
        if (std::abs(speeder.tow_wound) < tow_wraps_to_trip * 2 * std::numbers::pi) {
            continue;
        }
        // Wound: the walker trips and falls.
        const uint32_t credit = speeder.tow_gunner != 0 ? speeder.tow_gunner : speeder.driver;
        if (credit != 0) {
            notify(credit, "The AT-AT is down!");
        }
        if (speeder.driver != 0 && speeder.driver != credit) {
            notify(speeder.driver, "The AT-AT is down!");
        }
        say("an AT-AT was tripped by a tow cable");
        release(nullptr);
        std::vector<Blast> blasts;
        damage_vehicle(*walker, walker->health + 1, credit, blasts);
        for (size_t b = 0; b < blasts.size(); ++b) {
            const Blast blast = blasts[b];  // explode() may add more
            explode(blast, blasts);
        }
    }
}

} // namespace swm::core
