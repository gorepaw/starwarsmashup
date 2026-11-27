// A hub's service points on the server: trainers (who teach a profession's
// milestones), the bank (credits and spare items, safe from death), clinics
// (who patch you up for a fee) and cloning terminals (where your clone is kept).
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <sstream>

#include "swm/core/server.h"

namespace swm::core {

namespace {

constexpr int64_t clinic_fee = 25;
constexpr int64_t travel_fare = 100;  // a shuttle ticket to another of the planet's towns (Stage 13)

// A bank item's fields on a line each, tab-separated (backslash escapes tab, newline and itself).
std::string escape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string unescape(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            ++i;
            out += text[i] == 't' ? '\t' : text[i] == 'n' ? '\n' : text[i];
        } else {
            out += text[i];
        }
    }
    return out;
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            field += line[i];
            field += line[++i];
        } else if (line[i] == '\t') {
            out.push_back(unescape(field));
            field.clear();
        } else {
            field += line[i];
        }
    }
    out.push_back(unescape(field));
    return out;
}

bool equipped_anywhere(const std::vector<CharacterLoadout>& loadouts, uint64_t item_id) {
    for (const CharacterLoadout& cl : loadouts) {
        if (cl.primary_id == item_id || cl.sidearm_id == item_id || cl.secondary_id == item_id) return true;
        for (const auto& [slot, id] : cl.gear_ids) {
            if (id == item_id) return true;
        }
        for (const auto& [slot, id] : cl.appearance_ids) {
            if (id == item_id) return true;
        }
    }
    return false;
}

} // namespace

const net::Vendor* GameServer::service_near(const Client& client, const std::string& kind,
                                            const std::string& detail) const {
    for (const net::Vendor& v : vendors_) {
        if (v.kind != kind) continue;
        if (!detail.empty()) {
            // `detail` is one of a comma-separated list.
            const std::string list = "," + v.detail + ",";
            if (list.find("," + detail + ",") == std::string::npos) continue;
        }
        const Vec3 d = client.state.position - v.position;
        if (std::hypot(d.x, d.z) <= v.radius + 1.5) return &v;
    }
    return nullptr;
}

namespace {

std::vector<std::string> split_commas(const std::string& text) {
    std::vector<std::string> out;
    std::string item;
    for (const char c : text) {
        if (c == ',') {
            if (!item.empty()) out.push_back(std::move(item));
            item.clear();
        } else {
            item += c;
        }
    }
    if (!item.empty()) out.push_back(std::move(item));
    return out;
}

std::string join_commas(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        if (!out.empty()) out += ',';
        out += item;
    }
    return out;
}

int clamp_level(const std::string& text) {
    return text.empty() ? 0 : std::clamp(std::atoi(text.c_str()), 0, profession_max_level);
}

} // namespace

// What a character has with the services: what they've learned from
// trainers, the bank, where their clone is kept.
void GameServer::load_services_data(Client& client) {
    if (store_ == nullptr || client.character == 0) return;
    if (config_.professions != nullptr) {
        int64_t refund = 0;
        for (const ProfessionDef& def : config_.professions->all()) {
            const std::string trained = store_->get_extra(client.character, "trained:" + def.id);
            const std::string paid = store_->get_extra(client.character, "paid:" + def.id);
            const std::string perks = store_->get_extra(client.character, "perks:" + def.id);
            const std::string paid_perks = store_->get_extra(client.character, "paid_perks:" + def.id);
            if (trained.empty() && paid.empty() && perks.empty() && paid_perks.empty()) continue;
            // Learned before it had any experience: it may have no row yet.
            ProfessionProgress& p = progress_for(client.professions, def.id);
            p.trained = clamp_level(trained);
            p.paid = paid.empty() ? p.trained : clamp_level(paid);
            p.perks.clear();
            p.paid_perks.clear();
            for (std::string& id : split_commas(perks)) {
                const ProfessionDef* owner = nullptr;
                if (config_.professions->find_perk(id, &owner) && owner == &def) p.perks.push_back(std::move(id));
            }
            for (std::string& id : split_commas(paid_perks)) {
                const ProfessionDef* owner = nullptr;
                if (config_.professions->find_perk(id, &owner) && owner == &def) p.paid_perks.push_back(std::move(id));
            }
            if (def.chooses() && p.trained > 0) {
                // An old save's milestones, from before perks (docs/PERKS.md):
                // what they cost is refunded, and they're gone.
                for (const int level : {1, 25, 50, 75, 100}) {
                    if (level <= p.trained) refund += milestone_cost(level);
                }
                p.trained = 0;
                p.paid = 0;
                store_->set_extra(client.character, "trained:" + def.id, "0");
                store_->set_extra(client.character, "paid:" + def.id, "0");
            }
        }
        if (refund > 0) {
            client.credits += refund;
            store_->update_economy(client.character, client.credits, client.faction_points, client.experience,
                                   int(client.loadout));
            say(std::format("{}: {} Cr refunded for weapon and armor milestones (now perks)", client.name, refund));
        }
    }
    client.bank_credits = std::atoll(store_->get_extra(client.character, "bank_credits").c_str());
    client.bank_items.clear();
    client.next_bank_id = 1;
    std::istringstream lines(store_->get_extra(client.character, "bank_items"));
    std::string line;
    while (std::getline(lines, line)) {
        const std::vector<std::string> f = split_tabs(line);
        if (f.size() < 5) continue;
        ItemInstance item;
        item.id = std::strtoull(f[0].c_str(), nullptr, 10);
        item.owner_id = client.character;
        item.def_id = f[1];
        item.quality = std::atof(f[2].c_str());
        item.custom_name = f[3];
        item.color_json = f[4];
        if (f.size() > 5) item.mark = clamp_mark(std::atoi(f[5].c_str()));
        client.next_bank_id = std::max(client.next_bank_id, item.id + 1);
        client.bank_items.push_back(std::move(item));
    }
    const std::string bound = store_->get_extra(client.character, "bound");
    if (!bound.empty()) {
        std::istringstream fields(bound);
        double x = 0, z = 0;
        if (fields >> x >> z) client.bound = Vec3{x, 0, z};
    }
}

void GameServer::save_training(Client& client, const std::string& profession) {
    if (store_ == nullptr || client.character == 0) return;
    const ProfessionProgress* p = find_progress(client.professions, profession);
    if (p == nullptr) return;
    try {
        store_->set_extra(client.character, "trained:" + profession, std::to_string(p->trained));
        store_->set_extra(client.character, "paid:" + profession, std::to_string(p->paid));
        store_->set_extra(client.character, "perks:" + profession, join_commas(p->perks));
        store_->set_extra(client.character, "paid_perks:" + profession, join_commas(p->paid_perks));
    } catch (const std::exception& error) {
        say(std::format("training save error for {}: {}", client.name, error.what()));
    }
}

void GameServer::training_changed(Client& client) {
    ++client.profession_rev;
    client.professions_dirty = true;
    rebuild_client_loadouts(client);
    ++client.inventory_rev;
    client.last_sync_tick = 0;
    client.stats = stats_for(loadout_of(client), client.species, config_.base_stats);
    client.health = std::min(client.health, client.stats.max_health);
    client.shield = std::min(client.shield, client.stats.shield_capacity);
    refresh_abilities(client);
}

bool GameServer::require_licence(Client& client, const std::string& profession) {
    // Without a character store (a match, a test) nothing is learned to check.
    if (config_.professions == nullptr || store_ == nullptr || client.character == 0 ||
        licensed(*config_.professions, client.professions, profession)) {
        return true;
    }
    const ProfessionDef* def = config_.professions->find(profession);
    notify(client.entity, std::format("Learn {} from a trainer first.", def ? def->name : profession));
    return false;
}

void GameServer::save_bank(Client& client) {
    if (store_ == nullptr || client.character == 0) return;
    std::string text;
    for (const ItemInstance& item : client.bank_items) {
        text += std::format("{}\t{}\t{}\t{}\t{}\t{}\n", item.id, escape(item.def_id), item.quality,
                            escape(item.custom_name), escape(item.color_json), item.mark);
    }
    try {
        store_->set_extra(client.character, "bank_credits", std::to_string(client.bank_credits));
        store_->set_extra(client.character, "bank_items", text);
    } catch (const std::exception& error) {
        say(std::format("bank save error for {}: {}", client.name, error.what()));
    }
    ++client.inventory_rev;
    client.last_sync_tick = 0;
}

void GameServer::handle_service_request(Client& client, const net::ServiceRequest& req) {
    if (conquest_ || store_ == nullptr || client.character == 0 || client.life != Life::alive) return;
    const auto save_economy_now = [&] {
        store_->update_economy(client.character, client.credits, client.faction_points, client.experience,
                               int(client.loadout));
    };
    switch (req.action) {
    case net::ServiceRequest::train:
    case net::ServiceRequest::learn_perk:
    case net::ServiceRequest::forget: {
        // A trainer teaches and takes back their own professions' perks and
        // milestones (docs/PERKS.md).
        const ProfessionBook* book = config_.professions;
        if (book == nullptr) return;
        const ProfessionDef* def = nullptr;
        if (book->find_perk(req.id, &def) == nullptr) def = book->find(req.id);
        if (def == nullptr || !def->available || service_near(client, "trainer", def->id) == nullptr) {
            notify(client.entity, "There's no one here to teach that.");
            return;
        }
        TrainResult result;
        switch (req.action) {
        case net::ServiceRequest::train:
            result = learn_milestone(*book, client.professions, def->id, int(req.amount), client.credits);
            break;
        case net::ServiceRequest::learn_perk:
            result = learn_perk(*book, client.professions, req.id, client.credits);
            break;
        default:
            result = forget(*book, client.professions, req.id);
            break;
        }
        notify(client.entity, result.message);
        if (!result.ok) return;
        if (result.cost > 0) {
            client.credits -= result.cost;
            save_economy_now();
        }
        save_training(client, def->id);
        training_changed(client);
        if (req.action != net::ServiceRequest::forget) quest_event(client, "learn");
        return;
    }
    case net::ServiceRequest::bank_deposit_credits:
    case net::ServiceRequest::bank_withdraw_credits: {
        if (service_near(client, "bank") == nullptr || req.amount <= 0) return;
        const bool deposit = req.action == net::ServiceRequest::bank_deposit_credits;
        int64_t& from = deposit ? client.credits : client.bank_credits;
        int64_t& to = deposit ? client.bank_credits : client.credits;
        const int64_t moved = std::min(req.amount, from);
        if (moved <= 0) return;
        from -= moved;
        to += moved;
        save_economy_now();
        save_bank(client);
        notify(client.entity, std::format("{} {} Cr", deposit ? "Deposited" : "Withdrew", moved));
        quest_event(client, "bank");
        return;
    }
    case net::ServiceRequest::bank_deposit_item: {
        if (service_near(client, "bank") == nullptr || config_.catalog == nullptr) return;
        const auto it = std::find_if(client.items.begin(), client.items.end(),
                                     [&](const ItemInstance& i) { return i.id == req.item_id; });
        if (it == client.items.end()) return;
        const ItemDef* def = config_.catalog->find(it->def_id);
        if (def == nullptr || def->kind == ItemKind::quest) return;
        if (equipped_anywhere(client.char_loadouts, it->id)) {
            notify(client.entity, "Unequip that before banking it.");
            return;
        }
        if (client.bank_items.size() >= 500) {
            notify(client.entity, "Your bank is full.");
            return;
        }
        ItemInstance banked = *it;
        banked.id = client.next_bank_id++;
        if (!store_->remove_item(it->id)) return;
        client.bank_items.push_back(std::move(banked));
        client.items = store_->list_items(client.character);
        ++client.inventory_rev;
        save_bank(client);
        notify(client.entity, "Banked " + item_name(def->id));
        quest_event(client, "bank");
        return;
    }
    case net::ServiceRequest::bank_withdraw_item: {
        if (service_near(client, "bank") == nullptr) return;
        const auto it = std::find_if(client.bank_items.begin(), client.bank_items.end(),
                                     [&](const ItemInstance& i) { return i.id == req.item_id; });
        if (it == client.bank_items.end()) return;
        store_->grant_item({0, client.character, it->def_id, it->quality, it->custom_name, it->color_json, 1, it->mark});
        const std::string name = item_name(it->def_id);
        client.bank_items.erase(it);
        client.items = store_->list_items(client.character);
        ++client.inventory_rev;
        save_bank(client);
        notify(client.entity, "Took " + name + " from the bank");
        return;
    }
    case net::ServiceRequest::heal: {
        if (service_near(client, "clinic") == nullptr) return;
        if (client.health >= client.stats.max_health && client.shield >= client.stats.shield_capacity) {
            notify(client.entity, "You don't need patching up.");
            return;
        }
        if (client.credits < clinic_fee) {
            notify(client.entity, std::format("{} Cr for a patch-up.", clinic_fee));
            return;
        }
        client.credits -= clinic_fee;
        save_economy_now();
        client.health = client.stats.max_health;
        client.shield = client.stats.shield_capacity;
        notify(client.entity, std::format("Patched up (-{} Cr)", clinic_fee));
        quest_event(client, "heal");
        return;
    }
    case net::ServiceRequest::bind: {
        const net::Vendor* terminal = service_near(client, "cloning");
        if (terminal == nullptr) return;
        client.bound = terminal->position;
        store_->set_extra(client.character, "bound", std::format("{} {}", terminal->position.x, terminal->position.z));
        notify(client.entity, "Your clone is kept here now: " + terminal->name);
        quest_event(client, "bind");
        return;
    }
    case net::ServiceRequest::travel: {
        // A shuttle (or starport transport) to another of the planet's travel
        // points; from a starport, to another planet's (Stage 13).
        const net::Vendor* from = service_near(client, "travel");
        if (from == nullptr) from = service_near(client, "starport");
        if (from == nullptr) return;
        const auto offworld = std::find_if(vendors_.begin(), vendors_.end(), [&](const net::Vendor& v) {
            return v.kind == "travel_offworld" && v.name == req.id;
        });
        if (offworld != vendors_.end()) {
            if (from->kind != "starport") {
                notify(client.entity, "Ships to other planets leave from a starport.");
                return;
            }
            if (client.state.vehicle >= 0) {
                notify(client.entity, "Put your vehicle away before boarding.");
                return;
            }
            const size_t bar = offworld->detail.find('|');
            const int64_t fare = bar == std::string::npos ? 1000 : std::atoll(offworld->detail.c_str() + bar + 1);
            if (client.credits < fare) {
                notify(client.entity, std::format("A ticket to {} is {} Cr.", offworld->name, fare));
                return;
            }
            if (!on_offworld_travel) {
                notify(client.entity, "No ships are flying there.");
                return;
            }
            client.credits -= fare;
            save_economy_now();
            quest_event(client, "travel");
            say(std::format("{} bought passage from {} to {} ({} Cr)", client.name, from->name, offworld->name, fare));
            on_offworld_travel(client.entity, offworld->name);
            return;
        }
        const auto to = std::find_if(vendors_.begin(), vendors_.end(), [&](const net::Vendor& v) {
            return (v.kind == "travel" || v.kind == "starport") && v.name == req.id;
        });
        if (to == vendors_.end() || to->name == from->name) {
            notify(client.entity, "No shuttle goes there from here.");
            return;
        }
        if (client.state.vehicle >= 0) {
            notify(client.entity, "Put your vehicle away before boarding.");
            return;
        }
        const int64_t fare = travel_fare;
        if (client.credits < fare) {
            notify(client.entity, std::format("A ticket to {} is {} Cr.", to->name, fare));
            return;
        }
        client.credits -= fare;
        save_economy_now();
        // Off the shuttle at the far end, spread out a little.
        const uint32_t slot = client.entity > 0 ? client.entity - 1 : 0;
        Vec3 pos = world_.clamp(to->position + Vec3{1.5 * double(slot % 4) - 2.25, 0, 1.5 * double((slot / 4) % 3)});
        const double ground = world_.terrain().height_at(pos.x, pos.z);
        const double reach = to->position.y > ground + 0.5 ? to->position.y + 1.0 : ground + 1.0;
        pos.y = world_.support_at(pos.x, pos.z, reach, client.stats.move.step_up).height;
        if (pos.y < -1e8) pos.y = ground;
        client.state.position = pos;  // weapons and all as they were
        client.state.velocity = {};
        client.history.clear();
        ++client.spawn_count;  // clients jump there rather than glide
        client.extract_started = -1.0;
        client.switching_to = -1;
        notify(client.entity, std::format("Arrived at {} ({} Cr)", to->name, fare));
        quest_event(client, "travel");
        say(std::format("{} travelled from {} to {}", client.name, from->name, to->name));
        return;
    }
    default:
        return;
    }
}

} // namespace swm::core
