// Professions on the server (docs/PROFESSIONS.md, docs/PERKS.md): experience
// from what players do, levels, and the perks and milestones they've learned.
#include <algorithm>
#include <format>

#include "swm/core/server.h"

namespace swm::core {

const std::vector<ProfessionProgress>* GameServer::professions_of(uint32_t entity) const {
    const Client* client = find(entity);
    return client ? &client->professions : nullptr;
}

std::string GameServer::held_profession(const Client& client) const {
    if (config_.professions == nullptr || client.state.vehicle >= 0) {
        return {};
    }
    const WeaponDef* w = weapon_in_slot(loadout_of(client), client.state.combat.weapon);
    const ProfessionDef* def = w ? config_.professions->for_archetype(w->archetype) : nullptr;
    return def && def->available ? def->id : std::string();
}

std::string GameServer::armor_profession(const Client& client) const {
    if (config_.professions == nullptr) {
        return {};
    }
    const Loadout& lo = loadout_of(client);
    if (lo.armor_class.empty()) {
        return {};  // Battlefront's own soldiers'
    }
    const ProfessionDef* def = config_.professions->for_armor(lo.armor_class);
    return def && def->available ? def->id : std::string();
}

void GameServer::earn_profession_xp(Client& client, const std::string& id, int64_t xp) {
    const ProfessionBook* book = config_.professions;
    const ProfessionDef* def = book ? book->find(id) : nullptr;
    if (def == nullptr || !def->available || xp <= 0) {
        return;
    }
    xp = int64_t(std::llround(double(xp) * std::max(0.0, client.stats.xp_gain)));  // (a Studious Inspiration)
    if (store_ == nullptr || client.character == 0) {
        // A match: the hub saves it with the battle's rewards.
        client.battle_profession_xp[id] += xp;
        return;
    }
    if (!licensed(*book, client.professions, id)) {
        return;  // only a licence holder levels it (docs/PERKS.md)
    }
    const int before = book->level_for(profession_xp(client.professions, id));
    const int after = book->level_for(add_profession_xp(client.professions, id, xp));
    ++client.profession_rev;
    client.professions_dirty = true;
    if (after == before) {
        return;
    }
    std::string text = std::format("{} reached level {}", def->name, after);
    for (const ProfessionMilestone& m : def->milestones) {
        if (m.level > before && m.level <= after) {
            text += std::format(": a trainer can teach you {}", m.name);
        }
    }
    for (const int level : perk_levels) {
        if (def->chooses() && level > before && level <= after) {
            text += std::format(": a trainer can teach you a level-{} perk", level);
        }
    }
    notify(client.entity, text);
    save_professions(client);
    // Ranks grow with the level (none now; kept for data that has them).
    if (def->rank_every > 0) {
        rebuild_client_loadouts(client);
        ++client.inventory_rev;
        client.last_sync_tick = 0;
        client.stats = stats_for(loadout_of(client), client.species, config_.base_stats);
        client.health = std::min(client.health, client.stats.max_health);
        client.shield = std::min(client.shield, client.stats.shield_capacity);
    }
}

void GameServer::route_xp(Client& client, int64_t xp) {
    if (config_.professions == nullptr || xp <= 0) {
        return;
    }
    const std::vector<std::string> ids = routed_professions(*config_.professions, loadout_of(client));
    if (ids.empty()) {
        return;
    }
    const int64_t share = std::max<int64_t>(1, xp / int64_t(ids.size()));
    for (const std::string& id : ids) {
        earn_profession_xp(client, id, share);
    }
}

int GameServer::combat_level_of(const Client& client) const {
    return config_.professions != nullptr ? combat_level(*config_.professions, loadout_of(client), client.professions) : 1;
}

void GameServer::save_professions(Client& client) {
    if (store_ == nullptr || client.character == 0 || !client.professions_dirty) {
        return;
    }
    try {
        store_->save_professions(client.character, client.professions);
        client.professions_dirty = false;
    } catch (const std::exception& error) {
        say(std::format("profession save error for {}: {}", client.name, error.what()));
    }
}

void GameServer::send_profession_sync(Client& client) {
    net::ProfessionSync sync;
    sync.revision = client.profession_rev;
    sync.professions = client.professions;
    link_.send(client.endpoint, net::encode(sync));
    client.profession_sync_tick = tick_;
}

void GameServer::award_profession_xp(const std::string& name, const std::map<std::string, int64_t>& earned,
                                     int64_t routed) {
    const ProfessionBook* book = config_.professions;
    if (book == nullptr || store_ == nullptr || (earned.empty() && routed <= 0)) {
        return;
    }
    for (Client& client : clients_) {
        if (client.name == name && client.character != 0) {
            for (const auto& [id, xp] : earned) {
                earn_profession_xp(client, id, xp);
            }
            route_xp(client, routed);
            save_professions(client);
            return;
        }
    }
    // Not here (still on the battle's server): saved for when they're back.
    try {
        const auto saved = store_->find_by_name(name);
        if (!saved) {
            return;
        }
        std::vector<ProfessionProgress> progress = store_->list_professions(saved->id);
        for (const auto& [id, xp] : earned) {
            if (const ProfessionDef* def = book->find(id); def && def->available && xp > 0) {
                add_profession_xp(progress, id, xp);
            }
        }
        if (routed > 0 && config_.catalog != nullptr) {
            const std::vector<CharacterLoadout> loadouts = store_->list_loadouts(saved->id);
            const auto active = std::find_if(loadouts.begin(), loadouts.end(), [&](const CharacterLoadout& cl) {
                return cl.slot_index == saved->active_loadout;
            });
            if (active != loadouts.end()) {
                const Species species = species_from_name(saved->species).value_or(Species::human);
                const Loadout lo = config_.catalog->build_loadout(*active, store_->list_items(saved->id), species);
                const std::vector<std::string> ids = routed_professions(*book, lo);
                for (const std::string& id : ids) {
                    if (const ProfessionDef* def = book->find(id); def && def->available) {
                        add_profession_xp(progress, id, std::max<int64_t>(1, routed / int64_t(ids.size())));
                    }
                }
            }
        }
        store_->save_professions(saved->id, progress);
    } catch (const std::exception& error) {
        say(std::format("profession save error for {}: {}", name, error.what()));
    }
}

void GameServer::apply_professions(const Client& client, const CharacterLoadout& cl, Loadout& loadout) const {
    if (config_.professions != nullptr) {
        apply_profession_perks(loadout, *config_.professions, client.professions);
    }
    // Stage 12: its abilities, specialist levels, Companion Slot and slices.
    fill_specialist_loadout(client, cl, loadout);
}

} // namespace swm::core
