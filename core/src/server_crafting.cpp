// Gathering, Material Vault, Unified Workbench crafting, Recipe Presets,
// Hub Work Orders, and Salvaging on the server (docs/PROFESSIONS.md).
#include <algorithm>
#include <cmath>
#include <format>

#include "swm/core/server.h"

namespace swm::core {

namespace {

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
    link_.send(client.endpoint, net::encode(sync));
    client.crafting_sync_tick = tick_;
}

void GameServer::handle_gather_request(Client& client, const net::GatherRequest& req, double now) {
    if (config_.crafting == nullptr || client.life != Life::alive) return;
    const CraftingBook& book = *config_.crafting;

    if (req.action == net::GatherRequest::extract_terrain) {
        if (client.extract_started >= 0.0) return;  // already at it
        const ResourceCategory cat = ResourceCategory(req.category);
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
        if (harvested_corpses_.contains(corpse_id)) {
            notify(client.entity, "Already harvested.");
            return;
        }
        const auto it = std::find_if(corpse_loots_.begin(), corpse_loots_.end(),
                                     [&](const CorpseLoot& c) { return c.npc_entity == corpse_id; });
        if (it == corpse_loots_.end()) return;
        const Vec3 d = client.state.position - it->position;
        if (d.x * d.x + d.z * d.z > 64.0) return;
        harvested_corpses_.insert(corpse_id);

        const bool salvage = is_bandit_name(it->name);
        const ResourceCategory cat = salvage ? ResourceCategory::salvage : ResourceCategory::organic;
        const std::string prof = salvage ? "salvaging" : "foraging";
        const int lv = profession_level(config_.professions, client.professions, prof);
        std::vector<const ResourceDef*> choices;
        for (const ResourceDef& r : book.resources()) {
            if (r.category == cat) choices.push_back(&r);
        }
        if (choices.empty()) return;
        const ResourceDef* picked = choices[size_t(corpse_id) % choices.size()];
        const int64_t qty = std::max<int64_t>(3, 4 + lv / 12);
        add_material(client.materials, picked->id, qty);
        save_materials(client);
        ++client.crafting_rev;
        client.crafting_sync_tick = 0;
        earn_profession_xp(client, prof, 28);
        notify(client.entity, std::format("{} {}x {} from {}", salvage ? "Salvaged" : "Harvested", qty, picked->name, it->name));
        return;
    }

    if (req.action == net::GatherRequest::salvage_item) {
        if (config_.catalog == nullptr || store_ == nullptr || client.character == 0) return;
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
        const auto yield = book.salvage_yield(*def, lv);
        if (!store_->remove_item(item_id)) return;
        client.items.erase(it);
        for (const MaterialStack& st : yield.materials) {
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
    const double yield_bonus = 1.0 + double(lv) * 0.01 + (lv >= 50 ? 0.20 : 0.0);
    const int64_t qty = std::max<int64_t>(2, int64_t(std::llround((4.0 + 14.0 * conc) * yield_bonus)));
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
        if (order == nullptr) return;
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
        client.credits += order->reward_credits;
        client.faction_points += order->reward_fp;
        save_economy(client);
        ++client.inventory_rev;
        client.last_sync_tick = 0;
        earn_profession_xp(client, order->profession, order->reward_xp);
        notify(client.entity, std::format("Work order complete: +{} Cr, +{} FP, +{} {} XP", order->reward_credits,
                                          order->reward_fp, order->reward_xp, order->profession));
        return;
    }

    if (req.action == net::CraftRequest::craft) {
        if (store_ == nullptr || client.character == 0 || config_.catalog == nullptr) return;
        const SchematicDef* sch = book.find_schematic(req.schematic_id);
        if (sch == nullptr) return;
        const int lv = profession_level(config_.professions, client.professions, sch->profession);
        if (lv < sch->required_level) {
            notify(client.entity, std::format("Requires {} level {}.", sch->profession, sch->required_level));
            return;
        }
        const int batch = std::clamp<int>(req.batch_count, 1, 10);
        if (req.resource_ids.size() < sch->slots.size()) {
            notify(client.entity, "Fill every material slot first.");
            return;
        }
        std::map<std::string, int64_t> needed;
        for (size_t i = 0; i < sch->slots.size(); ++i) {
            const ResourceDef* r = book.find_resource(req.resource_ids[i]);
            if (r == nullptr || r->category != sch->slots[i].category) {
                notify(client.entity, "Invalid material for schematic slot.");
                return;
            }
            needed[r->id] += int64_t(sch->slots[i].amount) * batch;
        }
        for (const auto& [rid, amt] : needed) {
            if (material_amount(client.materials, rid) < amt) {
                notify(client.entity, "Not enough materials in Material Vault.");
                return;
            }
        }
        std::vector<uint64_t> enh_ids;
        for (const uint64_t eid : req.enhancement_item_ids) {
            if (int(enh_ids.size()) >= sch->enhancement_slots) break;
            if (std::find(enh_ids.begin(), enh_ids.end(), eid) != enh_ids.end()) continue;
            if (item_equipped_in_loadouts(client.char_loadouts, eid)) continue;
            const auto it = std::find_if(client.items.begin(), client.items.end(),
                                         [&](const ItemInstance& inst) { return inst.id == eid; });
            if (it == client.items.end()) continue;
            const ItemDef* edef = config_.catalog->find(it->def_id);
            if (edef != nullptr && edef->kind == ItemKind::enhancement) {
                enh_ids.push_back(eid);
            }
        }
        for (const auto& [rid, amt] : needed) {
            add_material(client.materials, rid, -amt);
        }
        for (const uint64_t eid : enh_ids) {
            store_->remove_item(eid);
            std::erase_if(client.items, [&](const ItemInstance& inst) { return inst.id == eid; });
        }
        const CraftPreview preview =
            book.preview_craft(*sch, req.resource_ids, int(enh_ids.size()), lv, req.exp_quality_pct);
        double best_q = 0.0;
        std::string last_outcome = "Success";
        for (int i = 0; i < batch; ++i) {
            const auto [q, outcome] = CraftingBook::roll_craft_quality(preview, random_unit());
            best_q = std::max(best_q, q);
            last_outcome = outcome;
            client.items.push_back(store_->grant_item({0, client.character, sch->output_item, q, "", "{}"}));
        }
        save_materials(client);
        ++client.inventory_rev;
        client.last_sync_tick = 0;
        ++client.crafting_rev;
        client.crafting_sync_tick = 0;
        earn_profession_xp(client, sch->profession, sch->base_xp * batch);
        quest_event(client, "craft");
        notify(client.entity, std::format("Crafted {}x {} ({}% - {})", batch, sch->name,
                                          int(std::lround(best_q * 100.0)), last_outcome));
    }
}

} // namespace swm::core
