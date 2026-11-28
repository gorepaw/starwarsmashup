// Gathering, Material Vault, Unified Workbench crafting, Recipe Presets,
// Hub Work Orders, and Salvaging on the server (docs/PROFESSIONS.md).
#include <algorithm>
#include <cmath>
#include <format>

#include "swm/core/server.h"

namespace swm::core {

bool item_equipped_in_loadouts(const std::vector<CharacterLoadout>& loadouts, uint64_t item_id) {
    for (const CharacterLoadout& cl : loadouts) {
        if (cl.primary_id == item_id || cl.sidearm_id == item_id || cl.secondary_id == item_id) {
            return true;
        }
        for (const auto& [slot, id] : cl.gear_ids) {
            if (id == item_id) return true;
        }
        for (const auto& [slot, id] : cl.appearance_ids) {
            if (id == item_id) return true;
        }
    }
    return false;
}

namespace {

int profession_level(const ProfessionBook* book, const std::vector<ProfessionProgress>& progress, std::string_view id) {
    if (book == nullptr) return 1;
    return book->level_for(profession_xp(progress, id));
}

bool is_bandit_name(std::string_view name) {
    for (std::string_view kw : {"Bandit", "Raider", "Valarian", "Tusken", "Thug", "Enforcer", "Slicer", "Outlaw",
                                 "Pirate"}) {
        if (name.find(kw) != std::string_view::npos) return true;
    }
    return false;
}

} // namespace

void GameServer::set_crafting(const CraftingBook* book, std::vector<net::Workbench> workbenches) {
    config_.crafting = book;
    workbenches_ = std::move(workbenches);
}

const std::vector<MaterialStack>* GameServer::materials_of(uint32_t entity) const {
    const Client* client = find(entity);
    return client ? &client->materials : nullptr;
}

const std::vector<RecipePreset>* GameServer::presets_of(uint32_t entity) const {
    const Client* client = find(entity);
    return client ? &client->presets : nullptr;
}

void GameServer::save_materials(Client& client) {
    if (store_ == nullptr || client.character == 0) return;
    try {
        store_->save_materials(client.character, client.materials);
    } catch (const std::exception& error) {
        say(std::format("material save error for {}: {}", client.name, error.what()));
    }
}

void GameServer::send_crafting_sync(Client& client) {
    net::CraftingSync sync;
    sync.revision = client.crafting_rev;
    sync.materials = client.materials;
    sync.presets = client.presets;
    sync.blueprints = client.blueprints;
    link_.send(client.endpoint, net::encode(sync));
    client.crafting_sync_tick = tick_;
}

bool GameServer::learn_blueprint(Client& client, const ItemDef& def, uint64_t item_id) {
    if (def.teaches.empty() || store_ == nullptr || client.character == 0) return false;
    // Its schematics (a whole armor set's, from a city blueprint): those
    // still hidden from this crafter.
    std::vector<const SchematicDef*> taught, fresh;
    for (const std::string& id : blueprint_teaches(def)) {
        const SchematicDef* sch = config_.crafting ? config_.crafting->find_schematic(id) : nullptr;
        if (sch == nullptr) continue;
        taught.push_back(sch);
        if (sch->blueprint &&
            std::find(client.blueprints.begin(), client.blueprints.end(), sch->id) == client.blueprints.end()) {
            fresh.push_back(sch);
        }
    }
    if (taught.empty()) {
        notify(client.entity, def.display_name + " teaches nothing anyone can make.");
        return true;
    }
    if (fresh.empty()) {
        notify(client.entity, taught.size() == 1 ? std::format("You already know how to make {}.", taught[0]->name)
                                                 : std::format("You already know everything {} teaches.",
                                                               def.display_name));
        return true;
    }
    if (!store_->remove_item(item_id)) return true;
    for (const SchematicDef* sch : fresh) client.blueprints.push_back(sch->id);
    std::string list;
    for (const std::string& id : client.blueprints) list += (list.empty() ? "" : ",") + id;
    store_->set_extra(client.character, "blueprints", list);
    client.items = store_->list_items(client.character);
    ++client.inventory_rev;
    client.last_sync_tick = 0;
    ++client.crafting_rev;
    notify(client.entity, fresh.size() == 1
                              ? std::format("Learned a blueprint: {} ({})", fresh[0]->name, fresh[0]->group)
                              : std::format("Learned a blueprint: {} ({} schematics, {})",
                                            def.display_name.substr(def.display_name.starts_with("Blueprint: ") ? 11 : 0),
                                            fresh.size(), fresh[0]->group));
    return true;
}

const GameServer::TownArea* GameServer::town_at(Vec3 at) const {
    const TownArea* best = nullptr;
    double best_d = 0;
    for (const TownArea& t : towns_) {
        const double d = std::hypot(at.x - t.center.x, at.z - t.center.z) - t.radius;
        if (best == nullptr || d < best_d) {
            best = &t;
            best_d = d;
        }
    }
    return best;
}

void GameServer::earn_town_reputation(Client& client, const TownArea& town, int64_t amount) {
    if (amount == 0 || client.character == 0) return;
    const std::string key = town_reputation_key(town.id);
    const RepTier before = reputation_tier(client.reputation[key]);
    client.reputation[key] = clamp_reputation(client.reputation[key] + amount);
    const RepTier after = reputation_tier(client.reputation[key]);
    if (store_ != nullptr) {
        try {
            store_->save_reputation(client.character, client.reputation);
        } catch (const std::exception& error) {
            say(std::format("reputation save error for {}: {}", client.name, error.what()));
        }
    }
    ++client.inventory_rev;  // (reputation rides with the inventory)
    client.last_sync_tick = 0;
    notify(client.entity, after != before
                              ? std::format("+{} {} reputation: you are now {}", amount, town.name,
                                            reputation_tier_name(after))
                              : std::format("+{} {} reputation", amount, town.name));
}

void GameServer::buy_city_blueprint(Client& client, const net::Quartermaster& qm, const ItemDef& def) {
    if (store_ == nullptr || client.character == 0) return;
    if (def.town != qm.town) {
        notify(client.entity, qm.name + " doesn't sell that");
        return;
    }
    const auto it = client.reputation.find(town_reputation_key(qm.town));
    const int64_t standing = it == client.reputation.end() ? 0 : it->second;
    if (reputation_tier(standing) < def.required_rank) {
        notify(client.entity, std::format("Requires {} standing with {}", reputation_tier_name(def.required_rank),
                                          qm.town_name));
        return;
    }
    if (client.credits < def.cost_credits) {
        notify(client.entity, "Not enough credits");
        return;
    }
    client.credits -= def.cost_credits;
    save_economy(client);
    store_->grant_item({0, client.character, def.id, ItemGrade::standard, "", "{}", 1});
    client.items = store_->list_items(client.character);
    ++client.inventory_rev;
    client.last_sync_tick = 0;
    notify(client.entity, std::format("Purchased {} (-{} Cr)", def.display_name, def.cost_credits));
}

// A haul with a gathering profession's perks: more from each (Bonus =
// yield), and a chance of it twice (double_haul).
int64_t GameServer::gathered_amount(Client& client, const std::string& profession, int64_t base) {
    if (config_.professions == nullptr) return base;
    const double more = perk_bonus(*config_.professions, client.professions, profession, "yield");
    int64_t qty = std::max<int64_t>(1, int64_t(std::llround(double(base) * (1.0 + std::max(0.0, more)))));
    if (random_unit() < perk_bonus(*config_.professions, client.professions, profession, "double_haul")) qty *= 2;
    return qty;
}

// What an order pays a crafter, with their profession's order_pay perks.
int64_t GameServer::order_pay(const Client& client, const std::string& profession, int64_t base) const {
    if (config_.professions == nullptr) return base;
    const double more = perk_bonus(*config_.professions, client.professions, profession, "order_pay");
    return int64_t(std::llround(double(base) * (1.0 + std::max(0.0, more))));
}

void GameServer::handle_gather_request(Client& client, const net::GatherRequest& req, double now) {
    if (config_.crafting == nullptr || client.life != Life::alive) return;
    const CraftingBook& book = *config_.crafting;

    if (req.action == net::GatherRequest::extract_terrain) {
        if (client.extract_started >= 0.0) return;  // already at it
        if (world_.near_structure(client.state.position, 40.0)) {
            notify(client.entity, "Cannot extract resources inside a city or near buildings.");
            return;
        }
        const ResourceCategory cat = ResourceCategory(req.category);
        if (!require_licence(client, cat == ResourceCategory::flora ? "foraging" : "surveying")) return;
        if (client.has_extracted) {
            const Vec3 d = client.state.position - client.last_extract_site;
            const double apart = std::hypot(d.x, d.z);
            if (apart < config_.extract_spacing) {
                notify(client.entity, std::format("This ground is worked out: move {} m from where you last extracted.",
                                                  int(std::ceil(config_.extract_spacing - apart))));
                return;
            }
        }
        const auto [res, conc] = book.best_resource_at(cat, client.state.position.x, client.state.position.z);
        if (res == nullptr) {
            notify(client.entity, "No matching resource signature nearby.");
            return;
        }
        client.extract_started = now;
        client.extract_from = client.state.position;
        client.extract_category = cat;
        notify(client.entity, "Extracting: stay still.");
        return;
    }

    if (req.action == net::GatherRequest::harvest_corpse) {
        const uint32_t corpse_id = uint32_t(req.target_id);
        const auto it = std::find_if(corpse_loots_.begin(), corpse_loots_.end(),
                                     [&](const CorpseLoot& c) { return c.npc_entity == corpse_id; });
        if (it == corpse_loots_.end()) return;
        if (it->harvested) {
            notify(client.entity, "Already harvested.");
            return;
        }
        const Vec3 d = client.state.position - it->position;
        if (d.x * d.x + d.z * d.z > 64.0) {
            notify(client.entity, "Move closer to harvest.");
            return;
        }
        if (!require_licence(client, is_bandit_name(it->name) ? "salvaging" : "foraging")) return;
        it->harvested = true;

        const bool salvage = is_bandit_name(it->name);
        const ResourceCategory cat = salvage ? ResourceCategory::salvage : ResourceCategory::organic;
        const std::string prof = salvage ? "salvaging" : "foraging";
        const int lv = profession_level(config_.professions, client.professions, prof);
        std::vector<const ResourceDef*> choices;
        for (const ResourceDef& r : book.resources()) {
            if (r.category == cat) choices.push_back(&r);
        }
        const std::string corpse_name = it->name;
        if (it->items.empty()) {
            corpse_loots_.erase(it);
        }
        if (choices.empty()) return;
        const ResourceDef* picked = choices[size_t(corpse_id) % choices.size()];
        const int64_t qty = gathered_amount(client, prof, std::max<int64_t>(3, 4 + lv / 12));
        add_material(client.materials, picked->id, qty);
        save_materials(client);
        ++client.crafting_rev;
        client.crafting_sync_tick = 0;
        earn_profession_xp(client, prof, 28);
        notify(client.entity, std::format("{} {}x {} from {}", salvage ? "Salvaged" : "Harvested", qty, picked->name, corpse_name));
        return;
    }

    if (req.action == net::GatherRequest::salvage_item) {
        if (config_.catalog == nullptr || store_ == nullptr || client.character == 0) return;
        if (!require_licence(client, "salvaging")) return;
        const uint64_t item_id = req.target_id;
        if (item_equipped_in_loadouts(client.char_loadouts, item_id)) {
            notify(client.entity, "Unequip that item before salvaging it.");
            return;
        }
        const auto it = std::find_if(client.items.begin(), client.items.end(),
                                     [&](const ItemInstance& inst) { return inst.id == item_id; });
        if (it == client.items.end()) return;
        const ItemDef* def = config_.catalog->find(it->def_id);
        if (def == nullptr || def->kind == ItemKind::quest) return;

        const int lv = profession_level(config_.professions, client.professions, "salvaging");
        const double bonus =
            config_.professions ? perk_bonus(*config_.professions, client.professions, "salvaging", "yield") : 0.0;
        auto yield = book.salvage_yield(*def, lv, bonus);
        if (!store_->remove_item(item_id)) return;
        client.items.erase(it);
        const bool twice = config_.professions != nullptr &&
                           random_unit() < perk_bonus(*config_.professions, client.professions, "salvaging", "double_haul");
        for (MaterialStack& st : yield.materials) {
            if (twice) st.amount *= 2;
            add_material(client.materials, st.resource_id, st.amount);
        }
        save_materials(client);
        ++client.inventory_rev;
        client.last_sync_tick = 0;
        ++client.crafting_rev;
        client.crafting_sync_tick = 0;
        earn_profession_xp(client, "salvaging", yield.xp);
        std::string summary;
        for (const MaterialStack& st : yield.materials) {
            const ResourceDef* r = book.find_resource(st.resource_id);
            if (!summary.empty()) summary += ", ";
            summary += std::format("{}x {}", st.amount, r ? r->name : st.resource_id);
        }
        notify(client.entity, std::format("Salvaged {} -> {}", def->display_name, summary));
    }
}

// What you got from the ground where you stood still: the best of its kind there.
void GameServer::finish_extraction(Client& client, ResourceCategory cat) {
    const CraftingBook& book = *config_.crafting;
    client.extract_started = -1.0;
    const auto [res, conc] = book.best_resource_at(cat, client.state.position.x, client.state.position.z);
    if (res == nullptr) {
        notify(client.entity, "No matching resource signature nearby.");
        return;
    }
    client.has_extracted = true;
    client.last_extract_site = client.state.position;
    const std::string prof = cat == ResourceCategory::flora ? "foraging" : "surveying";
    const int lv = profession_level(config_.professions, client.professions, prof);
    const double yield_bonus = 1.0 + double(lv) * 0.01;
    const int64_t qty = gathered_amount(client, prof, std::max<int64_t>(2, int64_t(std::llround((4.0 + 14.0 * conc) * yield_bonus))));
    add_material(client.materials, res->id, qty);
    save_materials(client);
    ++client.crafting_rev;
    client.crafting_sync_tick = 0;
    const int64_t xp = 15 + int64_t(std::llround(conc * 25.0));
    earn_profession_xp(client, prof, xp);
    notify(client.entity, std::format("Extracted {}x {} ({}% concentration)", qty, res->name, int(conc * 100.0)));
    quest_event(client, "extract");
}

// Extractions under way: done after `extract_time` standing where they began;
// broken off by moving (or dying).
void GameServer::update_extractions(double now) {
    if (config_.crafting == nullptr) return;
    for (Client& client : clients_) {
        if (client.extract_started < 0.0) continue;
        const Vec3 d = client.state.position - client.extract_from;
        if (client.life != Life::alive || std::hypot(d.x, d.z) > 1.0 || client.state.vehicle >= 0) {
            client.extract_started = -1.0;
            notify(client.entity, "Extraction broken off: you moved.");
        } else if (now - client.extract_started >= config_.extract_time) {
            finish_extraction(client, client.extract_category);
        }
    }
}

void GameServer::handle_craft_request(Client& client, const net::CraftRequest& req) {
    if (config_.crafting == nullptr) return;
    const CraftingBook& book = *config_.crafting;

    if (req.action == net::CraftRequest::save_preset) {
        if (store_ == nullptr || client.character == 0 || req.preset_name.empty() || req.schematic_id.empty()) return;
        RecipePreset preset;
        preset.name = req.preset_name;
        preset.schematic_id = req.schematic_id;
        preset.resource_ids = req.resource_ids;
        preset.enhancement_defs = req.enhancement_defs;
        preset.exp_quality_pct = uint8_t(std::clamp<int>(req.exp_quality_pct, 0, 100));
        store_->save_preset(client.character, preset);
        client.presets = store_->list_presets(client.character);
        ++client.crafting_rev;
        client.crafting_sync_tick = 0;
        notify(client.entity, std::format("Saved preset '{}'", preset.name));
        return;
    }

    if (req.action == net::CraftRequest::delete_preset) {
        if (store_ == nullptr || client.character == 0 || req.preset_name.empty()) return;
        if (store_->delete_preset(client.character, req.preset_name)) {
            client.presets = store_->list_presets(client.character);
            ++client.crafting_rev;
            client.crafting_sync_tick = 0;
        }
        return;
    }

    if (req.action == net::CraftRequest::fulfill_order) {
        if (store_ == nullptr || client.character == 0 || config_.catalog == nullptr) return;
        const WorkOrder* order = book.find_work_order(req.order_id);
        if (order == nullptr || !require_licence(client, order->profession)) return;
        std::vector<uint64_t> matching_ids;
        for (const ItemInstance& inst : client.items) {
            if (inst.def_id == order->output_item && !item_equipped_in_loadouts(client.char_loadouts, inst.id)) {
                matching_ids.push_back(inst.id);
                if (int(matching_ids.size()) >= order->count) break;
            }
        }
        if (int(matching_ids.size()) < order->count) {
            notify(client.entity, std::format("Need {} unequipped {} to fulfill this work order.", order->count,
                                              item_name(order->output_item)));
            return;
        }
        for (const uint64_t id : matching_ids) {
            store_->remove_item(id);
            std::erase_if(client.items, [&](const ItemInstance& inst) { return inst.id == id; });
        }
        const int64_t pay = order_pay(client, order->profession, order->reward_credits);
        client.credits += pay;
        client.faction_points += int64_t(std::llround(double(order->reward_fp) * client.stats.faction_gain));
        save_economy(client);
        ++client.inventory_rev;
        client.last_sync_tick = 0;
        earn_profession_xp(client, order->profession, order->reward_xp);
        notify(client.entity, std::format("Work order complete: +{} Cr, +{} FP, +{} {} XP", pay,
                                          order->reward_fp, order->reward_xp, order->profession));
        return;
    }

    // At a station for a profession's work (where the world has stations at all).
    const auto at_station = [&](std::string_view profession, std::string_view what) {
        if (workbenches_.empty()) return true;
        bool near_one = false;
        bool fits = false;
        for (const net::Workbench& wb : workbenches_) {
            const Vec3 d = client.state.position - wb.position;
            if (std::hypot(d.x, d.z) > wb.radius + 1.0) continue;
            near_one = true;
            fits = fits || station_makes(wb.kind, profession);
        }
        if (!fits) {
            notify(client.entity, near_one ? std::format("{} is done at a {}.", what, station_name(station_for(profession)))
                                           : std::string("Stand at a crafting station to craft."));
        }
        return fits;
    };

    if (req.action == net::CraftRequest::dye) {
        // A Tailor dyes clothing (and a bodysuit), an Armorsmith armor
        // (docs/CRAFTING.md, Dyes): colors of its model's own palettes, for
        // two units of a chemical from the vault.
        if (store_ == nullptr || client.character == 0 || config_.catalog == nullptr) return;
        const auto it = std::find_if(client.items.begin(), client.items.end(),
                                     [&](const ItemInstance& inst) { return inst.id == req.item_id; });
        const ItemDef* def = it == client.items.end() ? nullptr : config_.catalog->find(it->def_id);
        if (def == nullptr) return;
        if (def->kind != ItemKind::clothing && def->kind != ItemKind::armor && def->kind != ItemKind::bodysuit) {
            notify(client.entity, "Only clothing and armor take a dye.");
            return;
        }
        if (!dye_values_ok(req.dye_values)) return;
        const std::string profession = def->kind == ItemKind::armor ? "armorsmith" : "tailor";
        if (!at_station(profession, "Dyeing " + def->display_name)) return;
        const ResourceDef* dye_stuff = nullptr;
        if (!req.dye_values.empty()) {
            for (const MaterialStack& st : client.materials) {
                const ResourceDef* r = book.find_resource(st.resource_id);
                if (r && r->category == ResourceCategory::chemical && st.amount >= dye_cost &&
                    (dye_stuff == nullptr || st.amount > material_amount(client.materials, dye_stuff->id))) {
                    dye_stuff = r;
                }
            }
            if (dye_stuff == nullptr) {
                notify(client.entity, std::format("A dye takes {} Chemical from your Material Vault.", dye_cost));
                return;
            }
        }
        const std::string colors = with_item_json_field(it->color_json, "dye", req.dye_values);
        if (!store_->update_item_colors(it->id, colors)) return;
        it->color_json = colors;
        if (dye_stuff != nullptr) {
            add_material(client.materials, dye_stuff->id, -dye_cost);
            save_materials(client);
            ++client.crafting_rev;
            client.crafting_sync_tick = 0;
            earn_profession_xp(client, profession, 20);
        }
        ++client.inventory_rev;
        client.last_sync_tick = 0;
        notify(client.entity, req.dye_values.empty() ? std::format("Took the dye out of {}.", def->display_name)
                                              : std::format("Dyed {}.", def->display_name));
        return;
    }

    if (req.action == net::CraftRequest::craft) {
        if (store_ == nullptr || client.character == 0 || config_.catalog == nullptr) return;
        const SchematicDef* sch = book.find_schematic(req.schematic_id);
        if (sch == nullptr) return;
        if (!at_station(sch->profession, sch->name)) return;
        if (!require_licence(client, sch->profession)) return;
        const int lv = profession_level(config_.professions, client.professions, sch->profession);
        if (lv < sch->required_level) {
            notify(client.entity, std::format("Requires {} level {}.", sch->profession, sch->required_level));
            return;
        }
        if (sch->blueprint &&
            std::find(client.blueprints.begin(), client.blueprints.end(), sch->id) == client.blueprints.end()) {
            notify(client.entity, std::format("Learn the blueprint for {} first.", sch->name));
            return;
        }
        const ItemDef* out_def = config_.catalog->find(sch->output_item);
        // Its mark: up to its crafter's level's, costing more the higher.
        int mark = 0;
        if (out_def != nullptr && item_takes_marks(*out_def)) {
            const int top = craft_max_mark(lv);
            mark = req.mark == net::CraftRequest::top_mark ? top : int(req.mark);
            if (mark > top) {
                notify(client.entity, std::format("{} takes {} level {}.", mark_name(mark), sch->profession,
                                                  mark_level(mark)));
                return;
            }
        }
        const int batch = std::clamp<int>(req.batch_count, 1, 10);
        // Focus buys a better grade, so it costs nothing on what has none.
        const bool graded = out_def != nullptr && item_takes_grade(*out_def) && !out_def->grade;
        const int focus = graded ? std::clamp<int>(req.exp_quality_pct, 0, 100) : 0;
        if (req.resource_ids.size() < sch->slots.size()) {
            notify(client.entity, "Fill every material slot first.");
            return;
        }
        // A crafting perk's fewer materials (each in turn: 20% and 20% is 36%).
        double material_mult = 1.0;
        if (config_.professions != nullptr) {
            if (const ProfessionProgress* p = find_progress(client.professions, sch->profession)) {
                for (const std::string& id : p->perks) {
                    if (const ProfessionPerkDef* perk = config_.professions->find_perk(id)) {
                        for (const auto& [k, v] : perk->bonuses) {
                            if (k == "materials") material_mult *= std::clamp(1.0 - v, 0.0, 1.0);
                        }
                    }
                }
            }
        }
        std::map<std::string, int64_t> needed;
        for (size_t i = 0; i < sch->slots.size(); ++i) {
            const ResourceDef* r = book.find_resource(req.resource_ids[i]);
            if (r == nullptr || r->category != sch->slots[i].category) {
                notify(client.entity, "Invalid material for schematic slot.");
                return;
            }
            needed[r->id] += int64_t(std::max(1.0, std::ceil(double(craft_material_amount(sch->slots[i].amount, mark, focus)) *
                                                             material_mult - 1e-6))) *
                             batch;
        }
        for (const auto& [rid, amt] : needed) {
            if (material_amount(client.materials, rid) < amt) {
                notify(client.entity, "Not enough materials in Material Vault.");
                return;
            }
        }
        // Its modules: each tunes it (docs/CRAFTING.md, Modules) if it fits,
        // and betters its grade odds; one for something else isn't used.
        std::vector<uint64_t> enh_ids;
        std::string mods;
        for (const uint64_t eid : req.enhancement_item_ids) {
            if (int(enh_ids.size()) >= sch->enhancement_slots) break;
            if (std::find(enh_ids.begin(), enh_ids.end(), eid) != enh_ids.end()) continue;
            if (item_equipped_in_loadouts(client.char_loadouts, eid)) continue;
            const auto it = std::find_if(client.items.begin(), client.items.end(),
                                         [&](const ItemInstance& inst) { return inst.id == eid; });
            if (it == client.items.end()) continue;
            const ItemDef* edef = config_.catalog->find(it->def_id);
            if (edef == nullptr || edef->kind != ItemKind::enhancement) continue;
            if (!edef->module_for.empty()) {
                if (out_def == nullptr || !module_fits(*edef, *out_def)) {
                    notify(client.entity, std::format("{} doesn't fit {}.", edef->display_name, sch->name));
                    return;
                }
                mods += (mods.empty() ? "" : ",") + edef->id;
            }
            enh_ids.push_back(eid);
        }
        for (const auto& [rid, amt] : needed) {
            add_material(client.materials, rid, -amt);
        }
        for (const uint64_t eid : enh_ids) {
            store_->remove_item(eid);
            std::erase_if(client.items, [&](const ItemInstance& inst) { return inst.id == eid; });
        }
        // A Pharmacologist's medicine rolls as if a module more went in (docs/PROFESSIONS.md).
        const bool pharmacologist = sch->profession == "doctor" && config_.professions != nullptr &&
                                    has_grant(*config_.professions, client.professions, "doctor", "pharmacologist");
        const int modules = std::min(2, int(enh_ids.size()) + (pharmacologist ? 1 : 0));
        const auto bonus = [&](std::string_view key) {
            return config_.professions ? perk_bonus(*config_.professions, client.professions, sch->profession, key) : 0.0;
        };
        const CraftPreview preview = book.preview_craft(*sch, req.resource_ids, modules, lv, uint8_t(focus), bonus("grade"),
                                                        1.0 + bonus("exceptional"));
        const double extra_chance = bonus("extra_item");
        std::array<int, item_grade_count> made{};
        const int unit_count = sch->output_count > 1 ? sch->output_count
                                                     : (out_def && out_def->kind == ItemKind::ammo ? out_def->ammo_batch : 1);
        int extras = 0;
        for (int i = 0; i < batch; ++i) {
            const ItemGrade grade =
                graded ? CraftingBook::roll_craft_grade(preview, random_unit()) : ItemGrade::standard;
            // A crafting perk's chance of one more, of the same grade.
            const int copies = extra_chance > 0.0 && random_unit() < extra_chance ? 2 : 1;
            extras += copies - 1;
            for (int c = 0; c < copies; ++c) {
                ++made[size_t(grade)];
                if (out_def != nullptr && (unit_count > 1 || out_def->kind == ItemKind::ammo)) {
                    grant_stacked_item(client, *out_def, unit_count, grade);
                } else {
                    client.items.push_back(store_->grant_item({0, client.character, sch->output_item, grade, "",
                                                               with_item_json_field("{}", "mods", mods), 1, mark}));
                }
            }
        }
        save_materials(client);
        ++client.inventory_rev;
        client.last_sync_tick = 0;
        ++client.crafting_rev;
        client.crafting_sync_tick = 0;
        earn_profession_xp(client, sch->profession, mark_craft_xp(sch->base_xp, mark) * batch);
        quest_event(client, "craft");
        // "Crafted 3x E-11 Blaster Rifle Mk II (2 Fine, 1 Exceptional)".
        std::string grades;
        for (int g = item_grade_count - 1; graded && g >= 0; --g) {
            if (made[size_t(g)] == 0) continue;
            const std::string_view name = item_grade_name(ItemGrade(g));
            grades += (grades.empty() ? "" : ", ") +
                      (batch + extras > 1 ? std::format("{} {}", made[size_t(g)], name) : std::string(name));
        }
        notify(client.entity, std::format("Crafted {}x {}{}", batch + extras,
                                          out_def ? item_display_name(*out_def, mark) : sch->name,
                                          grades.empty() ? "" : " (" + grades + ")"));
    }
}

} // namespace swm::core
