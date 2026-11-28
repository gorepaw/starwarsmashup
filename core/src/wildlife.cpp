#include "swm/core/wildlife.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>

#include "swm/core/ini.h"
#include "swm/core/nav.h"
#include "swm/core/server.h"
#include "swm/core/world.h"

namespace swm::core {

namespace {

bool same_ci(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); });
}

double horiz_dist(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

Vec3 parse_xz(std::string_view text, const std::string& what) {
    std::istringstream in{std::string(text)};
    double x = 0, z = 0;
    if (!(in >> x >> z)) {
        throw std::runtime_error(what + " needs x z");
    }
    return {-x, 0, z};  // Galaxies to game coordinates
}

CreatureBehavior parse_behavior(std::string_view text) {
    if (same_ci(text, "herd")) return CreatureBehavior::herd;
    if (same_ci(text, "territorial")) return CreatureBehavior::territorial;
    if (same_ci(text, "aggressive")) return CreatureBehavior::aggressive;
    return CreatureBehavior::passive;
}

WeaponDef fallback_bandit_weapon() {
    WeaponDef w;
    w.name = "blaster_carbine";
    w.damage_type = DamageType::laser;
    w.projectile = Projectile::bolt;
    w.damage = 28.0;
    w.shot_delay = 0.28;
    w.rounds_per_clip = 24;
    w.reload_time = 1.8;
    w.bolt_speed = 95.0;
    w.bolt_life = 2.0;
    w.yaw_spread = 0.025;
    w.pitch_spread = 0.025;
    return w;
}

constexpr double corpse_visible_seconds = 45.0;

} // namespace

WorldSpawns load_world_spawns(const std::filesystem::path& path) {
    const Ini ini = Ini::load(path);
    const std::string where = path.string();
    WorldSpawns out;
    for (const Ini::Section& s : ini.sections()) {
        if (s.name == "creature_type") {
            CreatureArchetype a;
            a.id = s.get("Id").value_or("");
            a.name = s.get("Name").value_or(a.id);
            a.template_path = s.get("Template").value_or("");
            if (a.id.empty() || a.template_path.empty()) {
                throw std::runtime_error(where + ": [creature_type] needs Id and Template");
            }
            a.tier = std::max(1, int(s.number("Tier", 1)));
            a.behavior = parse_behavior(s.get("Behavior").value_or("passive"));
            a.health = std::max(10.0, s.number("Health", 120));
            a.walk_speed = std::max(0.5, s.number("WalkSpeed", 2.6));
            a.run_speed = std::max(a.walk_speed, s.number("RunSpeed", 6.0));
            a.radius = std::max(0.2, s.number("Radius", 0.6));
            a.height = std::max(0.4, s.number("Height", 1.2));
            a.damage = std::max(0.0, s.number("Damage", 14));
            a.attack_range = std::max(1.2, s.number("AttackRange", 2.4));
            a.attack_cooldown = std::max(0.4, s.number("AttackCooldown", 1.4));
            a.aggro_radius = std::max(0.0, s.number("AggroRadius", 18));
            a.leash_radius = std::max(20.0, s.number("LeashRadius", 70));
            a.respawn_time = std::max(2.0, s.number("RespawnTime", 25));
            a.xp_reward = std::max<int64_t>(0, int64_t(s.number("Xp", 25)));
            a.loot_table = s.get("LootTable").value_or("");
            out.creatures.push_back(std::move(a));
        } else if (s.name == "lair") {
            LairSpawn l;
            l.type_id = s.get("Type").value_or("");
            if (l.type_id.empty()) {
                throw std::runtime_error(where + ": [lair] needs Type");
            }
            l.position = parse_xz(s.get("Position").value_or(""), where + ": [lair] Position");
            l.radius = std::max(4.0, s.number("Radius", 25));
            l.count = std::clamp(int(s.number("Count", 4)), 1, 16);
            out.lairs.push_back(std::move(l));
        } else if (s.name == "bandit_type") {
            BanditArchetype b;
            b.id = s.get("Id").value_or("");
            b.name = s.get("Name").value_or(b.id);
            b.template_path = s.get("Template").value_or("");
            if (b.id.empty() || b.template_path.empty()) {
                throw std::runtime_error(where + ": [bandit_type] needs Id and Template");
            }
            b.gang = s.get("Gang").value_or("Bandits");
            b.tier = std::max(1, int(s.number("Tier", 1)));
            b.health = std::max(20.0, s.number("Health", 220));
            b.skill = BotSkill::preset(s.get("Difficulty").value_or("normal"));
            b.skill.fire_range = std::max(15.0, s.number("FireRange", b.skill.fire_range));
            b.weapon_id = s.get("Weapon").value_or("wp_rifle_e11");
            b.aggro_radius = std::max(5.0, s.number("AggroRadius", 35));
            b.leash_radius = std::max(25.0, s.number("LeashRadius", 85));
            b.respawn_time = std::max(2.0, s.number("RespawnTime", 30));
            b.xp_reward = std::max<int64_t>(0, int64_t(s.number("Xp", 45)));
            b.loot_table = s.get("LootTable").value_or("");
            out.bandits.push_back(std::move(b));
        } else if (s.name == "camp") {
            CampSpawn c;
            c.name = s.get("Name").value_or("Bandit Camp");
            c.position = parse_xz(s.get("Position").value_or(""), where + ": [camp] Position");
            c.radius = std::max(5.0, s.number("Radius", 22));
            for (const auto& [k, v] : s.values) {
                if (same_ci(k, "Member")) {
                    c.members.push_back(v);
                }
            }
            if (c.members.empty()) {
                throw std::runtime_error(where + ": [camp] needs at least one Member");
            }
            out.camps.push_back(std::move(c));
        }
    }
    return out;
}

WorldPopulation::WorldPopulation(GameServer& server, const World& world, const NavGrid* nav, const WorldSpawns& spawns,
                                 const ItemCatalog* catalog, const std::map<std::string, uint8_t>& look_indices)
    : server_(server), world_(world), nav_(nav), spawns_(spawns), catalog_(catalog), look_indices_(look_indices) {}

double WorldPopulation::random_unit() {
    uint64_t z = (rng_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return double(z >> 11) / double(1ull << 53);
}

Vec3 WorldPopulation::random_point_around(Vec3 center, double radius) {
    Vec3 p;
    // Somewhere dry, if a few tries find it (a lair on a lake's shore).
    for (int attempt = 0; attempt < 6; ++attempt) {
        const double angle = random_unit() * 2.0 * std::numbers::pi;
        const double r = std::sqrt(random_unit()) * radius;
        p = world_.clamp(center + Vec3{std::cos(angle) * r, 0.0, std::sin(angle) * r});
        if (!world_.swim_floor_at(p.x, p.z)) {
            break;
        }
    }
    const double h = world_.terrain().height_at(p.x, p.z);
    p.y = world_.ground_at(p.x, p.z, h + 1.0, 0.5).height;
    if (p.y < -1e8) p.y = h;
    return p;
}

const CreatureArchetype* WorldPopulation::find_creature_type(const std::string& id, size_t& out_index) const {
    for (size_t i = 0; i < spawns_.creatures.size(); ++i) {
        if (spawns_.creatures[i].id == id) {
            out_index = i;
            return &spawns_.creatures[i];
        }
    }
    return nullptr;
}

const BanditArchetype* WorldPopulation::find_bandit_type(const std::string& id, size_t& out_index) const {
    for (size_t i = 0; i < spawns_.bandits.size(); ++i) {
        if (spawns_.bandits[i].id == id) {
            out_index = i;
            return &spawns_.bandits[i];
        }
    }
    return nullptr;
}

Loadout WorldPopulation::build_bandit_loadout(const BanditArchetype& arch) const {
    Loadout lo;
    lo.name = arch.name;
    if (catalog_ != nullptr) {
        if (const ItemDef* item = catalog_->find(arch.weapon_id)) {
            if (item->kind == ItemKind::weapon) {
                lo.weapons[primary_slot] = catalog_->resolve_weapon(*item);
                return lo;
            }
        }
    }
    lo.weapons[primary_slot] = fallback_bandit_weapon();
    return lo;
}

void WorldPopulation::spawn_all() {
    for (size_t li = 0; li < spawns_.lairs.size(); ++li) {
        const LairSpawn& lair = spawns_.lairs[li];
        size_t arch_idx = 0;
        const CreatureArchetype* arch = find_creature_type(lair.type_id, arch_idx);
        if (!arch) continue;

        Vec3 center = world_.clamp(lair.position);
        center.y = world_.terrain().height_at(center.x, center.z);
        lair_states_.push_back({center, lair.radius, center, 3.0 + random_unit() * 5.0});

        const auto lit = look_indices_.find(arch->template_path);
        const uint8_t look = lit != look_indices_.end() ? lit->second : 0;

        for (int i = 0; i < lair.count; ++i) {
            const Vec3 pos = random_point_around(center, lair.radius * 0.7);
            const double yaw = random_unit() * 2.0 * std::numbers::pi;
            GameServer::NpcCombatSpec spec;
            spec.actor_kind = 2;
            spec.passive = arch->behavior == CreatureBehavior::passive;
            spec.max_health = arch->health;
            spec.hit_radius = arch->radius;
            spec.hit_height = arch->height;
            spec.walk_speed = arch->walk_speed;
            spec.run_speed = arch->run_speed;
            const uint16_t colors = uint16_t((li * 17 + i * 7) % 100 + 1);
            const uint32_t entity = server_.add_combat_npc(arch->name, pos, yaw, look, spec, colors);
            CreatureMob mob;
            mob.entity = entity;
            mob.archetype = arch_idx;
            mob.lair = lair_states_.size() - 1;  // lairs of unknown types have no state
            mob.home = pos;
            mob.goal = pos;
            mob.wait_until = random_unit() * 4.0;
            lair_states_.back().mobs.push_back(creatures_.size());
            by_entity_[entity] = {false, creatures_.size()};
            creatures_.push_back(mob);
        }
    }

    for (size_t ci = 0; ci < spawns_.camps.size(); ++ci) {
        const CampSpawn& camp = spawns_.camps[ci];
        Vec3 center = world_.clamp(camp.position);
        center.y = world_.terrain().height_at(center.x, center.z);
        camp_states_.push_back({center, camp.radius});

        for (size_t mi = 0; mi < camp.members.size(); ++mi) {
            size_t arch_idx = 0;
            const BanditArchetype* arch = find_bandit_type(camp.members[mi], arch_idx);
            if (!arch) continue;
            const auto lit = look_indices_.find(arch->template_path);
            const uint8_t look = lit != look_indices_.end() ? lit->second : 0;
            const Vec3 pos = random_point_around(center, camp.radius * 0.65);
            const double yaw = random_unit() * 2.0 * std::numbers::pi;
            GameServer::NpcCombatSpec spec;
            spec.actor_kind = 3;
            spec.max_health = arch->health;
            spec.hit_radius = 0.45;
            spec.hit_height = 1.8;
            spec.walk_speed = 4.2;
            spec.run_speed = 6.5;
            spec.loadout = build_bandit_loadout(*arch);
            if (const WeaponDef* w = weapon_in_slot(spec.loadout, primary_slot)) {
                spec.weapon_model = w->model;
                spec.anim_set = w->anim_set;
            }
            const uint16_t colors = uint16_t((ci * 23 + mi * 11) % 100 + 1);
            const uint32_t entity = server_.add_combat_npc(arch->name, pos, yaw, look, spec, colors);
            BanditMob mob;
            mob.entity = entity;
            mob.archetype = arch_idx;
            mob.camp = ci;
            mob.home = pos;
            mob.patrol_goal = pos;
            mob.wait_until = random_unit() * 3.0;
            mob.loadout = std::move(spec.loadout);
            mob.brain.aim_phase = random_unit() * 100.0;
            camp_states_.back().mobs.push_back(bandits_.size());
            by_entity_[entity] = {true, bandits_.size()};
            bandits_.push_back(std::move(mob));
        }
    }

    // Everyone starts asleep; groups wake as players come near.
    if (activity_.wake_radius > 0) {
        for (LairState& lair : lair_states_) {
            lair.awake = false;
            for (const size_t idx : lair.mobs) {
                sleep_creature(creatures_[idx]);
            }
        }
        for (CampState& camp : camp_states_) {
            camp.awake = false;
            for (const size_t idx : camp.mobs) {
                sleep_bandit(bandits_[idx]);
            }
        }
    }
}

void WorldPopulation::sleep_creature(CreatureMob& mob) {
    const auto info = server_.npc_info(mob.entity);
    if (!info || info->life != Life::alive) {
        return;  // the dead wait to come back when their lair wakes
    }
    // Home, healed, and out of the world.
    server_.respawn_npc(mob.entity, mob.home, random_unit() * 2.0 * std::numbers::pi);
    server_.set_npc_hidden(mob.entity, true);
    mob.dormant = true;
    mob.goal = mob.home;
    mob.target = 0;
    mob.returning = false;
    mob.flee_until = 0;
    mob.pose = 0;
    mob.attack_pose_until = 0;
    mob.last_seen_damage_tick = 0;
}

void WorldPopulation::sleep_bandit(BanditMob& mob) {
    const auto info = server_.npc_info(mob.entity);
    if (!info || info->life != Life::alive) {
        return;
    }
    server_.respawn_npc(mob.entity, mob.home, random_unit() * 2.0 * std::numbers::pi);
    server_.set_npc_hidden(mob.entity, true);
    mob.dormant = true;
    mob.patrol_goal = mob.home;
    mob.target = 0;
    mob.returning = false;
    mob.lost_target_at = 0;
    mob.brain.reset();
    mob.last_seen_damage_tick = 0;
}

void WorldPopulation::update_activity(double now) {
    if (activity_.wake_radius <= 0 || now < next_activity_check_) {
        return;
    }
    next_activity_check_ = now + activity_.check_interval;
    const auto decide = [&](Vec3 center, double radius, bool& awake, double& quiet_since, bool engaged) {
        // Returns +1 to wake, -1 to sleep, 0 to stay as it is.
        const double reach = radius + activity_.wake_radius;
        if (!awake) {
            return server_.player_near(center, reach) ? 1 : 0;
        }
        if (engaged || server_.player_near(center, reach + activity_.sleep_margin)) {
            quiet_since = -1;
            return 0;
        }
        if (quiet_since < 0) {
            quiet_since = now;
        }
        return now - quiet_since >= activity_.linger ? -1 : 0;
    };
    for (LairState& lair : lair_states_) {
        bool engaged = false;
        for (const size_t idx : lair.mobs) {
            const CreatureMob& m = creatures_[idx];
            engaged = engaged || (m.dead_since < 0 && (m.target != 0 || m.returning || m.flee_until > now));
        }
        const int change = decide(lair.center, lair.radius, lair.awake, lair.quiet_since, engaged);
        if (change > 0) {
            lair.awake = true;
            lair.quiet_since = -1;
            for (const size_t idx : lair.mobs) {
                CreatureMob& mob = creatures_[idx];
                if (mob.dormant) {
                    mob.dormant = false;
                    mob.wait_until = now + random_unit() * 3.0;
                    server_.set_npc_hidden(mob.entity, false);
                }
            }
        } else if (change < 0) {
            lair.awake = false;
            for (const size_t idx : lair.mobs) {
                sleep_creature(creatures_[idx]);
            }
        }
    }
    for (CampState& camp : camp_states_) {
        bool engaged = false;
        for (const size_t idx : camp.mobs) {
            const BanditMob& m = bandits_[idx];
            engaged = engaged || (m.dead_since < 0 && (m.target != 0 || m.returning));
        }
        const int change = decide(camp.center, camp.radius, camp.awake, camp.quiet_since, engaged);
        if (change > 0) {
            camp.awake = true;
            camp.quiet_since = -1;
            for (const size_t idx : camp.mobs) {
                BanditMob& mob = bandits_[idx];
                if (mob.dormant) {
                    mob.dormant = false;
                    mob.wait_until = now + random_unit() * 3.0;
                    server_.set_npc_hidden(mob.entity, false);
                }
            }
        } else if (change < 0) {
            camp.awake = false;
            for (const size_t idx : camp.mobs) {
                sleep_bandit(bandits_[idx]);
            }
        }
    }
}

double WorldPopulation::respawn_scale(Vec3 center, double radius) const {
    if (activity_.respawn_players <= 0) return 1.0;
    const double reach = radius + (activity_.wake_radius > 0 ? activity_.wake_radius : 300.0);
    int near = 0;
    for (const PlayerPos& p : players_) {
        const double dx = p.pos.x - center.x;
        const double dz = p.pos.z - center.z;
        near += dx * dx + dz * dz <= reach * reach ? 1 : 0;
    }
    return std::clamp(activity_.respawn_players / std::max(1, near), 1.0, std::max(1.0, activity_.respawn_max_scale));
}

size_t WorldPopulation::awake_groups() const {
    size_t n = 0;
    for (const LairState& l : lair_states_) n += l.awake ? 1 : 0;
    for (const CampState& c : camp_states_) n += c.awake ? 1 : 0;
    return n;
}

size_t WorldPopulation::dormant_mobs() const {
    size_t n = 0;
    for (const CreatureMob& m : creatures_) n += m.dormant ? 1 : 0;
    for (const BanditMob& m : bandits_) n += m.dormant ? 1 : 0;
    return n;
}

bool WorldPopulation::dormant(uint32_t entity) const {
    const auto it = by_entity_.find(entity);
    if (it == by_entity_.end()) return false;
    return it->second.first ? bandits_[it->second.second].dormant : creatures_[it->second.second].dormant;
}

void WorldPopulation::update(double now) {
    if (last_update_ == 0.0) {
        last_update_ = now;
    }
    update_activity(now);
    players_.clear();
    for (const uint32_t id : server_.entities()) {
        if (server_.alive(id) && !server_.cloaked(id)) {  // Stage 12: a cloak hides you from the wild too
            if (const MoveState* st = server_.state_of(id)) {
                players_.push_back({id, st->position, st->velocity, server_.notice_of(id)});
            }
        }
    }
    update_creatures(now);
    update_bandits(now);
    last_update_ = now;
}

void WorldPopulation::update_creatures(double now) {
    const double dt = std::clamp(now - last_update_, 0.0, 0.25);

    for (LairState& lair : lair_states_) {
        if (now >= lair.next_herd_move) {
            lair.herd_anchor = random_point_around(lair.center, lair.radius * 0.6);
            lair.next_herd_move = now + 10.0 + random_unit() * 8.0;
        }
    }

    const std::vector<PlayerPos>& players = players_;

    for (size_t idx = 0; idx < creatures_.size(); ++idx) {
        CreatureMob& mob = creatures_[idx];
        if (mob.dormant) continue;  // asleep: out of the world
        const CreatureArchetype& arch = spawns_.creatures[mob.archetype];
        const bool awake = lair_states_[mob.lair].awake;
        const auto info = server_.npc_info(mob.entity);
        if (!info) continue;

        if (info->life != Life::alive) {
            if (mob.dead_since < 0) {
                mob.dead_since = now;
                mob.target = 0;
                mob.returning = false;
                mob.flee_until = 0;
            }
            if (!mob.corpse_hidden && now - mob.dead_since >= corpse_visible_seconds) {
                server_.set_npc_hidden(mob.entity, true);
                mob.corpse_hidden = true;
            }
            if (awake && now - mob.dead_since >= arch.respawn_time * respawn_scale(lair_states_[mob.lair].center,
                                                                                  lair_states_[mob.lair].radius)) {
                const Vec3 spawn_pos = random_point_around(lair_states_[mob.lair].center, lair_states_[mob.lair].radius * 0.5);
                mob.home = spawn_pos;
                mob.goal = spawn_pos;
                mob.dead_since = -1;
                mob.corpse_hidden = false;
                mob.last_seen_damage_tick = info->last_damaged_tick;
                server_.respawn_npc(mob.entity, spawn_pos, random_unit() * 2.0 * std::numbers::pi);
            }
            continue;
        }
        if (!awake) continue;

        const Vec3 pos = info->state.position;
        MoveInput input;
        input.yaw = info->state.yaw;

        if (now >= mob.attack_pose_until && mob.pose == 2) {
            mob.pose = 0;
            server_.set_npc_pose(mob.entity, 0);
        }

        // Handle returning / leashing
        if (mob.returning) {
            server_.heal_npc(mob.entity, arch.health * 0.15 * dt);
            if (horiz_dist(pos, mob.home) <= 4.0) {
                mob.returning = false;
                mob.target = 0;
                server_.heal_npc(mob.entity, arch.health);
            } else {
                const Vec3 d{mob.home.x - pos.x, 0, mob.home.z - pos.z};
                input.yaw = float(std::atan2(-d.x, -d.z));
                input.forward = 127;
                input.buttons = MoveInput::sprint;
                server_.steer_npc(mob.entity, input);
                continue;
            }
        }

        // Check if damaged since last update
        if (info->last_damaged_tick != mob.last_seen_damage_tick && info->last_attacker != 0) {
            mob.last_seen_damage_tick = info->last_damaged_tick;
            if (arch.behavior == CreatureBehavior::passive) {
                mob.flee_until = now + 8.0;
                mob.target = info->last_attacker;
            } else {
                mob.target = info->last_attacker;
                if (arch.behavior == CreatureBehavior::herd) {
                    // Alert same-lair herd mates within 25 m
                    for (CreatureMob& mate : creatures_) {
                        if (mate.lair == mob.lair && mate.entity != mob.entity && !mate.returning && mate.target == 0) {
                            if (const auto mi = server_.npc_info(mate.entity);
                                mi && mi->life == Life::alive && horiz_dist(mi->state.position, pos) <= 25.0) {
                                mate.target = info->last_attacker;
                            }
                        }
                    }
                }
            }
        }

        // Fleeing (passive creatures)
        if (arch.behavior == CreatureBehavior::passive && now < mob.flee_until) {
            Vec3 away{1, 0, 0};
            if (const MoveState* att = server_.state_of(mob.target)) {
                away = {pos.x - att->position.x, 0, pos.z - att->position.z};
            }
            if (away.length() < 1e-3) away = {1, 0, 0};
            input.yaw = float(std::atan2(-away.x, -away.z));
            input.forward = 127;
            input.buttons = MoveInput::sprint;
            server_.steer_npc(mob.entity, input);
            continue;
        }

        // Leash check
        if (mob.target != 0 && horiz_dist(pos, lair_states_[mob.lair].center) > arch.leash_radius) {
            mob.returning = true;
            mob.target = 0;
            mob.pose = 0;
            server_.set_npc_pose(mob.entity, 0);
            server_.steer_npc(mob.entity, input);
            continue;
        }

        // Aggro scan for territorial / aggressive creatures
        if (mob.target == 0 && (arch.behavior == CreatureBehavior::aggressive || arch.behavior == CreatureBehavior::territorial)) {
            const double scan = arch.behavior == CreatureBehavior::aggressive ? arch.aggro_radius : std::min(12.0, arch.aggro_radius);
            double best = scan;
            for (const PlayerPos& p : players) {
                const double d = horiz_dist(pos, p.pos);
                if (d < best && d < scan * p.notice &&
                    !world_.raycast(pos + Vec3{0, arch.height * 0.7, 0}, p.pos + Vec3{0, 1.2, 0})) {
                    best = d;
                    mob.target = p.entity;
                }
            }
        }

        // Combat chase & melee
        if (mob.target != 0) {
            const MoveState* target_st = server_.state_of(mob.target);
            if (!target_st || !server_.alive(mob.target)) {
                mob.target = 0;
                mob.returning = horiz_dist(pos, mob.home) > 12.0;
                server_.steer_npc(mob.entity, input);
                continue;
            }
            if (mob.pose == 1) {
                mob.pose = 0;
                server_.set_npc_pose(mob.entity, 0);
            }
            const Vec3 d{target_st->position.x - pos.x, 0, target_st->position.z - pos.z};
            const double dist = (target_st->position - pos).length();
            input.yaw = float(std::atan2(-d.x, -d.z));
            if (dist > arch.attack_range) {
                input.forward = 127;
                input.buttons = MoveInput::sprint;
            } else if (now >= mob.next_attack) {
                mob.next_attack = now + arch.attack_cooldown;
                mob.pose = 2;
                mob.attack_pose_until = now + 0.45;
                server_.set_npc_pose(mob.entity, 2);
                server_.npc_melee(mob.entity, mob.target, arch.damage, DamageType::melee);
            }
            server_.steer_npc(mob.entity, input);
            continue;
        }

        // Idle / Wander / Herd
        if (horiz_dist(pos, mob.goal) <= 1.2) {
            if (now >= mob.wait_until) {
                const Vec3 anchor = arch.behavior == CreatureBehavior::herd ? lair_states_[mob.lair].herd_anchor : mob.home;
                mob.goal = random_point_around(anchor, arch.behavior == CreatureBehavior::herd ? 10.0 : 14.0);
                mob.wait_until = now + 3.0 + random_unit() * 5.0;
                if (mob.pose == 1) {
                    mob.pose = 0;
                    server_.set_npc_pose(mob.entity, 0);
                }
            }
        } else {
            const Vec3 d{mob.goal.x - pos.x, 0, mob.goal.z - pos.z};
            input.yaw = float(std::atan2(-d.x, -d.z));
            input.forward = 105;
        }
        server_.steer_npc(mob.entity, input);
    }
}

void WorldPopulation::update_bandits(double now) {
    const double dt = std::clamp(now - last_update_, 0.0, 0.25);

    const std::vector<PlayerPos>& players = players_;

    for (BanditMob& mob : bandits_) {
        if (mob.dormant) continue;
        const BanditArchetype& arch = spawns_.bandits[mob.archetype];
        const bool awake = camp_states_[mob.camp].awake;
        const auto info = server_.npc_info(mob.entity);
        if (!info) continue;

        if (info->life != Life::alive) {
            if (mob.dead_since < 0) {
                mob.dead_since = now;
                mob.target = 0;
                mob.returning = false;
                mob.brain.reset();
            }
            if (!mob.corpse_hidden && now - mob.dead_since >= corpse_visible_seconds) {
                server_.set_npc_hidden(mob.entity, true);
                mob.corpse_hidden = true;
            }
            if (awake && !camp_states_[mob.camp].temporary &&
                now - mob.dead_since >= arch.respawn_time * respawn_scale(camp_states_[mob.camp].center,
                                                                                  camp_states_[mob.camp].radius)) {
                const Vec3 spawn_pos = random_point_around(camp_states_[mob.camp].center, camp_states_[mob.camp].radius * 0.65);
                mob.home = spawn_pos;
                mob.patrol_goal = spawn_pos;
                mob.dead_since = -1;
                mob.corpse_hidden = false;
                mob.last_seen_damage_tick = info->last_damaged_tick;
                server_.respawn_npc(mob.entity, spawn_pos, random_unit() * 2.0 * std::numbers::pi);
            }
            continue;
        }
        if (!awake) continue;

        const Vec3 pos = info->state.position;
        MoveInput input;
        input.yaw = info->state.yaw;

        if (mob.returning) {
            server_.heal_npc(mob.entity, arch.health * 0.2 * dt);
            if (horiz_dist(pos, mob.home) <= 4.0) {
                mob.returning = false;
                mob.target = 0;
                mob.brain.reset();
                server_.heal_npc(mob.entity, arch.health);
            } else {
                const Vec3 d{mob.home.x - pos.x, 0, mob.home.z - pos.z};
                input.yaw = float(std::atan2(-d.x, -d.z));
                input.forward = 127;
                input.buttons = MoveInput::sprint;
                server_.steer_npc(mob.entity, input);
                continue;
            }
        }

        if (info->last_damaged_tick != mob.last_seen_damage_tick && info->last_attacker != 0) {
            mob.last_seen_damage_tick = info->last_damaged_tick;
            mob.target = info->last_attacker;
            mob.lost_target_at = 0;
            // Alert campmates within 30 m
            for (BanditMob& mate : bandits_) {
                if (mate.camp == mob.camp && mate.entity != mob.entity && !mate.returning && mate.target == 0) {
                    if (const auto mi = server_.npc_info(mate.entity);
                        mi && mi->life == Life::alive && horiz_dist(mi->state.position, pos) <= 30.0) {
                        mate.target = info->last_attacker;
                    }
                }
            }
        }

        if (mob.target == 0) {
            double best = arch.aggro_radius;
            for (const PlayerPos& p : players) {
                const double d = horiz_dist(pos, p.pos);
                if (d < best && d < arch.aggro_radius * p.notice &&
                    !world_.raycast(pos + Vec3{0, 1.6, 0}, p.pos + Vec3{0, 1.2, 0})) {
                    best = d;
                    mob.target = p.entity;
                    mob.lost_target_at = 0;
                }
            }
        }

        if (horiz_dist(pos, camp_states_[mob.camp].center) > arch.leash_radius) {
            mob.returning = true;
            mob.target = 0;
            mob.brain.reset();
            server_.steer_npc(mob.entity, input);
            continue;
        }

        if (mob.target != 0) {
            const MoveState* target_st = server_.state_of(mob.target);
            if (!target_st || !server_.alive(mob.target)) {
                mob.target = 0;
                mob.brain.reset();
                mob.returning = horiz_dist(pos, mob.home) > 12.0;
                server_.steer_npc(mob.entity, input);
                continue;
            }
            const bool los = !world_.raycast(pos + Vec3{0, 1.6, 0}, target_st->position + Vec3{0, 1.2, 0});
            if (los) {
                mob.lost_target_at = 0;
            } else {
                if (mob.lost_target_at <= 0) mob.lost_target_at = now;
                if (now - mob.lost_target_at > 8.0) {
                    mob.target = 0;
                    mob.brain.reset();
                    mob.returning = horiz_dist(pos, mob.home) > 12.0;
                    server_.steer_npc(mob.entity, input);
                    continue;
                }
            }
            CharacterStats stats;
            stats.max_health = arch.health;
            input = mob.brain.step_combat(info->state, info->health / std::max(1.0, info->max_health), mob.loadout,
                                          stats, mob.target, target_st->position, target_st->velocity, world_, nav_,
                                          arch.skill, now, rng_);
            server_.steer_npc(mob.entity, input);
            continue;
        }

        // Guard / Patrol inside camp
        if (horiz_dist(pos, mob.patrol_goal) <= 1.2) {
            if (now >= mob.wait_until) {
                mob.patrol_goal = random_point_around(camp_states_[mob.camp].center, camp_states_[mob.camp].radius * 0.7);
                mob.wait_until = now + 4.0 + random_unit() * 6.0;
            }
        } else {
            const Vec3 d{mob.patrol_goal.x - pos.x, 0, mob.patrol_goal.z - pos.z};
            input.yaw = float(std::atan2(-d.x, -d.z));
            input.forward = 95;
        }
        server_.steer_npc(mob.entity, input);
    }
}

std::vector<uint32_t> WorldPopulation::mob_entities() const {
    std::vector<uint32_t> ids;
    ids.reserve(creatures_.size() + bandits_.size());
    for (const CreatureMob& m : creatures_) ids.push_back(m.entity);
    for (const BanditMob& m : bandits_) {
        if (!m.gone) ids.push_back(m.entity);
    }
    return ids;
}

std::vector<uint32_t> WorldPopulation::spawn_mark(const std::string& name, Vec3 center, int tier, int guards) {
    std::vector<uint32_t> out;
    // The toughest bandits up to its tier (the weakest there are, if none is).
    std::vector<size_t> fit;
    int best = -1;
    for (size_t i = 0; i < spawns_.bandits.size(); ++i) {
        if (spawns_.bandits[i].tier <= tier) best = std::max(best, spawns_.bandits[i].tier);
    }
    if (best < 0) {
        for (const BanditArchetype& b : spawns_.bandits) best = best < 0 ? b.tier : std::min(best, b.tier);
    }
    for (size_t i = 0; i < spawns_.bandits.size(); ++i) {
        if (spawns_.bandits[i].tier == best) fit.push_back(i);
    }
    if (fit.empty()) return out;
    center = world_.clamp(center);
    center.y = world_.terrain().height_at(center.x, center.z);
    const size_t camp = camp_states_.size();
    camp_states_.push_back({center, 12.0, true});
    for (int m = 0; m <= guards; ++m) {
        const size_t arch_idx = fit[size_t(random_unit() * double(fit.size())) % fit.size()];
        const BanditArchetype& arch = spawns_.bandits[arch_idx];
        const auto lit = look_indices_.find(arch.template_path);
        GameServer::NpcCombatSpec spec;
        spec.actor_kind = 3;
        // The mark himself is tougher than his gang, more so the higher the tier.
        spec.max_health = m == 0 ? arch.health * (1.5 + 0.25 * tier) : arch.health;
        spec.hit_radius = 0.45;
        spec.hit_height = 1.8;
        spec.walk_speed = 4.2;
        spec.run_speed = 6.5;
        spec.loadout = build_bandit_loadout(arch);
        if (const WeaponDef* w = weapon_in_slot(spec.loadout, primary_slot)) {
            spec.weapon_model = w->model;
            spec.anim_set = w->anim_set;
        }
        const Vec3 pos = m == 0 ? center : random_point_around(center, 8.0);
        const uint32_t entity = server_.add_combat_npc(m == 0 ? name : arch.name, pos, random_unit() * 2.0 * std::numbers::pi,
                                                       lit != look_indices_.end() ? lit->second : 0, spec,
                                                       uint16_t(random_unit() * 99.0) + 1);
        BanditMob mob;
        mob.entity = entity;
        mob.archetype = arch_idx;
        mob.camp = camp;
        mob.home = pos;
        mob.patrol_goal = pos;
        mob.wait_until = random_unit() * 3.0;
        mob.loadout = std::move(spec.loadout);
        mob.brain.aim_phase = random_unit() * 100.0;
        camp_states_[camp].mobs.push_back(bandits_.size());
        by_entity_[entity] = {true, bandits_.size()};
        bandits_.push_back(std::move(mob));
        out.push_back(entity);
    }
    return out;
}

void WorldPopulation::retire_mark(const std::vector<uint32_t>& entities) {
    for (const uint32_t entity : entities) {
        const auto it = by_entity_.find(entity);
        if (it == by_entity_.end() || !it->second.first) continue;
        BanditMob& mob = bandits_[it->second.second];
        std::erase(camp_states_[mob.camp].mobs, it->second.second);
        mob.gone = true;
        mob.dormant = true;  // (never updated again)
        by_entity_.erase(it);
        server_.remove_npc(entity);
    }
}

int64_t WorldPopulation::xp_for(uint32_t entity) const {
    const auto it = by_entity_.find(entity);
    if (it == by_entity_.end()) return 0;
    const auto [bandit, index] = it->second;
    return bandit ? spawns_.bandits[bandits_[index].archetype].xp_reward
                  : spawns_.creatures[creatures_[index].archetype].xp_reward;
}

std::string WorldPopulation::loot_table_for(uint32_t entity) const {
    const auto it = by_entity_.find(entity);
    if (it == by_entity_.end()) return {};
    const auto [bandit, index] = it->second;
    return bandit ? spawns_.bandits[bandits_[index].archetype].loot_table
                  : spawns_.creatures[creatures_[index].archetype].loot_table;
}

KillTags WorldPopulation::kill_tags(uint32_t entity) const {
    const auto it = by_entity_.find(entity);
    if (it == by_entity_.end()) return {};
    const auto [bandit, index] = it->second;
    if (!bandit) {
        return {spawns_.creatures[creatures_[index].archetype].id, "", false};
    }
    const BanditArchetype& type = spawns_.bandits[bandits_[index].archetype];
    return {type.id, type.gang, true};
}

} // namespace swm::core
