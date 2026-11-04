#include "swm/core/sqlite_store.h"

#include <stdexcept>
#include <string>

#include <sqlite3.h>

namespace swm::core {

namespace {

[[noreturn]] void fail(sqlite3* db, const std::string& what) {
    throw std::runtime_error("sqlite: " + what + ": " + (db ? sqlite3_errmsg(db) : "out of memory"));
}

sqlite3_stmt* prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, &statement, nullptr) != SQLITE_OK) {
        fail(db, std::string("prepare ") + sql);
    }
    return statement;
}

// Resets a statement when leaving scope, so it can run again.
struct Reset {
    sqlite3_stmt* statement;
    ~Reset() {
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
    }
};

} // namespace

SqliteCharacterStore::SqliteCharacterStore(const std::filesystem::path& path) {
    const std::string file = path.string();
    if (sqlite3_open_v2(file.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        const std::string message = db_ ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        throw std::runtime_error("sqlite: cannot open " + file + ": " + message);
    }
    try {
        sqlite3_busy_timeout(db_, 2000);
        exec("PRAGMA journal_mode = WAL");
        exec("PRAGMA synchronous = NORMAL");

        sqlite3_stmt* version_query = prepare(db_, "PRAGMA user_version");
        const int version = sqlite3_step(version_query) == SQLITE_ROW ? sqlite3_column_int(version_query, 0) : -1;
        sqlite3_finalize(version_query);
        if (version == 0) {
            // AUTOINCREMENT: ids are never reused, even after deletes.
            exec("BEGIN");
            exec("CREATE TABLE characters ("
                 " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 " name TEXT NOT NULL UNIQUE,"
                 " x REAL NOT NULL, y REAL NOT NULL, z REAL NOT NULL,"
                 " yaw REAL NOT NULL"
                 ") STRICT");
            exec(("PRAGMA user_version = " + std::to_string(schema_version)).c_str());
            exec("COMMIT");
        } else if (version != schema_version) {
            throw std::runtime_error("sqlite: " + file + " has schema version " + std::to_string(version) +
                                     ", this build reads version " + std::to_string(schema_version));
        }

        find_ = prepare(db_, "SELECT id, name, x, y, z, yaw FROM characters WHERE id = ?");
        find_by_name_ = prepare(db_, "SELECT id, name, x, y, z, yaw FROM characters WHERE name = ?");
        insert_ = prepare(db_, "INSERT INTO characters (name, x, y, z, yaw) VALUES (?, ?, ?, ?, ?)");
        update_ = prepare(db_, "UPDATE characters SET x = ?, y = ?, z = ?, yaw = ? WHERE id = ?");
    } catch (...) {
        close();
        throw;
    }
}

SqliteCharacterStore::~SqliteCharacterStore() {
    close();
}

void SqliteCharacterStore::close() {
    for (sqlite3_stmt* statement : {find_, find_by_name_, insert_, update_}) {
        sqlite3_finalize(statement);
    }
    find_ = find_by_name_ = insert_ = update_ = nullptr;
    sqlite3_close(db_);
    db_ = nullptr;
}

void SqliteCharacterStore::exec(const char* sql) {
    if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        fail(db_, sql);
    }
}

std::optional<CharacterRecord> SqliteCharacterStore::find_one(sqlite3_stmt* statement) {
    const int step = sqlite3_step(statement);
    if (step == SQLITE_DONE) {
        return std::nullopt;
    }
    if (step != SQLITE_ROW) {
        fail(db_, "query");
    }
    CharacterRecord record;
    record.id = PersistentId(sqlite3_column_int64(statement, 0));
    record.name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
    record.position = {sqlite3_column_double(statement, 2), sqlite3_column_double(statement, 3),
                       sqlite3_column_double(statement, 4)};
    record.yaw = float(sqlite3_column_double(statement, 5));
    return record;
}

std::optional<CharacterRecord> SqliteCharacterStore::find(PersistentId id) {
    Reset reset{find_};
    sqlite3_bind_int64(find_, 1, sqlite3_int64(id));
    return find_one(find_);
}

std::optional<CharacterRecord> SqliteCharacterStore::find_by_name(const std::string& name) {
    Reset reset{find_by_name_};
    sqlite3_bind_text(find_by_name_, 1, name.c_str(), int(name.size()), SQLITE_TRANSIENT);
    return find_one(find_by_name_);
}

CharacterRecord SqliteCharacterStore::create(const std::string& name, Vec3 position, float yaw) {
    Reset reset{insert_};
    sqlite3_bind_text(insert_, 1, name.c_str(), int(name.size()), SQLITE_TRANSIENT);
    sqlite3_bind_double(insert_, 2, position.x);
    sqlite3_bind_double(insert_, 3, position.y);
    sqlite3_bind_double(insert_, 4, position.z);
    sqlite3_bind_double(insert_, 5, yaw);
    if (sqlite3_step(insert_) != SQLITE_DONE) {
        fail(db_, "create character " + name);
    }
    return {PersistentId(sqlite3_last_insert_rowid(db_)), name, position, yaw};
}

void SqliteCharacterStore::save(std::span<const CharacterRecord> characters) {
    exec("BEGIN");
    try {
        for (const CharacterRecord& character : characters) {
            Reset reset{update_};
            sqlite3_bind_double(update_, 1, character.position.x);
            sqlite3_bind_double(update_, 2, character.position.y);
            sqlite3_bind_double(update_, 3, character.position.z);
            sqlite3_bind_double(update_, 4, character.yaw);
            sqlite3_bind_int64(update_, 5, sqlite3_int64(character.id));
            if (sqlite3_step(update_) != SQLITE_DONE) {
                fail(db_, "save character");
            }
            if (sqlite3_changes(db_) != 1) {
                throw std::runtime_error("sqlite: no character " + std::to_string(character.id));
            }
        }
        exec("COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

} // namespace swm::core
