#include "swm/core/hub.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "swm/core/ini.h"
#include "swm/core/net/udp.h"
#include "swm/core/world.h"

namespace swm::core {

namespace {

bool same_name(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); });
}

std::string trimmed(std::string_view text) {
    const size_t first = text.find_first_not_of(" \t");
    const size_t last = text.find_last_not_of(" \t");
    return first == std::string_view::npos ? std::string() : std::string(text.substr(first, last - first + 1));
}

std::string file_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// A hub file and its towns' files (Town = <file> in [hub], relative to it)
// after it, read as one (Stage 13).
Ini load_hub_ini(const std::filesystem::path& path) {
    Ini ini = Ini::load(path);
    const Ini::Section* main = ini.section("hub");
    if (main == nullptr) {
        return ini;
    }
    std::string own = file_text(path);
    // BattlefieldsFrom = <hub file>: another planet's hub's battles (its
    // Battlefield, Sides, TeamSize, QueueTime and ReturnDelay), so each
    // planet's hub offers the same ones (Stage 13).
    if (const auto from = main->get("BattlefieldsFrom")) {
        const Ini other = Ini::load(path.parent_path() / *from);
        std::string lines;
        if (const Ini::Section* theirs = other.section("hub")) {
            for (const auto& [key, value] : theirs->values) {
                for (const char* wanted : {"Battlefield", "Sides", "TeamSize", "QueueTime", "ReturnDelay"}) {
                    if (same_name(key, wanted)) {
                        lines += key + " = " + value + "\n";
                    }
                }
            }
        }
        const size_t header = own.find("[hub]");
        const size_t line_end = own.find('\n', header);
        own.insert(line_end == std::string::npos ? own.size() : line_end + 1, lines);
    }
    std::string text;
    for (const auto& [key, value] : main->values) {
        if (same_name(key, "Town")) {
            text += "\n" + file_text(path.parent_path() / value);
        }
    }
    return Ini::parse(own + text);
}

} // namespace

std::array<int, 2> teams_for_sides(const Battlefield& field, const std::array<std::string, 2>& sides) {
    std::array<int, 2> teams{};
    for (size_t side = 0; side < 2; ++side) {
        const auto it = std::find_if(field.teams.begin(), field.teams.end(),
                                     [&](const Team& team) { return same_name(team.name, sides[side]); });
        if (it == field.teams.end()) {
            throw std::runtime_error(std::format("{} has no team named {}", field.name, sides[side]));
        }
        teams[side] = int(it - field.teams.begin());
    }
    if (teams[0] == teams[1]) {
        throw std::runtime_error(std::format("{}: both sides are its {}", field.name, sides[0]));
    }
    return teams;
}

HubConfig load_hub(const std::filesystem::path& path) {
    const Ini ini = load_hub_ini(path);
    const std::string where = path.string();
    const Ini::Section* main = ini.section("hub");
    if (main == nullptr) {
        throw std::runtime_error(where + ": no [hub] section");
    }
    HubConfig hub;
    hub.name = main->get("Name").value_or(path.stem().string());
    if (const auto spawn = main->get("Spawn")) {
        std::istringstream fields(*spawn);
        double x = 0, z = 0;
        if (!(fields >> x >> z)) {
            throw std::runtime_error(where + ": Spawn needs x z");
        }
        hub.spawn = Vec3{-x, 0, z};
    }
    if (const auto cloning = main->get("CloningFacility")) {
        std::istringstream fields(*cloning);
        double x = 0, z = 0;
        if (!(fields >> x >> z)) {
            throw std::runtime_error(where + ": CloningFacility needs x z");
        }
        hub.cloning_facility = Vec3{-x, 0, z};
    }
    for (const auto& [key, value] : main->values) {
        if (!same_name(key, "Battlefield")) {
            continue;
        }
        const std::filesystem::path battlefield_path = path.parent_path() / value;
        HubBattlefield battlefield{battlefield_path.stem().string(), load_battlefield(battlefield_path)};
        battlefield.path = battlefield_path;
        hub.battlefields.push_back(std::move(battlefield));
    }
    if (hub.battlefields.empty()) {
        throw std::runtime_error(where + ": no Battlefield");
    }
    // Sides = Rebellion, Empire [, Republic, Separatists]; by default the first battlefield's teams.
    if (const auto sides = main->get("Sides")) {
        std::string_view rest = *sides;
        while (!rest.empty()) {
            const size_t comma = rest.find(',');
            const std::string side = trimmed(rest.substr(0, comma));
            if (!side.empty()) hub.sides.push_back(side);
            rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
        }
        if (hub.sides.size() != 2 && hub.sides.size() != 4) {
            throw std::runtime_error(where + ": Sides needs 2 or 4 comma-separated names");
        }
    } else {
        hub.sides = {hub.battlefields[0].field.teams[0].name, hub.battlefields[0].field.teams[1].name};
    }
    for (HubBattlefield& battlefield : hub.battlefields) {
        if (hub.sides.size() == 2) {
            try {
                battlefield.teams = teams_for_sides(battlefield.field, {hub.sides[0], hub.sides[1]});
                battlefield.sides = {0, 1};
            } catch (const std::runtime_error& error) {
                throw std::runtime_error(where + ": " + error.what());
            }
        } else {
            // Find which pair of hub sides fights on this battlefield (e.g. GCW or Clone Wars).
            int found_count = 0;
            for (int si = 0; si < int(hub.sides.size()) && found_count < 2; ++si) {
                const auto it = std::find_if(battlefield.field.teams.begin(), battlefield.field.teams.end(),
                                             [&](const Team& team) { return same_name(team.name, hub.sides[size_t(si)]); });
                if (it != battlefield.field.teams.end()) {
                    battlefield.sides[size_t(found_count)] = si;
                    battlefield.teams[size_t(found_count)] = int(it - battlefield.field.teams.begin());
                    ++found_count;
                }
            }
            if (found_count != 2 || battlefield.teams[0] == battlefield.teams[1]) {
                throw std::runtime_error(
                    std::format("{}: {}'s teams ({} / {}) do not match hub Sides", where, battlefield.field.name,
                                battlefield.field.teams[0].name, battlefield.field.teams[1].name));
            }
        }
    }
    hub.team_size = std::clamp(int(main->number("TeamSize", hub.team_size)), 1, 32);
    hub.queue_time = std::max(0.0, main->number("QueueTime", hub.queue_time));
    hub.return_delay = std::max(0.0, main->number("ReturnDelay", hub.return_delay));
    // Its people. A look per template, in the order they come.
    const auto look_of = [&](const std::string& template_name) {
        const auto it = std::find(hub.looks.begin(), hub.looks.end(), template_name);
        if (it != hub.looks.end()) {
            return int(it - hub.looks.begin());
        }
        if (hub.looks.size() >= 255) {
            throw std::runtime_error(where + ": more than 255 kinds of people");
        }
        hub.looks.push_back(template_name);
        return int(hub.looks.size() - 1);
    };
    const auto point = [&](std::string_view text, const std::string& what) {
        std::istringstream fields{std::string(text)};
        double x = 0, z = 0;
        if (!(fields >> x >> z)) {
            throw std::runtime_error(where + ": " + what + " needs x z");
        }
        return Vec3{-x, 0, z};  // Galaxies to game coordinates
    };
    for (const Ini::Section& s : ini.sections()) {
        if (s.name == "npc") {
            HubPerson person;
            const auto template_name = s.get("Template");
            if (!template_name) {
                throw std::runtime_error(where + ": an [npc] needs a Template");
            }
            person.look = look_of(*template_name);
            person.name = s.get("Name").value_or("Citizen");
            person.position = point(s.get("Position").value_or(""), "an [npc]'s Position");
            person.position.y = s.number("Height", 0.0);  // a floor's, indoors (absolute)
            // Galaxies' yaw faces +Z at 0 and turns toward +X; the game's faces -Z.
            person.yaw = std::numbers::pi - s.number("Yaw", 0) * std::numbers::pi / 180;
            person.pose = same_name(s.get("Pose").value_or("stand"), "sit") ? 1 : 0;
            person.radius = std::clamp(s.number("Radius", 0), 0.0, 5.0);
            if (const auto route = s.get("Route")) {
                std::string_view rest = *route;
                while (!rest.empty()) {
                    const size_t semicolon = rest.find(';');
                    const std::string_view part = rest.substr(0, semicolon);
                    if (part.find_first_not_of(" \t") != std::string_view::npos) {
                        person.route.push_back(point(part, "a Route's point"));
                    }
                    rest = semicolon == std::string_view::npos ? std::string_view() : rest.substr(semicolon + 1);
                }
            }
            hub.people.push_back(std::move(person));
        } else if (s.name == "quartermaster") {
            net::Quartermaster qm;
            qm.name = s.get("Name").value_or("Quartermaster");
            qm.faction = s.get("Faction").value_or("neutral");
            qm.position = point(s.get("Position").value_or(""), "a [quartermaster]'s Position");
            qm.radius = s.number("Radius", qm.radius);
            hub.quartermasters.push_back(std::move(qm));
        } else if (s.name == "recruiter") {
            net::Recruiter r;
            r.name = s.get("Name").value_or("War Terminal");
            r.position = point(s.get("Position").value_or(""), "a [recruiter]'s Position");
            r.position.y = s.number("Height", 0.0);
            r.radius = s.number("Radius", r.radius);
            // Type = ground (Ground Battle Terminal), space (Space Terminal) or
            // both; by default from its name.
            const std::string type = s.get("Type").value_or("");
            if (same_name(type, "space")) {
                r.kind = net::Recruiter::space;
            } else if (same_name(type, "both")) {
                r.kind = net::Recruiter::both;
            } else if (same_name(type, "ground")) {
                r.kind = net::Recruiter::ground;
            } else if (!type.empty()) {
                throw std::runtime_error(where + ": a [recruiter]'s Type is ground, space or both");
            } else {
                r.kind = r.name.find("Space") != std::string::npos ? net::Recruiter::space : net::Recruiter::ground;
            }
            hub.recruiters.push_back(std::move(r));
        } else if (s.name == "mission_terminal") {
            net::MissionTerminal t;
            t.name = s.get("Name").value_or("Mission Terminal");
            t.kind = s.get("Type").value_or("general");
            t.position = point(s.get("Position").value_or(""), "a [mission_terminal]'s Position");
            t.position.y = s.number("Height", 0.0);
            t.radius = s.number("Radius", 3);
            hub.mission_terminals.push_back(std::move(t));
        } else if (s.name == "workbench") {
            net::Workbench wb;
            wb.name = s.get("Name").value_or("Workbench");
            wb.kind = s.get("Type").value_or("general");
            wb.position = point(s.get("Position").value_or(""), "a [workbench]'s Position");
            wb.position.y = s.number("Height", 0.0);
            wb.radius = s.number("Radius", 4);
            hub.workbenches.push_back(std::move(wb));
        } else if (s.name == "vendor") {
            net::Vendor v;
            v.name = s.get("Name").value_or("Vendor");
            v.kind = s.get("Type").value_or("gear_vendor");
            v.position = point(s.get("Position").value_or(""), "a [vendor]'s Position");
            v.position.y = s.number("Height", 0.0);
            v.radius = s.number("Radius", v.radius);
            hub.vendors.push_back(std::move(v));
        } else if (s.name == "service") {
            // A service point: Type = trainer (Profession = ids, comma-separated), bank, clinic, cloning,
            // bazaar, newsnet, gambling or data_terminal.
            net::Vendor v;
            v.name = s.get("Name").value_or("Service");
            v.kind = s.get("Type").value_or("bank");
            v.detail = s.get("Profession").value_or(s.get("Detail").value_or(""));
            v.position = point(s.get("Position").value_or(""), "a [service]'s Position");
            v.position.y = s.number("Height", 0.0);
            v.radius = s.number("Radius", v.radius);
            hub.vendors.push_back(std::move(v));
        } else if (s.name == "target") {
            HubRangeTarget rt;
            rt.name = s.get("Name").value_or("Target");
            rt.position = point(s.get("Position").value_or(""), "a [target]'s Position");
            rt.patrol_end = s.get("Patrol") ? point(*s.get("Patrol"), "a [target]'s Patrol") : rt.position;
            rt.speed = s.number("Speed", 0.0);
            rt.health = s.number("Health", 400.0);
            rt.mitigation = s.number("Mitigation", 0.0);
            rt.respawn = s.number("Respawn", 2.5);
            hub.range_targets.push_back(std::move(rt));
        } else if (s.name == "crowd") {
            HubCrowd crowd;
            crowd.count = std::clamp(int(s.number("Count", 0)), 0, 200);
            for (const auto& [key, value] : s.values) {
                if (same_name(key, "Template")) {
                    crowd.looks.push_back(look_of(value));
                }
            }
            std::istringstream area(s.get("Area").value_or(""));
            double x = 0, z = 0, radius = 0;
            if (crowd.count > 0 && (!(area >> x >> z >> radius) || radius <= 0 || crowd.looks.empty())) {
                throw std::runtime_error(where + ": a [crowd] needs Area = x z radius and a Template");
            }
            crowd.center = {-x, 0, z};
            crowd.radius = radius;
            if (hub.crowds.empty()) {
                hub.crowd = crowd;
            }
            hub.crowds.push_back(std::move(crowd));
        } else if (s.name == "town") {
            // Another of the planet's towns (Stage 13).
            HubTown town;
            town.name = s.get("Name").value_or("Town");
            town.center = point(s.get("Center").value_or(""), "a [town]'s Center");
            town.radius = std::max(50.0, s.number("Radius", town.radius));
            for (const auto& [key, value] : s.values) {
                if (same_name(key, "CloningFacility")) {
                    town.cloning.push_back(point(value, "a [town]'s CloningFacility"));
                }
            }
            hub.towns.push_back(std::move(town));
        }
    }
    if (hub.recruiters.empty()) {
        throw std::runtime_error(where + ": no [recruiter] sections");
    }
    // The hub's own town, first: its center (else where players start), its
    // radius and cloning facilities.
    {
        HubTown own;
        own.name = hub.name;
        if (const auto center = main->get("Center")) {
            own.center = point(*center, "Center");
        } else if (hub.spawn) {
            own.center = *hub.spawn;
        }
        own.radius = std::max(50.0, main->number("Radius", 700));
        for (const auto& [key, value] : main->values) {
            if (same_name(key, "CloningFacility")) {
                own.cloning.push_back(point(value, "CloningFacility"));
            }
        }
        hub.towns.insert(hub.towns.begin(), std::move(own));
    }
    if (const auto spawns = main->get("Spawns")) {
        hub.spawns = load_world_spawns(path.parent_path() / *spawns);
        for (const CreatureArchetype& c : hub.spawns->creatures) {
            look_of(c.template_path);
        }
        for (const BanditArchetype& b : hub.spawns->bandits) {
            look_of(b.template_path);
        }
    }
    if (const auto quests = main->get("Quests")) {
        hub.quests.load(path.parent_path() / *quests);
    }
    if (const auto loot = main->get("LootTables")) {
        hub.loot_tables.load(path.parent_path() / *loot);
    }
    if (const auto res = main->get("Resources")) {
        hub.crafting.load_resources(path.parent_path() / *res);
    }
    if (const auto sch = main->get("Schematics")) {
        hub.crafting.load_schematics(path.parent_path() / *sch);
    }
    return hub;
}

std::unique_ptr<net::Link> UdpHubNetwork::open_server(uint16_t& port) {
    auto link = std::make_unique<net::UdpLink>();
    port = link->port();
    return link;
}

std::unique_ptr<net::Link> UdpHubNetwork::open_client() {
    return std::make_unique<net::UdpLink>();
}

net::Endpoint UdpHubNetwork::local(uint16_t port) const {
    return {0x7F000001, port};  // 127.0.0.1
}

namespace {

// Where to look for the floor under something a hub places: under its
// Height where the ini gives one (indoors, upstairs); else a little over the
// terrain (at least 7.8 m on Mos Eisley's low ground, for its raised floors),
// so on Deeja Peak's heights or by Kaadara's shore (-192 m) it's the ground
// floor, not a roof.
double floor_reach(const World& world, const Vec3& p) {
    if (p.y != 0.0) {
        return p.y;
    }
    const double terrain = world.terrain().height_at(p.x, p.z);
    return terrain >= 0 && terrain < 6.0 ? 7.8 : terrain + 1.8;
}

} // namespace

Hub::Hub(GameServer& server, uint16_t port, const World& world, HubConfig config, HubNetwork& network)
    : server_(server), port_(port), world_(world), config_(std::move(config)), network_(network),
      bot_roster_(build_stock_bot_roster(server.catalog())) {
    server_.set_pvp_enabled(false);
    if (config_.cloning_facility) {
        server_.set_cloning_facility(*config_.cloning_facility);
    }
    // Every town's cloning facilities: the dead are cloned at the nearest (Stage 13).
    std::vector<std::pair<std::string, Vec3>> facilities;
    for (const HubTown& town : config_.towns) {
        for (const Vec3& at : town.cloning) {
            facilities.push_back({town.name + " Cloning Facility", at});
        }
    }
    if (!facilities.empty()) {
        server_.set_cloning_facilities(std::move(facilities));
    }
    const auto snap_floor = [&](Vec3& p) {
        const double reach = floor_reach(world_, p);
        p = world_.clamp(p);
        const World::Ground g = world_.ground_at(p.x, p.z, reach, 0.5);
        p.y = g.on_floor ? g.height : world_.terrain().height_at(p.x, p.z);
        // A terminal set against a wall has its middle in the wall, a little
        // past its room's floor (Mos Eisley's Imperial mission terminal, 5 cm
        // past a house's living room, over its cellar 10 m down): where the
        // floor under it is a storey or more down, the floor it's against.
        if (g.on_floor && p.y < reach - 3.0) {
            constexpr double against = 0.5;  // m
            double best = p.y;
            for (int i = 0; i < 8; ++i) {
                const double a = i * std::numbers::pi / 4;
                const World::Ground near =
                    world_.ground_at(p.x + against * std::cos(a), p.z + against * std::sin(a), reach, 0.5);
                if (near.on_floor && near.height > best + 2.5) {
                    best = near.height;
                }
            }
            p.y = best;
        }
    };
    for (net::Recruiter& recruiter : config_.recruiters) {
        snap_floor(recruiter.position);
    }
    server_.set_recruiters(config_.recruiters, config_.sides);
    for (net::Quartermaster& qm : config_.quartermasters) {
        snap_floor(qm.position);
    }
    server_.set_quartermasters(config_.quartermasters);
    for (net::Vendor& v : config_.vendors) {
        snap_floor(v.position);
    }
    server_.set_vendors(config_.vendors);
    // A terminal or crafting station is a Galaxies object with its own
    // collision: the floor under its middle is its own top (a crafting
    // station's 2.1-2.9 m up), so its symbol floated a station's height too
    // high. It stands on the floor around it: where, a little way off on
    // most sides, the floor is lower (by less than a station is tall), that
    // (the highest of those). Its symbol is placed by its model, measured
    // (scenes/hub_ui.gd), so this needn't find its foot exactly.
    const auto snap_object = [&](Vec3& p) {
        snap_floor(p);
        constexpr double off = 1.8;  // m: past a station's or terminal's footprint
        int lower = 0;
        double base = -1e9;
        for (const auto& [dx, dz] : {std::pair{off, 0.0}, {-off, 0.0}, {0.0, off}, {0.0, -off}}) {
            const World::Ground g = world_.ground_at(p.x + dx, p.z + dz, p.y + 0.1, 0.5);
            const double floor = g.on_floor ? g.height : world_.terrain().height_at(p.x + dx, p.z + dz);
            // (No lower than a station is tall: not a cellar under its building.)
            if (floor < p.y - 0.4 && floor > p.y - 3.2) {
                ++lower;
                base = std::max(base, floor);
            }
        }
        if (lower >= 3) {
            p.y = base;
        }
    };
    for (net::MissionTerminal& t : config_.mission_terminals) {
        snap_object(t.position);
    }
    for (net::Workbench& wb : config_.workbenches) {
        snap_object(wb.position);
    }
    server_.set_crafting(&config_.crafting, config_.workbenches);
    if (config_.spawns) {
        const Vec3 origin = config_.spawn ? *config_.spawn
                            : !config_.recruiters.empty() ? config_.recruiters[0].position
                                                          : Vec3{};
        config_.quests.set_world(&*config_.spawns, origin);
        std::vector<std::pair<std::string, Vec3>> towns;
        for (const HubTown& town : config_.towns) {
            towns.push_back({town.name, town.center});
        }
        config_.quests.set_towns(std::move(towns));
    }
    for (const HubRangeTarget& rt : config_.range_targets) {
        server_.add_range_target(rt.name, rt.position, rt.patrol_end, rt.speed, rt.health, rt.mitigation, rt.respawn);
    }
    load_battle_world = [](const Battlefield& field) -> std::shared_ptr<const World> {
        return field.terrain.empty() ? nullptr : load_world(field.terrain);
    };
    server_.on_queue = [this](uint32_t entity, int team, uint8_t mode, uint8_t terminal) {
        queue(entity, team, mode, terminal);
    };
    server_.on_join = [this](uint32_t entity, const std::string& name) {
        if (auto it = notices_.find(name); it != notices_.end()) {
            server_.notify(entity, it->second);
            notices_.erase(it);
        }
    };
    server_.on_npc_killed = [this](uint32_t npc_entity, uint32_t killer_entity) {
        if (!population_ || killer_entity == 0) return;
        const int64_t xp = population_->xp_for(npc_entity);
        if (xp > 0) {
            server_.award_xp(killer_entity, xp);
        }
        const std::string table_id = population_->loot_table_for(npc_entity);
        const RolledLoot rolled = table_id.empty() ? RolledLoot{} : config_.loot_tables.roll(table_id, loot_rng_);
        if (rolled.credits > 0) {
            server_.award_credits(killer_entity, rolled.credits);
        }
        const auto info = server_.npc_info(npc_entity);
        if (info && !rolled.items.empty()) {
            std::vector<net::LootItemEntry> entries;
            entries.reserve(rolled.items.size());
            for (size_t i = 0; i < rolled.items.size() && i < 250; ++i) {
                entries.push_back({uint8_t(i), rolled.items[i].def_id, float(rolled.items[i].quality),
                                   uint8_t(rolled.items[i].mark)});
            }
            server_.set_corpse_loot(npc_entity, killer_entity, info->name, info->state.position, std::move(entries));
        }
        if (info) {
            std::string msg = std::format("Defeated {}", info->name);
            if (xp > 0 && rolled.credits > 0) {
                msg += std::format(" (+{} XP, +{} Cr)", xp, rolled.credits);
            } else if (xp > 0) {
                msg += std::format(" (+{} XP)", xp);
            } else if (rolled.credits > 0) {
                msg += std::format(" (+{} Cr)", rolled.credits);
            }
            if (!rolled.items.empty()) {
                msg += " — Press E on corpse to loot";
            }
            server_.notify(killer_entity, msg);
            if (const KillTags tags = population_->kill_tags(npc_entity); !tags.type_id.empty()) {
                server_.quest_kill(killer_entity, tags, info->state.position);
            }
        }
    };
}

namespace {

constexpr int8_t walking = 25;        // of 127: about 1.4 m/s, a walk
constexpr double arrived = 0.6;       // meters from a corner
constexpr double stuck_after = 4;     // seconds without getting anywhere

double yaw_to(Vec3 d) {
    return std::atan2(-d.x, -d.z);  // 0 faces -Z, positive turns left
}

} // namespace

void Hub::add_people() {
    if (config_.people.empty() && config_.crowds.empty() && !config_.spawns) {
        return;
    }
    server_.set_looks(config_.looks);
    // Where they walk: the streets around the crowd's area, every route, and camps/lairs.
    Battlefield streets;
    streets.name = config_.name + " streets";
    double lo_x = 1e30, lo_z = 1e30, hi_x = -1e30, hi_z = -1e30;
    const auto grow = [&](Vec3 p, double r) {
        lo_x = std::min(lo_x, p.x - r), hi_x = std::max(hi_x, p.x + r);
        lo_z = std::min(lo_z, p.z - r), hi_z = std::max(hi_z, p.z + r);
    };
    for (const HubCrowd& crowd : config_.crowds) {
        if (crowd.count > 0) {
            grow(crowd.center, crowd.radius + 20);
        }
    }
    for (const HubPerson& person : config_.people) {
        grow(person.position, 20);
        for (const Vec3& p : person.route) {
            grow(p, 20);
        }
    }
    if (config_.spawns) {
        for (const LairSpawn& l : config_.spawns->lairs) grow(l.position, l.radius + 100);
        for (const CampSpawn& c : config_.spawns->camps) grow(c.position, c.radius + 100);
    }
    if (lo_x > hi_x) {
        lo_x = lo_z = -500;
        hi_x = hi_z = 500;
    }
    streets.min_x = lo_x, streets.max_x = hi_x, streets.min_z = lo_z, streets.max_z = hi_z;
    streets_ = std::make_unique<NavGrid>(world_, streets);

    // Who quests send players to talk to: their givers and the people in their steps.
    std::vector<std::string> quest_people = config_.quests.givers();
    for (const QuestDef& def : config_.quests.quests()) {
        for (const QuestStep& step : def.steps) {
            if (!step.npc.empty()) quest_people.push_back(step.npc);
        }
    }
    const std::vector<std::string> givers = config_.quests.givers();
    std::vector<net::QuestNpc> quest_npcs;
    for (const HubPerson& person : config_.people) {
        Vec3 at = person.position;
        const double reach = floor_reach(world_, at);
        const World::Ground g = world_.ground_at(at.x, at.z, reach, 0.5);
        at.y = g.on_floor ? g.height : world_.terrain().height_at(at.x, at.z);
        const uint32_t entity =
            server_.add_npc(person.name, at, person.yaw, uint8_t(person.look + 1), person.pose, 0, person.radius);
        const auto named = [&](const std::string& name) { return name == person.name; };
        if (std::any_of(quest_people.begin(), quest_people.end(), named) &&
            std::none_of(quest_npcs.begin(), quest_npcs.end(),
                         [&](const net::QuestNpc& n) { return n.name == person.name; })) {
            quest_npcs.push_back({entity, person.name, at, std::any_of(givers.begin(), givers.end(), named)});
        }
        if (person.route.size() >= 1) {
            walkers_.push_back({.entity = entity, .route = person.route});
        } else {
            ++standing_;
        }
    }
    for (size_t c = 0; c < config_.crowds.size(); ++c)
    for (int i = 0; i < config_.crowds[c].count; ++i) {
        const HubCrowd& crowd = config_.crowds[c];
        const auto at = wander_point(c);
        if (!at) {
            break;
        }
        const int look = crowd.looks[size_t(i) % crowd.looks.size()];
        const double yaw = std::uniform_real_distribution<double>(-std::numbers::pi, std::numbers::pi)(random_);
        // Each in colors of their own (the set pieces keep their looks').
        const auto colors = uint16_t(std::uniform_int_distribution<int>(1, 65535)(random_));
        const uint32_t entity = server_.add_npc("Citizen", *at, yaw, uint8_t(look + 1), 0, colors);
        // Some linger a while before they set off.
        Walker walker{.entity = entity, .crowd = c};
        walker.pause_until = std::uniform_real_distribution<double>(0, 10)(random_);
        walkers_.push_back(walker);
    }
    say(std::format("{} people in {}: {} walking", walkers_.size() + standing_, config_.name, walkers_.size()));
    for (const std::string& name : quest_people) {
        if (std::none_of(quest_npcs.begin(), quest_npcs.end(), [&](const net::QuestNpc& n) { return n.name == name; })) {
            say(std::format("quests name {}, who isn't one of {}'s people", name, config_.name));
        }
    }
    if (!config_.quests.quests().empty() || !config_.mission_terminals.empty()) {
        say(std::format("{} quest(s), {} quest people, {} mission terminal(s)", config_.quests.quests().size(),
                        quest_npcs.size(), config_.mission_terminals.size()));
        server_.set_quests(&config_.quests, std::move(quest_npcs), config_.mission_terminals);
    }
    if (config_.spawns) {
        std::map<std::string, uint8_t> look_map;
        for (size_t i = 0; i < config_.looks.size(); ++i) {
            look_map[config_.looks[i]] = uint8_t(i + 1);
        }
        population_ = std::make_unique<WorldPopulation>(server_, world_, streets_.get(), *config_.spawns,
                                                        server_.catalog(), look_map);
        population_->spawn_all();
        say(std::format("populated world with {} creature(s) and {} bandit(s)", population_->creature_count(),
                        population_->bandit_count()));
    }
}

// Somewhere in the crowd's area to walk to: on the ground (not a roof),
// where a soldier could stand.
std::optional<Vec3> Hub::wander_point(size_t crowd_index) {
    if (crowd_index >= config_.crowds.size()) {
        return std::nullopt;
    }
    const HubCrowd& crowd = config_.crowds[crowd_index];
    const MoveParams params;
    std::uniform_real_distribution<double> unit(0, 1);
    for (int attempt = 0; attempt < 60; ++attempt) {
        const double angle = unit(random_) * 2 * std::numbers::pi;
        const double r = crowd.radius * std::sqrt(unit(random_));
        Vec3 p{crowd.center.x + r * std::cos(angle), 0, crowd.center.z + r * std::sin(angle)};
        const double ground = world_.terrain().height_at(p.x, p.z);
        p.y = world_.support_at(p.x, p.z, ground + 1, params.step_up).height;
        if (p.y > ground - 0.5 && p.y < ground + 1 && streets_->walkable(p)) {
            return p;
        }
    }
    return std::nullopt;
}

void Hub::walk_people() {
    for (Walker& walker : walkers_) {
        const MoveState* state = server_.state_of(walker.entity);
        if (state == nullptr) {
            continue;
        }
        if (!server_.npc_awake_at(state->position)) {
            // No one near: it stands where it is (unsimulated), and isn't stuck.
            walker.checked = state->position;
            walker.checked_at = now_;
            continue;
        }
        MoveInput input;
        input.yaw = state->yaw;
        if (now_ < walker.pause_until) {
            server_.steer_npc(walker.entity, input);
            continue;
        }
        // Somewhere to go: the crowd anywhere in its area, a route its next point.
        if (walker.next >= walker.path.size()) {
            std::optional<Vec3> goal;
            if (walker.route.empty()) {
                goal = wander_point(walker.crowd);
            } else {
                goal = walker.route[walker.route_index % walker.route.size()];
                ++walker.route_index;
            }
            walker.path = goal ? streets_->find_path(state->position, *goal) : std::vector<Vec3>{};
            walker.next = 0;
            walker.checked = state->position;
            walker.checked_at = now_;
            if (walker.path.empty()) {
                walker.pause_until = now_ + 2;
                server_.steer_npc(walker.entity, input);
                continue;
            }
        }
        Vec3 to = walker.path[walker.next] - state->position;
        to.y = 0;
        if (to.length() < arrived) {
            if (++walker.next >= walker.path.size()) {
                // There: linger (the crowd longer than a route's walker).
                const double low = walker.route.empty() ? 4 : 2, high = walker.route.empty() ? 15 : 5;
                walker.pause_until = now_ + std::uniform_real_distribution<double>(low, high)(random_);
                server_.steer_npc(walker.entity, input);
                continue;
            }
            to = walker.path[walker.next] - state->position;
            to.y = 0;
        }
        // Not getting anywhere (someone in the way): try somewhere else soon.
        if (now_ - walker.checked_at > stuck_after) {
            Vec3 moved = state->position - walker.checked;
            moved.y = 0;
            if (moved.length() < 0.5) {
                walker.path.clear();
                walker.pause_until = now_ + 1;
            }
            walker.checked = state->position;
            walker.checked_at = now_;
        }
        input.yaw = float(yaw_to(to));
        input.forward = walking;
        server_.steer_npc(walker.entity, input);
    }
}

Hub::~Hub() {
    server_.on_queue = nullptr;
    server_.on_join = nullptr;
    for (auto& battle : battles_) {
        battle->bots->disconnect();
    }
}

void Hub::say(const std::string& text) const {
    if (log) {
        log(text);
    }
}

std::pair<int, int> Hub::queue_pair_for_side(int side) const {
    for (const HubBattlefield& bf : config_.battlefields) {
        if (bf.sides[0] == side || bf.sides[1] == side) {
            return {std::min(bf.sides[0], bf.sides[1]), std::max(bf.sides[0], bf.sides[1])};
        }
    }
    if (config_.sides.size() >= 4 && side >= 2) {
        return {2, 3};
    }
    return {0, 1};
}

void Hub::queue(uint32_t entity, int team, uint8_t mode, uint8_t terminal) {
    for (auto& [key, q] : queues_) {
        std::erase_if(q, [&](const Queued& entry) { return entry.entity == entity; });
    }
    if (net::PlayerStatus* status = server_.status(entity)) {
        status->queue = net::PlayerStatus::none;
        status->team = -1;
        status->in_queue = 0;
        status->starts_in = 0;
        status->battlefield.clear();
    }
    const std::string name = server_.name_of(entity);
    const int max_side = std::max(0, int(config_.sides.size()) - 1);
    if (team >= 0 && !config_.sides.empty()) {
        const int side = std::clamp(team, 0, max_side);
        QueueKey key;
        key.sides = queue_pair_for_side(side);
        // A Space Terminal queues for space battles; a ground terminal for
        // ground ones; one doing both, by the mode asked for.
        key.space = terminal == net::Recruiter::space ||
                    (terminal == net::Recruiter::both && mode == uint8_t(GameMode::space_assault));
        key.mode = key.space ? uint8_t(GameMode::space_assault)
                   : mode == uint8_t(GameMode::space_assault) ? uint8_t(GameMode::conquest)
                                                              : mode;
        const bool offered = std::any_of(config_.battlefields.begin(), config_.battlefields.end(),
                                         [&](const HubBattlefield& bf) { return offers(bf, key); });
        if (!offered) {
            const std::string what =
                key.mode == net::QueueRequest::any_mode ? std::string("battles")
                                                        : std::string(game_mode_name(GameMode(key.mode)));
            server_.notify(entity, std::format("No {} {} for the {} here", key.space ? "space" : "ground", what,
                                               config_.sides[size_t(side)]));
            say(std::format("{} can't queue: no {} {} for the {}", name, key.space ? "space" : "ground", what,
                            config_.sides[size_t(side)]));
            return;
        }
        queues_[key].push_back({entity, name, side, now_});
        say(std::format("{} queued for the {} ({}, {})", name, config_.sides[size_t(side)],
                        key.space ? "space" : "ground",
                        key.mode == net::QueueRequest::any_mode ? std::string_view("any mode")
                                                                : game_mode_name(GameMode(key.mode))));
    } else {
        say(std::format("{} left the queue", name));
    }
}

bool Hub::has_room(const Battle& battle, int side) const {
    const MatchState& match = battle.server->match()->state();
    int team = -1;
    if (battle.field.sides[0] == side) team = battle.field.teams[0];
    else if (battle.field.sides[1] == side) team = battle.field.teams[1];
    if (team < 0) return false;
    return battle.over_since < 0 && match.phase != Phase::ended &&
           battle.players[size_t(team)] < battle.team_size;
}

bool Hub::players_connected(const Battle& battle) const {
    for (const uint32_t entity : battle.server->entities()) {
        const std::string name = battle.server->name_of(entity);
        for (const auto& [ticket, player] : battle.tickets) {
            if (player.first == name) {
                return true;
            }
        }
    }
    return false;
}

void Hub::send(Battle& battle, const Queued& player) {
    uint64_t ticket = 0;
    while (ticket == 0 || ticket == battle.bot_ticket || battle.tickets.contains(ticket)) {
        ticket = random_();
    }
    const int team = player.team == battle.field.sides[1] ? battle.field.teams[1] : battle.field.teams[0];
    battle.tickets[ticket] = {player.name, team};
    if (const auto prof = server_.profile_of(player.entity)) {
        battle.profiles[player.name] = *prof;
    }
    std::vector<std::string> worn;
    auto loadouts = server_.build_player_loadouts(player.entity, true, &worn);
    if (!loadouts.empty()) {
        battle.player_loadouts[player.name] = std::move(loadouts);
        battle.player_worn[player.name] = std::move(worn);
    }
    ++battle.soldiers[size_t(team)];
    ++battle.players[size_t(team)];
    server_.transfer(player.entity, net::Transfer{0, battle.port, ticket, battle.field.id});
    if (net::PlayerStatus* status = server_.status(player.entity)) {
        status->queue = net::PlayerStatus::none;
        status->team = -1;
        status->in_queue = 0;
        status->starts_in = 0;
        status->battlefield.clear();
    }
    say(std::format("sent {} to battle {} on {} for the {}", player.name, battle.id, battle.field.field.name,
                    config_.sides[size_t(player.team)]));
}

bool Hub::offers(const HubBattlefield& bf, const QueueKey& key) {
    const auto& sides = bf.sides;
    const std::pair<int, int> pair{std::min(sides[0], sides[1]), std::max(sides[0], sides[1])};
    if (pair != key.sides || bf.field.is_space() != key.space) {
        return false;
    }
    return key.space || key.mode == net::QueueRequest::any_mode ||
           std::find(bf.field.modes.begin(), bf.field.modes.end(), GameMode(key.mode)) != bf.field.modes.end();
}

void Hub::choose_next(const QueueKey& key) {
    std::vector<size_t> choices;
    for (size_t i = 0; i < config_.battlefields.size(); ++i) {
        if (offers(config_.battlefields[i], key) && config_.battlefields[i].id != last_battlefield_) {
            choices.push_back(i);
        }
    }
    if (choices.empty()) {
        for (size_t i = 0; i < config_.battlefields.size(); ++i) {
            if (offers(config_.battlefields[i], key)) {
                choices.push_back(i);
            }
        }
    }
    while (!choices.empty()) {
        const size_t pick = std::uniform_int_distribution<size_t>(0, choices.size() - 1)(random_);
        const size_t index = choices[pick];
        const HubBattlefield& bf = config_.battlefields[index];
        GameMode mode = bf.field.mode;
        if (!key.space) {
            mode = key.mode != net::QueueRequest::any_mode
                       ? GameMode(key.mode)
                       : bf.field.modes[std::uniform_int_distribution<size_t>(0, bf.field.modes.size() - 1)(random_)];
        }
        Next next;
        next.battlefield = index;
        next.chosen = bf;
        next.chosen.id = battlefield_id(bf.id, mode);
        if (mode != bf.field.mode) {
            try {
                next.chosen.field = load_battlefield(bf.path, mode);
            } catch (const std::exception& error) {
                say(std::format("can't set up {} for {}: {}", bf.field.name, game_mode_name(mode), error.what()));
                choices.erase(choices.begin() + std::ptrdiff_t(pick));
                continue;
            }
        }
        const HubBattlefield& chosen = next.chosen;
        if (const auto it = worlds_.find(bf.id); it != worlds_.end() && !it->second.expired()) {
            std::promise<std::shared_ptr<const World>> loaded;
            loaded.set_value(it->second.lock());
            next.world = loaded.get_future().share();
        } else {
            next.world = std::async(std::launch::async, load_battle_world, chosen.field).share();
        }
        say(std::format("next battle on {} ({})", chosen.field.name, game_mode_name(chosen.field.mode)));
        next_[key] = std::move(next);
        return;
    }
}

bool Hub::next_ready(const QueueKey& key) {
    const auto nit = next_.find(key);
    if (nit == next_.end() || nit->second.world.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }
    try {
        nit->second.world.get();
        return true;
    } catch (const std::exception& error) {
        // Off the list; the queue waits for another.
        say(std::format("can't load {}: {}", config_.battlefields[nit->second.battlefield].field.name, error.what()));
        config_.battlefields.erase(config_.battlefields.begin() + std::ptrdiff_t(nit->second.battlefield));
        next_.erase(nit);
        return false;
    }
}

void Hub::start_battle(const QueueKey& key) {
    auto nit = next_.find(key);
    if (nit == next_.end()) return;
    auto& queue = queues_[key];
    auto battle = std::make_unique<Battle>();
    Battle* b = battle.get();
    b->field = nit->second.chosen;
    b->world = nit->second.world.get();
    const std::string file_id = config_.battlefields[nit->second.battlefield].id;
    next_.erase(nit);
    last_battlefield_ = file_id;
    if (b->world) {
        worlds_[file_id] = b->world;
    }
    b->key = key;
    b->team_size = b->field.field.team_size > 0 ? b->field.field.team_size : config_.team_size;
    b->started = now_;
    const World& world = b->world ? *b->world : world_;
    b->id = next_battle_++;
    b->link = network_.open_server(b->port);
    b->bot_ticket = random_() | 1;
    b->last_player = now_;
    ServerConfig config;
    config.battlefield = b->field.field;
    config.single_match = true;
    config.professions = server_.profession_book();
    config.specialists = server_.specialist_book();  // Stage 12: abilities and companions in battle
    b->server = std::make_unique<GameServer>(*b->link, world, config);
    b->server->log = [this, id = b->id](const std::string& line) { say(std::format("battle {}: {}", id, line)); };
    b->server->on_join = [b](uint32_t entity, const std::string& name) {
        if (const auto it = b->profiles.find(name); it != b->profiles.end()) {
            b->server->set_player_profile(entity, it->second);
        }
        if (const auto lit = b->player_loadouts.find(name); lit != b->player_loadouts.end()) {
            const auto wit = b->player_worn.find(name);
            b->server->set_player_loadouts(entity, lit->second, wit != b->player_worn.end() ? wit->second : std::vector<std::string>{});
        }
    };
    // Players with their ticket, on their side; bots wherever there's room.
    b->server->admit = [b](const std::string& name, uint64_t ticket, int asked) -> std::optional<int> {
        if (ticket == b->bot_ticket && asked == natives_team) {
            return natives_team;  // a native: not a side's soldier
        }
        if (ticket == b->bot_ticket) {
            const int team = (asked == 0 || asked == 1) ? asked : (b->soldiers[0] <= b->soldiers[1] ? 0 : 1);
            ++b->soldiers[size_t(team)];
            return team;
        }
        const auto it = b->tickets.find(ticket);
        if (it != b->tickets.end() && it->second.first == name) {
            return it->second.second;
        }
        return std::nullopt;
    };
    // Leave Match (the pause menu): back here at once, the match going on without them.
    b->server->on_leave_match = [this, b](uint32_t entity) {
        const std::string name = b->server->name_of(entity);
        if (std::any_of(b->tickets.begin(), b->tickets.end(),
                        [&](const auto& t) { return t.second.first == name; })) {
            b->server->transfer(entity, net::Transfer{0, port_, 0, "", config_.planet});
            say(std::format("{} left battle {}", name, b->id));
        }
    };
    b->server->on_match_ended = [this, b](const MatchResult& result) {
        last_result_ = result;
        record_telemetry(*b, result);
        for (const PlayerResult& player : result.players) {
            const bool sent = std::any_of(b->tickets.begin(), b->tickets.end(),
                                          [&](const auto& t) { return t.second.first == player.name; });
            if (!sent) {
                continue;  // a bot
            }
            const char* outcome = result.winner < 0 ? "Draw" : result.winner == player.team ? "Victory" : "Defeat";
            const BattleReward reward = compute_battle_reward(
                result.winner == player.team, result.winner < 0, player.kills, player.captures);
            const std::string fought_faction =
                (player.team == 0 || player.team == 1) ? b->field.field.teams[size_t(player.team)].name : std::string();
            server_.award_battle_rewards(player.name, fought_faction, reward);
            server_.award_profession_xp(player.name, player.profession_xp, reward.xp);
            server_.apply_match_ammo_consumption(player.name, player.remaining_std_ammo, player.remaining_hvy_ammo);
            const auto count = [](int n, const char* one, const char* many) {
                return std::format("{} {}", n, n == 1 ? one : many);
            };
            notices_[player.name] = std::format("{} at {}: {}, {}, {} (+{} FP, +{} Cr, +{} XP)", outcome,
                                                result.battlefield, count(player.kills, "kill", "kills"),
                                                count(player.deaths, "death", "deaths"),
                                                count(player.captures, "capture", "captures"),
                                                reward.faction_points, reward.credits, reward.xp);
        }
    };

    // Send the queue, up to the team size per side.
    std::vector<Queued> waiting;
    std::swap(waiting, queue);
    for (const Queued& player : waiting) {
        if (has_room(*b, player.team)) {
            send(*b, player);
        } else {
            queue.push_back(player);
        }
    }
    const int bots = std::max(0, 2 * b->team_size - b->players[0] - b->players[1]);
    ClientConfig bot_config;
    bot_config.server = network_.local(b->port);
    bot_config.battlefield = b->field.field;
    bot_config.ticket = b->bot_ticket;
    std::array<int, 2> assigned = b->soldiers;
    std::array<size_t, 2> faction_cursor{0, 0};
    std::array<std::vector<const BotProfileEntry*>, 2> pools{
        roster_for_faction(bot_roster_, b->field.field.teams[0].name),
        roster_for_faction(bot_roster_, b->field.field.teams[1].name),
    };
    std::vector<ClientConfig> bot_configs;
    bot_configs.reserve(size_t(bots));
    for (int i = 0; i < bots; ++i) {
        const int team = assigned[0] <= assigned[1] ? 0 : 1;
        ++assigned[size_t(team)];
        ClientConfig cfg = bot_config;
        cfg.team = team;
        const auto& pool = pools[size_t(team)];
        if (!pool.empty()) {
            const BotProfileEntry* picked = nullptr;
            std::string chosen_name;
            for (size_t attempt = 0; attempt < pool.size(); ++attempt) {
                const BotProfileEntry* cand = pool[(faction_cursor[size_t(team)] + attempt) % pool.size()];
                const bool name_used =
                    b->profiles.contains(cand->name) ||
                    std::any_of(bot_configs.begin(), bot_configs.end(),
                                [&](const ClientConfig& existing) { return existing.name == cand->name; });
                if (!name_used) {
                    picked = cand;
                    chosen_name = cand->name;
                    faction_cursor[size_t(team)] += attempt + 1;
                    break;
                }
            }
            if (picked == nullptr) {
                picked = pool[faction_cursor[size_t(team)]++ % pool.size()];
                chosen_name = std::format("{} #{}", picked->name, i + 1);
            }
            cfg.name = std::move(chosen_name);
            b->profiles[cfg.name] = PlayerProfile{picked->species, picked->gender, picked->appearance};
            b->player_loadouts[cfg.name] = picked->loadouts;
            b->player_worn[cfg.name] = picked->worn_per_loadout;
        } else {
            cfg.name = std::format("Bot {}", i + 1);
        }
        bot_configs.push_back(std::move(cfg));
    }
    b->bots = std::make_unique<BotPlayers>(bot_configs, world, bot_config, [this] { return network_.open_client(); },
                                           config_.bot_skill);
    say(std::format("battle {} on {} ({}): port {}, {} players, {} bots", b->id, b->field.field.name,
                    game_mode_name(b->field.field.mode), b->port, b->players[0] + b->players[1], bots));
    battles_.push_back(std::move(battle));
}

void Hub::record_telemetry(const Battle& battle, const MatchResult& result) {
    MatchTelemetry t;
    t.battlefield = battle.field.id;
    t.map = result.battlefield;
    t.mode = std::string(game_mode_id(result.mode));
    t.teams = result.teams;
    t.era = era_of(result.teams);
    t.space = battle.field.field.is_space();
    t.winner = result.winner;
    t.tickets = result.tickets;
    t.scores = result.scores;
    t.duration = result.duration;
    t.captures = result.captures;
    for (const PlayerResult& player : result.players) {
        MatchParticipant p;
        p.name = player.name;
        p.team = player.team;
        p.bot = !std::any_of(battle.tickets.begin(), battle.tickets.end(),
                             [&](const auto& ticket) { return ticket.second.first == player.name; });
        p.kills = player.kills;
        p.deaths = player.deaths;
        p.captures = player.captures;
        p.primary_weapon = player.primary_weapon;
        p.weapon_kills = player.weapon_kills;
        t.participants.push_back(std::move(p));
    }
    telemetry_.push_back(t);
    if (telemetry_.size() > 64) {
        telemetry_.erase(telemetry_.begin());
    }
    if (CharacterStore* store = server_.store()) {
        try {
            store->record_match(t);
        } catch (const std::exception& error) {
            say(std::format("can't record match telemetry: {}", error.what()));
        }
    }
    say(std::format("telemetry: {} {} {} ({}), winner {}, {:.0f} s, {} players, {} bots", t.map, t.era, t.mode,
                    t.battlefield, t.winner < 0 ? std::string("draw") : t.teams[size_t(t.winner)], t.duration,
                    t.player_count(), t.bot_count()));
}

void Hub::update(double now) {
    now_ = now;
    if (!people_added_) {
        people_added_ = true;  // on the first update, so the log says so
        add_people();
    }
    // People and creatures are steered once a server tick (Stage 13): the
    // server only reads their input when it ticks, and this is called far
    // more often than that (every pass of the server's loop).
    if (server_.tick() != steered_tick_) {
        steered_tick_ = server_.tick();
        walk_people();
        if (population_) {
            population_->update(now);
        }
    }
    // Players who left the hub leave the queue.
    std::vector<QueueKey> active_keys;
    for (auto& [key, queue] : queues_) {
        std::erase_if(queue, [&](const Queued& q) { return server_.name_of(q.entity) != q.name; });
        std::vector<Queued> waiting;
        std::swap(waiting, queue);
        for (const Queued& player : waiting) {
            auto open = std::find_if(battles_.begin(), battles_.end(),
                                     [&](const auto& b) { return b->key == key && has_room(*b, player.team); });
            if (open != battles_.end()) {
                send(**open, player);
            } else {
                queue.push_back(player);
            }
        }
        if (!queue.empty()) {
            active_keys.push_back(key);
        } else {
            next_.erase(key);
        }
    }
    for (const QueueKey& key : active_keys) {
        auto& queue = queues_[key];
        if (!next_.contains(key)) {
            choose_next(key);
        }
        if (!queue.empty() && now - queue.front().since >= config_.queue_time && next_ready(key)) {
            start_battle(key);
        }
        const auto nit = next_.find(key);
        for (const Queued& player : queue) {
            if (net::PlayerStatus* status = server_.status(player.entity)) {
                status->queue = net::PlayerStatus::queued;
                status->team = int8_t(player.team);
                status->in_queue = uint16_t(queue.size());
                status->starts_in = float(std::max(0.0, config_.queue_time - (now - queue.front().since)));
                status->battlefield =
                    nit != next_.end() ? std::format("{} ({})", nit->second.chosen.field.name,
                                                     game_mode_name(nit->second.chosen.field.mode))
                                       : std::string();
            }
        }
    }

    for (auto& battle : battles_) {
        battle->server->update(now);
        battle->bots->update(now);
        if (players_connected(*battle)) {
            battle->last_player = now;
        }
        if (battle->over_since < 0 && battle->server->match_over()) {
            battle->over_since = now;
            for (const uint32_t entity : battle->server->entities()) {
                const std::string name = battle->server->name_of(entity);
                if (std::any_of(battle->tickets.begin(), battle->tickets.end(),
                                [&](const auto& t) { return t.second.first == name; })) {
                    battle->server->transfer(entity, net::Transfer{0, port_, 0, "", config_.planet});
                }
            }
            say(std::format("battle {} is over: sending everyone back", battle->id));
        }
    }
    std::erase_if(battles_, [&](const std::unique_ptr<Battle>& battle) {
        const bool done = battle->over_since >= 0 && now - battle->over_since >= config_.return_delay;
        const bool abandoned = now - battle->last_player > 30;
        if (done || abandoned) {
            say(std::format("battle {} closed{}", battle->id, abandoned && !done ? " (no players left)" : ""));
            battle->bots->disconnect();
        }
        return done || abandoned;
    });
}

void Hub::set_bot_skill(BotSkill skill) {
    config_.bot_skill = skill;
    for (const auto& battle : battles_) {
        if (battle->bots) {
            battle->bots->set_skill(skill);
        }
    }
}

} // namespace swm::core
