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
    loadouts_.erase(id);
    quests_.erase(id);
    professions_.erase(id);
    std::erase_if(items_, [id](const auto& kv) { return kv.second.owner_id == id; });
    return characters_.erase(id) > 0;
}

std::vector<ItemInstance> MemoryCharacterStore::list_items(PersistentId owner_id) {
    std::vector<ItemInstance> out;
    for (const auto& [id, inst] : items_) {
        if (inst.owner_id == owner_id) {
            out.push_back(inst);
        }
    }
    return out;
}

ItemInstance MemoryCharacterStore::grant_item(const ItemInstance& item) {
    ItemInstance copy = item;
    copy.id = next_item_id_++;
    if (copy.color_json.empty()) copy.color_json = "{}";
    items_[copy.id] = copy;
    return copy;
}

bool MemoryCharacterStore::remove_item(PersistentId item_id) {
    return items_.erase(item_id) > 0;
}

std::vector<CharacterLoadout> MemoryCharacterStore::list_loadouts(PersistentId owner_id) {
    std::vector<CharacterLoadout> out;
    if (const auto it = loadouts_.find(owner_id); it != loadouts_.end()) {
        for (const auto& [slot, lo] : it->second) {
            out.push_back(lo);
        }
    }
    return out;
}

void MemoryCharacterStore::save_loadout(PersistentId owner_id, const CharacterLoadout& loadout) {
    loadouts_[owner_id][loadout.slot_index] = loadout;
}

void MemoryCharacterStore::update_economy(PersistentId id, int64_t credits, int64_t faction_points, int64_t experience,
                                          int active_loadout) {
    auto it = characters_.find(id);
    if (it == characters_.end()) {
        throw std::runtime_error("no character " + std::to_string(id));
    }
    it->second.credits = credits;
    it->second.faction_points = faction_points;
    it->second.experience = experience;
    it->second.active_loadout = active_loadout;
}

std::vector<QuestProgress> MemoryCharacterStore::list_quests(PersistentId owner_id) {
    const auto it = quests_.find(owner_id);
    return it == quests_.end() ? std::vector<QuestProgress>{} : it->second;
}

void MemoryCharacterStore::save_quests(PersistentId owner_id, std::span<const QuestProgress> quests) {
    quests_[owner_id].assign(quests.begin(), quests.end());
}

std::vector<ProfessionProgress> MemoryCharacterStore::list_professions(PersistentId owner_id) {
    const auto it = professions_.find(owner_id);
    return it == professions_.end() ? std::vector<ProfessionProgress>{} : it->second;
}

void MemoryCharacterStore::save_professions(PersistentId owner_id, std::span<const ProfessionProgress> professions) {
    professions_[owner_id].assign(professions.begin(), professions.end());
}

void ensure_starter_kit(CharacterStore& store, PersistentId char_id) {
    if (!store.list_items(char_id).empty()) {
        return;
    }
    const auto ch = store.find(char_id);
    if (!ch) {
        return;
    }
    const auto grant = [&](const char* def_id) {
        return store.grant_item({0, char_id, def_id, 0.5, "", "{}"});
    };
    const auto primary = grant("wp_rifle_e11");
    const auto sidearm = grant("wp_pistol_dh17");
    const auto grenade = grant("wp_grenade_c22");
    const auto psg = grant("psg_mark1");
    grant("cloth_jacket_rebel");
    const auto pants = grant("cloth_pants_cargo");
    const auto boots = grant("cloth_boots_leather");

    CharacterLoadout lo;
    lo.slot_index = 0;
    lo.name = "Standard Issue";
    lo.primary_id = primary.id;
    lo.sidearm_id = sidearm.id;
    lo.secondary_id = grenade.id;
    lo.gear_ids["deflector_shield"] = psg.id;
    lo.appearance_ids["pants"] = pants.id;
    lo.appearance_ids["boots_cosmetic"] = boots.id;
    store.save_loadout(char_id, lo);
    if (ch->credits == 0 && ch->faction_points == 0 && ch->experience == 0) {
        store.update_economy(char_id, 500, 100, 0, 0);
    }
}

} // namespace swm::core
