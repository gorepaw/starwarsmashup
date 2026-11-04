#include "swm/core/persistence.h"

#include <stdexcept>

namespace swm::core {

std::optional<CharacterRecord> MemoryCharacterStore::find(PersistentId id) {
    auto it = characters_.find(id);
    return it == characters_.end() ? std::nullopt : std::optional(it->second);
}

std::optional<CharacterRecord> MemoryCharacterStore::find_by_name(const std::string& name) {
    for (const auto& [id, character] : characters_) {
        if (character.name == name) {
            return character;
        }
    }
    return std::nullopt;
}

CharacterRecord MemoryCharacterStore::create(const std::string& name, Vec3 position, float yaw) {
    if (find_by_name(name)) {
        throw std::runtime_error("character name taken: " + name);
    }
    CharacterRecord record{next_id_++, name, position, yaw};
    characters_[record.id] = record;
    return record;
}

void MemoryCharacterStore::save(std::span<const CharacterRecord> characters) {
    for (const CharacterRecord& character : characters) {
        if (!characters_.contains(character.id)) {
            throw std::runtime_error("no character " + std::to_string(character.id));
        }
    }
    for (const CharacterRecord& character : characters) {
        characters_[character.id] = character;
    }
}

} // namespace swm::core
