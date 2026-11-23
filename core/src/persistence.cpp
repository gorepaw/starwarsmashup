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

std::vector<CharacterRecord> MemoryCharacterStore::list_all() {
    std::vector<CharacterRecord> out;
    out.reserve(characters_.size());
    for (const auto& [id, character] : characters_) {
        out.push_back(character);
    }
    return out;
}

CharacterRecord MemoryCharacterStore::create_character(const CharacterRecord& input) {
    if (find_by_name(input.name)) {
        throw std::runtime_error("character name taken: " + input.name);
    }
    CharacterRecord record = input;
    record.id = next_id_++;
    if (record.species.empty()) record.species = "human";
    if (record.gender.empty()) record.gender = "male";
    if (record.location.empty()) record.location = "Mos Eisley";
    if (record.appearance.empty()) record.appearance = "{}";
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

bool MemoryCharacterStore::remove(PersistentId id) {
    return characters_.erase(id) > 0;
}

} // namespace swm::core
