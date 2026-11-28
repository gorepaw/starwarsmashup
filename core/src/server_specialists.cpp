// Stage 12 on the server (docs/ROADMAP.md, docs/PROFESSIONS.md): the Wave 7B
// and 7C professions played out. Abilities and the timed effects they leave,
// deployables, hazards and call-ins, marks, fielded companions (tamed and
// hatched creatures, assembled droids), mounts, contracts from mission
// terminals, the bazaar and slicing. See specialist.h.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>

#include "swm/core/server.h"

namespace swm::core {

namespace {

constexpr double touch_range = 3.0;
constexpr double cohesion_radius = 12;
constexpr double reveal_radius = 4;     // a cloaked enemy this close shows anyway
constexpr uint32_t sync_resend_ticks = 20;
constexpr size_t max_samples = 24;
constexpr size_t max_companions = 20;
constexpr int64_t slice_cost = 250;

double flat_distance(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

int64_t epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// A JSON string's contents, quotes and backslashes escaped.
std::string json_string(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n' || c == '\r') continue;
        out += c;
    }
    return out;
}

// The string value of "key" in a flat JSON object ("" if absent).
std::string json_value(const std::string& json, std::string_view key) {
    const std::string quoted = std::format("\"{}\"", key);
    const size_t at = json.find(quoted);
    if (at == std::string::npos) return {};
    size_t i = json.find(':', at + quoted.size());
    if (i == std::string::npos) return {};
    i = json.find('"', i + 1);
    if (i == std::string::npos) return {};
    std::string out;
    for (++i; i < json.size() && json[i] != '"'; ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) ++i;
        out += json[i];
    }
    return out;
}

// A companion record carried by an item (an egg, a droid).
std::string companion_payload(const CompanionRecord& record) {
    return std::format("{{\"companion\":\"{}\"}}", json_string(format_companion(record)));
}

std::string clean_name(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\' || c == '|' || static_cast<unsigned char>(c) < 32) continue;
        out += c;
        if (out.size() >= 24) break;
    }
    return out;
}

double modifier_multiply(std::span<const StatModifier> mods, Stat stat) {
    double m = 1;
    for (const StatModifier& mod : mods) {
        if (mod.stat == stat) m *= mod.multiply;
    }
    return m;
}

bool is_specialist_profession(const ProfessionDef& def) {
    return def.category == ProfessionCategory::specialist || def.category == ProfessionCategory::companion;
}

} // namespace

// ---------------------------------------------------------------------------
// Looks, state, the field

void GameServer::add_companion_looks() {
    if (config_.specialists == nullptr) return;
    for (const CompanionTemplate& c : config_.specialists->companions()) {
        if (c.look.empty() || looks_.size() >= 254) continue;
        if (std::find(looks_.begin(), looks_.end(), c.look) == looks_.end()) {
            looks_.push_back(c.look);
        }
    }
}

const SpecialistState* GameServer::specialist_state(uint32_t entity) const {
    const Client* client = find(entity);
    return client ? &client->spec : nullptr;
}

std::optional<ActiveCompanion> GameServer::companion_of(uint32_t owner) const {
    for (const ActiveCompanion& c : companions_) {
        if (c.owner == owner) return c;
    }
    return std::nullopt;
}

int GameServer::field_team(const Client& client) const {
    return conquest_ ? client.team : -1;
}

bool GameServer::is_enemy(const Client& a, uint32_t entity) const {
    if (entity == a.entity) return false;
    if (const Client* c = find(entity)) {
        if (conquest_) return !allied(a.team, c->team);
        return bounty_pvp_allowed(a.entity, entity);
    }
    if (const Npc* n = find_npc(entity)) {
        if (n->hidden || n->life != Life::alive || n->actor_kind < 2) return false;
        for (const ActiveCompanion& comp : companions_) {
            if (comp.entity == entity) return comp.owner != a.entity && is_enemy(a, comp.owner);
        }
        return true;  // a creature or an outlaw
    }
    return false;
}

bool GameServer::hidden_from(uint32_t viewer, uint32_t entity) const {
    const Client* target = find(entity);
    const Client* who = find(viewer);
    if (target == nullptr || who == nullptr || viewer == entity) return false;
    if (conquest_ ? allied(who->team, target->team) : true) {
        // Outside a match players aren't enemies, unless one hunts the other.
        if (conquest_ || !bounty_pvp_allowed(viewer, entity)) return false;
    }
    bool hidden = has_effect_flag(target->spec.effects, effect_flag::cloaked);
    if (!hidden) {
        for (const Hazard& h : hazards_) {
            if (h.kind == "smoke" && (conquest_ ? allied(h.team, target->team) : h.owner == entity) &&
                flat_distance(h.position, target->state.position) <= h.radius) {
                hidden = true;
                break;
            }
        }
    }
    return hidden && flat_distance(who->state.position, target->state.position) > reveal_radius;
}

bool GameServer::cloaked(uint32_t entity) const {
    const Client* client = find(entity);
    return client != nullptr && has_effect_flag(client->spec.effects, effect_flag::cloaked);
}

net::FieldState GameServer::field_state_for(uint32_t entity) const {
    net::FieldState field;
    const Client* client = find(entity);
    if (client == nullptr) return field;
    const int team = field_team(*client);
    const auto sees = [&](int mark_team, uint32_t owner) {
        if (mark_team == -2) return owner == entity;
        if (!conquest_) return true;
        return allied(mark_team, client->team);
    };
    for (const Mark& m : marks_) {
        if (sees(m.team, m.owner) && !hidden_from(entity, m.entity)) {
            field.marks.push_back({m.entity, float(m.remaining), m.kind});
        }
    }
    for (const Client& c : clients_) {
        if (c.life != Life::alive) continue;
        if (has_effect_flag(c.spec.effects, effect_flag::cloaked) && !hidden_from(entity, c.entity)) {
            field.cloaked.push_back(c.entity);
        }
        if (c.spec.performing) field.performers.push_back(c.entity);
        uint8_t flags = 0;
        if (has_effect_flag(c.spec.effects, effect_flag::holo_flair)) flags |= 1;
        if (has_effect_flag(c.spec.effects, effect_flag::inspired)) flags |= 2;
        if (flags) field.flair.emplace_back(c.entity, flags);
    }
    for (const ActiveCompanion& c : companions_) {
        field.companions.emplace_back(c.entity, c.owner);
    }
    for (const Deployable& d : deployables_) {
        const bool friendly = conquest_ ? allied(d.team, team) : true;
        // Enemies don't see traps or sensors (they're hidden); everything else shows.
        if (!friendly && (d.kind == DeployKind::trap || d.kind == DeployKind::sensor)) continue;
        field.deployables.push_back({d.id, uint8_t(d.kind), d.owner, friendly, d.position, float(d.radius),
                                     float(d.remaining)});
    }
    for (const Hazard& h : hazards_) {
        field.hazards.push_back({h.id, h.kind, h.position, float(h.radius), float(h.remaining)});
    }
    for (const PendingStrike& s : strikes_) {
        field.hazards.push_back({0, "strike", s.position, float(s.radius), float(s.remaining)});
    }
    return field;
}

// ---------------------------------------------------------------------------
// Saving

void GameServer::load_specialist_data(Client& client) {
    SpecialistState& spec = client.spec;
    spec.loaded = true;
    if (store_ == nullptr || client.character == 0) return;
    spec.companions.clear();
    const std::string datapad = store_->get_extra(client.character, "datapad");
    size_t start = 0;
    while (start < datapad.size()) {
        const size_t end = std::min(datapad.find('\n', start), datapad.size());
        if (auto record = parse_companion(std::string_view(datapad).substr(start, end - start))) {
            spec.next_companion_id = std::max(spec.next_companion_id, record->id + 1);
            spec.companions.push_back(std::move(*record));
        }
        start = end + 1;
    }
    spec.samples = parse_samples(store_->get_extra(client.character, "dna"));
    for (const DnaSample& s : spec.samples) spec.next_sample_id = std::max(spec.next_sample_id, s.id + 1);
    spec.bounty_flag = store_->get_extra(client.character, "bounty_flag") == "1";
    spec.inspiration = parse_inspiration(store_->get_extra(client.character, "inspiration")).value_or(Inspiration{});
    spec.saved_inspiration = format_inspiration(spec.inspiration);
}

void GameServer::save_specialist_data(const Client& client) const {
    if (store_ == nullptr || client.character == 0) return;
    // Inspiration, as it stands (it counts down only while its character plays).
    if (const std::string insp = format_inspiration(client.spec.inspiration); insp != client.spec.saved_inspiration) {
        try {
            store_->set_extra(client.character, "inspiration", insp);
            client.spec.saved_inspiration = insp;
        } catch (const std::exception& error) {
            say(std::format("inspiration save error for {}: {}", client.name, error.what()));
        }
    }
    if (!client.spec.dirty) return;
    try {
        std::string datapad;
        for (const CompanionRecord& r : client.spec.companions) datapad += format_companion(r) + "\n";
        store_->set_extra(client.character, "datapad", datapad);
        store_->set_extra(client.character, "dna", format_samples(client.spec.samples));
        store_->set_extra(client.character, "bounty_flag", client.spec.bounty_flag ? "1" : "0");
        client.spec.dirty = false;
    } catch (const std::exception& error) {
        say(std::format("specialist save error for {}: {}", client.name, error.what()));
    }
}

// ---------------------------------------------------------------------------
// Loadouts: abilities, levels, the Companion Slot, slices

void GameServer::fill_specialist_loadout(const Client& client, const CharacterLoadout& cl, Loadout& loadout) const {
    const SpecialistBook* book = config_.specialists;
    const ProfessionBook* professions = config_.professions;
    if (book == nullptr || professions == nullptr) return;
    loadout.abilities = unlocked_abilities(*book, *professions, client.professions, false);
    for (std::string& id : unlocked_abilities(*book, *professions, client.professions, true)) {
        if (std::find(loadout.abilities.begin(), loadout.abilities.end(), id) == loadout.abilities.end()) {
            loadout.abilities.push_back(std::move(id));
        }
    }
    // The specialists it's licensed in (docs/PERKS.md), at their levels.
    for (const ProfessionDef& def : professions->all()) {
        if (!def.available || !is_specialist_profession(def)) continue;
        const ProfessionStanding st = profession_standing(*professions, client.professions, def.id);
        if (st.trained < 1) continue;
        // Reactor Tuning is Pilot's milestone 75: without it learned, the
        // loadout carries the level only below it.
        loadout.specialist_levels[def.id] = def.id == "pilot" && st.trained < 75 ? std::min(st.level, 74) : st.level;
    }
    loadout.companion.clear();
    if (!cl.companion.empty()) {
        const uint64_t id = std::strtoull(cl.companion.c_str(), nullptr, 10);
        for (const CompanionRecord& r : client.spec.companions) {
            if (r.id == id) loadout.companion = format_companion(r);
        }
    }
    apply_slices(client, cl, loadout);
    loadout.inspiration = format_inspiration(client.spec.inspiration);
}

void GameServer::apply_slices(const Client& client, const CharacterLoadout& cl, Loadout& loadout) const {
    const SpecialistBook* book = config_.specialists;
    if (book == nullptr) return;
    const auto item = [&](uint64_t id) -> const ItemInstance* {
        if (id == 0) return nullptr;
        for (const ItemInstance& i : client.items) {
            if (i.id == id) return &i;
        }
        return nullptr;
    };
    const std::array<uint64_t, weapon_slots> weapon_ids{cl.primary_id, cl.sidearm_id, cl.secondary_id};
    for (int s = 0; s < weapon_slots; ++s) {
        const ItemInstance* inst = item(weapon_ids[size_t(s)]);
        if (inst == nullptr || !loadout.weapons[size_t(s)]) continue;
        if (const SliceDef* slice = book->slice(item_slice(*inst)); slice && slice->applies == "weapon") {
            apply_slice(*loadout.weapons[size_t(s)], *slice);
        }
    }
    Gear sliced{"slices", "slices", {}};
    for (const auto& [slot, id] : cl.gear_ids) {
        const ItemInstance* inst = item(id);
        if (inst == nullptr) continue;
        if (const SliceDef* slice = book->slice(item_slice(*inst)); slice && slice->applies == "armor") {
            apply_slice(sliced, *slice);
        }
    }
    if (!sliced.modifiers.empty()) loadout.gear.push_back(std::move(sliced));
}

int GameServer::specialist_level(const Client& client, const std::string& profession) const {
    const Loadout& lo = loadout_of(client);
    if (const auto it = lo.specialist_levels.find(profession); it != lo.specialist_levels.end()) return it->second;
    if (config_.professions != nullptr && !client.professions.empty()) {
        if (licensed(*config_.professions, client.professions, profession)) {
            return profession_standing(*config_.professions, client.professions, profession).level;
        }
    }
    return 0;
}

void GameServer::earn_specialist_xp(Client& client, const std::string& profession, int64_t xp) {
    if (xp > 0) earn_profession_xp(client, profession, xp);
}

void GameServer::refresh_abilities(Client& client) {
    std::vector<std::string> list;
    if (config_.specialists != nullptr) {
        for (const std::string& id : loadout_of(client).abilities) {
            const AbilityDef* a = config_.specialists->ability(id);
            if (a != nullptr && (conquest_ ? !a->world_only : !a->match_only)) list.push_back(id);
        }
    }
    if (list != client.spec.abilities) {
        client.spec.abilities = std::move(list);
        ++client.spec.revision;
        client.spec.sync_tick = 0;
    }
}

// The active effects' modifiers as gear on each of the client's loadouts, so
// its own client predicts with them (they're sent with its resolved loadouts).
void GameServer::refresh_effect_gear(Client& client) {
    if (client.custom_loadouts.empty()) return;
    const std::optional<Gear> gear = effects_gear(client.spec.effects);
    for (Loadout& lo : client.custom_loadouts) {
        std::erase_if(lo.gear, [](const Gear& g) { return g.slot == "effects"; });
        if (gear) lo.gear.push_back(*gear);
    }
    const double share = client.stats.max_health > 0 ? client.health / client.stats.max_health : 1;
    client.stats = stats_for(loadout_of(client), client.species, config_.base_stats);
    if (client.life == Life::alive) client.health = std::clamp(share * client.stats.max_health, 1.0, client.stats.max_health);
    client.shield = std::min(client.shield, client.stats.shield_capacity);
    ++client.inventory_rev;
    client.last_sync_tick = 0;
}

void GameServer::give_effect(Client& client, TimedEffect effect) {
    if (client.life != Life::alive) return;
    if (effect.flags & effect_flag::debuff) {  // shrugged off in part (effect_resistance)
        const double kept = 1.0 - std::clamp(client.stats.effect_resistance, 0.0, max_mitigation);
        effect.remaining *= kept;
        effect.duration *= kept;
        if (effect.remaining <= 0) return;
        // Its slows, weaker (a perk's slow_resist: Juggernaut shrugs them off).
        if (const double resist = std::clamp(client.stats.slow_resist, 0.0, 1.0); resist > 0) {
            for (StatModifier& m : effect.modifiers) {
                const bool speed = m.stat == Stat::run_speed || m.stat == Stat::strafe_speed ||
                                   m.stat == Stat::sprint_factor || m.stat == Stat::sprint_strafe ||
                                   m.stat == Stat::acceleration || m.stat == Stat::jump_height;
                if (speed && m.multiply < 1) m.multiply = 1 - (1 - m.multiply) * (1 - resist);
                if (speed && m.add < 0) m.add *= 1 - resist;
            }
            std::erase_if(effect.modifiers, [](const StatModifier& m) { return m.add == 0 && m.multiply == 1; });
            if (effect.modifiers.empty() && effect.damage_per_second <= 0 && effect.flags == effect_flag::debuff) return;
        }
    }
    if (add_effect(client.spec.effects, std::move(effect))) refresh_effect_gear(client);
    ++client.spec.revision;
    client.spec.sync_tick = 0;
}

bool GameServer::granted(const Client& client, std::string_view profession, std::string_view grant) const {
    return config_.professions != nullptr && has_grant(*config_.professions, client.professions, profession, grant);
}

ProfessionStanding GameServer::entertainer_standing(const Client& client) const {
    if (config_.professions == nullptr) return {};
    return profession_standing(*config_.professions, client.professions, "entertainer");
}

bool GameServer::inspire(Client& listener, const Client& performer_client, double gain) {
    const SpecialistBook* book = config_.specialists;
    const ProfessionStanding performer = entertainer_standing(performer_client);
    if (book == nullptr || performer.trained < 1 || gain <= 0 || listener.life != Life::alive) return false;
    SpecialistState& spec = listener.spec;
    // The menu grows with the performer's level (docs/PROFESSIONS.md).
    const auto offered = [&](const std::string& id) -> const InspirationDef* {
        const InspirationDef* d = book->inspiration(id);
        return d != nullptr && d->level <= performer.level ? d : nullptr;
    };
    // The listener's choice, if this performer offers it; else more of what
    // it has; else (having none) the first this performer offers. One it
    // doesn't offer is never written over.
    const InspirationDef* def = offered(spec.inspiration_choice);
    if (def == nullptr) def = offered(spec.inspiration.id);
    if (def == nullptr && !spec.inspiration.id.empty()) return false;
    for (const InspirationDef& d : book->inspirations()) {
        if (def == nullptr && d.level <= performer.level) def = &d;
    }
    if (def == nullptr) return false;
    Inspiration& insp = spec.inspiration;
    const bool virtuoso = granted(performer_client, "entertainer", "virtuoso");
    const double strength = (virtuoso ? 1.0 : inspiration_strength(performer.level)) *
                            (granted(performer_client, "entertainer", "crowd_pleaser") ? 1.1 : 1.0);
    if (insp.id != def->id) {
        insp = Inspiration{def->id, 0, strength};
    } else {
        insp.strength = std::max(insp.strength, strength);
    }
    const double cap = virtuoso ? virtuoso_inspiration_cap : inspiration_cap;
    insp.remaining = std::max(insp.remaining, std::min(cap, insp.remaining + gain));
    std::erase_if(spec.effects, [](const TimedEffect& e) { return e.id == "inspiration"; });
    sync_inspiration(listener);
    return true;
}

void GameServer::sync_inspiration(Client& client) {
    SpecialistState& spec = client.spec;
    if (store_ == nullptr && !spec.inspiration_carried && client.life == Life::alive) {
        spec.inspiration_carried = true;  // a match: what the hub's loadout brought
        spec.inspiration = parse_inspiration(loadout_of(client).inspiration).value_or(Inspiration{});
    }
    const auto it = std::find_if(spec.effects.begin(), spec.effects.end(),
                                 [](const TimedEffect& e) { return e.id == "inspiration"; });
    const InspirationDef* def = config_.specialists ? config_.specialists->inspiration(spec.inspiration.id) : nullptr;
    if (def == nullptr || spec.inspiration.remaining <= 0) {
        if (it != spec.effects.end()) {
            spec.effects.erase(it);
            refresh_effect_gear(client);
            ++spec.revision;
            spec.sync_tick = 0;
        }
        return;
    }
    if (client.life != Life::alive) return;
    if (it != spec.effects.end() && std::abs(it->remaining - spec.inspiration.remaining) < 2) return;
    if (it != spec.effects.end()) spec.effects.erase(it);
    give_effect(client, inspiration_effect(*def, spec.inspiration));
}

void GameServer::give_hit_effect(const WeaponDef& weapon, uint32_t attacker, Client& victim) {
    if (!hit_effect_lands(weapon, random_unit())) return;
    const HitEffect& h = *weapon.hit_effect;
    TimedEffect e;
    e.id = "hit:" + h.id;
    e.name = h.name;
    e.source = attacker;
    e.remaining = h.duration;
    e.duration = h.duration;
    e.modifiers = h.modifiers;
    e.damage_per_second = h.damage_per_second;
    e.damage_type = h.damage_type;
    e.flags = effect_flag::debuff;
    give_effect(victim, std::move(e));
}

void GameServer::give_hit_effect(const WeaponDef& weapon, uint32_t attacker, Npc& victim) {
    if (victim.life != Life::alive || !hit_effect_lands(weapon, random_unit())) return;
    const HitEffect& h = *weapon.hit_effect;
    const std::string id = "hit:" + h.id;
    npc_base_speed_.try_emplace(victim.entity, victim.walk_speed, victim.run_speed);
    for (NpcEffect& e : npc_effects_) {
        if (e.entity == victim.entity && e.id == id) {  // refreshed, never stacked
            e.remaining = std::max(e.remaining, h.duration);
            e.source = attacker;
            return;
        }
    }
    npc_effects_.push_back({victim.entity, id, h.duration, modifier_multiply(h.modifiers, Stat::run_speed),
                            h.damage_per_second, h.damage_type, attacker, false});
}

void GameServer::mark_entity(uint32_t entity, int team, uint32_t owner, double seconds, uint8_t kind) {
    for (Mark& m : marks_) {
        if (m.entity == entity && m.team == team && m.kind == kind && (team != -2 || m.owner == owner)) {
            m.remaining = std::max(m.remaining, seconds);
            return;
        }
    }
    marks_.push_back({entity, team, owner, seconds, kind});
}

void GameServer::break_cloak(Client& client) {
    if (has_effect_flag(client.spec.effects, effect_flag::cloaked)) {
        if (remove_effects(client.spec.effects, effect_flag::cloaked)) refresh_effect_gear(client);
        ++client.spec.revision;
        client.spec.sync_tick = 0;
    }
    if (client.spec.performing) {
        client.spec.performing = false;
        ++client.spec.revision;
        client.spec.sync_tick = 0;
    }
}

bool GameServer::bounty_pvp_allowed(uint32_t shooter, uint32_t victim) const {
    if (conquest_) return false;
    const Client* a = find(shooter);
    const Client* b = find(victim);
    if (a == nullptr || b == nullptr || a == b) return false;
    const auto hunts = [](const Client& hunter, const Client& mark) {
        if (!mark.spec.bounty_flag) return false;
        return std::any_of(hunter.spec.contracts.begin(), hunter.spec.contracts.end(), [&](const Contract& c) {
            return c.kind == ContractKind::pvp_mark && c.target_entity == mark.entity;
        });
    };
    // The hunter may strike their mark, and the mark may fight back.
    return hunts(*a, *b) || hunts(*b, *a);
}

double GameServer::vehicle_damage_factor(uint32_t driver) const {
    const Client* client = find(driver);
    if (client == nullptr) return 1;
    double factor = 1;
    for (const TimedEffect& e : client->spec.effects) {
        if (e.flags & effect_flag::vehicle_guard) factor = std::min(factor, std::clamp(e.heal_per_second, 0.1, 1.0));
    }
    // Reactor Tuning (Pilot 75, learned): up to 10% less.
    if (const int level = specialist_level(*client, "pilot"); level >= 75) {
        factor *= 1 - 0.1 * std::min(1.0, double(level - 74) / 26.0);
    }
    return factor;
}

// ---------------------------------------------------------------------------
// Requests

void GameServer::handle_specialist_request(Client& client, const net::SpecialistRequest& request) {
    using R = net::SpecialistRequest;
    const SpecialistBook* book = config_.specialists;
    if (book == nullptr) return;
    switch (request.action) {
    case R::use_ability: {
        const AbilityDef* ability = book->ability(request.id);
        if (ability == nullptr) return;
        if (std::find(client.spec.abilities.begin(), client.spec.abilities.end(), ability->id) ==
            client.spec.abilities.end()) {
            notify(client.entity, std::format("{} isn't in this loadout's kit", ability->name));
            return;
        }
        if (client.life != Life::alive) return;
        if (const auto it = client.spec.cooldowns.find(ability->id); it != client.spec.cooldowns.end() && it->second > 0) {
            return;
        }
        if (!client.spec.channel.empty()) {
            notify(client.entity, "Busy: finish or cancel what you're doing first");
            return;
        }
        if (use_ability(client, *ability, request.target, request.position)) {
            client.spec.cooldowns[ability->id] = ability->cooldown * client.stats.ability_cooldown;
            ++client.spec.revision;
            client.spec.sync_tick = 0;
        }
        return;
    }
    case R::cancel_channel:
        if (!client.spec.channel.empty()) {
            client.spec.channel.clear();
            ++client.spec.revision;
            client.spec.sync_tick = 0;
        }
        return;
    case R::companion_summon:
        summon_companion(client, uint64_t(std::max<int64_t>(0, request.amount)));
        return;
    case R::companion_dismiss:
        if (client.spec.active_companion != 0) {
            dismiss_companion(client, false);
            notify(client.entity, "Companion stored");
        }
        return;
    case R::companion_command:
        for (ActiveCompanion& c : companions_) {
            if (c.owner != client.entity) continue;
            const auto mode = CompanionMode(std::clamp<int64_t>(request.amount, 0, 2));
            if (mode == CompanionMode::attack) {
                if (!is_enemy(client, request.target)) return;
                c.target = request.target;
            } else {
                c.target = 0;
            }
            if (mode == CompanionMode::guard) c.guard_at = request.position;
            c.mode = mode;
            ++client.spec.revision;
            client.spec.sync_tick = 0;
        }
        return;
    case R::bazaar_browse:
    case R::bazaar_list:
    case R::bazaar_buy:
    case R::bazaar_cancel:
        bazaar_action(client, request);
        return;
    case R::contracts:
        offer_contracts(client, size_t(request.target));
        return;
    case R::contract_accept: {
        const auto it = std::find_if(client.spec.offers.begin(), client.spec.offers.end(),
                                     [&](const Contract& c) { return int64_t(c.id) == request.amount; });
        if (it == client.spec.offers.end()) return;
        if (client.spec.contracts.size() >= max_contracts) {
            notify(client.entity, std::format("You can hold {} contracts at once", max_contracts));
            return;
        }
        client.spec.contracts.push_back(*it);
        notify(client.entity, "Contract accepted: " + it->title);
        client.spec.offers.erase(it);
        ++client.spec.revision;
        client.spec.sync_tick = 0;
        return;
    }
    case R::contract_abandon:
        std::erase_if(client.spec.contracts, [&](const Contract& c) { return int64_t(c.id) == request.amount; });
        ++client.spec.revision;
        client.spec.sync_tick = 0;
        return;
    default:
        datapad_action(client, request);
        return;
    }
}

// ---------------------------------------------------------------------------
// Abilities

bool GameServer::use_ability(Client& client, const AbilityDef& a, uint32_t target, Vec3 at) {
    const Vec3 from = client.state.position;
    const bool driving = client.state.vehicle >= 0;
    const bool vehicle_ability = a.effect == AbilityEffect::vehicle_repair || a.effect == AbilityEffect::vehicle_guard;
    if (driving != vehicle_ability) {
        notify(client.entity, vehicle_ability ? "You need to be driving or flying" : "Not from a vehicle");
        return false;
    }
    const double reach = a.range > 0 ? a.range : touch_range;
    const auto in_reach = [&](Vec3 p) { return flat_distance(from, p) <= reach + 0.5 && std::abs(p.y - from.y) < reach + 4; };
    const auto position_of = [&](uint32_t e) -> std::optional<Vec3> {
        if (const Client* c = find(e)) return c->state.position;
        if (const Npc* n = find_npc(e)) return n->state.position;
        return std::nullopt;
    };
    // Where it lands: the point aimed at, within range (else as far as it reaches that way).
    Vec3 point = at;
    if (a.target == AbilityTarget::ground) {
        Vec3 d{at.x - from.x, 0, at.z - from.z};
        const double len = std::hypot(d.x, d.z);
        if (len > reach && len > 1e-6) {
            point = from + d * (reach / len);
        }
        point.y = world_.ground_at(point.x, point.z, std::max(point.y, from.y) + 2, 0.5).height;
        if (point.y < -1e8) point.y = from.y;
    } else if (a.target == AbilityTarget::self) {
        point = from;
    }
    const int team = field_team(client);
    const uint32_t me = client.entity;
    const std::string& profession = a.profession;

    // Allies within `radius` of `center` (the user too).
    const auto allies_near = [&](Vec3 center, double radius) {
        std::vector<Client*> out;
        for (Client& c : clients_) {
            if (c.life != Life::alive || flat_distance(c.state.position, center) > radius) continue;
            if (&c == &client || (conquest_ ? allied(c.team, client.team) : !bounty_pvp_allowed(me, c.entity))) {
                out.push_back(&c);
            }
        }
        return out;
    };
    const auto hurt = [&](uint32_t e, double amount, DamageType type) {
        if (Client* c = find(e)) {
            damage(*c, amount, type, me);
        } else if (Npc* n = find_npc(e)) {
            damage_npc(*n, amount, type, me);
        }
    };
    const auto enemies_near = [&](Vec3 center, double radius) {
        std::vector<uint32_t> out;
        for (const Client& c : clients_) {
            if (c.life == Life::alive && flat_distance(c.state.position, center) <= radius && is_enemy(client, c.entity)) {
                out.push_back(c.entity);
            }
        }
        for (const Npc& n : npcs_) {
            if (n.actor_kind >= 2 && !n.hidden && n.life == Life::alive &&
                flat_distance(n.state.position, center) <= radius && is_enemy(client, n.entity)) {
                out.push_back(n.entity);
            }
        }
        return out;
    };
    const auto snare_npc = [&](uint32_t e, double seconds, double speed, double dps, bool tranq) {
        Npc* n = find_npc(e);
        if (n == nullptr) return;
        npc_base_speed_.try_emplace(e, n->walk_speed, n->run_speed);
        npc_effects_.push_back({e, a.id, seconds, speed, dps, a.damage_type, me, tranq});
    };
    // A Squad Leader's orders: a Field Commander's last and reach half again.
    const bool order = a.profession == "squad_leader" && a.effect == AbilityEffect::buff;
    const double order_scale = order && granted(client, "squad_leader", "field_commander") ? 1.5 : 1.0;
    const auto buff_effect = [&]() {
        TimedEffect e;
        e.id = a.id;
        e.name = a.name;
        e.source = me;
        e.remaining = e.duration = std::max(1.0, a.duration * order_scale);
        e.modifiers = a.modifiers;
        if (a.flair) e.flags |= effect_flag::holo_flair;
        return e;
    };

    switch (a.effect) {
    case AbilityEffect::buff: {
        std::vector<Client*> targets;
        if (a.target == AbilityTarget::ally) {
            Client* t = target != 0 ? find(target) : &client;
            if (t == nullptr || t->life != Life::alive || !in_reach(t->state.position)) t = &client;
            if (t != &client && (conquest_ ? !allied(t->team, client.team) : bounty_pvp_allowed(me, t->entity))) t = &client;
            targets.push_back(t);
        } else if (a.radius > 0) {
            targets = allies_near(from, a.radius * order_scale);
        } else {
            targets.push_back(&client);
        }
        for (Client* t : targets) give_effect(*t, buff_effect());
        // Second Wind: once a life, an order fills its side's stamina too.
        if (order && !client.spec.second_wind_used && granted(client, "squad_leader", "second_wind")) {
            client.spec.second_wind_used = true;
            for (Client* t : targets) {
                MoveState s = t->state;
                s.energy = t->stats.move.max_energy;
                set_state(t->entity, s);
            }
        }
        earn_specialist_xp(client, profession, a.xp + int64_t(targets.size() - 1) * a.xp / 4);
        return true;
    }
    case AbilityEffect::heal: {
        Client* t = target != 0 ? find(target) : &client;
        if (t == nullptr || t->life != Life::alive || !in_reach(t->state.position) ||
            (t != &client && conquest_ && !allied(t->team, client.team))) {
            notify(client.entity, "No ally in reach");
            return false;
        }
        const double healed = heal(*t, a.amount);
        if (healed > 0 && t != &client) notify(t->entity, std::format("{} healed you (+{})", client.name, int(healed)));
        earn_specialist_xp(client, profession, healed > 0 ? a.xp + int64_t(healed / 10) : a.xp / 4);
        return true;
    }
    case AbilityEffect::heal_area: {
        double healed = 0;
        for (Client* t : allies_near(point, std::max(1.0, a.radius))) {
            healed += heal(*t, a.amount);
        }
        earn_specialist_xp(client, profession, a.xp + int64_t(healed / 10));
        return true;
    }
    case AbilityEffect::damage_area: {
        const std::vector<uint32_t> hit = enemies_near(point, std::max(0.5, a.radius));
        for (const uint32_t e : hit) {
            hurt(e, a.amount, a.damage_type);
            if (!a.modifiers.empty() && a.duration > 0) {
                if (Client* c = find(e)) {
                    TimedEffect daze = buff_effect();
                    daze.flags |= effect_flag::debuff;
                    give_effect(*c, std::move(daze));
                } else {
                    snare_npc(e, a.duration, modifier_multiply(a.modifiers, Stat::run_speed), 0, false);
                }
            }
        }
        explosions_.push_back({me, net::droid_explosion_weapon, point, {0, 1, 0}, float(std::max(1.0, a.radius))});
        earn_specialist_xp(client, profession, hit.empty() ? a.xp / 4 : a.xp * int64_t(std::min<size_t>(hit.size(), 4)));
        return true;
    }
    case AbilityEffect::hazard: {
        Hazard h;
        h.id = next_field_id_++;
        h.owner = me;
        h.team = team;
        h.position = point;
        h.radius = std::max(1.0, a.radius);
        h.remaining = std::max(1.0, a.duration);
        h.damage_per_second = a.amount;
        h.damage_type = a.damage_type;
        h.kind = a.amount > 0 ? std::string(damage_type_name(a.damage_type)) : "smoke";
        hazards_.push_back(h);
        earn_specialist_xp(client, profession, a.xp);
        return true;
    }
    case AbilityEffect::cleanse: {
        // One ally, or (with a Radius: Purge) everyone of its side near.
        std::vector<Client*> targets;
        if (a.radius > 0) {
            targets = allies_near(from, a.radius);
        } else {
            Client* t = target != 0 ? find(target) : &client;
            if (t == nullptr || t->life != Life::alive || !in_reach(t->state.position)) t = &client;
            targets.push_back(t);
        }
        bool had = false;
        for (Client* t : targets) {
            had |= has_effect_flag(t->spec.effects, effect_flag::debuff);
            if (remove_effects(t->spec.effects, effect_flag::debuff)) refresh_effect_gear(*t);
            ++t->spec.revision;
            t->spec.sync_tick = 0;
        }
        earn_specialist_xp(client, profession, had ? a.xp : a.xp / 4);
        return true;
    }
    case AbilityEffect::revive: {
        Client* t = target != 0 ? find(target) : nullptr;
        if (t == nullptr) {
            // The nearest fallen ally in reach.
            double best = reach + 0.5;
            for (Client& c : clients_) {
                if (&c != &client && c.life == Life::dead && (!conquest_ || allied(c.team, client.team))) {
                    const double d = flat_distance(c.state.position, from);
                    if (d <= best) {
                        best = d;
                        t = &c;
                    }
                }
            }
        }
        if (t == nullptr || t == &client || t->life != Life::dead || !in_reach(t->state.position) ||
            (conquest_ && !allied(t->team, client.team))) {
            notify(client.entity, "No fallen ally in reach");
            return false;
        }
        t->life = Life::alive;
        t->health = std::max(1.0, t->stats.max_health * std::clamp(a.amount, 0.05, 1.0));
        t->shield = 0;
        t->state.velocity = {};
        t->wants_spawn.reset();
        notify(t->entity, std::format("{} brought you back!", client.name));
        earn_specialist_xp(client, profession, a.xp);
        return true;
    }
    case AbilityEffect::cloak: {
        TimedEffect e = buff_effect();
        e.flags |= effect_flag::cloaked;
        give_effect(client, std::move(e));
        std::erase_if(marks_, [&](const Mark& m) { return m.entity == me; });
        earn_specialist_xp(client, profession, a.xp);
        return true;
    }
    case AbilityEffect::mark: {
        const auto p = position_of(target);
        if (!p || !is_enemy(client, target) || !in_reach(*p) || hidden_from(me, target)) {
            notify(client.entity, "No enemy in sight to mark");
            return false;
        }
        mark_entity(target, conquest_ ? team : -1, me, a.duration, net::FieldMark::marked);
        earn_specialist_xp(client, profession, a.xp);
        return true;
    }
    case AbilityEffect::snare: {
        const auto p = position_of(target);
        if (!p || !is_enemy(client, target) || !in_reach(*p) || hidden_from(me, target)) {
            notify(client.entity, "No enemy in reach");
            return false;
        }
        if (Client* c = find(target)) {
            TimedEffect e = buff_effect();
            e.flags |= effect_flag::debuff;
            e.damage_per_second = std::max(0.0, a.amount);
            e.damage_type = a.damage_type;
            give_effect(*c, std::move(e));
        } else {
            snare_npc(target, std::max(0.5, a.duration), modifier_multiply(a.modifiers, Stat::run_speed),
                      std::max(0.0, a.amount), false);
        }
        earn_specialist_xp(client, profession, a.xp);
        return true;
    }
    case AbilityEffect::jump: {
        MoveState s = client.state;
        const double yaw = s.yaw;
        const Vec3 forward{-std::sin(yaw), 0, -std::cos(yaw)};
        s.velocity = s.velocity * 0.5 + forward * (a.amount * 0.6);
        s.velocity.y = a.amount;
        s.grounded = false;
        set_state(me, s);
        earn_specialist_xp(client, profession, a.xp);
        return true;
    }
    case AbilityEffect::deploy: {
        if (a.deploy == DeployKind::spawn_beacon && !conquest_) return false;
        // One of each kind per owner: a new one replaces the old.
        std::erase_if(deployables_, [&](const Deployable& d) { return d.owner == me && d.ability == a.id; });
        Deployable d;
        d.id = next_field_id_++;
        d.kind = a.deploy;
        d.ability = a.id;
        d.owner = me;
        d.team = team;
        d.position = point;
        d.radius = std::max(0.5, a.radius);
        d.remaining = std::max(1.0, a.duration);
        d.amount = a.amount;
        deployables_.push_back(d);
        earn_specialist_xp(client, profession, a.xp);
        return true;
    }
    case AbilityEffect::strike:
        strikes_.push_back({me, team, point, std::max(1.0, a.radius), a.amount, a.damage_type, std::max(0.5, a.delay)});
        earn_specialist_xp(client, profession, a.xp);
        return true;
    case AbilityEffect::supply:
        spawn_pickup(PickupKind::health_large, point + Vec3{0.8, 0.3, 0});
        spawn_pickup(PickupKind::ammo, point + Vec3{-0.8, 0.3, 0});
        spawn_pickup(PickupKind::ammo, point + Vec3{0, 0.3, 0.8});
        earn_specialist_xp(client, profession, a.xp);
        return true;
    case AbilityEffect::vehicle_repair: {
        const size_t vi = size_t(client.state.vehicle);
        const auto classes = vehicle_classes();
        if (vi >= vehicles_.size() || size_t(vehicles_[vi].cls) >= classes.size()) return false;
        Vehicle& v = vehicles_[vi];
        const double top = classes[size_t(v.cls)].max_health;
        if (v.health >= top) {
            notify(client.entity, "Your vehicle isn't damaged");
            return false;
        }
        v.health = std::min(top, v.health + top * std::clamp(a.amount, 0.0, 1.0));
        earn_specialist_xp(client, profession, a.xp);
        return true;
    }
    case AbilityEffect::vehicle_guard: {
        TimedEffect e = buff_effect();
        e.flags |= effect_flag::vehicle_guard;
        e.heal_per_second = 0;
        // (Carried in heal_per_second: the share of damage the vehicle still takes.)
        e.heal_per_second = std::clamp(a.amount > 0 ? a.amount : 0.5, 0.1, 1.0);
        give_effect(client, std::move(e));
        earn_specialist_xp(client, profession, a.xp);
        return true;
    }
    case AbilityEffect::companion_repair:
    case AbilityEffect::companion_overcharge: {
        ActiveCompanion* comp = nullptr;
        for (ActiveCompanion& c : companions_) {
            if (c.owner == me) comp = &c;
        }
        Npc* npc = comp ? find_npc(comp->entity) : nullptr;
        if (comp == nullptr || npc == nullptr || npc->life != Life::alive) {
            notify(client.entity, "No companion fielded");
            return false;
        }
        const CompanionTemplate* tpl = config_.specialists->companion(comp->template_id);
        const bool droid = tpl && tpl->kind == CompanionKind::droid;
        if ((profession == "droid_operations") != droid) {
            notify(client.entity, droid ? "That's for creatures" : "That's for droids");
            return false;
        }
        if (a.effect == AbilityEffect::companion_repair) {
            if (!in_reach(npc->state.position)) {
                notify(client.entity, "Your companion is out of reach");
                return false;
            }
            npc->health = std::min(npc->max_health, npc->health + npc->max_health * std::clamp(a.amount, 0.0, 1.0));
        } else {
            comp->overcharge = std::max(1.0, a.duration);
            comp->overcharge_factor = std::max(1.0, a.amount);
        }
        earn_specialist_xp(client, profession, a.xp);
        return true;
    }
    case AbilityEffect::perform:
        if (!client.spec.performing && in_combat(client)) {
            notify(client.entity, "Not in a fight: performances are for when it's over");
            return false;
        }
        client.spec.performing = !client.spec.performing;
        client.spec.perform_pulse = 1.0;
        notify(client.entity, client.spec.performing ? "You begin to perform" : "You stop performing");
        return true;
    case AbilityEffect::track: {
        int found = 0;
        for (const Npc& n : npcs_) {
            if (n.actor_kind == 2 && !n.hidden && n.life == Life::alive &&
                flat_distance(n.state.position, from) <= a.radius) {
                bool companion = false;
                for (const ActiveCompanion& c : companions_) companion = companion || c.entity == n.entity;
                if (companion) continue;
                mark_entity(n.entity, -2, me, a.duration, net::FieldMark::tracked);
                ++found;
            }
        }
        notify(client.entity, found ? std::format("You pick up {} trail(s)", found) : "No tracks here");
        earn_specialist_xp(client, profession, found ? a.xp + std::min(found, 10) * 2 : a.xp / 4);
        return true;
    }
    case AbilityEffect::seeker: {
        int found = 0;
        for (Contract& c : client.spec.contracts) {
            if (c.target_entity == 0) continue;
            if (const auto p = position_of(c.target_entity)) {
                c.waypoint = *p;
                mark_entity(c.target_entity, -2, me, a.duration, net::FieldMark::bounty);
                ++found;
            }
        }
        if (found == 0) {
            notify(client.entity, "No marks to seek");
            return false;
        }
        ++client.spec.revision;
        client.spec.sync_tick = 0;
        earn_specialist_xp(client, profession, a.xp);
        return true;
    }
    case AbilityEffect::hvt: {
        const Client* best = nullptr;
        for (const Client& c : clients_) {
            if (c.life == Life::alive && is_enemy(client, c.entity) && (!best || c.kills > best->kills)) best = &c;
        }
        if (best == nullptr) {
            notify(client.entity, "No enemies to spot");
            return false;
        }
        mark_entity(best->entity, team, me, a.duration, net::FieldMark::marked);
        notify(client.entity, std::format("HVT spotted: {}", best->name));
        earn_specialist_xp(client, profession, a.xp);
        return true;
    }
    case AbilityEffect::tranquilize: {
        Npc* n = find_npc(target);
        if (n == nullptr || n->actor_kind != 2 || n->life != Life::alive || n->hidden || !in_reach(n->state.position) ||
            !is_enemy(client, target)) {
            notify(client.entity, "No wild creature in reach");
            return false;
        }
        snare_npc(target, std::max(1.0, a.duration), modifier_multiply(a.modifiers, Stat::run_speed), 0, true);
        earn_specialist_xp(client, profession, a.xp);
        return true;
    }
    case AbilityEffect::tame:
    case AbilityEffect::sample_dna: {
        Npc* n = find_npc(target);
        const bool sampling = a.effect == AbilityEffect::sample_dna;
        const bool usable = n != nullptr && n->actor_kind == 2 && !n->hidden &&
                            (sampling || n->life == Life::alive) && in_reach(n->state.position);
        const CompanionTemplate* tpl = usable && n->look >= 1 && size_t(n->look) <= looks_.size()
                                           ? config_.specialists->companion_for_look(looks_[size_t(n->look) - 1])
                                           : nullptr;
        bool companion = false;
        for (const ActiveCompanion& c : companions_) companion = companion || (n && c.entity == n->entity);
        if (!usable || tpl == nullptr || companion) {
            notify(client.entity, sampling ? "No creature to sample in reach" : "No wild creature you could tame in reach");
            return false;
        }
        if (!sampling) {
            const int level = specialist_level(client, "creature_handler");
            if (tpl->tier > tameable_tier(level) || level < tpl->level) {
                notify(client.entity, std::format("A {} is beyond you (Creature Handler {})", tpl->name, tpl->level));
                return false;
            }
            if (client.spec.companions.size() >= max_companions) {
                notify(client.entity, "Your datapad is full");
                return false;
            }
        } else if (client.spec.samples.size() >= max_samples) {
            notify(client.entity, "Your sample case is full");
            return false;
        }
        client.spec.channel = a.id;
        client.spec.channel_target = target;
        client.spec.channel_remaining = std::max(0.5, a.channel);
        client.spec.channel_from = from;
        notify(client.entity, sampling ? "Sampling... hold still" : "Bonding... hold still");
        return true;
    }
    }
    return false;
}

void GameServer::finish_channel(Client& client) {
    const SpecialistBook* book = config_.specialists;
    const AbilityDef* a = book ? book->ability(client.spec.channel) : nullptr;
    const uint32_t target = client.spec.channel_target;
    client.spec.channel.clear();
    ++client.spec.revision;
    client.spec.sync_tick = 0;
    Npc* n = find_npc(target);
    if (a == nullptr || n == nullptr || n->hidden || n->look < 1 || size_t(n->look) > looks_.size()) {
        notify(client.entity, "It got away");
        return;
    }
    const CompanionTemplate* tpl = book->companion_for_look(looks_[size_t(n->look) - 1]);
    if (tpl == nullptr || flat_distance(n->state.position, client.state.position) > a->range * 1.5 + 2) {
        notify(client.entity, "It got away");
        return;
    }
    if (a->effect == AbilityEffect::sample_dna) {
        const int level = specialist_level(client, "bio_engineer");
        DnaSample s{client.spec.next_sample_id++, tpl->id, dna_quality(level, random_unit())};
        client.spec.samples.push_back(s);
        client.spec.dirty = true;
        notify(client.entity, std::format("DNA sample: {} (quality {})", tpl->name, int(std::round(s.quality * 100))));
        earn_specialist_xp(client, a->profession, a->xp + tpl->tier * 10);
        return;
    }
    if (n->life != Life::alive) {
        notify(client.entity, "It's beyond taming now");
        return;
    }
    const int level = specialist_level(client, "creature_handler");
    const bool tranq = std::any_of(npc_effects_.begin(), npc_effects_.end(),
                                   [&](const NpcEffect& e) { return e.entity == target && e.tranquilized; });
    const double chance = tame_chance(*tpl, level, tranq, n->max_health > 0 ? n->health / n->max_health : 1);
    if (random_unit() >= chance) {
        n->last_attacker = client.entity;  // it takes offense
        n->last_damaged_tick = tick_;
        notify(client.entity, std::format("The {} shies away ({}% chance)", tpl->name, int(std::round(chance * 100))));
        earn_specialist_xp(client, a->profession, a->xp / 4);
        return;
    }
    CompanionRecord r;
    r.id = client.spec.next_companion_id++;
    r.template_id = tpl->id;
    r.name = tpl->name;
    r.origin = "tamed";
    r.quality = 0.35 + random_unit() * 0.3;
    r.health = 0.9 + r.quality * 0.2;
    r.damage = 0.9 + r.quality * 0.2;
    r.colors = n->colors;
    client.spec.companions.push_back(r);
    client.spec.dirty = true;
    // It leaves the wild (its lair raises another in time).
    n->life = Life::dead;
    n->health = 0;
    n->input = MoveInput{};
    n->state.velocity = {};
    n->hidden = true;
    clear_corpse_loot(n->entity);
    notify(client.entity, std::format("Tamed a {}! It's in your datapad.", tpl->name));
    earn_specialist_xp(client, a->profession, a->xp + tpl->tier * 40);
}

// ---------------------------------------------------------------------------
// Companions

void GameServer::summon_companion(Client& client, uint64_t record_id) {
    const SpecialistBook* book = config_.specialists;
    if (book == nullptr || client.life != Life::alive) return;
    if (client.spec.companion_cooldown > 0) {
        notify(client.entity, std::format("Your companion is recovering ({}s)", int(std::ceil(client.spec.companion_cooldown))));
        return;
    }
    // Which: the one asked for (outside a match), else the Companion Slot's.
    std::optional<CompanionRecord> record;
    if (record_id != 0 && !conquest_) {
        for (const CompanionRecord& r : client.spec.companions) {
            if (r.id == record_id) record = r;
        }
    }
    if (!record && !loadout_of(client).companion.empty()) record = parse_companion(loadout_of(client).companion);
    if (!record) {
        notify(client.entity, conquest_ ? "No companion in this loadout's Companion Slot" : "No such companion");
        return;
    }
    const CompanionTemplate* tpl = book->companion(record->template_id);
    if (tpl == nullptr) return;
    const bool droid = tpl->kind == CompanionKind::droid;
    const int level = specialist_level(client, droid ? "droid_operations" : "creature_handler");
    if (level < tpl->level) {
        notify(client.entity, std::format("Fielding a {} needs {} {}", tpl->name, droid ? "Droid Operations" : "Creature Handler",
                                          tpl->level));
        return;
    }
    if (conquest_ && !tpl->tactical()) {
        notify(client.entity, std::format("A {} is too big for a battle", tpl->name));
        return;
    }
    if (client.spec.active_companion != 0) dismiss_companion(client, false);
    uint8_t look = 0;
    if (const auto it = std::find(looks_.begin(), looks_.end(), tpl->look); it != looks_.end()) {
        look = uint8_t(it - looks_.begin() + 1);
    }
    const CompanionStats stats = companion_stats(*book, *tpl, *record, level);
    NpcCombatSpec spec;
    spec.actor_kind = 4;
    spec.max_health = stats.max_health;
    spec.hit_radius = tpl->radius;
    spec.hit_height = tpl->height;
    spec.mitigation = stats.mitigation;
    spec.walk_speed = stats.speed * 0.5;
    spec.run_speed = stats.speed;
    // Beside its owner, clear of them: a bantha's flank is 1.7 m from its middle.
    const double yaw = client.state.yaw;
    const double side = 1.2 + tpl->radius;
    const Vec3 beside = client.state.position + Vec3{std::cos(yaw) * side, 0, -std::sin(yaw) * side};
    const uint32_t entity = add_combat_npc(record->name, beside, yaw, look, spec, record->colors);
    ActiveCompanion active;
    active.entity = entity;
    active.owner = client.entity;
    active.record = record->id;
    active.template_id = tpl->id;
    active.name = record->name;
    active.stats = stats;
    companions_.push_back(active);
    client.spec.active_companion = record->id;
    client.spec.companion_entity = entity;
    ++client.spec.revision;
    client.spec.sync_tick = 0;
    notify(client.entity, std::format("{} is at your side", record->name));
}

void GameServer::dismiss_companion(Client& client, bool destroyed) {
    for (const ActiveCompanion& c : companions_) {
        if (c.owner == client.entity) remove_npc(c.entity);
    }
    std::erase_if(companions_, [&](const ActiveCompanion& c) { return c.owner == client.entity; });
    client.spec.active_companion = 0;
    client.spec.companion_entity = 0;
    if (destroyed) {
        client.spec.companion_cooldown = conquest_ ? companion_match_death_cooldown : companion_death_cooldown;
    }
    ++client.spec.revision;
    client.spec.sync_tick = 0;
}

void GameServer::companion_died(uint32_t npc_entity) {
    for (const ActiveCompanion& c : companions_) {
        if (c.entity != npc_entity) continue;
        if (Client* owner = find(c.owner)) {
            notify(owner->entity, std::format("{} has fallen", c.name));
            // Taken out of the world once its fall has shown (next tick).
            owner->spec.companion_cooldown = conquest_ ? companion_match_death_cooldown : companion_death_cooldown;
        }
        return;
    }
}

double GameServer::companion_hit(uint32_t npc_entity, uint32_t attacker, double raw, DamageType type) const {
    for (const ActiveCompanion& c : companions_) {
        if (c.entity != npc_entity) continue;
        if (attacker == c.owner) return 0;
        if (const Client* owner = find(c.owner); owner && attacker != 0) {
            if (const Client* shooter = find(attacker); shooter && !is_enemy(*shooter, c.owner)) return 0;
        }
        const CompanionTemplate* tpl = config_.specialists ? config_.specialists->companion(c.template_id) : nullptr;
        // Droids take bonus electro (ion) damage (docs/PROFESSIONS.md).
        if (tpl && tpl->kind == CompanionKind::droid && type == DamageType::electro) return raw * 1.5;
        return raw;
    }
    return raw;
}

void GameServer::update_companions(double dt) {
    for (size_t i = 0; i < companions_.size();) {
        ActiveCompanion& c = companions_[i];
        Client* owner = find(c.owner);
        Npc* npc = find_npc(c.entity);
        if (owner == nullptr || npc == nullptr || npc->life != Life::alive ||
            (owner->life == Life::waiting && conquest_)) {
            const bool destroyed = npc != nullptr && npc->life != Life::alive;
            if (npc != nullptr) remove_npc(c.entity);
            if (owner != nullptr) {
                owner->spec.active_companion = 0;
                owner->spec.companion_entity = 0;
                if (destroyed) {
                    owner->spec.companion_cooldown = std::max(
                        owner->spec.companion_cooldown, conquest_ ? companion_match_death_cooldown : companion_death_cooldown);
                }
                ++owner->spec.revision;
                owner->spec.sync_tick = 0;
            }
            companions_.erase(companions_.begin() + std::ptrdiff_t(i));
            continue;
        }
        ++i;
        c.next_attack = std::max(0.0, c.next_attack - dt);
        c.overcharge = std::max(0.0, c.overcharge - dt);
        c.recall_cooldown = std::max(0.0, c.recall_cooldown - dt);
        c.pulse -= dt;
        const Vec3 me = npc->state.position;
        const Vec3 home = owner->state.position;
        // How big it is: it keeps its flank, not its middle, near its owner.
        const CompanionTemplate* tpl = config_.specialists ? config_.specialists->companion(c.template_id) : nullptr;
        const double size = tpl ? tpl->radius : 0.5;
        // Too far from its owner: brought back to them.
        if (flat_distance(me, home) > companion_leash && c.recall_cooldown <= 0) {
            MoveState s = npc->state;
            s.position = world_.clamp(home + Vec3{0.9 + size * 0.71, 0, 0.9 + size * 0.71});
            s.position.y = world_.ground_at(s.position.x, s.position.z, home.y + 1, 0.5).height;
            if (s.position.y < -1e8) s.position.y = home.y;
            s.velocity = {};
            npc->state = s;
            c.recall_cooldown = 2;
            c.target = 0;
            continue;
        }
        // Its target: the one it was told to attack, or (following or
        // guarding) whatever its owner is fighting, or an enemy that comes near.
        const auto target_alive = [&](uint32_t e) {
            if (e == 0) return false;
            if (const Client* t = find(e)) return t->life == Life::alive && is_enemy(*owner, e) && !hidden_from(owner->entity, e);
            if (const Npc* t = find_npc(e)) return t->life == Life::alive && !t->hidden && is_enemy(*owner, e);
            return false;
        };
        if (!target_alive(c.target)) {
            c.target = 0;
            if (c.mode == CompanionMode::attack) c.mode = CompanionMode::follow;
        }
        const Vec3 anchor = c.mode == CompanionMode::guard ? c.guard_at : home;
        if (c.target == 0 && c.stats.role != CompanionRole::support) {
            double best = conquest_ ? 18.0 : 25.0;
            for (const Npc& n : npcs_) {
                if (n.actor_kind < 2 || n.actor_kind == 4 || n.hidden || n.life != Life::alive) continue;
                const bool fighting_owner = n.last_attacker == owner->entity && tick_ - n.last_damaged_tick < 150;
                const double d = flat_distance(n.state.position, anchor);
                if (fighting_owner && d < best) {
                    best = d;
                    c.target = n.entity;
                }
            }
            if (conquest_) {
                for (const Client& t : clients_) {
                    const double d = flat_distance(t.state.position, anchor);
                    if (t.life == Life::alive && d < best && is_enemy(*owner, t.entity) && !hidden_from(owner->entity, t.entity)) {
                        best = d;
                        c.target = t.entity;
                    }
                }
            }
        }
        // Support: heal its owner and their side nearby. Recon: spot enemies.
        if (c.pulse <= 0) {
            c.pulse = c.stats.role == CompanionRole::support ? 3.0 : 2.0;
            if (c.stats.role == CompanionRole::support) {
                for (Client& t : clients_) {
                    if (t.life == Life::alive && flat_distance(t.state.position, me) <= 8 &&
                        (&t == owner || (conquest_ ? allied(t.team, owner->team) : true))) {
                        heal(t, 25 * (c.overcharge > 0 ? c.overcharge_factor : 1));
                    }
                }
            } else if (c.stats.role == CompanionRole::recon) {
                const int team = field_team(*owner);
                for (const Client& t : clients_) {
                    if (t.life == Life::alive && flat_distance(t.state.position, me) <= 25 && is_enemy(*owner, t.entity) &&
                        !hidden_from(owner->entity, t.entity)) {
                        mark_entity(t.entity, team, owner->entity, 3, net::FieldMark::marked);
                    }
                }
            }
        }
        // Steering: toward the target, the guard point, or its owner's side.
        MoveInput input;
        input.yaw = npc->state.yaw;
        Vec3 goal = anchor;
        double stop = c.mode == CompanionMode::guard ? 1.0 : 2.5 + size;
        Vec3 target_at = me;
        if (c.target != 0) {
            if (const Client* tc = find(c.target)) target_at = tc->state.position;
            else if (const Npc* tn = find_npc(c.target)) target_at = tn->state.position;
            goal = target_at;
            stop = c.stats.attack_range * 0.8;
        }
        const Vec3 d{goal.x - me.x, 0, goal.z - me.z};
        const double dist = std::hypot(d.x, d.z);
        if (dist > stop) {
            input.yaw = float(std::atan2(-d.x, -d.z));
            input.forward = 127;
            if (dist > stop + 6) input.buttons = MoveInput::sprint;
        }
        npc->input = input;
        npc->pose = 0;
        // Attacking: within reach, its hit lands (a droid's needs a clear line).
        if (c.target != 0 && c.next_attack <= 0 && c.stats.damage > 0 &&
            flat_distance(target_at, me) <= c.stats.attack_range + 0.5) {
            const Vec3 eye = me + Vec3{0, 1.0, 0};
            const Vec3 aim = target_at + Vec3{0, 1.0, 0};
            const bool clear = c.stats.attack_range < 4 || !shot_hit(eye, aim);
            if (clear) {
                c.next_attack = c.stats.attack_cooldown;
                const double amount = c.stats.damage * (c.overcharge > 0 ? c.overcharge_factor : 1);
                // Its owner gets the credit (and the creature's attention).
                if (Client* tc = find(c.target)) {
                    damage(*tc, amount, c.stats.damage_type, owner->entity);
                } else if (Npc* tn = find_npc(c.target)) {
                    damage_npc(*tn, amount, c.stats.damage_type, owner->entity);
                }
                if (c.stats.attack_range >= 4) {
                    impacts_.push_back({c.entity, 0, aim, {0, 1, 0}, true});
                }
                npc->pose = 2;
                npc->input.yaw = float(std::atan2(-(target_at.x - me.x), -(target_at.z - me.z)));
                earn_specialist_xp(*owner, c.template_id.starts_with("droid_") ? "droid_operations" : "creature_handler", 2);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Kills: bounties, pilots, companions

void GameServer::specialist_kill(uint32_t victim, uint32_t killer, bool npc) {
    if (npc) companion_died(victim);
    Client* hunter = find(killer);
    if (hunter == nullptr) return;
    // A kill from a vehicle: the Pilot's (licensed, in a match).
    if (!npc && conquest_ && hunter->state.vehicle >= 0 && specialist_level(*hunter, "pilot") > 0) {
        earn_specialist_xp(*hunter, "pilot", 40);
    }
    // A kill on a marked enemy: the marker's too.
    for (const Mark& m : marks_) {
        if (m.entity == victim && m.owner != killer && m.kind == net::FieldMark::marked) {
            if (Client* marker = find(m.owner)) {
                const char* profession = specialist_level(*marker, "squad_leader") > 0 ? "squad_leader" : "bounty_hunter";
                earn_specialist_xp(*marker, profession, 10);
            }
        }
    }
    for (auto it = hunter->spec.contracts.begin(); it != hunter->spec.contracts.end(); ++it) {
        if ((it->kind == ContractKind::npc_mark || it->kind == ContractKind::pvp_mark) && it->target_entity == victim) {
            hunter->credits += it->reward;
            save_economy(*hunter);
            notify(hunter->entity, std::format("Bounty collected: {} (+{} credits)", it->target_name, it->reward));
            earn_specialist_xp(*hunter, it->profession, it->xp);
            if (it->kind == ContractKind::pvp_mark) {
                if (Client* mark = find(victim)) notify(mark->entity, std::format("{} collected the bounty on you", hunter->name));
            }
            hunter->spec.contracts.erase(it);
            ++hunter->spec.revision;
            hunter->spec.sync_tick = 0;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// The tick

// A character's timed effects: damage over time, then time running out. A
// dead character's are gone.
void GameServer::update_effects(Client& client, double dt) {
    update_perks(client);   // (triggered perks' held conditions)
    update_charge(client);  // (a perk's shoulder charge)
    SpecialistState& spec = client.spec;
    // Inspiration counts down while its character plays; a death in the open
    // world ends it, one in a match doesn't (docs/PROFESSIONS.md).
    if (client.life != Life::alive) spec.second_wind_used = false;  // (each life its own)
    if (!spec.inspiration.id.empty()) {
        spec.inspiration.remaining -= dt;
        if (spec.inspiration.remaining <= 0 || (!conquest_ && client.life == Life::dead)) spec.inspiration = {};
    }
    sync_inspiration(client);
    if (client.life != Life::alive) {
        if (!spec.effects.empty()) {
            const bool had_stats = effects_gear(spec.effects).has_value();
            spec.effects.clear();
            if (had_stats) refresh_effect_gear(client);
            ++spec.revision;
            spec.sync_tick = 0;
        }
        return;
    }
    for (const TimedEffect& e : spec.effects) {
        if (e.damage_per_second > 0) damage(client, e.damage_per_second * dt, e.damage_type, e.source);
        // A heal over time (a medpack's; a vehicle guard's heal_per_second is its share of damage).
        if (e.heal_per_second > 0 && (e.flags & effect_flag::vehicle_guard) == 0) heal(client, e.heal_per_second * dt);
    }
    if (client.life != Life::alive) return;
    const size_t before = spec.effects.size();
    if (tick_effects(spec.effects, dt)) refresh_effect_gear(client);
    if (spec.effects.size() != before) {
        ++spec.revision;
        spec.sync_tick = 0;
    }
    // A loadout rebuilt elsewhere (a level, a save) lost its effects' gear: put it back.
    if (!client.custom_loadouts.empty() && effects_gear(spec.effects).has_value()) {
        const Loadout& lo = loadout_of(client);
        if (std::none_of(lo.gear.begin(), lo.gear.end(), [](const Gear& g) { return g.slot == "effects"; })) {
            refresh_effect_gear(client);
        }
    }
}

// Snares, toxins, tranquilizers and hits' effects on NPCs.
void GameServer::update_npc_effects(double dt) {
    for (NpcEffect& e : npc_effects_) {
        e.remaining -= dt;
        if (e.dps > 0) {
            if (Npc* n = find_npc(e.entity)) damage_npc(*n, e.dps * dt, e.type, e.source);
        }
    }
    std::erase_if(npc_effects_, [&](const NpcEffect& e) { return e.remaining <= 0 || find_npc(e.entity) == nullptr; });
    for (auto it = npc_base_speed_.begin(); it != npc_base_speed_.end();) {
        Npc* n = find_npc(it->first);
        if (n == nullptr) {
            it = npc_base_speed_.erase(it);
            continue;
        }
        double factor = 1;
        bool any = false;
        for (const NpcEffect& e : npc_effects_) {
            if (e.entity == it->first) {
                factor = std::min(factor, std::clamp(e.speed, 0.05, 1.0));
                any = true;
            }
        }
        n->walk_speed = it->second.first * factor;
        n->run_speed = it->second.second * factor;
        it = any ? std::next(it) : npc_base_speed_.erase(it);
    }
}

void GameServer::update_specialists(double dt) {
    const SpecialistBook* book = config_.specialists;
    if (book == nullptr) {
        // No abilities or companions here, but hits still leave their effects (an ammo's).
        for (Client& client : clients_) update_effects(client, dt);
        update_npc_effects(dt);
        return;
    }
    specialist_clock_ += dt;

    // Characters: their effects, channels and cooldowns.
    for (Client& client : clients_) {
        SpecialistState& spec = client.spec;
        if (tick_ % 15 == client.entity % 15) refresh_abilities(client);
        for (auto& [id, left] : spec.cooldowns) left = std::max(0.0, left - dt);
        spec.companion_cooldown = std::max(0.0, spec.companion_cooldown - dt);
        update_effects(client, dt);
        if (client.life != Life::alive) {
            if (!spec.channel.empty()) spec.channel.clear();
            spec.performing = false;
        } else {
            if (!spec.channel.empty()) {
                spec.channel_remaining -= dt;
                if (flat_distance(client.state.position, spec.channel_from) > 1.0 || client.state.vehicle >= 0) {
                    spec.channel.clear();
                    notify(client.entity, "Interrupted");
                    ++spec.revision;
                    spec.sync_tick = 0;
                } else if (spec.channel_remaining <= 0) {
                    finish_channel(client);
                }
            }
            if (spec.performing && (client.state.vehicle >= 0 || std::hypot(client.state.velocity.x, client.state.velocity.z) > 1.5)) {
                spec.performing = false;
                notify(client.entity, "You stop performing");
                ++spec.revision;
                spec.sync_tick = 0;
            }
        }
        if (!spec.contracts.empty() && tick_ % 10 == client.entity % 10) update_contracts(client);
        if (spec.acked != spec.revision && (spec.sync_tick == 0 || tick_ - spec.sync_tick >= sync_resend_ticks)) {
            send_specialist_sync(client);
        }
        if (spec.dirty && tick_ % 150 == client.entity % 150) save_specialist_data(client);
    }

    // Performances: every pulse, the audience builds Inspiration (a band of
    // performers, and a flourish, faster); a gig counts with an audience.
    for (Client& performer : clients_) {
        if (!performer.spec.performing || performer.life != Life::alive) continue;
        performer.spec.perform_pulse -= dt;
        if (performer.spec.perform_pulse > 0) continue;
        performer.spec.perform_pulse = performance_pulse;
        int band = 0;
        for (const Client& other : clients_) {
            if (other.spec.performing && flat_distance(other.state.position, performer.state.position) <= band_radius) ++band;
        }
        const ProfessionStanding st = entertainer_standing(performer);
        const auto has = [&](std::string_view flag) { return granted(performer, "entertainer", flag); };
        const bool flourish = std::any_of(performer.spec.effects.begin(), performer.spec.effects.end(),
                                          [](const TimedEffect& e) { return e.id == "ent_flourish"; });
        if (band == 1 && has("soloist")) band = 2;  // alone, as fast as a band of two
        const double gain =
            inspiration_gain(performance_pulse, band, has("band_leader"), flourish) * (has("dancer") ? 1.25 : 1.0);
        const double radius = has("musician") ? 20.0 : performance_radius;
        int audience = 0;
        for (Client& listener : clients_) {
            if (&listener == &performer || listener.spec.performing || listener.life != Life::alive ||
                flat_distance(listener.state.position, performer.state.position) > radius) {
                continue;
            }
            if (inspire(listener, performer, gain)) ++audience;
        }
        earn_specialist_xp(performer, "entertainer", 6 + 6 * std::min(audience, 8) + 3 * (band - 1));
        if (st.trained >= 1) {  // tips, even to an empty room
            performer.credits += performance_tips(st.level, audience) * (has("showstopper") ? 3 : has("encore") ? 2 : 1);
            save_economy(performer);
        }
        for (Contract& c : performer.spec.contracts) {
            if (c.kind == ContractKind::gig && audience > 0 && flat_distance(performer.state.position, c.waypoint) <= c.radius) {
                c.progress += performance_pulse;
            }
        }
    }

    // Squad Cohesion: allies near a Squad Leader with Rally recover stamina faster.
    cohesion_pulse_ -= dt;
    if (cohesion_pulse_ <= 0) {
        cohesion_pulse_ = 1.0;
        for (const Client& leader : clients_) {
            if (leader.life != Life::alive || specialist_level(leader, "squad_leader") <= 0) continue;
            const Loadout& lo = loadout_of(leader);
            const auto rallies = [&](const char* id) {
                return std::find(lo.abilities.begin(), lo.abilities.end(), id) != lo.abilities.end();
            };
            if (!rallies("sl_rally") && !rallies("sl_steady_rally")) continue;
            for (Client& ally : clients_) {
                if (&ally == &leader || ally.life != Life::alive) continue;
                if (flat_distance(ally.state.position, leader.state.position) > cohesion_radius) continue;
                if (conquest_ && !allied(ally.team, leader.team)) continue;
                TimedEffect e;
                e.id = "squad_cohesion";
                e.name = "Squad Cohesion";
                e.source = leader.entity;
                e.remaining = e.duration = 2.5;
                e.modifiers.push_back({Stat::energy_restore, 0, 1.08});
                give_effect(ally, std::move(e));
            }
        }
    }

    // Deployables.
    for (Deployable& d : deployables_) {
        d.remaining -= dt;
        d.pulse -= dt;
        const Client* owner = find(d.owner);
        if (owner == nullptr) {
            d.remaining = 0;
            continue;
        }
        const auto friendly = [&](const Client& c) { return conquest_ ? allied(c.team, d.team) : !bounty_pvp_allowed(d.owner, c.entity); };
        switch (d.kind) {
        case DeployKind::heal_aura:
        case DeployKind::camp: {
            double healed = 0;
            for (Client& c : clients_) {
                if (c.life == Life::alive && friendly(c) && flat_distance(c.state.position, d.position) <= d.radius) {
                    const double before = c.health;
                    heal(c, d.amount * dt);
                    healed += c.health - before;
                    // A camp: loadout swaps there are quick.
                    if (d.kind == DeployKind::camp && c.switching_to >= 0) c.switch_remaining = std::min(c.switch_remaining, 0.5);
                }
            }
            if (healed > 0 && tick_ % 30 == 0) {
                if (Client* o = find(d.owner)) {
                    const AbilityDef* a = book->ability(d.ability);
                    earn_specialist_xp(*o, a ? a->profession : "doctor", 2);
                }
            }
            break;
        }
        case DeployKind::buff_aura:
            // A holo-emitter: its owner's performance, recorded: anyone near
            // builds Inspiration from it at half speed.
            if (d.pulse <= 0) {
                d.pulse = performance_pulse;
                const double gain = inspiration_gain(performance_pulse, 1, false, false) * 0.5;
                int audience = 0;
                for (Client& c : clients_) {
                    if (c.life == Life::alive && !c.spec.performing && friendly(c) &&
                        flat_distance(c.state.position, d.position) <= d.radius && inspire(c, *owner, gain)) {
                        ++audience;
                    }
                }
                if (Client* o = find(d.owner); o != nullptr && audience > 0) {
                    earn_specialist_xp(*o, "entertainer", 2 * std::min(audience, 8));
                }
            }
            break;
        case DeployKind::sensor:
            if (d.pulse <= 0) {
                d.pulse = 1.0;
                Client* o = find(d.owner);
                for (const Client& c : clients_) {
                    if (c.life == Life::alive && flat_distance(c.state.position, d.position) <= d.radius && o &&
                        is_enemy(*o, c.entity)) {
                        mark_entity(c.entity, d.team, d.owner, 1.5, net::FieldMark::marked);
                    }
                }
                if (!conquest_) {
                    for (const Npc& n : npcs_) {
                        if (n.actor_kind == 3 && !n.hidden && n.life == Life::alive &&
                            flat_distance(n.state.position, d.position) <= d.radius) {
                            mark_entity(n.entity, d.team, d.owner, 1.5, net::FieldMark::marked);
                        }
                    }
                }
            }
            break;
        case DeployKind::trap: {
            if (d.spent) break;
            Client* o = find(d.owner);
            if (o == nullptr) break;
            const AbilityDef* a = book->ability(d.ability);
            uint32_t caught = 0;
            for (const Client& c : clients_) {
                if (c.life == Life::alive && flat_distance(c.state.position, d.position) <= d.radius && is_enemy(*o, c.entity)) {
                    caught = c.entity;
                    break;
                }
            }
            if (caught == 0) {
                for (const Npc& n : npcs_) {
                    if (n.actor_kind >= 2 && n.actor_kind != 4 && !n.hidden && n.life == Life::alive &&
                        flat_distance(n.state.position, d.position) <= d.radius) {
                        caught = n.entity;
                        break;
                    }
                }
            }
            if (caught == 0) break;
            d.spent = true;
            d.remaining = std::min(d.remaining, 0.5);
            if (Client* c = find(caught)) {
                damage(*c, d.amount, DamageType::ballistic, d.owner);
                if (a) {
                    TimedEffect e;
                    e.id = a->id;
                    e.name = a->name;
                    e.source = d.owner;
                    e.remaining = e.duration = 3;
                    e.modifiers = a->modifiers;
                    e.flags = effect_flag::debuff;
                    give_effect(*c, std::move(e));
                }
            } else if (Npc* n = find_npc(caught)) {
                damage_npc(*n, d.amount, DamageType::ballistic, d.owner);
                npc_base_speed_.try_emplace(caught, n->walk_speed, n->run_speed);
                npc_effects_.push_back({caught, d.ability, 3, a ? modifier_multiply(a->modifiers, Stat::run_speed) : 0.3, 0,
                                        DamageType::ballistic, d.owner, false});
            }
            mark_entity(caught, d.team, d.owner, 5, net::FieldMark::marked);
            if (o) earn_specialist_xp(*o, a ? a->profession : "ranger", a ? a->xp : 20);
            break;
        }
        case DeployKind::spawn_beacon:
        case DeployKind::none:
            break;
        }
    }
    std::erase_if(deployables_, [](const Deployable& d) { return d.remaining <= 0; });

    // Hazards: clouds that hurt the other side.
    for (Hazard& h : hazards_) {
        h.remaining -= dt;
        if (h.damage_per_second <= 0) continue;
        Client* owner = find(h.owner);
        if (owner == nullptr) continue;
        for (Client& c : clients_) {
            if (c.life == Life::alive && flat_distance(c.state.position, h.position) <= h.radius && is_enemy(*owner, c.entity)) {
                damage(c, h.damage_per_second * dt, h.damage_type, h.owner);
            }
        }
        for (Npc& n : npcs_) {
            if (n.actor_kind >= 2 && !n.hidden && n.life == Life::alive &&
                flat_distance(n.state.position, h.position) <= h.radius && is_enemy(*owner, n.entity)) {
                damage_npc(n, h.damage_per_second * dt, h.damage_type, h.owner);
            }
        }
    }
    std::erase_if(hazards_, [](const Hazard& h) { return h.remaining <= 0; });

    // Call-ins landing.
    for (PendingStrike& s : strikes_) {
        s.remaining -= dt;
        if (s.remaining > 0) continue;
        Client* owner = find(s.owner);
        explosions_.push_back({s.owner, net::droid_explosion_weapon, s.position, {0, 1, 0}, float(s.radius)});
        if (owner == nullptr) continue;
        for (Client& c : clients_) {
            const double d = flat_distance(c.state.position, s.position);
            if (c.life == Life::alive && d <= s.radius && is_enemy(*owner, c.entity)) {
                damage(c, s.damage * (1 - 0.5 * d / s.radius), s.damage_type, s.owner);
            }
        }
        for (Npc& n : npcs_) {
            const double d = flat_distance(n.state.position, s.position);
            if (n.actor_kind >= 2 && !n.hidden && n.life == Life::alive && d <= s.radius && is_enemy(*owner, n.entity)) {
                damage_npc(n, s.damage * (1 - 0.5 * d / s.radius), s.damage_type, s.owner);
            }
        }
    }
    std::erase_if(strikes_, [](const PendingStrike& s) { return s.remaining <= 0; });

    // Marks fade.
    for (Mark& m : marks_) m.remaining -= dt;
    std::erase_if(marks_, [&](const Mark& m) {
        if (m.remaining <= 0) return true;
        if (const Client* c = find(m.entity)) return c->life != Life::alive;
        if (const Npc* n = find_npc(m.entity)) return n->life != Life::alive || n->hidden;
        return true;
    });

    update_npc_effects(dt);

    update_companions(dt);
}

// ---------------------------------------------------------------------------
// Contracts

void GameServer::offer_contracts(Client& client, size_t terminal) {
    if (terminal >= mission_terminals_.size() || conquest_) return;
    const net::MissionTerminal& t = mission_terminals_[terminal];
    if (flat_distance(client.state.position, t.position) > t.radius + 2) {
        notify(client.entity, "Too far from the terminal");
        return;
    }
    SpecialistState& spec = client.spec;
    spec.offers.clear();
    spec.offer_source = t.name;
    const Vec3 at = client.state.position;
    const auto dressed = [&](Contract c) {
        c.id = next_contract_++;
        c.expires = specialist_clock_ + contract_lifetime;
        return c;
    };
    const auto pick_npcs = [&](uint8_t kind, double lo, double hi, size_t want) {
        std::vector<const Npc*> found;
        for (const Npc& n : npcs_) {
            const double d = flat_distance(n.state.position, at);
            if (n.actor_kind == kind && !n.hidden && n.life == Life::alive && d >= lo && d <= hi) found.push_back(&n);
        }
        // A spread of them: shuffled, the first `want`.
        for (size_t i = found.size(); i > 1; --i) std::swap(found[i - 1], found[size_t(random_unit() * double(i)) % i]);
        if (found.size() > want) found.resize(want);
        return found;
    };
    if (t.kind == "bounty") {
        for (const Npc* n : pick_npcs(3, 120, 1500, 3)) {
            Contract c;
            c.kind = ContractKind::npc_mark;
            c.profession = "bounty_hunter";
            c.target_name = n->name;
            c.target_entity = n->entity;
            c.waypoint = n->state.position;
            const double d = flat_distance(n->state.position, at);
            const int tier = std::clamp(int(n->max_health / 120), 1, 5);
            c.title = std::format("Bounty: {}", n->name);
            c.reward = contract_reward(c.kind, tier, d);
            c.xp = 120 + tier * 40;
            spec.offers.push_back(dressed(c));
        }
        for (const Client& other : clients_) {
            if (&other == &client || !other.spec.bounty_flag || other.life == Life::waiting) continue;
            Contract c;
            c.kind = ContractKind::pvp_mark;
            c.profession = "bounty_hunter";
            c.target_name = other.name;
            c.target_entity = other.entity;
            c.waypoint = other.state.position;
            c.title = std::format("Bounty: {} (player, opted in)", other.name);
            c.reward = contract_reward(c.kind, 1, 0);
            c.xp = 400;
            spec.offers.push_back(dressed(c));
            if (spec.offers.size() >= 5) break;
        }
    } else if (t.kind == "scout") {
        for (const Npc* n : pick_npcs(2, 150, 900, 3)) {
            Contract c;
            c.kind = ContractKind::track;
            c.profession = "ranger";
            c.target_name = n->name;
            c.target_entity = n->entity;
            c.waypoint = n->state.position;
            c.radius = 15;
            c.title = std::format("Track: {}", n->name);
            c.reward = contract_reward(c.kind, 1, flat_distance(n->state.position, at));
            c.xp = 150;
            spec.offers.push_back(dressed(c));
        }
    } else if (t.kind == "entertainer") {
        // Gigs at the hub's busy places: by its quest people and service points.
        std::vector<std::pair<std::string, Vec3>> venues;
        for (const net::Vendor& v : vendors_) venues.emplace_back(v.name, v.position);
        for (const net::QuestNpc& q : quest_npcs_) venues.emplace_back(q.name, q.position);
        for (size_t i = 0; i < 3 && !venues.empty(); ++i) {
            const size_t k = size_t(random_unit() * double(venues.size())) % venues.size();
            Contract c;
            c.kind = ContractKind::gig;
            c.profession = "entertainer";
            c.target_name = venues[k].first;
            c.waypoint = venues[k].second;
            c.radius = 15;
            c.needed = 180;  // with an audience
            c.title = std::format("Gig: perform by {} for three minutes, to an audience", venues[k].first);
            c.reward = contract_reward(c.kind, 1, 0);
            c.xp = 200;
            spec.offers.push_back(dressed(c));
            venues.erase(venues.begin() + std::ptrdiff_t(k));
        }
    } else if (t.kind == "artisan" && config_.crafting != nullptr && config_.catalog != nullptr) {
        // Crafting orders: a few of what the professions it takes orders for
        // make, brought back here. Fewer of the harder ones, paid more each.
        std::vector<const SchematicDef*> made;
        for (const SchematicDef& s : config_.crafting->schematics()) {
            const bool taken = std::find(artisan_order_professions.begin(), artisan_order_professions.end(),
                                         s.profession) != artisan_order_professions.end();
            if (taken && !s.blueprint && config_.catalog->find(s.output_item) != nullptr) made.push_back(&s);
        }
        for (size_t i = 0; i < 3 && !made.empty(); ++i) {
            const size_t k = size_t(random_unit() * double(made.size())) % made.size();
            const SchematicDef& s = *made[k];
            const int tier = std::clamp(s.required_level / 25 + 1, 1, 5);
            const int count = std::max(1, 4 - tier);
            Contract c;
            c.kind = ContractKind::craft_order;
            c.profession = s.profession;
            c.item = s.output_item;
            c.target_name = t.name;
            c.waypoint = t.position;
            c.radius = t.radius + 2;
            c.needed = count;
            c.title = std::format("Crafting order: {} {} for {}", count, item_name(s.output_item), t.name);
            c.reward = contract_reward(c.kind, tier, 0) * count;
            c.xp = s.base_xp * count;
            spec.offers.push_back(dressed(c));
            made.erase(made.begin() + std::ptrdiff_t(k));
        }
    }
    if (t.kind == "general" || t.kind == "bounty" || t.kind == "artisan") {
        for (int i = 0; i < 2; ++i) {
            // A drop point out in the dunes, 300 to 900 m off.
            const double angle = random_unit() * 2 * std::numbers::pi;
            const double dist = 300 + random_unit() * 600;
            Vec3 p = world_.clamp(at + Vec3{std::cos(angle) * dist, 0, std::sin(angle) * dist});
            p.y = world_.terrain().height_at(p.x, p.z);
            Contract c;
            c.kind = ContractKind::contraband;
            c.profession = "smuggler";
            c.target_name = "a drop point";
            c.waypoint = p;
            c.radius = 10;
            c.title = "Contraband run: a package to a drop point, no questions asked";
            c.reward = contract_reward(c.kind, 1, flat_distance(p, at));
            c.xp = 180;
            spec.offers.push_back(dressed(c));
        }
    }
    if (spec.offers.empty()) notify(client.entity, "No contracts for you here right now");
    ++spec.revision;
    spec.sync_tick = 0;
}

void GameServer::update_contracts(Client& client) {
    SpecialistState& spec = client.spec;
    const ProfessionBook* professions = config_.professions;
    bool changed = false;
    for (auto it = spec.contracts.begin(); it != spec.contracts.end();) {
        Contract& c = *it;
        bool done = false, failed = false;
        std::string why;
        if (specialist_clock_ > c.expires) {
            failed = true;
            why = "it expired";
        } else if (c.kind == ContractKind::contraband) {
            done = flat_distance(client.state.position, c.waypoint) <= c.radius && client.life == Life::alive;
        } else if (c.kind == ContractKind::gig) {
            done = c.progress >= c.needed;
        } else if (c.kind == ContractKind::craft_order) {
            // How many of its item are held (not in a loadout); handed in at the terminal.
            int have = 0;
            for (const ItemInstance& inst : client.items) {
                if (inst.def_id == c.item && !item_equipped_in_loadouts(client.char_loadouts, inst.id)) {
                    have += std::max(1, inst.quantity);
                }
            }
            const double held = std::min(double(have), c.needed);
            if (held != c.progress) {
                c.progress = held;
                changed = true;
            }
            done = have >= int(c.needed) && client.life == Life::alive &&
                   flat_distance(client.state.position, c.waypoint) <= c.radius;
        } else if (c.kind == ContractKind::track) {
            const Npc* n = find_npc(c.target_entity);
            if (n == nullptr || n->hidden || n->life != Life::alive) {
                failed = true;
                why = "the trail went cold";
            } else {
                c.waypoint = n->state.position;  // a ranger keeps the trail
                done = flat_distance(client.state.position, n->state.position) <= c.radius && client.life == Life::alive;
            }
        } else {
            const bool gone = c.kind == ContractKind::pvp_mark
                                  ? find(c.target_entity) == nullptr
                                  : (find_npc(c.target_entity) == nullptr || find_npc(c.target_entity)->life != Life::alive);
            if (gone) {
                failed = true;
                why = "the mark is gone";
            }
        }
        // Only those who have the profession may profit (it must be learned, level 1).
        if (done && professions != nullptr) {
            const ProfessionStanding st = profession_standing(*professions, client.professions, c.profession);
            if (st.trained < 1) {
                done = false;
                failed = true;
                const ProfessionDef* def = professions->find(c.profession);
                why = std::format("you aren't a trained {}", def ? def->name : c.profession);
            }
        }
        if (done && c.kind == ContractKind::craft_order && store_ != nullptr) {
            // The order's items handed over (a stack only in part, if that's enough).
            int left = int(c.needed);
            for (const ItemInstance& inst : std::vector<ItemInstance>(client.items)) {
                if (left <= 0) break;
                if (inst.def_id != c.item || item_equipped_in_loadouts(client.char_loadouts, inst.id)) continue;
                const int q = std::max(1, inst.quantity);
                if (q > left) {
                    store_->update_item_quantity(inst.id, q - left);
                    left = 0;
                } else {
                    store_->remove_item(inst.id);
                    left -= q;
                }
            }
            client.items = store_->list_items(client.character);
            ++client.inventory_rev;
            client.last_sync_tick = 0;
        }
        if (done && c.kind == ContractKind::gig && granted(client, "entertainer", "encore")) {
            c.reward += c.reward / 2;  // an Encore's
        }
        if (done && c.kind == ContractKind::craft_order) {
            c.reward = order_pay(client, c.profession, c.reward);  // a crafter's order_pay perks
        }
        if (done) {
            client.credits += c.reward;
            save_economy(client);
            notify(client.entity, std::format("Contract complete: {} (+{} credits)", c.title, c.reward));
            earn_specialist_xp(client, c.profession, c.xp);
            it = spec.contracts.erase(it);
            changed = true;
        } else if (failed) {
            notify(client.entity, std::format("Contract failed: {} ({})", c.title, why));
            it = spec.contracts.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed || tick_ % 90 == 0) {
        ++spec.revision;
        spec.sync_tick = 0;
    }
}

// ---------------------------------------------------------------------------
// The bazaar

void GameServer::load_bazaar() {
    if (bazaar_loaded_) return;
    bazaar_loaded_ = true;
    if (store_ == nullptr) return;
    try {
        bazaar_ = parse_listings(store_->get_extra(0, "bazaar_listings"));
        bazaar_sales_ = parse_sales(store_->get_extra(0, "bazaar_sales"));
    } catch (const std::exception& error) {
        say(std::format("bazaar load error: {}", error.what()));
    }
    for (const BazaarListing& l : bazaar_) next_listing_ = std::max(next_listing_, l.id + 1);
    // Listings past their time go back to their sellers.
    const int64_t now = epoch_seconds();
    bool expired = false;
    std::erase_if(bazaar_, [&](const BazaarListing& l) {
        if (now - l.listed_at < bazaar_listing_days * 86400) return false;
        ItemInstance back = l.item;
        back.id = 0;
        back.owner_id = l.seller;
        try {
            store_->grant_item(back);
        } catch (const std::exception&) {
        }
        expired = true;
        return true;
    });
    if (expired) save_bazaar();
}

void GameServer::save_bazaar() {
    if (store_ == nullptr) return;
    try {
        store_->set_extra(0, "bazaar_listings", format_listings(bazaar_));
        store_->set_extra(0, "bazaar_sales", format_sales(bazaar_sales_));
    } catch (const std::exception& error) {
        say(std::format("bazaar save error: {}", error.what()));
    }
}

void GameServer::bazaar_action(Client& client, const net::SpecialistRequest& request) {
    using R = net::SpecialistRequest;
    if (conquest_ || store_ == nullptr || client.character == 0) return;
    if (service_near(client, "bazaar") == nullptr) {
        notify(client.entity, "You need to be at a bazaar terminal");
        return;
    }
    load_bazaar();
    SpecialistState& spec = client.spec;
    const ProfessionBook* professions = config_.professions;
    const ProfessionStanding merchant = professions ? profession_standing(*professions, client.professions, "merchant")
                                                    : ProfessionStanding{};
    const auto item_name = [&](const ItemInstance& item) {
        if (!item.custom_name.empty()) return item.custom_name;
        const ItemDef* def = config_.catalog ? config_.catalog->find(item.def_id) : nullptr;
        return def ? def->display_name : item.def_id;
    };
    const auto refresh_view = [&]() {
        spec.bazaar_view.clear();
        for (const BazaarListing& l : bazaar_) {
            if (listing_matches(l, item_name(l.item), spec.bazaar_query)) spec.bazaar_view.push_back(l);
        }
        std::sort(spec.bazaar_view.begin(), spec.bazaar_view.end(),
                  [](const BazaarListing& a, const BazaarListing& b) { return a.price < b.price; });
        if (spec.bazaar_view.size() > 40) spec.bazaar_view.resize(40);
        spec.bazaar_prices.clear();
        // Market Analytics (Merchant 50, trained): price trends for what's shown.
        if (merchant.level >= 50 && merchant.trained >= 50) {
            for (const BazaarListing& l : spec.bazaar_view) {
                if (std::none_of(spec.bazaar_prices.begin(), spec.bazaar_prices.end(),
                                 [&](const auto& p) { return p.first == l.item.def_id; })) {
                    spec.bazaar_prices.emplace_back(l.item.def_id, price_stats(bazaar_sales_, l.item.def_id));
                }
            }
        }
        ++spec.revision;
        spec.sync_tick = 0;
    };
    switch (request.action) {
    case R::bazaar_browse:
        spec.bazaar_query = request.text.substr(0, 64);
        refresh_view();
        return;
    case R::bazaar_list: {
        if (merchant.trained < 1) {
            notify(client.entity, "Selling on the bazaar takes Merchant training (any trainer of merchants)");
            return;
        }
        const auto it = std::find_if(client.items.begin(), client.items.end(),
                                     [&](const ItemInstance& i) { return i.id == request.item_id; });
        if (it == client.items.end()) return;
        const auto equipped = [&](uint64_t id) {
            for (const CharacterLoadout& cl : client.char_loadouts) {
                if (cl.primary_id == id || cl.sidearm_id == id || cl.secondary_id == id) return true;
                for (const auto& [slot, gid] : cl.gear_ids) if (gid == id) return true;
                for (const auto& [slot, gid] : cl.appearance_ids) if (gid == id) return true;
            }
            return false;
        };
        if (equipped(it->id)) {
            notify(client.entity, "Unequip it first");
            return;
        }
        const int64_t price = std::clamp<int64_t>(request.amount, 1, bazaar_max_price);
        const auto mine = std::count_if(bazaar_.begin(), bazaar_.end(),
                                        [&](const BazaarListing& l) { return l.seller == client.character; });
        if (mine >= bazaar_listing_limit(merchant.level)) {
            notify(client.entity, std::format("You have {} listings up, all your shop slots", mine));
            return;
        }
        BazaarListing l;
        l.id = next_listing_++;
        l.seller = client.character;
        l.seller_name = client.name;
        l.shop = merchant.trained >= 25 ? client.name + "'s Shop" : std::string();
        l.item = *it;
        l.price = price;
        l.listed_at = epoch_seconds();
        if (!store_->remove_item(it->id)) return;
        client.items = store_->list_items(client.character);
        ++client.inventory_rev;
        client.last_sync_tick = 0;
        bazaar_.push_back(std::move(l));
        save_bazaar();
        notify(client.entity, std::format("Listed {} for {} credits (fee on sale: {}%)", item_name(bazaar_.back().item), price,
                                          int(std::round(bazaar_fee_rate(merchant.level) * 100))));
        refresh_view();
        return;
    }
    case R::bazaar_buy: {
        const auto it = std::find_if(bazaar_.begin(), bazaar_.end(),
                                     [&](const BazaarListing& l) { return int64_t(l.id) == request.amount; });
        if (it == bazaar_.end()) {
            notify(client.entity, "That listing is gone");
            refresh_view();
            return;
        }
        if (it->seller == client.character) {
            notify(client.entity, "That's your own listing");
            return;
        }
        if (client.credits < it->price) {
            notify(client.entity, "Not enough credits");
            return;
        }
        const BazaarListing sold = *it;
        bazaar_.erase(it);
        client.credits -= sold.price;
        save_economy(client);
        ItemInstance bought = sold.item;
        bought.id = 0;
        bought.owner_id = client.character;
        store_->grant_item(bought);
        client.items = store_->list_items(client.character);
        ++client.inventory_rev;
        client.last_sync_tick = 0;
        // The seller's share: here, or saved for when they're back.
        Client* seller = nullptr;
        for (Client& c : clients_) {
            if (c.character == sold.seller) seller = &c;
        }
        int seller_level = 0;
        if (seller != nullptr && professions) {
            seller_level = profession_standing(*professions, seller->professions, "merchant").level;
        } else if (professions) {
            try {
                seller_level = professions->level_for(profession_xp(store_->list_professions(sold.seller), "merchant"));
            } catch (const std::exception&) {
            }
        }
        const int64_t share = sold.price - bazaar_fee(sold.price, seller_level);
        const int64_t xp = std::max<int64_t>(5, sold.price / 20);
        if (seller != nullptr) {
            seller->credits += share;
            save_economy(*seller);
            notify(seller->entity, std::format("Sold {} to {} (+{} credits)", item_name(sold.item), client.name, share));
            earn_specialist_xp(*seller, "merchant", xp);
        } else {
            try {
                if (auto record = store_->find(sold.seller)) {
                    store_->update_economy(record->id, record->credits + share, record->faction_points, record->experience,
                                           record->active_loadout);
                    award_profession_xp(record->name, {{"merchant", xp}}, 0);
                }
            } catch (const std::exception& error) {
                say(std::format("bazaar payout error: {}", error.what()));
            }
        }
        bazaar_sales_.push_back({sold.item.def_id, sold.price, epoch_seconds()});
        if (bazaar_sales_.size() > bazaar_history) {
            bazaar_sales_.erase(bazaar_sales_.begin(), bazaar_sales_.begin() + std::ptrdiff_t(bazaar_sales_.size() - bazaar_history));
        }
        save_bazaar();
        notify(client.entity, std::format("Bought {} for {} credits", item_name(sold.item), sold.price));
        refresh_view();
        return;
    }
    case R::bazaar_cancel: {
        const auto it = std::find_if(bazaar_.begin(), bazaar_.end(), [&](const BazaarListing& l) {
            return int64_t(l.id) == request.amount && l.seller == client.character;
        });
        if (it == bazaar_.end()) return;
        ItemInstance back = it->item;
        back.id = 0;
        back.owner_id = client.character;
        bazaar_.erase(it);
        store_->grant_item(back);
        client.items = store_->list_items(client.character);
        ++client.inventory_rev;
        client.last_sync_tick = 0;
        save_bazaar();
        refresh_view();
        return;
    }
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// The datapad: companions, the lab, droids, slicing, flags

void GameServer::datapad_action(Client& client, const net::SpecialistRequest& request) {
    using R = net::SpecialistRequest;
    const SpecialistBook* book = config_.specialists;
    const ProfessionBook* professions = config_.professions;
    SpecialistState& spec = client.spec;
    const auto standing = [&](const char* id) {
        return professions ? profession_standing(*professions, client.professions, id) : ProfessionStanding{};
    };
    const auto changed = [&]() {
        spec.dirty = true;
        ++spec.revision;
        spec.sync_tick = 0;
    };
    const auto record = [&](int64_t id) -> CompanionRecord* {
        for (CompanionRecord& r : spec.companions) {
            if (int64_t(r.id) == id) return &r;
        }
        return nullptr;
    };
    const auto owned = [&](uint64_t id) -> ItemInstance* {
        for (ItemInstance& i : client.items) {
            if (i.id == id) return &i;
        }
        return nullptr;
    };
    // Mounts, eggs, tissue and droids aren't graded (their quality is the
    // companion's own, in its payload).
    const auto grant = [&](const std::string& def_id, const std::string& name, const std::string& colors) {
        if (store_ == nullptr || client.character == 0) return;
        store_->grant_item({0, client.character, def_id, ItemGrade::standard, name, colors});
        client.items = store_->list_items(client.character);
        ++client.inventory_rev;
        client.last_sync_tick = 0;
    };
    const auto consume = [&](uint64_t item_id) {
        if (store_ == nullptr || !store_->remove_item(item_id)) return false;
        scrub_item_from_loadouts(client, item_id);
        client.items = store_->list_items(client.character);
        ++client.inventory_rev;
        client.last_sync_tick = 0;
        return true;
    };
    const bool world = !conquest_ && store_ != nullptr && client.character != 0;
    switch (request.action) {
    case R::companion_rename:
        if (CompanionRecord* r = record(request.amount)) {
            const std::string name = clean_name(request.text);
            if (!name.empty()) {
                r->name = name;
                changed();
            }
        }
        return;
    case R::companion_release:
        if (!world) return;
        if (CompanionRecord* r = record(request.amount)) {
            if (spec.active_companion == r->id) dismiss_companion(client, false);
            notify(client.entity, std::format("You release {}", r->name));
            std::erase_if(spec.companions, [&](const CompanionRecord& c) { return int64_t(c.id) == request.amount; });
            for (CharacterLoadout& cl : client.char_loadouts) {
                if (cl.companion == std::to_string(request.amount)) {
                    cl.companion.clear();
                    store_->save_loadout(client.character, cl);
                }
            }
            changed();
        }
        return;
    case R::companion_mount: {
        if (!world || book == nullptr) return;
        CompanionRecord* r = record(request.amount);
        const CompanionTemplate* tpl = r ? book->companion(r->template_id) : nullptr;
        if (tpl == nullptr || !tpl->mountable || r->mount) {
            notify(client.entity, "That companion can't be trained as a mount");
            return;
        }
        const ProfessionStanding ch = standing("creature_handler");
        if (ch.level < 50 || ch.trained < 50) {
            notify(client.entity, "Training mounts takes the Mounts milestone (Creature Handler 50)");
            return;
        }
        r->mount = true;
        grant(tpl->mount_item, r->name, "{}");
        notify(client.entity, std::format("{} is trained to ride: equip it in your Mount slot", r->name));
        earn_specialist_xp(client, "creature_handler", 150);
        changed();
        return;
    }
    case R::engineer_egg: {
        if (!world) return;
        const ProfessionStanding bio = standing("bio_engineer");
        if (bio.level < 25 || bio.trained < 25) {
            notify(client.entity, "Engineering eggs takes the Creature Eggs milestone (Bio-Engineer 25)");
            return;
        }
        const auto sample = [&](int64_t id) -> const DnaSample* {
            for (const DnaSample& s : spec.samples) if (int64_t(s.id) == id) return &s;
            return nullptr;
        };
        const DnaSample* a = sample(request.amount);
        const DnaSample* b = request.item_id != 0 && int64_t(request.item_id) != request.amount ? sample(int64_t(request.item_id)) : nullptr;
        const CompanionTemplate* tpl = a && book ? book->companion(a->template_id) : nullptr;
        if (tpl == nullptr) return;
        CompanionRecord egg = engineer_egg(*a, b, bio.level, uint16_t(1 + random_unit() * 65534));
        egg.name = tpl->name;
        const uint64_t a_id = a->id, b_id = b ? b->id : 0;
        std::erase_if(spec.samples, [&](const DnaSample& s) { return s.id == a_id || (b_id && s.id == b_id); });
        grant("bio_creature_egg", std::format("{} Egg", tpl->name), companion_payload(egg));
        notify(client.entity, std::format("Engineered a {} egg (quality {})", tpl->name, int(std::round(egg.quality * 100))));
        earn_specialist_xp(client, "bio_engineer", 120 + tpl->tier * 20);
        changed();
        return;
    }
    case R::grow_tissue: {
        if (!world) return;
        const bool additive = request.id == "additive";
        const ProfessionStanding bio = standing("bio_engineer");
        const int need = additive ? 75 : 50;
        if (bio.level < need || bio.trained < need) {
            notify(client.entity, std::format("That takes Bio-Engineer {}", need));
            return;
        }
        const auto it = std::find_if(spec.samples.begin(), spec.samples.end(),
                                     [&](const DnaSample& s) { return int64_t(s.id) == request.amount; });
        if (it == spec.samples.end()) return;
        spec.samples.erase(it);
        grant(additive ? "bio_stim_additive" : "bio_tissue_organic", "", "{}");
        earn_specialist_xp(client, "bio_engineer", 80);
        changed();
        return;
    }
    case R::hatch_egg:
    case R::register_droid: {
        if (!world || book == nullptr) return;
        ItemInstance* item = owned(request.item_id);
        const char* wanted = request.action == R::hatch_egg ? "bio_creature_egg" : "companion_droid";
        if (item == nullptr || item->def_id != wanted) return;
        auto r = parse_companion(json_value(item->color_json, "companion"));
        const CompanionTemplate* tpl = r ? book->companion(r->template_id) : nullptr;
        if (tpl == nullptr) return;
        const bool droid = tpl->kind == CompanionKind::droid;
        const ProfessionStanding st = standing(droid ? "droid_operations" : "creature_handler");
        if (st.trained < 1) {
            notify(client.entity, droid ? "Registering droids takes Droid Operations training"
                                        : "Hatching eggs takes Creature Handler training");
            return;
        }
        if (spec.companions.size() >= max_companions) {
            notify(client.entity, "Your datapad is full");
            return;
        }
        if (!consume(item->id)) return;
        r->id = spec.next_companion_id++;
        r->mount = false;
        if (r->name.empty()) r->name = tpl->name;
        spec.companions.push_back(*r);
        notify(client.entity, std::format("{} joins your datapad", r->name));
        earn_specialist_xp(client, droid ? "droid_operations" : "creature_handler", 50);
        changed();
        return;
    }
    case R::assemble_droid: {
        if (!world || book == nullptr || config_.crafting == nullptr) return;
        const CompanionTemplate* chassis = book->companion(request.id);
        if (chassis == nullptr || chassis->kind != CompanionKind::droid) return;
        const ProfessionStanding de = standing("droid_engineer");
        if (de.level < chassis->build_level || de.trained < 1) {
            notify(client.entity, std::format("Assembling a {} needs Droid Engineer {}", chassis->name, chassis->build_level));
            return;
        }
        std::vector<std::string> modules;
        std::string item;
        for (const char c : request.text + ",") {
            if (c == ',') {
                if (!item.empty()) modules.push_back(item);
                item.clear();
            } else if (c != ' ') {
                item += c;
            }
        }
        if (const std::string why = check_droid_modules(*book, *chassis, modules, de.level); !why.empty()) {
            notify(client.entity, why);
            return;
        }
        // What it takes from the Material Vault, by category; better materials, a better droid.
        std::vector<std::pair<std::string, int>> needs = chassis->materials;
        for (const std::string& m : modules) {
            if (const DroidModule* mod = book->module(m)) needs.insert(needs.end(), mod->materials.begin(), mod->materials.end());
        }
        std::vector<MaterialStack> vault = client.materials;
        double quality_sum = 0;
        int units = 0;
        for (const auto& [category, amount] : needs) {
            int left = amount;
            for (MaterialStack& stack : vault) {
                if (left <= 0) break;
                const ResourceDef* res = config_.crafting->find_resource(stack.resource_id);
                if (res == nullptr || stack.amount <= 0) continue;
                const auto cat = resource_category_from_name(category);
                if (!cat || res->category != *cat) continue;
                const int take = int(std::min<int64_t>(left, stack.amount));
                stack.amount -= take;
                left -= take;
                quality_sum += take * res->stats[size_t(ResourceStat::quality)] / 1000.0;
                units += take;
            }
            if (left > 0) {
                notify(client.entity, std::format("Not enough {} in your Material Vault ({} more)", category, left));
                return;
            }
        }
        std::erase_if(vault, [](const MaterialStack& m) { return m.amount <= 0; });
        client.materials = std::move(vault);
        ++client.crafting_rev;
        client.crafting_sync_tick = 0;
        save_materials(client);
        CompanionRecord r;
        r.template_id = chassis->id;
        r.name = chassis->name;
        r.origin = "assembled";
        r.quality = std::clamp((units ? quality_sum / units : 0.5) * 0.7 + de.level * 0.003, 0.0, 1.0);
        r.health = 0.9 + r.quality * 0.25;
        r.damage = 0.9 + r.quality * 0.25;
        r.modules = modules;
        grant("companion_droid", std::format("{} (Q{})", chassis->name, int(std::round(r.quality * 100))),
              companion_payload(r));
        notify(client.entity, std::format("Assembled a {}", chassis->name));
        earn_specialist_xp(client, "droid_engineer", 100 + chassis->tier * 50 + int64_t(modules.size()) * 25);
        return;
    }
    case R::slice_item: {
        if (!world || book == nullptr) return;
        const SliceDef* slice = book->slice(request.id);
        ItemInstance* item = owned(request.item_id);
        const ItemDef* def = item && config_.catalog ? config_.catalog->find(item->def_id) : nullptr;
        if (slice == nullptr || def == nullptr) return;
        const ProfessionStanding smg = standing("smuggler");
        if (smg.level < std::max(50, slice->level) || smg.trained < 50) {
            notify(client.entity, std::format("{} needs Smuggler {} (Gear Slicing trained)", slice->name, std::max(50, slice->level)));
            return;
        }
        const bool weapon = def->kind == ItemKind::weapon;
        const bool armor = def->kind == ItemKind::armor;
        if ((slice->applies == "weapon" && !weapon) || (slice->applies == "armor" && !armor)) {
            notify(client.entity, std::format("{} is for {}", slice->name, slice->applies));
            return;
        }
        if (client.credits < slice_cost) {
            notify(client.entity, std::format("Slicing takes {} credits of parts", slice_cost));
            return;
        }
        ItemInstance sliced = *item;
        set_item_slice(sliced, slice->id);
        if (!store_->update_item_colors(item->id, sliced.color_json)) return;
        client.credits -= slice_cost;
        save_economy(client);
        item->color_json = sliced.color_json;
        rebuild_client_loadouts(client);
        client.stats = stats_for(loadout_of(client), client.species, config_.base_stats);
        ++client.inventory_rev;
        client.last_sync_tick = 0;
        notify(client.entity, std::format("Sliced {} into {}", slice->name, def->display_name));
        earn_specialist_xp(client, "smuggler", 60 + slice->level);
        return;
    }
    case R::choose_inspiration:
        // What to build in a performer's audience ("" whatever they offer).
        if (!request.id.empty() && (config_.specialists == nullptr || !config_.specialists->inspiration(request.id))) return;
        spec.inspiration_choice = request.id;
        changed();
        return;
    case R::bounty_flag:
        if (conquest_) return;
        spec.bounty_flag = request.amount == 1;
        notify(client.entity, spec.bounty_flag ? "You're open to bounty hunters: they can hunt you, and you can fight back"
                                               : "You're no longer open to bounty hunters");
        changed();
        return;
    case R::supply_contract: {
        if (!world || config_.catalog == nullptr) return;
        const ProfessionStanding m = standing("merchant");
        if (m.level < 100 || m.trained < 100) {
            notify(client.entity, "Faction supply contracts take Merchant 100");
            return;
        }
        const net::Quartermaster* qm = nullptr;
        for (const net::Quartermaster& q : quartermasters_) {
            if (flat_distance(q.position, client.state.position) <= q.radius + 1.5) qm = &q;
        }
        ItemInstance* item = owned(request.item_id);
        const ItemDef* def = item ? config_.catalog->find(item->def_id) : nullptr;
        if (qm == nullptr || def == nullptr) {
            notify(client.entity, "Stand at a quartermaster to supply them");
            return;
        }
        const int64_t value = std::max<int64_t>(10, def->sell_value);
        if (!consume(item->id)) return;
        client.credits += value * 3 / 2;
        client.faction_points += int64_t(std::llround(double(std::max<int64_t>(1, value / 10)) * client.stats.faction_gain));
        save_economy(client);
        notify(client.entity, std::format("Supplied {} with {} (+{} credits, +{} FP)", qm->name, def->display_name, value * 3 / 2,
                                          std::max<int64_t>(1, value / 10)));
        earn_specialist_xp(client, "merchant", std::max<int64_t>(10, value / 5));
        return;
    }
    case R::set_companion_slot: {
        if (!world || client.loadout >= client.char_loadouts.size()) return;
        CharacterLoadout& cl = client.char_loadouts[client.loadout];
        if (request.amount == 0) {
            cl.companion.clear();
        } else if (record(request.amount)) {
            cl.companion = std::to_string(request.amount);
        } else {
            return;
        }
        store_->save_loadout(client.character, cl);
        rebuild_client_loadouts(client);
        ++client.inventory_rev;
        client.last_sync_tick = 0;
        changed();
        return;
    }
    default:
        return;
    }
}

// Pharmaceuticals and droid repair kits used from the inventory.
bool GameServer::use_specialist_item(Client& client, const ItemDef& def, uint64_t item_id) {
    if (def.id == "droid_repair_kit") {
        ActiveCompanion* comp = nullptr;
        for (ActiveCompanion& c : companions_) {
            if (c.owner == client.entity && c.template_id.starts_with("droid_")) comp = &c;
        }
        Npc* npc = comp ? find_npc(comp->entity) : nullptr;
        if (npc == nullptr || npc->health >= npc->max_health) {
            notify(client.entity, "No damaged droid fielded");
            return true;
        }
        if (store_ == nullptr || !store_->remove_item(item_id)) return true;
        npc->health = std::min(npc->max_health, npc->health + npc->max_health * 0.4);
    } else if (def.id == "doc_antidote" || def.id == "doc_performance_stim") {
        if (store_ == nullptr || !store_->remove_item(item_id)) return true;
        if (def.id == "doc_antidote") {
            if (remove_effects(client.spec.effects, effect_flag::debuff)) refresh_effect_gear(client);
        } else {
            TimedEffect e;
            e.id = "doc_stim";
            e.name = "Performance Stim";
            e.remaining = e.duration = 60;
            e.modifiers = {{Stat::energy_restore, 0, 1.15}, {Stat::max_energy, 0, 1.08}};
            give_effect(client, std::move(e));
        }
        ++client.spec.revision;
        client.spec.sync_tick = 0;
    } else {
        return false;
    }
    client.items = store_->list_items(client.character);
    ++client.inventory_rev;
    client.last_sync_tick = 0;
    notify(client.entity, "Used " + def.display_name);
    return true;
}

bool GameServer::spawn_at_beacon(Client& client, size_t loadout) {
    if (!conquest_) return false;
    const Deployable* beacon = nullptr;
    for (const Deployable& d : deployables_) {
        if (d.kind == DeployKind::spawn_beacon && allied(d.team, client.team)) beacon = &d;
    }
    if (beacon == nullptr || conquest_->battlefield().posts.empty()) return false;
    const Vec3 at = beacon->position;
    spawn_at_post(client, loadout, 0);
    if (client.life != Life::alive) return false;
    const double angle = random_unit() * 2 * std::numbers::pi;
    MoveState s = client.state;
    s.position = world_.clamp(at + Vec3{std::cos(angle) * 1.5, 0, std::sin(angle) * 1.5});
    s.position.y = world_.ground_at(s.position.x, s.position.z, at.y + 1.5, client.stats.move.step_up).height;
    if (s.position.y < -1e8) s.position.y = at.y;
    client.state = s;
    client.history.clear();
    return true;
}

// ---------------------------------------------------------------------------
// The sync

void GameServer::send_specialist_sync(Client& client) {
    const SpecialistState& spec = client.spec;
    net::SpecialistSync sync;
    sync.revision = spec.revision;
    for (const std::string& id : spec.abilities) {
        const auto it = spec.cooldowns.find(id);
        sync.abilities.push_back({id, float(it != spec.cooldowns.end() ? it->second : 0)});
    }
    for (const TimedEffect& e : spec.effects) {
        sync.effects.push_back({e.id, e.name, float(e.remaining), float(e.duration), e.flags});
    }
    sync.companions = spec.companions;
    if (conquest_ && !loadout_of(client).companion.empty()) {
        if (auto r = parse_companion(loadout_of(client).companion)) sync.companions = {*r};
    }
    sync.active_companion = spec.active_companion;
    sync.companion_entity = spec.companion_entity;
    sync.companion_cooldown = float(spec.companion_cooldown);
    for (const ActiveCompanion& c : companions_) {
        if (c.owner == client.entity) sync.companion_mode = uint8_t(c.mode);
    }
    if (client.loadout < client.char_loadouts.size()) {
        sync.companion_slot = std::strtoull(client.char_loadouts[client.loadout].companion.c_str(), nullptr, 10);
    }
    sync.samples = spec.samples;
    sync.contracts = spec.contracts;
    for (Contract& c : sync.contracts) c.expires = std::max(0.0, c.expires - specialist_clock_);
    sync.offers = spec.offers;
    for (Contract& c : sync.offers) c.expires = std::max(0.0, c.expires - specialist_clock_);
    sync.offer_source = spec.offer_source;
    sync.bazaar = spec.bazaar_view;
    for (const BazaarListing& l : bazaar_) {
        if (l.seller == client.character && client.character != 0) sync.my_listings.push_back(l);
    }
    sync.prices = spec.bazaar_prices;
    sync.performing = spec.performing;
    sync.inspiration_choice = spec.inspiration_choice;
    sync.bounty_flag = spec.bounty_flag;
    sync.channel = spec.channel;
    sync.channel_remaining = float(spec.channel_remaining);
    link_.send(client.endpoint, net::encode(sync));
    client.spec.sync_tick = std::max<uint32_t>(1, tick_);
}

} // namespace swm::core
