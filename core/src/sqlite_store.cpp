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
                 " yaw REAL NOT NULL,"
                 " species TEXT NOT NULL DEFAULT 'human',"
                 " gender TEXT NOT NULL DEFAULT 'male',"
                 " location TEXT NOT NULL DEFAULT 'Mos Eisley',"
                 " appearance TEXT NOT NULL DEFAULT '{}'"
                 ") STRICT");
            exec(("PRAGMA user_version = " + std::to_string(schema_version)).c_str());
            exec("COMMIT");
        } else if (version == 1) {
            exec("BEGIN");
            exec("ALTER TABLE characters ADD COLUMN species TEXT NOT NULL DEFAULT 'human'");
            exec("ALTER TABLE characters ADD COLUMN gender TEXT NOT NULL DEFAULT 'male'");
            exec("ALTER TABLE characters ADD COLUMN location TEXT NOT NULL DEFAULT 'Mos Eisley'");
            exec("ALTER TABLE characters ADD COLUMN appearance TEXT NOT NULL DEFAULT '{}'");
            exec(("PRAGMA user_version = " + std::to_string(schema_version)).c_str());
            exec("COMMIT");
        } else if (version != schema_version) {
            throw std::runtime_error("sqlite: " + file + " has schema version " + std::to_string(version) +
                                     ", this build reads version " + std::to_string(schema_version));
        }

        constexpr const char* cols = "id, name, x, y, z, yaw, species, gender, location, appearance";
        find_ = prepare(db_, (std::string("SELECT ") + cols + " FROM characters WHERE id = ?").c_str());
        find_by_name_ = prepare(db_, (std::string("SELECT ") + cols + " FROM characters WHERE name = ?").c_str());
        list_all_ = prepare(db_, (std::string("SELECT ") + cols + " FROM characters ORDER BY id ASC").c_str());
        insert_ = prepare(db_, "INSERT INTO characters (name, x, y, z, yaw, species, gender, location, appearance) "
                               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        update_ = prepare(db_, "UPDATE characters SET x = ?, y = ?, z = ?, yaw = ?, location = ?, appearance = ? WHERE id = ?");
        delete_ = prepare(db_, "DELETE FROM characters WHERE id = ?");
    } catch (...) {
        close();
        throw;
    }
}

SqliteCharacterStore::~SqliteCharacterStore() {
    close();
}

void SqliteCharacterStore::close() {
    for (sqlite3_stmt* statement : {find_, find_by_name_, list_all_, insert_, update_, delete_}) {
        sqlite3_finalize(statement);
    }
    find_ = find_by_name_ = list_all_ = insert_ = update_ = delete_ = nullptr;
    sqlite3_close(db_);
    db_ = nullptr;
}

void SqliteCharacterStore::exec(const char* sql) {
    if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        fail(db_, sql);
    }
}

CharacterRecord SqliteCharacterStore::read_row(sqlite3_stmt* statement) {
    auto col_str = [&](int col) {
        const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(statement, col));
        return txt ? std::string(txt) : std::string();
    };
    CharacterRecord record;
    record.id = PersistentId(sqlite3_column_int64(statement, 0));
    record.name = col_str(1);
    record.position = {sqlite3_column_double(statement, 2), sqlite3_column_double(statement, 3),
                       sqlite3_column_double(statement, 4)};
    record.yaw = float(sqlite3_column_double(statement, 5));
    record.species = col_str(6);
    record.gender = col_str(7);
    record.location = col_str(8);
    record.appearance = col_str(9);
    return record;
}

std::optional<CharacterRecord> SqliteCharacterStore::find_one(sqlite3_stmt* statement) {
    const int step = sqlite3_step(statement);
    if (step == SQLITE_DONE) {
        return std::nullopt;
    }
    if (step != SQLITE_ROW) {
        fail(db_, "query");
    }
    return read_row(statement);
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

std::vector<CharacterRecord> SqliteCharacterStore::list_all() {
    Reset reset{list_all_};
    std::vector<CharacterRecord> out;
    for (;;) {
        const int step = sqlite3_step(list_all_);
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            fail(db_, "list_all");
        }
        out.push_back(read_row(list_all_));
    }
    return out;
}

CharacterRecord SqliteCharacterStore::create_character(const CharacterRecord& input) {
    CharacterRecord record = input;
    if (record.species.empty()) record.species = "human";
    if (record.gender.empty()) record.gender = "male";
    if (record.location.empty()) record.location = "Mos Eisley";
    if (record.appearance.empty()) record.appearance = "{}";

    Reset reset{insert_};
    sqlite3_bind_text(insert_, 1, record.name.c_str(), int(record.name.size()), SQLITE_TRANSIENT);
    sqlite3_bind_double(insert_, 2, record.position.x);
    sqlite3_bind_double(insert_, 3, record.position.y);
    sqlite3_bind_double(insert_, 4, record.position.z);
    sqlite3_bind_double(insert_, 5, record.yaw);
    sqlite3_bind_text(insert_, 6, record.species.c_str(), int(record.species.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_, 7, record.gender.c_str(), int(record.gender.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_, 8, record.location.c_str(), int(record.location.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_, 9, record.appearance.c_str(), int(record.appearance.size()), SQLITE_TRANSIENT);
    if (sqlite3_step(insert_) != SQLITE_DONE) {
        fail(db_, "create character " + record.name);
    }
    record.id = PersistentId(sqlite3_last_insert_rowid(db_));
    return record;
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
            sqlite3_bind_text(update_, 5, character.location.c_str(), int(character.location.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(update_, 6, character.appearance.c_str(), int(character.appearance.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int64(update_, 7, sqlite3_int64(character.id));
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

bool SqliteCharacterStore::remove(PersistentId id) {
    Reset reset{delete_};
    sqlite3_bind_int64(delete_, 1, sqlite3_int64(id));
    if (sqlite3_step(delete_) != SQLITE_DONE) {
        fail(db_, "delete character");
    }
    return sqlite3_changes(db_) > 0;
}

} // namespace swm::core
