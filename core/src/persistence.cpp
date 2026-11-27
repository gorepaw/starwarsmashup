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
    materials_.erase(id);
    presets_.erase(id);
    reputation_.erase(id);
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

bool MemoryCharacterStore::update_item_colors(PersistentId item_id, const std::string& color_json) {
    auto it = items_.find(item_id);
    if (it == items_.end()) return false;
    it->second.color_json = color_json;
    return true;
}

bool MemoryCharacterStore::update_item_quantity(PersistentId item_id, int quantity) {
    auto it = items_.find(item_id);
    if (it == items_.end()) return false;
    it->second.quantity = quantity;
    return true;
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

std::vector<MaterialStack> MemoryCharacterStore::list_materials(PersistentId owner_id) {
    const auto it = materials_.find(owner_id);
    return it == materials_.end() ? std::vector<MaterialStack>{} : it->second;
}

void MemoryCharacterStore::save_materials(PersistentId owner_id, std::span<const MaterialStack> materials) {
    materials_[owner_id].assign(materials.begin(), materials.end());
}

std::vector<RecipePreset> MemoryCharacterStore::list_presets(PersistentId owner_id) {
    const auto it = presets_.find(owner_id);
    return it == presets_.end() ? std::vector<RecipePreset>{} : it->second;
}

void MemoryCharacterStore::save_preset(PersistentId owner_id, const RecipePreset& preset) {
    auto& list = presets_[owner_id];
    for (RecipePreset& p : list) {
        if (p.name == preset.name) {
            p = preset;
            return;
        }
    }
    list.push_back(preset);
}

bool MemoryCharacterStore::delete_preset(PersistentId owner_id, const std::string& name) {
    auto it = presets_.find(owner_id);
    if (it == presets_.end()) return false;
    return std::erase_if(it->second, [&](const RecipePreset& p) { return p.name == name; }) > 0;
}

std::map<std::string, int64_t> MemoryCharacterStore::list_reputation(PersistentId owner_id) {
    const auto it = reputation_.find(owner_id);
    return it == reputation_.end() ? std::map<std::string, int64_t>{} : it->second;
}

void MemoryCharacterStore::save_reputation(PersistentId owner_id, const std::map<std::string, int64_t>& reputation) {
    reputation_[owner_id] = reputation;
}

std::string MemoryCharacterStore::get_extra(PersistentId owner_id, const std::string& key) {
    const auto owner = extras_.find(owner_id);
    if (owner == extras_.end()) return {};
    const auto it = owner->second.find(key);
    return it == owner->second.end() ? std::string() : it->second;
}

void MemoryCharacterStore::set_extra(PersistentId owner_id, const std::string& key, const std::string& value) {
    extras_[owner_id][key] = value;
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
    const auto ammo_std = store.grant_item({0, char_id, "ammo_tibanna_standard", 0.5, "", "{}", 250});
    grant("cloth_jacket_rebel");
    const auto shirt = grant("cloth_shirt_tunic");
    const auto pants = grant("cloth_pants_cargo");
    const auto boots = grant("cloth_boots_leather");

    CharacterLoadout lo;
    lo.slot_index = 0;
    lo.name = "Standard Issue";
    lo.primary_id = primary.id;
    lo.sidearm_id = sidearm.id;
    lo.secondary_id = grenade.id;
    lo.gear_ids["deflector_shield"] = psg.id;
    lo.gear_ids["ammo_standard"] = ammo_std.id;
    lo.gear_ids["chest"] = shirt.id;  // clothes are worn like armor: unarmored
    lo.gear_ids["leggings"] = pants.id;
    lo.gear_ids["boots"] = boots.id;
    store.save_loadout(char_id, lo);
    if (ch->credits == 0 && ch->faction_points == 0 && ch->experience == 0) {
        store.update_economy(char_id, 500, 100, 0, 0);
    }
}

int64_t MemoryCharacterStore::record_match(const MatchTelemetry& match) {
    MatchTelemetry stored = match;
    stored.id = int64_t(matches_.size()) + 1;
    matches_.push_back(std::move(stored));
    return matches_.back().id;
}

std::vector<MatchTelemetry> MemoryCharacterStore::list_matches(size_t limit) {
    std::vector<MatchTelemetry> out;
    for (auto it = matches_.rbegin(); it != matches_.rend() && out.size() < limit; ++it) {
        out.push_back(*it);
    }
    return out;
}

} // namespace swm::core
