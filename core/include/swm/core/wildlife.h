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
    int tier = 1;
    double health = 220;
    BotSkill skill;
    std::string weapon_id = "wp_carbine_e11";
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

class WorldPopulation {
public:
    WorldPopulation(GameServer& server, const World& world, const NavGrid* nav, const WorldSpawns& spawns,
                    const ItemCatalog* catalog, const std::map<std::string, uint8_t>& look_indices);

    void spawn_all();
    void update(double now);

    size_t creature_count() const { return creatures_.size(); }
    size_t bandit_count() const { return bandits_.size(); }
    std::vector<uint32_t> mob_entities() const;
    int64_t xp_for(uint32_t entity) const;
    std::string loot_table_for(uint32_t entity) const;
    // What a creature or bandit counts as for quests (empty type id: neither).
    KillTags kill_tags(uint32_t entity) const;

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
    };

    struct LairState {
        Vec3 center;
        double radius = 25;
        Vec3 herd_anchor;
        double next_herd_move = 0;
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
    };

    struct CampState {
        Vec3 center;
        double radius = 22;
    };

    double random_unit();
    Vec3 random_point_around(Vec3 center, double radius);
    const CreatureArchetype* find_creature_type(const std::string& id, size_t& out_index) const;
    const BanditArchetype* find_bandit_type(const std::string& id, size_t& out_index) const;
    Loadout build_bandit_loadout(const BanditArchetype& arch) const;
    void update_creatures(double now);
    void update_bandits(double now);

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
};

} // namespace swm::core
