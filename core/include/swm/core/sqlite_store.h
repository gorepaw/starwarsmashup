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

    // The schema version this build writes (PRAGMA user_version).
    static constexpr int schema_version = 2;

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
};

} // namespace swm::core
