#include "swm/core/server.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "swm/core/net/bytes.h"
#include "swm/core/net/protocol.h"

namespace swm::core {

GameServer::GameServer(net::Link& link, const World& world, ServerConfig config, CharacterStore* store)
    : link_(link), world_(world), config_(std::move(config)), store_(config_.battlefield ? nullptr : store) {
    if (config_.battlefield) {
        conquest_.emplace(*config_.battlefield, !config_.single_match);
    }
}

bool GameServer::match_over() const {
    return conquest_ && config_.single_match && conquest_->state().phase == Phase::ended &&
           conquest_->state().time_left <= 0;
}

void GameServer::set_recruiters(std::vector<net::Recruiter> recruiters, std::array<std::string, 2> factions) {
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
        if (out_worn) {
            out_worn->push_back(resolve_worn_models(*config_.catalog, cl, client->items, client->species, client->gender));
        }
    }
    return out;
}

void GameServer::award_battle_rewards(const std::string& name, const BattleReward& reward) {
    if (store_ == nullptr) {
        return;
    }
    try {
        if (const auto saved = store_->find_by_name(name)) {
            const int64_t cr = saved->credits + reward.credits;
            const int64_t fp = saved->faction_points + reward.faction_points;
            const int64_t xp = saved->experience + reward.xp;
            store_->update_economy(saved->id, cr, fp, xp, saved->active_loadout);
            for (Client& c : clients_) {
                if (c.name == name) {
                    c.credits = cr;
                    c.faction_points = fp;
                    c.experience = xp;
                }
            }
        }
    } catch (const std::exception& e) {
        say(std::format("reward save error for {}: {}", name, e.what()));
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
    const auto npc = std::find_if(npcs_.begin(), npcs_.end(), [&](const Npc& n) { return n.entity == entity; });
    return npc == npcs_.end() ? nullptr : &npc->state;
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
    npcs_.push_back(std::move(npc));
    return npcs_.back().entity;
}

GameServer::Npc* GameServer::find_npc(uint32_t entity) {
    for (Npc& npc : npcs_) {
        if (npc.entity == entity) return &npc;
    }
    return nullptr;
}

const GameServer::Npc* GameServer::find_npc(uint32_t entity) const {
    for (const Npc& npc : npcs_) {
        if (npc.entity == entity) return &npc;
    }
    return nullptr;
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
    send(client.endpoint, sync);
    client.last_sync_tick = tick_;
}

void GameServer::update(double now) {
    if (!started_) {
        started_ = true;
        next_tick_time_ = now;
        next_save_time_ = now + config_.save_interval;
    }
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
                                looks_, quartermasters_, vendors_, quest_npcs_, mission_terminals_});
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
        // Joining a queue takes standing at a war terminal; leaving doesn't.
        const bool at_terminal = std::any_of(recruiters_.begin(), recruiters_.end(), [&](const net::Recruiter& r) {
            const Vec3 d = client->state.position - r.position;
            return std::hypot(d.x, d.z) <= r.radius + 1;
        });
        if (on_queue && !client->transfer && (queue->team < 0 || (client->life == Life::alive && at_terminal))) {
            on_queue(client->entity, queue->team, queue->mode);
        }
    } else if (const auto* ack = std::get_if<net::InventoryAck>(&message)) {
        if (ack->revision == client->inventory_rev) {
            client->acked_inventory_rev = ack->revision;
        }
    } else if (const auto* quest_ack = std::get_if<net::QuestAck>(&message)) {
        if (quest_ack->revision == client->quest_rev) {
            client->acked_quest_rev = quest_ack->revision;
        }
    } else if (const auto* quest = std::get_if<net::QuestRequest>(&message)) {
        handle_quest_request(*client, *quest);
    } else if (const auto* save_lo = std::get_if<net::SaveLoadoutRequest>(&message)) {
        if (!conquest_ && store_ != nullptr && config_.catalog != nullptr && client->character != 0 &&
            save_lo->loadout.slot_index >= 0 && save_lo->loadout.slot_index < max_character_loadouts) {
            const auto owns = [&](uint64_t id) {
                if (id == 0) return true;
                return std::any_of(client->items.begin(), client->items.end(),
                                   [id](const ItemInstance& inst) { return inst.id == id; });
            };
            CharacterLoadout clean = save_lo->loadout;
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
                const bool near_qm = std::any_of(quartermasters_.begin(), quartermasters_.end(),
                                                 [&](const net::Quartermaster& q) {
                    const Vec3 d = client->state.position - q.position;
                    const bool faction_ok = def->faction == "neutral" || q.faction == "neutral" || def->faction == q.faction;
                    return faction_ok && std::hypot(d.x, d.z) <= q.radius + 1.5;
                });
                const bool near_gear_vendor = std::any_of(vendors_.begin(), vendors_.end(),
                                                          [&](const net::Vendor& v) {
                    if (v.kind != "gear_vendor" || def->faction != "neutral") return false;
                    const Vec3 d = client->state.position - v.position;
                    return std::hypot(d.x, d.z) <= v.radius + 1.5;
                });
                if (buy->use_faction_points && near_qm && client->faction_points >= def->cost_faction_points) {
                    client->faction_points -= def->cost_faction_points;
                    store_->update_economy(client->character, client->credits, client->faction_points,
                                           client->experience, int(client->loadout));
                    store_->grant_item({0, client->character, def->id, 0.5, "", "{}"});
                    client->items = store_->list_items(client->character);
                    ++client->inventory_rev;
                    client->last_sync_tick = 0;
                    notify(client->entity, "Purchased " + def->display_name);
                } else if (!buy->use_faction_points && (near_qm || near_gear_vendor) &&
                           def->cost_credits > 0 && client->credits >= def->cost_credits) {
                    client->credits -= def->cost_credits;
                    store_->update_economy(client->character, client->credits, client->faction_points,
                                           client->experience, int(client->loadout));
                    store_->grant_item({0, client->character, def->id, 0.5, "", "{}"});
                    client->items = store_->list_items(client->character);
                    ++client->inventory_rev;
                    client->last_sync_tick = 0;
                    notify(client->entity, std::format("Purchased {} (-{} Cr)", def->display_name, def->cost_credits));
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
                if (cit->items.empty()) {
                    corpse_loots_.erase(cit);
                }
            }
        }
    } else if (const auto* sell = std::get_if<net::VendorSellRequest>(&message)) {
        if (!conquest_ && store_ != nullptr && config_.catalog != nullptr && client->character != 0) {
            const bool near_vendor = std::any_of(vendors_.begin(), vendors_.end(), [&](const net::Vendor& v) {
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
                if (def != nullptr && def->kind == ItemKind::supply && def->heal_amount > 0 &&
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

    Vec3 spawn = spawn_point(joined.entity - 1);
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
            joined.char_loadouts = store_->list_loadouts(joined.character);
            rebuild_client_loadouts(joined);
            joined.inventory_rev = 1;
            joined.quests = store_->list_quests(joined.character);
            joined.quest_rev = 1;
        } catch (const std::exception& error) {
            say(std::format("inventory load error for {}: {}", name, error.what()));
        }
    }
    const Loadout& initial_lo = loadout_of(joined);
    joined.stats = stats_for(initial_lo, joined.species, config_.base_stats);
    spawn.y = world_.ground_at(spawn.x, spawn.z, spawn.y, joined.stats.move.step_up).height;
    joined.state.position = spawn;
    reset_loadout(joined.state, initial_lo, joined.stats);
    joined.health = joined.stats.max_health;
    clients_.push_back(joined);
    say(std::format("{} joined from {} as entity {} ({} character {}) at ({:.2f}, {:.2f}, {:.2f})", name,
                    from.to_string(), joined.entity, where, joined.character, spawn.x, spawn.y, spawn.z));
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
}

void GameServer::step() {
    const double dt = 1 / config_.tick_rate;
    for (Client& client : clients_) {
        for (size_t i = 0; i < config_.max_inputs_per_tick && !client.queue.empty(); ++i) {
            const MoveInput input = client.queue.front();
            client.queue.pop_front();
            client.last_input = input.sequence;
            if (client.life == Life::waiting) {
                continue;  // not in the world
            }
            const auto shot = simulate_character(client.state, input, client.life == Life::alive, world_,
                                                 loadout_of(client), client.stats, dt, vehicle_classes());
            if (shot) {
                fire(client, *shot, input.view_tick);
            }
            // The use button (a press): into a vehicle or out of it.
            const bool use = input.held(MoveInput::use);
            if (use && !client.use_held && client.life == Life::alive && conquest_) {
                use_vehicle(client);
            }
            client.use_held = use;
        }
    }
    // The server's own people, creatures, and bandits, by how they're steered.
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
                        client.loadout = size_t(client.switching_to);
                        client.switching_to = -1;
                        client.switch_remaining = 0.0;
                        client.stats = stats_for(loadout_of(client), client.species, config_.base_stats);
                        client.health = std::min(client.health, client.stats.max_health);
                        reset_loadout(client.state, loadout_of(client), client.stats);
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

    // Where everyone is this tick, kept for a second for rewinding.
    const size_t keep = size_t(config_.tick_rate) + 2;
    for (Client& client : clients_) {
        client.history.emplace_back(tick_, client.state.position);
        while (client.history.size() > keep) {
            client.history.pop_front();
        }
    }
    update_vehicles(1 / config_.tick_rate);
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

    net::Snapshot base_snapshot;
    base_snapshot.server_tick = tick_;
    for (const Client& client : clients_) {
        base_snapshot.entities.push_back(describe(client));
    }
    const bool filter_npcs = !conquest_ && npcs_.size() > 40;
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
    if (!filter_npcs) {
        for (const Npc& npc : npcs_) {
            if (!npc.hidden) {
                base_snapshot.entities.push_back(make_npc_entity(npc));
            }
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
        client.status.inventory_rev = client.inventory_rev;
        net::Snapshot client_snapshot = base_snapshot;
        if (filter_npcs) {
            constexpr double npc_view_range2 = 240.0 * 240.0;
            for (const Npc& npc : npcs_) {
                if (npc.hidden) continue;
                const double dx = npc.state.position.x - client.state.position.x;
                const double dz = npc.state.position.z - client.state.position.z;
                if (dx * dx + dz * dz <= npc_view_range2) {
                    client_snapshot.entities.push_back(make_npc_entity(npc));
                }
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
        client_snapshot.you = client.status;
        send(client.endpoint, client_snapshot);
        if (client.inventory_rev != client.acked_inventory_rev &&
            (client.last_sync_tick == 0 || tick_ - client.last_sync_tick >= 8)) {
            send_inventory_sync(client);
        }
        if (quest_book_ != nullptr && client.quest_rev != client.acked_quest_rev &&
            (client.quest_sync_tick == 0 || tick_ - client.quest_sync_tick >= 8)) {
            send_quest_sync(client);
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
        // terrain with nothing below.
        if (client.life == Life::alive && (conquest_->deadly(client.state.position) ||
                                           client.state.position.y < world_.terrain().lowest() - fall_depth)) {
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
            units.push_back({client.team, client.state.position});
        }
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
            for (Client& client : clients_) {
                client.kills = client.deaths = client.captures = 0;
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
            for (const Client& client : clients_) {
                say(std::format("  {:<16} {:<12} {} kills, {} deaths, {} captures", client.name,
                                field.team(client.team).name, client.kills, client.deaths, client.captures));
                result.players.push_back({client.name, client.team, client.kills, client.deaths, client.captures});
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
        if (loadout < avail_loadouts && conquest_->can_spawn(client.team, post)) {
            spawn_at_post(client, loadout, post);
        }
    }
}

void GameServer::return_to_waiting(Client& client) {
    client.life = Life::waiting;
    client.health = 0;
    client.outside = 0;
    client.wants_spawn.reset();
    client.state.velocity = {};
}

void GameServer::spawn_at_post(Client& client, size_t loadout_index, size_t post_index) {
    const Battlefield& field = conquest_->battlefield();
    const CommandPost& post = field.posts[post_index];
    // At the post's spawn points in turn, or spread around it (golden-angle
    // steps); facing away from it.
    const uint32_t slot = next_spawn_slot_++;
    const double angle = double(slot) * 2.399963;
    // Natives at its ally spawn points, if it has them.
    const bool ally = client.team == natives_team && !post.ally_spawn_points.empty();
    const auto& points = ally ? post.ally_spawn_points : post.spawn_points;
    Vec3 position = points.empty() ? post.position + Vec3{std::cos(angle), 0, std::sin(angle)} * post.spawn_radius
                                   : points[slot % points.size()];
    position.x = std::clamp(position.x, field.min_x, field.max_x);
    position.z = std::clamp(position.z, field.min_z, field.max_z);
    position = world_.clamp(position);
    // On the surface at the spawn point's height, if it has one; else on
    // the terrain (or what stands on it).
    const double from = ally || (post.spawn_heights && !post.spawn_points.empty())
                            ? position.y
                            : world_.terrain().height_at(position.x, position.z);
    client.loadout = loadout_index;
    const Loadout& loadout = loadout_of(client);
    client.stats = stats_for(loadout, client.species, config_.base_stats);
    // (A spawn point over a seam in the floor, one of Mygeeto's CP2's: on
    // the floor both sides of it.) A spawn point a little under its floor
    // (one of Utapau's, a meter down) is on it: looking from a meter up.
    constexpr double spawn_lift = 1;
    position.y = world_.support_at(position.x, position.z, from + spawn_lift, client.stats.move.step_up).height;

    client.state = MoveState{};
    client.state.position = position;
    client.state.yaw = float(std::atan2(-(position.x - post.position.x), -(position.z - post.position.z)));
    reset_loadout(client.state, loadout, client.stats);
    client.health = client.stats.max_health;
    client.life = Life::alive;
    client.outside = 0;
    client.history.clear();
    client.wants_spawn.reset();
    ++client.spawn_count;
    say(std::format("{} spawned with {} at {}", client.name, loadout.name, post.name));
}

void GameServer::fire(const Client& shooter, const Shot& shot, double view_tick) {
    const auto classes = vehicle_classes();
    const bool driving = shooter.state.vehicle >= 0 && size_t(shooter.state.vehicle) < classes.size();
    const WeaponDef* def =
        weapon_in_slot(driving ? classes[size_t(shooter.state.vehicle)].loadout : loadout_of(shooter), shot.weapon);
    if (def == nullptr) {
        return;
    }
    const WeaponDef& weapon = *def;
    // The tick being simulated is tick_ + 1; the shooter saw others at view_tick.
    const double rewind = std::clamp(double(tick_ + 1) - view_tick, 0.0, config_.max_rewind * config_.tick_rate);
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
            if (bounces || harmless || pvp_blocked || target.entity == bolt.owner || target.life != Life::alive || teammate ||
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
            for (Vehicle& v : vehicles_) {
                if (v.dead_for >= 0 || (v.driver != 0 && v.driver == bolt.owner) ||
                    (allied(bolt.team, v.team) && !config_.friendly_fire)) {
                    continue;
                }
                const auto t = segment_hits_vehicle(from, to, v.state.position, v.state.yaw, classes[size_t(v.cls)]);
                if (t && (!nearest || *t < *nearest)) {
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
    if (conquest_->damage_destructible(index, amount)) {
        const Destructible& d = conquest_->battlefield().destructibles[index];
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
        const double push = explosion_push(def, blast.at, target.state.position);
        if (push > 0 && !(pvp_blocked && target.entity != bolt.owner)) {
            Vec3 away = target.state.position + Vec3{0, 1.0, 0} - blast.at;
            away = away.length() > 1e-3 ? away * (1 / away.length()) : Vec3{0, 1, 0};
            away.y = std::max(away.y, 0.35);  // and off its feet
            target.state.velocity += away * (push / away.length());
            target.state.grounded = false;
        }
        if (teammate || (pvp_blocked && target.entity != bolt.owner)) {
            continue;
        }
        const double amount = explosion_damage(def, blast.at, target_pos);
        if (amount > 0) {
            damage(target, amount, DamageType::explosive, bolt.owner);
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

void GameServer::damage_npc(Npc& npc, double raw, DamageType, uint32_t attacker) {
    if (npc.hidden || npc.actor_kind < 2 || npc.life != Life::alive || raw <= 0) {
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

// Damage after the victim's mitigation (its armor, mostly).
void GameServer::damage(Client& victim, double raw, DamageType type, uint32_t attacker) {
    const double amount = mitigated_damage(victim.stats, raw, type);
    if (victim.life != Life::alive || amount <= 0) {
        return;
    }
    if (victim.switching_to >= 0) {
        victim.switching_to = -1;
        victim.switch_remaining = 0.0;
        notify(victim.entity, "Loadout switch interrupted!");
    }
    victim.health = std::max(0.0, victim.health - amount);
    events_.push_back({net::CombatEvent::hit, attacker, victim.entity, float(amount)});
    if (victim.health <= 0) {
        kill(victim, attacker);
    }
}

void GameServer::kill(Client& victim, uint32_t attacker) {
    if (victim.state.vehicle >= 0) {
        leave_vehicle(victim);
    }
    victim.health = 0;
    victim.life = Life::dead;
    victim.respawn_tick = tick_ + uint32_t(std::lround(config_.respawn_delay * config_.tick_rate));
    ++victim.deaths;
    events_.push_back({net::CombatEvent::kill, attacker, victim.entity, 0});
    Client* killer = attacker != 0 ? find(attacker) : nullptr;
    if (killer != nullptr && killer != &victim) {
        ++killer->kills;
    }
    if (conquest_) {
        conquest_->killed(victim.team);
        drop_pickup(victim.state.position);
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
    client.life = Life::alive;
    client.health = client.stats.max_health;
    const float yaw = client.state.yaw;
    client.state = MoveState{};
    if (!conquest_ && cloning_facility_) {
        const uint32_t slot = client.entity > 0 ? client.entity - 1 : 0;
        Vec3 pos = world_.clamp(*cloning_facility_ + Vec3{1.5 * (slot % 4), 0, 0});
        const double terrain = world_.terrain().height_at(pos.x, pos.z);
        pos.y = world_.support_at(pos.x, pos.z, terrain + 1.0, client.stats.move.step_up).height;
        if (pos.y < -1e8) pos.y = terrain;
        client.state.position = pos;
        notify(client.entity, "Cloned at Mos Eisley Cloning Facility");
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
