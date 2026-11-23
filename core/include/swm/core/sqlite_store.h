#pragma once

#include <filesystem>

#include "swm/core/persistence.h"

struct sqlite3;
struct sqlite3_stmt;

namespace swm::core {

// CharacterStore in a SQLite database file, created if missing. Lives in its
// own library (swm_sqlite_store) so core/ itself doesn't depend on SQLite; a
// Postgres store would sit beside it. Not thread-safe: one per thread.
class SqliteCharacterStore : public CharacterStore {
public:
    explicit SqliteCharacterStore(const std::filesystem::path& path);
    ~SqliteCharacterStore() override;
    SqliteCharacterStore(const SqliteCharacterStore&) = delete;
    SqliteCharacterStore& operator=(const SqliteCharacterStore&) = delete;

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
    std::vector<QuestProgress> list_quests(PersistentId owner_id) override;
    void save_quests(PersistentId owner_id, std::span<const QuestProgress> quests) override;

    // The schema version this build writes (PRAGMA user_version).
    static constexpr int schema_version = 4;

private:
    void close();
    void exec(const char* sql);
    std::optional<CharacterRecord> find_one(sqlite3_stmt* statement);
    static CharacterRecord read_row(sqlite3_stmt* statement);

    sqlite3* db_ = nullptr;
    sqlite3_stmt* find_ = nullptr;
    sqlite3_stmt* find_by_name_ = nullptr;
    sqlite3_stmt* list_all_ = nullptr;
    sqlite3_stmt* insert_ = nullptr;
    sqlite3_stmt* update_ = nullptr;
    sqlite3_stmt* delete_ = nullptr;
    sqlite3_stmt* list_items_ = nullptr;
    sqlite3_stmt* grant_item_ = nullptr;
    sqlite3_stmt* remove_item_ = nullptr;
    sqlite3_stmt* remove_items_by_owner_ = nullptr;
    sqlite3_stmt* list_loadouts_ = nullptr;
    sqlite3_stmt* save_loadout_ = nullptr;
    sqlite3_stmt* remove_loadouts_by_owner_ = nullptr;
    sqlite3_stmt* update_economy_ = nullptr;
    sqlite3_stmt* list_quests_ = nullptr;
    sqlite3_stmt* insert_quest_ = nullptr;
    sqlite3_stmt* remove_quests_by_owner_ = nullptr;
};

} // namespace swm::core
