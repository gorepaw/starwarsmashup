#include "swm_net.h"

#include <exception>
#include <algorithm>
#include <filesystem>
#include <format>
#include <limits>
#include <string>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "swm/core/species.h"
#include "swm/core/specialist.h"

namespace swm::godot_bindings {

namespace {

std::string to_std(const godot::String& text) {
    return text.utf8().get_data();
}

godot::Vector3 to_godot(core::Vec3 v) {
    return {v.x, v.y, v.z};
}

godot::Dictionary to_dictionary(const core::MoveState& state) {
    godot::Dictionary out;
    out["position"] = to_godot(state.position);
    out["velocity"] = to_godot(state.velocity);
    out["yaw"] = double(state.yaw);
    out["pitch"] = double(state.pitch);
    out["grounded"] = state.grounded;
    out["energy"] = state.energy;
    out["sprinting"] = state.sprinting;
    out["crouched"] = state.crouched;
    out["roll"] = double(state.roll);          // seconds of rolling left (0: not rolling)
    out["roll_yaw"] = double(state.roll_yaw);  // the way it rolls
    out["blocking"] = state.blocking;
    out["stagger"] = double(state.stagger);
    out["vehicle"] = int64_t(state.vehicle);    // the vehicle class it rides in; -1 on foot
    out["seat"] = int64_t(state.seat);          // 0 driving; 1.. a gunner's seat
    const core::CombatState& c = state.combat;
    out["weapon"] = int64_t(c.weapon);
    out["rounds"] = int64_t(c.weapons[c.weapon].rounds);
    out["reserve"] = int64_t(c.weapons[c.weapon].reserve);
    const core::WeaponState& grenades = c.weapons[core::secondary_slot];
    out["grenades"] = int64_t(grenades.rounds + std::max<int>(0, grenades.reserve));
    out["heat"] = double(c.weapons[c.weapon].heat);
    out["overheated"] = c.weapons[c.weapon].overheated;
    out["reloading"] = c.reload > 0;
    return out;
}

const char* life_name(core::Life life) {
    switch (life) {
    case core::Life::waiting:
        return "waiting";
    case core::Life::alive:
        return "alive";
    case core::Life::dead:
        return "dead";
    }
    return "";
}

// What snapshots say about an entity, besides its state.
void describe(godot::Dictionary& out, const core::net::EntityState& entity) {
    out["health"] = double(entity.health);
    out["max_health"] = double(entity.max_health);
    out["shield"] = double(entity.shield);
    out["max_shield"] = double(entity.max_shield);
    out["alive"] = entity.alive();
    out["life"] = life_name(entity.life);
    out["team"] = int64_t(entity.team);
    out["loadout"] = int64_t(entity.loadout);
    out["kills"] = int64_t(entity.kills);
    out["deaths"] = int64_t(entity.deaths);
    out["captures"] = int64_t(entity.captures);
    out["look"] = int64_t(entity.look);  // 0: a player; else get_looks()[look - 1]
    out["pose"] = int64_t(entity.pose);  // 0 on their feet, 1 sitting, 2 attacking
    out["actor_kind"] = int64_t(entity.actor_kind);  // 0 player/bot, 1 citizen, 2 creature, 3 bandit
    out["passive"] = entity.passive;
    out["colors"] = int64_t(entity.colors);  // 0: the look's own colors; else a seed to vary them by
    out["species"] = godot::String::utf8(core::species_def(core::Species(entity.species)).id.c_str());
    out["gender"] = godot::String::utf8(entity.gender == 1 ? "female" : "male");
    out["name"] = godot::String::utf8(entity.name.c_str());
    out["appearance"] = godot::String::utf8(entity.appearance.c_str());
    out["worn"] = godot::String::utf8(entity.worn.c_str());
}

godot::Color to_color(const std::array<double, 4>& c) {
    return godot::Color(float(c[0]), float(c[1]), float(c[2]), float(c[3]));
}

void add_bolt_look(godot::Dictionary& entry, const core::WeaponDef& w) {
    entry["color"] = to_color(w.bolt_color);
    entry["length"] = w.bolt_length;
    entry["width"] = w.bolt_width;
    entry["texture"] = godot::String::utf8(w.bolt_texture.c_str());
    entry["glow_length"] = w.glow_length;
    entry["blur_length"] = w.blur_length;
    entry["light_color"] = to_color(w.light_color);
    entry["light_radius"] = w.light_radius;
    entry["model"] = godot::String::utf8(w.ordnance_model.c_str());
    entry["trail_effect"] = godot::String::utf8(w.trail_effect.c_str());
    entry["muzzle_flash"] = godot::String::utf8(w.muzzle_flash.c_str());
    entry["flash_color"] = to_color(w.flash_color);
    entry["flash_length"] = w.flash_length;
    entry["flash_light_color"] = to_color(w.flash_light_color);
    entry["flash_light_radius"] = w.flash_light_radius;
    entry["flash_light_duration"] = w.flash_light_duration;
    entry["fire_sound"] = godot::String::utf8(w.fire_sound.c_str());
    entry["ordnance_sound"] = godot::String::utf8(w.ordnance_sound.c_str());
    entry["bounce_sound"] = godot::String::utf8(w.bounce_sound.c_str());
}

godot::Error load_battlefield_file(std::optional<core::Battlefield>& battlefield, const godot::String& path) {
    try {
        battlefield = core::load_battlefield(to_std(path));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("set_battlefield: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    return godot::OK;
}

godot::Error load_loadout_file(core::Loadout& loadout, const godot::String& path) {
    try {
        loadout = core::load_loadout(to_std(path));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("set_loadout: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    return godot::OK;
}

// Loads a building for SwmClient/SwmServer.add_building.
godot::Error load_building(std::vector<std::pair<core::BuildingCollision, core::Placement>>& buildings,
                           const godot::String& path, const godot::Vector3& position, double yaw_degrees) {
    try {
        buildings.emplace_back(core::load_building_collision(to_std(path)),
                               core::placement_from_galaxies(position.x, position.y, position.z, yaw_degrees));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("add_building: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    return godot::OK;
}

// The world on a bake: its terrain, the map collision next to it (a
// Battlefront map's props), and placed buildings.
std::unique_ptr<core::World> make_world(const core::Heightfield& ground, const std::string& bake,
                                        const std::vector<std::pair<core::BuildingCollision, core::Placement>>& buildings) {
    auto world = std::make_unique<core::World>(ground);
    world->add_map_collision_of(bake);
    for (const auto& [collision, placement] : buildings) {
        world->add_building(collision, placement);
    }
    return world;
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

double SwmClient::now() const {
    return seconds_since(start_time_);
}

const std::optional<core::Battlefield>& SwmClient::battlefield() const {
    return client() ? client()->config().battlefield : battlefield_;
}

const core::Loadout& SwmClient::loadout() const {
    return client() ? client()->loadout() : loadout_;
}

const core::Loadout& SwmClient::loadout_of(int team, int index) const {
    if (battlefield() && team >= 0 && (team < 2 || (team == core::natives_team && battlefield()->natives))) {
        const auto& loadouts = battlefield()->team(team).loadouts;
        return loadouts[std::min<size_t>(size_t(index), loadouts.size() - 1)];
    }
    return loadout_;
}

godot::Error SwmClient::set_battlefield(const godot::String& path) {
    return load_battlefield_file(battlefield_, path);
}

godot::Array vehicle_class_dicts(std::span<const core::VehicleClass> classes);

godot::Error SwmClient::set_world_vehicles(const godot::String& dir) {
    try {
        world_vehicles_ = core::load_vehicle_classes(to_std(dir));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("set_world_vehicles: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    return godot::OK;
}

godot::Array SwmClient::get_vehicle_classes() const {
    return vehicle_class_dicts(client() ? client()->vehicle_classes()
                                        : std::span<const core::VehicleClass>(world_vehicles_));
}

void SwmClient::set_bot(bool enabled) {
    if (!enabled) {
        bot_.reset();
    } else if (!bot_) {
        bot_ = std::make_unique<core::Bot>(uint64_t(start_time_.time_since_epoch().count()));
        bot_->set_navigation(navigation_);
    }
}

void SwmClient::request_spawn(int64_t loadout, int64_t post) {
    if (client()) {
        client()->request_spawn(int(loadout), int(post));
    }
}

void SwmClient::set_battlefield_dir(const godot::String& path) {
    battlefield_dir_ = to_std(path);
}

int64_t SwmClient::get_generation() const {
    return session_ ? session_->generation() : 0;
}

godot::String SwmClient::get_session_error() const {
    return session_ ? godot::String::utf8(session_->error().c_str()) : godot::String();
}

godot::Array SwmClient::get_recruiters() const {
    godot::Array out;
    if (client()) {
        for (const core::net::Recruiter& r : client()->recruiters()) {
            godot::Dictionary entry;
            entry["name"] = godot::String::utf8(r.name.c_str());
            entry["position"] = to_godot(r.position);
            entry["radius"] = r.radius;
            // Stage 11: which battles it queues for.
            entry["kind"] = r.kind == core::net::Recruiter::space  ? "space"
                            : r.kind == core::net::Recruiter::both ? "both"
                                                                    : "ground";
            out.push_back(entry);
        }
    }
    return out;
}

godot::PackedStringArray SwmClient::get_factions() const {
    godot::PackedStringArray out;
    if (client()) {
        for (const std::string& name : client()->factions()) {
            out.push_back(godot::String::utf8(name.c_str()));
        }
    }
    return out;
}

void SwmClient::queue(int64_t team, const godot::String& mode) {
    if (client()) {
        // Stage 11: the game mode by id ("conquest", "ctf", "1flag", "hunt",
        // "xl"), or "any".
        uint8_t wanted = 0;
        const std::string id = mode.utf8().get_data();
        if (id == "any") {
            wanted = core::net::QueueRequest::any_mode;
        } else if (const auto parsed = core::parse_game_mode(id)) {
            wanted = uint8_t(*parsed);
        }
        client()->queue(int(team), wanted);
    }
}

godot::Array SwmClient::get_game_modes() const {
    godot::Array out;
    for (size_t i = 0; i < core::game_mode_count; ++i) {
        godot::Dictionary entry;
        entry["id"] = godot::String::utf8(std::string(core::game_mode_id(core::GameMode(i))).c_str());
        entry["name"] = godot::String::utf8(std::string(core::game_mode_name(core::GameMode(i))).c_str());
        out.push_back(entry);
    }
    return out;
}

godot::Dictionary SwmClient::get_player_status() const {
    godot::Dictionary out;
    if (client()) {
        const core::net::PlayerStatus& status = client()->player_status();
        out["queue"] = status.queue == core::net::PlayerStatus::queued ? "queued" : "none";
        out["team"] = int64_t(status.team);
        out["in_queue"] = int64_t(status.in_queue);
        out["starts_in"] = double(status.starts_in);
        out["battlefield"] = godot::String::utf8(status.battlefield.c_str());
        out["notice_id"] = int64_t(status.notice_id);
        out["notice"] = godot::String::utf8(status.notice.c_str());
        out["credits"] = int64_t(status.credits);
        out["faction_points"] = int64_t(status.faction_points);
        out["experience"] = int64_t(status.experience);
        out["active_loadout"] = int64_t(status.active_loadout);
        out["switching_loadout"] = int64_t(status.switching_loadout);
        out["switch_progress"] = double(status.switch_progress);
        out["extract_progress"] = double(status.extract_progress);
        out["combat_level"] = int64_t(status.combat_level);
        out["inventory_rev"] = int64_t(status.inventory_rev);
    }
    return out;
}

godot::Array SwmClient::get_quartermasters() const {
    godot::Array out;
    if (client()) {
        for (const core::net::Quartermaster& q : client()->quartermasters()) {
            godot::Dictionary entry;
            entry["name"] = godot::String::utf8(q.name.c_str());
            entry["faction"] = godot::String::utf8(q.faction.c_str());
            entry["position"] = to_godot(q.position);
            entry["radius"] = q.radius;
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::get_vendors() const {
    godot::Array out;
    if (client()) {
        for (const core::net::Vendor& v : client()->vendors()) {
            godot::Dictionary entry;
            entry["name"] = godot::String::utf8(v.name.c_str());
            entry["kind"] = godot::String::utf8(v.kind.c_str());
            entry["position"] = to_godot(v.position);
            entry["radius"] = v.radius;
            entry["detail"] = godot::String::utf8(v.detail.c_str());
            out.push_back(entry);
        }
    }
    return out;
}

const core::Loadout& SwmClient::weapons_of(uint32_t entity) const {
    const auto classes = client() ? client()->vehicle_classes() : std::span<const core::VehicleClass>{};
    // Riding: the seat's guns (the driver's are the vehicle's own).
    const auto ridden = [&](const core::MoveState& state) -> const core::Loadout* {
        if (state.vehicle < 0 || size_t(state.vehicle) >= classes.size()) return nullptr;
        const core::VehicleClass& vehicle = classes[size_t(state.vehicle)];
        const core::VehicleSeat* seat = core::vehicle_seat(vehicle, state.seat);
        return seat ? &seat->loadout : &vehicle.loadout;
    };
    if (client() && entity == uint32_t(client()->entity_id())) {
        const core::Loadout* riding = ridden(client()->predicted());
        return riding ? *riding : loadout();
    }
    if (client()) {
        for (const auto& remote : client()->remotes()) {
            if (remote.id == entity) {
                if (const core::Loadout* riding = ridden(remote.state)) {
                    return *riding;
                }
                return loadout_of(remote.team, remote.loadout);
            }
        }
    }
    return loadout_of(-1, 0);
}

godot::Array SwmClient::get_vehicles() const {
    godot::Array out;
    if (client()) {
        for (const core::net::VehicleState& v : client()->vehicles()) {
            godot::Dictionary entry;
            entry["id"] = int64_t(v.id);
            entry["vehicle"] = int64_t(v.vehicle);
            entry["position"] = to_godot(v.position);
            entry["velocity"] = to_godot(v.velocity);
            entry["yaw"] = double(v.yaw);
            entry["pitch"] = double(v.pitch);
            entry["health"] = double(v.health);
            entry["max_health"] = double(v.max_health);
            entry["driver"] = int64_t(v.driver);
            entry["team"] = int64_t(v.team);
            entry["alive"] = v.alive;
            entry["bank"] = double(v.bank);
            entry["grounded"] = (v.flags & core::net::VehicleState::flag_grounded) != 0;
            entry["mobile_spawn"] = (v.flags & core::net::VehicleState::flag_mobile_spawn) != 0;
            godot::Array gunners;
            for (const uint32_t g : v.gunners) {
                gunners.push_back(int64_t(g));
            }
            entry["gunners"] = gunners;
            entry["owner"] = int64_t(v.owner);  // in the world: whose it is (0 in a match)
            entry["towing"] = int64_t(v.towing);  // Stage 11: the walker its tow cable holds (0: none)
            entry["tow_wraps"] = double(v.tow_wraps);
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::get_targets() const {
    godot::Array out;
    if (client()) {
        for (const core::net::TargetState& t : client()->targets()) {
            godot::Dictionary entry;
            entry["id"] = int64_t(t.id);
            entry["name"] = godot::String::utf8(t.name.c_str());
            entry["position"] = to_godot(t.position);
            entry["health"] = double(t.health);
            entry["max_health"] = double(t.max_health);
            entry["alive"] = t.alive;
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::get_corpses() const {
    godot::Array out;
    if (client()) {
        for (const core::net::CorpseLootState& c : client()->corpses()) {
            godot::Dictionary entry;
            entry["entity_id"] = int64_t(c.entity_id);
            entry["name"] = godot::String::utf8(c.name.c_str());
            entry["position"] = to_godot(c.position);
            godot::Array items;
            for (const core::net::LootItemEntry& it : c.items) {
                godot::Dictionary idict;
                idict["slot"] = int64_t(it.slot);
                idict["def_id"] = godot::String::utf8(it.def_id.c_str());
                idict["quality"] = double(it.quality);
                items.push_back(idict);
            }
            entry["items"] = items;
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::get_inventory() const {
    godot::Array out;
    if (client()) {
        for (const core::ItemInstance& inst : client()->items()) {
            godot::Dictionary entry;
            entry["id"] = int64_t(inst.id);
            entry["owner_id"] = int64_t(inst.owner_id);
            entry["def_id"] = godot::String::utf8(inst.def_id.c_str());
            entry["quality"] = inst.quality;
            entry["quantity"] = int64_t(inst.quantity);
            entry["custom_name"] = godot::String::utf8(inst.custom_name.c_str());
            entry["color_json"] = godot::String::utf8(inst.color_json.c_str());
            out.push_back(entry);
        }
    }
    return out;
}

godot::Dictionary SwmClient::get_reputation() const {
    godot::Dictionary out;
    if (client()) {
        for (const std::string_view pf : core::playable_factions) {
            out[godot::String::utf8(pf.data(), int(pf.size()))] = int64_t(0);
        }
        for (const auto& [faction, rep] : client()->reputation()) {
            out[godot::String::utf8(faction.c_str())] = int64_t(rep);
        }
    }
    return out;
}

godot::Array SwmClient::get_character_loadouts() const {
    godot::Array out;
    if (client()) {
        for (const core::CharacterLoadout& lo : client()->char_loadouts()) {
            godot::Dictionary entry;
            entry["slot_index"] = int64_t(lo.slot_index);
            entry["name"] = godot::String::utf8(lo.name.c_str());
            entry["primary_id"] = int64_t(lo.primary_id);
            entry["sidearm_id"] = int64_t(lo.sidearm_id);
            entry["secondary_id"] = int64_t(lo.secondary_id);
            godot::Dictionary gear, app;
            for (const auto& [k, v] : lo.gear_ids) {
                gear[godot::String::utf8(k.c_str())] = int64_t(v);
            }
            for (const auto& [k, v] : lo.appearance_ids) {
                app[godot::String::utf8(k.c_str())] = int64_t(v);
            }
            entry["gear_ids"] = gear;
            entry["appearance_ids"] = app;
            godot::Array specializations;
            for (const std::string& id : lo.specializations) {
                specializations.push_back(godot::String::utf8(id.c_str()));
            }
            entry["specializations"] = specializations;
            entry["companion"] = godot::String::utf8(lo.companion.c_str());
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::get_custom_loadouts() const {
    godot::Array out;
    if (client()) {
        for (const core::Loadout& lo : client()->custom_loadouts()) {
            godot::Array weapons;
            for (int slot = 0; slot < core::hand_slots; ++slot) {
                if (const core::WeaponDef* w = core::weapon_in_slot(lo, slot)) {
                    weapons.push_back(godot::String::utf8(w->name.c_str()));
                }
            }
            const core::WeaponDef* sec = core::weapon_in_slot(lo, core::secondary_slot);
            const auto sp = core::Species(std::min<size_t>(client()->self().species, core::species_count - 1));
            godot::Dictionary entry;
            entry["name"] = godot::String::utf8(lo.name.c_str());
            entry["max_health"] = core::stats_for(lo, sp).max_health;
            entry["weapons"] = weapons;
            entry["secondary"] = sec ? godot::String::utf8(sec->name.c_str()) : godot::String();
            out.push_back(entry);
        }
    }
    return out;
}

void SwmClient::save_loadout(int64_t slot_index, const godot::Dictionary& loadout) {
    if (!client()) return;
    core::CharacterLoadout lo;
    lo.slot_index = int(slot_index);
    lo.name = loadout.has("name") ? to_std(loadout["name"]) : ("Loadout " + std::to_string(slot_index + 1));
    lo.primary_id = loadout.has("primary_id") ? uint64_t(int64_t(loadout["primary_id"])) : 0;
    lo.sidearm_id = loadout.has("sidearm_id") ? uint64_t(int64_t(loadout["sidearm_id"])) : 0;
    lo.secondary_id = loadout.has("secondary_id") ? uint64_t(int64_t(loadout["secondary_id"])) : 0;
    if (loadout.has("gear_ids")) {
        godot::Dictionary gear = loadout["gear_ids"];
        godot::Array keys = gear.keys();
        for (int i = 0; i < keys.size(); ++i) {
            lo.gear_ids[to_std(keys[i])] = uint64_t(int64_t(gear[keys[i]]));
        }
    }
    if (loadout.has("appearance_ids")) {
        godot::Dictionary app = loadout["appearance_ids"];
        godot::Array keys = app.keys();
        for (int i = 0; i < keys.size(); ++i) {
            lo.appearance_ids[to_std(keys[i])] = uint64_t(int64_t(app[keys[i]]));
        }
    }
    if (loadout.has("specializations")) {
        const godot::Array ids = loadout["specializations"];
        for (int i = 0; i < ids.size(); ++i) {
            lo.specializations.push_back(to_std(ids[i]));
        }
    }
    if (loadout.has("companion")) {
        lo.companion = to_std(loadout["companion"]);
    }
    client()->save_loadout(lo);
}

void SwmClient::switch_loadout(int64_t slot_index) {
    if (client()) {
        client()->switch_loadout(int(slot_index));
    }
}

void SwmClient::buy_item(const godot::String& def_id, bool use_faction_points) {
    if (client()) {
        client()->buy_item(to_std(def_id), use_faction_points);
    }
}

void SwmClient::destroy_item(int64_t item_id) {
    if (client()) {
        client()->destroy_item(uint64_t(item_id));
    }
}

void SwmClient::take_loot(int64_t corpse_entity, int64_t slot) {
    if (client()) {
        client()->take_loot(uint32_t(corpse_entity), int(slot));
    }
}

void SwmClient::sell_item(int64_t item_id) {
    if (client()) {
        client()->sell_item(uint64_t(item_id));
    }
}

void SwmClient::sell_all_junk() {
    if (client()) {
        client()->sell_all_junk();
    }
}

void SwmClient::use_item(int64_t item_id) {
    if (client()) {
        client()->use_item(uint64_t(item_id));
    }
}

namespace {

godot::Dictionary quest_dictionary(const core::net::QuestInfo& q) {
    godot::Dictionary out;
    out["id"] = godot::String::utf8(q.id.c_str());
    out["title"] = godot::String::utf8(q.title.c_str());
    out["giver"] = godot::String::utf8(q.giver.c_str());
    out["summary"] = godot::String::utf8(q.summary.c_str());
    godot::Array steps;
    for (const std::string& step : q.steps) {
        steps.push_back(godot::String::utf8(step.c_str()));
    }
    out["steps"] = steps;
    out["step"] = int64_t(q.step);
    out["count"] = int64_t(q.count);
    out["needed"] = int64_t(q.needed);
    if (q.has_waypoint) {
        out["waypoint"] = to_godot(q.waypoint);
    }
    out["credits"] = int64_t(q.credits);
    out["experience"] = int64_t(q.experience);
    out["faction_points"] = int64_t(q.faction_points);
    godot::Array items;
    for (const std::string& item : q.items) {
        items.push_back(godot::String::utf8(item.c_str()));
    }
    out["items"] = items;
    out["mission"] = q.mission;
    out["level"] = int64_t(q.level);
    out["step_npc"] = godot::String::utf8(q.step_npc.c_str());
    godot::PackedStringArray targets;
    for (const std::string& name : q.targets) targets.push_back(godot::String::utf8(name.c_str()));
    out["targets"] = targets;
    out["area_radius"] = double(q.area_radius);
    return out;
}

} // namespace

godot::Array SwmClient::get_quest_npcs() const {
    godot::Array out;
    if (client()) {
        for (const core::net::QuestNpc& n : client()->quest_npcs()) {
            godot::Dictionary entry;
            entry["entity"] = int64_t(n.entity);
            entry["name"] = godot::String::utf8(n.name.c_str());
            entry["position"] = to_godot(n.position);
            entry["giver"] = n.giver;
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::get_mission_terminals() const {
    godot::Array out;
    if (client()) {
        const auto& terminals = client()->mission_terminals();
        for (size_t i = 0; i < terminals.size(); ++i) {
            godot::Dictionary entry;
            entry["index"] = int64_t(i);
            entry["name"] = godot::String::utf8(terminals[i].name.c_str());
            entry["kind"] = godot::String::utf8(terminals[i].kind.c_str());
            entry["position"] = to_godot(terminals[i].position);
            entry["radius"] = terminals[i].radius;
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::get_professions() const {
    godot::Array out;
    if (client()) {
        for (const core::ProfessionProgress& p : client()->professions().professions) {
            godot::Dictionary entry;
            entry["id"] = godot::String::utf8(p.id.c_str());
            entry["xp"] = p.xp;
            entry["trained"] = int64_t(p.trained);
            out.push_back(entry);
        }
    }
    return out;
}

godot::Dictionary SwmClient::get_bank() const {
    godot::Dictionary out;
    godot::Array items;
    if (client()) {
        out["credits"] = int64_t(client()->bank_credits());
        for (const core::ItemInstance& item : client()->bank_items()) {
            godot::Dictionary d;
            d["id"] = int64_t(item.id);
            d["def_id"] = godot::String::utf8(item.def_id.c_str());
            d["quality"] = item.quality;
            d["custom_name"] = godot::String::utf8(item.custom_name.c_str());
            items.push_back(d);
        }
    } else {
        out["credits"] = int64_t(0);
    }
    out["items"] = items;
    return out;
}

void SwmClient::service(int64_t action, const godot::String& id, int64_t amount, int64_t item_id) {
    if (client()) client()->service(uint8_t(action), std::string(id.utf8().get_data()), amount, uint64_t(item_id));
}

// Stage 12 ---------------------------------------------------------------

namespace {

godot::Dictionary companion_dictionary(const core::CompanionRecord& r) {
    godot::Dictionary d;
    d["id"] = int64_t(r.id);
    d["template"] = godot::String::utf8(r.template_id.c_str());
    d["name"] = godot::String::utf8(r.name.c_str());
    d["origin"] = godot::String::utf8(r.origin.c_str());
    d["quality"] = r.quality;
    d["health"] = r.health;
    d["damage"] = r.damage;
    d["speed"] = r.speed;
    d["colors"] = int64_t(r.colors);
    godot::PackedStringArray modules;
    for (const std::string& m : r.modules) modules.push_back(godot::String::utf8(m.c_str()));
    d["modules"] = modules;
    d["mount"] = r.mount;
    return d;
}

godot::Dictionary contract_dictionary(const core::Contract& c) {
    godot::Dictionary d;
    d["id"] = int64_t(c.id);
    d["kind"] = godot::String::utf8(std::string(core::contract_kind_name(c.kind)).c_str());
    d["title"] = godot::String::utf8(c.title.c_str());
    d["target_name"] = godot::String::utf8(c.target_name.c_str());
    d["target_entity"] = int64_t(c.target_entity);
    d["waypoint"] = to_godot(c.waypoint);
    d["radius"] = c.radius;
    d["reward"] = int64_t(c.reward);
    d["xp"] = int64_t(c.xp);
    d["expires_in"] = c.expires;
    d["progress"] = c.progress;
    d["needed"] = c.needed;
    d["profession"] = godot::String::utf8(c.profession.c_str());
    return d;
}

godot::Dictionary listing_dictionary(const core::BazaarListing& l) {
    godot::Dictionary d;
    d["id"] = int64_t(l.id);
    d["seller"] = godot::String::utf8(l.seller_name.c_str());
    d["shop"] = godot::String::utf8(l.shop.c_str());
    d["def_id"] = godot::String::utf8(l.item.def_id.c_str());
    d["quality"] = l.item.quality;
    d["quantity"] = int64_t(l.item.quantity);
    d["custom_name"] = godot::String::utf8(l.item.custom_name.c_str());
    d["color_json"] = godot::String::utf8(l.item.color_json.c_str());
    d["price"] = int64_t(l.price);
    d["listed_at"] = int64_t(l.listed_at);
    return d;
}

} // namespace

void SwmClient::specialist(int64_t action, const godot::String& id, int64_t target, const godot::Vector3& position,
                           int64_t amount, int64_t item_id, const godot::String& text) {
    if (!client()) return;
    core::net::SpecialistRequest r;
    r.action = uint8_t(std::clamp<int64_t>(action, 0, 255));
    r.id = to_std(id);
    r.target = uint32_t(std::max<int64_t>(0, target));
    r.position = {position.x, position.y, position.z};
    r.amount = amount;
    r.item_id = uint64_t(std::max<int64_t>(0, item_id));
    r.text = to_std(text);
    client()->specialist_request(r);
}

godot::Dictionary SwmClient::get_specialist() const {
    godot::Dictionary out;
    if (!client()) return out;
    const core::net::SpecialistSync& s = client()->specialist();
    out["revision"] = int64_t(s.revision);
    godot::Array abilities;
    for (const core::net::AbilityStatus& a : s.abilities) {
        godot::Dictionary d;
        d["id"] = godot::String::utf8(a.id.c_str());
        d["cooldown"] = double(a.cooldown);
        abilities.push_back(d);
    }
    out["abilities"] = abilities;
    godot::Array effects;
    for (const core::net::EffectStatus& e : s.effects) {
        godot::Dictionary d;
        d["id"] = godot::String::utf8(e.id.c_str());
        d["name"] = godot::String::utf8(e.name.c_str());
        d["remaining"] = double(e.remaining);
        d["duration"] = double(e.duration);
        d["flags"] = int64_t(e.flags);
        effects.push_back(d);
    }
    out["effects"] = effects;
    godot::Array companions;
    for (const core::CompanionRecord& r : s.companions) companions.push_back(companion_dictionary(r));
    out["companions"] = companions;
    out["active_companion"] = int64_t(s.active_companion);
    out["companion_entity"] = int64_t(s.companion_entity);
    out["companion_cooldown"] = double(s.companion_cooldown);
    out["companion_mode"] = int64_t(s.companion_mode);
    out["companion_slot"] = int64_t(s.companion_slot);
    godot::Array samples;
    for (const core::DnaSample& d : s.samples) {
        godot::Dictionary e;
        e["id"] = int64_t(d.id);
        e["template"] = godot::String::utf8(d.template_id.c_str());
        e["quality"] = d.quality;
        samples.push_back(e);
    }
    out["samples"] = samples;
    godot::Array contracts, offers, bazaar, mine, prices;
    for (const core::Contract& c : s.contracts) contracts.push_back(contract_dictionary(c));
    for (const core::Contract& c : s.offers) offers.push_back(contract_dictionary(c));
    for (const core::BazaarListing& l : s.bazaar) bazaar.push_back(listing_dictionary(l));
    for (const core::BazaarListing& l : s.my_listings) mine.push_back(listing_dictionary(l));
    for (const auto& [def, p] : s.prices) {
        godot::Dictionary d;
        d["def_id"] = godot::String::utf8(def.c_str());
        d["count"] = int64_t(p.count);
        d["low"] = int64_t(p.low);
        d["high"] = int64_t(p.high);
        d["average"] = int64_t(p.average);
        d["last"] = int64_t(p.last);
        prices.push_back(d);
    }
    out["contracts"] = contracts;
    out["offers"] = offers;
    out["offer_source"] = godot::String::utf8(s.offer_source.c_str());
    out["bazaar"] = bazaar;
    out["my_listings"] = mine;
    out["prices"] = prices;
    out["performing"] = s.performing;
    out["bounty_flag"] = s.bounty_flag;
    out["design_from"] = godot::String::utf8(s.design_from.c_str());
    out["design_appearance"] = godot::String::utf8(s.design_appearance.c_str());
    out["channel"] = godot::String::utf8(s.channel.c_str());
    out["channel_remaining"] = double(s.channel_remaining);
    return out;
}

godot::Dictionary SwmClient::get_field() const {
    godot::Dictionary out;
    if (!client()) return out;
    const core::net::FieldState& f = client()->field();
    godot::Array marks;
    for (const core::net::FieldMark& m : f.marks) {
        godot::Dictionary d;
        d["entity"] = int64_t(m.entity);
        d["remaining"] = double(m.remaining);
        d["kind"] = int64_t(m.kind);  // 0 marked, 1 tracked, 2 bounty
        marks.push_back(d);
    }
    out["marks"] = marks;
    godot::PackedInt64Array cloaked, performers;
    for (const uint32_t e : f.cloaked) cloaked.push_back(int64_t(e));
    for (const uint32_t e : f.performers) performers.push_back(int64_t(e));
    out["cloaked"] = cloaked;
    out["performers"] = performers;
    godot::Dictionary companions;  // companion entity -> owner entity
    for (const auto& [entity, owner] : f.companions) companions[int64_t(entity)] = int64_t(owner);
    out["companions"] = companions;
    godot::Array deployables;
    for (const core::net::FieldDeployable& d : f.deployables) {
        godot::Dictionary e;
        e["id"] = int64_t(d.id);
        e["kind"] = godot::String::utf8(std::string(core::deploy_kind_name(core::DeployKind(d.kind))).c_str());
        e["owner"] = int64_t(d.owner);
        e["friendly"] = d.friendly;
        e["position"] = to_godot(d.position);
        e["radius"] = double(d.radius);
        e["remaining"] = double(d.remaining);
        deployables.push_back(e);
    }
    out["deployables"] = deployables;
    godot::Array hazards;
    for (const core::net::FieldHazard& h : f.hazards) {
        godot::Dictionary e;
        e["id"] = int64_t(h.id);
        e["kind"] = godot::String::utf8(h.kind.c_str());
        e["position"] = to_godot(h.position);
        e["radius"] = double(h.radius);
        e["remaining"] = double(h.remaining);
        hazards.push_back(e);
    }
    out["hazards"] = hazards;
    godot::Dictionary flair;  // entity -> flags (1 holo flair, 2 inspired)
    for (const auto& [entity, flags] : f.flair) flair[int64_t(entity)] = int64_t(flags);
    out["flair"] = flair;
    return out;
}

godot::Dictionary SwmClient::get_specialist_book(const godot::String& path) {
    godot::Dictionary out;
    core::SpecialistBook book;
    try {
        book.load(to_std(path));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("get_specialist_book: ", error.what());
        return out;
    }
    godot::Dictionary abilities;
    for (const core::AbilityDef& a : book.abilities()) {
        godot::Dictionary d;
        d["id"] = godot::String::utf8(a.id.c_str());
        d["name"] = godot::String::utf8(a.name.c_str());
        d["description"] = godot::String::utf8(a.description.c_str());
        d["profession"] = godot::String::utf8(a.profession.c_str());
        d["level"] = int64_t(a.level);
        d["effect"] = godot::String::utf8(std::string(core::ability_effect_name(a.effect)).c_str());
        d["target"] = godot::String::utf8(std::string(core::ability_target_name(a.target)).c_str());
        d["deploy"] = godot::String::utf8(std::string(core::deploy_kind_name(a.deploy)).c_str());
        d["cooldown"] = a.cooldown;
        d["range"] = a.range;
        d["radius"] = a.radius;
        d["duration"] = a.duration;
        d["channel"] = a.channel;
        d["where"] = a.match_only ? "match" : a.world_only ? "world" : "both";
        abilities[d["id"]] = d;
    }
    out["abilities"] = abilities;
    godot::Dictionary companions;
    for (const core::CompanionTemplate& c : book.companions()) {
        godot::Dictionary d;
        d["id"] = godot::String::utf8(c.id.c_str());
        d["name"] = godot::String::utf8(c.name.c_str());
        d["kind"] = godot::String::utf8(std::string(core::companion_kind_name(c.kind)).c_str());
        d["role"] = godot::String::utf8(std::string(core::companion_role_name(c.role)).c_str());
        d["look"] = godot::String::utf8(c.look.c_str());
        d["tier"] = int64_t(c.tier);
        d["level"] = int64_t(c.level);
        d["build_level"] = int64_t(c.build_level);
        d["health"] = c.health;
        d["damage"] = c.damage;
        d["bays"] = int64_t(c.bays);
        d["mountable"] = c.mountable;
        d["tactical"] = c.tactical();
        d["radius"] = c.radius;
        d["height"] = c.height;
        godot::Array materials;
        for (const auto& [cat, units] : c.materials) materials.push_back(godot::String::utf8(std::format("{} {}", cat, units).c_str()));
        d["materials"] = materials;
        companions[d["id"]] = d;
    }
    out["companions"] = companions;
    godot::Array modules;
    for (const core::DroidModule& m : book.modules()) {
        godot::Dictionary d;
        d["id"] = godot::String::utf8(m.id.c_str());
        d["name"] = godot::String::utf8(m.name.c_str());
        d["description"] = godot::String::utf8(m.description.c_str());
        d["bay"] = godot::String::utf8(m.bay.c_str());
        d["level"] = int64_t(m.level);
        modules.push_back(d);
    }
    out["modules"] = modules;
    godot::Array slices;
    for (const core::SliceDef& s : book.slices()) {
        godot::Dictionary d;
        d["id"] = godot::String::utf8(s.id.c_str());
        d["name"] = godot::String::utf8(s.name.c_str());
        d["description"] = godot::String::utf8(s.description.c_str());
        d["applies"] = godot::String::utf8(s.applies.c_str());
        d["level"] = int64_t(s.level);
        slices.push_back(d);
    }
    out["slices"] = slices;
    return out;
}

godot::Array SwmClient::get_workbenches() const {
    godot::Array out;
    if (client()) {
        const auto& wbs = client()->workbenches();
        for (size_t i = 0; i < wbs.size(); ++i) {
            godot::Dictionary entry;
            entry["index"] = int64_t(i);
            entry["name"] = godot::String::utf8(wbs[i].name.c_str());
            entry["kind"] = godot::String::utf8(wbs[i].kind.c_str());
            entry["position"] = to_godot(wbs[i].position);
            entry["radius"] = wbs[i].radius;
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::get_materials() const {
    godot::Array out;
    if (client()) {
        for (const core::MaterialStack& m : client()->crafting().materials) {
            godot::Dictionary entry;
            entry["resource_id"] = godot::String::utf8(m.resource_id.c_str());
            entry["amount"] = m.amount;
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::get_presets() const {
    godot::Array out;
    if (client()) {
        for (const core::RecipePreset& p : client()->crafting().presets) {
            godot::Dictionary entry;
            entry["name"] = godot::String::utf8(p.name.c_str());
            entry["schematic_id"] = godot::String::utf8(p.schematic_id.c_str());
            godot::PackedStringArray rids, edefs;
            for (const std::string& s : p.resource_ids) rids.push_back(godot::String::utf8(s.c_str()));
            for (const std::string& s : p.enhancement_defs) edefs.push_back(godot::String::utf8(s.c_str()));
            entry["resource_ids"] = rids;
            entry["enhancement_defs"] = edefs;
            entry["exp_quality_pct"] = int64_t(p.exp_quality_pct);
            out.push_back(entry);
        }
    }
    return out;
}

void SwmClient::gather_extract(const godot::String& category_name) {
    if (client()) {
        const auto cat = core::resource_category_from_name(to_std(category_name));
        if (cat) client()->gather_extract(*cat);
    }
}

void SwmClient::harvest_corpse(int64_t corpse_entity) {
    if (client() && corpse_entity > 0) {
        client()->harvest_corpse(uint32_t(corpse_entity));
    }
}

void SwmClient::salvage_item(int64_t item_id) {
    if (client() && item_id > 0) {
        client()->salvage_item(uint64_t(item_id));
    }
}

void SwmClient::craft_item(const godot::String& schematic_id, const godot::PackedStringArray& resource_ids,
                           const godot::PackedInt64Array& enhancement_ids, int64_t exp_quality_pct,
                           int64_t batch_count) {
    if (!client()) return;
    std::vector<std::string> rids;
    rids.reserve(size_t(resource_ids.size()));
    for (int i = 0; i < resource_ids.size(); ++i) rids.push_back(to_std(resource_ids[i]));
    std::vector<uint64_t> eids;
    eids.reserve(size_t(enhancement_ids.size()));
    for (int i = 0; i < enhancement_ids.size(); ++i) eids.push_back(uint64_t(enhancement_ids[i]));
    client()->craft_item(to_std(schematic_id), std::move(rids), std::move(eids), int(exp_quality_pct), int(batch_count));
}

void SwmClient::save_recipe_preset(const godot::String& name, const godot::String& schematic_id,
                                   const godot::PackedStringArray& resource_ids,
                                   const godot::PackedStringArray& enhancement_defs, int64_t exp_quality_pct) {
    if (!client()) return;
    std::vector<std::string> rids, edefs;
    for (int i = 0; i < resource_ids.size(); ++i) rids.push_back(to_std(resource_ids[i]));
    for (int i = 0; i < enhancement_defs.size(); ++i) edefs.push_back(to_std(enhancement_defs[i]));
    client()->save_recipe_preset(to_std(name), to_std(schematic_id), std::move(rids), std::move(edefs),
                                 int(exp_quality_pct));
}

void SwmClient::delete_recipe_preset(const godot::String& name) {
    if (client()) client()->delete_recipe_preset(to_std(name));
}

void SwmClient::fulfill_work_order(const godot::String& order_id) {
    if (client()) client()->fulfill_work_order(to_std(order_id));
}

godot::Dictionary SwmClient::get_quests() const {
    godot::Dictionary out;
    if (!client()) {
        return out;
    }
    const core::net::QuestSync& sync = client()->quests();
    out["revision"] = int64_t(sync.revision);
    godot::Array active, completed, offers;
    for (const core::net::QuestInfo& q : sync.active) active.push_back(quest_dictionary(q));
    for (const std::string& title : sync.completed) completed.push_back(godot::String::utf8(title.c_str()));
    for (const core::net::QuestInfo& q : sync.offers) offers.push_back(quest_dictionary(q));
    out["active"] = active;
    out["completed"] = completed;
    out["offers"] = offers;
    out["offer_source"] = godot::String::utf8(sync.offer_source.c_str());
    out["offer_serial"] = int64_t(sync.offer_serial);
    out["offer_note"] = godot::String::utf8(sync.offer_note.c_str());
    godot::PackedStringArray ready;
    for (const std::string& giver : sync.ready_givers) ready.push_back(godot::String::utf8(giver.c_str()));
    out["ready_givers"] = ready;
    godot::PackedStringArray waiting;
    for (const std::string& giver : sync.waiting_givers) waiting.push_back(godot::String::utf8(giver.c_str()));
    out["waiting_givers"] = waiting;
    return out;
}

void SwmClient::talk_to(int64_t npc_entity) {
    if (client()) {
        client()->talk_to(uint32_t(npc_entity));
    }
}

void SwmClient::use_mission_terminal(int64_t index) {
    if (client()) {
        client()->use_mission_terminal(int(index));
    }
}

void SwmClient::accept_quest(const godot::String& id) {
    if (client()) {
        client()->accept_quest(to_std(id));
    }
}

void SwmClient::abandon_quest(const godot::String& id) {
    if (client()) {
        client()->abandon_quest(to_std(id));
    }
}

// Vehicle classes (a battlefield's, or the world's) as the client shows them.
godot::Array vehicle_class_dicts(std::span<const core::VehicleClass> classes) {
    godot::Array vehicles;
    for (const core::VehicleClass& v : classes) {
        godot::Dictionary entry;
        entry["name"] = godot::String::utf8(v.name.c_str());
        entry["label"] = godot::String::utf8(v.label.c_str());
        entry["kind"] = v.kind == core::VehicleKind::walker   ? "walker"
                        : v.kind == core::VehicleKind::turret ? "turret"
                        : v.kind == core::VehicleKind::flyer  ? "flyer"
                                                              : "hover";
        entry["model"] = godot::String::utf8(v.model.generic_string().c_str());  // absolute or game-relative path
        entry["max_health"] = v.max_health;
        entry["altitude"] = v.altitude;
        entry["eye"] = to_godot(v.eye);
        godot::Array seats;
        for (const core::VehicleSeat& seat : v.seats) {
            godot::Dictionary s;
            s["name"] = godot::String::utf8(seat.name.c_str());
            s["eye"] = to_godot(seat.eye);
            s["turret_node"] = godot::String::utf8(seat.turret_node.c_str());
            s["pitch_node"] = godot::String::utf8(seat.pitch_node.c_str());
            seats.push_back(s);
        }
        entry["seats"] = seats;
        entry["track_center"] = to_godot(v.track_center);
        entry["track_offset"] = to_godot(v.track_offset);
        entry["bounds_min"] = to_godot(v.bounds_min);
        entry["bounds_max"] = to_godot(v.bounds_max);
        entry["explosion_effect"] = godot::String::utf8(v.explosion_effect.c_str());
        entry["engine_sound"] = godot::String::utf8(v.engine_sound.c_str());
        entry["stride_speed"] = v.stride_speed;
        // Its top speed ahead (not boosting): its engine's sound goes by it.
        entry["top_speed"] = v.kind == core::VehicleKind::flyer ? v.max_speed : v.forward_speed;
        entry["rider_seat"] = to_godot(v.rider_seat);
        entry["wake_effect"] = godot::String::utf8(v.wake_effect.c_str());
        entry["step_sound"] = godot::String::utf8(v.step_sound.c_str());
        entry["stomp_effect"] = godot::String::utf8(v.stomp_effect.c_str());
        entry["rider_pose"] = godot::String::utf8(v.rider_pose.c_str());
        godot::Array damage_effects;
        for (const core::VehicleClass::DamageEffect& d : v.damage_effects) {
            godot::Dictionary e;
            e["effect"] = godot::String::utf8(d.effect.c_str());
            e["start"] = d.start;
            e["stop"] = d.stop;
            e["at"] = to_godot(d.at);
            damage_effects.push_back(e);
        }
        entry["damage_effects"] = damage_effects;
        entry["map_texture"] = godot::String::utf8(v.map_texture.c_str());
        entry["health_texture"] = godot::String::utf8(v.health_texture.c_str());
        entry["is_transport"] = v.is_transport;
        entry["contrail_effect"] = godot::String::utf8(v.contrail_effect.c_str());
        godot::Array spawn_points;
        for (const core::Vec3& p : v.spawn_points) {
            spawn_points.push_back(to_godot(p));
        }
        entry["spawn_points"] = spawn_points;
        vehicles.push_back(entry);
    }
    return vehicles;
}

godot::Dictionary SwmClient::get_battlefield() const {
    godot::Dictionary out;
    if (!battlefield()) {
        return out;
    }
    const core::Battlefield& field = *battlefield();
    out["name"] = godot::String::utf8(field.name.c_str());
    out["min_x"] = field.min_x;
    out["min_z"] = field.min_z;
    out["max_x"] = field.max_x;
    out["max_z"] = field.max_z;
    godot::Array minimap;
    for (const double edge : field.minimap) {
        minimap.push_back(edge);
    }
    out["minimap"] = minimap;  // game x, z under the image's left, top, right, bottom edges; zeros if unset
    godot::Array boundaries;
    for (const auto& polygon : field.boundaries) {
        godot::PackedVector2Array points;
        for (const core::Vec3& p : polygon) {
            points.push_back(godot::Vector2(real_t(p.x), real_t(p.z)));
        }
        boundaries.push_back(points);
    }
    out["boundaries"] = boundaries;
    godot::Array teams;
    for (const core::Team& team : field.teams) {
        godot::Array loadouts;
        for (const core::Loadout& loadout : team.loadouts) {
            godot::Array weapons;
            for (int slot = 0; slot < core::hand_slots; ++slot) {
                if (const core::WeaponDef* weapon = core::weapon_in_slot(loadout, slot)) {
                    weapons.push_back(godot::String::utf8(weapon->name.c_str()));
                }
            }
            const core::WeaponDef* secondary = core::weapon_in_slot(loadout, core::secondary_slot);
            godot::Dictionary entry;
            entry["name"] = godot::String::utf8(loadout.name.c_str());
            entry["max_health"] = core::stats_for(loadout).max_health;
            entry["weapons"] = weapons;
            entry["secondary"] = secondary ? godot::String::utf8(secondary->name.c_str()) : godot::String();
            loadouts.push_back(entry);
        }
        godot::Dictionary entry;
        entry["name"] = godot::String::utf8(team.name.c_str());
        entry["loadouts"] = loadouts;
        teams.push_back(entry);
    }
    out["teams"] = teams;
    godot::Array posts;
    for (const core::CommandPost& post : field.posts) {
        godot::Dictionary entry;
        entry["name"] = godot::String::utf8(post.name.c_str());
        core::Vec3 position = post.position;
        if (world()) {
            position.y = world()->terrain().height_at(position.x, position.z);
        }
        entry["position"] = to_godot(position);
        entry["radius"] = post.radius;
        entry["capturable"] = post.capturable;
        posts.push_back(entry);
    }
    out["posts"] = posts;
    // Its vehicles' classes: what they look like and how they're seen.
    const godot::Array vehicles = vehicle_class_dicts(field.vehicles);
    out["vehicles"] = vehicles;
    godot::Array powerups;
    for (const core::PowerupStation& station : field.powerups) {
        godot::Dictionary entry;
        entry["position"] = to_godot(station.position);
        entry["radius"] = station.radius;
        entry["health"] = station.health;
        entry["ammo"] = station.ammo;
        entry["max_health"] = station.max_health;
        powerups.push_back(entry);
    }
    out["powerups"] = powerups;
    out["is_space"] = field.is_space();
    // Stage 11: its game mode, and a flag mode's flags and capture regions.
    out["mode"] = godot::String::utf8(std::string(core::game_mode_id(field.mode)).c_str());
    out["mode_name"] = godot::String::utf8(std::string(core::game_mode_name(field.mode)).c_str());
    godot::Array flags;
    for (const core::Flag& flag : field.flags) {
        godot::Dictionary entry;
        entry["owner"] = int64_t(flag.owner);
        entry["home"] = to_godot(flag.home);
        flags.push_back(entry);
    }
    out["flags"] = flags;
    godot::Array captures;
    for (const core::FlagCapture& capture : field.flag_captures) {
        godot::Dictionary entry;
        entry["team"] = int64_t(capture.team);
        entry["position"] = to_godot(capture.where.center);
        entry["radius"] = std::max(capture.where.size.x, capture.where.size.z);
        captures.push_back(entry);
    }
    out["flag_captures"] = captures;
    out["time_limit"] = field.time_limit;
    out["min_fly_y"] = field.min_fly_y;
    out["max_fly_y"] = field.max_fly_y;
    out["score_limit"] = int64_t(field.score_limit);
    godot::Array space_systems;
    for (const core::SpaceSystem& sys : field.space_systems) {
        godot::Dictionary entry;
        entry["destructible"] = int64_t(sys.destructible);
        entry["owner_team"] = int64_t(sys.owner_team);
        entry["kind"] = godot::String::utf8(sys.kind.c_str());
        entry["points"] = int64_t(sys.points);
        entry["label"] = godot::String::utf8(sys.label.c_str());
        core::Vec3 pos;
        double max_hp = 0;
        if (sys.destructible >= 0 && size_t(sys.destructible) < field.destructibles.size()) {
            pos = field.destructibles[size_t(sys.destructible)].center;
            max_hp = field.destructibles[size_t(sys.destructible)].max_health;
        }
        entry["position"] = to_godot(pos);
        entry["max_health"] = max_hp;
        int shield_ctrl = -1;
        for (const core::ShieldLink& sl : field.shield_links) {
            for (int target : sl.targets) {
                if (target == sys.destructible) {
                    shield_ctrl = sl.controller;
                    break;
                }
            }
            if (shield_ctrl >= 0) break;
        }
        entry["shield_controller"] = int64_t(shield_ctrl);
        space_systems.push_back(entry);
    }
    out["space_systems"] = space_systems;
    godot::Array turret_chairs;
    for (const core::TurretChair& chair : field.turret_chairs) {
        godot::Dictionary entry;
        entry["name"] = godot::String::utf8(chair.name.c_str());
        entry["position"] = to_godot(chair.position);
        entry["yaw"] = chair.yaw;
        entry["team"] = int64_t(chair.team);
        turret_chairs.push_back(entry);
    }
    out["turret_chairs"] = turret_chairs;
    godot::Array landing_regions;
    for (const core::DeathRegion& r : field.landing_regions) {
        godot::Dictionary entry;
        entry["shape"] = r.shape == core::DeathRegion::Shape::sphere     ? "sphere"
                         : r.shape == core::DeathRegion::Shape::cylinder ? "cylinder"
                                                                         : "box";
        entry["center"] = to_godot(r.center);
        entry["size"] = to_godot(r.size);
        landing_regions.push_back(entry);
    }
    out["landing_regions"] = landing_regions;
    if (field.natives) {
        // Its natives (team 2): name, model ("<character>" or
        // "species:<id>"), how many, and whose friends they are.
        godot::Dictionary natives;
        natives["name"] = godot::String::utf8(field.natives->team.name.c_str());
        natives["look"] = godot::String::utf8(field.natives->look.c_str());
        natives["count"] = field.natives->count;
        godot::Array friends;
        for (const int team : field.natives->friends) {
            friends.push_back(team);
        }
        natives["friends"] = friends;
        out["natives"] = natives;
    }
    return out;
}

bool SwmClient::is_allied(int a, int b) const {
    return battlefield() ? battlefield()->allied(a, b) : a >= 0 && a == b;
}

godot::Dictionary SwmClient::get_match() const {
    godot::Dictionary out;
    if (!client() || !client()->match()) {
        return out;
    }
    const core::MatchState& match = *client()->match();
    out["phase"] = match.phase == core::Phase::warmup ? "warmup" : match.phase == core::Phase::live ? "live" : "ended";
    out["time_left"] = match.time_left;
    godot::Array tickets;
    tickets.push_back(match.tickets[0]);
    tickets.push_back(match.tickets[1]);
    out["tickets"] = tickets;
    godot::Array scores;
    scores.push_back(int64_t(match.scores[0]));
    scores.push_back(int64_t(match.scores[1]));
    out["scores"] = scores;
    out["score_limit"] = int64_t(match.score_limit);
    out["mode"] = godot::String::utf8(std::string(core::game_mode_id(match.mode)).c_str());
    godot::Array flag_states;
    for (const core::FlagState& flag : match.flags) {
        godot::Dictionary entry;
        entry["position"] = to_godot(flag.position);
        entry["carrier"] = int64_t(flag.carrier);
        entry["home"] = flag.home;
        entry["dropped_for"] = double(flag.dropped_for);
        flag_states.push_back(entry);
    }
    out["flags"] = flag_states;
    out["dominant"] = int64_t(match.dominant);
    out["victory_left"] = match.victory_left;
    out["winner"] = int64_t(match.winner);
    godot::Array posts;
    for (const core::PostState& post : match.posts) {
        godot::Dictionary entry;
        entry["owner"] = int64_t(post.owner);
        entry["holder"] = int64_t(post.holder);
        entry["charge"] = double(post.charge);
        entry["contested"] = post.contested;
        posts.push_back(entry);
    }
    out["posts"] = posts;
    godot::Array powerups;
    const auto& defs = battlefield() ? battlefield()->powerups : std::vector<core::PowerupStation>{};
    for (size_t i = 0; i < match.powerup_health.size(); ++i) {
        godot::Dictionary entry;
        const double hp = double(match.powerup_health[i]);
        const double max_hp = i < defs.size() ? defs[i].max_health : core::PowerupStation::default_max_health;
        entry["health"] = hp;
        entry["max_health"] = max_hp;
        entry["alive"] = hp > 0.0;
        powerups.push_back(entry);
    }
    out["powerups"] = powerups;
    godot::PackedFloat32Array doors;
    for (const float open : match.doors) {
        doors.push_back(open);
    }
    out["doors"] = doors;  // how open each of the battlefield's doors is, 0..1
    godot::PackedFloat32Array destructibles;
    for (const float health : match.destructibles) {
        destructibles.push_back(health);
    }
    out["destructibles"] = destructibles;  // health of each of the battlefield's destructibles; <= 0: destroyed
    out["clock"] = match.clock;  // seconds since the battlefield started: world animations keep to it
    return out;
}

void SwmClient::set_simulated_network(double latency_ms, double jitter_ms, double loss) {
    impairment_.latency = latency_ms / 1000;
    impairment_.jitter = jitter_ms / 1000;
    impairment_.loss = loss;
}

godot::Error SwmClient::add_building(const godot::String& collision_path, const godot::Vector3& position,
                                     double yaw_degrees) {
    return load_building(buildings_, collision_path, position, yaw_degrees);
}

godot::Error SwmClient::start(const godot::String& server, const godot::String& bake_prefix,
                              const godot::String& name) {
    const auto endpoint = core::net::parse_endpoint(to_std(server));
    if (!endpoint) {
        godot::UtilityFunctions::push_error("SwmClient: bad server address ", server);
        return godot::ERR_INVALID_PARAMETER;
    }
    session_.reset();  // it refers to the ground and link replaced below
    impaired_.reset();
    try {
        ground_ = core::load_bake(to_std(bake_prefix));
        world_ = make_world(ground_, to_std(bake_prefix), buildings_);
        bake_ = to_std(bake_prefix);
        udp_ = std::make_unique<core::net::UdpLink>();
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("SwmClient: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    core::net::Link* link = udp_.get();
    if (impairment_.latency > 0 || impairment_.jitter > 0 || impairment_.loss > 0) {
        impaired_ = std::make_unique<core::net::ImpairedLink>(*udp_, impairment_, [this] { return now(); });
        link = impaired_.get();
    }
    core::ClientConfig config{.server = *endpoint, .name = to_std(name), .loadout = loadout_};
    config.battlefield = battlefield_;
    config.world_vehicles = world_vehicles_;
    // Battles a hub sends us to: their battlefield file, by name.
    auto find_battlefield = [dir = battlefield_dir_](const std::string& id) -> std::optional<core::Battlefield> {
        if (dir.empty() || id.find_first_of("/\\.") != std::string::npos) {
            return std::nullopt;
        }
        try {
            return core::load_battlefield_id(std::filesystem::path(dir), id);  // id: file[@mode]
        } catch (const std::exception& error) {
            godot::UtilityFunctions::push_error("SwmClient: ", error.what());
            return std::nullopt;
        }
    };
    // And the world it's fought in: its map's, or ours when it names none.
    auto find_world = [](const core::Battlefield& field) -> std::shared_ptr<const core::World> {
        return field.terrain.empty() ? nullptr : core::load_world(field.terrain);
    };
    // Another planet a hub sends us to (Stage 13): its bake, next to ours.
    auto find_planet = [dir = std::filesystem::path(bake_).parent_path()](const std::string& planet)
        -> std::shared_ptr<const core::World> {
        if (planet.find_first_of("/\\.") != std::string::npos) {
            return nullptr;
        }
        return core::load_world(dir / planet);
    };
    session_ = std::make_unique<core::ClientSession>(*link, *world_, config, find_battlefield, find_world, find_planet,
                                                     std::filesystem::path(bake_).filename().string());
    return godot::OK;
}

godot::Error SwmClient::set_loadout(const godot::String& path) {
    return load_loadout_file(loadout_, path);
}

void SwmClient::set_input(const godot::Dictionary& c) {
    if (!client()) {
        return;
    }
    core::GameClient::Controls controls;
    controls.forward = double(c.get("forward", 0.0));
    controls.right = double(c.get("right", 0.0));
    controls.yaw = double(c.get("yaw", 0.0));
    controls.pitch = double(c.get("pitch", 0.0));
    controls.sprint = bool(c.get("sprint", false));
    controls.jump = bool(c.get("jump", false));
    controls.fire = bool(c.get("fire", false));
    controls.reload = bool(c.get("reload", false));
    controls.secondary = bool(c.get("secondary", false));
    controls.crouch = bool(c.get("crouch", false));
    controls.use = bool(c.get("use", false));
    controls.block = bool(c.get("block", false));
    controls.weapon = int(int64_t(c.get("weapon", 0)));
    client()->set_input(controls);
}

void SwmClient::update() {
    if (client()) {
        if (bot_) {
            if (bot_generation_ != session_->generation()) {
                bot_generation_ = int(session_->generation());
                bot_ = std::make_unique<core::Bot>(uint64_t(start_time_.time_since_epoch().count()));
                navigation_.reset();
            }
            // Its way around the battlefield, worked out as it goes.
            if (!navigation_ && battlefield()) {
                navigation_ = std::make_shared<core::NavGrid>(*world(), *battlefield());
                bot_->set_navigation(navigation_);
            }
            bot_->update(*client(), *world(), now());
        }
        session_->update(now());
    }
}

void SwmClient::leave() {
    if (client()) {
        client()->disconnect();
    }
}

godot::String SwmClient::get_status() const {
    if (!client()) {
        return "stopped";
    }
    switch (client()->status()) {
    case core::GameClient::Status::connecting:
        return "connecting";
    case core::GameClient::Status::connected:
        return "connected";
    case core::GameClient::Status::refused:
        return "refused";
    case core::GameClient::Status::timed_out:
        return "timed_out";
    case core::GameClient::Status::disconnected:
        return "disconnected";
    case core::GameClient::Status::transferring:
        return "transferring";
    }
    return "";
}

bool SwmClient::is_spawned() const {
    return client() && client()->spawned();
}

int64_t SwmClient::get_entity_id() const {
    return client() ? client()->entity_id() : 0;
}

int64_t SwmClient::get_character_id() const {
    return client() ? int64_t(client()->character_id()) : 0;
}

godot::String SwmClient::get_refusal() const {
    return client() ? godot::String::utf8(client()->refusal().c_str()) : godot::String();
}

godot::Dictionary SwmClient::get_presented() const {
    if (!client()) {
        return {};
    }
    godot::Dictionary out = to_dictionary(client()->presented());
    describe(out, client()->self());
    const core::WeaponDef* held = core::weapon_in_slot(loadout(), client()->presented().combat.weapon);
    out["weapon_model"] = godot::String::utf8(held ? held->model.c_str() : "");
    out["anim_set"] = godot::String::utf8(held ? held->anim_set.c_str() : "rifle");
    return out;
}

godot::Array SwmClient::get_bolts() const {
    godot::Array out;
    if (client()) {
        const auto remotes = client()->remotes();
        for (const core::net::BoltState& bolt : client()->bolts(now())) {
            godot::Dictionary entry;
            entry["id"] = int64_t(bolt.id);
            entry["owner"] = int64_t(bolt.owner);
            entry["weapon"] = int64_t(bolt.weapon);
            entry["position"] = to_godot(bolt.position);
            entry["velocity"] = to_godot(bolt.velocity);
            int team = -1, loadout_index = 0;
            for (const auto& remote : remotes) {
                if (remote.id == bolt.owner) {
                    team = remote.team;
                    loadout_index = remote.loadout;
                }
            }
            const core::Loadout* owner_def = &loadout_of(team, loadout_index);
            if (bolt.owner == uint32_t(client()->entity_id())) {
                owner_def = &loadout();  // own missiles and grenades
            }
            owner_def = &weapons_of(bolt.owner);  // a vehicle's guns, while it drives
            entry["kind"] = bolt.kind == core::Projectile::bolt      ? "bolt"
                            : bolt.kind == core::Projectile::missile ? "missile"
                                                                     : "grenade";
            const core::WeaponDef* weapon = core::weapon_in_slot(*owner_def, bolt.weapon);
            add_bolt_look(entry, weapon != nullptr ? *weapon : core::WeaponDef{});
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::take_shots() {
    godot::Array out;
    if (client()) {
        const core::Loadout& own = loadout();
        for (const core::Shot& shot : client()->take_shots()) {
            const core::WeaponDef* weapon = core::weapon_in_slot(own, shot.weapon);
            if (weapon == nullptr ||
                (weapon->projectile != core::Projectile::bolt && weapon->projectile != core::Projectile::melee)) {
                continue;  // drawn from the server's snapshots
            }
            godot::Dictionary entry;
            entry["origin"] = to_godot(shot.origin);
            godot::Array directions;
            for (const core::Vec3& direction : shot.directions) {
                directions.push_back(to_godot(direction));
            }
            entry["directions"] = directions;
            entry["weapon"] = int64_t(shot.weapon);
            entry["projectile"] = weapon->projectile == core::Projectile::melee ? "melee" : "bolt";
            entry["melee_range"] = weapon->melee_range;
            entry["melee_lunge"] = weapon->melee_lunge;
            entry["melee_stagger"] = weapon->melee_stagger;
            add_bolt_look(entry, *weapon);
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::take_events() {
    godot::Array out;
    if (client()) {
        for (const core::net::CombatEvent& event : client()->take_events()) {
            godot::Dictionary entry;
            switch (event.kind) {
            case core::net::CombatEvent::hit:
                entry["kind"] = "hit";
                break;
            case core::net::CombatEvent::kill:
                entry["kind"] = "kill";
                break;
            case core::net::CombatEvent::captured:
                entry["kind"] = "captured";
                break;
            case core::net::CombatEvent::neutralized:
                entry["kind"] = "neutralized";
                break;
            // Stage 11: flags. attacker = the soldier, victim = the flag, damage = the team.
            case core::net::CombatEvent::flag_taken:
                entry["kind"] = "flag_taken";
                break;
            case core::net::CombatEvent::flag_dropped:
                entry["kind"] = "flag_dropped";
                break;
            case core::net::CombatEvent::flag_returned:
                entry["kind"] = "flag_returned";
                break;
            case core::net::CombatEvent::flag_captured:
                entry["kind"] = "flag_captured";
                break;
            }
            entry["attacker"] = int64_t(event.attacker);
            entry["victim"] = int64_t(event.victim);
            entry["damage"] = double(event.damage);
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::take_explosions() {
    godot::Array out;
    if (client()) {
        const auto remotes = client()->remotes();
        for (const core::net::Explosion& e : client()->take_explosions()) {
            godot::Dictionary entry;
            entry["position"] = to_godot(e.position);
            entry["normal"] = to_godot(e.normal);
            entry["radius"] = double(e.radius);
            entry["owner"] = int64_t(e.owner);
            if (e.weapon == core::net::object_explosion_weapon) {
                continue;  // drawn from the object's own explosion as it's seen to go (Bf2Map)
            }
            if (e.weapon == core::net::vehicle_explosion_weapon) {
                // A destroyed vehicle: its class's explosion (the wreck nearest it).
                std::string effect = "com_sfx_explosion_lg";
                double best = 1e9;
                const auto classes = client()->vehicle_classes();
                for (const core::net::VehicleState& v : client()->vehicles()) {
                    const double d = (v.position - e.position).length();
                    if (!v.alive && d < best && v.vehicle < classes.size() &&
                        !classes[v.vehicle].explosion_effect.empty()) {
                        best = d;
                        effect = classes[v.vehicle].explosion_effect;
                    }
                }
                entry["effect"] = godot::String::utf8(effect.c_str());
                entry["sound"] = godot::String("com_weap_obj_large_exp");
                entry["light_color"] = godot::Color(1.0f, 0.7f, 0.3f, 1.0f);
                entry["light_radius"] = std::max(8.0, double(e.radius) * 2.0);
                out.push_back(entry);
                continue;
            }
            if (e.weapon == core::net::droid_explosion_weapon) {
                entry["effect"] = godot::String("com_sfx_explosion_sm");
                entry["sound"] = godot::String("com_weap_obj_small_exp");
                entry["light_color"] = godot::Color(1.0f, 0.72f, 0.3f, 1.0f);
                entry["light_radius"] = 6.0;
                out.push_back(entry);
                continue;
            }
            const core::Loadout* owner_def = &loadout();
            if (e.owner != uint32_t(client()->entity_id())) {
                int team = -1, loadout_index = 0;
                for (const auto& remote : remotes) {
                    if (remote.id == e.owner) {
                        team = remote.team;
                        loadout_index = remote.loadout;
                    }
                }
                owner_def = &loadout_of(team, loadout_index);
            }
            owner_def = &weapons_of(e.owner);
            const core::WeaponDef* weapon = core::weapon_in_slot(*owner_def, e.weapon);
            entry["effect"] = weapon ? godot::String::utf8(weapon->explosion_effect.c_str()) : godot::String();
            entry["sound"] = weapon && !weapon->explosion_sound.empty()
                                 ? godot::String::utf8(weapon->explosion_sound.c_str())
                                 : godot::String("com_weap_thermaldetonator_exp");
            entry["light_color"] = weapon ? to_color(weapon->light_color) : godot::Color(1.0f, 0.7f, 0.3f, 1.0f);
            entry["light_radius"] = weapon && weapon->light_radius > 0 ? weapon->light_radius : double(e.radius) * 2.0;
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::take_impacts() {
    godot::Array out;
    if (client()) {
        const auto remotes = client()->remotes();
        for (const core::net::Impact& imp : client()->take_impacts()) {
            const core::Loadout* owner_def = &loadout();
            if (imp.owner != uint32_t(client()->entity_id())) {
                int team = -1, loadout_index = 0;
                for (const auto& remote : remotes) {
                    if (remote.id == imp.owner) {
                        team = remote.team;
                        loadout_index = remote.loadout;
                    }
                }
                owner_def = &loadout_of(team, loadout_index);
            }
            owner_def = &weapons_of(imp.owner);
            const core::WeaponDef* weapon = core::weapon_in_slot(*owner_def, imp.weapon);
            godot::Dictionary entry;
            entry["position"] = to_godot(imp.position);
            entry["normal"] = to_godot(imp.normal);
            entry["hit_character"] = imp.hit_character;
            entry["effect"] = weapon ? godot::String::utf8(weapon->impact_effect.c_str()) : godot::String("com_sfx_ord_exp");
            entry["sound"] = godot::String("imp_weap_ord_exp");
            entry["color"] = weapon ? to_color(weapon->bolt_color) : godot::Color(1.0f, 0.5f, 0.2f, 1.0f);
            out.push_back(entry);
        }
    }
    return out;
}

godot::Array SwmClient::get_pickups() const {
    godot::Array out;
    if (client()) {
        for (const core::net::PickupState& p : client()->pickups()) {
            const core::PickupDef& def = core::pickup_def(p.kind);
            godot::Dictionary entry;
            entry["id"] = int64_t(p.id);
            switch (p.kind) {
            case core::PickupKind::health_small:
                entry["kind"] = "health_small";
                break;
            case core::PickupKind::health_large:
                entry["kind"] = "health_large";
                break;
            case core::PickupKind::ammo:
                entry["kind"] = "ammo";
                break;
            case core::PickupKind::dual:
                entry["kind"] = "dual";
                break;
            case core::PickupKind::energy:
                entry["kind"] = "energy";
                break;
            }
            entry["model"] = godot::String::utf8(def.model);
            entry["position"] = to_godot(p.position);
            entry["time_left"] = double(p.time_left);
            out.push_back(entry);
        }
    }
    return out;
}

godot::Dictionary SwmClient::get_secondary() const {
    godot::Dictionary out;
    if (const core::WeaponDef* secondary = core::weapon_in_slot(loadout(), core::secondary_slot)) {
        out["name"] = godot::String::utf8(secondary->name.c_str());
        out["held_model"] = godot::String::utf8(secondary->model.c_str());
        out["fire_sound"] = godot::String::utf8(secondary->fire_sound.c_str());
    }
    return out;
}

godot::Array SwmClient::get_weapons() const {
    godot::Array out;
    const core::Loadout& carried = client() ? weapons_of(uint32_t(client()->entity_id())) : loadout();
    for (int slot = 0; slot < core::hand_slots; ++slot) {
        const core::WeaponDef* held = core::weapon_in_slot(carried, slot);
        godot::Dictionary entry;
        if (held == nullptr) {
            out.push_back(entry);
            continue;
        }
        const core::WeaponDef& w = *held;
        entry["name"] = godot::String::utf8(w.name.c_str());
        entry["held_model"] = godot::String::utf8(w.model.c_str());  // what's held: imported_assets/-relative (not "model": the ordnance's)
        entry["anim_set"] = godot::String::utf8(w.anim_set.c_str());  // rifle, pistol or bazooka
        entry["rounds_per_clip"] = int64_t(w.rounds_per_clip);
        entry["zoom_min"] = w.zoom_min;
        entry["zoom_max"] = w.zoom_max;
        entry["zoom_first_person"] = w.zoom_first_person;
        entry["projectile"] = w.projectile == core::Projectile::bolt      ? "bolt"
                              : w.projectile == core::Projectile::missile ? "missile"
                              : w.projectile == core::Projectile::grenade ? "grenade"
                                                                          : "melee";
        entry["melee_range"] = w.melee_range;
        entry["melee_lunge"] = w.melee_lunge;
        entry["melee_stagger"] = w.melee_stagger;
        entry["heat_per_shot"] = w.heat_per_shot;
        entry["bolt_speed"] = w.bolt_speed;
        entry["bolt_life"] = w.bolt_life;
        entry["bolt_color"] = godot::Color(float(w.bolt_color[0]), float(w.bolt_color[1]), float(w.bolt_color[2]),
                                           float(w.bolt_color[3]));
        entry["bolt_length"] = w.bolt_length;
        entry["bolt_width"] = w.bolt_width;
        entry["fire_sound"] = godot::String::utf8(w.fire_sound.c_str());
        entry["fire_empty_sound"] = godot::String::utf8(w.fire_empty_sound.c_str());
        entry["reload_sound"] = godot::String::utf8(w.reload_sound.c_str());
        entry["overheat_sound"] = godot::String::utf8(w.overheat_sound.c_str());
        entry["overheat_stop_sound"] = godot::String::utf8(w.overheat_stop_sound.c_str());
        entry["weapon_change_sound"] = godot::String::utf8(w.weapon_change_sound.c_str());
        out.push_back(entry);
    }
    return out;
}

double SwmClient::get_max_health() const {
    return client() ? client()->character_stats().max_health : core::stats_for(loadout_).max_health;
}

double SwmClient::get_max_shield() const {
    return client() ? client()->max_shield() : core::stats_for(loadout_).shield_capacity;
}

godot::PackedStringArray SwmClient::get_looks() const {
    godot::PackedStringArray out;
    if (client()) {
        for (const std::string& look : client()->looks()) {
            out.push_back(godot::String::utf8(look.c_str()));
        }
    }
    return out;
}

godot::Array SwmClient::get_remotes() const {
    godot::Array out;
    if (client()) {
        for (const core::GameClient::Remote& remote : client()->remotes()) {
            godot::Dictionary entry = to_dictionary(remote.state);
            entry["id"] = int64_t(remote.id);
            describe(entry, remote);
            // What it's holding: its weapon's model and how it's held.
            const core::WeaponDef* held =
                core::weapon_in_slot(loadout_of(remote.team, remote.loadout), remote.state.combat.weapon);
            entry["weapon_model"] = !remote.weapon_model.empty()
                                        ? godot::String::utf8(remote.weapon_model.c_str())
                                        : godot::String::utf8(held ? held->model.c_str() : "");
            entry["anim_set"] = !remote.anim_set.empty()
                                    ? godot::String::utf8(remote.anim_set.c_str())
                                    : godot::String::utf8(held ? held->anim_set.c_str() : "rifle");
            out.push_back(entry);
        }
    }
    return out;
}

godot::Dictionary SwmClient::get_stats() const {
    godot::Dictionary out;
    if (client()) {
        const core::GameClient::Stats& stats = client()->stats();
        out["ticks"] = int64_t(stats.ticks);
        out["snapshots"] = int64_t(stats.snapshots);
        out["corrections"] = int64_t(stats.corrections);
        out["max_correction"] = stats.max_correction;
        out["rtt_ms"] = stats.rtt * 1000;
    }
    return out;
}

godot::String SwmClient::get_bake() const {
    if (battlefield() && !battlefield()->terrain.empty() && session_ && &session_->world() != world_.get()) {
        return godot::String::utf8(battlefield()->terrain.generic_string().c_str());
    }
    // Another planet's hub (Stage 13): its bake, next to ours.
    if (!battlefield() && session_ && !session_->planet().empty()) {
        const std::filesystem::path planet = std::filesystem::path(bake_).parent_path() / session_->planet();
        return godot::String::utf8(planet.generic_string().c_str());
    }
    return godot::String::utf8(bake_.c_str());
}

double SwmClient::get_ground_height(double x, double z, double below) const {
    // For keeping a camera above the ground: the highest surface at (x, z)
    // under `below` (the terrain where it isn't cut away, buildings, a map's
    // props), so in a cave or a base under a hill it's the floor there, not
    // the hillside overhead. Nothing under it: the terrain.
    if (!world()) {
        return 0;
    }
    const double ground = world()->ground_at(x, z, below, 0).height;
    return ground > -1e8 ? ground : world()->terrain().height_at(x, z);
}

double SwmClient::get_swim_floor(double x, double z) const {
    // Deep water's swim floor (World::swim_floor_at): where a swimmer's feet
    // are; NAN where there's none.
    if (!world()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto floor = world()->swim_floor_at(x, z);
    return floor ? *floor : std::numeric_limits<double>::quiet_NaN();
}

double SwmClient::raycast(const godot::Vector3& from, const godot::Vector3& to) const {
    if (!world()) {
        return -1;
    }
    const auto t = world()->raycast({from.x, from.y, from.z}, {to.x, to.y, to.z});
    return t ? *t : -1;
}

bool SwmClient::in_battlefield(const godot::Vector3& point) const {
    return !battlefield() || battlefield()->contains({point.x, point.y, point.z});
}

void SwmClient::_bind_methods() {
    using godot::ClassDB;
    using godot::D_METHOD;
    ClassDB::bind_method(D_METHOD("in_battlefield", "point"), &SwmClient::in_battlefield);
    ClassDB::bind_method(D_METHOD("set_simulated_network", "latency_ms", "jitter_ms", "loss"),
                         &SwmClient::set_simulated_network);
    ClassDB::bind_method(D_METHOD("add_building", "collision_path", "position", "yaw_degrees"),
                         &SwmClient::add_building);
    ClassDB::bind_method(D_METHOD("start", "server", "bake_prefix", "name"), &SwmClient::start);
    ClassDB::bind_method(D_METHOD("set_loadout", "path"), &SwmClient::set_loadout);
    ClassDB::bind_method(D_METHOD("set_battlefield", "path"), &SwmClient::set_battlefield);
    ClassDB::bind_method(D_METHOD("set_world_vehicles", "dir"), &SwmClient::set_world_vehicles);
    ClassDB::bind_method(D_METHOD("get_vehicle_classes"), &SwmClient::get_vehicle_classes);
    ClassDB::bind_method(D_METHOD("set_bot", "enabled"), &SwmClient::set_bot);
    ClassDB::bind_method(D_METHOD("set_battlefield_dir", "path"), &SwmClient::set_battlefield_dir);
    ClassDB::bind_method(D_METHOD("get_generation"), &SwmClient::get_generation);
    ClassDB::bind_method(D_METHOD("get_bake"), &SwmClient::get_bake);
    ClassDB::bind_method(D_METHOD("get_session_error"), &SwmClient::get_session_error);
    ClassDB::bind_method(D_METHOD("get_recruiters"), &SwmClient::get_recruiters);
    ClassDB::bind_method(D_METHOD("get_quartermasters"), &SwmClient::get_quartermasters);
    ClassDB::bind_method(D_METHOD("get_vendors"), &SwmClient::get_vendors);
    ClassDB::bind_method(D_METHOD("get_targets"), &SwmClient::get_targets);
    ClassDB::bind_method(D_METHOD("get_vehicles"), &SwmClient::get_vehicles);
    ClassDB::bind_method(D_METHOD("get_corpses"), &SwmClient::get_corpses);
    ClassDB::bind_method(D_METHOD("get_factions"), &SwmClient::get_factions);
    ClassDB::bind_method(D_METHOD("queue", "team", "mode"), &SwmClient::queue, DEFVAL("conquest"));
    ClassDB::bind_method(D_METHOD("get_game_modes"), &SwmClient::get_game_modes);
    ClassDB::bind_method(D_METHOD("get_player_status"), &SwmClient::get_player_status);
    ClassDB::bind_method(D_METHOD("get_inventory"), &SwmClient::get_inventory);
    ClassDB::bind_method(D_METHOD("get_reputation"), &SwmClient::get_reputation);
    ClassDB::bind_method(D_METHOD("get_character_loadouts"), &SwmClient::get_character_loadouts);
    ClassDB::bind_method(D_METHOD("get_custom_loadouts"), &SwmClient::get_custom_loadouts);
    ClassDB::bind_method(D_METHOD("save_loadout", "slot_index", "loadout"), &SwmClient::save_loadout);
    ClassDB::bind_method(D_METHOD("switch_loadout", "slot_index"), &SwmClient::switch_loadout);
    ClassDB::bind_method(D_METHOD("buy_item", "def_id", "use_faction_points"), &SwmClient::buy_item, DEFVAL(true));
    ClassDB::bind_method(D_METHOD("destroy_item", "item_id"), &SwmClient::destroy_item);
    ClassDB::bind_method(D_METHOD("take_loot", "corpse_entity", "slot"), &SwmClient::take_loot, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("sell_item", "item_id"), &SwmClient::sell_item);
    ClassDB::bind_method(D_METHOD("sell_all_junk"), &SwmClient::sell_all_junk);
    ClassDB::bind_method(D_METHOD("use_item", "item_id"), &SwmClient::use_item);
    ClassDB::bind_method(D_METHOD("get_quest_npcs"), &SwmClient::get_quest_npcs);
    ClassDB::bind_method(D_METHOD("get_mission_terminals"), &SwmClient::get_mission_terminals);
    ClassDB::bind_method(D_METHOD("get_quests"), &SwmClient::get_quests);
    ClassDB::bind_method(D_METHOD("get_professions"), &SwmClient::get_professions);
    ClassDB::bind_method(D_METHOD("get_bank"), &SwmClient::get_bank);
    ClassDB::bind_method(D_METHOD("service", "action", "id", "amount", "item_id"), &SwmClient::service);
    ClassDB::bind_method(D_METHOD("specialist", "action", "id", "target", "position", "amount", "item_id", "text"),
                         &SwmClient::specialist);
    ClassDB::bind_method(D_METHOD("get_specialist"), &SwmClient::get_specialist);
    ClassDB::bind_method(D_METHOD("get_field"), &SwmClient::get_field);
    ClassDB::bind_method(D_METHOD("get_specialist_book", "path"), &SwmClient::get_specialist_book);
    ClassDB::bind_method(D_METHOD("get_workbenches"), &SwmClient::get_workbenches);
    ClassDB::bind_method(D_METHOD("get_materials"), &SwmClient::get_materials);
    ClassDB::bind_method(D_METHOD("get_presets"), &SwmClient::get_presets);
    ClassDB::bind_method(D_METHOD("gather_extract", "category_name"), &SwmClient::gather_extract);
    ClassDB::bind_method(D_METHOD("harvest_corpse", "corpse_entity"), &SwmClient::harvest_corpse);
    ClassDB::bind_method(D_METHOD("salvage_item", "item_id"), &SwmClient::salvage_item);
    ClassDB::bind_method(D_METHOD("craft_item", "schematic_id", "resource_ids", "enhancement_ids", "exp_quality_pct", "batch_count"),
                         &SwmClient::craft_item);
    ClassDB::bind_method(D_METHOD("save_recipe_preset", "name", "schematic_id", "resource_ids", "enhancement_defs", "exp_quality_pct"),
                         &SwmClient::save_recipe_preset);
    ClassDB::bind_method(D_METHOD("delete_recipe_preset", "name"), &SwmClient::delete_recipe_preset);
    ClassDB::bind_method(D_METHOD("fulfill_work_order", "order_id"), &SwmClient::fulfill_work_order);
    ClassDB::bind_method(D_METHOD("talk_to", "npc_entity"), &SwmClient::talk_to);
    ClassDB::bind_method(D_METHOD("use_mission_terminal", "index"), &SwmClient::use_mission_terminal);
    ClassDB::bind_method(D_METHOD("accept_quest", "id"), &SwmClient::accept_quest);
    ClassDB::bind_method(D_METHOD("abandon_quest", "id"), &SwmClient::abandon_quest);
    ClassDB::bind_method(D_METHOD("get_battlefield"), &SwmClient::get_battlefield);
    ClassDB::bind_method(D_METHOD("is_allied", "a", "b"), &SwmClient::is_allied);
    ClassDB::bind_method(D_METHOD("get_match"), &SwmClient::get_match);
    ClassDB::bind_method(D_METHOD("request_spawn", "loadout", "post"), &SwmClient::request_spawn);
    ClassDB::bind_method(D_METHOD("set_input", "controls"), &SwmClient::set_input);
    ClassDB::bind_method(D_METHOD("get_bolts"), &SwmClient::get_bolts);
    ClassDB::bind_method(D_METHOD("take_shots"), &SwmClient::take_shots);
    ClassDB::bind_method(D_METHOD("take_events"), &SwmClient::take_events);
    ClassDB::bind_method(D_METHOD("take_explosions"), &SwmClient::take_explosions);
    ClassDB::bind_method(D_METHOD("take_impacts"), &SwmClient::take_impacts);
    ClassDB::bind_method(D_METHOD("get_pickups"), &SwmClient::get_pickups);
    ClassDB::bind_method(D_METHOD("get_secondary"), &SwmClient::get_secondary);
    ClassDB::bind_method(D_METHOD("get_weapons"), &SwmClient::get_weapons);
    ClassDB::bind_method(D_METHOD("get_max_health"), &SwmClient::get_max_health);
    ClassDB::bind_method(D_METHOD("get_max_shield"), &SwmClient::get_max_shield);
    ClassDB::bind_method(D_METHOD("update"), &SwmClient::update);
    ClassDB::bind_method(D_METHOD("leave"), &SwmClient::leave);
    ClassDB::bind_method(D_METHOD("get_status"), &SwmClient::get_status);
    ClassDB::bind_method(D_METHOD("is_spawned"), &SwmClient::is_spawned);
    ClassDB::bind_method(D_METHOD("get_entity_id"), &SwmClient::get_entity_id);
    ClassDB::bind_method(D_METHOD("get_character_id"), &SwmClient::get_character_id);
    ClassDB::bind_method(D_METHOD("get_refusal"), &SwmClient::get_refusal);
    ClassDB::bind_method(D_METHOD("get_presented"), &SwmClient::get_presented);
    ClassDB::bind_method(D_METHOD("get_remotes"), &SwmClient::get_remotes);
    ClassDB::bind_method(D_METHOD("get_looks"), &SwmClient::get_looks);
    ClassDB::bind_method(D_METHOD("get_stats"), &SwmClient::get_stats);
    ClassDB::bind_method(D_METHOD("get_ground_height", "x", "z", "below"), &SwmClient::get_ground_height,
                         DEFVAL(1e9));
    ClassDB::bind_method(D_METHOD("get_swim_floor", "x", "z"), &SwmClient::get_swim_floor);
    ClassDB::bind_method(D_METHOD("raycast", "from", "to"), &SwmClient::raycast);
}

SwmServer::~SwmServer() {
    stop();
}

void SwmServer::set_database(const godot::String& path) {
    database_ = to_std(path);
}

godot::Error SwmServer::add_building(const godot::String& collision_path, const godot::Vector3& position,
                                     double yaw_degrees) {
    return load_building(buildings_, collision_path, position, yaw_degrees);
}

godot::Error SwmServer::set_loadout(const godot::String& path) {
    return load_loadout_file(loadout_, path);
}

godot::Error SwmServer::set_battlefield(const godot::String& path) {
    return load_battlefield_file(battlefield_, path);
}

void SwmServer::set_bots(int64_t count) {
    bot_count_ = int(std::clamp<int64_t>(count, 0, 64));
}

godot::Error SwmServer::set_hub(const godot::String& path) {
    try {
        hub_config_ = core::load_hub(to_std(path));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("set_hub: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    return godot::OK;
}

godot::Error SwmServer::set_galaxy(const godot::String& path) {
    try {
        galaxy_config_ = core::load_galaxy(to_std(path));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("set_galaxy: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    return godot::OK;
}

godot::Error SwmServer::set_catalog(const godot::String& catalog_ini, const godot::String& bf2_items_dir) {
    try {
        catalog_.load(to_std(catalog_ini), to_std(bf2_items_dir));
        has_catalog_ = true;
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("set_catalog: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    return godot::OK;
}

godot::Error SwmServer::set_world_vehicles(const godot::String& dir) {
    try {
        world_vehicles_ = core::load_vehicle_classes(to_std(dir));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("set_world_vehicles: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    return godot::OK;
}

godot::Error SwmServer::set_professions(const godot::String& path) {
    try {
        professions_.load(to_std(path));
        has_professions_ = true;
        // Stage 12: the specialists' abilities and companions, next to it.
        const std::filesystem::path specialists = std::filesystem::path(to_std(path)).parent_path() / "specialists.ini";
        if (std::filesystem::exists(specialists)) {
            specialists_.load(specialists);
            has_specialists_ = true;
        }
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("set_professions: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    return godot::OK;
}

godot::Error SwmServer::set_crafting(const godot::String& resources_path, const godot::String& schematics_path) {
    try {
        crafting_.load_resources(to_std(resources_path));
        crafting_.load_schematics(to_std(schematics_path));
        has_crafting_ = true;
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("set_crafting: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    return godot::OK;
}

godot::Error SwmServer::start(int64_t port, const godot::String& bake_prefix, const godot::Vector2& spawn) {
    stop();  // the server refers to the ground, store and link replaced below
    if (galaxy_config_) {
        // This server's own planet (its bake's) is the galaxy's home: hosted
        // on Coronet, Corellia is home, not the galaxy's first planet.
        const std::string stem = std::filesystem::path(to_std(bake_prefix)).filename().string();
        auto& planets = galaxy_config_->planets;
        const auto own = std::find_if(planets.begin(), planets.end(),
                                      [&](const core::PlanetConfig& p) { return p.stem() == stem; });
        if (own != planets.end()) {
            std::rotate(planets.begin(), own, own + 1);
        }
    }
    try {
        ground_ = core::load_bake(to_std(bake_prefix));
        world_ = make_world(ground_, to_std(bake_prefix), buildings_);
        if (!database_.empty()) {
            store_ = std::make_unique<core::SqliteCharacterStore>(database_);
        }
        udp_ = std::make_unique<core::net::UdpLink>(uint16_t(port));
    } catch (const std::exception& error) {
        godot::UtilityFunctions::push_error("SwmServer: ", error.what());
        return godot::ERR_CANT_OPEN;
    }
    core::ServerConfig config;
    config.loadout = loadout_;
    config.battlefield = battlefield_;
    config.catalog = has_catalog_ ? &catalog_ : nullptr;
    config.professions = has_professions_ ? &professions_ : nullptr;
    config.specialists = has_specialists_ ? &specialists_ : nullptr;
    if (!battlefield_) {
        config.world_vehicles = world_vehicles_;
    }
    config.crafting = has_crafting_ ? &crafting_ : nullptr;
    config.spawn = {-spawn.x, 0, spawn.y};  // Galaxies to game coordinates
    if (galaxy_config_ && !battlefield_) {
        config.planet = galaxy_config_->planets[0].name;
    }
    server_ = std::make_unique<core::GameServer>(*udp_, *world_, config, store_.get());
    server_->log = [](const std::string& line) { godot::UtilityFunctions::print("server: ", line.c_str()); };
    if (hub_config_) {
        core::HubConfig hub_config = *hub_config_;
        if (galaxy_config_) {
            hub_config.planet = galaxy_config_->planets[0].stem();
        }
        hub_ = std::make_unique<core::Hub>(*server_, udp_->port(), *world_, std::move(hub_config), hub_network_);
        hub_->log = server_->log;
        if (galaxy_config_) {
            try {
                galaxy_ = std::make_unique<core::Galaxy>(*server_, udp_->port(), *galaxy_config_, config, store_.get(),
                                                         hub_network_, server_->log);
            } catch (const std::exception& error) {
                godot::UtilityFunctions::push_error("SwmServer: galaxy: ", error.what());
            }
        }
    }
    if (bot_count_ > 0 || (battlefield_ && battlefield_->natives)) {  // (and the battlefield's natives)
        core::ClientConfig bot_config;
        bot_config.server = *core::net::parse_endpoint("127.0.0.1:" + std::to_string(udp_->port()));
        bot_config.loadout = loadout_;
        bot_config.battlefield = battlefield_;
        try {
            bots_ = std::make_unique<core::BotPlayers>(bot_count_, *world_, bot_config, core::BotPlayers::LinkFactory{}, bot_skill_);
        } catch (const std::exception& error) {
            godot::UtilityFunctions::push_error("SwmServer: bots: ", error.what());
        }
    }
    return godot::OK;
}

void SwmServer::update() {
    if (server_) {
        const double now = seconds_since(start_time_);
        server_->update(now);
        if (bots_) {
            bots_->update(now);
        }
        if (hub_) {
            hub_->update(now);
        }
        if (galaxy_) {
            galaxy_->update(now);
        }
    }
}

void SwmServer::stop() {
    if (galaxy_ && galaxy_config_) {
        for (const auto& planet : galaxy_config_->planets) {
            if (core::GameServer* other = galaxy_->server(planet.name); other && other != server_.get()) {
                other->save_all();
            }
        }
    }
    galaxy_.reset();
    hub_.reset();
    if (bots_) {
        bots_->disconnect();
    }
    bots_.reset();
    if (server_) {
        server_->save_all();
    }
    server_.reset();
    store_.reset();
    udp_.reset();
}

int64_t SwmServer::get_client_count() const {
    return server_ ? int64_t(server_->client_count()) : 0;
}

int64_t SwmServer::get_port() const {
    return udp_ ? udp_->port() : 0;
}

void SwmServer::_bind_methods() {
    using godot::ClassDB;
    using godot::D_METHOD;
    ClassDB::bind_method(D_METHOD("set_database", "path"), &SwmServer::set_database);
    ClassDB::bind_method(D_METHOD("set_loadout", "path"), &SwmServer::set_loadout);
    ClassDB::bind_method(D_METHOD("set_battlefield", "path"), &SwmServer::set_battlefield);
    ClassDB::bind_method(D_METHOD("set_bots", "count"), &SwmServer::set_bots);
    ClassDB::bind_method(D_METHOD("set_hub", "path"), &SwmServer::set_hub);
    ClassDB::bind_method(D_METHOD("set_galaxy", "path"), &SwmServer::set_galaxy);
    ClassDB::bind_method(D_METHOD("set_catalog", "catalog_ini", "bf2_items_dir"), &SwmServer::set_catalog);
    ClassDB::bind_method(D_METHOD("set_professions", "path"), &SwmServer::set_professions);
    ClassDB::bind_method(D_METHOD("set_world_vehicles", "dir"), &SwmServer::set_world_vehicles);
    ClassDB::bind_method(D_METHOD("set_crafting", "resources_path", "schematics_path"), &SwmServer::set_crafting);
    ClassDB::bind_method(D_METHOD("add_building", "collision_path", "position", "yaw_degrees"),
                         &SwmServer::add_building);
    ClassDB::bind_method(D_METHOD("start", "port", "bake_prefix", "spawn"), &SwmServer::start);
    ClassDB::bind_method(D_METHOD("update"), &SwmServer::update);
    ClassDB::bind_method(D_METHOD("stop"), &SwmServer::stop);
    ClassDB::bind_method(D_METHOD("get_client_count"), &SwmServer::get_client_count);
    ClassDB::bind_method(D_METHOD("get_port"), &SwmServer::get_port);
}

} // namespace swm::godot_bindings
