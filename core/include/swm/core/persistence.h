#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "swm/core/items.h"
#include "swm/core/vec3.h"

namespace swm::core {

// Identifies anything that's saved (characters now; items, structures and
// resource spawns later). Never reused, and never 0.
using PersistentId = uint64_t;

// A saved character.
struct CharacterRecord {
    PersistentId id = 0;
    std::string name;  // unique; stands in for an account login until there are accounts
    Vec3 position;     // game space
    float yaw = 0;
    std::string species = "human";
    std::string gender = "male";
    std::string location = "Mos Eisley";
    std::string appearance = "{}";
    int64_t credits = 0;
    int64_t faction_points = 0;
    int64_t experience = 0;
    int active_loadout = 0;

    friend bool operator==(const CharacterRecord&, const CharacterRecord&) = default;
};

// Durable storage for characters. Game code talks only to this interface,
// never to a database (see docs/ARCHITECTURE.md#persistence). Calls are
// synchronous for now. Implementations throw std::runtime_error on storage
// failures.
class CharacterStore {
public:
    virtual ~CharacterStore() = default;

    virtual std::optional<CharacterRecord> find(PersistentId id) = 0;
    virtual std::optional<CharacterRecord> find_by_name(const std::string& name) = 0;
    virtual std::vector<CharacterRecord> list_all() = 0;
    // Saves a new character and returns it with its newly assigned id.
    // Throws std::runtime_error if the name is taken.
    virtual CharacterRecord create_character(const CharacterRecord& record) = 0;
    virtual CharacterRecord create(const std::string& name, Vec3 position, float yaw) {
        return create_character({.name = name, .position = position, .yaw = yaw});
    }
    // Updates existing characters, all or none.
    virtual void save(std::span<const CharacterRecord> characters) = 0;
    virtual bool remove(PersistentId id) = 0;

    // Inventory, loadouts, and character economy.
    virtual std::vector<ItemInstance> list_items(PersistentId owner_id) = 0;
    virtual ItemInstance grant_item(const ItemInstance& item) = 0;
    virtual bool remove_item(PersistentId item_id) = 0;
    virtual std::vector<CharacterLoadout> list_loadouts(PersistentId owner_id) = 0;
    virtual void save_loadout(PersistentId owner_id, const CharacterLoadout& loadout) = 0;
    virtual void update_economy(PersistentId id, int64_t credits, int64_t faction_points, int64_t experience,
                                int active_loadout) = 0;
};

// Keeps everything in memory, for tests.
class MemoryCharacterStore : public CharacterStore {
public:
    std::optional<CharacterRecord> find(PersistentId id) override;
    std::optional<CharacterRecord> find_by_name(const std::string& name) override;
    std::vector<CharacterRecord> list_all() override;
    CharacterRecord create_character(const CharacterRecord& record) override;
    void save(std::span<const CharacterRecord> characters) override;
    bool remove(PersistentId id) override;

    std::vector<ItemInstance> list_items(PersistentId owner_id) override;
    ItemInstance grant_item(const ItemInstance& item) override;
    bool remove_item(PersistentId item_id) override;
    std::vector<CharacterLoadout> list_loadouts(PersistentId owner_id) override;
    void save_loadout(PersistentId owner_id, const CharacterLoadout& loadout) override;
    void update_economy(PersistentId id, int64_t credits, int64_t faction_points, int64_t experience,
                        int active_loadout) override;

private:
    std::map<PersistentId, CharacterRecord> characters_;
    PersistentId next_id_ = 1;
    std::map<PersistentId, ItemInstance> items_;
    PersistentId next_item_id_ = 1;
    std::map<PersistentId, std::map<int, CharacterLoadout>> loadouts_;
};

// Grants the standard-issue starter kit and Loadout 0 if the character has no items yet.
void ensure_starter_kit(CharacterStore& store, PersistentId char_id);

} // namespace swm::core
