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
    CharacterRecord create(const std::string& name, Vec3 position, float yaw) override;
    void save(std::span<const CharacterRecord> characters) override;

    // The schema version this build writes (PRAGMA user_version).
    static constexpr int schema_version = 1;

private:
    void close();
    void exec(const char* sql);
    std::optional<CharacterRecord> find_one(sqlite3_stmt* statement);

    sqlite3* db_ = nullptr;
    sqlite3_stmt* find_ = nullptr;
    sqlite3_stmt* find_by_name_ = nullptr;
    sqlite3_stmt* insert_ = nullptr;
    sqlite3_stmt* update_ = nullptr;
};

} // namespace swm::core
