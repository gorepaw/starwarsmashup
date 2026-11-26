#include "swm/core/server.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>

#include "swm/core/net/bytes.h"
#include "swm/core/net/protocol.h"

namespace swm::core {

namespace {

// Professions: a point of armor experience for each 10 damage taken
// (ASSUMPTION), and 25 for a weapon's kill of a soldier in a match (as a
// battle's reward counts a kill).
constexpr double armor_damage_per_xp = 10;
constexpr int64_t match_kill_profession_xp = 25;

} // namespace

GameServer::GameServer(net::Link& link, const World& world, ServerConfig config, CharacterStore* store)
    : link_(link), world_(world), config_(std::move(config)), store_(config_.battlefield ? nullptr : store) {
    if (config_.battlefield) {
        conquest_.emplace(*config_.battlefield, !config_.single_match);
    }
    add_companion_looks();  // Stage 12
}

bool GameServer::match_over() const {
    return conquest_ && config_.single_match && conquest_->state().phase == Phase::ended &&
           conquest_->state().time_left <= 0;
}

void GameServer::set_recruiters(std::vector<net::Recruiter> recruiters, std::vector<std::string> factions) {
    recruiters_ = std::move(recruiters);
    factions_ = std::move(factions);
}

void GameServer::set_quartermasters(std::vector<net::Quartermaster> quartermasters) {
    quartermasters_ = std::move(quartermasters);
}

uint32_t GameServer::add_range_target(std::string name, Vec3 home, Vec3 patrol_end, double speed, double max_health,
                                      double mitigation, double respawn_time) {
    RangeTarget t;
    t.entity = next_entity_++;
    t.name = std::move(name);
    t.home = world_.clamp(home);
    t.home.y = world_.ground_at(t.home.x, t.home.z, t.home.y + 1.0, 0.5).height;
    if (t.home.y < -1e8) t.home.y = world_.terrain().height_at(t.home.x, t.home.z);
    t.patrol_end = world_.clamp(patrol_end);
    t.patrol_end.y = world_.ground_at(t.patrol_end.x, t.patrol_end.z, t.patrol_end.y + 1.0, 0.5).height;
    if (t.patrol_end.y < -1e8) t.patrol_end.y = world_.terrain().height_at(t.patrol_end.x, t.patrol_end.z);
    t.position = t.home;
    t.speed = std::max(0.0, speed);
    t.max_health = std::max(1.0, max_health);
    t.health = t.max_health;
    t.mitigation = std::clamp(mitigation, 0.0, 0.9);
    t.respawn_time = std::max(0.2, respawn_time);
    targets_.push_back(std::move(t));
    return targets_.back().entity;
}

void GameServer::set_player_loadouts(uint32_t entity, std::vector<Loadout> loadouts,
                                     std::vector<std::string> worn_per_loadout) {
    Client* client = find(entity);
    if (client == nullptr || loadouts.empty()) {
        return;
    }
    client->custom_loadouts = std::move(loadouts);
    client->worn_per_loadout = std::move(worn_per_loadout);
    if (client->loadout >= client->custom_loadouts.size()) {
        client->loadout = 0;
    }
    client->stats = stats_for(loadout_of(*client), client->species, config_.base_stats);
    if (client->life == Life::waiting || client->health <= 0 || client->health > client->stats.max_health) {
        client->health = client->stats.max_health;
    }
    reset_loadout(client->state, loadout_of(*client), client->stats);
    ++client->inventory_rev;
    client->last_sync_tick = 0;
}

std::vector<Loadout> GameServer::build_player_loadouts(uint32_t entity, bool apply_quality,
                                                       std::vector<std::string>* out_worn) const {
    const Client* client = find(entity);
    if (client == nullptr || config_.catalog == nullptr) {
        return {};
    }
    std::vector<Loadout> out;
    if (out_worn) out_worn->clear();
    for (const CharacterLoadout& cl : client->char_loadouts) {
        out.push_back(config_.catalog->build_loadout(cl, client->items, client->species, apply_quality));
        apply_professions(*client, cl, out.back());
        if (out_worn) {
            out_worn->push_back(resolve_worn_models(*config_.catalog, cl, client->items, client->species, client->gender));
        }
    }
    return out;
}

void GameServer::award_battle_rewards(const std::string& name, const std::string& faction, const BattleReward& reward) {
    if (store_ == nullptr) {
        return;
    }
    try {
        if (const auto saved = store_->find_by_name(name)) {
            const int64_t cr = saved->credits + reward.credits;
            const int64_t fp = saved->faction_points + reward.faction_points;
            const int64_t xp = saved->experience + reward.xp;
            store_->update_economy(saved->id, cr, fp, xp, saved->active_loadout);
            std::map<std::string, int64_t> rep = store_->list_reputation(saved->id);
            const std::string norm = normalize_faction(faction);
            if (!norm.empty() && norm != "neutral") {
                rep[norm] = clamp_reputation(rep[norm] + reward.faction_points);
                const std::string opp = opposing_faction(norm);
                if (!opp.empty()) {
                    rep[opp] = clamp_reputation(rep[opp] - reward.faction_points / 2);
                }
                store_->save_reputation(saved->id, rep);
            }
            for (Client& c : clients_) {
                if (c.name == name) {
                    c.credits = cr;
                    c.faction_points = fp;
                    c.experience = xp;
                    c.reputation = rep;
                    ++c.inventory_rev;
                    c.last_sync_tick = 0;
                }
            }
        }
    } catch (const std::exception& e) {
        say(std::format("reward save error for {}: {}", name, e.what()));
    }
}

namespace {

bool apply_ammo_quantities(CharacterStore* store, PersistentId char_id, std::vector<ItemInstance>& items,
                           std::vector<CharacterLoadout>& char_loadouts, size_t active_slot,
                           int rem_std, int rem_hvy, bool& scrubbed) {
    bool changed = false;
    scrubbed = false;
    if (store == nullptr || char_id == 0 || char_loadouts.empty()) return false;
    const CharacterLoadout& cl = char_loadouts[std::min(active_slot, char_loadouts.size() - 1)];
    for (const auto& [slot_key, new_qty] :
         {std::pair<const char*, int>{"ammo_standard", rem_std},
          std::pair<const char*, int>{"ammo_heavy", rem_hvy}}) {
        if (new_qty < 0) continue;
        const auto git = cl.gear_ids.find(slot_key);
        if (git == cl.gear_ids.end() || git->second == 0) continue;
        const auto it = std::find_if(items.begin(), items.end(),
                                     [&](const ItemInstance& inst) { return inst.id == git->second; });
        if (it == items.end() || it->quantity == new_qty) continue;
        changed = true;
        if (new_qty > 0) {
            it->quantity = new_qty;
            store->update_item_quantity(it->id, new_qty);
        } else {
            const uint64_t dead_id = it->id;
            store->remove_item(dead_id);
            items.erase(it);
            for (CharacterLoadout& lo : char_loadouts) {
                bool lo_chg = false;
                for (auto g = lo.gear_ids.begin(); g != lo.gear_ids.end();) {
                    if (g->second == dead_id) { g = lo.gear_ids.erase(g); lo_chg = true; }
                    else ++g;
                }
                if (lo_chg) store->save_loadout(char_id, lo);
            }
            scrubbed = true;
        }
    }
    return changed;
}

} // namespace

void GameServer::apply_match_ammo_consumption(const std::string& name, int remaining_std_ammo,
                                              int remaining_hvy_ammo) {
    if (store_ == nullptr || (remaining_std_ammo < 0 && remaining_hvy_ammo < 0)) {
        return;
    }
    try {
        Client* live = nullptr;
        for (Client& c : clients_) {
            if (c.name == name) {
                live = &c;
                break;
            }
        }
        if (live != nullptr && live->character != 0) {
            bool scrubbed = false;
            if (apply_ammo_quantities(store_, live->character, live->items, live->char_loadouts, live->loadout,
                                      remaining_std_ammo, remaining_hvy_ammo, scrubbed)) {
                rebuild_client_loadouts(*live);
                live->synced_std_ammo = remaining_std_ammo;
                live->synced_hvy_ammo = remaining_hvy_ammo;
                ++live->inventory_rev;
                live->last_sync_tick = 0;
            }
            return;
        }
        if (const auto saved = store_->find_by_name(name)) {
            auto items = store_->list_items(saved->id);
            auto los = store_->list_loadouts(saved->id);
            bool scrubbed = false;
            apply_ammo_quantities(store_, saved->id, items, los, size_t(std::max(0, saved->active_loadout)),
                                  remaining_std_ammo, remaining_hvy_ammo, scrubbed);
        }
    } catch (const std::exception& e) {
        say(std::format("ammo sync error for {}: {}", name, e.what()));
    }
}

ItemInstance GameServer::grant_stacked_item(Client& client, const ItemDef& def, int delta_qty, double quality) {
    if (store_ == nullptr || client.character == 0) {
        return {};
    }
    const int add_qty = std::max(1, delta_qty);
    if (def.kind == ItemKind::ammo && def.ammo_class != AmmoClass::none) {
        const int cap = max_ammo_for(def.ammo_class);
        for (ItemInstance& inst : client.items) {
            if (inst.def_id == def.id && inst.quantity < cap) {
                const int next_qty = std::min(cap, inst.quantity + add_qty);
                inst.quantity = next_qty;
                store_->update_item_quantity(inst.id, next_qty);
                rebuild_client_loadouts(client);
                if (!client.char_loadouts.empty()) {
                    const auto& cl = client.char_loadouts[std::min(client.loadout, client.char_loadouts.size() - 1)];
                    const auto git = cl.gear_ids.find(def.slot);
                    if (git != cl.gear_ids.end() && git->second == inst.id) {
                        int clip_sum = 0;
                        const Loadout& lo = loadout_of(client);
                        for (int s = 0; s < weapon_slots; ++s) {
                            if (const WeaponDef* w = weapon_in_slot(lo, s); w && w->ammo_class == def.ammo_class) {
                                clip_sum += std::max<int>(0, client.state.combat.weapons[size_t(s)].rounds);
                            }
                        }
                        const int spare = std::max(0, next_qty - clip_sum);
                        for (int s = 0; s < weapon_slots; ++s) {
                            if (const WeaponDef* w = weapon_in_slot(lo, s); w && w->ammo_class == def.ammo_class) {
                                client.state.combat.weapons[size_t(s)].reserve = int16_t(spare);
                            }
                        }
                        if (def.ammo_class == AmmoClass::standard) client.synced_std_ammo = next_qty;
                        else if (def.ammo_class == AmmoClass::heavy) client.synced_hvy_ammo = next_qty;
                    }
                }
                return inst;
            }
        }
        ItemInstance created = store_->grant_item({0, client.character, def.id, quality, "", "{}", std::min(cap, add_qty)});
        client.items.push_back(created);
        return created;
    }
    ItemInstance created = store_->grant_item({0, client.character, def.id, quality, "", "{}", 1});
    client.items.push_back(created);
    return created;
}

void GameServer::sync_client_ammo(Client& client, bool flush_now) {
    if (conquest_ || store_ == nullptr || client.character == 0 || client.custom_loadouts.empty()) {
        return;
    }
    // Aboard a vehicle or mount, the combat state is its weapons', not the
    // soldier's (theirs is stashed until they get out): counted against the
    // soldier's loadout it reads as no ammo left, and the stack was deleted.
    // The item keeps what was last synced on foot.
    if (client.state.vehicle >= 0) {
        return;
    }
    const Loadout& lo = loadout_of(client);
    const int rem_std = remaining_unified_ammo(client.state.combat, lo, AmmoClass::standard);
    const int rem_hvy = remaining_unified_ammo(client.state.combat, lo, AmmoClass::heavy);
    if (rem_std == client.synced_std_ammo && rem_hvy == client.synced_hvy_ammo && !flush_now) {
        return;
    }
    if (!flush_now && client.ammo_sync_timer < 0.5 && rem_std != 0 && rem_hvy != 0) {
        return;
    }
    client.ammo_sync_timer = 0.0;
    client.synced_std_ammo = rem_std;
    client.synced_hvy_ammo = rem_hvy;
    for (Loadout& cur : client.custom_loadouts) {
        if (rem_std >= 0) cur.unified_standard_ammo = rem_std;
        if (rem_hvy >= 0) cur.unified_heavy_ammo = rem_hvy;
    }
    bool scrubbed = false;
    if (apply_ammo_quantities(store_, client.character, client.items, client.char_loadouts, client.loadout,
                              rem_std, rem_hvy, scrubbed)) {
        if (scrubbed) {
            rebuild_client_loadouts(client);
            reset_loadout(client.state, loadout_of(client), client.stats);
        }
        ++client.inventory_rev;
    }
}

net::PlayerStatus* GameServer::status(uint32_t entity) {
    Client* client = find(entity);
    return client ? &client->status : nullptr;
}

void GameServer::notify(uint32_t entity, const std::string& text) {
    if (Client* client = find(entity)) {
        // Several in one tick (a kill's reward and the quest it advanced) share the line.
        const std::string line = client->notice_tick == tick_ && !client->status.notice.empty()
                                     ? client->status.notice + "  |  " + text
                                     : text;
        client->status.notice = line.substr(0, 255);
        client->notice_tick = tick_;
        ++client->status.notice_id;
    }
}

void GameServer::transfer(uint32_t entity, const net::Transfer& transfer) {
    if (Client* client = find(entity)) {
        client->transfer = transfer;
        client->transfer_tick = tick_ + uint32_t(3 * config_.tick_rate);
    }
}

void GameServer::transfer_all(const net::Transfer& transfer) {
    for (const Client& client : clients_) {
        this->transfer(client.entity, transfer);
    }
}

std::string GameServer::name_of(uint32_t entity) const {
    const Client* client = find(entity);
    return client ? client->name : std::string();
}

void GameServer::say(const std::string& text) const {
    if (log) {
        log(text);
    }
}

void GameServer::send(const net::Endpoint& to, const auto& message) {
    link_.send(to, net::encode(message));
}

GameServer::Client* GameServer::find(const net::Endpoint& endpoint) {
    auto it = std::find_if(clients_.begin(), clients_.end(), [&](const Client& c) { return c.endpoint == endpoint; });
    return it == clients_.end() ? nullptr : &*it;
}

GameServer::Client* GameServer::find(uint32_t entity) {
    auto it = std::find_if(clients_.begin(), clients_.end(), [&](const Client& c) { return c.entity == entity; });
    return it == clients_.end() ? nullptr : &*it;
}

const GameServer::Client* GameServer::find(uint32_t entity) const {
    auto it = std::find_if(clients_.begin(), clients_.end(), [&](const Client& c) { return c.entity == entity; });
    return it == clients_.end() ? nullptr : &*it;
}

std::vector<uint32_t> GameServer::entities() const {
    std::vector<uint32_t> ids;
    for (const Client& client : clients_) {
        ids.push_back(client.entity);
    }
    return ids;
}

const MoveState* GameServer::state_of(uint32_t entity) const {
    if (const Client* client = find(entity)) {
        return &client->state;
    }
    const Npc* npc = find_npc(entity);
    return npc == nullptr ? nullptr : &npc->state;
}

uint32_t GameServer::add_npc(const std::string& name, Vec3 position, double yaw, uint8_t look, uint8_t pose,
                             uint16_t colors, double radius) {
    constexpr double lift = 1;  // from a meter over where it's put (as spawns are)
    const MoveParams params;
    Npc npc;
    npc.entity = next_entity_++;
    npc.name = name;
    npc.look = look;
    npc.pose = pose;
    npc.colors = colors;
    npc.radius = radius;
    npc.state.position = world_.clamp(position);
    npc.state.position.y = world_.support_at(npc.state.position.x, npc.state.position.z,
                                             position.y + lift, params.step_up).height;
    if (npc.state.position.y < -1e8) {
        npc.state.position.y = world_.terrain().height_at(npc.state.position.x, npc.state.position.z);
    }
    npc.state.yaw = float(yaw);
    npc.state.grounded = true;
    npc.input.yaw = float(yaw);
    npc_index_[npc.entity] = npcs_.size();
    npcs_.push_back(npc);
    return npc.entity;
}

uint32_t GameServer::add_combat_npc(const std::string& name, Vec3 position, double yaw, uint8_t look,
                                    const NpcCombatSpec& spec, uint16_t colors) {
    constexpr double lift = 1;
    const MoveParams params;
    Npc npc;
    npc.entity = next_entity_++;
    npc.name = name;
    npc.look = look;
    npc.pose = 0;
    npc.actor_kind = spec.actor_kind;
    npc.passive = spec.passive;
    npc.colors = colors;
    npc.radius = spec.hit_radius;
    npc.max_health = std::max(1.0, spec.max_health);
    npc.health = npc.max_health;
    npc.hit_radius = std::max(0.2, spec.hit_radius);
    npc.hit_height = std::max(0.4, spec.hit_height);
    npc.mitigation = std::clamp(spec.mitigation, 0.0, 0.9);
    npc.walk_speed = spec.walk_speed;
    npc.run_speed = spec.run_speed;
    npc.loadout = spec.loadout;
    npc.weapon_model = spec.weapon_model;
    npc.anim_set = spec.anim_set;
    npc.state.position = world_.clamp(position);
    npc.state.position.y = world_.support_at(npc.state.position.x, npc.state.position.z,
                                             position.y + lift, params.step_up).height;
    if (npc.state.position.y < -1e8) {
        npc.state.position.y = world_.terrain().height_at(npc.state.position.x, npc.state.position.z);
    }
    npc.state.yaw = float(yaw);
    npc.state.grounded = true;
    npc.input.yaw = float(yaw);
    CharacterStats own = stats_for(npc.loadout, config_.base_stats);
    reset_loadout(npc.state, npc.loadout, own);
    npc_index_[npc.entity] = npcs_.size();
    npcs_.push_back(std::move(npc));
    return npcs_.back().entity;
}

GameServer::Npc* GameServer::find_npc(uint32_t entity) {
    const auto it = npc_index_.find(entity);
    return it == npc_index_.end() ? nullptr : &npcs_[it->second];
}

const GameServer::Npc* GameServer::find_npc(uint32_t entity) const {
    const auto it = npc_index_.find(entity);
    return it == npc_index_.end() ? nullptr : &npcs_[it->second];
}

void GameServer::remove_npc(uint32_t entity) {
    const auto it = npc_index_.find(entity);
    if (it == npc_index_.end()) {
        return;
    }
    clear_corpse_loot(entity);
    const size_t index = it->second;
    npc_index_.erase(it);
    if (index + 1 != npcs_.size()) {
        npcs_[index] = std::move(npcs_.back());
        npc_index_[npcs_[index].entity] = index;
    }
    npcs_.pop_back();
}

std::vector<Vec3> GameServer::player_positions() const {
    std::vector<Vec3> out;
    out.reserve(clients_.size());
    for (const Client& client : clients_) {
        if (client.life != Life::waiting) {
            out.push_back(client.state.position);
        }
    }
    return out;
}

bool GameServer::npc_awake_at(Vec3 position) const {
    return conquest_ || config_.npc_sim_radius <= 0 || player_grid_.any_near(position, config_.npc_sim_radius);
}

ServerPerf GameServer::take_perf(double now) {
    ServerPerf out = perf_;
    out.window = perf_since_ < 0 ? double(out.ticks) / config_.tick_rate : now - perf_since_;
    perf_ = {};
    perf_since_ = now;
    return out;
}

std::optional<GameServer::NpcInfo> GameServer::npc_info(uint32_t entity) const {
    const Npc* npc = find_npc(entity);
    if (!npc) return std::nullopt;
    return NpcInfo{npc->entity, npc->name, npc->state, npc->health, npc->max_health, npc->life,
                   npc->actor_kind, npc->last_attacker, npc->last_damaged_tick, npc->hidden};
}

void GameServer::set_npc_pose(uint32_t entity, uint8_t pose) {
    if (Npc* npc = find_npc(entity)) npc->pose = pose;
}

void GameServer::heal_npc(uint32_t entity, double amount) {
    if (Npc* npc = find_npc(entity); npc && npc->life == Life::alive && amount > 0) {
        npc->health = std::min(npc->max_health, npc->health + amount);
    }
}

void GameServer::respawn_npc(uint32_t entity, Vec3 position, double yaw) {
    clear_corpse_loot(entity);
    Npc* npc = find_npc(entity);
    if (!npc) return;
    constexpr double lift = 1.0;
    const MoveParams params;
    npc->life = Life::alive;
    npc->health = npc->max_health;
    npc->hidden = false;
    npc->pose = 0;
    npc->last_attacker = 0;
    npc->last_damaged_tick = 0;
    npc->state = MoveState{};
    npc->state.position = world_.clamp(position);
    npc->state.position.y = world_.support_at(npc->state.position.x, npc->state.position.z,
                                              position.y + lift, params.step_up).height;
    if (npc->state.position.y < -1e8) {
        npc->state.position.y = world_.terrain().height_at(npc->state.position.x, npc->state.position.z);
    }
    npc->state.yaw = float(yaw);
    npc->state.grounded = true;
    npc->input = MoveInput{};
    npc->input.yaw = float(yaw);
    ++npc->spawn_count;
    CharacterStats own = stats_for(npc->loadout, config_.base_stats);
    reset_loadout(npc->state, npc->loadout, own);
}

void GameServer::set_npc_hidden(uint32_t entity, bool hidden) {
    if (hidden) {
        clear_corpse_loot(entity);
    }
    if (Npc* npc = find_npc(entity)) npc->hidden = hidden;
}

void GameServer::set_vendors(std::vector<net::Vendor> vendors) {
    vendors_ = std::move(vendors);
}

void GameServer::award_credits(uint32_t entity, int64_t credits) {
    if (credits == 0) return;
    Client* client = find(entity);
    if (!client) return;
    client->credits = std::max<int64_t>(0, client->credits + credits);
    if (store_ != nullptr && client->character != 0) {
        try {
            store_->update_economy(client->character, client->credits, client->faction_points,
                                   client->experience, int(client->loadout));
        } catch (const std::exception& error) {
            say(std::format("credits persist error: {}", error.what()));
        }
    }
}

void GameServer::set_corpse_loot(uint32_t npc_entity, uint32_t owner_entity, const std::string& name, Vec3 position,
                                 std::vector<net::LootItemEntry> items) {
    clear_corpse_loot(npc_entity);
    if (items.empty()) return;
    corpse_loots_.push_back({npc_entity, owner_entity, name, position, std::move(items)});
}

void GameServer::clear_corpse_loot(uint32_t npc_entity) {
    std::erase_if(corpse_loots_, [npc_entity](const CorpseLoot& c) { return c.npc_entity == npc_entity; });
}

void GameServer::npc_melee(uint32_t attacker_entity, uint32_t victim_entity, double raw_damage, DamageType type) {
    Client* victim = find(victim_entity);
    if (!victim || victim->life != Life::alive) return;
    damage(*victim, raw_damage, type, attacker_entity);
}

void GameServer::award_xp(uint32_t entity, int64_t xp) {
    Client* client = find(entity);
    if (!client || xp <= 0) return;
    client->experience += xp;
    if (const std::string held = held_profession(*client); !held.empty()) {
        earn_profession_xp(*client, held, xp);
    } else {
        route_xp(*client, xp);
    }
    if (store_ != nullptr && client->character != 0) {
        try {
            store_->update_economy(client->character, client->credits, client->faction_points,
                                   client->experience, int(client->loadout));
        } catch (const std::exception&) {
        }
    }
}

void GameServer::steer_npc(uint32_t entity, const MoveInput& input) {
    for (Npc& npc : npcs_) {
        if (npc.entity == entity) {
            npc.input = input;
            if (npc.actor_kind <= 1) {
                npc.input.buttons &= MoveInput::sprint | MoveInput::jump;  // citizens don't fight
            }
        }
    }
}

std::vector<uint32_t> GameServer::npcs() const {
    std::vector<uint32_t> ids;
    for (const Npc& npc : npcs_) {
        ids.push_back(npc.entity);
    }
    return ids;
}

void GameServer::set_state(uint32_t entity, const MoveState& state) {
    if (Client* client = find(entity)) {
        client->state = state;
    }
}

double GameServer::health_of(uint32_t entity) const {
    const Client* client = find(entity);
    return client ? client->health : 0;
}

bool GameServer::alive(uint32_t entity) const {
    const Client* client = find(entity);
    return client && client->life == Life::alive;
}

std::optional<net::EntityState> GameServer::describe(uint32_t entity) const {
    const Client* client = find(entity);
    return client ? std::optional(describe(*client)) : std::nullopt;
}

std::optional<PlayerProfile> GameServer::profile_of(uint32_t entity) const {
    const Client* client = find(entity);
    if (client == nullptr) {
        return std::nullopt;
    }
    return PlayerProfile{client->species, client->gender, client->appearance};
}

void GameServer::set_player_profile(uint32_t entity, const PlayerProfile& profile) {
    Client* client = find(entity);
    if (client == nullptr) {
        return;
    }
    client->species = profile.species;
    client->gender = profile.gender;
    if (!profile.appearance.empty()) {
        client->appearance = profile.appearance;
    }
    client->stats = stats_for(loadout_of(*client), client->species, config_.base_stats);
    if (client->life == Life::waiting || client->health <= 0 || client->health > client->stats.max_health) {
        client->health = client->stats.max_health;
    }
}

net::EntityState GameServer::describe(const Client& c) const {
    net::EntityState out;
    out.id = c.entity;
    out.state = c.state;
    out.health = float(c.health);
    out.max_health = float(c.stats.max_health);
    out.shield = float(c.shield);
    out.max_shield = float(c.stats.shield_capacity);
    out.life = c.life;
    out.spawn_count = c.spawn_count;
    out.team = int8_t(c.team);
    out.loadout = uint8_t(c.loadout);
    out.kills = c.kills;
    out.deaths = c.deaths;
    out.captures = c.captures;
    out.actor_kind = 0;
    out.species = uint8_t(c.species);
    out.gender = uint8_t(c.gender);
    out.name = c.name;
    out.appearance = c.appearance;
    if (!c.custom_loadouts.empty()) {
        const Loadout& lo = loadout_of(c);
        if (const WeaponDef* w = weapon_in_slot(lo, c.state.combat.weapon)) {
            out.weapon_model = w->model;
            out.anim_set = w->anim_set;
        }
        if (c.loadout < c.worn_per_loadout.size()) {
            out.worn = c.worn_per_loadout[c.loadout];
        }
    } else if (config_.catalog != nullptr) {
        out.worn = plain_clothes(*config_.catalog, c.gender);  // a bot's, or a guest's: plain clothes
    }
    return out;
}

const Loadout& GameServer::loadout_of(const Client& client) const {
    if (!client.custom_loadouts.empty()) {
        return client.custom_loadouts[std::min(client.loadout, client.custom_loadouts.size() - 1)];
    }
    if (config_.battlefield && client.team >= 0) {
        const auto& loadouts = config_.battlefield->team(client.team).loadouts;
        return loadouts[std::min(client.loadout, loadouts.size() - 1)];
    }
    return config_.loadout;
}

void GameServer::scrub_item_from_loadouts(Client& client, uint64_t item_id) {
    if (store_ == nullptr || client.character == 0 || item_id == 0) return;
    for (CharacterLoadout& cl : client.char_loadouts) {
        bool changed = false;
        if (cl.primary_id == item_id) { cl.primary_id = 0; changed = true; }
        if (cl.sidearm_id == item_id) { cl.sidearm_id = 0; changed = true; }
        if (cl.secondary_id == item_id) { cl.secondary_id = 0; changed = true; }
        for (auto git = cl.gear_ids.begin(); git != cl.gear_ids.end();) {
            if (git->second == item_id) { git = cl.gear_ids.erase(git); changed = true; } else { ++git; }
        }
        for (auto ait = cl.appearance_ids.begin(); ait != cl.appearance_ids.end();) {
            if (ait->second == item_id) { ait = cl.appearance_ids.erase(ait); changed = true; } else { ++ait; }
        }
        if (changed) {
            store_->save_loadout(client.character, cl);
        }
    }
}

void GameServer::rebuild_client_loadouts(Client& client) {
    if (config_.catalog == nullptr) {
        return;
    }
    client.custom_loadouts.clear();
    client.worn_per_loadout.clear();
    for (const CharacterLoadout& cl : client.char_loadouts) {
        client.custom_loadouts.push_back(
            config_.catalog->build_loadout(cl, client.items, client.species, true));
        apply_professions(client, cl, client.custom_loadouts.back());
        client.worn_per_loadout.push_back(
            resolve_worn_models(*config_.catalog, cl, client.items, client.species, client.gender));
    }
    if (!client.custom_loadouts.empty() && client.loadout >= client.custom_loadouts.size()) {
        client.loadout = 0;
    }
}

void GameServer::send_inventory_sync(Client& client) {
    net::InventorySync sync;
    sync.revision = client.inventory_rev;
    sync.items = client.items;
    sync.loadouts = client.char_loadouts;
    sync.resolved_loadouts = client.custom_loadouts;
    sync.bank_credits = client.bank_credits;
    sync.bank_items = client.bank_items;
    sync.reputation = client.reputation;
    send(client.endpoint, sync);
    client.last_sync_tick = tick_;
}

void GameServer::update(double now) {
    if (!started_) {
        started_ = true;
        next_tick_time_ = now;
        next_save_time_ = now + config_.save_interval;
    }
    now_ = now;
    receive(now);
    const double dt = 1 / config_.tick_rate;
    if (now - next_tick_time_ > 0.25) {
        next_tick_time_ = now;  // stalled (e.g. a debugger): don't try to catch up
    }
    while (now >= next_tick_time_) {
        step();
        next_tick_time_ += dt;
    }
    drop_silent(now);
    if (now >= next_save_time_) {
        save_all();
        next_save_time_ = now + config_.save_interval;
    }
}

void GameServer::receive(double now) {
    while (auto datagram = link_.receive()) {
        try {
            handle(datagram->from, *datagram, now);
        } catch (const net::PacketError& error) {
            say(std::format("dropped a packet from {}: {}", datagram->from.to_string(), error.what()));
        }
    }
}

void GameServer::handle(const net::Endpoint& from, const net::Datagram& datagram, double now) {
    const net::Message message = net::decode(datagram.data);
    Client* client = find(from);
    if (const auto* hello = std::get_if<net::Hello>(&message)) {
        if (client == nullptr) {
            client = join(from, hello->name, hello->ticket, hello->team);
            if (client == nullptr) {
                return;
            }
            if (on_join) {
                const uint32_t entity = client->entity;
                on_join(entity, client->name);
                client = find(entity);
            }
        }
        // Also answers a repeated Hello whose Welcome was lost.
        send(from, net::Welcome{client->entity, config_.tick_rate, tick_, client->character, recruiters_, factions_,
                                looks_, quartermasters_, vendors_, quest_npcs_, mission_terminals_, workbenches_});
    }
    if (client == nullptr) {
        return;  // not connected: ignore everything but Hello
    }
    client->last_heard = now;
    if (const auto* batch = std::get_if<net::InputBatch>(&message)) {
        // Batches overlap: keep only inputs newer than any already seen.
        uint32_t newest = client->queue.empty() ? client->last_input : client->queue.back().sequence;
        for (const MoveInput& input : batch->inputs) {
            if (input.sequence > newest && std::isfinite(input.yaw) && std::isfinite(input.pitch) &&
                std::isfinite(input.view_tick)) {
                client->queue.push_back(input);
                newest = input.sequence;
            }
        }
        while (client->queue.size() > config_.max_queued_inputs) {
            client->queue.pop_front();
        }
    } else if (const auto* request = std::get_if<net::SpawnRequest>(&message)) {
        client->wants_spawn = *request;
    } else if (const auto* queue = std::get_if<net::QueueRequest>(&message)) {
        // Joining a queue takes standing at a battle terminal (its kind says
        // ground or space battles); leaving doesn't.
        const net::Recruiter* terminal = nullptr;
        for (const net::Recruiter& r : recruiters_) {
            const Vec3 d = client->state.position - r.position;
            if (std::hypot(d.x, d.z) <= r.radius + 1) {
                terminal = &r;
                break;
            }
        }
        if (on_queue && !client->transfer && (queue->team < 0 || (client->life == Life::alive && terminal))) {
            const uint8_t kind = terminal ? uint8_t(terminal->kind) : uint8_t(net::Recruiter::ground);
            on_queue(client->entity, queue->team, queue->mode, kind);
        }
    } else if (const auto* ack = std::get_if<net::InventoryAck>(&message)) {
        if (ack->revision == client->inventory_rev) {
            client->acked_inventory_rev = ack->revision;
        }
    } else if (const auto* profession_ack = std::get_if<net::ProfessionAck>(&message)) {
        if (profession_ack->revision == client->profession_rev) {
            client->acked_profession_rev = profession_ack->revision;
        }
    } else if (const auto* quest_ack = std::get_if<net::QuestAck>(&message)) {
        if (quest_ack->revision == client->quest_rev) {
            client->acked_quest_rev = quest_ack->revision;
        }
    } else if (const auto* quest = std::get_if<net::QuestRequest>(&message)) {
        handle_quest_request(*client, *quest);
    } else if (const auto* crafting_ack = std::get_if<net::CraftingAck>(&message)) {
        if (crafting_ack->revision == client->crafting_rev) {
            client->acked_crafting_rev = crafting_ack->revision;
        }
    } else if (const auto* gather = std::get_if<net::GatherRequest>(&message)) {
        handle_gather_request(*client, *gather, now);
    } else if (const auto* craft = std::get_if<net::CraftRequest>(&message)) {
        handle_craft_request(*client, *craft);
    } else if (const auto* service = std::get_if<net::ServiceRequest>(&message)) {
        handle_service_request(*client, *service);
    } else if (const auto* spec_request = std::get_if<net::SpecialistRequest>(&message)) {
        handle_specialist_request(*client, *spec_request);  // Stage 12
    } else if (const auto* spec_ack = std::get_if<net::SpecialistAck>(&message)) {
        if (spec_ack->revision == client->spec.revision) {
            client->spec.acked = spec_ack->revision;
        }
    } else if (const auto* save_lo = std::get_if<net::SaveLoadoutRequest>(&message)) {
        if (!conquest_ && store_ != nullptr && config_.catalog != nullptr && client->character != 0 &&
            save_lo->loadout.slot_index >= 0 && save_lo->loadout.slot_index < max_character_loadouts) {
            const auto owns = [&](uint64_t id) {
                if (id == 0) return true;
                return std::any_of(client->items.begin(), client->items.end(),
                                   [id](const ItemInstance& inst) { return inst.id == id; });
            };
            CharacterLoadout clean = save_lo->loadout;
            clean.specializations = config_.professions
                                        ? clean_specializations(*config_.professions, clean.specializations)
                                        : std::vector<std::string>{};
            // Stage 12: the Companion Slot keeps a companion from their datapad.
            if (!clean.companion.empty() &&
                std::none_of(client->spec.companions.begin(), client->spec.companions.end(), [&](const CompanionRecord& r) {
                    return std::to_string(r.id) == clean.companion;
                })) {
                clean.companion.clear();
            }
            if (!owns(clean.primary_id)) clean.primary_id = 0;
            if (!owns(clean.sidearm_id)) clean.sidearm_id = 0;
            if (!owns(clean.secondary_id)) clean.secondary_id = 0;
            for (auto it = clean.gear_ids.begin(); it != clean.gear_ids.end();) {
                if (!owns(it->second) || !species_can_wear_slot(client->species, it->first)) {
                    it = clean.gear_ids.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto it = clean.appearance_ids.begin(); it != clean.appearance_ids.end();) {
                if (!owns(it->second)) {
                    it = clean.appearance_ids.erase(it);
                } else {
                    ++it;
                }
            }
            if (clean.name.empty()) {
                clean.name = "Loadout " + std::to_string(clean.slot_index + 1);
            }
            sync_client_ammo(*client, true);
            store_->save_loadout(client->character, clean);
            client->char_loadouts = store_->list_loadouts(client->character);
            rebuild_client_loadouts(*client);
            ++client->inventory_rev;
            client->last_sync_tick = 0;
            if (client->loadout < client->char_loadouts.size() &&
                client->char_loadouts[client->loadout].slot_index == clean.slot_index) {
                client->stats = stats_for(loadout_of(*client), client->species, config_.base_stats);
                client->health = std::min(client->health, client->stats.max_health);
                reset_loadout(client->state, loadout_of(*client), client->stats);
            }
        }
    } else if (const auto* sw = std::get_if<net::SwitchLoadoutRequest>(&message)) {
        if (!conquest_ && client->life == Life::alive && sw->slot < client->custom_loadouts.size() &&
            sw->slot != client->loadout) {
            client->switching_to = int(sw->slot);
            client->switch_remaining = loadout_switch_time;
        }
    } else if (const auto* buy = std::get_if<net::ShopBuyRequest>(&message)) {
        if (!conquest_ && store_ != nullptr && config_.catalog != nullptr && client->character != 0) {
            const ItemDef* def = config_.catalog->find(buy->def_id);
            if (def != nullptr && def->kind != ItemKind::junk) {
                const net::Quartermaster* matched_qm = nullptr;
                for (const net::Quartermaster& q : quartermasters_) {
                    const Vec3 d = client->state.position - q.position;
                    if (std::hypot(d.x, d.z) > q.radius + 1.5) continue;
                    const std::string qf = normalize_faction(q.faction);
                    const bool faction_ok = def->faction == "neutral" || qf == "neutral" || def->faction == qf;
                    if (faction_ok) {
                        matched_qm = &q;
                        break;
                    }
                }
                const bool near_gear_vendor = std::any_of(vendors_.begin(), vendors_.end(),
                                                          [&](const net::Vendor& v) {
                    // Gear vendors sell combat gear, weapons, supplies & enhancements for Credits; garages sell vehicles.
                    if (!((v.kind == "gear_vendor" && def->cost_credits > 0) ||
                          (v.kind == "garage" && def->kind == ItemKind::vehicle))) return false;
                    const Vec3 d = client->state.position - v.position;
                    return std::hypot(d.x, d.z) <= v.radius + 1.5;
                });
                if (buy->use_faction_points && matched_qm != nullptr) {
                    if (def->kind != ItemKind::clothing) {
                        notify(client->entity, "Quartermasters only issue cosmetic rewards for Faction Points");
                    } else if (def->cost_faction_points <= 0) {
                        notify(client->entity, def->display_name + " is not available for Faction Points");
                    } else {
                        const std::string qf = normalize_faction(matched_qm->faction);
                        const std::string rep_faction = def->faction != "neutral" ? def->faction : qf;
                        const int64_t rep_val = rep_faction != "neutral" && client->reputation.contains(rep_faction)
                                                    ? client->reputation.at(rep_faction)
                                                    : 0;
                        if (reputation_tier(rep_val) < def->required_rank) {
                            notify(client->entity,
                                   std::format("Requires {} standing", reputation_tier_name(def->required_rank)));
                        } else if (client->faction_points >= def->cost_faction_points) {
                            client->faction_points -= def->cost_faction_points;
                            store_->update_economy(client->character, client->credits, client->faction_points,
                                                   client->experience, int(client->loadout));
                            store_->grant_item({0, client->character, def->id, 0.5, "", "{}", 1});
                            client->items = store_->list_items(client->character);
                            ++client->inventory_rev;
                            client->last_sync_tick = 0;
                            notify(client->entity, std::format("Purchased {} (-{} FP)", def->display_name,
                                                               def->cost_faction_points));
                        }
                    }
                } else if (!buy->use_faction_points && near_gear_vendor &&
                           def->cost_credits > 0 && client->credits >= def->cost_credits) {
                    sync_client_ammo(*client, true);
                    client->credits -= def->cost_credits;
                    store_->update_economy(client->character, client->credits, client->faction_points,
                                           client->experience, int(client->loadout));
                    const int qty = def->kind == ItemKind::ammo ? def->ammo_batch : 1;
                    grant_stacked_item(*client, *def, qty, 0.5);
                    client->items = store_->list_items(client->character);
                    ++client->inventory_rev;
                    client->last_sync_tick = 0;
                    notify(client->entity, def->kind == ItemKind::ammo
                                               ? std::format("Purchased {} x{} (-{} Cr)", def->display_name, qty,
                                                             def->cost_credits)
                                               : std::format("Purchased {} (-{} Cr)", def->display_name, def->cost_credits));
                }
            }
        }
    } else if (const auto* del = std::get_if<net::DestroyItemRequest>(&message)) {
        if (!conquest_ && store_ != nullptr && config_.catalog != nullptr && client->character != 0) {
            const auto it = std::find_if(client->items.begin(), client->items.end(),
                                         [&](const ItemInstance& inst) { return inst.id == del->item_id; });
            if (it != client->items.end() && store_->remove_item(del->item_id)) {
                scrub_item_from_loadouts(*client, del->item_id);
                client->items = store_->list_items(client->character);
                client->char_loadouts = store_->list_loadouts(client->character);
                rebuild_client_loadouts(*client);
                ++client->inventory_rev;
                client->last_sync_tick = 0;
                client->stats = stats_for(loadout_of(*client), client->species, config_.base_stats);
                client->health = std::min(client->health, client->stats.max_health);
                reset_loadout(client->state, loadout_of(*client), client->stats);
            }
        }
    } else if (const auto* take = std::get_if<net::LootTakeRequest>(&message)) {
        if (!conquest_ && client->life == Life::alive) {
            const auto cit = std::find_if(corpse_loots_.begin(), corpse_loots_.end(), [&](const CorpseLoot& c) {
                if (c.npc_entity != take->corpse_entity) return false;
                if (c.owner_entity != 0 && c.owner_entity != client->entity) return false;
                const Vec3 d = client->state.position - c.position;
                return std::hypot(d.x, d.z) <= 7.0;
            });
            if (cit != corpse_loots_.end()) {
                std::vector<net::LootItemEntry> taken;
                for (auto iit = cit->items.begin(); iit != cit->items.end();) {
                    if (take->slot < 0 || int(iit->slot) == int(take->slot)) {
                        taken.push_back(*iit);
                        iit = cit->items.erase(iit);
                    } else {
                        ++iit;
                    }
                }
                if (!taken.empty()) {
                    for (const net::LootItemEntry& entry : taken) {
                        if (store_ != nullptr && client->character != 0) {
                            store_->grant_item({0, client->character, entry.def_id, double(entry.quality), "", "{}"});
                        } else {
                            uint64_t next_id = 1;
                            for (const ItemInstance& cur : client->items) next_id = std::max(next_id, cur.id + 1);
                            client->items.push_back({next_id, 0, entry.def_id, double(entry.quality), "", "{}"});
                        }
                    }
                    if (store_ != nullptr && client->character != 0) {
                        client->items = store_->list_items(client->character);
                    }
                    ++client->inventory_rev;
                    client->last_sync_tick = 0;
                    if (taken.size() == 1 && config_.catalog != nullptr) {
                        const ItemDef* def = config_.catalog->find(taken[0].def_id);
                        notify(client->entity, "Looted " + (def ? def->display_name : taken[0].def_id));
                    } else {
                        notify(client->entity, std::format("Looted {} item(s)", taken.size()));
                    }
                }
                if (cit->items.empty() && (cit->harvested || config_.crafting == nullptr)) {
                    corpse_loots_.erase(cit);
                }
            }
        }
    } else if (const auto* sell = std::get_if<net::VendorSellRequest>(&message)) {
        if (!conquest_ && store_ != nullptr && config_.catalog != nullptr && client->character != 0) {
            const bool near_vendor = std::any_of(vendors_.begin(), vendors_.end(), [&](const net::Vendor& v) {
                if (v.kind != "junk_dealer" && v.kind != "gear_vendor") return false;  // (a shop buys; a service doesn't)
                const Vec3 d = client->state.position - v.position;
                return std::hypot(d.x, d.z) <= v.radius + 1.5;
            });
            if (near_vendor) {
                if (sell->sell_all_junk) {
                    int sold_count = 0;
                    int64_t total_cr = 0;
                    for (const ItemInstance& inst : client->items) {
                        const ItemDef* def = config_.catalog->find(inst.def_id);
                        if (def != nullptr && def->kind == ItemKind::junk && store_->remove_item(inst.id)) {
                            ++sold_count;
                            total_cr += std::max<int64_t>(1, def->sell_value);
                        }
                    }
                    if (sold_count > 0) {
                        client->credits += total_cr;
                        store_->update_economy(client->character, client->credits, client->faction_points,
                                               client->experience, int(client->loadout));
                        client->items = store_->list_items(client->character);
                        ++client->inventory_rev;
                        client->last_sync_tick = 0;
                        notify(client->entity, std::format("Sold {} junk item(s) for {} Cr", sold_count, total_cr));
                    }
                } else if (sell->item_id != 0) {
                    const auto it = std::find_if(client->items.begin(), client->items.end(),
                                                 [&](const ItemInstance& inst) { return inst.id == sell->item_id; });
                    if (it != client->items.end()) {
                        const ItemDef* def = config_.catalog->find(it->def_id);
                        if (def != nullptr && def->sell_value > 0 && store_->remove_item(sell->item_id)) {
                            scrub_item_from_loadouts(*client, sell->item_id);
                            client->credits += def->sell_value;
                            store_->update_economy(client->character, client->credits, client->faction_points,
                                                   client->experience, int(client->loadout));
                            client->items = store_->list_items(client->character);
                            client->char_loadouts = store_->list_loadouts(client->character);
                            rebuild_client_loadouts(*client);
                            ++client->inventory_rev;
                            client->last_sync_tick = 0;
                            client->stats = stats_for(loadout_of(*client), client->species, config_.base_stats);
                            client->health = std::min(client->health, client->stats.max_health);
                            reset_loadout(client->state, loadout_of(*client), client->stats);
                            notify(client->entity, std::format("Sold {} for {} Cr", def->display_name, def->sell_value));
                        }
                    }
                }
            }
        }
    } else if (const auto* use = std::get_if<net::UseItemRequest>(&message)) {
        if (!conquest_ && client->life == Life::alive && store_ != nullptr && config_.catalog != nullptr &&
            client->character != 0) {
            const auto it = std::find_if(client->items.begin(), client->items.end(),
                                         [&](const ItemInstance& inst) { return inst.id == use->item_id; });
            if (it != client->items.end()) {
                const ItemDef* def = config_.catalog->find(it->def_id);
                if (def != nullptr && def->kind == ItemKind::vehicle) {
                    call_vehicle(*client, *def);
                } else if (def != nullptr && use_specialist_item(*client, *def, use->item_id)) {
                    // Stage 12: used.
                } else if (def != nullptr && def->kind == ItemKind::supply && def->heal_amount > 0 &&
                    client->health < client->stats.max_health && store_->remove_item(use->item_id)) {
                    const double before = client->health;
                    client->health = std::min(client->stats.max_health, client->health + def->heal_amount);
                    client->items = store_->list_items(client->character);
                    ++client->inventory_rev;
                    client->last_sync_tick = 0;
                    notify(client->entity, std::format("Used {} (+{} HP)", def->display_name,
                                                       int(std::round(client->health - before))));
                }
            }
        }
    } else if (std::holds_alternative<net::Goodbye>(message)) {
        say(std::format("{} (entity {}) left", client->name, client->entity));
        save({client, 1});
        clients_.erase(clients_.begin() + (client - clients_.data()));
    }
}

GameServer::Client* GameServer::join(const net::Endpoint& from, const std::string& name, uint64_t ticket,
                                     int team) {
    const auto refuse = [&](const std::string& reason) {
        say(std::format("refused {} from {}: {}", name, from.to_string(), reason));
        send(from, net::Refused{reason});
        return nullptr;
    };
    if (name.empty() || name.size() > 32) {
        return refuse("names are 1 to 32 characters");
    }
    if (std::any_of(clients_.begin(), clients_.end(), [&](const Client& c) { return c.name == name; })) {
        return refuse(name + " is already playing");
    }
    std::optional<int> admitted_team = -1;
    if (admit) {
        admitted_team = admit(name, ticket, team);
        if (!admitted_team) {
            return refuse("this match only admits players sent by its hub");
        }
    }
    Client joined;
    joined.endpoint = from;
    joined.name = name;
    joined.entity = next_entity_++;

    if (conquest_) {
        // The smaller team; then wait for the client to choose a spawn.
        std::array<int, 3> sizes{};
        for (const Client& c : clients_) {
            if (c.team >= 0 && c.team <= natives_team) {
                ++sizes[size_t(c.team)];
            }
        }
        joined.team = *admitted_team >= 0 ? std::min(*admitted_team, 1) : sizes[1] < sizes[0] ? 1 : 0;
        // A native (a bot asking), while the battlefield has room for one.
        const auto& natives = conquest_->battlefield().natives;
        if (team == natives_team && natives && sizes[natives_team] < natives->count) {
            joined.team = natives_team;
        }
        joined.life = Life::waiting;
        const Battlefield& field = conquest_->battlefield();
        Vec3 view = field.center();
        for (const CommandPost& post : field.posts) {
            if (post.owner == joined.team) {
                view = post.position;
                break;
            }
        }
        view = world_.clamp(view);
        view.y = world_.terrain().height_at(view.x, view.z);
        joined.state.position = view;
        joined.stats = stats_for(loadout_of(joined), joined.species, config_.base_stats);
        reset_loadout(joined.state, loadout_of(joined), joined.stats);
        clients_.push_back(joined);
        say(std::format("{} joined from {} as entity {} on team {}", name, from.to_string(), joined.entity,
                        field.team(joined.team).name));
        return &clients_.back();
    }

    // Side by side with whoever's already in (by players, not entity ids:
    // a hub's people take ids too, which put a lone player meters off the
    // spawn point, off a shuttleport's ticket counter).
    Vec3 spawn = spawn_point(uint32_t(clients_.size()));
    std::string where = "new";
    if (store_ != nullptr) {
        try {
            if (auto saved = store_->find_by_name(name)) {
                joined.character = saved->id;
                joined.state.yaw = saved->yaw;
                joined.species = species_from_name(saved->species).value_or(Species::human);
                joined.gender = gender_from_name(saved->gender).value_or(Gender::male);
                if (!saved->location.empty()) {
                    joined.location = saved->location;
                }
                if (!saved->appearance.empty()) {
                    joined.appearance = saved->appearance;
                }
                spawn = world_.clamp(saved->position);
                where = "returning";
            } else {
                joined.character = store_->create(name, spawn, 0).id;
            }
        } catch (const std::exception& error) {
            say(std::format("storage error: {}", error.what()));
            return refuse("the server can't load characters right now");
        }
    }
    // Stand on whatever is under the spawn point, near its height: a building
    // floor, or the terrain. A saved position may have been mid-jump.
    if (store_ != nullptr && config_.catalog != nullptr && joined.character != 0) {
        try {
            ensure_starter_kit(*store_, joined.character);
            if (auto refreshed = store_->find(joined.character)) {
                joined.credits = refreshed->credits;
                joined.faction_points = refreshed->faction_points;
                joined.experience = refreshed->experience;
                joined.loadout = size_t(std::max(0, refreshed->active_loadout));
            }
            joined.items = store_->list_items(joined.character);
            joined.reputation = store_->list_reputation(joined.character);
            for (const std::string_view pf : playable_factions) {
                joined.reputation.try_emplace(std::string(pf), 0);
            }
            joined.char_loadouts = store_->list_loadouts(joined.character);
            rebuild_client_loadouts(joined);
            joined.inventory_rev = 1;
            joined.quests = store_->list_quests(joined.character);
            joined.quest_rev = 1;
            joined.professions = store_->list_professions(joined.character);
            joined.profession_rev = 1;
            load_services_data(joined);
            load_specialist_data(joined);  // Stage 12: before the loadouts are rebuilt below
            joined.materials = store_->list_materials(joined.character);
            joined.presets = store_->list_presets(joined.character);
            joined.crafting_rev = 1;
            rebuild_client_loadouts(joined);  // with their professions' perks
        } catch (const std::exception& error) {
            say(std::format("inventory load error for {}: {}", name, error.what()));
        }
    }
    // Off a starship from another planet (Stage 13): at its starport.
    if (const auto arrival = arrivals_.find(name); arrival != arrivals_.end()) {
        spawn = world_.clamp(arrival->second);
        // A height below the world: unknown (the ground there, then).
        spawn.y = (arrival->second.y < -1e8 ? world_.terrain().height_at(spawn.x, spawn.z) : arrival->second.y) + 1.0;
        arrivals_.erase(arrival);
        where = "arriving";
    }
    const Loadout& initial_lo = loadout_of(joined);
    joined.stats = stats_for(initial_lo, joined.species, config_.base_stats);
    spawn.y = world_.ground_at(spawn.x, spawn.z, spawn.y, joined.stats.move.step_up).height;
    if (spawn.y < -1e8) {
        // Nothing under it from there (inside a building, over its rooms'
        // terrain holes): from over the ground, else the ground itself.
        const double terrain = world_.terrain().height_at(spawn.x, spawn.z);
        spawn.y = world_.ground_at(spawn.x, spawn.z, terrain + 2.0, joined.stats.move.step_up).height;
        if (spawn.y < -1e8) {
            spawn.y = terrain;
        }
    }
    joined.state.position = spawn;
    reset_loadout(joined.state, initial_lo, joined.stats);
    joined.health = joined.stats.max_health;
    joined.shield = joined.stats.shield_capacity;
    clients_.push_back(joined);
    say(std::format("{} joined from {} as entity {} ({} character {}) at ({:.2f}, {:.2f}, {:.2f})", name,
                    from.to_string(), joined.entity, where, joined.character, spawn.x, spawn.y, spawn.z));
    // Stage 13: characters are kept on their planet. One saved on another
    // is sent there (keeping that location); everyone else is here now.
    if (!config_.planet.empty()) {
        Client& here = clients_.back();
        const bool elsewhere = where != "arriving" && here.location != config_.planet && on_wrong_planet &&
                               on_wrong_planet(here.entity, here.location);
        if (!elsewhere) {
            here.location = config_.planet;
        }
    }
    return &clients_.back();
}

void GameServer::save_all() {
    save(clients_);
}

void GameServer::save(std::span<const Client> clients) {
    if (store_ == nullptr || clients.empty()) {
        return;
    }
    std::vector<CharacterRecord> records;
    for (const Client& client : clients) {
        records.push_back({client.character, client.name, client.state.position, client.state.yaw,
                           species_def(client.species).id, client.gender == Gender::female ? "female" : "male",
                           client.location, client.appearance});
    }
    try {
        store_->save(records);
    } catch (const std::exception& error) {
        say(std::format("storage error, {} character(s) not saved: {}", records.size(), error.what()));
    }
    for (const Client& client : clients) {
        if (client.professions_dirty && client.character != 0) {
            try {
                store_->save_professions(client.character, client.professions);
                client.professions_dirty = false;
            } catch (const std::exception& error) {
                say(std::format("profession save error for {}: {}", client.name, error.what()));
            }
        }
        save_specialist_data(client);  // Stage 12
    }
}

void GameServer::step() {
    const auto tick_started = std::chrono::steady_clock::now();
    const double dt = 1 / config_.tick_rate;
    for (Client& client : clients_) {
        for (size_t i = 0; i < config_.max_inputs_per_tick && !client.queue.empty(); ++i) {
            const MoveInput input = client.queue.front();
            client.queue.pop_front();
            client.last_input = input.sequence;
            if (client.life == Life::waiting) {
                continue;  // not in the world
            }
            const Battlefield* field = conquest_ ? &conquest_->battlefield() : nullptr;
            const auto shot = simulate_character(client.state, input, client.life == Life::alive, world_,
                                                 loadout_of(client), client.stats, dt, vehicle_classes(), field);
            if (shot) {
                fire(client, *shot, input.view_tick);
            }
            // The use button (a press): into a vehicle or out of it.
            const bool use = input.held(MoveInput::use);
            if (use && !client.use_held && client.life == Life::alive && !vehicle_classes().empty()) {
                use_vehicle(client);
            }
            client.use_held = use;
        }
    }
    // Where the players in the world are, for what's near them (Stage 13).
    player_grid_.clear();
    for (const Client& client : clients_) {
        if (client.life != Life::waiting) {
            player_grid_.insert(client.entity, client.state.position);
        }
    }
    // The server's own people, creatures, and bandits, by how they're steered.
    // Outside a match, those no player is near stand still, unsimulated.
    const bool freeze_far = !conquest_ && config_.npc_sim_radius > 0;
    if (!npcs_.empty()) {
        const CharacterStats stats = stats_for(config_.loadout, config_.base_stats);
        CharacterStats own = stats;
        for (Npc& npc : npcs_) {
            if (npc.hidden) {
                continue;
            }
            if (npc.actor_kind >= 2 && npc.life != Life::alive) {
                continue;
            }
            if (freeze_far && !player_grid_.any_near(npc.state.position, config_.npc_sim_radius)) {
                ++perf_.npc_frozen;
                continue;
            }
            ++perf_.npc_ticks;
            if (npc.actor_kind >= 2) {
                own = stats_for(npc.loadout, config_.base_stats);
                own.move.radius = npc.radius > 0 ? npc.radius : stats.move.radius;
                own.move.run_speed = (npc.input.buttons & MoveInput::sprint) ? npc.run_speed : npc.walk_speed;
                const Loadout& npc_lo = npc.actor_kind == 3 ? npc.loadout : config_.loadout;
                const auto shot = simulate_character(npc.state, npc.input, true, world_, npc_lo, own, dt);
                if (shot && npc.actor_kind == 3) {
                    fire_npc(npc, *shot);
                }
            } else {
                own = stats;
                own.move.radius = npc.radius > 0 ? npc.radius : stats.move.radius;
                simulate_character(npc.state, npc.input, true, world_, config_.loadout, own, dt);
            }
        }
    }
    // Shooting-range targets: patrol and respawn.
    for (RangeTarget& target : targets_) {
        if (!target.alive) {
            target.respawn_timer -= dt;
            if (target.respawn_timer <= 0) {
                target.alive = true;
                target.health = target.max_health;
            }
            continue;
        }
        const double dist = (target.patrol_end - target.home).length();
        if (target.speed > 0 && dist > 1e-3) {
            target.phase += target.speed * dt / dist;
            const double cycle = std::fmod(target.phase, 2.0);
            const double alpha = cycle <= 1.0 ? cycle : 2.0 - cycle;
            target.position = target.home + (target.patrol_end - target.home) * alpha;
            const double gy = world_.ground_at(target.position.x, target.position.z, target.position.y + 1.0, 0.5).height;
            if (gy > -1e8) target.position.y = gy;
        }
    }
    // World loadout switching channels.
    if (!conquest_) {
        for (Client& client : clients_) {
            if (client.switching_to >= 0) {
                if (client.life != Life::alive || client.switching_to >= int(client.custom_loadouts.size())) {
                    client.switching_to = -1;
                    client.switch_remaining = 0.0;
                } else {
                    client.switch_remaining -= dt;
                    if (client.switch_remaining <= 0.0) {
                        sync_client_ammo(client, true);
                        client.loadout = size_t(client.switching_to);
                        client.switching_to = -1;
                        client.switch_remaining = 0.0;
                        client.stats = stats_for(loadout_of(client), client.species, config_.base_stats);
                        client.health = std::min(client.health, client.stats.max_health);
                        client.shield = client.stats.shield_capacity;
                        client.shield_recharge_timer = 0;
                        client.shield_disrupt_timer = 0;
                        reset_loadout(client.state, loadout_of(client), client.stats);
                        client.synced_std_ammo = remaining_unified_ammo(client.state.combat, loadout_of(client), AmmoClass::standard);
                        client.synced_hvy_ammo = remaining_unified_ammo(client.state.combat, loadout_of(client), AmmoClass::heavy);
                        if (store_ != nullptr && client.character != 0) {
                            try {
                                store_->update_economy(client.character, client.credits, client.faction_points,
                                                       client.experience, int(client.loadout));
                            } catch (const std::exception&) {
                            }
                        }
                        notify(client.entity, "Switched to " + loadout_of(client).name);
                    }
                }
            }
        }
    }
    ++tick_;

    for (Client& client : clients_) {
        if (client.life != Life::alive) {
            continue;
        }
        client.shield_recharge_timer = std::max(0.0, client.shield_recharge_timer - dt);
        client.shield_disrupt_timer = std::max(0.0, client.shield_disrupt_timer - dt);
        if (client.shield_recharge_timer <= 0 && client.shield_disrupt_timer <= 0 &&
            client.shield < client.stats.shield_capacity && client.stats.shield_recharge > 0) {
            client.shield = std::min(client.stats.shield_capacity, client.shield + client.stats.shield_recharge * dt);
        }
        if (!conquest_) {
            client.ammo_sync_timer += dt;
            sync_client_ammo(client, false);
            if (client.state.vehicle < 0) {
                if (const WeaponDef* sec = weapon_in_slot(loadout_of(client), secondary_slot); sec && sec->ammo > 0) {
                    const int clip = std::max(1, sec->rounds_per_clip);
                    const int full = sec->ammo * clip;
                    WeaponState& sw = client.state.combat.weapons[secondary_slot];
                    int cur = std::max<int>(0, sw.rounds) + std::max<int>(0, sw.reserve);
                    if (cur < full) {
                        client.secondary_recharge_timer += dt;
                        while (client.secondary_recharge_timer >= 20.0 && cur < full) {
                            client.secondary_recharge_timer -= 20.0;
                            if (sw.rounds < clip) ++sw.rounds;
                            else sw.reserve = int16_t(std::max<int>(0, sw.reserve) + 1);
                            ++cur;
                        }
                    } else {
                        client.secondary_recharge_timer = 0.0;
                    }
                }
            }
        }
    }

    // Where everyone is this tick, kept for a second for rewinding.
    const size_t keep = size_t(config_.tick_rate) + 2;
    for (Client& client : clients_) {
        client.history.emplace_back(tick_, client.state.position);
        while (client.history.size() > keep) {
            client.history.pop_front();
        }
    }
    update_vehicles(1 / config_.tick_rate);
    update_tow_cables(1 / config_.tick_rate);  // Stage 11
    advance_bolts();
    if (conquest_) {
        run_match();
    } else {
        for (Client& client : clients_) {
            if (client.life == Life::dead && tick_ >= client.respawn_tick) {
                respawn(client);
            }
        }
    }
    advance_pickups();
    if (tick_ % 15 == 0) {
        check_quest_positions();
    }
    update_extractions(now_);
    update_specialists(1 / config_.tick_rate);

    net::Snapshot base_snapshot;
    base_snapshot.server_tick = tick_;
    // Every entity described once, with and without its strings (Stage 13:
    // a client is sent each entity's strings only when it may not have them).
    struct Described {
        net::EntityState full;
        net::EntityState lean;  // without its strings
    };
    const auto described = [](net::EntityState entity) {
        entity.static_key = net::static_key(entity);
        entity.has_static = true;
        Described out{entity, entity};
        out.lean.has_static = false;
        out.lean.name.clear();
        out.lean.appearance.clear();
        out.lean.weapon_model.clear();
        out.lean.anim_set.clear();
        out.lean.worn.clear();
        return out;
    };
    std::vector<Described> player_entities;
    player_entities.reserve(clients_.size());
    for (const Client& client : clients_) {
        player_entities.push_back(described(describe(client)));
    }
    const auto make_npc_entity = [&](const Npc& npc) {
        net::EntityState entity;
        entity.id = npc.entity;
        entity.state = npc.state;
        entity.health = float(npc.actor_kind >= 2 ? npc.health : config_.base_stats.max_health);
        entity.max_health = float(npc.actor_kind >= 2 ? npc.max_health : config_.base_stats.max_health);
        entity.life = npc.life;
        entity.spawn_count = npc.spawn_count;
        entity.team = -1;
        entity.look = npc.look;
        entity.pose = npc.pose;
        entity.actor_kind = npc.actor_kind;
        entity.passive = npc.passive;
        entity.colors = npc.colors;
        if (npc.actor_kind >= 2) {
            entity.name = npc.name;
        }
        if (npc.actor_kind == 3) {
            entity.weapon_model = npc.weapon_model;
            entity.anim_set = npc.anim_set;
        }
        return entity;
    };
    std::vector<Described> npc_entities;
    npc_entities.reserve(npcs_.size());
    npc_grid_.clear();
    for (const Npc& npc : npcs_) {
        if (!npc.hidden) {
            npc_grid_.insert(uint32_t(npc_entities.size()), npc.state.position);
            npc_entities.push_back(described(make_npc_entity(npc)));
        }
    }
    for (const Bolt& bolt : bolts_) {
        base_snapshot.bolts.push_back({bolt.id, bolt.owner, bolt.weapon, bolt.def.projectile, bolt.position, bolt.velocity});
    }
    base_snapshot.events = std::move(events_);
    events_.clear();
    base_snapshot.explosions = std::move(explosions_);
    explosions_.clear();
    base_snapshot.impacts = std::move(impacts_);
    impacts_.clear();
    for (const Pickup& pickup : pickups_) {
        base_snapshot.pickups.push_back({pickup.id, pickup.kind, pickup.position, float(pickup.life)});
    }
    for (const RangeTarget& t : targets_) {
        base_snapshot.targets.push_back({t.entity, t.name, t.position, float(t.health), float(t.max_health), t.alive});
    }
    for (const Vehicle& v : vehicles_) {
        base_snapshot.vehicles.push_back(describe(v));
    }
    if (conquest_) {
        base_snapshot.match = conquest_->state();
    }
    for (Client& client : clients_) {
        client.status.credits = client.credits;
        client.status.faction_points = client.faction_points;
        client.status.experience = client.experience;
        client.status.active_loadout = uint8_t(client.loadout);
        client.status.switching_loadout = int8_t(client.switching_to);
        client.status.switch_progress =
            client.switching_to >= 0
                ? float(std::clamp(1.0 - client.switch_remaining / loadout_switch_time, 0.0, 1.0))
                : 0.0f;
        client.status.extract_progress =
            client.extract_started >= 0.0 && config_.extract_time > 0.0
                ? float(std::clamp((now_ - client.extract_started) / config_.extract_time, 0.0, 1.0))
                : -1.0f;
        const uint8_t new_combat_level = uint8_t(std::clamp(combat_level_of(client), 1, 100));
        if (new_combat_level != client.status.combat_level) {
            client.status.combat_level = new_combat_level;
            ++client.quest_rev;
            client.quest_sync_tick = 0;
        }
        client.status.inventory_rev = client.inventory_rev;
        net::Snapshot client_snapshot = base_snapshot;
        // Interest management (Stage 13): outside a match, only what's near.
        {
            const bool open_world = !conquest_;
            const Vec3 at = client.state.position;
            std::set<uint32_t> seen;
            const auto d2_to = [&](Vec3 p) {
                const double dx = p.x - at.x, dz = p.z - at.z;
                return dx * dx + dz * dz;
            };
            // Within radius, or kept while within its margin once sent.
            const auto in_view = [&](uint32_t id, double d2, double radius) {
                if (!open_world || radius <= 0 || d2 <= radius * radius) {
                    return true;
                }
                const double outer = radius + config_.view_margin;
                return d2 <= outer * outer && client.seen_entities.contains(id);
            };
            const auto add = [&](const Described& e) {
                if (e.full.id != client.entity && hidden_from(client.entity, e.full.id)) {
                    return;  // cloaked or smoked from this client (Stage 12)
                }
                auto [it, fresh] = client.sent_static.try_emplace(e.full.id);
                Client::SentStatic& sent = it->second;
                bool full = false;
                if (fresh || sent.key != e.full.static_key) {
                    sent = {e.full.static_key, tick_, tick_};
                    full = true;
                } else if (tick_ - sent.since < 8 || tick_ - sent.last_full >= 90) {
                    // Again for the first few ticks (in case one's lost), then now and then.
                    sent.last_full = tick_;
                    full = true;
                }
                client_snapshot.entities.push_back(full ? e.full : e.lean);
                // Only its own in full: everyone else's as drawn.
                client_snapshot.entities.back().full_state = e.full.id == client.entity;
                seen.insert(e.full.id);
                ++perf_.entities_sent;
                perf_.static_sent += full ? 1 : 0;
            };
            for (const Described& e : player_entities) {
                if (e.full.id == client.entity || in_view(e.full.id, d2_to(e.full.state.position), config_.player_view_radius)) {
                    add(e);
                }
            }
            if (!open_world || config_.npc_view_radius <= 0) {
                for (const Described& e : npc_entities) {
                    add(e);
                }
            } else {
                npc_grid_.for_each_near(at, config_.npc_view_radius + config_.view_margin,
                                        [&](uint32_t index, Vec3, double d2) {
                                            const Described& e = npc_entities[index];
                                            if (in_view(e.full.id, d2, config_.npc_view_radius)) {
                                                add(e);
                                            }
                                        });
            }
            std::erase_if(client.sent_static, [&](const auto& entry) { return !seen.contains(entry.first); });
            client.seen_entities = std::move(seen);
            if (open_world && config_.effect_view_radius > 0) {
                const double r2 = config_.effect_view_radius * config_.effect_view_radius;
                std::erase_if(client_snapshot.bolts, [&](const net::BoltState& b) {
                    return b.owner != client.entity && d2_to(b.position) > r2;
                });
                std::erase_if(client_snapshot.explosions, [&](const net::Explosion& x) {
                    return x.owner != client.entity && d2_to(x.position) > r2;
                });
                std::erase_if(client_snapshot.impacts, [&](const net::Impact& x) {
                    return x.owner != client.entity && d2_to(x.position) > r2;
                });
            }
        }
        if (!corpse_loots_.empty()) {
            constexpr double corpse_range2 = 45.0 * 45.0;
            std::vector<std::pair<double, size_t>> nearby;
            for (size_t ci = 0; ci < corpse_loots_.size(); ++ci) {
                const CorpseLoot& c = corpse_loots_[ci];
                if (c.items.empty()) continue;
                if (c.owner_entity != 0 && c.owner_entity != client.entity) continue;
                const double dx = c.position.x - client.state.position.x;
                const double dz = c.position.z - client.state.position.z;
                const double d2 = dx * dx + dz * dz;
                if (d2 <= corpse_range2) {
                    nearby.push_back({d2, ci});
                }
            }
            if (nearby.size() > 8) {
                std::partial_sort(nearby.begin(), nearby.begin() + 8, nearby.end());
                nearby.resize(8);
            }
            for (const auto& [_, ci] : nearby) {
                const CorpseLoot& c = corpse_loots_[ci];
                client_snapshot.corpses.push_back({c.npc_entity, c.name, c.position, c.items});
            }
        }
        client_snapshot.last_input = client.last_input;
        client_snapshot.field = field_state_for(client.entity);
        client_snapshot.you = client.status;
        {
            const std::vector<uint8_t> bytes = net::encode(net::Message{std::move(client_snapshot)});
            ++perf_.snapshots;
            perf_.snapshot_bytes += bytes.size();
            perf_.largest_snapshot = std::max(perf_.largest_snapshot, bytes.size());
            link_.send(client.endpoint, bytes);
        }
        if (client.inventory_rev != client.acked_inventory_rev &&
            (client.last_sync_tick == 0 || tick_ - client.last_sync_tick >= 8)) {
            send_inventory_sync(client);
        }
        if (quest_book_ != nullptr && client.quest_rev != client.acked_quest_rev &&
            (client.quest_sync_tick == 0 || tick_ - client.quest_sync_tick >= 8)) {
            send_quest_sync(client);
        }
        if (config_.professions != nullptr && client.profession_rev != client.acked_profession_rev &&
            (client.profession_sync_tick == 0 || tick_ - client.profession_sync_tick >= 8)) {
            send_profession_sync(client);
        }
        if (client.crafting_rev != client.acked_crafting_rev &&
            (client.crafting_sync_tick == 0 || tick_ - client.crafting_sync_tick >= 8)) {
            send_crafting_sync(client);
        }
        if (client.transfer) {
            send(client.endpoint, *client.transfer);
        }
    }
    // Transferred clients that never said goodbye.
    std::erase_if(clients_, [&](const Client& client) {
        const bool gone = client.transfer && tick_ >= client.transfer_tick;
        if (gone) {
            say(std::format("{} (entity {}) transferred", client.name, client.entity));
            save({&client, 1});
        }
        return gone;
    });
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - tick_started).count();
    ++perf_.ticks;
    perf_.tick_seconds += took;
    perf_.worst_tick = std::max(perf_.worst_tick, took);
}

// How far below the lowest terrain a falling soldier dies.
constexpr double fall_depth = 50;

void GameServer::run_match() {
    const double dt = 1 / config_.tick_rate;
    const Battlefield& field = conquest_->battlefield();
    for (Client& client : clients_) {
        if (client.life == Life::dead && tick_ >= client.respawn_tick) {
            return_to_waiting(client);
        }
        // So do death regions, at once, and falling through a hole in the
        // terrain with nothing below (flyers in space use min_fly_y instead).
        const auto classes = vehicle_classes();
        const bool in_flyer = client.state.vehicle >= 0 && size_t(client.state.vehicle) < vehicles_.size() &&
                              size_t(vehicles_[size_t(client.state.vehicle)].cls) < classes.size() &&
                              classes[size_t(vehicles_[size_t(client.state.vehicle)].cls)].kind == VehicleKind::flyer;
        const double death_y =
            (in_flyer && field.is_space()) ? field.min_fly_y - 50.0 : world_.terrain().lowest() - fall_depth;
        if (client.life == Life::alive &&
            (conquest_->deadly(client.state.position, !in_flyer) || client.state.position.y < death_y)) {
            kill(client, 0);
            continue;
        }
        // Damage regions hurt so much a second, armor or not (Mustafar's
        // lava, at once).
        if (client.life == Life::alive) {
            if (const double rate = field.damage_rate(client.state.position); rate > 0) {
                client.health = std::max(0.0, client.health - rate * dt);
                if (client.health <= 0) {
                    kill(client, 0);
                    continue;
                }
            }
        }
        // Health and ammo droids: every so often (each its own delay), to
        // whoever stands by them (while the droid is intact).
        if (client.life == Life::alive) {
            for (size_t i = 0; i < field.powerups.size(); ++i) {
                if (!conquest_->powerup_alive(i)) {
                    continue;
                }
                const PowerupStation& station = field.powerups[i];
                const double now = tick_ * dt / station.delay;
                if (std::floor(now) == std::floor(now - dt / station.delay) || !station.reaches(client.state.position)) {
                    continue;
                }
                client.health = std::min(client.stats.max_health, client.health + station.health);
                resupply(client.state, loadout_of(client), station.ammo);
            }
        }
        // Leaving the battlefield kills, after a warning.
        if (client.life == Life::alive && !field.contains(client.state.position)) {
            client.outside += dt;
            if (client.outside >= field.out_of_bounds_time) {
                kill(client, 0);
            }
        } else {
            client.outside = 0;
        }
    }

    std::vector<Conquest::Unit> units;
    for (const Client& client : clients_) {
        if (client.life == Life::alive) {
            units.push_back({client.team, client.state.position, client.entity, client.state.vehicle < 0});
            // Stage 12: a slotted Smuggler slices command posts as fast as two
            // (a second unit that can't carry a flag).
            if (client.state.vehicle < 0 && specialist_level(client, "smuggler") > 0) {
                units.push_back({client.team, client.state.position, 0, false});
            }
        }
    }
    if (conquest_->state().phase == Phase::live) {
        match_live_time_ += dt;
    }
    const auto match_events = conquest_->step(dt, units, clients_.size());
    for (size_t i = 0; i < field.destructibles.size(); ++i) {
        world_.set_destroyed(i, !conquest_->destructible_alive(i));
    }
    for (const MatchEvent& event : match_events) {
        switch (event.kind) {
        case MatchEvent::went_live:
            say("the match is live");
            bolts_.clear();
            pickups_.clear();
            match_live_time_ = 0;
            match_captures_ = 0;
            for (Client& client : clients_) {
                client.kills = client.deaths = client.captures = 0;
                client.weapon_kills.clear();
                client.battle_profession_xp.clear();
                return_to_waiting(client);
            }
            break;
        case MatchEvent::restarted:
            say("warmup for the next match");
            pickups_.clear();
            for (Client& client : clients_) {
                return_to_waiting(client);
            }
            break;
        case MatchEvent::ended: {
            say(event.team >= 0 ? std::format("{} won", field.teams[size_t(event.team)].name)
                                : std::string("the match is a draw"));
            MatchResult result{field.name, event.team, {}};
            result.mode = field.mode;
            result.teams = {field.teams[0].name, field.teams[1].name};
            result.tickets = conquest_->state().tickets;
            result.scores = conquest_->state().scores;
            result.duration = match_live_time_;
            result.captures = match_captures_;
            for (const Client& client : clients_) {
                say(std::format("  {:<16} {:<12} {} kills, {} deaths, {} captures", client.name,
                                field.team(client.team).name, client.kills, client.deaths, client.captures));
                const Loadout& lo = loadout_of(client);
                result.players.push_back({client.name, client.team, client.kills, client.deaths, client.captures,
                                          client.battle_profession_xp,
                                          remaining_unified_ammo(client.state.combat, lo, AmmoClass::standard),
                                          remaining_unified_ammo(client.state.combat, lo, AmmoClass::heavy)});
                if (const WeaponDef* primary = weapon_in_slot(lo, 0)) {
                    result.players.back().primary_weapon = primary->name;
                }
                result.players.back().weapon_kills = client.weapon_kills;
            }
            if (on_match_ended) {
                on_match_ended(result);
            }
            break;
        }
        case MatchEvent::captured:
        case MatchEvent::neutralized: {
            const CommandPost& post = field.posts[size_t(event.post)];
            for (Client& client : clients_) {
                if (client.team == event.team && client.life == Life::alive && post.contains(client.state.position)) {
                    ++client.captures;
                }
            }
            events_.push_back({event.kind == MatchEvent::captured ? net::CombatEvent::captured
                                                                   : net::CombatEvent::neutralized,
                               uint32_t(event.team), uint32_t(event.post), 0});
            say(std::format("{} {} {}", field.teams[size_t(event.team)].name,
                            event.kind == MatchEvent::captured ? "captured" : "neutralized", post.name));
            ++match_captures_;
            break;
        }
        case MatchEvent::flag_taken:
        case MatchEvent::flag_dropped:
        case MatchEvent::flag_returned:
        case MatchEvent::flag_captured: {
            // CombatEvent: attacker = the soldier (0: none), victim = the flag, damage = the team.
            const auto kind = event.kind == MatchEvent::flag_taken      ? net::CombatEvent::flag_taken
                              : event.kind == MatchEvent::flag_dropped  ? net::CombatEvent::flag_dropped
                              : event.kind == MatchEvent::flag_returned ? net::CombatEvent::flag_returned
                                                                        : net::CombatEvent::flag_captured;
            events_.push_back({kind, event.entity, uint32_t(event.post), float(event.team)});
            if (event.kind == MatchEvent::flag_captured) {
                ++match_captures_;
                if (Client* carrier = find(event.entity)) {
                    ++carrier->captures;
                }
            }
            if (event.team >= 0 && event.team < 2) {
                const char* what = event.kind == MatchEvent::flag_taken     ? "took a flag"
                                   : event.kind == MatchEvent::flag_dropped ? "dropped a flag"
                                   : event.kind == MatchEvent::flag_returned ? "returned their flag"
                                                                             : "captured a flag";
                say(std::format("{} {}", field.teams[size_t(event.team)].name, what));
            }
            break;
        }
        }
    }

    for (Client& client : clients_) {
        if (client.life != Life::waiting || !client.wants_spawn) {
            continue;
        }
        const size_t loadout = client.wants_spawn->loadout;
        const size_t post = client.wants_spawn->post;
        const size_t avail_loadouts =
            client.custom_loadouts.empty() ? field.team(client.team).loadouts.size() : client.custom_loadouts.size();
        if (loadout < avail_loadouts && post == beacon_spawn_post && spawn_at_beacon(client, loadout)) {
            continue;  // Stage 12: at a squad spawn beacon
        }
        if (loadout < avail_loadouts && can_spawn_at(client.team, post)) {
            spawn_at_post(client, loadout, post);
        }
    }
}

void GameServer::return_to_waiting(Client& client) {
    client.life = Life::waiting;
    client.health = 0;
    client.shield = 0;
    client.outside = 0;
    client.wants_spawn.reset();
    client.state.velocity = {};
}

bool GameServer::is_mobile_spawn(const Vehicle& vehicle, int team, const Battlefield& field,
                                 std::span<const VehicleClass> classes) const {
    if (vehicle.dead_for >= 0 || vehicle.team < 0 || vehicle.team != team ||
        size_t(vehicle.cls) >= classes.size()) {
        return false;
    }
    const VehicleClass& cls = classes[size_t(vehicle.cls)];
    if (!cls.is_transport || cls.spawn_points.empty()) {
        return false;
    }
    if (vehicle.driver != 0) {
        return true;
    }
    if (vehicle.spawn >= 0 && size_t(vehicle.spawn) < field.vehicle_spawns.size()) {
        const Vec3 origin = field.vehicle_spawns[size_t(vehicle.spawn)].position;
        return (vehicle.state.position - origin).length() > 25.0;
    }
    return false;
}

bool GameServer::can_spawn_at(int team, size_t post) const {
    if (!conquest_) {
        return false;
    }
    const Battlefield& field = conquest_->battlefield();
    if (post < field.posts.size()) {
        return conquest_->can_spawn(team, post);
    }
    if (conquest_->state().phase == Phase::ended || team < 0 || team >= 2) {
        return false;
    }
    size_t idx = post - field.posts.size();
    const auto classes = vehicle_classes();
    for (const Vehicle& v : vehicles_) {
        if (is_mobile_spawn(v, team, field, classes)) {
            if (idx-- == 0) {
                return true;
            }
        }
    }
    return false;
}

void GameServer::spawn_at_post(Client& client, size_t loadout_index, size_t post_index) {
    const Battlefield& field = conquest_->battlefield();
    const uint32_t slot = next_spawn_slot_++;
    Vec3 position;
    double face_yaw = 0;
    bool exact_height = false;
    double from_y = 0;
    std::string spawn_name;
    if (post_index >= field.posts.size()) {
        // Mobile transport spawn (commandflyer spawn points).
        size_t wanted = post_index - field.posts.size();
        const Vehicle* transport = nullptr;
        const auto classes = vehicle_classes();
        for (const Vehicle& v : vehicles_) {
            if (is_mobile_spawn(v, client.team, field, classes)) {
                if (wanted-- == 0) {
                    transport = &v;
                    break;
                }
            }
        }
        if (transport == nullptr) {
            return;
        }
        const VehicleClass& cls = classes[size_t(transport->cls)];
        spawn_name = cls.label;
        const Vec3 local = cls.spawn_points[slot % cls.spawn_points.size()];
        position = vehicle_to_world(transport->state.position, transport->state.yaw, local);
        face_yaw = transport->state.yaw;
        exact_height = !transport->state.grounded;
        from_y = position.y;
    } else {
        const CommandPost& post = field.posts[post_index];
        spawn_name = post.name;
        // At the post's spawn points in turn, or spread around it (golden-angle
        // steps); facing away from it.
        const double angle = double(slot) * 2.399963;
        // Natives at its ally spawn points, if it has them.
        const bool ally = client.team == natives_team && !post.ally_spawn_points.empty();
        const auto& points = ally ? post.ally_spawn_points : post.spawn_points;
        position = points.empty() ? post.position + Vec3{std::cos(angle), 0, std::sin(angle)} * post.spawn_radius
                                  : points[slot % points.size()];
        position.x = std::clamp(position.x, field.min_x, field.max_x);
        position.z = std::clamp(position.z, field.min_z, field.max_z);
        position = world_.clamp(position);
        // On the surface at the spawn point's height, if it has one; else on
        // the terrain (or what stands on it).
        from_y = ally || (post.spawn_heights && !post.spawn_points.empty())
                     ? position.y
                     : world_.terrain().height_at(position.x, position.z);
        face_yaw = std::atan2(-(position.x - post.position.x), -(position.z - post.position.z));
    }
    if (conquest_ && client.spawn_count > 0 && !client.custom_loadouts.empty()) {
        const Loadout& prev = loadout_of(client);
        const int rem_std = remaining_unified_ammo(client.state.combat, prev, AmmoClass::standard);
        const int rem_hvy = remaining_unified_ammo(client.state.combat, prev, AmmoClass::heavy);
        for (Loadout& lo : client.custom_loadouts) {
            if (rem_std >= 0 && lo.unified_standard_ammo >= 0) lo.unified_standard_ammo = rem_std;
            if (rem_hvy >= 0 && lo.unified_heavy_ammo >= 0) lo.unified_heavy_ammo = rem_hvy;
        }
    }
    client.loadout = loadout_index;
    const Loadout& loadout = loadout_of(client);
    client.stats = stats_for(loadout, client.species, config_.base_stats);
    constexpr double spawn_lift = 1;
    if (!exact_height) {
        position.y = world_.support_at(position.x, position.z, from_y + spawn_lift, client.stats.move.step_up).height;
    }

    client.state = MoveState{};
    client.state.position = position;
    client.state.yaw = float(face_yaw);
    reset_loadout(client.state, loadout, client.stats);
    client.health = client.stats.max_health;
    client.shield = client.stats.shield_capacity;
    client.shield_recharge_timer = 0;
    client.shield_disrupt_timer = 0;
    client.life = Life::alive;
    client.outside = 0;
    client.history.clear();
    client.wants_spawn.reset();
    ++client.spawn_count;
    say(std::format("{} spawned with {} at {}", client.name, loadout.name, spawn_name));
}

void GameServer::fire(Client& shooter, const Shot& shot, double view_tick) {
    break_cloak(shooter);  // Stage 12
    const auto classes = vehicle_classes();
    const bool driving = shooter.state.vehicle >= 0 && size_t(shooter.state.vehicle) < classes.size();
    const VehicleSeat* seat = driving ? vehicle_seat(classes[size_t(shooter.state.vehicle)], shooter.state.seat) : nullptr;
    if (seat != nullptr && seat->tow_lock_range > 0) {
        fire_tow_cable(shooter, *seat);  // Stage 11: a harpoon, not a bolt
        return;
    }
    const WeaponDef* def = weapon_in_slot(seat       ? seat->loadout
                                          : driving ? classes[size_t(shooter.state.vehicle)].loadout
                                                    : loadout_of(shooter),
                                          shot.weapon);
    if (def == nullptr) {
        return;
    }
    const WeaponDef& weapon = *def;
    // The tick being simulated is tick_ + 1; the shooter saw others at view_tick.
    const double rewind = std::clamp(double(tick_ + 1) - view_tick, 0.0, config_.max_rewind * config_.tick_rate);
    if (weapon.projectile == Projectile::melee) {
        const bool harmless = conquest_ && conquest_->state().phase == Phase::ended;
        const bool pvp_blocked = !conquest_ && !config_.pvp_enabled && shooter.team != hostile_npc_team;
        if (harmless) {
            return;
        }
        const Vec3 fwd{-std::sin(double(shooter.state.yaw)), 0, -std::cos(double(shooter.state.yaw))};
        const double cos_half = std::cos(weapon.melee_arc * std::numbers::pi / 180.0);
        const auto in_arc = [&](Vec3 target_pos, double radius, double& out_dist) {
            const Vec3 delta = target_pos - shooter.state.position;
            if (delta.y < -1.2 || delta.y > 2.0) return false;
            const double horiz = std::hypot(delta.x, delta.z);
            if (horiz > weapon.melee_range + radius) return false;
            if (horiz > 1e-4) {
                const double dot = (delta.x * fwd.x + delta.z * fwd.z) / horiz;
                if (dot < cos_half) return false;
            }
            out_dist = horiz;
            return !shot_hit(shot.origin, target_pos + Vec3{0, 1.0, 0});
        };
        if (!pvp_blocked) {
            for (Client& target : clients_) {
                const bool teammate = allied(shooter.team, target.team) && !config_.friendly_fire;
                if (target.entity == shooter.entity || target.life != Life::alive || teammate ||
                    target.state.vehicle >= 0) {
                    continue;
                }
                const Vec3 target_pos = position_at(target, double(tick_) - rewind);
                double dist = 0;
                if (!in_arc(target_pos, hit_radius, dist)) {
                    continue;
                }
                bool facing_block = false;
                if (target.state.blocking) {
                    const Vec3 to_shooter = shooter.state.position - target_pos;
                    const double len = std::hypot(to_shooter.x, to_shooter.z);
                    if (len > 1e-4) {
                        const Vec3 t_fwd{-std::sin(double(target.state.yaw)), 0, -std::cos(double(target.state.yaw))};
                        facing_block = (to_shooter.x * t_fwd.x + to_shooter.z * t_fwd.z) / len >= 0.0;
                    } else {
                        facing_block = true;
                    }
                }
                if (facing_block && target.state.parry_window > 0) {
                    damage(target, weapon.damage * 0.15, DamageType::melee, shooter.entity, 0.0);
                    shooter.state.stagger = std::max(shooter.state.stagger, 0.6f);
                } else if (facing_block) {
                    damage(target, weapon.damage * 0.45, DamageType::melee, shooter.entity, 0.0);
                    target.state.energy = std::max(0.0, target.state.energy - 15.0);
                } else {
                    damage(target, weapon.damage, DamageType::melee, shooter.entity, weapon.shield_disrupt);
                    target.state.stagger = std::max(target.state.stagger, float(weapon.melee_stagger));
                }
                impacts_.push_back({shooter.entity, uint8_t(shot.weapon), target_pos + Vec3{0, 1.1, 0}, {0, 1, 0}, true});
            }
        }
        if (shooter.team != hostile_npc_team) {
            for (Npc& npc : npcs_) {
                if (npc.hidden || npc.actor_kind < 2 || npc.life != Life::alive) continue;
                double dist = 0;
                if (in_arc(npc.state.position, npc.hit_radius, dist)) {
                    damage_npc(npc, weapon.damage, DamageType::melee, shooter.entity);
                    impacts_.push_back(
                        {shooter.entity, uint8_t(shot.weapon), npc.state.position + Vec3{0, 1.0, 0}, {0, 1, 0}, true});
                }
            }
        }
        for (RangeTarget& target : targets_) {
            if (!target.alive) continue;
            double dist = 0;
            if (in_arc(target.position, hit_radius, dist)) {
                damage_target(target, weapon.damage, DamageType::melee, shooter.entity);
                impacts_.push_back(
                    {shooter.entity, uint8_t(shot.weapon), target.position + Vec3{0, 1.0, 0}, {0, 1, 0}, true});
            }
        }
        if (conquest_) {
            std::vector<Blast> blasts;
            const auto& stations = conquest_->battlefield().powerups;
            for (size_t i = 0; i < stations.size(); ++i) {
                if (!conquest_->powerup_alive(i)) continue;
                double dist = 0;
                if (in_arc(stations[i].position, PowerupStation::hit_radius + 0.6, dist)) {
                    damage_powerup(i, weapon.damage, shooter.entity, blasts);
                }
            }
            const auto& objects = conquest_->battlefield().destructibles;
            for (size_t i = 0; i < objects.size(); ++i) {
                if (!conquest_->destructible_alive(i)) continue;
                double dist = 0;
                if (in_arc(objects[i].nearest(shooter.state.position), 1.0, dist)) {
                    damage_destructible(i, weapon.damage, shooter.entity, blasts);
                }
            }
            for (size_t i = 0; i < blasts.size(); ++i) {
                explode(blasts[i], blasts);
            }
        }
        return;
    }
    for (const Vec3& direction : shot.directions) {
        Bolt bolt;
        bolt.id = next_bolt_++;
        bolt.owner = shooter.entity;
        bolt.team = shooter.team;
        bolt.weapon = uint8_t(shot.weapon);
        bolt.position = shot.origin;
        bolt.def = weapon;
        bolt.velocity = direction * weapon.bolt_speed;
        // A grenade timed from where it lands flies at most this long first.
        constexpr double longest_flight = 10;
        const bool timed = weapon.projectile == Projectile::grenade && !weapon.explode_on_impact;
        bolt.life = !timed ? weapon.bolt_life : weapon.timer_on_contact ? longest_flight : weapon.fuse;
        bolt.gravity = weapon.bolt_gravity * shooter.stats.move.gravity;
        bolt.rewind = rewind;
        bolts_.push_back(bolt);
    }
}

void GameServer::fire_npc(const Npc& shooter, const Shot& shot) {
    const WeaponDef* def = weapon_in_slot(shooter.loadout, shot.weapon);
    if (def == nullptr) {
        return;
    }
    const WeaponDef& weapon = *def;
    for (const Vec3& direction : shot.directions) {
        Bolt bolt;
        bolt.id = next_bolt_++;
        bolt.owner = shooter.entity;
        bolt.team = hostile_npc_team;
        bolt.weapon = uint8_t(shot.weapon);
        bolt.position = shot.origin;
        bolt.def = weapon;
        bolt.velocity = direction * weapon.bolt_speed;
        constexpr double longest_flight = 10;
        const bool timed = weapon.projectile == Projectile::grenade && !weapon.explode_on_impact;
        bolt.life = !timed ? weapon.bolt_life : weapon.timer_on_contact ? longest_flight : weapon.fuse;
        bolt.gravity = weapon.bolt_gravity * config_.base_stats.move.gravity;
        bolt.rewind = 0;
        bolts_.push_back(bolt);
    }
}

bool GameServer::allied(int a, int b) const {
    return conquest_ ? conquest_->battlefield().allied(a, b) : a >= 0 && a == b;
}

std::optional<World::RayHit> GameServer::shot_hit(Vec3 a, Vec3 b) const {
    auto hit = world_.raycast_hit(a, b);
    if (conquest_) {
        if (const auto t = conquest_->door_hit(a, b); t && (!hit || *t < hit->t)) {
            const Vec3 d = b - a;
            const double length = d.length();
            hit = World::RayHit{*t, length > 1e-9 ? d * (-1.0 / length) : Vec3{0, 1, 0}};
        }
    }
    return hit;
}

Vec3 GameServer::position_at(const Client& client, double tick) const {
    const auto& h = client.history;
    if (h.empty() || tick >= h.back().first) {
        return client.state.position;
    }
    if (tick <= h.front().first) {
        return h.front().second;
    }
    for (size_t i = 1; i < h.size(); ++i) {
        if (tick <= h[i].first) {
            const auto& [t0, p0] = h[i - 1];
            const auto& [t1, p1] = h[i];
            const double f = (tick - t0) / double(t1 - t0);
            return p0 + (p1 - p0) * f;
        }
    }
    return client.state.position;
}

void GameServer::advance_bolts() {
    const double dt = 1 / config_.tick_rate;
    // After a match ends, bolts fly on but harm nobody.
    const bool harmless = conquest_ && conquest_->state().phase == Phase::ended;
    // Explosions damage the bolt list's owners, so collect them first.
    std::vector<Blast> blasts;
    std::erase_if(bolts_, [&](Bolt& bolt) {
        const WeaponDef& def = bolt.def;
        bolt.life -= dt;
        if (bolt.resting) {
            if (bolt.life <= 0) {
                blasts.push_back({bolt, bolt.position, {0, 1, 0}, false});
                return true;
            }
            return false;
        }
        const Vec3 from = bolt.position;
        bolt.velocity.y -= bolt.gravity * dt;
        if (def.projectile == Projectile::missile && def.bolt_acceleration > 0) {
            const double speed = bolt.velocity.length();
            if (speed > 0) {
                bolt.velocity += bolt.velocity * (def.bolt_acceleration * dt / speed);
            }
        }
        const Vec3 to = from + bolt.velocity * dt;
        // The first thing along the way: the world (shut doors too), someone
        // where the shooter saw them, or a living health/ammo droid.
        // Bouncing grenades pass people by.
        const auto world_hit = shot_hit(from, to);
        std::optional<double> nearest = world_hit ? std::optional(world_hit->t) : std::nullopt;
        Vec3 normal = world_hit ? world_hit->normal : Vec3{0, 1, 0};
        Client* victim = nullptr;
        Npc* hit_npc = nullptr;
        RangeTarget* hit_target = nullptr;
        double victim_top = 0;  // the top of the victim's body (its head's), when hit
        int hit_powerup = -1;
        const bool bounces = def.projectile == Projectile::grenade && !def.explode_on_impact;
        const bool pvp_blocked = !conquest_ && !config_.pvp_enabled && bolt.team != hostile_npc_team;
        for (Client& target : clients_) {
            const bool teammate = allied(bolt.team, target.team) && !config_.friendly_fire;
            if (bounces || harmless || (pvp_blocked && !bounty_pvp_allowed(bolt.owner, target.entity)) ||
                target.entity == bolt.owner || target.life != Life::alive || teammate ||
                target.state.vehicle >= 0) {
                continue;  // (a driver: its vehicle takes the hit)
            }
            // Its posture now (not rewound: ASSUMPTION, close enough).
            const Vec3 feet = position_at(target, double(tick_) - bolt.rewind);
            const double height = target.state.crouched ? crouched_hit_height : hit_height;
            const auto t = segment_hits_cylinder(from, to, feet, hit_radius, height);
            if (t && (!nearest || *t < *nearest)) {
                nearest = t;
                victim = &target;
                victim_top = feet.y + height;
                const double speed = bolt.velocity.length();
                normal = speed > 1e-6 ? bolt.velocity * (-1.0 / speed) : Vec3{0, 1, 0};
            }
        }
        if (!bounces && !harmless && bolt.team != hostile_npc_team) {
            for (Npc& npc : npcs_) {
                if (npc.hidden || npc.actor_kind < 2 || npc.life != Life::alive) continue;
                const double h = npc.state.crouched ? std::min(npc.hit_height, crouched_hit_height) : npc.hit_height;
                const auto t = segment_hits_cylinder(from, to, npc.state.position, npc.hit_radius, h);
                if (t && (!nearest || *t < *nearest)) {
                    nearest = t;
                    victim = nullptr;
                    hit_npc = &npc;
                    victim_top = npc.state.position.y + h;
                    const double speed = bolt.velocity.length();
                    normal = speed > 1e-6 ? bolt.velocity * (-1.0 / speed) : Vec3{0, 1, 0};
                }
            }
        }
        if (!bounces && !harmless) {
            for (RangeTarget& target : targets_) {
                if (!target.alive) continue;
                const auto t = segment_hits_cylinder(from, to, target.position, hit_radius, hit_height);
                if (t && (!nearest || *t < *nearest)) {
                    nearest = t;
                    victim = nullptr;
                    hit_npc = nullptr;
                    hit_target = &target;
                    victim_top = target.position.y + hit_height;
                    const double speed = bolt.velocity.length();
                    normal = speed > 1e-6 ? bolt.velocity * (-1.0 / speed) : Vec3{0, 1, 0};
                }
            }
        }
        if (conquest_ && !harmless && !bounces) {
            const auto& stations = conquest_->battlefield().powerups;
            for (size_t i = 0; i < stations.size(); ++i) {
                if (!conquest_->powerup_alive(i)) {
                    continue;
                }
                const auto t = segment_hits_cylinder(from, to, stations[i].position, PowerupStation::hit_radius,
                                                     PowerupStation::hit_height);
                if (t && (!nearest || *t < *nearest)) {
                    nearest = t;
                    victim = nullptr;
                    hit_powerup = int(i);
                    const double speed = bolt.velocity.length();
                    normal = speed > 1e-6 ? bolt.velocity * (-1.0 / speed) : Vec3{0, 1, 0};
                }
            }
            // A droid's own exported collision mesh may catch the ray slightly
            // ahead of its cylinder hitbox; attribute world hits inside its padded volume.
            if (victim == nullptr && hit_powerup < 0 && nearest) {
                const Vec3 at = from + (to - from) * *nearest;
                constexpr double pad_r = PowerupStation::hit_radius + 0.25;
                for (size_t i = 0; i < stations.size(); ++i) {
                    if (!conquest_->powerup_alive(i)) {
                        continue;
                    }
                    const Vec3 d = at - stations[i].position;
                    if (d.x * d.x + d.z * d.z <= pad_r * pad_r && d.y >= -0.25 &&
                        d.y <= PowerupStation::hit_height + 0.25) {
                        hit_powerup = int(i);
                        break;
                    }
                }
            }
        }
        Vehicle* hit_vehicle = nullptr;
        if (!bounces && !harmless) {
            const auto classes = vehicle_classes();
            // A turret set into a hull (a capital ship's, guarding its bay)
            // sits just behind the hull's own surface: it's hit by what
            // strikes the hull this near it, else hardly ever at all.
            const double length = std::max(1e-6, (to - from).length());
            const double sunk = 2.5 / length;  // (a share of this step)
            for (Vehicle& v : vehicles_) {
                if (v.dead_for >= 0 || (v.driver != 0 && v.driver == bolt.owner) ||
                    (allied(bolt.team, v.team) && !config_.friendly_fire)) {
                    continue;
                }
                const VehicleClass& cls = classes[size_t(v.cls)];
                const bool behind_hull = cls.kind == VehicleKind::turret && world_hit && nearest && world_hit->t == *nearest;
                std::optional<double> t;
                if (behind_hull) {
                    // Looked for a little past the step's end, too.
                    const Vec3 further = to + (to - from) * sunk;
                    if (const auto beyond = segment_hits_vehicle(from, further, v.state.position, v.state.yaw, cls)) {
                        t = *beyond * (1 + sunk);
                    }
                } else {
                    t = segment_hits_vehicle(from, to, v.state.position, v.state.yaw, cls);
                }
                if (t && (!nearest || *t < *nearest + (behind_hull ? sunk : 0.0))) {
                    nearest = t;
                    victim = nullptr;
                    hit_npc = nullptr;
                    hit_target = nullptr;
                    hit_powerup = -1;
                    hit_vehicle = &v;
                    const double speed = bolt.velocity.length();
                    normal = speed > 1e-6 ? bolt.velocity * (-1.0 / speed) : Vec3{0, 1, 0};
                }
            }
        }
        if (hit_vehicle != nullptr) {
            const Vec3 at = from + (to - from) * *nearest;
            damage_vehicle(*hit_vehicle, bolt_damage(def, def.bolt_life - bolt.life) * def.vehicle_factor, bolt.owner,
                           blasts);
            if (def.explosion_damage > 0 && def.explosion_outer > 0) {
                blasts.push_back({bolt, at, normal, false});
            } else {
                impacts_.push_back({bolt.owner, bolt.weapon, at, normal, true});
            }
            return true;
        }
        if (victim != nullptr) {
            const Vec3 at = from + (to - from) * *nearest;
            // Less after a long flight; double in the head.
            const bool headshot = at.y >= victim_top - head_size;
            const double flown = def.bolt_life - bolt.life;
            damage(*victim, bolt_damage(def, flown) * (headshot ? headshot_multiplier : 1.0), def.damage_type,
                   bolt.owner);
            if (def.explosion_damage > 0 && def.explosion_outer > 0) {
                blasts.push_back({bolt, at, normal, false});
            } else {
                impacts_.push_back({bolt.owner, bolt.weapon, at, normal, true});
            }
            return true;
        }
        if (hit_npc != nullptr) {
            const Vec3 at = from + (to - from) * *nearest;
            const bool headshot = hit_npc->actor_kind == 3 && at.y >= victim_top - head_size;
            const double flown = def.bolt_life - bolt.life;
            damage_npc(*hit_npc, bolt_damage(def, flown) * (headshot ? headshot_multiplier : 1.0),
                       def.damage_type, bolt.owner);
            if (def.explosion_damage > 0 && def.explosion_outer > 0) {
                blasts.push_back({bolt, at, normal, false});
            } else {
                impacts_.push_back({bolt.owner, bolt.weapon, at, normal, true});
            }
            return true;
        }
        if (hit_target != nullptr) {
            const Vec3 at = from + (to - from) * *nearest;
            const bool headshot = at.y >= victim_top - head_size;
            const double flown = def.bolt_life - bolt.life;
            damage_target(*hit_target, bolt_damage(def, flown) * (headshot ? headshot_multiplier : 1.0),
                          def.damage_type, bolt.owner);
            if (def.explosion_damage > 0 && def.explosion_outer > 0) {
                blasts.push_back({bolt, at, normal, false});
            } else {
                impacts_.push_back({bolt.owner, bolt.weapon, at, normal, true});
            }
            return true;
        }
        if (hit_powerup >= 0) {
            const Vec3 at = from + (to - from) * *nearest;
            damage_powerup(size_t(hit_powerup), def.damage, bolt.owner, blasts);
            if (def.explosion_damage > 0 && def.explosion_outer > 0) {
                blasts.push_back({bolt, at, normal, false});
            } else {
                impacts_.push_back({bolt.owner, bolt.weapon, at, normal, false});
            }
            return true;
        }
        if (nearest) {
            const Vec3 at = from + (to - from) * *nearest;
            if (!bounces && !harmless && world_hit && world_hit->t == *nearest && world_hit->destructible >= 0) {
                damage_destructible(size_t(world_hit->destructible), bolt_damage(def, def.bolt_life - bolt.life),
                                    bolt.owner, blasts);
            }
            if (!bounces) {
                if (def.explosion_damage > 0 && def.explosion_outer > 0) {
                    blasts.push_back({bolt, at, normal, false});
                } else {
                    impacts_.push_back({bolt.owner, bolt.weapon, at, normal, false});
                }
                return true;
            }
            // A bounce off the surface it hit: back out of it with Rebound
            // of the speed it came in with, along it losing some to Friction
            // (ASSUMPTION: kept 1 / (1 + Friction / 4) of it). It comes to
            // rest on ground once slow. Its timer may start now.
            bolt.position = from + (to - from) * std::max(0.0, *nearest - 0.05);
            const double into = bolt.velocity.dot(normal);
            const Vec3 across = bolt.velocity - normal * into;
            bolt.velocity = across * (1 / (1 + def.friction / 4)) - normal * (into * def.rebound);
            if (!bolt.touched && def.timer_on_contact) {
                bolt.life = def.fuse;
            }
            bolt.touched = true;
            if (normal.y > 0.7 && bolt.velocity.length() < 1.5) {
                bolt.resting = true;
                bolt.velocity = {};
            }
        } else {
            bolt.position = to;
        }
        if (bolt.life <= 0) {
            if (bounces) {
                blasts.push_back({bolt, bolt.position, {0, 1, 0}, false});  // the fuse ran out
            }
            return true;
        }
        return false;
    });
    if (!harmless) {
        for (size_t i = 0; i < blasts.size(); ++i) {
            const Blast blast = blasts[i];
            explode(blast, blasts);
        }
    }
}

void GameServer::damage_powerup(size_t index, double amount, uint32_t attacker, std::vector<Blast>& blasts) {
    if (!conquest_ || !conquest_->powerup_alive(index) || amount <= 0) {
        return;
    }
    events_.push_back({net::CombatEvent::hit, attacker, 0, float(amount)});
    if (conquest_->damage_powerup(index, amount)) {
        Blast blast;
        blast.bolt.owner = attacker;
        blast.bolt.team = -1;
        blast.bolt.weapon = net::droid_explosion_weapon;
        blast.at = conquest_->battlefield().powerups[index].position + Vec3{0, 0.7, 0};
        blast.normal = {0, 1, 0};
        blast.droid = true;
        blasts.push_back(blast);
    }
}

void GameServer::damage_destructible(size_t index, double amount, uint32_t attacker, std::vector<Blast>& blasts) {
    if (!conquest_ || !conquest_->destructible_alive(index) || amount <= 0) {
        return;
    }
    const Battlefield& field = conquest_->battlefield();
    if (amount < 1e11 && attacker != 0 && !config_.friendly_fire) {
        if (const Client* src = find(attacker)) {
            for (const SpaceSystem& sys : field.space_systems) {
                if (sys.destructible == int(index) && sys.owner_team >= 0 && field.allied(src->team, sys.owner_team)) {
                    return;
                }
            }
        }
    }
    if (conquest_->destructible_shielded(index)) {
        if (attacker != 0 && amount < 1e11) {
            // Its shield took it: the shooter's told (else nothing showed it).
            events_.push_back({net::CombatEvent::shielded, attacker, uint32_t(index), float(amount)});
        }
        return;
    }
    if (attacker != 0 && amount < 1e11) {
        events_.push_back({net::CombatEvent::hit, attacker, 0, float(amount)});
    }
    if (conquest_->damage_destructible(index, amount)) {
        const Destructible& d = field.destructibles[index];
        world_.set_destroyed(index, true);
        say(std::format("{} destroyed", d.name));
        Blast blast;
        blast.bolt.owner = attacker;
        blast.bolt.team = -1;
        blast.bolt.weapon = net::object_explosion_weapon;
        blast.at = d.center;
        blast.normal = {0, 1, 0};
        blast.destructible = int(index);
        blasts.push_back(blast);

        for (const SpaceSystem& sys : field.space_systems) {
            if (sys.destructible == int(index) && sys.owner_team >= 0 && sys.points > 0) {
                conquest_->add_score(1 - sys.owner_team, sys.points);
            }
        }
        for (const DestroyLink& link : field.destroy_links) {
            if (link.source == int(index)) {
                for (const int target : link.targets) {
                    if (target >= 0 && size_t(target) != index) {
                        damage_destructible(size_t(target), 1e12, attacker, blasts);
                    }
                }
            }
        }
        for (const TurretMainframe& link : field.destroy_turrets) {
            if (link.destructible == int(index)) {
                for (Vehicle& v : vehicles_) {
                    if (v.dead_for >= 0 || v.spawn < 0 || size_t(v.spawn) >= field.vehicle_spawns.size()) {
                        continue;
                    }
                    const std::string& tname = field.vehicle_spawns[size_t(v.spawn)].name;
                    const bool linked = std::any_of(link.turrets.begin(), link.turrets.end(), [&](const std::string& n) {
                        return n.size() == tname.size() &&
                               std::equal(n.begin(), n.end(), tname.begin(),
                                          [](char a, char b) { return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b)); });
                    });
                    if (linked) {
                        if (v.driver != 0) {
                            if (Client* driver = find(v.driver)) {
                                leave_vehicle(*driver);
                                kill(*driver, attacker);
                            }
                        }
                        v.dead_for = 1e9;
                    }
                }
            }
        }
    }
}

// Damage to everyone near: teammates are spared (unless friendly fire is
// on), the thrower isn't (ASSUMPTION: Battlefront hurts the thrower too).
void GameServer::explode(const Blast& blast, std::vector<Blast>& blasts) {
    const Bolt& bolt = blast.bolt;
    WeaponDef droid_def;
    if (blast.droid) {
        // Battlefront's com_inf_rechargedroid_exp: small burst when a droid blows up.
        droid_def.explosion_damage = 20.0;
        droid_def.explosion_inner = 1.25;
        droid_def.explosion_outer = 2.5;
    } else if (blast.vehicle >= 0 && size_t(blast.vehicle) < vehicle_classes().size()) {
        // A destroyed vehicle's own explosion (its class's).
        const VehicleClass& cls = vehicle_classes()[size_t(blast.vehicle)];
        droid_def.explosion_damage = cls.explosion_damage;
        droid_def.explosion_inner = cls.explosion_inner;
        droid_def.explosion_outer = std::max(cls.explosion_outer, 0.1);
    } else if (blast.destructible >= 0 && conquest_) {
        // A destroyed object's own explosion, out from its middle.
        const Destructible& d = conquest_->battlefield().destructibles[size_t(blast.destructible)];
        droid_def.explosion_damage = d.explosion_damage;
        droid_def.explosion_inner = d.explosion_inner;
        droid_def.explosion_outer = d.explosion_outer;
    }
    const WeaponDef& def = blast.droid || blast.destructible >= 0 || blast.vehicle >= 0 ? droid_def : bolt.def;
    if (blast.vehicle >= 0 && def.explosion_damage <= 0) {
        // No harm, but drawn.
        explosions_.push_back({bolt.owner, bolt.weapon, blast.at, blast.normal, float(def.explosion_outer)});
        return;
    }
    if (def.explosion_damage <= 0 || def.explosion_outer <= 0) {
        return;
    }
    explosions_.push_back({bolt.owner, bolt.weapon, blast.at, blast.normal, float(def.explosion_outer)});
    // From a little off the surface it went off at: walls shelter whoever
    // it can't see (chest or head).
    const Vec3 center = blast.at + blast.normal * 0.1;
    const auto sheltered = [&](Vec3 feet) {
        return shot_hit(center, feet + Vec3{0, 1.0, 0}) && shot_hit(center, feet + Vec3{0, 1.6, 0});
    };
    const bool pvp_blocked = !conquest_ && !config_.pvp_enabled && bolt.team != hostile_npc_team;
    for (Client& target : clients_) {
        const bool teammate = !blast.droid && allied(bolt.team, target.team) &&
                              target.entity != bolt.owner && !config_.friendly_fire;
        if (target.life != Life::alive || target.state.vehicle >= 0) {
            continue;  // (a driver: its vehicle takes it)
        }
        const Vec3 target_pos = blast.droid ? target.state.position : position_at(target, double(tick_) - bolt.rewind);
        if (sheltered(target_pos)) {
            continue;
        }
        // Thrown away from it (teammates too: a blast doesn't take sides).
        const double push = explosion_push(def, blast.at, target.state.position) *
                            (1.0 - std::clamp(target.stats.explosion_push_resist, 0.0, max_mitigation));
        const bool hunted = pvp_blocked && bounty_pvp_allowed(bolt.owner, target.entity);  // Stage 12
        if (push > 0 && !(pvp_blocked && !hunted && target.entity != bolt.owner)) {
            Vec3 away = target.state.position + Vec3{0, 1.0, 0} - blast.at;
            away = away.length() > 1e-3 ? away * (1 / away.length()) : Vec3{0, 1, 0};
            away.y = std::max(away.y, 0.35);  // and off its feet
            target.state.velocity += away * (push / away.length());
            target.state.grounded = false;
        }
        if (teammate || (pvp_blocked && !hunted && target.entity != bolt.owner)) {
            continue;
        }
        const double amount = explosion_damage(def, blast.at, target_pos);
        if (amount > 0) {
            const DamageType blast_type = def.damage_type == DamageType::electro ? DamageType::electro : DamageType::explosive;
            damage(target, amount, blast_type, bolt.owner);
        }
    }
    if (bolt.team != hostile_npc_team) {
        for (Npc& npc : npcs_) {
            if (npc.hidden || npc.actor_kind < 2 || npc.life != Life::alive || sheltered(npc.state.position)) continue;
            const double amount = explosion_damage(def, blast.at, npc.state.position);
            if (amount > 0) {
                damage_npc(npc, amount, DamageType::explosive, bolt.owner);
            }
        }
    }
    for (RangeTarget& target : targets_) {
        if (!target.alive || sheltered(target.position)) continue;
        const double amount = explosion_damage(def, blast.at, target.position);
        if (amount > 0) {
            damage_target(target, amount, DamageType::explosive, bolt.owner);
        }
    }
    // Vehicles in reach: from the nearest point of their bodies, as
    // objects' bounds (and the weapon's VehicleScale).
    {
        const auto classes = vehicle_classes();
        for (Vehicle& v : vehicles_) {
            if (v.dead_for >= 0 || (allied(bolt.team, v.team) && !config_.friendly_fire &&
                                    v.driver != bolt.owner)) {
                continue;
            }
            const double distance = distance_to_vehicle(blast.at, v.state.position, v.state.yaw, classes[size_t(v.cls)]);
            const double full = def.explosion_damage * (blast.droid || blast.destructible >= 0 || blast.vehicle >= 0
                                                            ? 1.0
                                                            : def.explosion_vehicle_factor);
            const double amount = distance <= def.explosion_inner ? full
                                  : distance >= def.explosion_outer
                                      ? 0.0
                                      : full * (def.explosion_outer - distance) /
                                            std::max(1e-6, def.explosion_outer - def.explosion_inner);
            if (amount > 0) {
                damage_vehicle(v, amount, bolt.owner, blasts);
            }
        }
    }
    if (conquest_) {
        const auto& stations = conquest_->battlefield().powerups;
        for (size_t i = 0; i < stations.size(); ++i) {
            if (!conquest_->powerup_alive(i)) {
                continue;
            }
            const double amount = explosion_damage(def, blast.at, stations[i].position);
            if (amount > 0 && !sheltered(stations[i].position)) {
                damage_powerup(i, amount, bolt.owner, blasts);
            }
        }
        // Objects whose bounds it reaches (not its own: that's gone).
        // ASSUMPTION: full damage within the inner radius of the nearest
        // point of its bounds, falling to nothing at the outer, unsheltered.
        const auto& objects = conquest_->battlefield().destructibles;
        for (size_t i = 0; i < objects.size(); ++i) {
            if (int(i) == blast.destructible || !conquest_->destructible_alive(i)) {
                continue;
            }
            const double distance = (objects[i].nearest(blast.at) - blast.at).length();
            const double amount =
                distance <= def.explosion_inner ? def.explosion_damage
                : distance >= def.explosion_outer
                    ? 0.0
                    : def.explosion_damage * (def.explosion_outer - distance) /
                          (def.explosion_outer - def.explosion_inner);
            if (amount > 0) {
                damage_destructible(i, amount, bolt.owner, blasts);
            }
        }
    }
}

void GameServer::damage_npc(Npc& npc, double raw, DamageType type, uint32_t attacker) {
    if (npc.hidden || npc.actor_kind < 2 || npc.life != Life::alive || raw <= 0) {
        return;
    }
    raw = companion_hit(npc.entity, attacker, raw, type);  // Stage 12
    if (raw <= 0) {
        return;
    }
    const double amount = raw * (1.0 - npc.mitigation);
    npc.health = std::max(0.0, npc.health - amount);
    npc.last_attacker = attacker;
    npc.last_damaged_tick = tick_;
    events_.push_back({net::CombatEvent::hit, attacker, npc.entity, float(amount)});
    if (npc.health <= 0) {
        npc.life = Life::dead;
        npc.pose = 0;
        npc.input = MoveInput{};
        npc.state.velocity = {};
        events_.push_back({net::CombatEvent::kill, attacker, npc.entity, 0});
        if (on_npc_killed) {
            on_npc_killed(npc.entity, attacker);
        }
        specialist_kill(npc.entity, attacker, true);  // Stage 12
    }
}

void GameServer::damage_target(RangeTarget& target, double raw, DamageType, uint32_t attacker) {
    if (!target.alive || raw <= 0) {
        return;
    }
    const double amount = raw * (1.0 - target.mitigation);
    target.health = std::max(0.0, target.health - amount);
    events_.push_back({net::CombatEvent::hit, attacker, target.entity, float(amount)});
    if (target.health <= 0) {
        target.alive = false;
        target.respawn_timer = target.respawn_time;
        events_.push_back({net::CombatEvent::kill, attacker, target.entity, 0});
    }
}

// Damage after the victim's mitigation (its armor, mostly), absorbed by any
// active Deflector Shield before reaching health.
void GameServer::damage(Client& victim, double raw, DamageType type, uint32_t attacker, double shield_disrupt) {
    const double amount = mitigated_damage(victim.stats, raw, type);
    if (victim.life != Life::alive || amount <= 0) {
        return;
    }
    if (victim.switching_to >= 0) {
        victim.switching_to = -1;
        victim.switch_remaining = 0.0;
        notify(victim.entity, "Loadout switch interrupted!");
    }
    const ShieldHitResult hit = apply_shield_damage(victim.shield, amount, type, shield_disrupt);
    victim.shield_recharge_timer = std::max(victim.shield_recharge_timer, victim.stats.shield_delay);
    if (hit.disrupt_seconds > 0) {
        victim.shield_disrupt_timer = std::max(victim.shield_disrupt_timer, hit.disrupt_seconds);
    }
    victim.health = std::max(0.0, victim.health - hit.health_damage);
    events_.push_back({net::CombatEvent::hit, attacker, victim.entity, float(amount)});
    if (attacker != 0 && attacker != victim.entity) {
        if (const std::string armor = armor_profession(victim); !armor.empty()) {
            victim.armor_xp += raw / armor_damage_per_xp;
            if (victim.armor_xp >= 1) {
                const auto points = int64_t(victim.armor_xp);
                victim.armor_xp -= double(points);
                earn_profession_xp(victim, armor, points);
            }
        }
    }
    if (victim.health <= 0) {
        kill(victim, attacker);
    }
}

void GameServer::kill(Client& victim, uint32_t attacker, bool is_flyer) {
    if (victim.state.vehicle >= 0) {
        const auto classes = vehicle_classes();
        const size_t vi = size_t(victim.state.vehicle);
        if (vi < vehicles_.size() && size_t(vehicles_[vi].cls) < classes.size() &&
            classes[size_t(vehicles_[vi].cls)].kind == VehicleKind::flyer) {
            is_flyer = true;
        }
        leave_vehicle(victim);
    }
    victim.health = 0;
    victim.shield = 0;
    victim.life = Life::dead;
    victim.respawn_tick = tick_ + uint32_t(std::lround(config_.respawn_delay * config_.tick_rate));
    ++victim.deaths;
    events_.push_back({net::CombatEvent::kill, attacker, victim.entity, 0});
    specialist_kill(victim.entity, attacker, false);  // Stage 12
    Client* killer = attacker != 0 ? find(attacker) : nullptr;
    if (killer != nullptr && killer != &victim) {
        ++killer->kills;
        if (conquest_ && killer->state.vehicle < 0) {
            if (const WeaponDef* w = weapon_in_slot(loadout_of(*killer), killer->state.combat.weapon)) {
                ++killer->weapon_kills[w->name];
            }
        } else if (conquest_ && killer->state.vehicle >= 0) {
            ++killer->weapon_kills["vehicle"];
        }
        if (conquest_ && !allied(killer->team, victim.team)) {
            if (const std::string held = held_profession(*killer); !held.empty()) {
                earn_profession_xp(*killer, held, match_kill_profession_xp);
            }
        }
    }
    if (conquest_) {
        conquest_->killed(victim.team, is_flyer);
        drop_pickup(victim.state.position);
    } else if (victim.character != 0) {
        const int64_t lost = std::max<int64_t>(0, victim.credits / 10);
        if (lost > 0) {
            victim.credits -= lost;
            save_economy(victim);
            notify(victim.entity, std::format("Died in the wild: lost {} credits (10%)", lost));
        }
    }
    say(killer != nullptr ? std::format("{} killed {}", killer->name, victim.name)
                          : std::format("{} died", victim.name));
}

double GameServer::random_unit() {
    uint64_t z = (rng_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return double(z >> 11) / double(1ull << 53);
}

void GameServer::spawn_pickup(PickupKind kind, Vec3 position) {
    constexpr size_t max_pickups = 64;
    if (pickups_.size() >= max_pickups) {
        pickups_.erase(pickups_.begin());
    }
    pickups_.push_back({next_pickup_++, kind, position, pickup_def(kind).lifespan});
}

void GameServer::drop_pickup(Vec3 at) {
    const auto kind = roll_pickup(random_unit());
    if (!kind) {
        return;
    }
    // Rest on the floor right under where the soldier fell; nothing dropped
    // over a chasm, in lava, or out of bounds.
    const double ground = world_.ground_at(at.x, at.z, at.y + 0.5, 0).height;
    if (ground < -1e8 || ground > at.y + 1.0 || at.y - ground > 3.0) {
        return;
    }
    const Vec3 landed{at.x, ground, at.z};
    if (conquest_) {
        const Battlefield& field = conquest_->battlefield();
        if (!field.contains(landed) || field.harmful(landed)) {
            return;
        }
    }
    spawn_pickup(*kind, landed);
}

void GameServer::advance_pickups() {
    if (pickups_.empty()) {
        return;
    }
    const double dt = 1 / config_.tick_rate;
    constexpr double reach2 = pickup_radius * pickup_radius;
    constexpr double vertical_reach = 1.5;
    std::erase_if(pickups_, [&](Pickup& pickup) {
        pickup.life -= dt;
        if (pickup.life <= 0) {
            return true;
        }
        const PickupDef& def = pickup_def(pickup.kind);
        for (Client& client : clients_) {
            if (client.life != Life::alive) {
                continue;
            }
            const double dx = client.state.position.x - pickup.position.x;
            const double dz = client.state.position.z - pickup.position.z;
            if (dx * dx + dz * dz > reach2 || std::abs(client.state.position.y - pickup.position.y) > vertical_reach) {
                continue;
            }
            // Only consumed if the soldier can actually use some of it:
            // walking over a health pack at full health leaves it for someone
            // else, as in Battlefront.
            bool used = false;
            if (def.health > 0 && client.health < client.stats.max_health) {
                client.health = std::min(client.stats.max_health, client.health + def.health);
                used = true;
            }
            if (def.ammo_clips > 0 && resupply_clips(client.state, loadout_of(client), def.ammo_clips)) {
                used = true;
            }
            if (def.energy > 0 && client.state.energy < client.stats.move.max_energy) {
                client.state.energy = std::min(client.stats.move.max_energy, client.state.energy + def.energy);
                used = true;
            }
            if (used) {
                return true;
            }
        }
        return false;
    });
}

Vec3 GameServer::spawn_point(uint32_t index) const {
    Vec3 spawn = world_.clamp(config_.spawn + Vec3{config_.spawn_spacing * (index % 8), 0, 0});
    const double terrain = world_.terrain().height_at(spawn.x, spawn.z);
    spawn.y = world_.ground_at(spawn.x, spawn.z, terrain, config_.base_stats.move.step_up).height;
    return spawn;
}

void GameServer::respawn(Client& client) {
    sync_client_ammo(client, true);
    client.life = Life::alive;
    client.health = client.stats.max_health;
    client.shield = client.stats.shield_capacity;
    client.shield_recharge_timer = 0;
    client.shield_disrupt_timer = 0;
    const float yaw = client.state.yaw;
    const Vec3 fell_at = client.state.position;
    client.state = MoveState{};
    std::optional<Vec3> clone_at = client.bound ? client.bound : cloning_facility_;
    std::string facility = "Mos Eisley Cloning Facility";
    if (!client.bound && !cloning_facilities_.empty()) {
        // The nearest town's (Stage 13).
        double best = 1e300;
        for (const auto& [name, at] : cloning_facilities_) {
            const double d = std::hypot(at.x - fell_at.x, at.z - fell_at.z);
            if (d < best) {
                best = d;
                clone_at = at;
                facility = name;
            }
        }
    }
    if (!conquest_ && clone_at) {
        const uint32_t slot = client.entity > 0 ? client.entity - 1 : 0;
        Vec3 pos = world_.clamp(*clone_at + Vec3{1.5 * (slot % 4), 0, 0});
        const double terrain = world_.terrain().height_at(pos.x, pos.z);
        pos.y = world_.support_at(pos.x, pos.z, terrain + 1.0, client.stats.move.step_up).height;
        if (pos.y < -1e8) pos.y = terrain;
        client.state.position = pos;
        notify(client.entity, client.bound ? std::string("Cloned at your cloning terminal") : "Cloned at " + facility);
    } else {
        client.state.position = spawn_point(client.entity - 1);
    }
    client.state.yaw = yaw;
    reset_loadout(client.state, loadout_of(client), client.stats);
    client.history.clear();
    ++client.spawn_count;
    say(std::format("{} respawned", client.name));
}

void GameServer::drop_silent(double now) {
    for (Client& client : clients_) {
        if (now - client.last_heard > config_.client_timeout) {
            sync_client_ammo(client, true);
        }
    }
    std::erase_if(clients_, [&](const Client& client) {
        const bool silent = now - client.last_heard > config_.client_timeout;
        if (silent) {
            say(std::format("{} (entity {}) timed out", client.name, client.entity));
            save({&client, 1});
        }
        return silent;
    });
}

} // namespace swm::core
