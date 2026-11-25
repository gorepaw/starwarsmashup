#include "swm_store.h"

#include <cmath>
#include <cstdio>
#include <exception>

#include <godot_cpp/core/class_db.hpp>

#include "swm/core/species.h"

namespace swm::godot_bindings {

namespace {

std::string to_std(const godot::String& s) {
    return s.utf8().get_data();
}

godot::Dictionary to_dict(const core::CharacterRecord& r) {
    godot::Dictionary out;
    out["id"] = int64_t(r.id);
    out["name"] = godot::String::utf8(r.name.c_str());
    out["species"] = godot::String::utf8(r.species.c_str());
    out["gender"] = godot::String::utf8(r.gender.c_str());
    out["location"] = godot::String::utf8(r.location.c_str());
    out["appearance"] = godot::String::utf8(r.appearance.c_str());
    out["position"] = godot::Vector3(real_t(r.position.x), real_t(r.position.y), real_t(r.position.z));
    out["yaw"] = double(r.yaw);
    out["credits"] = int64_t(r.credits);
    out["faction_points"] = int64_t(r.faction_points);
    out["experience"] = int64_t(r.experience);
    out["active_loadout"] = int64_t(r.active_loadout);
    return out;
}

godot::Dictionary to_dict(const core::ItemInstance& inst) {
    godot::Dictionary out;
    out["id"] = int64_t(inst.id);
    out["owner_id"] = int64_t(inst.owner_id);
    out["def_id"] = godot::String::utf8(inst.def_id.c_str());
    out["quality"] = inst.quality;
    out["quantity"] = int64_t(inst.quantity);
    out["custom_name"] = godot::String::utf8(inst.custom_name.c_str());
    out["color_json"] = godot::String::utf8(inst.color_json.c_str());
    return out;
}

godot::Dictionary to_dict(const core::CharacterLoadout& lo) {
    godot::Dictionary out;
    out["slot_index"] = int64_t(lo.slot_index);
    out["name"] = godot::String::utf8(lo.name.c_str());
    out["primary_id"] = int64_t(lo.primary_id);
    out["sidearm_id"] = int64_t(lo.sidearm_id);
    out["secondary_id"] = int64_t(lo.secondary_id);
    godot::Dictionary gear, app;
    for (const auto& [k, v] : lo.gear_ids) {
        gear[godot::String::utf8(k.c_str())] = int64_t(v);
    }
    for (const auto& [k, v] : lo.appearance_ids) {
        app[godot::String::utf8(k.c_str())] = int64_t(v);
    }
    out["gear_ids"] = gear;
    out["appearance_ids"] = app;
    godot::Array specializations;
    for (const std::string& id : lo.specializations) {
        specializations.push_back(godot::String::utf8(id.c_str()));
    }
    out["specializations"] = specializations;
    out["companion"] = godot::String::utf8(lo.companion.c_str());
    return out;
}

// A perk in words: "+2% run speed", "5% tighter spread".
godot::String perk_line(const core::StatModifier& m) {
    std::string name(core::stat_name(m.stat));
    std::string what = name;
    if (name.rfind("mitigation", 0) == 0) {
        what = name == "mitigation" ? "resistance to everything" : name.substr(11) + " resistance";
    } else if (name == "explosion_push_resist") {
        what = "blast knockback resistance";
    } else {
        for (char& c : what) {
            if (c == '_') c = ' ';
        }
    }
    char text[96];
    if (m.add != 0) {
        const bool share = name.rfind("mitigation", 0) == 0 || name == "explosion_push_resist";
        if (share) {
            std::snprintf(text, sizeof text, "%+.1f%% %s", m.add * 100, what.c_str());
        } else {
            const char* unit = name == "shield_delay" ? "s" : "";
            std::snprintf(text, sizeof text, "%+.0f%s %s", m.add, unit, what.c_str());
        }
    } else {
        std::snprintf(text, sizeof text, "%+.0f%% %s", (m.multiply - 1) * 100, what.c_str());
    }
    std::string s = text;
    if (s.find(".0%") != std::string::npos) s.erase(s.find(".0%"), 2);
    return godot::String::utf8(s.c_str());
}

godot::String perk_line(const core::WeaponModifier& m) {
    const double pct = std::abs(m.multiply - 1) * 100;
    const bool less = m.multiply < 1;
    const char* what = "";
    switch (m.field) {
    case core::WeaponModifier::spread: what = less ? "tighter spread" : "wider spread"; break;
    case core::WeaponModifier::heat: what = less ? "less heat" : "more heat"; break;
    case core::WeaponModifier::reload: what = less ? "faster reload" : "slower reload"; break;
    case core::WeaponModifier::recovery: what = less ? "slower bloom recovery" : "faster bloom recovery"; break;
    case core::WeaponModifier::range: what = less ? "shorter full-damage range" : "longer full-damage range"; break;
    case core::WeaponModifier::speed: what = less ? "faster swings" : "slower swings"; break;
    case core::WeaponModifier::lunge: what = less ? "shorter lunge" : "stronger lunge"; break;
    case core::WeaponModifier::disrupt: what = less ? "weaker shield disruption" : "stronger shield disruption"; break;
    }
    char text[96];
    std::snprintf(text, sizeof text, "%.0f%% %s", pct, what);
    return godot::String::utf8(text);
}

godot::Array perk_lines(const core::ProfessionPerk& perk) {
    godot::Array out;
    for (const core::StatModifier& m : perk.stats) out.push_back(perk_line(m));
    for (const core::WeaponModifier& m : perk.weapons) out.push_back(perk_line(m));
    return out;
}

godot::Dictionary to_dict(const core::ItemDef& d, const core::ItemCatalog& catalog) {
    godot::Dictionary out;
    out["id"] = godot::String::utf8(d.id.c_str());
    out["name"] = godot::String::utf8(d.display_name.c_str());
    out["description"] = godot::String::utf8(d.description.c_str());
    out["kind"] = godot::String::utf8(std::string(core::item_kind_name(d.kind)).c_str());
    out["slot"] = godot::String::utf8(d.slot.c_str());
    out["archetype"] = godot::String::utf8(core::weapon_archetype_info(d.archetype).id.c_str());
    out["anim_set"] = godot::String::utf8(d.anim_set.c_str());
    out["faction"] = godot::String::utf8(d.faction.c_str());
    out["required_rank"] = godot::String::utf8(std::string(core::reputation_tier_name(d.required_rank)).c_str());
    out["required_rep"] = int64_t(core::reputation_threshold(d.required_rank));
    out["cost_fp"] = int64_t(d.cost_faction_points);
    out["cost_credits"] = int64_t(d.cost_credits);
    out["sell_value"] = int64_t(d.sell_value);
    out["heal_amount"] = d.heal_amount;
    out["ammo_class"] = godot::String::utf8(std::string(core::ammo_class_name(d.ammo_class)).c_str());
    out["ammo_batch"] = int64_t(d.ammo_batch);
    out["damage_mult"] = d.damage_mult;
    out["rate_mult"] = d.rate_mult;
    out["heat_mult"] = d.heat_mult;
    out["spread_mult"] = d.spread_mult;
    if (d.damage_type) {
        const std::string_view dt = core::damage_type_name(*d.damage_type);
        out["damage_type"] = godot::String::utf8(dt.data(), int(dt.size()));
    }
    out["model"] = godot::String::utf8(d.model_path.c_str());

    godot::Array mods;
    for (const core::StatModifier& m : d.modifiers) {
        godot::Dictionary md;
        md["stat"] = int64_t(m.stat);
        const std::string_view sname = core::stat_name(m.stat);
        md["stat_name"] = godot::String::utf8(sname.data(), int(sname.size()));
        md["add"] = m.add;
        md["multiply"] = m.multiply;
        mods.push_back(md);
    }
    out["modifiers"] = mods;

    if (d.kind == core::ItemKind::weapon) {
        const core::WeaponDef w = catalog.resolve_weapon(d, 0.5, false);
        godot::Dictionary ws;
        ws["damage"] = w.damage;
        ws["shot_delay"] = w.shot_delay;
        ws["rounds_per_clip"] = int64_t(w.rounds_per_clip);
        ws["heat_per_shot"] = w.heat_per_shot;
        ws["reload_time"] = w.reload_time;
        ws["bolt_speed"] = w.bolt_speed;
        ws["bolt_life"] = w.bolt_life;
        ws["yaw_spread"] = w.yaw_spread;
        ws["shots_per_salvo"] = int64_t(w.shots_per_salvo);
        ws["zoom_max"] = w.zoom_max;
        ws["explosion_damage"] = w.explosion_damage;
        ws["explosion_outer"] = w.explosion_outer;
        const std::string_view dt_name = core::damage_type_name(w.damage_type);
        ws["damage_type"] = godot::String::utf8(dt_name.data(), int(dt_name.size()));
        ws["bolt_color"] = godot::String::utf8(d.bolt_color_name.c_str());
        out["weapon_stats"] = ws;
    }
    return out;
}

} // namespace

godot::Error SwmCharacterStore::open(const godot::String& path) {
    last_error_.clear();
    store_.reset();
    try {
        store_ = std::make_unique<core::SqliteCharacterStore>(to_std(path));
        return godot::OK;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return godot::ERR_CANT_OPEN;
    }
}

void SwmCharacterStore::close() {
    store_.reset();
}

godot::Array SwmCharacterStore::list_characters() {
    godot::Array out;
    if (!store_) {
        return out;
    }
    try {
        for (const core::CharacterRecord& r : store_->list_all()) {
            out.push_back(to_dict(r));
        }
    } catch (const std::exception& e) {
        last_error_ = e.what();
    }
    return out;
}

godot::Dictionary SwmCharacterStore::create_character(const godot::String& name, const godot::String& species,
                                                      const godot::String& gender, const godot::String& appearance_json,
                                                      const godot::String& location, const godot::Vector3& position,
                                                      double yaw) {
    last_error_.clear();
    if (!store_) {
        last_error_ = "database not open";
        return {};
    }
    try {
        core::CharacterRecord req;
        req.name = to_std(name);
        req.species = to_std(species);
        req.gender = to_std(gender);
        req.appearance = to_std(appearance_json);
        req.location = to_std(location);
        req.position = {position.x, position.y, position.z};
        req.yaw = float(yaw);
        return to_dict(store_->create_character(req));
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return {};
    }
}

bool SwmCharacterStore::delete_character(int64_t id) {
    last_error_.clear();
    if (!store_) {
        last_error_ = "database not open";
        return false;
    }
    try {
        return store_->remove(core::PersistentId(id));
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

godot::String SwmCharacterStore::get_last_error() const {
    return godot::String::utf8(last_error_.c_str());
}

godot::Error SwmCharacterStore::load_catalog(const godot::String& catalog_ini, const godot::String& bf2_items_dir) {
    last_error_.clear();
    try {
        catalog_.load(to_std(catalog_ini), to_std(bf2_items_dir));
        return godot::OK;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return godot::ERR_CANT_OPEN;
    }
}

godot::Array SwmCharacterStore::get_catalog() const {
    godot::Array out;
    for (const core::ItemDef& d : catalog_.all()) {
        out.push_back(to_dict(d, catalog_));
    }
    return out;
}

godot::Array SwmCharacterStore::list_items(int64_t char_id) {
    godot::Array out;
    if (!store_) return out;
    try {
        for (const core::ItemInstance& inst : store_->list_items(core::PersistentId(char_id))) {
            out.push_back(to_dict(inst));
        }
    } catch (const std::exception& e) {
        last_error_ = e.what();
    }
    return out;
}

godot::Dictionary SwmCharacterStore::grant_item(int64_t char_id, const godot::String& def_id, double quality) {
    last_error_.clear();
    if (!store_) {
        last_error_ = "database not open";
        return {};
    }
    try {
        core::ItemInstance inst;
        inst.owner_id = core::PersistentId(char_id);
        inst.def_id = to_std(def_id);
        inst.quality = quality;
        return to_dict(store_->grant_item(inst));
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return {};
    }
}

bool SwmCharacterStore::remove_item(int64_t item_id) {
    last_error_.clear();
    if (!store_) return false;
    try {
        return store_->remove_item(core::PersistentId(item_id));
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

godot::Dictionary SwmCharacterStore::get_reputation(int64_t char_id) {
    godot::Dictionary out;
    for (const std::string_view pf : core::playable_factions) {
        out[godot::String::utf8(pf.data(), int(pf.size()))] = int64_t(0);
    }
    if (!store_) return out;
    try {
        for (const auto& [faction, rep] : store_->list_reputation(core::PersistentId(char_id))) {
            out[godot::String::utf8(faction.c_str())] = int64_t(rep);
        }
    } catch (const std::exception& e) {
        last_error_ = e.what();
    }
    return out;
}

godot::Array SwmCharacterStore::list_loadouts(int64_t char_id) {
    godot::Array out;
    if (!store_) return out;
    try {
        for (const core::CharacterLoadout& lo : store_->list_loadouts(core::PersistentId(char_id))) {
            out.push_back(to_dict(lo));
        }
    } catch (const std::exception& e) {
        last_error_ = e.what();
    }
    return out;
}

godot::Error SwmCharacterStore::load_professions(const godot::String& path) {
    last_error_.clear();
    try {
        professions_.load(to_std(path));
        return godot::OK;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return godot::ERR_CANT_OPEN;
    }
}

godot::Array SwmCharacterStore::get_professions() const {
    godot::Array out;
    for (const core::ProfessionDef& def : professions_.all()) {
        godot::Dictionary entry;
        entry["id"] = godot::String::utf8(def.id.c_str());
        entry["name"] = godot::String::utf8(def.name.c_str());
        entry["description"] = godot::String::utf8(def.description.c_str());
        entry["category"] = godot::String::utf8(std::string(core::profession_category_name(def.category)).c_str());
        entry["wave"] = godot::String::utf8(def.wave.c_str());
        entry["available"] = def.available;
        entry["slotted"] = def.slotted;
        entry["armor_class"] = godot::String::utf8(def.armor_class.c_str());
        entry["earns"] = godot::String::utf8(def.earns.c_str());
        entry["rank_every"] = int64_t(def.rank_every);
        entry["rank"] = perk_lines(def.rank);
        godot::Array milestones;
        for (const core::ProfessionMilestone& m : def.milestones) {
            godot::Dictionary ms;
            ms["level"] = int64_t(m.level);
            ms["name"] = godot::String::utf8(m.name.c_str());
            ms["description"] = godot::String::utf8(m.description.c_str());
            ms["perks"] = perk_lines(m.perk);
            ms["cost"] = int64_t(core::milestone_cost(m.level));  // what a trainer asks for teaching it
            milestones.push_back(ms);
        }
        entry["milestones"] = milestones;
        out.push_back(entry);
    }
    return out;
}

godot::Dictionary SwmCharacterStore::profession_level(int64_t xp) const {
    const int level = professions_.level_for(xp);
    godot::Dictionary out;
    out["level"] = int64_t(level);
    out["into"] = xp - professions_.xp_for_level(level);
    out["needed"] = professions_.xp_to_next(level);
    return out;
}

godot::Error SwmCharacterStore::load_crafting(const godot::String& resources_path,
                                              const godot::String& schematics_path) {
    last_error_.clear();
    try {
        crafting_.load_resources(to_std(resources_path));
        crafting_.load_schematics(to_std(schematics_path));
        return godot::OK;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return godot::ERR_CANT_OPEN;
    }
}

godot::Array SwmCharacterStore::get_resources() const {
    godot::Array out;
    for (const core::ResourceDef& r : crafting_.resources()) {
        godot::Dictionary d;
        d["id"] = godot::String::utf8(r.id.c_str());
        d["name"] = godot::String::utf8(r.name.c_str());
        d["category"] = godot::String::utf8(std::string(core::resource_category_name(r.category)).c_str());
        d["category_label"] = godot::String::utf8(std::string(core::resource_category_label(r.category)).c_str());
        d["profession"] = godot::String::utf8(r.profession.c_str());
        godot::Dictionary stats;
        for (size_t i = 0; i < core::resource_stat_count; ++i) {
            const std::string_view sname = core::resource_stat_name(core::ResourceStat(i));
            stats[godot::String::utf8(sname.data(), int(sname.size()))] = int64_t(r.stats[i]);
        }
        d["stats"] = stats;
        out.push_back(d);
    }
    return out;
}

godot::Array SwmCharacterStore::get_schematics() const {
    godot::Array out;
    for (const core::SchematicDef& s : crafting_.schematics()) {
        godot::Dictionary d;
        d["id"] = godot::String::utf8(s.id.c_str());
        d["name"] = godot::String::utf8(s.name.c_str());
        d["profession"] = godot::String::utf8(s.profession.c_str());
        d["required_level"] = int64_t(s.required_level);
        d["output_item"] = godot::String::utf8(s.output_item.c_str());
        d["base_xp"] = s.base_xp;
        d["enhancement_slots"] = int64_t(s.enhancement_slots);
        godot::Array slots, weights;
        for (const core::SchematicSlot& sl : s.slots) {
            godot::Dictionary sd;
            sd["category"] = godot::String::utf8(std::string(core::resource_category_name(sl.category)).c_str());
            sd["category_label"] = godot::String::utf8(std::string(core::resource_category_label(sl.category)).c_str());
            sd["amount"] = int64_t(sl.amount);
            slots.push_back(sd);
        }
        for (const core::StatWeight& w : s.weights) {
            godot::Dictionary wd;
            wd["stat"] = godot::String::utf8(std::string(core::resource_stat_name(w.stat)).c_str());
            wd["label"] = godot::String::utf8(std::string(core::resource_stat_label(w.stat)).c_str());
            wd["weight"] = w.weight;
            weights.push_back(wd);
        }
        d["slots"] = slots;
        d["weights"] = weights;
        out.push_back(d);
    }
    return out;
}

godot::Array SwmCharacterStore::get_work_orders() const {
    godot::Array out;
    for (const core::WorkOrder& wo : crafting_.work_orders()) {
        godot::Dictionary d;
        d["id"] = godot::String::utf8(wo.id.c_str());
        d["title"] = godot::String::utf8(wo.title.c_str());
        d["profession"] = godot::String::utf8(wo.profession.c_str());
        d["output_item"] = godot::String::utf8(wo.output_item.c_str());
        d["count"] = int64_t(wo.count);
        d["reward_credits"] = wo.reward_credits;
        d["reward_fp"] = wo.reward_fp;
        d["reward_xp"] = wo.reward_xp;
        out.push_back(d);
    }
    return out;
}

godot::Dictionary SwmCharacterStore::scan_terrain(const godot::String& category, double x, double z, double radius,
                                                  int64_t grid_n) const {
    godot::Dictionary out;
    const auto cat = core::resource_category_from_name(to_std(category));
    if (!cat) return out;
    const core::ScanResult res = crafting_.scan_terrain(*cat, x, z, radius, int(grid_n));
    out["resource_id"] = godot::String::utf8(res.best_resource_id.c_str());
    out["resource_name"] = godot::String::utf8(res.best_resource_name.c_str());
    out["concentration"] = res.concentration;
    out["peak_x"] = res.peak_x;
    out["peak_z"] = res.peak_z;
    out["grid_n"] = int64_t(res.grid_n);
    out["step"] = res.step;
    godot::PackedFloat32Array grid;
    grid.resize(int(res.grid.size()));
    for (size_t i = 0; i < res.grid.size(); ++i) grid[int(i)] = res.grid[i];
    out["grid"] = grid;
    return out;
}

godot::PackedStringArray SwmCharacterStore::auto_fill_schematic(const godot::String& schematic_id,
                                                                const godot::Array& materials,
                                                                const godot::String& mode) const {
    godot::PackedStringArray out;
    const core::SchematicDef* s = crafting_.find_schematic(to_std(schematic_id));
    if (s == nullptr) return out;
    std::vector<core::MaterialStack> vault;
    vault.reserve(size_t(materials.size()));
    for (int i = 0; i < materials.size(); ++i) {
        godot::Dictionary d = materials[i];
        vault.push_back({to_std(d.get("resource_id", "")), int64_t(d.get("amount", 0))});
    }
    const std::string m = to_std(mode);
    const core::AutoFillMode fill_mode = m == "best_quality" ? core::AutoFillMode::best_quality
                                         : m == "cheapest"   ? core::AutoFillMode::cheapest
                                                             : core::AutoFillMode::best_stat;
    for (const std::string& rid : crafting_.auto_fill(*s, vault, fill_mode)) {
        out.push_back(godot::String::utf8(rid.c_str()));
    }
    return out;
}

godot::Dictionary SwmCharacterStore::preview_craft(const godot::String& schematic_id,
                                                   const godot::PackedStringArray& resource_ids,
                                                   int64_t enhancement_count, int64_t crafter_level,
                                                   int64_t exp_quality_pct) const {
    godot::Dictionary out;
    const core::SchematicDef* s = crafting_.find_schematic(to_std(schematic_id));
    if (s == nullptr) return out;
    std::vector<std::string> rids;
    rids.reserve(size_t(resource_ids.size()));
    for (int i = 0; i < resource_ids.size(); ++i) rids.push_back(to_std(resource_ids[i]));
    const core::CraftPreview p = crafting_.preview_craft(*s, rids, int(enhancement_count), int(crafter_level),
                                                         uint8_t(std::clamp<int64_t>(exp_quality_pct, 0, 100)));
    out["base_quality"] = p.base_quality;
    out["min_quality"] = p.min_quality;
    out["max_quality"] = p.max_quality;
    out["great_success"] = p.odds.great_success;
    out["success"] = p.odds.success;
    out["neutral"] = p.odds.neutral;
    out["minor_flaw"] = p.odds.minor_flaw;
    return out;
}

godot::Error SwmCharacterStore::save_loadout(int64_t char_id, int64_t slot_index, const godot::Dictionary& loadout) {
    last_error_.clear();
    if (!store_) {
        last_error_ = "database not open";
        return godot::ERR_UNCONFIGURED;
    }
    try {
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
    store_->save_loadout(core::PersistentId(char_id), lo);
        return godot::OK;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return godot::FAILED;
    }
}

godot::Error SwmCharacterStore::ensure_starter_kit(int64_t char_id) {
    last_error_.clear();
    if (!store_) {
        last_error_ = "database not open";
        return godot::ERR_UNCONFIGURED;
    }
    try {
        const auto ch = store_->find(core::PersistentId(char_id));
        if (!ch) {
            last_error_ = "character not found";
            return godot::ERR_DOES_NOT_EXIST;
        }
        core::ensure_starter_kit(*store_, ch->id);
        return godot::OK;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return godot::FAILED;
    }
}

godot::Dictionary SwmCharacterStore::resolve_item_stats(const godot::String& def_id, double quality,
                                                        bool apply_quality) const {
    godot::Dictionary out;
    const core::ItemDef* def = catalog_.find(to_std(def_id));
    if (def == nullptr) return out;
    if (def->kind == core::ItemKind::weapon) {
        const core::WeaponDef w = catalog_.resolve_weapon(*def, quality, apply_quality);
        out["damage"] = w.damage;
        out["shot_delay"] = w.shot_delay;
        out["rounds_per_clip"] = int64_t(w.rounds_per_clip);
        out["heat_per_shot"] = w.heat_per_shot;
        out["reload_time"] = w.reload_time;
        out["bolt_speed"] = w.bolt_speed;
        out["bolt_life"] = w.bolt_life;
        out["yaw_spread"] = w.yaw_spread;
        out["shots_per_salvo"] = int64_t(w.shots_per_salvo);
        out["zoom_max"] = w.zoom_max;
        out["explosion_damage"] = w.explosion_damage;
        out["explosion_outer"] = w.explosion_outer;
        out["projectile"] = w.projectile == core::Projectile::bolt      ? "bolt"
                            : w.projectile == core::Projectile::missile ? "missile"
                            : w.projectile == core::Projectile::grenade ? "grenade"
                                                                        : "melee";
        out["melee_range"] = w.melee_range;
        out["melee_arc"] = w.melee_arc;
        out["melee_lunge"] = w.melee_lunge;
        out["melee_stagger"] = w.melee_stagger;
        out["shield_disrupt"] = w.shield_disrupt;
        const std::string_view dt_name = core::damage_type_name(w.damage_type);
        out["damage_type"] = godot::String::utf8(dt_name.data(), int(dt_name.size()));
    }
    const core::Gear g = catalog_.resolve_gear(*def, quality, apply_quality);
    godot::Array mods;
    for (const core::StatModifier& m : g.modifiers) {
        godot::Dictionary md;
        const std::string_view sname = core::stat_name(m.stat);
        md["stat_name"] = godot::String::utf8(sname.data(), int(sname.size()));
        md["add"] = m.add;
        md["multiply"] = m.multiply;
        mods.push_back(md);
    }
    out["modifiers"] = mods;
    return out;
}

godot::Dictionary SwmCharacterStore::compute_loadout_stats(const godot::String& species, const godot::Dictionary& loadout,
                                                           const godot::Array& items, bool apply_quality) const {
    core::CharacterLoadout cl;
    cl.primary_id = loadout.has("primary_id") ? uint64_t(int64_t(loadout["primary_id"])) : 0;
    cl.sidearm_id = loadout.has("sidearm_id") ? uint64_t(int64_t(loadout["sidearm_id"])) : 0;
    cl.secondary_id = loadout.has("secondary_id") ? uint64_t(int64_t(loadout["secondary_id"])) : 0;
    if (loadout.has("gear_ids")) {
        godot::Dictionary gear = loadout["gear_ids"];
        godot::Array keys = gear.keys();
        for (int i = 0; i < keys.size(); ++i) {
            cl.gear_ids[to_std(keys[i])] = uint64_t(int64_t(gear[keys[i]]));
        }
    }
    std::vector<core::ItemInstance> inv;
    inv.reserve(size_t(items.size()));
    for (int i = 0; i < items.size(); ++i) {
        godot::Dictionary d = items[i];
        core::ItemInstance inst;
        inst.id = d.has("id") ? uint64_t(int64_t(d["id"])) : 0;
        inst.def_id = d.has("def_id") ? to_std(d["def_id"]) : "";
        inst.quality = d.has("quality") ? double(d["quality"]) : 0.5;
        inv.push_back(std::move(inst));
    }
    const core::Species sp = core::species_from_name(to_std(species)).value_or(core::Species::human);
    const core::Loadout built = catalog_.build_loadout(cl, inv, sp, apply_quality);
    const core::CharacterStats stats = core::stats_for(built, sp);
    godot::Dictionary out;
    out["max_health"] = stats.max_health;
    out["run_speed"] = stats.move.run_speed;
    out["sprint_factor"] = stats.move.sprint_factor;
    out["max_energy"] = stats.move.max_energy;
    out["energy_restore"] = stats.move.energy_restore;
    out["mitigation"] = stats.mitigation;
    out["mitigation_ballistic"] = stats.mitigation_by_type[size_t(core::DamageType::ballistic)];
    out["mitigation_laser"] = stats.mitigation_by_type[size_t(core::DamageType::laser)];
    out["mitigation_electro"] = stats.mitigation_by_type[size_t(core::DamageType::electro)];
    out["mitigation_bio"] = stats.mitigation_by_type[size_t(core::DamageType::bio)];
    out["mitigation_explosive"] = stats.mitigation_by_type[size_t(core::DamageType::explosive)];
    out["mitigation_melee"] = stats.mitigation_by_type[size_t(core::DamageType::melee)];
    return out;
}

godot::Dictionary SwmCharacterStore::buy_item(int64_t char_id, const godot::String& def_id,
                                              bool use_faction_points) {
    last_error_.clear();
    if (!store_) {
        last_error_ = "database not open";
        return {};
    }
    try {
        const std::string id_str = to_std(def_id);
        const core::ItemDef* def = catalog_.find(id_str);
        if (def == nullptr) {
            last_error_ = "unknown item: " + id_str;
            return {};
        }
        const auto ch = store_->find(core::PersistentId(char_id));
        if (!ch) {
            last_error_ = "character not found";
            return {};
        }
        int64_t credits = ch->credits;
        int64_t fp = ch->faction_points;
        if (use_faction_points) {
            if (fp < def->cost_faction_points) {
                last_error_ = "insufficient faction points";
                return {};
            }
            fp -= def->cost_faction_points;
        } else {
            if (credits < def->cost_credits) {
                last_error_ = "insufficient credits";
                return {};
            }
            credits -= def->cost_credits;
        }
        store_->update_economy(ch->id, credits, fp, ch->experience, ch->active_loadout);
        return to_dict(store_->grant_item({0, ch->id, id_str, 0.5, "", "{}", 1}));
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return {};
    }
}

godot::Dictionary SwmCharacterStore::add_rewards(int64_t char_id, int64_t xp, int64_t faction_points,
                                                 int64_t credits) {
    last_error_.clear();
    if (!store_) {
        last_error_ = "database not open";
        return {};
    }
    try {
        const auto ch = store_->find(core::PersistentId(char_id));
        if (!ch) {
            last_error_ = "character not found";
            return {};
        }
        store_->update_economy(ch->id, ch->credits + credits, ch->faction_points + faction_points,
                               ch->experience + xp, ch->active_loadout);
        return to_dict(*store_->find(ch->id));
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return {};
    }
}

godot::Error SwmCharacterStore::set_active_loadout(int64_t char_id, int64_t slot_index) {
    last_error_.clear();
    if (!store_) {
        last_error_ = "database not open";
        return godot::ERR_UNCONFIGURED;
    }
    try {
        const auto ch = store_->find(core::PersistentId(char_id));
        if (!ch) {
            last_error_ = "character not found";
            return godot::ERR_DOES_NOT_EXIST;
        }
        store_->update_economy(ch->id, ch->credits, ch->faction_points, ch->experience, int(slot_index));
        return godot::OK;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return godot::FAILED;
    }
}

godot::Array SwmCharacterStore::get_species_list() {
    godot::Array out;
    for (const core::SpeciesDef& sp : core::all_species()) {
        godot::Dictionary d;
        d["id"] = godot::String::utf8(sp.id.c_str());
        d["code"] = godot::String::utf8(sp.code.c_str());
        d["name"] = godot::String::utf8(sp.display_name.c_str());
        d["male_height"] = sp.male_body.height;
        d["female_height"] = sp.female_body.height;
        d["male_eye_height"] = sp.male_body.eye_height;
        d["female_eye_height"] = sp.female_body.eye_height;
        d["scale_min"] = sp.male_body.scale_min;
        d["scale_max"] = sp.male_body.scale_max;
        godot::PackedStringArray blocked;
        for (const std::string& slot : sp.blocked_slots) {
            blocked.push_back(godot::String::utf8(slot.c_str()));
        }
        d["blocked_slots"] = blocked;
        d["traits"] = godot::String::utf8(sp.traits_summary.c_str());
        out.push_back(d);
    }
    return out;
}

void SwmCharacterStore::_bind_methods() {
    using godot::ClassDB;
    using godot::D_METHOD;
    ClassDB::bind_method(D_METHOD("open", "path"), &SwmCharacterStore::open);
    ClassDB::bind_method(D_METHOD("close"), &SwmCharacterStore::close);
    ClassDB::bind_method(D_METHOD("list_characters"), &SwmCharacterStore::list_characters);
    ClassDB::bind_method(
        D_METHOD("create_character", "name", "species", "gender", "appearance_json", "location", "position", "yaw"),
        &SwmCharacterStore::create_character);
    ClassDB::bind_method(D_METHOD("delete_character", "id"), &SwmCharacterStore::delete_character);
    ClassDB::bind_method(D_METHOD("get_last_error"), &SwmCharacterStore::get_last_error);
    ClassDB::bind_method(D_METHOD("load_catalog", "catalog_ini", "bf2_items_dir"), &SwmCharacterStore::load_catalog);
    ClassDB::bind_method(D_METHOD("get_catalog"), &SwmCharacterStore::get_catalog);
    ClassDB::bind_method(D_METHOD("list_items", "char_id"), &SwmCharacterStore::list_items);
    ClassDB::bind_method(D_METHOD("get_reputation", "char_id"), &SwmCharacterStore::get_reputation);
    ClassDB::bind_method(D_METHOD("grant_item", "char_id", "def_id", "quality"), &SwmCharacterStore::grant_item);
    ClassDB::bind_method(D_METHOD("remove_item", "item_id"), &SwmCharacterStore::remove_item);
    ClassDB::bind_method(D_METHOD("list_loadouts", "char_id"), &SwmCharacterStore::list_loadouts);
    ClassDB::bind_method(D_METHOD("save_loadout", "char_id", "slot_index", "loadout"),
                         &SwmCharacterStore::save_loadout);
    ClassDB::bind_method(D_METHOD("ensure_starter_kit", "char_id"), &SwmCharacterStore::ensure_starter_kit);
    ClassDB::bind_method(D_METHOD("buy_item", "char_id", "def_id", "use_faction_points"), &SwmCharacterStore::buy_item);
    ClassDB::bind_method(D_METHOD("add_rewards", "char_id", "xp", "faction_points", "credits"),
                         &SwmCharacterStore::add_rewards);
    ClassDB::bind_method(D_METHOD("set_active_loadout", "char_id", "slot_index"),
                         &SwmCharacterStore::set_active_loadout);
    ClassDB::bind_method(D_METHOD("resolve_item_stats", "def_id", "quality", "apply_quality"),
                         &SwmCharacterStore::resolve_item_stats, DEFVAL(0.5), DEFVAL(true));
    ClassDB::bind_method(D_METHOD("compute_loadout_stats", "species", "loadout", "items", "apply_quality"),
                         &SwmCharacterStore::compute_loadout_stats, DEFVAL(true));
    ClassDB::bind_method(D_METHOD("load_professions", "path"), &SwmCharacterStore::load_professions);
    ClassDB::bind_method(D_METHOD("get_professions"), &SwmCharacterStore::get_professions);
    ClassDB::bind_method(D_METHOD("profession_level", "xp"), &SwmCharacterStore::profession_level);
    ClassDB::bind_method(D_METHOD("get_specialization_slots"), &SwmCharacterStore::get_specialization_slots);
    ClassDB::bind_method(D_METHOD("load_crafting", "resources_path", "schematics_path"),
                         &SwmCharacterStore::load_crafting);
    ClassDB::bind_method(D_METHOD("get_resources"), &SwmCharacterStore::get_resources);
    ClassDB::bind_method(D_METHOD("get_schematics"), &SwmCharacterStore::get_schematics);
    ClassDB::bind_method(D_METHOD("get_work_orders"), &SwmCharacterStore::get_work_orders);
    ClassDB::bind_method(D_METHOD("scan_terrain", "category", "x", "z", "radius", "grid_n"),
                         &SwmCharacterStore::scan_terrain, DEFVAL(60.0), DEFVAL(17));
    ClassDB::bind_method(D_METHOD("auto_fill_schematic", "schematic_id", "materials", "mode"),
                         &SwmCharacterStore::auto_fill_schematic);
    ClassDB::bind_method(D_METHOD("preview_craft", "schematic_id", "resource_ids", "enhancement_count", "crafter_level", "exp_quality_pct"),
                         &SwmCharacterStore::preview_craft);
    ClassDB::bind_static_method("SwmCharacterStore", D_METHOD("get_species_list"),
                                &SwmCharacterStore::get_species_list);
}

} // namespace swm::godot_bindings
