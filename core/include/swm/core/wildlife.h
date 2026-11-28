#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "swm/core/bot.h"
#include "swm/core/items.h"
#include "swm/core/quest.h"
#include "swm/core/vec3.h"

namespace swm::core {

class GameServer;
class NavGrid;
class World;

enum class CreatureBehavior : uint8_t { passive = 0, herd = 1, territorial = 2, aggressive = 3 };

struct CreatureArchetype {
    std::string id;
    std::string name;
    std::string template_path;
    int tier = 1;
    CreatureBehavior behavior = CreatureBehavior::passive;
    double health = 120;
    double walk_speed = 2.6;
    double run_speed = 6.0;
    double radius = 0.6;
    double height = 1.2;
    double damage = 14;
    double attack_range = 2.4;
    double attack_cooldown = 1.4;
    double aggro_radius = 18;
    double leash_radius = 70;
    double respawn_time = 25;
    int64_t xp_reward = 25;
    std::string loot_table;
};

struct LairSpawn {
    std::string type_id;
    Vec3 position;  // game space
    double radius = 25;
    int count = 4;
};

struct BanditArchetype {
    std::string id;
    std::string name;
    std::string template_path;
    std::string gang = "Bandits";
    std::string faction;  // a faction trooper's ("imperial", "rebel": faction_troops.ini); empty: a bandit
    int tier = 1;
    double health = 220;
    BotSkill skill;
    std::string weapon_id = "wp_rifle_e11";
    double aggro_radius = 35;
    double leash_radius = 85;
    double respawn_time = 30;
    int64_t xp_reward = 45;
    std::string loot_table;
};

struct CampSpawn {
    std::string name;
    Vec3 position;  // game space
    double radius = 22;
    std::vector<std::string> members;
};

struct WorldSpawns {
    std::vector<CreatureArchetype> creatures;
    std::vector<LairSpawn> lairs;
    std::vector<BanditArchetype> bandits;
    std::vector<CampSpawn> camps;
};

WorldSpawns load_world_spawns(const std::filesystem::path& path);

// When a lair or camp is simulated (Stage 13). A whole planet's wildlife
// can't think all the time for a few players: a group is asleep (its living
// members out of the world, at home and healed; its dead waiting to come
// back) until a player comes within wake_radius of its edge, and goes back to
// sleep once none has been within wake_radius + sleep_margin for `linger`
// seconds and none of its members is fighting or going home.
struct PopulationActivity {
    double wake_radius = 300;   // 0: every group always awake
    double sleep_margin = 100;
    double linger = 15;
    double check_interval = 0.5;  // seconds between looks at who's near
    // A group's RespawnTime suits a busy server. With fewer players near
    // (within wake_radius of its edge) it waits longer: times
    // clamp(respawn_players / players near, 1, respawn_max_scale).
    double respawn_players = 4;
    double respawn_max_scale = 3;
};

class WorldPopulation {
public:
    WorldPopulation(GameServer& server, const World& world, const NavGrid* nav, const WorldSpawns& spawns,
                    const ItemCatalog* catalog, const std::map<std::string, uint8_t>& look_indices);

    // Before spawn_all().
    void set_activity(const PopulationActivity& activity) { activity_ = activity; }
    void spawn_all();
    void update(double now);

    size_t creature_count() const { return creatures_.size(); }
    size_t bandit_count() const { return bandits_.size(); }
    std::vector<uint32_t> mob_entities() const;
    int64_t xp_for(uint32_t entity) const;
    std::string loot_table_for(uint32_t entity) const;
    // What a creature or bandit counts as for quests (empty type id: neither).
    KillTags kill_tags(uint32_t entity) const;

    // Lairs and camps, and how many are awake; mobs asleep (out of the world).
    size_t group_count() const { return lair_states_.size() + camp_states_.size(); }
    size_t awake_groups() const;
    size_t dormant_mobs() const;
    bool lair_awake(size_t lair) const { return lair < lair_states_.size() && lair_states_[lair].awake; }
    bool camp_awake(size_t camp) const { return camp < camp_states_.size() && camp_states_[camp].awake; }
    // Whether a creature or bandit is asleep (out of the world until a player comes near).
    bool dormant(uint32_t entity) const;

    // A bounty mark (docs/PROFESSIONS.md, Bounty Hunter): a named outlaw and
    // `guards` gang members at `center`, of bandits up to `tier`, who fight
    // like any camp's but never come back once dead. Their entities, the
    // leader first; empty if there are no bandits to make them of. With a
    // `faction`, of its troopers instead (a faction terminal's destroy
    // mission), the leader named for his type when `name` is empty.
    std::vector<uint32_t> spawn_mark(const std::string& name, Vec3 center, int tier, int guards,
                                     std::string_view faction = {});
    // A Ranger's quarry (docs/PROFESSIONS.md): one named creature of a type
    // up to `tier`, tougher than its kind, that never comes back once dead.
    // Its entity, or 0 if there are no creatures to make it of.
    uint32_t spawn_quarry(const std::string& name, Vec3 center, int tier);
    // Takes a mark's outlaws or a quarry (spawn_mark's, spawn_quarry's
    // entities) out of the world for good.
    void retire_mark(const std::vector<uint32_t>& entities);

private:
    struct CreatureMob {
        uint32_t entity = 0;
        size_t archetype = 0;
        size_t lair = 0;
        Vec3 home;
        Vec3 goal;
        double wait_until = 0;
        uint8_t pose = 0;
        double attack_pose_until = 0;
        double next_attack = 0;
        uint32_t target = 0;
        double flee_until = 0;
        bool returning = false;
        double dead_since = -1;
        bool corpse_hidden = false;
        uint32_t last_seen_damage_tick = 0;
        bool dormant = false;  // alive, out of the world while its lair sleeps
        bool gone = false;     // a retired quarry's: out of the world for good
    };

    struct LairState {
        Vec3 center;
        double radius = 25;
        bool temporary = false;  // a quarry's: its dead stay dead
        Vec3 herd_anchor;
        double next_herd_move = 0;
        bool awake = true;
        double quiet_since = -1;    // when no player was last near (or -1)
        std::vector<size_t> mobs;   // indices into creatures_
    };

    struct BanditMob {
        uint32_t entity = 0;
        size_t archetype = 0;
        size_t camp = 0;
        Vec3 home;
        Vec3 patrol_goal;
        double wait_until = 0;
        uint32_t target = 0;
        double lost_target_at = 0;
        bool returning = false;
        double dead_since = -1;
        bool corpse_hidden = false;
        uint32_t last_seen_damage_tick = 0;
        Loadout loadout;
        CombatBrain brain;
        bool dormant = false;
        bool gone = false;  // a retired mark's: out of the world for good
    };

    struct CampState {
        Vec3 center;
        double radius = 22;
        bool temporary = false;  // a bounty mark's: its dead stay dead
        bool awake = true;
        double quiet_since = -1;
        std::vector<size_t> mobs;  // indices into bandits_
    };

    struct PlayerPos {
        uint32_t entity;
        Vec3 pos;
        Vec3 vel;
        double notice = 1;  // times how far it's noticed from (a perk's: Quiet Step)
    };

    double random_unit();
    Vec3 random_point_around(Vec3 center, double radius);
    const CreatureArchetype* find_creature_type(const std::string& id, size_t& out_index) const;
    const BanditArchetype* find_bandit_type(const std::string& id, size_t& out_index) const;
    Loadout build_bandit_loadout(const BanditArchetype& arch) const;
    void update_creatures(double now);
    void update_bandits(double now);
    void update_activity(double now);
    void sleep_creature(CreatureMob& mob);
    void sleep_bandit(BanditMob& mob);
    // How many times its RespawnTime a group's dead wait (PopulationActivity).
    double respawn_scale(Vec3 center, double radius) const;

    GameServer& server_;
    const World& world_;
    const NavGrid* nav_;
    WorldSpawns spawns_;
    const ItemCatalog* catalog_;
    std::map<std::string, uint8_t> look_indices_;
    std::vector<LairState> lair_states_;
    std::vector<CreatureMob> creatures_;
    std::vector<CampState> camp_states_;
    std::vector<BanditMob> bandits_;
    uint64_t rng_ = 0x5441544F4F494E45ull;
    double last_update_ = 0;
    PopulationActivity activity_;
    double next_activity_check_ = 0;
    std::vector<PlayerPos> players_;  // this update's, alive
    // entity -> (bandit?, index into creatures_ or bandits_)
    std::map<uint32_t, std::pair<bool, size_t>> by_entity_;
};

} // namespace swm::core
