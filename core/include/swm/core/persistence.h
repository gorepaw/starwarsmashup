#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>

#include "swm/core/vec3.h"

namespace swm::core {

// Identifies anything that's saved (characters now; items, structures and
// resource spawns later). Never reused, and never 0.
using PersistentId = uint64_t;

// A saved character. Only what spike 5 needs so far: where it stands.
struct CharacterRecord {
    PersistentId id = 0;
    std::string name;  // unique; stands in for an account login until there are accounts
    Vec3 position;     // game space
    float yaw = 0;

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
    // Saves a new character and returns it with its newly assigned id.
    // Throws std::runtime_error if the name is taken.
    virtual CharacterRecord create(const std::string& name, Vec3 position, float yaw) = 0;
    // Updates existing characters, all or none.
    virtual void save(std::span<const CharacterRecord> characters) = 0;
};

// Keeps everything in memory, for tests.
class MemoryCharacterStore : public CharacterStore {
public:
    std::optional<CharacterRecord> find(PersistentId id) override;
    std::optional<CharacterRecord> find_by_name(const std::string& name) override;
    CharacterRecord create(const std::string& name, Vec3 position, float yaw) override;
    void save(std::span<const CharacterRecord> characters) override;

private:
    std::map<PersistentId, CharacterRecord> characters_;
    PersistentId next_id_ = 1;
};

} // namespace swm::core
