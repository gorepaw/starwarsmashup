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

std::string encode_id_map(const std::map<std::string, uint64_t>& map) {
    std::string out = "{";
    bool first = true;
    for (const auto& [k, v] : map) {
        if (v == 0) continue;
        if (!first) out += ",";
        first = false;
        out += "\"" + k + "\":" + std::to_string(v);
    }
    out += "}";
    return out;
}

std::map<std::string, uint64_t> decode_id_map(const std::string& json) {
    std::map<std::string, uint64_t> out;
    size_t i = 0;
    while (i < json.size()) {
        const size_t q1 = json.find('"', i);
        if (q1 == std::string::npos) break;
        const size_t q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        const std::string key = json.substr(q1 + 1, q2 - q1 - 1);
        const size_t colon = json.find(':', q2 + 1);
        if (colon == std::string::npos) break;
        size_t pos = colon + 1;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
        uint64_t val = 0;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
            val = val * 10 + uint64_t(json[pos] - '0');
            ++pos;
        }
        if (val > 0) {
            out[key] = val;
        }
        i = pos;
    }
    return out;
}

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
                 " appearance TEXT NOT NULL DEFAULT '{}',"
                 " credits INTEGER NOT NULL DEFAULT 0,"
                 " faction_points INTEGER NOT NULL DEFAULT 0,"
                 " experience INTEGER NOT NULL DEFAULT 0,"
                 " active_loadout INTEGER NOT NULL DEFAULT 0"
                 ") STRICT");
            exec("CREATE TABLE items ("
                 " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 " owner_id INTEGER NOT NULL,"
                 " def_id TEXT NOT NULL,"
                 " quality REAL NOT NULL DEFAULT 0.5,"
                 " custom_name TEXT NOT NULL DEFAULT '',"
                 " color_json TEXT NOT NULL DEFAULT '{}'"
                 ") STRICT");
            exec("CREATE TABLE loadouts ("
                 " owner_id INTEGER NOT NULL,"
                 " slot_index INTEGER NOT NULL,"
                 " name TEXT NOT NULL,"
                 " primary_id INTEGER NOT NULL DEFAULT 0,"
                 " sidearm_id INTEGER NOT NULL DEFAULT 0,"
                 " secondary_id INTEGER NOT NULL DEFAULT 0,"
                 " gear_json TEXT NOT NULL DEFAULT '{}',"
                 " appearance_json TEXT NOT NULL DEFAULT '{}',"
                 " specializations TEXT NOT NULL DEFAULT '',"
                 " companion TEXT NOT NULL DEFAULT '',"
                 " PRIMARY KEY (owner_id, slot_index)"
                 ") STRICT");
            exec("CREATE TABLE IF NOT EXISTS quests ("
                 " owner_id INTEGER NOT NULL,"
                 " quest_id TEXT NOT NULL,"
                 " step INTEGER NOT NULL DEFAULT 0,"
                 " count INTEGER NOT NULL DEFAULT 0,"
                 " done INTEGER NOT NULL DEFAULT 0,"
                 " PRIMARY KEY (owner_id, quest_id)"
                 ") STRICT");
            exec("CREATE TABLE IF NOT EXISTS professions ("
                 " owner_id INTEGER NOT NULL,"
                 " profession_id TEXT NOT NULL,"
                 " xp INTEGER NOT NULL DEFAULT 0,"
                 " PRIMARY KEY (owner_id, profession_id)"
                 ") STRICT");
            exec(("PRAGMA user_version = " + std::to_string(schema_version)).c_str());
            exec("COMMIT");
        } else if (version == 1 || version == 2) {
            exec("BEGIN");
            if (version == 1) {
                exec("ALTER TABLE characters ADD COLUMN species TEXT NOT NULL DEFAULT 'human'");
                exec("ALTER TABLE characters ADD COLUMN gender TEXT NOT NULL DEFAULT 'male'");
                exec("ALTER TABLE characters ADD COLUMN location TEXT NOT NULL DEFAULT 'Mos Eisley'");
                exec("ALTER TABLE characters ADD COLUMN appearance TEXT NOT NULL DEFAULT '{}'");
            }
            exec("ALTER TABLE characters ADD COLUMN credits INTEGER NOT NULL DEFAULT 0");
            exec("ALTER TABLE characters ADD COLUMN faction_points INTEGER NOT NULL DEFAULT 0");
            exec("ALTER TABLE characters ADD COLUMN experience INTEGER NOT NULL DEFAULT 0");
            exec("ALTER TABLE characters ADD COLUMN active_loadout INTEGER NOT NULL DEFAULT 0");
            exec("CREATE TABLE IF NOT EXISTS items ("
                 " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 " owner_id INTEGER NOT NULL,"
                 " def_id TEXT NOT NULL,"
                 " quality REAL NOT NULL DEFAULT 0.5,"
                 " custom_name TEXT NOT NULL DEFAULT '',"
                 " color_json TEXT NOT NULL DEFAULT '{}'"
                 ") STRICT");
            exec("CREATE TABLE IF NOT EXISTS loadouts ("
                 " owner_id INTEGER NOT NULL,"
                 " slot_index INTEGER NOT NULL,"
                 " name TEXT NOT NULL,"
                 " primary_id INTEGER NOT NULL DEFAULT 0,"
                 " sidearm_id INTEGER NOT NULL DEFAULT 0,"
                 " secondary_id INTEGER NOT NULL DEFAULT 0,"
                 " gear_json TEXT NOT NULL DEFAULT '{}',"
                 " appearance_json TEXT NOT NULL DEFAULT '{}',"
                 " PRIMARY KEY (owner_id, slot_index)"
                 ") STRICT");
            exec("CREATE TABLE IF NOT EXISTS quests ("
                 " owner_id INTEGER NOT NULL,"
                 " quest_id TEXT NOT NULL,"
                 " step INTEGER NOT NULL DEFAULT 0,"
                 " count INTEGER NOT NULL DEFAULT 0,"
                 " done INTEGER NOT NULL DEFAULT 0,"
                 " PRIMARY KEY (owner_id, quest_id)"
                 ") STRICT");
            // Version 5: loadouts' Active Specialization and Companion Slots, and professions.
            exec("ALTER TABLE loadouts ADD COLUMN specializations TEXT NOT NULL DEFAULT ''");
            exec("ALTER TABLE loadouts ADD COLUMN companion TEXT NOT NULL DEFAULT ''");
            exec("CREATE TABLE IF NOT EXISTS professions ("
                 " owner_id INTEGER NOT NULL,"
                 " profession_id TEXT NOT NULL,"
                 " xp INTEGER NOT NULL DEFAULT 0,"
                 " PRIMARY KEY (owner_id, profession_id)"
                 ") STRICT");
            exec(("PRAGMA user_version = " + std::to_string(schema_version)).c_str());
            exec("COMMIT");
        } else if (version == 3) {
            exec("BEGIN");
            exec("CREATE TABLE IF NOT EXISTS quests ("
                 " owner_id INTEGER NOT NULL,"
                 " quest_id TEXT NOT NULL,"
                 " step INTEGER NOT NULL DEFAULT 0,"
                 " count INTEGER NOT NULL DEFAULT 0,"
                 " done INTEGER NOT NULL DEFAULT 0,"
                 " PRIMARY KEY (owner_id, quest_id)"
                 ") STRICT");
            // Version 5: loadouts' Active Specialization and Companion Slots, and professions.
            exec("ALTER TABLE loadouts ADD COLUMN specializations TEXT NOT NULL DEFAULT ''");
            exec("ALTER TABLE loadouts ADD COLUMN companion TEXT NOT NULL DEFAULT ''");
            exec("CREATE TABLE IF NOT EXISTS professions ("
                 " owner_id INTEGER NOT NULL,"
                 " profession_id TEXT NOT NULL,"
                 " xp INTEGER NOT NULL DEFAULT 0,"
                 " PRIMARY KEY (owner_id, profession_id)"
                 ") STRICT");
            exec(("PRAGMA user_version = " + std::to_string(schema_version)).c_str());
            exec("COMMIT");
        } else if (version == 4) {
            exec("BEGIN");
            // Version 5: loadouts' Active Specialization and Companion Slots, and professions.
            exec("ALTER TABLE loadouts ADD COLUMN specializations TEXT NOT NULL DEFAULT ''");
            exec("ALTER TABLE loadouts ADD COLUMN companion TEXT NOT NULL DEFAULT ''");
            exec("CREATE TABLE IF NOT EXISTS professions ("
                 " owner_id INTEGER NOT NULL,"
                 " profession_id TEXT NOT NULL,"
                 " xp INTEGER NOT NULL DEFAULT 0,"
                 " PRIMARY KEY (owner_id, profession_id)"
                 ") STRICT");
            exec(("PRAGMA user_version = " + std::to_string(schema_version)).c_str());
            exec("COMMIT");
        } else if (version != schema_version) {
            throw std::runtime_error("sqlite: " + file + " has schema version " + std::to_string(version) +
                                     ", this build reads version " + std::to_string(schema_version));
        }

        constexpr const char* cols =
            "id, name, x, y, z, yaw, species, gender, location, appearance, credits, faction_points, experience, active_loadout";
        find_ = prepare(db_, (std::string("SELECT ") + cols + " FROM characters WHERE id = ?").c_str());
        find_by_name_ = prepare(db_, (std::string("SELECT ") + cols + " FROM characters WHERE name = ?").c_str());
        list_all_ = prepare(db_, (std::string("SELECT ") + cols + " FROM characters ORDER BY id ASC").c_str());
        insert_ = prepare(db_, "INSERT INTO characters (name, x, y, z, yaw, species, gender, location, appearance, "
                               "credits, faction_points, experience, active_loadout) "
                               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        update_ = prepare(db_, "UPDATE characters SET x = ?, y = ?, z = ?, yaw = ?, location = ?, appearance = ? WHERE id = ?");
        delete_ = prepare(db_, "DELETE FROM characters WHERE id = ?");
        list_items_ = prepare(db_, "SELECT id, owner_id, def_id, quality, custom_name, color_json FROM items WHERE owner_id = ? ORDER BY id ASC");
        grant_item_ = prepare(db_, "INSERT INTO items (owner_id, def_id, quality, custom_name, color_json) VALUES (?, ?, ?, ?, ?)");
        remove_item_ = prepare(db_, "DELETE FROM items WHERE id = ?");
        remove_items_by_owner_ = prepare(db_, "DELETE FROM items WHERE owner_id = ?");
        list_loadouts_ = prepare(db_, "SELECT slot_index, name, primary_id, sidearm_id, secondary_id, gear_json, appearance_json, "
                                      "specializations, companion FROM loadouts WHERE owner_id = ? ORDER BY slot_index ASC");
        save_loadout_ = prepare(db_, "INSERT INTO loadouts (owner_id, slot_index, name, primary_id, sidearm_id, secondary_id, gear_json, "
                                     "appearance_json, specializations, companion) "
                                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                                     "ON CONFLICT(owner_id, slot_index) DO UPDATE SET "
                                     "name = excluded.name, primary_id = excluded.primary_id, sidearm_id = excluded.sidearm_id, "
                                     "secondary_id = excluded.secondary_id, gear_json = excluded.gear_json, appearance_json = excluded.appearance_json, "
                                     "specializations = excluded.specializations, companion = excluded.companion");
        remove_loadouts_by_owner_ = prepare(db_, "DELETE FROM loadouts WHERE owner_id = ?");
        update_economy_ = prepare(db_, "UPDATE characters SET credits = ?, faction_points = ?, experience = ?, active_loadout = ? WHERE id = ?");
        list_quests_ = prepare(db_, "SELECT quest_id, step, count, done FROM quests WHERE owner_id = ? ORDER BY rowid ASC");
        insert_quest_ = prepare(db_, "INSERT INTO quests (owner_id, quest_id, step, count, done) VALUES (?, ?, ?, ?, ?)");
        remove_quests_by_owner_ = prepare(db_, "DELETE FROM quests WHERE owner_id = ?");
        list_professions_ = prepare(db_, "SELECT profession_id, xp FROM professions WHERE owner_id = ? ORDER BY rowid ASC");
        insert_profession_ = prepare(db_, "INSERT INTO professions (owner_id, profession_id, xp) VALUES (?, ?, ?)");
        remove_professions_by_owner_ = prepare(db_, "DELETE FROM professions WHERE owner_id = ?");
    } catch (...) {
        close();
        throw;
    }
}

SqliteCharacterStore::~SqliteCharacterStore() {
    close();
}

void SqliteCharacterStore::close() {
    for (sqlite3_stmt* statement :
         {find_, find_by_name_, list_all_, insert_, update_, delete_, list_items_, grant_item_, remove_item_,
          remove_items_by_owner_, list_loadouts_, save_loadout_, remove_loadouts_by_owner_, update_economy_,
          list_quests_, insert_quest_, remove_quests_by_owner_, list_professions_, insert_profession_,
          remove_professions_by_owner_}) {
        sqlite3_finalize(statement);
    }
    find_ = find_by_name_ = list_all_ = insert_ = update_ = delete_ = nullptr;
    list_items_ = grant_item_ = remove_item_ = remove_items_by_owner_ = nullptr;
    list_loadouts_ = save_loadout_ = remove_loadouts_by_owner_ = update_economy_ = nullptr;
    list_quests_ = insert_quest_ = remove_quests_by_owner_ = nullptr;
    list_professions_ = insert_profession_ = remove_professions_by_owner_ = nullptr;
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
    record.credits = sqlite3_column_int64(statement, 10);
    record.faction_points = sqlite3_column_int64(statement, 11);
    record.experience = sqlite3_column_int64(statement, 12);
    record.active_loadout = sqlite3_column_int(statement, 13);
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
    sqlite3_bind_int64(insert_, 10, record.credits);
    sqlite3_bind_int64(insert_, 11, record.faction_points);
    sqlite3_bind_int64(insert_, 12, record.experience);
    sqlite3_bind_int(insert_, 13, record.active_loadout);
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
    {
        Reset r1{remove_items_by_owner_};
        sqlite3_bind_int64(remove_items_by_owner_, 1, sqlite3_int64(id));
        if (sqlite3_step(remove_items_by_owner_) != SQLITE_DONE) {
            fail(db_, "delete character items");
        }
    }
    {
        Reset r2{remove_loadouts_by_owner_};
        sqlite3_bind_int64(remove_loadouts_by_owner_, 1, sqlite3_int64(id));
        if (sqlite3_step(remove_loadouts_by_owner_) != SQLITE_DONE) {
            fail(db_, "delete character loadouts");
        }
    }
    {
        Reset r3{remove_quests_by_owner_};
        sqlite3_bind_int64(remove_quests_by_owner_, 1, sqlite3_int64(id));
        if (sqlite3_step(remove_quests_by_owner_) != SQLITE_DONE) {
            fail(db_, "delete character quests");
        }
    }
    {
        Reset r4{remove_professions_by_owner_};
        sqlite3_bind_int64(remove_professions_by_owner_, 1, sqlite3_int64(id));
        if (sqlite3_step(remove_professions_by_owner_) != SQLITE_DONE) {
            fail(db_, "delete character professions");
        }
    }
    Reset reset{delete_};
    sqlite3_bind_int64(delete_, 1, sqlite3_int64(id));
    if (sqlite3_step(delete_) != SQLITE_DONE) {
        fail(db_, "delete character");
    }
    return sqlite3_changes(db_) > 0;
}

std::vector<ItemInstance> SqliteCharacterStore::list_items(PersistentId owner_id) {
    Reset reset{list_items_};
    sqlite3_bind_int64(list_items_, 1, sqlite3_int64(owner_id));
    std::vector<ItemInstance> out;
    for (;;) {
        const int step = sqlite3_step(list_items_);
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) fail(db_, "list_items");
        auto col_str = [&](int col) {
            const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(list_items_, col));
            return txt ? std::string(txt) : std::string();
        };
        ItemInstance inst;
        inst.id = PersistentId(sqlite3_column_int64(list_items_, 0));
        inst.owner_id = PersistentId(sqlite3_column_int64(list_items_, 1));
        inst.def_id = col_str(2);
        inst.quality = sqlite3_column_double(list_items_, 3);
        inst.custom_name = col_str(4);
        inst.color_json = col_str(5);
        out.push_back(std::move(inst));
    }
    return out;
}

ItemInstance SqliteCharacterStore::grant_item(const ItemInstance& item) {
    ItemInstance copy = item;
    if (copy.color_json.empty()) copy.color_json = "{}";
    Reset reset{grant_item_};
    sqlite3_bind_int64(grant_item_, 1, sqlite3_int64(copy.owner_id));
    sqlite3_bind_text(grant_item_, 2, copy.def_id.c_str(), int(copy.def_id.size()), SQLITE_TRANSIENT);
    sqlite3_bind_double(grant_item_, 3, copy.quality);
    sqlite3_bind_text(grant_item_, 4, copy.custom_name.c_str(), int(copy.custom_name.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(grant_item_, 5, copy.color_json.c_str(), int(copy.color_json.size()), SQLITE_TRANSIENT);
    if (sqlite3_step(grant_item_) != SQLITE_DONE) {
        fail(db_, "grant_item " + copy.def_id);
    }
    copy.id = PersistentId(sqlite3_last_insert_rowid(db_));
    return copy;
}

bool SqliteCharacterStore::remove_item(PersistentId item_id) {
    Reset reset{remove_item_};
    sqlite3_bind_int64(remove_item_, 1, sqlite3_int64(item_id));
    if (sqlite3_step(remove_item_) != SQLITE_DONE) {
        fail(db_, "remove_item");
    }
    return sqlite3_changes(db_) > 0;
}

std::vector<CharacterLoadout> SqliteCharacterStore::list_loadouts(PersistentId owner_id) {
    Reset reset{list_loadouts_};
    sqlite3_bind_int64(list_loadouts_, 1, sqlite3_int64(owner_id));
    std::vector<CharacterLoadout> out;
    for (;;) {
        const int step = sqlite3_step(list_loadouts_);
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) fail(db_, "list_loadouts");
        auto col_str = [&](int col) {
            const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(list_loadouts_, col));
            return txt ? std::string(txt) : std::string();
        };
        CharacterLoadout lo;
        lo.slot_index = sqlite3_column_int(list_loadouts_, 0);
        lo.name = col_str(1);
        lo.primary_id = PersistentId(sqlite3_column_int64(list_loadouts_, 2));
        lo.sidearm_id = PersistentId(sqlite3_column_int64(list_loadouts_, 3));
        lo.secondary_id = PersistentId(sqlite3_column_int64(list_loadouts_, 4));
        lo.gear_ids = decode_id_map(col_str(5));
        lo.appearance_ids = decode_id_map(col_str(6));
        std::string item;
        for (const char c : col_str(7)) {
            if (c == ',') {
                if (!item.empty()) lo.specializations.push_back(std::move(item));
                item.clear();
            } else {
                item += c;
            }
        }
        if (!item.empty()) lo.specializations.push_back(std::move(item));
        lo.companion = col_str(8);
        out.push_back(std::move(lo));
    }
    return out;
}

void SqliteCharacterStore::save_loadout(PersistentId owner_id, const CharacterLoadout& loadout) {
    const std::string gear_json = encode_id_map(loadout.gear_ids);
    const std::string app_json = encode_id_map(loadout.appearance_ids);
    std::string specializations;
    for (const std::string& id : loadout.specializations) {
        if (!specializations.empty()) specializations += ',';
        specializations += id;
    }
    Reset reset{save_loadout_};
    sqlite3_bind_int64(save_loadout_, 1, sqlite3_int64(owner_id));
    sqlite3_bind_int(save_loadout_, 2, loadout.slot_index);
    sqlite3_bind_text(save_loadout_, 3, loadout.name.c_str(), int(loadout.name.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(save_loadout_, 4, sqlite3_int64(loadout.primary_id));
    sqlite3_bind_int64(save_loadout_, 5, sqlite3_int64(loadout.sidearm_id));
    sqlite3_bind_int64(save_loadout_, 6, sqlite3_int64(loadout.secondary_id));
    sqlite3_bind_text(save_loadout_, 7, gear_json.c_str(), int(gear_json.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(save_loadout_, 8, app_json.c_str(), int(app_json.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(save_loadout_, 9, specializations.c_str(), int(specializations.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(save_loadout_, 10, loadout.companion.c_str(), int(loadout.companion.size()), SQLITE_TRANSIENT);
    if (sqlite3_step(save_loadout_) != SQLITE_DONE) {
        fail(db_, "save_loadout");
    }
}

void SqliteCharacterStore::update_economy(PersistentId id, int64_t credits, int64_t faction_points, int64_t experience,
                                          int active_loadout) {
    Reset reset{update_economy_};
    sqlite3_bind_int64(update_economy_, 1, credits);
    sqlite3_bind_int64(update_economy_, 2, faction_points);
    sqlite3_bind_int64(update_economy_, 3, experience);
    sqlite3_bind_int(update_economy_, 4, active_loadout);
    sqlite3_bind_int64(update_economy_, 5, sqlite3_int64(id));
    if (sqlite3_step(update_economy_) != SQLITE_DONE) {
        fail(db_, "update_economy");
    }
    if (sqlite3_changes(db_) != 1) {
        throw std::runtime_error("sqlite: no character " + std::to_string(id));
    }
}

std::vector<QuestProgress> SqliteCharacterStore::list_quests(PersistentId owner_id) {
    Reset reset{list_quests_};
    sqlite3_bind_int64(list_quests_, 1, sqlite3_int64(owner_id));
    std::vector<QuestProgress> out;
    for (;;) {
        const int step = sqlite3_step(list_quests_);
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) fail(db_, "list_quests");
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(list_quests_, 0));
        out.push_back({id ? id : "", sqlite3_column_int(list_quests_, 1), sqlite3_column_int(list_quests_, 2),
                       sqlite3_column_int(list_quests_, 3) != 0});
    }
    return out;
}

void SqliteCharacterStore::save_quests(PersistentId owner_id, std::span<const QuestProgress> quests) {
    exec("BEGIN");
    try {
        {
            Reset reset{remove_quests_by_owner_};
            sqlite3_bind_int64(remove_quests_by_owner_, 1, sqlite3_int64(owner_id));
            if (sqlite3_step(remove_quests_by_owner_) != SQLITE_DONE) fail(db_, "save_quests");
        }
        for (const QuestProgress& q : quests) {
            Reset reset{insert_quest_};
            sqlite3_bind_int64(insert_quest_, 1, sqlite3_int64(owner_id));
            sqlite3_bind_text(insert_quest_, 2, q.id.c_str(), int(q.id.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int(insert_quest_, 3, q.step);
            sqlite3_bind_int(insert_quest_, 4, q.count);
            sqlite3_bind_int(insert_quest_, 5, q.done ? 1 : 0);
            if (sqlite3_step(insert_quest_) != SQLITE_DONE) fail(db_, "save_quests");
        }
        exec("COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

std::vector<ProfessionProgress> SqliteCharacterStore::list_professions(PersistentId owner_id) {
    Reset reset{list_professions_};
    sqlite3_bind_int64(list_professions_, 1, sqlite3_int64(owner_id));
    std::vector<ProfessionProgress> out;
    for (;;) {
        const int step = sqlite3_step(list_professions_);
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) fail(db_, "list_professions");
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(list_professions_, 0));
        out.push_back({id ? id : "", sqlite3_column_int64(list_professions_, 1)});
    }
    return out;
}

void SqliteCharacterStore::save_professions(PersistentId owner_id, std::span<const ProfessionProgress> professions) {
    exec("BEGIN");
    try {
        {
            Reset reset{remove_professions_by_owner_};
            sqlite3_bind_int64(remove_professions_by_owner_, 1, sqlite3_int64(owner_id));
            if (sqlite3_step(remove_professions_by_owner_) != SQLITE_DONE) fail(db_, "save_professions");
        }
        for (const ProfessionProgress& p : professions) {
            Reset reset{insert_profession_};
            sqlite3_bind_int64(insert_profession_, 1, sqlite3_int64(owner_id));
            sqlite3_bind_text(insert_profession_, 2, p.id.c_str(), int(p.id.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int64(insert_profession_, 3, p.xp);
            if (sqlite3_step(insert_profession_) != SQLITE_DONE) fail(db_, "save_professions");
        }
        exec("COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

} // namespace swm::core
