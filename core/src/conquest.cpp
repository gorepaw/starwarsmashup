#include "swm/core/conquest.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "swm/core/ini.h"

namespace swm::core {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); });
}

// Source-world "x z" (Galaxies or Battlefront) to game space.
Vec3 source_point(const std::string& text, const std::string& what) {
    std::istringstream fields(text);
    double x = 0, z = 0;
    if (!(fields >> x >> z)) {
        throw std::runtime_error(what + ": expected \"x z\", got \"" + text + "\"");
    }
    return {-x, 0, z};
}

// Bounds = min_x min_z max_x max_z, in source-world coordinates.
void read_bounds(Battlefield& field, const std::string& text, const std::string& where) {
    std::istringstream fields(text);
    double x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    if (!(fields >> x0 >> z0 >> x1 >> z1) || x0 >= x1 || z0 >= z1) {
        throw std::runtime_error(where + ": Bounds is \"min_x min_z max_x max_z\"");
    }
    field.min_x = -x1;  // x runs the other way in game space
    field.max_x = -x0;
    field.min_z = z0;
    field.max_z = z1;
}

// Barrier = x1 z1 x2 z2 x3 z3 x4 z4: a quadrilateral's corners in order, in
// source-world coordinates.
void read_barriers(Battlefield& field, const Ini::Section& section, const std::string& where) {
    for (const auto& [key, value] : section.values) {
        if (!iequals(key, "Barrier")) {
            continue;
        }
        std::istringstream fields(value);
        Barrier barrier;
        for (Vec3& corner : barrier.corners) {
            double x = 0, z = 0;
            if (!(fields >> x >> z)) {
                throw std::runtime_error(where + ": Barrier is \"x1 z1 x2 z2 x3 z3 x4 z4\", got \"" + value + "\"");
            }
            corner = {-x, 0, z};
        }
        field.barriers.push_back(barrier);
    }
}

// The [post] sections of a battlefield or map file. Owner is a team's name,
// or its number (1: the first [team]).
void read_posts(Battlefield& field, const Ini& ini, const std::string& where) {
    for (const Ini::Section& s : ini.sections()) {
        if (s.name != "post") {
            continue;
        }
        CommandPost post;
        post.name = s.get("Name").value_or("Post " + std::to_string(field.posts.size() + 1));
        post.position = source_point(s.get("Position").value_or(""), where + ": post " + post.name);
        post.radius = s.number("Radius", post.radius);
        post.neutralize_time = std::max(0.1, s.number("NeutralizeTime", post.neutralize_time));
        post.capture_time = std::max(0.1, s.number("CaptureTime", post.capture_time));
        post.spawn_radius = s.number("SpawnRadius", post.spawn_radius);
        post.capturable = s.number("Capturable", 1) != 0;
        if (const auto altitude = s.get("Altitude")) {
            std::istringstream fields(*altitude);
            if (!(fields >> post.low >> post.high) || post.low > post.high) {
                throw std::runtime_error(where + ": post " + post.name + ": Altitude is \"low high\"");
            }
            post.position.y = (post.low + post.high) / 2;
        }
        if (const auto owner = s.get("Owner"); owner && !owner->empty()) {
            for (size_t t = 0; t < field.teams.size(); ++t) {
                if (iequals(*owner, field.teams[t].name) || *owner == std::to_string(t + 1)) {
                    post.owner = int(t);
                }
            }
            if (post.owner < 0) {
                throw std::runtime_error(where + ": post " + post.name + " has unknown Owner " + *owner);
            }
        }
        if (const auto console = s.get("Console")) {
            post.console = source_point(*console, where + ": post " + post.name + " Console");
            std::istringstream fields(*console);
            double x = 0, z = 0, y = 0;
            if (!(fields >> x >> z >> y)) {
                throw std::runtime_error(where + ": post " + post.name + ": Console is \"x z y\"");
            }
            post.console->y = y;
        }
        size_t heights = 0;
        for (const auto& [key, value] : s.values) {
            if (iequals(key, "SpawnPoint")) {
                Vec3 point = source_point(value, where + ": post " + post.name + " SpawnPoint");
                std::istringstream fields(value);
                double x = 0, z = 0, y = 0;
                if (fields >> x >> z >> y) {
                    point.y = y;
                    ++heights;
                }
                post.spawn_points.push_back(point);
            }
        }
        post.spawn_heights = heights > 0 && heights == post.spawn_points.size();
        for (const auto& [key, value] : s.values) {
            if (iequals(key, "AllySpawnPoint")) {
                Vec3 point = source_point(value, where + ": post " + post.name + " AllySpawnPoint");
                std::istringstream fields(value);
                double x = 0, z = 0, y = 0;
                if (!(fields >> x >> z >> y)) {
                    throw std::runtime_error(where + ": post " + post.name + ": AllySpawnPoint is \"x z y\"");
                }
                point.y = y;
                post.ally_spawn_points.push_back(point);
            }
        }
        field.posts.push_back(post);
    }
}

// Boundary = x1 z1 x2 z2 ...: a polygon, in source-world coordinates.
// Powerup = x y z radius delay health ammo: a health or ammo droid.
// DeathRegion = shape x y z sx sy sz m00 m01 ... m22: in source-world
// coordinates, its rotation's rows. DamageRegion = rate shape x y z ...: the
// same, hurting `rate` health a second.
void read_regions(Battlefield& field, const Ini::Section& section, const std::string& where) {
    for (const auto& [key, value] : section.values) {
        if (iequals(key, "Boundary")) {
            std::istringstream fields(value);
            std::vector<Vec3> polygon;
            double x = 0, z = 0;
            while (fields >> x >> z) {
                polygon.push_back({-x, 0, z});
            }
            if (polygon.size() < 3) {
                throw std::runtime_error(where + ": Boundary is \"x1 z1 x2 z2 x3 z3 ...\", got \"" + value + "\"");
            }
            field.boundaries.push_back(std::move(polygon));
        } else if (iequals(key, "Powerup")) {
            std::istringstream fields(value);
            PowerupStation station;
            if (!(fields >> station.position.x >> station.position.y >> station.position.z >> station.radius >>
                  station.delay >> station.health >> station.ammo) ||
                station.radius <= 0 || station.delay <= 0 || station.health < 0 || station.ammo < 0) {
                throw std::runtime_error(where + ": Powerup is \"x y z radius delay health ammo [max_health]\", got \"" +
                                         value + "\"");
            }
            if (double max_hp = 0; fields >> max_hp) {
                if (max_hp <= 0) {
                    throw std::runtime_error(where + ": Powerup max_health must be > 0");
                }
                station.max_health = max_hp;
            }
            station.position.x = -station.position.x;
            field.powerups.push_back(station);
        } else if (iequals(key, "Destructible")) {
            std::istringstream fields(value);
            Destructible d;
            if (!(fields >> d.max_health >> d.center.x >> d.center.y >> d.center.z >> d.half_size.x >>
                  d.half_size.y >> d.half_size.z >> d.explosion_damage >> d.explosion_inner >> d.explosion_outer >>
                  d.regen) ||
                d.max_health <= 0 || d.regen < 0) {
                throw std::runtime_error(where + ": Destructible is \"health x y z hx hy hz damage inner outer regen "
                                         "name\", got \"" + value + "\"");
            }
            std::getline(fields >> std::ws, d.name);
            d.center.x = -d.center.x;
            field.destructibles.push_back(d);
        } else if (iequals(key, "Door")) {
            std::istringstream fields(value);
            Door door;
            door.box.shape = DeathRegion::Shape::box;
            std::array<double, 9> m{};
            bool ok = bool(fields >> door.trigger.x >> door.trigger.y >> door.trigger.z >> door.radius >>
                           door.open_time >> door.box.center.x >> door.box.center.y >> door.box.center.z >>
                           door.box.size.x >> door.box.size.y >> door.box.size.z);
            for (double& entry : m) {
                ok = ok && bool(fields >> entry);
            }
            if (!ok || door.radius <= 0 || door.open_time <= 0) {
                throw std::runtime_error(where + ": Door is \"x y z radius open_time\" and a box, \"x y z sx sy sz\" "
                                         "and 9 rotation numbers, got \"" + value + "\"");
            }
            door.trigger.x = -door.trigger.x;
            door.box.center.x = -door.box.center.x;
            const double sign[3] = {-1, 1, 1};
            for (size_t row = 0; row < 3; ++row) {
                for (size_t column = 0; column < 3; ++column) {
                    door.box.basis[row * 3 + column] = sign[row] * sign[column] * m[row * 3 + column];
                }
            }
            field.doors.push_back(door);
        } else if (iequals(key, "DeathRegion") || iequals(key, "DamageRegion")) {
            std::istringstream fields(value);
            const bool damage = iequals(key, "DamageRegion");
            double rate = 0;
            std::string shape;
            DeathRegion region;
            std::array<double, 9> m{};
            bool ok = (!damage || (fields >> rate && rate > 0)) &&
                      bool(fields >> shape >> region.center.x >> region.center.y >> region.center.z >> region.size.x >>
                           region.size.y >> region.size.z);
            for (double& entry : m) {
                ok = ok && bool(fields >> entry);
            }
            if (iequals(shape, "sphere")) {
                region.shape = DeathRegion::Shape::sphere;
            } else if (iequals(shape, "cylinder")) {
                region.shape = DeathRegion::Shape::cylinder;
            } else if (!iequals(shape, "box")) {
                ok = false;
            }
            if (!ok) {
                throw std::runtime_error(where + ": " + key + " is " + (damage ? "\"rate " : "\"") +
                                         "box|sphere|cylinder x y z sx sy sz\" and 9 rotation numbers, got \"" + value +
                                         "\"");
            }
            // Mirrored like every source point (x negated): the rotation is
            // conjugated by the mirror.
            region.center.x = -region.center.x;
            const double sign[3] = {-1, 1, 1};
            for (size_t row = 0; row < 3; ++row) {
                for (size_t column = 0; column < 3; ++column) {
                    region.basis[row * 3 + column] = sign[row] * sign[column] * m[row * 3 + column];
                }
            }
            // Then: `standing <n>` (or a bare n), `timed <first> <on> <off>`.
            for (std::string word; !damage && fields >> word;) {
                if (iequals(word, "timed")) {
                    ok = ok && bool(fields >> region.first >> region.on >> region.off) && region.on > 0 &&
                         region.off >= 0;
                } else if (iequals(word, "standing")) {
                    ok = ok && bool(fields >> region.while_standing);
                } else {
                    std::istringstream(word) >> region.while_standing;
                }
            }
            if (!ok) {
                throw std::runtime_error(where + ": " + key + " has a bad standing or timed part: \"" + value + "\"");
            }
            if (damage) {
                field.damage_regions.push_back({region, rate});
            } else {
                field.death_regions.push_back(region);
            }
        }
    }
}

// Inside a polygon (x, z), by the even-odd rule.
bool inside_polygon(const std::vector<Vec3>& polygon, Vec3 point) {
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec3 a = polygon[i], b = polygon[j];
        if ((a.z > point.z) != (b.z > point.z) && point.x < (b.x - a.x) * (point.z - a.z) / (b.z - a.z) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

} // namespace

bool DeathRegion::contains(Vec3 point) const {
    // Into the region's own axes: the basis's columns.
    const Vec3 d = point - center;
    const double x = basis[0] * d.x + basis[3] * d.y + basis[6] * d.z;
    const double y = basis[1] * d.x + basis[4] * d.y + basis[7] * d.z;
    const double z = basis[2] * d.x + basis[5] * d.y + basis[8] * d.z;
    switch (shape) {
    case Shape::sphere:
        return x * x + y * y + z * z <= size.x * size.x;
    case Shape::cylinder:
        return x * x + z * z <= size.x * size.x && std::abs(y) <= size.y;
    case Shape::box:
        break;
    }
    return std::abs(x) <= size.x && std::abs(y) <= size.y && std::abs(z) <= size.z;
}

Vec3 Destructible::nearest(Vec3 point) const {
    return {std::clamp(point.x, center.x - half_size.x, center.x + half_size.x),
            std::clamp(point.y, center.y - half_size.y, center.y + half_size.y),
            std::clamp(point.z, center.z - half_size.z, center.z + half_size.z)};
}

bool Door::triggered_by(Vec3 feet) const {
    // From the nearest point of the soldier's body (feet to head): triggers
    // are mostly on the floor, some a storey up, some only 1 m across.
    constexpr double body = 1.8;
    const Vec3 nearest{feet.x, std::clamp(trigger.y, feet.y, feet.y + body), feet.z};
    return (nearest - trigger).length() <= radius;
}

std::optional<double> Door::hit(Vec3 a, Vec3 b) const {
    // Into the box's axes (the basis's columns), then through its slabs.
    const auto local = [&](Vec3 p) {
        const Vec3 d = p - box.center;
        const auto& m = box.basis;
        return std::array<double, 3>{m[0] * d.x + m[3] * d.y + m[6] * d.z, m[1] * d.x + m[4] * d.y + m[7] * d.z,
                                     m[2] * d.x + m[5] * d.y + m[8] * d.z};
    };
    const auto from = local(a), to = local(b);
    const double half[3] = {box.size.x, box.size.y, box.size.z};
    double enter = 0, leave = 1;
    for (size_t i = 0; i < 3; ++i) {
        const double d = to[i] - from[i];
        if (std::abs(d) < 1e-12) {
            if (std::abs(from[i]) > half[i]) {
                return std::nullopt;
            }
            continue;
        }
        double t0 = (-half[i] - from[i]) / d, t1 = (half[i] - from[i]) / d;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
        if (enter > leave) {
            return std::nullopt;
        }
    }
    return enter;
}

const Team& Battlefield::team(int index) const {
    if (index == natives_team && natives) {
        return natives->team;
    }
    return teams[size_t(std::clamp(index, 0, 1))];
}

bool Battlefield::allied(int a, int b) const {
    if (a < 0 || b < 0) {
        return false;
    }
    if (a == b) {
        return true;
    }
    if (!natives || (a != natives_team && b != natives_team)) {
        return false;
    }
    const int side = a == natives_team ? b : a;
    return std::find(natives->friends.begin(), natives->friends.end(), side) != natives->friends.end();
}

bool DeathRegion::on_at(double clock) const {
    if (on <= 0) {
        return true;
    }
    return clock >= first && std::fmod(clock - first, on + off) < on;
}

bool CommandPost::contains(Vec3 point) const {
    // Feet a little out of the region's height still count.
    constexpr double slack = 0.5;
    const double dx = point.x - position.x, dz = point.z - position.z;
    return dx * dx + dz * dz <= radius * radius && point.y >= low - slack && point.y <= high + slack;
}

bool Barrier::contains(Vec3 point) const {
    // Inside a convex polygon: on the same side of every edge.
    int sides = 0;
    for (size_t i = 0; i < corners.size(); ++i) {
        const Vec3 a = corners[i], b = corners[(i + 1) % corners.size()];
        const double cross = (b.x - a.x) * (point.z - a.z) - (b.z - a.z) * (point.x - a.x);
        sides |= cross > 0 ? 1 : cross < 0 ? 2 : 0;
    }
    return sides != 3;
}

bool Battlefield::contains(Vec3 point) const {
    if (point.x < min_x || point.x > max_x || point.z < min_z || point.z > max_z) {
        return false;
    }
    return boundaries.empty() || std::any_of(boundaries.begin(), boundaries.end(),
                                             [&](const std::vector<Vec3>& polygon) { return inside_polygon(polygon, point); });
}

bool Battlefield::deadly(Vec3 feet) const {
    // The middle of the body: a soldier falling into a shallow region dies
    // as it lands.
    const Vec3 body = feet + Vec3{0, 0.9, 0};
    return std::any_of(death_regions.begin(), death_regions.end(),
                       [&](const DeathRegion& region) { return region.contains(feet) || region.contains(body); });
}

double Battlefield::damage_rate(Vec3 feet) const {
    const Vec3 body = feet + Vec3{0, 0.9, 0};
    double rate = 0;
    for (const DamageRegion& region : damage_regions) {
        if (region.where.contains(feet) || region.where.contains(body)) {
            rate += region.rate;
        }
    }
    return rate;
}

bool PowerupStation::reaches(Vec3 feet) const {
    const double dx = feet.x - position.x, dz = feet.z - position.z;
    return dx * dx + dz * dz <= radius * radius && std::abs(feet.y - position.y) <= 2.0;
}

const PickupDef& pickup_def(PickupKind kind) {
    // From Battlefront II's ingame.lvl (com_inf_default and powerupitem classes).
    static constexpr std::array<PickupDef, pickup_kind_count> defs{{
        {PickupKind::health_small, "com_item_powerup_health25", "item_powerup_health25", 0.15, 100, 0, 0, 30},
        {PickupKind::health_large, "com_item_powerup_health100", "item_powerup_health100", 0.08, 200, 0, 0, 30},
        {PickupKind::ammo, "com_item_powerup_ammo", "item_powerup_ammo", 0.25, 0, 1, 0, 30},
        {PickupKind::dual, "com_item_powerup_dual", "com_item_powerup_dual", 0.08, 200, 2, 0, 30},
        {PickupKind::energy, "com_item_powerup_energy", "item_powerup_energy", 0.10, 0, 0, 150, 30},
    }};
    const size_t index = size_t(kind);
    return defs[index < defs.size() ? index : 0];
}

std::optional<PickupKind> roll_pickup(double roll) {
    if (roll < 0) {
        return std::nullopt;
    }
    double sum = 0;
    for (size_t i = 0; i < pickup_kind_count; ++i) {
        const PickupDef& def = pickup_def(PickupKind(i));
        sum += def.probability;
        if (roll < sum) {
            return def.kind;
        }
    }
    return std::nullopt;
}

Vec3 Battlefield::center() const {
    return {(min_x + max_x) / 2, 0, (min_z + max_z) / 2};
}

Battlefield load_battlefield(const std::filesystem::path& path) {
    const Ini ini = Ini::load(path);
    const std::string where = path.string();
    Battlefield field;
    const Ini::Section* main = ini.section("battlefield");
    if (main == nullptr) {
        throw std::runtime_error(where + ": no [battlefield] section");
    }
    field.name = main->get("Name").value_or(path.stem().string());
    if (const auto bounds = main->get("Bounds")) {
        read_bounds(field, *bounds, where);
    }
    field.tickets = main->number("Tickets", field.tickets);
    field.warmup = main->number("Warmup", field.warmup);
    field.results = main->number("Results", field.results);
    field.victory_time = main->number("VictoryTime", field.victory_time);
    field.bleed = main->number("Bleed", field.bleed);
    field.out_of_bounds_time = main->number("OutOfBoundsTime", field.out_of_bounds_time);
    field.min_players = size_t(std::max(1.0, main->number("MinPlayers", double(field.min_players))));
    if (const auto minimap = main->get("Minimap")) {
        std::istringstream fields(*minimap);
        double x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        if (!(fields >> x0 >> z0 >> x1 >> z1) || x0 == x1 || z0 == z1) {
            throw std::runtime_error(where + ": Minimap is \"left_x top_z right_x bottom_z\"");
        }
        field.minimap = {-x0, z0, -x1, z1};  // x runs the other way in game space
    }

    size_t team_count = 0;
    for (const Ini::Section& s : ini.sections()) {
        if (s.name == "team") {
            if (team_count == field.teams.size()) {
                throw std::runtime_error(where + ": more than two teams");
            }
            Team& team = field.teams[team_count++];
            team.name = s.get("Name").value_or("Team " + std::to_string(team_count));
            for (const auto& [key, value] : s.values) {
                if (iequals(key, "Loadout")) {
                    team.loadouts.push_back(load_loadout(path.parent_path() / value));
                }
            }
            if (team.loadouts.empty()) {
                throw std::runtime_error(where + ": team " + team.name + " has no Loadout");
            }
        }
    }
    if (team_count != field.teams.size()) {
        throw std::runtime_error(where + ": needs two [team] sections");
    }
    if (const Ini::Section* s = ini.section("natives")) {
        Natives natives;
        natives.team.name = s->get("Name").value_or("Natives");
        natives.look = s->get("Look").value_or("");
        natives.count = int(s->number("Count", 0));
        if (const auto friends = s->get("Friends")) {
            std::istringstream fields(*friends);
            for (int number = 0; fields >> number;) {
                if (number < 1 || number > 2) {
                    throw std::runtime_error(where + ": natives' Friends are team numbers, 1 or 2");
                }
                natives.friends.push_back(number - 1);
            }
        }
        for (const auto& [key, value] : s->values) {
            if (iequals(key, "Loadout")) {
                natives.team.loadouts.push_back(load_loadout(path.parent_path() / value));
            }
        }
        if (natives.count < 0 || natives.team.loadouts.empty()) {
            throw std::runtime_error(where + ": [natives] needs a Count and a Loadout");
        }
        field.natives = std::move(natives);
    }
    // A Battlefront map's posts, bounds and terrain, then any of our own.
    if (const auto map = main->get("Map")) {
        const std::filesystem::path map_path = path.parent_path() / *map;
        if (!std::filesystem::exists(map_path)) {
            throw std::runtime_error(where + ": map file " + map_path.string() +
                                     " is missing (export it with bf2tool export-map)");
        }
        const Ini map_ini = Ini::load(map_path);
        if (const Ini::Section* about = map_ini.section("map")) {
            if (const auto bounds = about->get("Bounds"); bounds && !main->get("Bounds")) {
                read_bounds(field, *bounds, map_path.string());
            }
            if (const auto terrain = about->get("Terrain")) {
                field.terrain = map_path.parent_path() / *terrain;
            }
            read_barriers(field, *about, map_path.string());
            read_regions(field, *about, map_path.string());
        }
        read_posts(field, map_ini, map_path.string());
    }
    read_posts(field, ini, where);
    read_barriers(field, *main, where);
    read_regions(field, *main, where);
    // The bounds shrink to the boundaries'.
    if (!field.boundaries.empty()) {
        double x0 = 1e18, z0 = 1e18, x1 = -1e18, z1 = -1e18;
        for (const auto& polygon : field.boundaries) {
            for (const Vec3& p : polygon) {
                x0 = std::min(x0, p.x);
                z0 = std::min(z0, p.z);
                x1 = std::max(x1, p.x);
                z1 = std::max(z1, p.z);
            }
        }
        field.min_x = std::max(field.min_x, x0);
        field.min_z = std::max(field.min_z, z0);
        field.max_x = std::min(field.max_x, x1);
        field.max_z = std::min(field.max_z, z1);
    }
    if (field.posts.empty()) {
        throw std::runtime_error(where + ": no [post] sections");
    }
    for (size_t t = 0; t < field.teams.size(); ++t) {
        if (std::none_of(field.posts.begin(), field.posts.end(), [&](const CommandPost& p) { return p.owner == int(t); })) {
            throw std::runtime_error(where + ": team " + field.teams[t].name + " starts with no post to spawn at");
        }
    }
    // Objectives: what a team wants destroyed, by name.
    for (const auto& [key, value] : main->values) {
        if (!iequals(key, "Objective")) {
            continue;
        }
        std::istringstream fields(value);
        std::string name;
        int team = 0;
        if (!(fields >> name >> team) || team < 1 || team > 2) {
            throw std::runtime_error(where + ": Objective is \"<destructible's name> <team number>\", got \"" + value +
                                     "\"");
        }
        const auto it = std::find_if(field.destructibles.begin(), field.destructibles.end(),
                                     [&](const Destructible& d) { return iequals(d.name, name); });
        if (it == field.destructibles.end()) {
            throw std::runtime_error(where + ": Objective " + name + ": no such destructible");
        }
        it->attackers.push_back(team - 1);
    }
    return field;
}

Conquest::Conquest(const Battlefield& battlefield, bool repeat) : battlefield_(battlefield), repeat_(repeat) {
    reset();
    state_.doors.assign(battlefield_.doors.size(), 0.0f);
    state_.time_left = battlefield_.warmup;
}

void Conquest::reset() {
    state_.tickets = {battlefield_.tickets, battlefield_.tickets};
    state_.dominant = -1;
    state_.victory_left = 0;
    state_.winner = -1;
    state_.posts.clear();
    for (const CommandPost& post : battlefield_.posts) {
        PostState s;
        s.owner = s.holder = int8_t(post.owner);
        s.charge = post.owner >= 0 ? 1.0f : 0.0f;
        state_.posts.push_back(s);
    }
    state_.powerup_health.clear();
    state_.powerup_health.reserve(battlefield_.powerups.size());
    for (const PowerupStation& station : battlefield_.powerups) {
        state_.powerup_health.push_back(float(station.max_health));
    }
    state_.destructibles.clear();
    for (const Destructible& d : battlefield_.destructibles) {
        state_.destructibles.push_back(float(d.max_health));
    }
}

bool Conquest::can_spawn(int team, size_t post) const {
    if (team == natives_team) {
        // At a friend's post's ally spawn points; on a map with none, at a
        // neutral post.
        if (state_.phase == Phase::ended || !battlefield_.natives || post >= state_.posts.size()) {
            return false;
        }
        const bool any_ally_points = std::any_of(battlefield_.posts.begin(), battlefield_.posts.end(),
                                                 [](const CommandPost& p) { return !p.ally_spawn_points.empty(); });
        const int owner = state_.posts[post].owner;
        if (!any_ally_points) {
            return owner < 0;
        }
        return !battlefield_.posts[post].ally_spawn_points.empty() && owner >= 0 &&
               battlefield_.allied(natives_team, owner);
    }
    return state_.phase != Phase::ended && team >= 0 && team < 2 && post < state_.posts.size() &&
           state_.posts[post].owner == team && (state_.phase != Phase::live || state_.tickets[size_t(team)] > 0);
}

bool Conquest::powerup_alive(size_t index) const {
    return index < state_.powerup_health.size() && state_.powerup_health[index] > 0.0f;
}

bool Conquest::damage_powerup(size_t index, double amount) {
    if (index >= state_.powerup_health.size() || amount <= 0 || state_.powerup_health[index] <= 0.0f) {
        return false;
    }
    state_.powerup_health[index] = float(std::max(0.0, double(state_.powerup_health[index]) - amount));
    return state_.powerup_health[index] <= 0.0f;
}

bool Conquest::destructible_alive(size_t index) const {
    return index < state_.destructibles.size() && state_.destructibles[index] > 0.0f;
}

bool Conquest::damage_destructible(size_t index, double amount) {
    if (!destructible_alive(index) || amount <= 0) {
        return false;
    }
    state_.destructibles[index] = float(std::max(0.0, double(state_.destructibles[index]) - amount));
    if (state_.destructibles[index] <= 0.0f && battlefield_.destructibles[index].regen > 0) {
        state_.destructibles[index] = -float(battlefield_.destructibles[index].max_health);  // grows back from here
    }
    return state_.destructibles[index] <= 0.0f;
}

bool Conquest::deadly(Vec3 feet) const {
    const Vec3 body = feet + Vec3{0, 0.9, 0};
    return std::any_of(battlefield_.death_regions.begin(), battlefield_.death_regions.end(), [&](const DeathRegion& r) {
        return (r.while_standing < 0 || destructible_alive(size_t(r.while_standing))) && r.on_at(clock_) &&
               (r.contains(feet) || r.contains(body));
    });
}

bool Conquest::door_shut(size_t index) const {
    return index < state_.doors.size() && state_.doors[index] < 0.5f;
}

std::optional<double> Conquest::door_hit(Vec3 a, Vec3 b) const {
    std::optional<double> nearest;
    for (size_t i = 0; i < battlefield_.doors.size(); ++i) {
        if (door_shut(i)) {
            if (const auto t = battlefield_.doors[i].hit(a, b); t && (!nearest || *t < *nearest)) {
                nearest = t;
            }
        }
    }
    return nearest;
}

void Conquest::mend(double dt) {
    for (size_t i = 0; i < battlefield_.destructibles.size() && i < state_.destructibles.size(); ++i) {
        const Destructible& d = battlefield_.destructibles[i];
        float& health = state_.destructibles[i];
        if (d.regen <= 0 || health >= float(d.max_health)) {
            continue;
        }
        if (health <= 0.0f) {
            // Destroyed: growing back; whole, it stands again.
            health = std::min(0.0f, health + float(d.regen * dt));
            if (health >= 0.0f) {
                health = float(d.max_health);
            }
        } else {
            health = std::min(float(d.max_health), health + float(d.regen * dt));
        }
    }
}

void Conquest::move_doors(double dt, std::span<const Unit> units) {
    for (size_t i = 0; i < battlefield_.doors.size() && i < state_.doors.size(); ++i) {
        const Door& door = battlefield_.doors[i];
        const bool open = std::any_of(units.begin(), units.end(),
                                      [&](const Unit& unit) { return door.triggered_by(unit.position); });
        const double step = dt / door.open_time;
        state_.doors[i] = float(std::clamp(state_.doors[i] + (open ? step : -step), 0.0, 1.0));
    }
}

void Conquest::killed(int team) {
    if (state_.phase == Phase::live && team >= 0 && team < 2) {
        state_.tickets[size_t(team)] = std::max(0.0, state_.tickets[size_t(team)] - 1);
    }
}

std::vector<MatchEvent> Conquest::step(double dt, std::span<const Unit> units, size_t players) {
    std::vector<MatchEvent> events;
    clock_ += dt;
    state_.clock = clock_;
    move_doors(dt, units);
    mend(dt);
    switch (state_.phase) {
    case Phase::warmup:
        if (players < battlefield_.min_players) {
            state_.time_left = battlefield_.warmup;  // waiting for players
            break;
        }
        state_.time_left -= dt;
        if (state_.time_left <= 0) {
            reset();
            state_.phase = Phase::live;
            state_.time_left = 0;
            events.push_back({MatchEvent::went_live});
        }
        break;
    case Phase::live: {
        capture(dt, units, events);
        std::array<int, 2> held{};
        for (const PostState& post : state_.posts) {
            if (post.owner >= 0) {
                ++held[size_t(post.owner)];
            }
        }
        // The team holding more posts bleeds the other's reinforcements.
        // (Ours: Battlefront's bleed comes from per-map scripts.)
        const int lead = held[0] - held[1];
        if (lead != 0) {
            double& tickets = state_.tickets[lead > 0 ? 1 : 0];
            tickets = std::max(0.0, tickets - battlefield_.bleed * std::abs(lead) * dt);
        }
        if (state_.tickets[0] <= 0 || state_.tickets[1] <= 0) {
            const int winner = state_.tickets[0] > 0 ? 0 : state_.tickets[1] > 0 ? 1 : -1;
            finish(winner, events);
            break;
        }
        // Holding every post starts the countdown to victory.
        const int total = int(state_.posts.size());
        const int dominant = held[0] == total ? 0 : held[1] == total ? 1 : -1;
        if (dominant != state_.dominant) {
            state_.dominant = int8_t(dominant);
            state_.victory_left = battlefield_.victory_time;
        }
        if (dominant >= 0) {
            state_.victory_left -= dt;
            if (state_.victory_left <= 0) {
                finish(dominant, events);
            }
        }
        break;
    }
    case Phase::ended:
        state_.time_left = std::max(0.0, state_.time_left - dt);
        if (state_.time_left <= 0 && repeat_) {
            reset();
            state_.phase = Phase::warmup;
            state_.time_left = battlefield_.warmup;
            events.push_back({MatchEvent::restarted});
        }
        break;
    }
    return events;
}

void Conquest::finish(int winner, std::vector<MatchEvent>& events) {
    state_.phase = Phase::ended;
    state_.winner = int8_t(winner);
    state_.dominant = -1;
    state_.victory_left = 0;
    state_.time_left = battlefield_.results;
    events.push_back({MatchEvent::ended, winner});
}

void Conquest::capture(double dt, std::span<const Unit> units, std::vector<MatchEvent>& events) {
    for (size_t i = 0; i < state_.posts.size(); ++i) {
        const CommandPost& def = battlefield_.posts[i];
        PostState& post = state_.posts[i];
        std::array<int, 2> present{};
        for (const Unit& unit : units) {
            if (unit.team >= 0 && unit.team < 2 && def.contains(unit.position)) {
                ++present[size_t(unit.team)];
            }
        }
        post.contested = present[0] > 0 && present[1] > 0;
        if (!def.capturable || post.contested || (present[0] == 0 && present[1] == 0)) {
            continue;  // a post nobody is in keeps its charge
        }
        const int team = present[0] > 0 ? 0 : 1;
        const double soldiers = std::min(present[size_t(team)], max_capturing_soldiers);
        if (post.owner == team) {
            post.charge = float(std::min(1.0, post.charge + soldiers * dt / def.neutralize_time));  // repairing
        } else if (post.owner >= 0) {
            post.charge = float(post.charge - soldiers * dt / def.neutralize_time);
            if (post.charge <= 0) {
                post.owner = post.holder = -1;
                post.charge = 0;
                events.push_back({MatchEvent::neutralized, team, int(i)});
            }
        } else if (post.holder >= 0 && post.holder != team) {
            post.charge = float(post.charge - soldiers * dt / def.capture_time);  // undoing the other team's capture
            if (post.charge <= 0) {
                post.holder = -1;
                post.charge = 0;
            }
        } else {
            post.holder = int8_t(team);
            post.charge = float(post.charge + soldiers * dt / def.capture_time);
            if (post.charge >= 1) {
                post.owner = int8_t(team);
                post.charge = 1;
                events.push_back({MatchEvent::captured, team, int(i)});
            }
        }
    }
}

} // namespace swm::core
