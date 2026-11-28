// GameServer's quests: the journal each character keeps, what advances it
// (kills, places, talking, carrying), and the quest log clients are sent.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

#include "swbfo/core/server.h"

namespace swbfo::core {
namespace {

constexpr double talk_reach = 6;  // meters from a quest NPC
constexpr size_t comms_kept = 4;  // QuestSync::comms

double flat_distance(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

} // namespace

void GameServer::set_quests(const QuestBook* book, std::vector<net::QuestNpc> npcs,
                            std::vector<net::MissionTerminal> terminals) {
    quest_book_ = book;
    quest_npcs_ = std::move(npcs);
    mission_terminals_ = std::move(terminals);
    for (Client& client : clients_) {
        ++client.quest_rev;
        client.quest_sync_tick = 0;
    }
}

const std::vector<QuestProgress>* GameServer::quests_of(uint32_t entity) const {
    const Client* client = find(entity);
    return client ? &client->quests : nullptr;
}

int GameServer::held_count(const Client& client, const std::string& def_id) const {
    return int(std::count_if(client.items.begin(), client.items.end(),
                             [&](const ItemInstance& item) { return item.def_id == def_id; }));
}

std::string GameServer::item_name(const std::string& def_id) const {
    const ItemDef* def = config_.catalog ? config_.catalog->find(def_id) : nullptr;
    return def ? def->display_name : def_id;
}

void GameServer::give_item(Client& client, const std::string& def_id, ItemGrade grade) {
    if (store_ != nullptr && client.character != 0) {
        store_->grant_item({0, client.character, def_id, grade, "", "{}"});
        client.items = store_->list_items(client.character);
    } else {
        uint64_t next_id = 1;
        for (const ItemInstance& item : client.items) next_id = std::max(next_id, item.id + 1);
        client.items.push_back({next_id, 0, def_id, grade, "", "{}"});
    }
    ++client.inventory_rev;
    client.last_sync_tick = 0;
}

void GameServer::take_items(Client& client, const std::string& def_id, int count) {
    // The worst first: a delivery hands over what's worth least.
    std::vector<ItemInstance> matching;
    for (const ItemInstance& item : client.items) {
        if (item.def_id == def_id) matching.push_back(item);
    }
    std::sort(matching.begin(), matching.end(),
              [](const ItemInstance& a, const ItemInstance& b) { return a.grade < b.grade; });
    bool loadouts = false;
    for (int i = 0; i < count && i < int(matching.size()); ++i) {
        const uint64_t id = matching[size_t(i)].id;
        if (store_ != nullptr && client.character != 0) {
            store_->remove_item(id);
        }
        std::erase_if(client.items, [id](const ItemInstance& item) { return item.id == id; });
        const auto before = client.char_loadouts;
        scrub_item_from_loadouts(client, id);
        loadouts = loadouts || before != client.char_loadouts;
    }
    if (store_ != nullptr && client.character != 0) {
        client.items = store_->list_items(client.character);
    }
    if (loadouts) {
        rebuild_client_loadouts(client);
    }
    ++client.inventory_rev;
    client.last_sync_tick = 0;
}

void GameServer::save_economy(Client& client) {
    if (store_ == nullptr || client.character == 0) return;
    try {
        store_->update_economy(client.character, client.credits, client.faction_points, client.experience,
                               int(client.loadout));
    } catch (const std::exception& error) {
        say(std::format("economy save error for {}: {}", client.name, error.what()));
    }
}

void GameServer::apply_quests(Client& client, QuestEffects effects) {
    if (quest_book_ == nullptr) return;
    std::vector<std::string> notes;
    bool changed = false;
    // Comms go to the client's comm window, with the journal.
    const auto hear = [&](const std::vector<QuestEffects::Comm>& from) {
        for (const QuestEffects::Comm& comm : from) {
            client.comms.push_back({++client.comm_serial, comm.quest_id, comm.from, comm.text});
            changed = true;
        }
        if (client.comms.size() > comms_kept) {
            client.comms.erase(client.comms.begin(), client.comms.end() - std::ptrdiff_t(comms_kept));
        }
    };
    // An effect may finish what the next step waits on (a delivery's reward
    // completes a collect step, a step ends where the next one is): look
    // again, a few times at most.
    for (int pass = 0; pass < 6; ++pass) {
        bool inventory = false;
        bool granted = false;
        for (const QuestEffects::Note& note : effects.notes) {
            if (!note.text.starts_with("Quest complete: ")) {  // said below, with the reward
                notes.push_back(note.text);
            }
        }
        hear(effects.comms);
        for (const std::string& item : effects.grant) {
            give_item(client, item);
            notes.push_back("Found " + item_name(item));
            inventory = true;
        }
        for (const auto& [item, count] : effects.take) {
            take_items(client, item, count);
            notes.push_back(count > 1 ? std::format("Handed over {} x{}", item_name(item), count)
                                      : "Handed over " + item_name(item));
            inventory = true;
        }
        for (const std::string& id : effects.completed) {
            const QuestDef* def = quest_book_->find(id);
            if (def == nullptr) continue;
            client.credits += def->reward.credits;
            client.experience += def->reward.experience;
            route_xp(client, def->reward.experience);
            client.faction_points += int64_t(std::llround(double(def->reward.faction_points) * client.stats.faction_gain));
            save_economy(client);
            // City reputation: with the giver's town (a mission's: its target's).
            {
                std::optional<Vec3> from;
                if (def->mission) {
                    for (const QuestStep& step : def->steps) {
                        if (step.where) {
                            from = step.where;
                            break;
                        }
                    }
                } else {
                    const auto giver = std::find_if(quest_npcs_.begin(), quest_npcs_.end(),
                                                    [&](const net::QuestNpc& n) { return n.name == def->giver; });
                    if (giver != quest_npcs_.end()) from = giver->position;
                }
                if (const TownArea* town = from ? town_at(*from) : nullptr) {
                    earn_town_reputation(client, *town, quest_town_reputation(def->level, def->mission));
                }
            }
            for (const std::string& item : def->reward.items) {
                give_item(client, item);
                inventory = true;
            }
            // Quest items have done their job.
            for (const QuestStep& step : def->steps) {
                const ItemDef* item = config_.catalog ? config_.catalog->find(step.target) : nullptr;
                if (item != nullptr && item->kind == ItemKind::quest && held_count(client, step.target) > 0) {
                    take_items(client, step.target, held_count(client, step.target));
                    inventory = true;
                }
            }
            std::string reward;
            if (def->reward.credits > 0) reward += std::format(" +{} Cr", def->reward.credits);
            if (def->reward.experience > 0) reward += std::format(" +{} XP", def->reward.experience);
            if (def->reward.faction_points > 0) reward += std::format(" +{} FP", def->reward.faction_points);
            for (const std::string& item : def->reward.items) reward += ", " + item_name(item);
            std::string line = "Quest complete: " + def->title + (reward.empty() ? "" : " (" + reward.substr(1) + ")");
            if (!def->done_text.empty()) {
                line = def->done_text + "\n" + line;
            }
            notes.push_back(std::move(line));
            say(std::format("{} completed {}", client.name, def->id));
            // The chain goes on by itself (the Legacy's hand-offs and comm-given quests).
            for (const std::string& next_id : def->next) {
                const QuestDef* next = quest_book_->find(next_id);
                if (next == nullptr) {
                    say(std::format("{} names Next = {}, which isn't a quest", def->id, next_id));
                    continue;
                }
                if (!quest_book_->can_accept(*next, client.quests)) {
                    notify(client.entity, std::format("Make room in your journal to take on {}", next->title));
                    continue;
                }
                QuestEffects started = quest_book_->accept(client.quests, *next);
                for (const QuestEffects::Note& note : started.notes) notes.push_back(note.text);
                hear(started.comms);
                granted = true;
            }
        }
        changed = changed || effects.changed || granted;
        if (!effects.changed && !inventory && !granted) {
            break;
        }
        QuestEffects more = quest_book_->on_position(client.quests, client.state.position);
        QuestEffects held = quest_book_->on_inventory(
            client.quests, [&](const std::string& item) { return held_count(client, item); });
        more.notes.insert(more.notes.end(), held.notes.begin(), held.notes.end());
        more.completed.insert(more.completed.end(), held.completed.begin(), held.completed.end());
        more.changed = more.changed || held.changed;
        effects = std::move(more);
    }
    if (changed) {
        ++client.quest_rev;
        client.quest_sync_tick = 0;
        if (store_ != nullptr && client.character != 0) {
            try {
                store_->save_quests(client.character, client.quests);
            } catch (const std::exception& error) {
                say(std::format("quest save error for {}: {}", client.name, error.what()));
            }
        }
    }
    if (!notes.empty()) {
        // The newest few, newest last (a notice is one line).
        std::string text;
        for (size_t i = notes.size() > 2 ? notes.size() - 2 : 0; i < notes.size(); ++i) {
            text += (text.empty() ? "" : "  |  ") + notes[i];
        }
        notify(client.entity, text);
    }
}

void GameServer::quest_kill(uint32_t killer, const KillTags& tags, Vec3 at) {
    Client* client = find(killer);
    if (client == nullptr || quest_book_ == nullptr || client->quests.empty()) return;
    apply_quests(*client, quest_book_->on_kill(client->quests, tags, at, [this] { return random_unit(); }));
}

void GameServer::quest_event(Client& client, const std::string& name) {
    if (quest_book_ == nullptr || client.quests.empty()) return;
    apply_quests(client, quest_book_->on_event(client.quests, name));
}

void GameServer::check_quest_positions() {
    if (quest_book_ == nullptr) return;
    for (Client& client : clients_) {
        // What's carried changed (looted, bought, sold): collect steps, and
        // the counts the log shows.
        if (client.inventory_rev != client.quest_inventory_rev) {
            client.quest_inventory_rev = client.inventory_rev;
            if (!client.quests.empty()) {
                ++client.quest_rev;
                client.quest_sync_tick = 0;
                QuestEffects held = quest_book_->on_inventory(
                    client.quests, [&](const std::string& item) { return held_count(client, item); });
                if (held.changed) {
                    apply_quests(client, std::move(held));
                }
            }
        }
        if (!client.quests.empty() && client.life == Life::alive) {
            QuestEffects effects = quest_book_->on_position(client.quests, client.state.position);
            if (effects.changed) {
                apply_quests(client, std::move(effects));
            }
        }
    }
}

void GameServer::handle_quest_request(Client& client, const net::QuestRequest& request) {
    if (quest_book_ == nullptr || conquest_) return;
    const auto held = [&](const std::string& item) { return held_count(client, item); };
    switch (request.action) {
    case net::QuestRequest::talk: {
        const auto npc = std::find_if(quest_npcs_.begin(), quest_npcs_.end(),
                                      [&](const net::QuestNpc& n) { return n.entity == request.target; });
        if (npc == quest_npcs_.end() || client.life != Life::alive) return;
        const MoveState* where = state_of(npc->entity);
        if (flat_distance(where ? where->position : npc->position, client.state.position) > talk_reach) return;
        QuestEffects talk_fx = quest_book_->on_talk(client.quests, npc->name, held);
        const bool completed_here = !talk_fx.completed.empty();
        apply_quests(client, std::move(talk_fx));
        if (npc->giver) {
            client.offers.clear();
            for (const QuestDef* def : quest_book_->offered_by(npc->name, client.quests)) {
                client.offers.push_back(def->id);
            }
            client.offer_source = npc->name;
            client.offer_note = client.offers.empty()
                                    ? (completed_here ? std::string("__completed__")
                                                      : quest_book_->why_none(npc->name, client.quests))
                                    : "";
            ++client.offer_serial;
            ++client.quest_rev;
            client.quest_sync_tick = 0;
        }
        break;
    }
    case net::QuestRequest::terminal: {
        if (request.target >= mission_terminals_.size() || client.life != Life::alive) return;
        const net::MissionTerminal& terminal = mission_terminals_[request.target];
        if (flat_distance(terminal.position, client.state.position) > terminal.radius + 1.5) return;
        client.offers.clear();
        if (terminal.kind.empty() || terminal.kind == "general") {
            for (const QuestDef* def :
                 quest_book_->missions(tick_ / config_.tick_rate, client.quests, 6, &terminal.position)) {
                client.offers.push_back(def->id);
            }
            client.offer_note.clear();
        } else {
            client.offer_note = std::format(
                "{} — Specialized {} contracts are coming soon.", terminal.name, terminal.kind);
        }
        client.offer_source = terminal.name;
        ++client.offer_serial;
        ++client.quest_rev;
        client.quest_sync_tick = 0;
        break;
    }
    case net::QuestRequest::accept: {
        if (std::find(client.offers.begin(), client.offers.end(), request.quest_id) == client.offers.end()) return;
        const QuestDef* def = quest_book_->find(request.quest_id);
        if (def == nullptr || !quest_book_->can_accept(*def, client.quests)) {
            notify(client.entity, def && def->mission ? std::format("You can take at most {} missions at a time",
                                                                    max_active_missions)
                                                      : "You can't take that quest now");
            return;
        }
        std::erase(client.offers, request.quest_id);
        apply_quests(client, quest_book_->accept(client.quests, *def));
        if (def->mission) quest_event(client, "mission");
        break;
    }
    case net::QuestRequest::abandon: {
        const QuestDef* def = quest_book_->find(request.quest_id);
        QuestEffects effects = quest_book_->abandon(client.quests, request.quest_id);
        if (def != nullptr && effects.changed) {
            for (const QuestStep& step : def->steps) {
                const ItemDef* item = config_.catalog ? config_.catalog->find(step.target) : nullptr;
                if (item != nullptr && item->kind == ItemKind::quest && held_count(client, step.target) > 0) {
                    take_items(client, step.target, held_count(client, step.target));
                }
            }
        }
        apply_quests(client, std::move(effects));
        break;
    }
    case net::QuestRequest::use: {
        if (client.life != Life::alive) return;
        apply_quests(client, quest_book_->on_use(client.quests, request.quest_id, int(request.target),
                                                 client.state.position));
        break;
    }
    }
}

std::string GameServer::step_text(const QuestStep& step) const {
    if (!step.text.empty()) return step.text;
    switch (step.kind) {
    case QuestStepKind::go_to: return "Travel to the marked location";
    case QuestStepKind::kill: return "Kill " + step.target;
    case QuestStepKind::collect: return "Collect " + item_name(step.target);
    case QuestStepKind::talk_to: return "Talk to " + step.npc;
    case QuestStepKind::deliver: return "Bring " + item_name(step.target) + " to " + step.npc;
    case QuestStepKind::event: return "Do: " + step.target;
    case QuestStepKind::use: return "Use the " + step.target;
    }
    return {};
}

net::QuestInfo GameServer::describe_quest(const Client& client, const QuestDef& def,
                                          const QuestProgress* progress) const {
    net::QuestInfo info;
    info.id = def.id;
    info.title = def.title;
    info.giver = def.giver;
    info.summary = def.summary;
    info.mission = def.mission;
    info.level = uint8_t(std::clamp(def.level, 1, 100));
    info.credits = def.reward.credits;
    info.experience = def.reward.experience;
    info.faction_points = def.reward.faction_points;
    info.items = def.reward.items;
    const size_t current = progress ? size_t(std::clamp(progress->step, 0, int(def.steps.size()) - 1)) : 0;
    const size_t shown = progress ? current + 1 : def.steps.size();
    for (size_t i = 0; i < shown && i < def.steps.size(); ++i) {
        std::string text = step_text(def.steps[i]);
        // A step on another planet says which (it can't be done, or shown, here).
        if (quest_book_ != nullptr && !quest_book_->here(def.steps[i])) {
            std::string planet = def.steps[i].planet;
            planet[0] = char(std::toupper(uint8_t(planet[0])));
            text += " (" + planet + ")";
        }
        info.steps.push_back(std::move(text));
    }
    if (def.steps.empty()) return info;
    const QuestStep& step = def.steps[current];
    info.step = uint8_t(current);
    info.needed = uint16_t(step.kind == QuestStepKind::go_to || step.kind == QuestStepKind::talk_to ? 0 : step.count);
    if (progress) {
        const int have = step.kind == QuestStepKind::collect || step.kind == QuestStepKind::deliver
                             ? held_count(client, step.target)
                         : step.kind == QuestStepKind::use ? QuestBook::objects_used(progress->count)
                                                           : progress->count;
        info.count = uint16_t(std::clamp(have, 0, int(info.needed)));
    }
    if (quest_book_ != nullptr && !quest_book_->here(step)) {
        return info;  // no waypoint or targets for a step on another planet
    }
    if (step.kind == QuestStepKind::use && progress) {
        // The objects not used yet; the waypoint is the nearest of them.
        double nearest = 1e300;
        for (size_t i = 0; i < step.spots.size(); ++i) {
            if (QuestBook::object_used(progress->count, int(i))) continue;
            const Vec3 spot = step.spots[i];
            info.objects.push_back({uint8_t(i), step.target, step.look,
                                    step.spot_heights ? spot : Vec3{spot.x, 0, spot.z}, step.spot_heights,
                                    float(step.radius)});
            if (const double d = flat_distance(spot, client.state.position); d < nearest) {
                nearest = d;
                info.has_waypoint = true;
                info.waypoint = step.spot_heights ? spot : Vec3{spot.x, 0, spot.z};
            }
        }
    } else if (step.where) {
        info.has_waypoint = true;
        info.waypoint = *step.where;
    } else if (!step.npc.empty()) {
        for (const net::QuestNpc& npc : quest_npcs_) {
            if (npc.name == step.npc) {
                const MoveState* state = state_of(npc.entity);
                info.has_waypoint = true;
                info.waypoint = state ? state->position : npc.position;
                break;
            }
        }
    }
    if (step.kind == QuestStepKind::talk_to || step.kind == QuestStepKind::deliver) {
        info.step_npc = step.npc;
    }
    // What to kill (or take the drop from): marked in the world, and the
    // nearest place they live is the waypoint when the step names none.
    const std::string& prey = step.kind == QuestStepKind::kill ? step.target
                              : step.kind == QuestStepKind::collect
                                  ? (!step.drop_from.empty() ? step.drop_from : step.track)
                                  : std::string();
    if (!prey.empty() && quest_book_ != nullptr) {
        info.targets = quest_book_->target_names(prey);
        if (step.where) {
            info.area_radius = float(step.radius);
        } else if (const auto nearest = quest_book_->nearest_of(prey, client.state.position)) {
            info.has_waypoint = true;
            info.waypoint = *nearest;
        }
    }
    return info;
}

void GameServer::send_quest_sync(Client& client) {
    net::QuestSync sync;
    sync.revision = client.quest_rev;
    if (quest_book_ != nullptr) {
        for (const QuestProgress& progress : client.quests) {
            const QuestDef* def = quest_book_->find(progress.id);
            if (def == nullptr) continue;
            if (progress.done) {
                sync.completed.push_back(def->title);
            } else {
                sync.active.push_back(describe_quest(client, *def, &progress));
            }
        }
        for (const std::string& id : client.offers) {
            if (const QuestDef* def = quest_book_->find(id)) {
                sync.offers.push_back(describe_quest(client, *def, nullptr));
            }
        }
    }
    sync.offer_source = client.offer_source;
    sync.offer_serial = client.offer_serial;
    sync.offer_note = client.offer_note;
    sync.comms = client.comms;
    if (quest_book_ != nullptr) {
        const int level = combat_level_of(client);
        for (const std::string& giver : quest_book_->givers()) {
            // Work worth doing now; work that's too easy for you is as good as none for now.
            const auto offered = quest_book_->offered_by(giver, client.quests);
            const bool worth = std::any_of(offered.begin(), offered.end(), [&](const QuestDef* def) {
                return def->level + quest_trivial_gap > level;
            });
            if (giver != "Mission Terminal" && worth) {
                sync.ready_givers.push_back(giver);
            } else if (giver != "Mission Terminal" && quest_book_->has_work_left(giver, client.quests)) {
                sync.waiting_givers.push_back(giver);
            }
        }
    }
    link_.send(client.endpoint, net::encode(sync));
    client.quest_sync_tick = tick_;
}

} // namespace swbfo::core
