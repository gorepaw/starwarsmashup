#include "swm/core/quest.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <numbers>
#include <sstream>
#include <stdexcept>

#include "swm/core/ini.h"
#include "swm/core/wildlife.h"

namespace swm::core {
namespace {

bool same_ci(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); });
}

std::string trim(std::string s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::vector<std::string> split_list(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string part;
    while (std::getline(in, part, ',')) {
        part = trim(part);
        if (!part.empty()) {
            out.push_back(part);
        }
    }
    return out;
}

// Galaxies "x z" (and an optional radius) to game space.
bool galaxies_point(const std::string& text, Vec3& out, double* radius = nullptr) {
    std::istringstream in(text);
    double x = 0, z = 0;
    if (!(in >> x >> z)) {
        return false;
    }
    out = {-x, 0, z};
    double r = 0;
    if (radius != nullptr && (in >> r)) {
        *radius = r;
    }
    return true;
}

double flat_distance(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

uint64_t mix(uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

std::string plural(const std::string& name) {
    if (name.empty() || name.back() == 's') {
        return name;
    }
    return name + "s";
}

bool target_matches(std::string_view target, const KillTags& tags) {
    if (target.empty()) {
        return false;
    }
    if (same_ci(target, "any")) return true;
    if (same_ci(target, "creature")) return !tags.bandit;
    if (same_ci(target, "bandit")) return tags.bandit;
    return same_ci(target, tags.type_id) || (!tags.gang.empty() && same_ci(target, tags.gang));
}

bool inside(const QuestStep& step, Vec3 at) {
    return !step.where || flat_distance(*step.where, at) <= step.radius;
}

QuestProgress* find_progress(std::vector<QuestProgress>& journal, std::string_view id) {
    for (QuestProgress& p : journal) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const QuestProgress* find_progress(const std::vector<QuestProgress>& journal, std::string_view id) {
    for (const QuestProgress& p : journal) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

} // namespace

std::optional<QuestStepKind> quest_step_kind_from_name(std::string_view name) {
    if (same_ci(name, "go_to") || same_ci(name, "goto")) return QuestStepKind::go_to;
    if (same_ci(name, "kill")) return QuestStepKind::kill;
    if (same_ci(name, "collect")) return QuestStepKind::collect;
    if (same_ci(name, "talk_to") || same_ci(name, "talk")) return QuestStepKind::talk_to;
    if (same_ci(name, "deliver")) return QuestStepKind::deliver;
    if (same_ci(name, "event") || same_ci(name, "do")) return QuestStepKind::event;
    return std::nullopt;
}

std::string_view quest_step_kind_name(QuestStepKind kind) {
    switch (kind) {
    case QuestStepKind::go_to: return "go_to";
    case QuestStepKind::kill: return "kill";
    case QuestStepKind::collect: return "collect";
    case QuestStepKind::talk_to: return "talk_to";
    case QuestStepKind::deliver: return "deliver";
    case QuestStepKind::event: return "event";
    }
    return "go_to";
}

std::string describe_direction(Vec3 from, Vec3 to) {
    const double east = -(to.x - from.x);
    const double north = to.z - from.z;
    const double distance = std::hypot(east, north);
    static constexpr const char* names[] = {"north", "north-east", "east", "south-east",
                                            "south", "south-west", "west", "north-west"};
    double bearing = std::atan2(east, north) * 180 / std::numbers::pi;  // 0 north, 90 east
    if (bearing < 0) bearing += 360;
    const char* direction = names[int(std::lround(bearing / 45)) % 8];
    if (distance < 1000) {
        return std::format("{} m {}", int(std::lround(distance / 10) * 10), direction);
    }
    return std::format("{:.1f} km {}", distance / 1000, direction);
}

void QuestBook::load(const std::filesystem::path& path) {
    const Ini ini = Ini::load(path);
    const std::string where = path.string();
    QuestDef* current = nullptr;
    for (const Ini::Section& s : ini.sections()) {
        if (s.name == "quest") {
            QuestDef def;
            def.id = s.get("Id").value_or("");
            if (def.id.empty() || def.id.starts_with("mission:")) {
                throw std::runtime_error(where + ": a [quest] needs an Id (not mission:...)");
            }
            def.title = s.get("Title").value_or(def.id);
            def.giver = s.get("Giver").value_or("");
            def.summary = s.get("Summary").value_or("");
            def.done_text = s.get("Done").value_or("");
            def.repeatable = s.number("Repeatable", 0) != 0;
            def.level = std::clamp(int(s.number("Level", 1)), 1, 100);
            for (const auto& [key, value] : s.values) {
                if (same_ci(key, "Requires")) {
                    for (std::string& id : split_list(value)) def.prerequisites.push_back(std::move(id));
                } else if (same_ci(key, "Item")) {
                    def.reward.items.push_back(trim(value));
                }
            }
            def.reward.credits = int64_t(s.number("Credits", 0));
            def.reward.experience = int64_t(s.number("Xp", 0));
            def.reward.faction_points = int64_t(s.number("FactionPoints", 0));
            add(std::move(def));
            current = &quests_.back();
        } else if (s.name == "step") {
            if (current == nullptr) {
                throw std::runtime_error(where + ": a [step] before any [quest]");
            }
            QuestStep step;
            const std::string kind = s.get("Kind").value_or("");
            const auto parsed = quest_step_kind_from_name(kind);
            if (!parsed) {
                throw std::runtime_error(where + ": " + current->id + ": unknown step Kind '" + kind + "'");
            }
            step.kind = *parsed;
            step.text = s.get("Text").value_or("");
            step.target = s.get("Target").value_or(s.get("Item").value_or(""));
            step.npc = s.get("Npc").value_or("");
            step.count = std::max(1, int(s.number("Count", 1)));
            step.drop_from = s.get("DropFrom").value_or("");
            step.drop_chance = std::clamp(s.number("DropChance", 0), 0.0, 1.0);
            step.track = s.get("Track").value_or("");
            Vec3 point;
            double radius = 0;
            if (const auto position = s.get("Position"); position && galaxies_point(*position, point)) {
                step.where = point;
                step.radius = s.number("Radius", 15);
            } else if (const auto area = s.get("Area"); area && galaxies_point(*area, point, &radius)) {
                step.where = point;
                step.radius = radius > 0 ? radius : 50;
            }
            const auto need = [&](bool ok, const char* what) {
                if (!ok) throw std::runtime_error(where + ": " + current->id + ": a " + kind + " step needs " + what);
            };
            switch (step.kind) {
            case QuestStepKind::go_to: need(step.where.has_value(), "a Position"); break;
            case QuestStepKind::kill: need(!step.target.empty(), "a Target"); break;
            case QuestStepKind::collect: need(!step.target.empty(), "an Item"); break;
            case QuestStepKind::talk_to: need(!step.npc.empty(), "an Npc"); break;
            case QuestStepKind::deliver: need(!step.target.empty() && !step.npc.empty(), "an Item and an Npc"); break;
            }
            current->steps.push_back(std::move(step));
        }
    }
    for (const QuestDef& def : quests_) {
        if (def.steps.empty()) {
            throw std::runtime_error(where + ": " + def.id + " has no [step]s");
        }
    }
}

void QuestBook::add(QuestDef def) {
    if (index_.contains(def.id)) {
        throw std::runtime_error("quest " + def.id + " is defined twice");
    }
    index_[def.id] = quests_.size();
    quests_.push_back(std::move(def));
}

void QuestBook::set_world(const WorldSpawns* spawns, Vec3 origin) {
    spawns_ = spawns;
    origin_ = origin;
    missions_.clear();
}

const QuestDef* QuestBook::find(std::string_view id) const {
    if (id.starts_with("mission:")) {
        return mission(id);
    }
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &quests_[it->second];
}

std::vector<std::string> QuestBook::givers() const {
    std::vector<std::string> out;
    for (const QuestDef& def : quests_) {
        if (!def.giver.empty() && std::find(out.begin(), out.end(), def.giver) == out.end()) {
            out.push_back(def.giver);
        }
    }
    return out;
}

// A destroy mission: travel to a lair (or a bandit camp) and clear it.
const QuestDef* QuestBook::mission(std::string_view id) const {
    if (const auto it = missions_.find(id); it != missions_.end()) {
        return &it->second;
    }
    if (spawns_ == nullptr) {
        return nullptr;
    }
    const std::string text(id);
    const bool lair = text.starts_with("mission:lair:");
    const bool camp = text.starts_with("mission:camp:");
    if (!lair && !camp) {
        return nullptr;
    }
    size_t index = 0;
    try {
        index = size_t(std::stoul(text.substr(13)));  // after "mission:lair:" or "mission:camp:"
    } catch (const std::exception&) {
        return nullptr;
    }
    QuestDef def;
    def.id = text;
    def.giver = "Mission Terminal";
    def.mission = true;
    Vec3 position;
    double radius = 0;
    int count = 0;
    int tier = 1;
    int64_t xp_each = 0;
    std::string target, name;
    if (lair) {
        if (index >= spawns_->lairs.size()) return nullptr;
        const LairSpawn& spawn = spawns_->lairs[index];
        const auto type = std::find_if(spawns_->creatures.begin(), spawns_->creatures.end(),
                                       [&](const CreatureArchetype& c) { return c.id == spawn.type_id; });
        if (type == spawns_->creatures.end()) return nullptr;
        position = spawn.position;
        radius = spawn.radius;
        count = std::max(1, spawn.count);
        tier = type->tier;
        xp_each = type->xp_reward;
        target = type->id;
        name = plural(type->name);
        def.title = "Destroy: " + type->name + " Lair";
    } else {
        if (index >= spawns_->camps.size()) return nullptr;
        const CampSpawn& spawn = spawns_->camps[index];
        if (spawn.members.empty()) return nullptr;
        position = spawn.position;
        radius = spawn.radius;
        count = int(spawn.members.size());
        std::string gang;
        for (const std::string& member : spawn.members) {
            const auto type = std::find_if(spawns_->bandits.begin(), spawns_->bandits.end(),
                                           [&](const BanditArchetype& b) { return b.id == member; });
            if (type != spawns_->bandits.end()) {
                tier = std::max(tier, type->tier);
                xp_each = std::max(xp_each, type->xp_reward);
                gang = type->gang;
            }
        }
        if (gang.empty()) return nullptr;
        target = gang;
        name = gang;
        def.title = "Destroy: " + spawn.name;
    }
    def.level = 1 + 8 * (tier - 1);  // a tier's creatures and bandits are a few levels' work (ASSUMPTION)
    const double distance = flat_distance(origin_, position);
    const std::string direction = describe_direction(origin_, position);
    def.summary = lair ? std::format("A lair of {} has been reported {} of the city. Clear it out: all {} of them.",
                                     name, direction, count)
                       : std::format("{} have set up camp {} of the city. Break it up: {} of them.", name, direction,
                                     count);
    QuestStep travel;
    travel.kind = QuestStepKind::go_to;
    travel.where = position;
    travel.radius = radius + 15;
    travel.text = std::format("Travel to the {} ({})", lair ? "lair" : "camp", direction);
    QuestStep clear;
    clear.kind = QuestStepKind::kill;
    clear.target = target;
    clear.count = count;
    clear.where = position;
    clear.radius = radius + 45;
    clear.text = lair ? "Clear the lair: kill " + name : "Break up the camp: kill " + name;
    def.steps = {travel, clear};
    // Paid by danger and distance (ASSUMPTION: tuned against the creatures'
    // and bandits' own loot, so a mission is worth a few lairs' worth).
    def.reward.credits = int64_t(30 * tier * count + distance * 0.25);
    def.reward.experience = xp_each * count / 2 + 10 * tier;
    def.done_text = "Mission complete. Payment has been transferred to your account.";
    return &missions_.emplace(text, std::move(def)).first->second;
}

bool QuestBook::can_accept(const QuestDef& def, const std::vector<QuestProgress>& journal) const {
    if (const QuestProgress* p = find_progress(journal, def.id); p != nullptr && (!p->done || !def.repeatable)) {
        return false;
    }
    for (const std::string& required : def.prerequisites) {
        const QuestProgress* p = find_progress(journal, required);
        if (p == nullptr || !p->done) return false;
    }
    int active = 0, missions = 0;
    for (const QuestProgress& p : journal) {
        if (p.done) continue;
        ++active;
        missions += p.id.starts_with("mission:") ? 1 : 0;
    }
    return active < max_active_quests && (!def.mission || missions < max_active_missions);
}

bool QuestBook::has_work_left(std::string_view giver, const std::vector<QuestProgress>& journal) const {
    for (const QuestDef& def : quests_) {
        if (!same_ci(def.giver, giver)) continue;
        const QuestProgress* p = find_progress(journal, def.id);
        if (p == nullptr || !p->done || def.repeatable) return true;
    }
    return false;
}

std::vector<std::string> QuestBook::target_names(std::string_view target) const {
    std::vector<std::string> out;
    if (spawns_ == nullptr || target.empty()) return out;
    const auto add = [&](const std::string& name) {
        if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
    };
    for (const CreatureArchetype& c : spawns_->creatures) {
        if (target_matches(target, KillTags{c.id, "", false})) add(c.name);
    }
    for (const BanditArchetype& b : spawns_->bandits) {
        if (target_matches(target, KillTags{b.id, b.gang, true})) add(b.name);
    }
    return out;
}

std::optional<Vec3> QuestBook::nearest_of(std::string_view target, Vec3 from) const {
    if (spawns_ == nullptr || target.empty()) return std::nullopt;
    std::optional<Vec3> best;
    double best_distance = 0;
    const auto consider = [&](Vec3 at) {
        const double d = flat_distance(from, at);
        if (!best || d < best_distance) {
            best = at;
            best_distance = d;
        }
    };
    for (const LairSpawn& lair : spawns_->lairs) {
        if (target_matches(target, KillTags{lair.type_id, "", false})) consider(lair.position);
    }
    for (const CampSpawn& camp : spawns_->camps) {
        for (const std::string& member : camp.members) {
            const auto type = std::find_if(spawns_->bandits.begin(), spawns_->bandits.end(),
                                           [&](const BanditArchetype& b) { return b.id == member; });
            if (type != spawns_->bandits.end() && target_matches(target, KillTags{type->id, type->gang, true})) {
                consider(camp.position);
                break;
            }
        }
    }
    return best;
}

std::string QuestBook::why_none(std::string_view giver, const std::vector<QuestProgress>& journal) const {
    if (!offered_by(giver, journal).empty()) return "";
    const std::string name(giver);
    for (const QuestDef& def : quests_) {
        const QuestProgress* p = find_progress(journal, def.id);
        if (same_ci(def.giver, giver) && p != nullptr && !p->done) {
            return std::format("You're already doing {} for {}.", def.title, name);
        }
    }
    for (const QuestDef& def : quests_) {
        if (!same_ci(def.giver, giver) || find_progress(journal, def.id) != nullptr) continue;
        for (const std::string& required : def.prerequisites) {
            const QuestProgress* p = find_progress(journal, required);
            if (p != nullptr && p->done) continue;
            const QuestDef* first = find(required);
            if (first == nullptr) continue;
            return first->giver.empty() || same_ci(first->giver, giver)
                       ? std::format("{} has work for you after {}.", name, first->title)
                       : std::format("{} has work for you after {} ({}).", name, first->title, first->giver);
        }
    }
    int active = 0;
    for (const QuestProgress& p : journal) active += p.done ? 0 : 1;
    if (active >= max_active_quests) {
        return std::format("Your journal is full ({} quests).", max_active_quests);
    }
    return std::format("{} has no more work for you.", name);
}

std::vector<const QuestDef*> QuestBook::offered_by(std::string_view giver,
                                                   const std::vector<QuestProgress>& journal) const {
    std::vector<const QuestDef*> out;
    for (const QuestDef& def : quests_) {
        if (same_ci(def.giver, giver) && can_accept(def, journal)) {
            out.push_back(&def);
        }
    }
    return out;
}

std::vector<const QuestDef*> QuestBook::missions(double now, const std::vector<QuestProgress>& journal,
                                                 size_t count) const {
    std::vector<std::pair<uint64_t, std::string>> candidates;
    if (spawns_ == nullptr) {
        return {};
    }
    const uint64_t window = uint64_t(std::max(0.0, now) / 600);
    const auto consider = [&](const std::string& id, Vec3 position) {
        const double distance = flat_distance(origin_, position);
        if (distance >= 300 && distance <= 3000 && find_progress(journal, id) == nullptr) {
            candidates.emplace_back(mix(window * 0x100000001B3ull ^ std::hash<std::string>{}(id)), id);
        }
    };
    for (size_t i = 0; i < spawns_->lairs.size(); ++i) {
        consider(std::format("mission:lair:{}", i), spawns_->lairs[i].position);
    }
    for (size_t i = 0; i < spawns_->camps.size(); ++i) {
        consider(std::format("mission:camp:{}", i), spawns_->camps[i].position);
    }
    std::sort(candidates.begin(), candidates.end());
    std::vector<const QuestDef*> out;
    for (const auto& [key, id] : candidates) {
        if (out.size() >= count) break;
        if (const QuestDef* def = find(id)) {
            out.push_back(def);
        }
    }
    std::sort(out.begin(), out.end(), [&](const QuestDef* a, const QuestDef* b) {
        return flat_distance(origin_, *a->steps[0].where) < flat_distance(origin_, *b->steps[0].where);
    });
    return out;
}

bool QuestBook::matches(const QuestStep& step, const KillTags& tags, Vec3 at) {
    return step.kind == QuestStepKind::kill && target_matches(step.target, tags) && inside(step, at);
}

void QuestBook::advance(QuestProgress& progress, const QuestDef& def, QuestEffects& effects) const {
    ++progress.step;
    progress.count = 0;
    effects.changed = true;
    if (progress.step >= int(def.steps.size())) {
        progress.done = true;
        effects.completed.push_back(def.id);
        effects.notes.push_back({def.id, "Quest complete: " + def.title});
    } else {
        effects.notes.push_back({def.id, def.title + ": " + def.steps[size_t(progress.step)].text});
    }
}

namespace {

// Drops finished missions and repeatable quests: nothing to keep them for.
void tidy(std::vector<QuestProgress>& journal, const QuestBook& book) {
    std::erase_if(journal, [&](const QuestProgress& p) {
        if (!p.done) return false;
        const QuestDef* def = book.find(p.id);
        return def == nullptr || def->mission || def->repeatable;
    });
}

} // namespace

QuestEffects QuestBook::on_kill(std::vector<QuestProgress>& journal, const KillTags& tags, Vec3 at,
                                const std::function<double()>& roll) const {
    QuestEffects effects;
    for (QuestProgress& p : journal) {
        const QuestDef* def = p.done ? nullptr : find(p.id);
        if (def == nullptr || p.step >= int(def->steps.size())) continue;
        const QuestStep& step = def->steps[size_t(p.step)];
        if (matches(step, tags, at)) {
            ++p.count;
            effects.changed = true;
            if (p.count >= step.count) {
                advance(p, *def, effects);
            } else {
                effects.notes.push_back({def->id, std::format("{}: {}/{}", step.text, p.count, step.count)});
            }
        } else if (step.kind == QuestStepKind::collect && target_matches(step.drop_from, tags) && inside(step, at) &&
                   roll && roll() < step.drop_chance) {
            effects.grant.push_back(step.target);
        }
    }
    tidy(journal, *this);
    return effects;
}

QuestEffects QuestBook::on_event(std::vector<QuestProgress>& journal, std::string_view name) const {
    QuestEffects effects;
    for (QuestProgress& p : journal) {
        const QuestDef* def = p.done ? nullptr : find(p.id);
        if (def == nullptr || p.step >= int(def->steps.size())) continue;
        const QuestStep& step = def->steps[size_t(p.step)];
        if (step.kind != QuestStepKind::event || !same_ci(step.target, name)) continue;
        ++p.count;
        effects.changed = true;
        if (p.count >= step.count) {
            advance(p, *def, effects);
        } else {
            effects.notes.push_back({def->id, std::format("{}: {}/{}", step.text, p.count, step.count)});
        }
    }
    tidy(journal, *this);
    return effects;
}

QuestEffects QuestBook::on_position(std::vector<QuestProgress>& journal, Vec3 at) const {
    QuestEffects effects;
    for (QuestProgress& p : journal) {
        const QuestDef* def = p.done ? nullptr : find(p.id);
        while (def != nullptr && !p.done && p.step < int(def->steps.size())) {
            const QuestStep& step = def->steps[size_t(p.step)];
            if (step.kind != QuestStepKind::go_to || !step.where || flat_distance(*step.where, at) > step.radius) {
                break;
            }
            advance(p, *def, effects);
        }
    }
    tidy(journal, *this);
    return effects;
}

QuestEffects QuestBook::on_talk(std::vector<QuestProgress>& journal, std::string_view npc,
                                const std::function<int(const std::string&)>& held) const {
    QuestEffects effects;
    std::map<std::string, int> promised;  // items already handed over by this talk
    for (QuestProgress& p : journal) {
        const QuestDef* def = p.done ? nullptr : find(p.id);
        if (def == nullptr || p.step >= int(def->steps.size())) continue;
        const QuestStep& step = def->steps[size_t(p.step)];
        if (!same_ci(step.npc, npc)) continue;
        if (step.kind == QuestStepKind::talk_to) {
            advance(p, *def, effects);
        } else if (step.kind == QuestStepKind::deliver) {
            const int have = (held ? held(step.target) : 0) - promised[step.target];
            if (have >= step.count) {
                promised[step.target] += step.count;
                effects.take.emplace_back(step.target, step.count);
                advance(p, *def, effects);
            } else {
                effects.notes.push_back({def->id, std::format("{}: {}/{}", step.text, std::max(0, have), step.count)});
            }
        }
    }
    tidy(journal, *this);
    return effects;
}

QuestEffects QuestBook::on_inventory(std::vector<QuestProgress>& journal,
                                     const std::function<int(const std::string&)>& held) const {
    QuestEffects effects;
    for (QuestProgress& p : journal) {
        const QuestDef* def = p.done ? nullptr : find(p.id);
        while (def != nullptr && !p.done && p.step < int(def->steps.size())) {
            const QuestStep& step = def->steps[size_t(p.step)];
            if (step.kind != QuestStepKind::collect) break;
            const int have = held ? std::max(0, held(step.target)) : 0;
            if (have >= step.count) {
                advance(p, *def, effects);
                continue;
            }
            if (have != p.count) {
                if (have > p.count) {
                    effects.notes.push_back({def->id, std::format("{}: {}/{}", step.text, have, step.count)});
                }
                p.count = have;
                effects.changed = true;
            }
            break;
        }
    }
    tidy(journal, *this);
    return effects;
}

QuestEffects QuestBook::accept(std::vector<QuestProgress>& journal, const QuestDef& def) const {
    QuestEffects effects;
    std::erase_if(journal, [&](const QuestProgress& p) { return p.id == def.id; });  // a repeatable one's last run
    journal.push_back({def.id, 0, 0, false});
    effects.changed = true;
    effects.notes.push_back({def.id, "Quest accepted: " + def.title});
    return effects;
}

QuestEffects QuestBook::abandon(std::vector<QuestProgress>& journal, std::string_view id) const {
    QuestEffects effects;
    const auto before = journal.size();
    std::erase_if(journal, [&](const QuestProgress& p) { return p.id == id && !p.done; });
    if (journal.size() != before) {
        effects.changed = true;
        const QuestDef* def = find(id);
        effects.notes.push_back({std::string(id), "Quest abandoned: " + (def ? def->title : std::string(id))});
    }
    return effects;
}

} // namespace swm::core
