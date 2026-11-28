#include "swbfo/core/sqlite_store.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

namespace swbfo::core {

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
                 " quality REAL NOT NULL DEFAULT 0.5,"  // unused since grades (schema 10)
                 " quantity INTEGER NOT NULL DEFAULT 1,"
                 " custom_name TEXT NOT NULL DEFAULT '',"
                 " color_json TEXT NOT NULL DEFAULT '{}',"
                 " mark INTEGER NOT NULL DEFAULT 0,"
                 " grade INTEGER NOT NULL DEFAULT 1"
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
                 " specializations TEXT NOT NULL DEFAULT '',"  // unused since docs/PERKS.md (no slots)
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
            exec("CREATE TABLE IF NOT EXISTS material_vault ("
                 " owner_id INTEGER NOT NULL,"
                 " resource_id TEXT NOT NULL,"
                 " amount INTEGER NOT NULL DEFAULT 0,"
                 " PRIMARY KEY (owner_id, resource_id)"
                 ") STRICT");
            exec("CREATE TABLE IF NOT EXISTS recipe_presets ("
                 " owner_id INTEGER NOT NULL,"
                 " name TEXT NOT NULL,"
                 " schematic_id TEXT NOT NULL,"
                 " resource_ids TEXT NOT NULL DEFAULT '',"
                 " enhancement_defs TEXT NOT NULL DEFAULT '',"
                 " exp_quality_pct INTEGER NOT NULL DEFAULT 50,"
                 " PRIMARY KEY (owner_id, name)"
                 ") STRICT");
            exec("CREATE TABLE IF NOT EXISTS faction_reputation ("
                 " owner_id INTEGER NOT NULL,"
                 " faction_id TEXT NOT NULL,"
                 " reputation INTEGER NOT NULL DEFAULT 0,"
                 " PRIMARY KEY (owner_id, faction_id)"
                 ") STRICT");
            exec(("PRAGMA user_version = " + std::to_string(schema_version)).c_str());
            exec("COMMIT");
        } else if (version >= 1 && version <= 9) {
            exec("BEGIN");
            if (version == 1) {
                exec("ALTER TABLE characters ADD COLUMN species TEXT NOT NULL DEFAULT 'human'");
                exec("ALTER TABLE characters ADD COLUMN gender TEXT NOT NULL DEFAULT 'male'");
                exec("ALTER TABLE characters ADD COLUMN location TEXT NOT NULL DEFAULT 'Mos Eisley'");
                exec("ALTER TABLE characters ADD COLUMN appearance TEXT NOT NULL DEFAULT '{}'");
            }
            if (version <= 2) {
                exec("ALTER TABLE characters ADD COLUMN credits INTEGER NOT NULL DEFAULT 0");
                exec("ALTER TABLE characters ADD COLUMN faction_points INTEGER NOT NULL DEFAULT 0");
                exec("ALTER TABLE characters ADD COLUMN experience INTEGER NOT NULL DEFAULT 0");
                exec("ALTER TABLE characters ADD COLUMN active_loadout INTEGER NOT NULL DEFAULT 0");
                exec("CREATE TABLE IF NOT EXISTS items ("
                     " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     " owner_id INTEGER NOT NULL,"
                     " def_id TEXT NOT NULL,"
                     " quality REAL NOT NULL DEFAULT 0.5,"
                     " quantity INTEGER NOT NULL DEFAULT 1,"
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
            }
            if (version <= 3) {
                exec("CREATE TABLE IF NOT EXISTS quests ("
                     " owner_id INTEGER NOT NULL,"
                     " quest_id TEXT NOT NULL,"
                     " step INTEGER NOT NULL DEFAULT 0,"
                     " count INTEGER NOT NULL DEFAULT 0,"
                     " done INTEGER NOT NULL DEFAULT 0,"
                     " PRIMARY KEY (owner_id, quest_id)"
                     ") STRICT");
            }
            if (version <= 4) {
                exec("ALTER TABLE loadouts ADD COLUMN specializations TEXT NOT NULL DEFAULT ''");
                exec("ALTER TABLE loadouts ADD COLUMN companion TEXT NOT NULL DEFAULT ''");
                exec("CREATE TABLE IF NOT EXISTS professions ("
                     " owner_id INTEGER NOT NULL,"
                     " profession_id TEXT NOT NULL,"
                     " xp INTEGER NOT NULL DEFAULT 0,"
                     " PRIMARY KEY (owner_id, profession_id)"
                     ") STRICT");
            }
            if (version <= 5) {
                exec("CREATE TABLE IF NOT EXISTS material_vault ("
                     " owner_id INTEGER NOT NULL,"
                     " resource_id TEXT NOT NULL,"
                     " amount INTEGER NOT NULL DEFAULT 0,"
                     " PRIMARY KEY (owner_id, resource_id)"
                     ") STRICT");
                exec("CREATE TABLE IF NOT EXISTS recipe_presets ("
                     " owner_id INTEGER NOT NULL,"
                     " name TEXT NOT NULL,"
                     " schematic_id TEXT NOT NULL,"
                     " resource_ids TEXT NOT NULL DEFAULT '',"
                     " enhancement_defs TEXT NOT NULL DEFAULT '',"
                     " exp_quality_pct INTEGER NOT NULL DEFAULT 50,"
                     " PRIMARY KEY (owner_id, name)"
                     ") STRICT");
            }
            if (version >= 3 && version <= 6) {
                exec("ALTER TABLE items ADD COLUMN quantity INTEGER NOT NULL DEFAULT 1");
            }
            if (version <= 7) {
                exec("ALTER TABLE items ADD COLUMN mark INTEGER NOT NULL DEFAULT 0");
            }
            if (version <= 8) {
                // No cosmetic-only items any more (docs/ARMOR.md): a look of
                // an armor piece is that piece, a full-body look a suit, and
                // the cosmetic override's slots are the worn slots.
                static constexpr std::array<std::pair<const char*, const char*>, 18> renamed{{
                    {"cosmetic_rebel_composite_helmet", "armor_composite_helmet"},
                    {"cosmetic_rebel_composite_chest", "armor_composite_chest"},
                    {"cosmetic_rebel_composite_pants", "armor_composite_leggings"},
                    {"cosmetic_rebel_composite_boots", "armor_composite_boots"},
                    {"cosmetic_imp_stormtrooper_helmet", "armor_stormtrooper_helmet"},
                    {"cosmetic_imp_stormtrooper_chest", "armor_stormtrooper_chest"},
                    {"cosmetic_imp_stormtrooper_pants", "armor_stormtrooper_leggings"},
                    {"cosmetic_imp_stormtrooper_boots", "armor_stormtrooper_boots"},
                    {"cosmetic_imp_stormtrooper_full", "suit_imp_stormtrooper"},
                    {"cosmetic_rep_clone_p1", "suit_rep_clone_p1"},
                    {"cosmetic_rep_clone_p2", "suit_rep_clone_p2"},
                    {"cosmetic_rep_heavy_trooper", "suit_rep_heavy_trooper"},
                    {"cosmetic_rep_jet_trooper", "suit_rep_jet_trooper"},
                    {"cosmetic_rep_arc_trooper", "suit_rep_arc_trooper"},
                    {"cosmetic_rep_commander", "suit_rep_commander"},
                    {"cosmetic_cis_bdroid", "suit_cis_bdroid"},
                    {"cosmetic_cis_sbdroid", "suit_cis_sbdroid"},
                    {"cosmetic_cis_magnaguard", "suit_cis_magnaguard"},
                }};
                for (const auto& [from, to] : renamed) {
                    exec((std::string("UPDATE items SET def_id = '") + to + "' WHERE def_id = '" + from + "'").c_str());
                }
                exec("UPDATE loadouts SET appearance_json = REPLACE(REPLACE(REPLACE(REPLACE(appearance_json,"
                     " '\"jacket\":', '\"chest\":'), '\"shirt\":', '\"chest\":'), '\"pants\":', '\"leggings\":'),"
                     " '\"boots_cosmetic\":', '\"boots\":')");
            }
            if (version <= 9) {
                // Grades replace quality (items.h): the grade whose stats are
                // nearest (grade_from_quality).
                sqlite3_stmt* has_grade = prepare(db_, "SELECT 1 FROM pragma_table_info('items') WHERE name = 'grade'");
                const bool had = sqlite3_step(has_grade) == SQLITE_ROW;
                sqlite3_finalize(has_grade);
                if (!had) {
                    exec("ALTER TABLE items ADD COLUMN grade INTEGER NOT NULL DEFAULT 1");
                    exec("UPDATE items SET grade = CASE WHEN quality < 0.25 THEN 0 WHEN quality >= 0.75 THEN 2 "
                         "ELSE 1 END");
                }
            }
            exec("CREATE TABLE IF NOT EXISTS faction_reputation ("
                 " owner_id INTEGER NOT NULL,"
                 " faction_id TEXT NOT NULL,"
                 " reputation INTEGER NOT NULL DEFAULT 0,"
                 " PRIMARY KEY (owner_id, faction_id)"
                 ") STRICT");
            exec(("PRAGMA user_version = " + std::to_string(schema_version)).c_str());
            exec("COMMIT");
        } else if (version != schema_version) {
            throw std::runtime_error("sqlite: " + file + " has schema version " + std::to_string(version) +
                                     ", this build reads version " + std::to_string(schema_version));
        }

        // Small per-character facts (CharacterStore::get_extra); a table of their own, outside
        // the versioned ones, so it exists in a database of any version.
        exec("CREATE TABLE IF NOT EXISTS character_extras ("
             " owner_id INTEGER NOT NULL,"
             " key TEXT NOT NULL,"
             " value TEXT NOT NULL,"
             " PRIMARY KEY (owner_id, key)"
             ") STRICT");
        // Stage 11: match telemetry (CharacterStore::record_match), likewise outside the versioned tables.
        exec("CREATE TABLE IF NOT EXISTS match_telemetry ("
             " id INTEGER PRIMARY KEY,"
             " ended_at INTEGER NOT NULL,"
             " battlefield TEXT NOT NULL,"
             " map TEXT NOT NULL,"
             " mode TEXT NOT NULL,"
             " era TEXT NOT NULL,"
             " space INTEGER NOT NULL,"
             " team1 TEXT NOT NULL,"
             " team2 TEXT NOT NULL,"
             " winner INTEGER NOT NULL,"
             " tickets1 REAL NOT NULL,"
             " tickets2 REAL NOT NULL,"
             " score1 REAL NOT NULL,"
             " score2 REAL NOT NULL,"
             " duration REAL NOT NULL,"
             " captures INTEGER NOT NULL,"
             " players INTEGER NOT NULL,"
             " bots INTEGER NOT NULL"
             ") STRICT");
        exec("CREATE TABLE IF NOT EXISTS match_participants ("
             " match_id INTEGER NOT NULL REFERENCES match_telemetry(id) ON DELETE CASCADE,"
             " name TEXT NOT NULL,"
             " team INTEGER NOT NULL,"
             " bot INTEGER NOT NULL,"
             " kills INTEGER NOT NULL,"
             " deaths INTEGER NOT NULL,"
             " captures INTEGER NOT NULL,"
             " primary_weapon TEXT NOT NULL,"
             " weapon_kills TEXT NOT NULL"
             ") STRICT");
        exec("CREATE INDEX IF NOT EXISTS match_participants_match ON match_participants(match_id)");

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
        list_items_ = prepare(db_, "SELECT id, owner_id, def_id, grade, quantity, custom_name, color_json, mark FROM items WHERE owner_id = ? ORDER BY id ASC");
        grant_item_ = prepare(db_, "INSERT INTO items (owner_id, def_id, grade, quantity, custom_name, color_json, mark) VALUES (?, ?, ?, ?, ?, ?, ?)");
        remove_item_ = prepare(db_, "DELETE FROM items WHERE id = ?");
        update_item_quantity_ = prepare(db_, "UPDATE items SET quantity = ? WHERE id = ?");
        remove_items_by_owner_ = prepare(db_, "DELETE FROM items WHERE owner_id = ?");
        list_loadouts_ = prepare(db_, "SELECT slot_index, name, primary_id, sidearm_id, secondary_id, gear_json, appearance_json, "
                                      "companion FROM loadouts WHERE owner_id = ? ORDER BY slot_index ASC");
        save_loadout_ = prepare(db_, "INSERT INTO loadouts (owner_id, slot_index, name, primary_id, sidearm_id, secondary_id, gear_json, "
                                     "appearance_json, companion) "
                                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
                                     "ON CONFLICT(owner_id, slot_index) DO UPDATE SET "
                                     "name = excluded.name, primary_id = excluded.primary_id, sidearm_id = excluded.sidearm_id, "
                                     "secondary_id = excluded.secondary_id, gear_json = excluded.gear_json, appearance_json = excluded.appearance_json, "
                                     "companion = excluded.companion");
        remove_loadouts_by_owner_ = prepare(db_, "DELETE FROM loadouts WHERE owner_id = ?");
        update_economy_ = prepare(db_, "UPDATE characters SET credits = ?, faction_points = ?, experience = ?, active_loadout = ? WHERE id = ?");
        list_quests_ = prepare(db_, "SELECT quest_id, step, count, done FROM quests WHERE owner_id = ? ORDER BY rowid ASC");
        insert_quest_ = prepare(db_, "INSERT INTO quests (owner_id, quest_id, step, count, done) VALUES (?, ?, ?, ?, ?)");
        remove_quests_by_owner_ = prepare(db_, "DELETE FROM quests WHERE owner_id = ?");
        list_professions_ = prepare(db_, "SELECT profession_id, xp FROM professions WHERE owner_id = ? ORDER BY rowid ASC");
        insert_profession_ = prepare(db_, "INSERT INTO professions (owner_id, profession_id, xp) VALUES (?, ?, ?)");
        remove_professions_by_owner_ = prepare(db_, "DELETE FROM professions WHERE owner_id = ?");
        list_materials_ = prepare(db_, "SELECT resource_id, amount FROM material_vault WHERE owner_id = ? ORDER BY rowid ASC");
        insert_material_ = prepare(db_, "INSERT INTO material_vault (owner_id, resource_id, amount) VALUES (?, ?, ?)");
        remove_materials_by_owner_ = prepare(db_, "DELETE FROM material_vault WHERE owner_id = ?");
        list_presets_ = prepare(db_, "SELECT name, schematic_id, resource_ids, enhancement_defs, exp_quality_pct FROM recipe_presets WHERE owner_id = ? ORDER BY rowid ASC");
        save_preset_ = prepare(db_, "INSERT INTO recipe_presets (owner_id, name, schematic_id, resource_ids, enhancement_defs, exp_quality_pct) "
                                    "VALUES (?, ?, ?, ?, ?, ?) "
                                    "ON CONFLICT(owner_id, name) DO UPDATE SET "
                                    "schematic_id = excluded.schematic_id, resource_ids = excluded.resource_ids, "
                                    "enhancement_defs = excluded.enhancement_defs, exp_quality_pct = excluded.exp_quality_pct");
        delete_preset_ = prepare(db_, "DELETE FROM recipe_presets WHERE owner_id = ? AND name = ?");
        remove_presets_by_owner_ = prepare(db_, "DELETE FROM recipe_presets WHERE owner_id = ?");
        list_reputation_ = prepare(db_, "SELECT faction_id, reputation FROM faction_reputation WHERE owner_id = ?");
        insert_reputation_ = prepare(db_, "INSERT INTO faction_reputation (owner_id, faction_id, reputation) VALUES (?, ?, ?)");
        remove_reputation_by_owner_ = prepare(db_, "DELETE FROM faction_reputation WHERE owner_id = ?");
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
          update_item_quantity_, remove_items_by_owner_, list_loadouts_, save_loadout_, remove_loadouts_by_owner_,
          update_economy_, list_quests_, insert_quest_, remove_quests_by_owner_, list_professions_, insert_profession_,
          remove_professions_by_owner_, list_materials_, insert_material_, remove_materials_by_owner_,
          list_presets_, save_preset_, delete_preset_, remove_presets_by_owner_, list_reputation_, insert_reputation_,
          remove_reputation_by_owner_}) {
        sqlite3_finalize(statement);
    }
    find_ = find_by_name_ = list_all_ = insert_ = update_ = delete_ = nullptr;
    list_items_ = grant_item_ = remove_item_ = update_item_quantity_ = remove_items_by_owner_ = nullptr;
    list_loadouts_ = save_loadout_ = remove_loadouts_by_owner_ = update_economy_ = nullptr;
    list_quests_ = insert_quest_ = remove_quests_by_owner_ = nullptr;
    list_professions_ = insert_profession_ = remove_professions_by_owner_ = nullptr;
    list_materials_ = insert_material_ = remove_materials_by_owner_ = nullptr;
    list_presets_ = save_preset_ = delete_preset_ = remove_presets_by_owner_ = nullptr;
    list_reputation_ = insert_reputation_ = remove_reputation_by_owner_ = nullptr;
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

std::string SqliteCharacterStore::get_extra(PersistentId owner_id, const std::string& key) {
    struct Finalize {
        sqlite3_stmt* statement;
        ~Finalize() { sqlite3_finalize(statement); }
    };
    sqlite3_stmt* statement = prepare(db_, "SELECT value FROM character_extras WHERE owner_id = ? AND key = ?");
    Finalize finalize{statement};
    sqlite3_bind_int64(statement, 1, sqlite3_int64(owner_id));
    sqlite3_bind_text(statement, 2, key.c_str(), int(key.size()), SQLITE_TRANSIENT);
    std::string value;
    const int step = sqlite3_step(statement);
    if (step == SQLITE_ROW) {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        value = text ? text : "";
    } else if (step != SQLITE_DONE) {
        fail(db_, "get_extra");
    }
    return value;
}

void SqliteCharacterStore::set_extra(PersistentId owner_id, const std::string& key, const std::string& value) {
    sqlite3_stmt* statement = prepare(db_, "INSERT INTO character_extras (owner_id, key, value) VALUES (?, ?, ?) "
                                           "ON CONFLICT(owner_id, key) DO UPDATE SET value = excluded.value");
    sqlite3_bind_int64(statement, 1, sqlite3_int64(owner_id));
    sqlite3_bind_text(statement, 2, key.c_str(), int(key.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, value.c_str(), int(value.size()), SQLITE_TRANSIENT);
    const int step = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (step != SQLITE_DONE) fail(db_, "set_extra");
}

bool SqliteCharacterStore::update_item_colors(PersistentId item_id, const std::string& color_json) {
    sqlite3_stmt* statement = prepare(db_, "UPDATE items SET color_json = ? WHERE id = ?");
    sqlite3_bind_text(statement, 1, color_json.c_str(), int(color_json.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 2, sqlite3_int64(item_id));
    const int step = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (step != SQLITE_DONE) fail(db_, "update_item_colors");
    return sqlite3_changes(db_) == 1;
}

bool SqliteCharacterStore::remove(PersistentId id) {
    exec((std::string("DELETE FROM character_extras WHERE owner_id = ") + std::to_string(id)).c_str());
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
    {
        Reset r5{remove_materials_by_owner_};
        sqlite3_bind_int64(remove_materials_by_owner_, 1, sqlite3_int64(id));
        if (sqlite3_step(remove_materials_by_owner_) != SQLITE_DONE) {
            fail(db_, "delete character materials");
        }
    }
    {
        Reset r6{remove_presets_by_owner_};
        sqlite3_bind_int64(remove_presets_by_owner_, 1, sqlite3_int64(id));
        if (sqlite3_step(remove_presets_by_owner_) != SQLITE_DONE) {
            fail(db_, "delete character presets");
        }
    }
    {
        Reset r7{remove_reputation_by_owner_};
        sqlite3_bind_int64(remove_reputation_by_owner_, 1, sqlite3_int64(id));
        if (sqlite3_step(remove_reputation_by_owner_) != SQLITE_DONE) {
            fail(db_, "delete character reputation");
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
        inst.grade = clamp_grade(sqlite3_column_int(list_items_, 3));
        inst.quantity = std::max(1, sqlite3_column_int(list_items_, 4));
        inst.custom_name = col_str(5);
        inst.color_json = col_str(6);
        inst.mark = clamp_mark(sqlite3_column_int(list_items_, 7));
        out.push_back(std::move(inst));
    }
    return out;
}

ItemInstance SqliteCharacterStore::grant_item(const ItemInstance& item) {
    ItemInstance copy = item;
    if (copy.color_json.empty()) copy.color_json = "{}";
    if (copy.quantity <= 0) copy.quantity = 1;
    Reset reset{grant_item_};
    sqlite3_bind_int64(grant_item_, 1, sqlite3_int64(copy.owner_id));
    sqlite3_bind_text(grant_item_, 2, copy.def_id.c_str(), int(copy.def_id.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(grant_item_, 3, int(copy.grade));
    sqlite3_bind_int(grant_item_, 4, copy.quantity);
    sqlite3_bind_text(grant_item_, 5, copy.custom_name.c_str(), int(copy.custom_name.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(grant_item_, 6, copy.color_json.c_str(), int(copy.color_json.size()), SQLITE_TRANSIENT);
    copy.mark = clamp_mark(copy.mark);
    sqlite3_bind_int(grant_item_, 7, copy.mark);
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

bool SqliteCharacterStore::update_item_quantity(PersistentId item_id, int quantity) {
    Reset reset{update_item_quantity_};
    sqlite3_bind_int(update_item_quantity_, 1, quantity);
    sqlite3_bind_int64(update_item_quantity_, 2, sqlite3_int64(item_id));
    if (sqlite3_step(update_item_quantity_) != SQLITE_DONE) {
        fail(db_, "update_item_quantity");
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
        lo.companion = col_str(7);
        out.push_back(std::move(lo));
    }
    return out;
}

void SqliteCharacterStore::save_loadout(PersistentId owner_id, const CharacterLoadout& loadout) {
    const std::string gear_json = encode_id_map(loadout.gear_ids);
    const std::string app_json = encode_id_map(loadout.appearance_ids);
    Reset reset{save_loadout_};
    sqlite3_bind_int64(save_loadout_, 1, sqlite3_int64(owner_id));
    sqlite3_bind_int(save_loadout_, 2, loadout.slot_index);
    sqlite3_bind_text(save_loadout_, 3, loadout.name.c_str(), int(loadout.name.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(save_loadout_, 4, sqlite3_int64(loadout.primary_id));
    sqlite3_bind_int64(save_loadout_, 5, sqlite3_int64(loadout.sidearm_id));
    sqlite3_bind_int64(save_loadout_, 6, sqlite3_int64(loadout.secondary_id));
    sqlite3_bind_text(save_loadout_, 7, gear_json.c_str(), int(gear_json.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(save_loadout_, 8, app_json.c_str(), int(app_json.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(save_loadout_, 9, loadout.companion.c_str(), int(loadout.companion.size()), SQLITE_TRANSIENT);
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

namespace {

std::string join_csv(const std::vector<std::string>& list) {
    std::string out;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i > 0) out += ',';
        out += list[i];
    }
    return out;
}

std::vector<std::string> split_csv(const char* raw) {
    std::vector<std::string> out;
    if (!raw || !*raw) return out;
    std::string cur;
    for (const char* p = raw; *p; ++p) {
        if (*p == ',') {
            out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur += *p;
        }
    }
    out.push_back(std::move(cur));
    return out;
}

} // namespace

std::vector<MaterialStack> SqliteCharacterStore::list_materials(PersistentId owner_id) {
    Reset reset{list_materials_};
    sqlite3_bind_int64(list_materials_, 1, sqlite3_int64(owner_id));
    std::vector<MaterialStack> out;
    for (;;) {
        const int step = sqlite3_step(list_materials_);
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) fail(db_, "list_materials");
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(list_materials_, 0));
        out.push_back({id ? id : "", sqlite3_column_int64(list_materials_, 1)});
    }
    return out;
}

void SqliteCharacterStore::save_materials(PersistentId owner_id, std::span<const MaterialStack> materials) {
    exec("BEGIN");
    try {
        {
            Reset reset{remove_materials_by_owner_};
            sqlite3_bind_int64(remove_materials_by_owner_, 1, sqlite3_int64(owner_id));
            if (sqlite3_step(remove_materials_by_owner_) != SQLITE_DONE) fail(db_, "save_materials");
        }
        for (const MaterialStack& m : materials) {
            if (m.amount <= 0) continue;
            Reset reset{insert_material_};
            sqlite3_bind_int64(insert_material_, 1, sqlite3_int64(owner_id));
            sqlite3_bind_text(insert_material_, 2, m.resource_id.c_str(), int(m.resource_id.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int64(insert_material_, 3, m.amount);
            if (sqlite3_step(insert_material_) != SQLITE_DONE) fail(db_, "save_materials");
        }
        exec("COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

std::vector<RecipePreset> SqliteCharacterStore::list_presets(PersistentId owner_id) {
    Reset reset{list_presets_};
    sqlite3_bind_int64(list_presets_, 1, sqlite3_int64(owner_id));
    std::vector<RecipePreset> out;
    for (;;) {
        const int step = sqlite3_step(list_presets_);
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) fail(db_, "list_presets");
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(list_presets_, 0));
        const auto* sch = reinterpret_cast<const char*>(sqlite3_column_text(list_presets_, 1));
        const auto* rids = reinterpret_cast<const char*>(sqlite3_column_text(list_presets_, 2));
        const auto* edefs = reinterpret_cast<const char*>(sqlite3_column_text(list_presets_, 3));
        RecipePreset p;
        p.name = name ? name : "";
        p.schematic_id = sch ? sch : "";
        p.resource_ids = split_csv(rids);
        p.enhancement_defs = split_csv(edefs);
        p.exp_quality_pct = uint8_t(std::clamp(sqlite3_column_int(list_presets_, 4), 0, 100));
        out.push_back(std::move(p));
    }
    return out;
}

void SqliteCharacterStore::save_preset(PersistentId owner_id, const RecipePreset& preset) {
    Reset reset{save_preset_};
    const std::string rids = join_csv(preset.resource_ids);
    const std::string edefs = join_csv(preset.enhancement_defs);
    sqlite3_bind_int64(save_preset_, 1, sqlite3_int64(owner_id));
    sqlite3_bind_text(save_preset_, 2, preset.name.c_str(), int(preset.name.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(save_preset_, 3, preset.schematic_id.c_str(), int(preset.schematic_id.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(save_preset_, 4, rids.c_str(), int(rids.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(save_preset_, 5, edefs.c_str(), int(edefs.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(save_preset_, 6, int(preset.exp_quality_pct));
    if (sqlite3_step(save_preset_) != SQLITE_DONE) fail(db_, "save_preset");
}

bool SqliteCharacterStore::delete_preset(PersistentId owner_id, const std::string& name) {
    Reset reset{delete_preset_};
    sqlite3_bind_int64(delete_preset_, 1, sqlite3_int64(owner_id));
    sqlite3_bind_text(delete_preset_, 2, name.c_str(), int(name.size()), SQLITE_TRANSIENT);
    if (sqlite3_step(delete_preset_) != SQLITE_DONE) fail(db_, "delete_preset");
    return sqlite3_changes(db_) > 0;
}

std::map<std::string, int64_t> SqliteCharacterStore::list_reputation(PersistentId owner_id) {
    Reset reset{list_reputation_};
    sqlite3_bind_int64(list_reputation_, 1, sqlite3_int64(owner_id));
    std::map<std::string, int64_t> out;
    for (;;) {
        const int step = sqlite3_step(list_reputation_);
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) fail(db_, "list_reputation");
        const auto* faction = reinterpret_cast<const char*>(sqlite3_column_text(list_reputation_, 0));
        if (faction && *faction) {
            out[faction] = sqlite3_column_int64(list_reputation_, 1);
        }
    }
    return out;
}

void SqliteCharacterStore::save_reputation(PersistentId owner_id, const std::map<std::string, int64_t>& reputation) {
    exec("BEGIN");
    try {
        {
            Reset reset{remove_reputation_by_owner_};
            sqlite3_bind_int64(remove_reputation_by_owner_, 1, sqlite3_int64(owner_id));
            if (sqlite3_step(remove_reputation_by_owner_) != SQLITE_DONE) fail(db_, "save_reputation");
        }
        for (const auto& [faction, rep] : reputation) {
            Reset reset{insert_reputation_};
            sqlite3_bind_int64(insert_reputation_, 1, sqlite3_int64(owner_id));
            sqlite3_bind_text(insert_reputation_, 2, faction.c_str(), int(faction.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int64(insert_reputation_, 3, rep);
            if (sqlite3_step(insert_reputation_) != SQLITE_DONE) fail(db_, "save_reputation");
        }
        exec("COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

int64_t SqliteCharacterStore::record_match(const MatchTelemetry& match) {
    struct Finalize {
        sqlite3_stmt* statement;
        ~Finalize() { sqlite3_finalize(statement); }
    };
    const auto text = [](sqlite3_stmt* st, int index, const std::string& value) {
        sqlite3_bind_text(st, index, value.c_str(), int(value.size()), SQLITE_TRANSIENT);
    };
    exec("BEGIN");
    try {
        int64_t id = 0;
        {
            sqlite3_stmt* st = prepare(
                db_, "INSERT INTO match_telemetry (ended_at, battlefield, map, mode, era, space, team1, team2, winner, "
                     "tickets1, tickets2, score1, score2, duration, captures, players, bots) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
            Finalize finalize{st};
            const int64_t ended =
                match.ended_at != 0
                    ? match.ended_at
                    : int64_t(std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count());
            sqlite3_bind_int64(st, 1, ended);
            text(st, 2, match.battlefield);
            text(st, 3, match.map);
            text(st, 4, match.mode);
            text(st, 5, match.era);
            sqlite3_bind_int(st, 6, match.space ? 1 : 0);
            text(st, 7, match.teams[0]);
            text(st, 8, match.teams[1]);
            sqlite3_bind_int(st, 9, match.winner);
            sqlite3_bind_double(st, 10, match.tickets[0]);
            sqlite3_bind_double(st, 11, match.tickets[1]);
            sqlite3_bind_double(st, 12, match.scores[0]);
            sqlite3_bind_double(st, 13, match.scores[1]);
            sqlite3_bind_double(st, 14, match.duration);
            sqlite3_bind_int(st, 15, match.captures);
            sqlite3_bind_int(st, 16, match.player_count());
            sqlite3_bind_int(st, 17, match.bot_count());
            if (sqlite3_step(st) != SQLITE_DONE) fail(db_, "record_match");
            id = sqlite3_last_insert_rowid(db_);
        }
        sqlite3_stmt* st = prepare(db_, "INSERT INTO match_participants (match_id, name, team, bot, kills, deaths, "
                                        "captures, primary_weapon, weapon_kills) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        Finalize finalize{st};
        for (const MatchParticipant& p : match.participants) {
            sqlite3_reset(st);
            sqlite3_bind_int64(st, 1, id);
            text(st, 2, p.name);
            sqlite3_bind_int(st, 3, p.team);
            sqlite3_bind_int(st, 4, p.bot ? 1 : 0);
            sqlite3_bind_int(st, 5, p.kills);
            sqlite3_bind_int(st, 6, p.deaths);
            sqlite3_bind_int(st, 7, p.captures);
            text(st, 8, p.primary_weapon);
            text(st, 9, encode_weapon_kills(p.weapon_kills));
            if (sqlite3_step(st) != SQLITE_DONE) fail(db_, "record_match participant");
        }
        exec("COMMIT");
        return id;
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}

std::vector<MatchTelemetry> SqliteCharacterStore::list_matches(size_t limit) {
    struct Finalize {
        sqlite3_stmt* statement;
        ~Finalize() { sqlite3_finalize(statement); }
    };
    const auto column = [](sqlite3_stmt* st, int index) {
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(st, index));
        return std::string(value ? value : "");
    };
    std::vector<MatchTelemetry> out;
    sqlite3_stmt* st = prepare(db_, "SELECT id, ended_at, battlefield, map, mode, era, space, team1, team2, winner, "
                                    "tickets1, tickets2, score1, score2, duration, captures FROM match_telemetry "
                                    "ORDER BY id DESC LIMIT ?");
    Finalize finalize{st};
    sqlite3_bind_int64(st, 1, sqlite3_int64(std::min<size_t>(limit, size_t(1) << 30)));
    for (;;) {
        const int step = sqlite3_step(st);
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) fail(db_, "list_matches");
        MatchTelemetry m;
        m.id = sqlite3_column_int64(st, 0);
        m.ended_at = sqlite3_column_int64(st, 1);
        m.battlefield = column(st, 2);
        m.map = column(st, 3);
        m.mode = column(st, 4);
        m.era = column(st, 5);
        m.space = sqlite3_column_int(st, 6) != 0;
        m.teams = {column(st, 7), column(st, 8)};
        m.winner = sqlite3_column_int(st, 9);
        m.tickets = {sqlite3_column_double(st, 10), sqlite3_column_double(st, 11)};
        m.scores = {sqlite3_column_double(st, 12), sqlite3_column_double(st, 13)};
        m.duration = sqlite3_column_double(st, 14);
        m.captures = uint16_t(sqlite3_column_int(st, 15));
        out.push_back(std::move(m));
    }
    sqlite3_stmt* people = prepare(db_, "SELECT name, team, bot, kills, deaths, captures, primary_weapon, "
                                        "weapon_kills FROM match_participants WHERE match_id = ?");
    Finalize finalize_people{people};
    for (MatchTelemetry& m : out) {
        sqlite3_reset(people);
        sqlite3_bind_int64(people, 1, m.id);
        for (;;) {
            const int step = sqlite3_step(people);
            if (step == SQLITE_DONE) break;
            if (step != SQLITE_ROW) fail(db_, "list_matches participants");
            MatchParticipant p;
            p.name = column(people, 0);
            p.team = sqlite3_column_int(people, 1);
            p.bot = sqlite3_column_int(people, 2) != 0;
            p.kills = uint16_t(sqlite3_column_int(people, 3));
            p.deaths = uint16_t(sqlite3_column_int(people, 4));
            p.captures = uint16_t(sqlite3_column_int(people, 5));
            p.primary_weapon = column(people, 6);
            p.weapon_kills = decode_weapon_kills(column(people, 7));
            m.participants.push_back(std::move(p));
        }
    }
    return out;
}

} // namespace swbfo::core
