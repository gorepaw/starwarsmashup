// Character stores (in memory and SQLite), and characters keeping their place
// across a server restart.

#include <filesystem>
#include <memory>
#include <numbers>
#include <stdexcept>

#include <sqlite3.h>

#include "swm/core/client.h"
#include "swm/core/net/link.h"
#include "swm/core/persistence.h"
#include "swm/core/server.h"
#include "swm/core/sqlite_store.h"
#include "swm/core/world.h"
#include "test.h"

using namespace swm::core;
using swm::test::near;

namespace {

// A database file in the temp folder, deleted before and after.
struct TempDatabase {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "swm_persistence_test.sqlite";
    TempDatabase() { remove(); }
    ~TempDatabase() { remove(); }
    void remove() const {
        for (const char* suffix : {"", "-wal", "-shm"}) {
            std::filesystem::remove(path.string() + suffix);
        }
    }
};

template <typename F>
bool throws(F f) {
    try {
        f();
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
}

// The CharacterStore contract, for any implementation.
void test_store(CharacterStore& store) {
    const Vec3 far_away{-3500.123456789012, 5.25, -4800.987654321098};  // doubles survive exactly
    const CharacterRecord a = store.create("alpha", far_away, 1.5f);
    const CharacterRecord b = store.create("beta", {}, 0);
    CHECK(a.id != 0 && b.id > a.id);
    CHECK(store.find(a.id) == a && store.find_by_name("beta") == b);
    CHECK(!store.find(b.id + 100) && !store.find_by_name("gamma"));
    CHECK(throws([&] { store.create("alpha", {}, 0); }));  // names are unique

    CharacterRecord moved = a;
    moved.position = {1, 2, 3};
    moved.yaw = -2.25f;
    store.save(std::span(&moved, 1));
    CHECK(store.find(a.id) == moved);

    // Rich character fields, list_all, and remove.
    CharacterRecord wookiee;
    wookiee.name = "chewie";
    wookiee.position = {10, 0, -20};
    wookiee.yaw = 0.75f;
    wookiee.species = "wookiee";
    wookiee.gender = "male";
    wookiee.location = "Mos Eisley";
    wookiee.appearance = R"({"height":0.9,"hair":-1})";
    const CharacterRecord created_wke = store.create_character(wookiee);
    CHECK(created_wke.id > b.id);
    CHECK(store.find(created_wke.id) == created_wke);
    const auto all = store.list_all();
    CHECK(all.size() == 3);

    // Items, loadouts, and economy.
    const ItemInstance i1 = store.grant_item({0, created_wke.id, "wp_rifle_e11", 0.75, "", "{}"});
    const ItemInstance i2 = store.grant_item({0, created_wke.id, "psg_mark1", 0.5, "", "{}"});
    CHECK(i1.id != 0 && i2.id > i1.id);
    CHECK(store.list_items(created_wke.id).size() == 2);
    // An item's Mk tier is kept.
    const ItemInstance mk7 = store.grant_item({0, created_wke.id, "wp_pistol_dl44", 0.6, "", "{}", 1, 7});
    CHECK(mk7.mark == 7);
    const auto with_mk = store.list_items(created_wke.id);
    CHECK(with_mk.size() == 3 && with_mk[2].mark == 7 && with_mk[0].mark == 0);
    CHECK(store.remove_item(mk7.id));

    CharacterLoadout lo;
    lo.slot_index = 0;
    lo.name = "Heavy";
    lo.primary_id = i1.id;
    lo.gear_ids["deflector_shield"] = i2.id;
    lo.appearance_ids["chest"] = 99;
    store.save_loadout(created_wke.id, lo);
    const auto loadouts = store.list_loadouts(created_wke.id);
    CHECK(loadouts.size() == 1 && loadouts[0] == lo);

    store.update_economy(created_wke.id, 1500, 320, 450, 1);
    const auto updated_wke = store.find(created_wke.id);
    CHECK(updated_wke && updated_wke->credits == 1500 && updated_wke->faction_points == 320 &&
          updated_wke->experience == 450 && updated_wke->active_loadout == 1);

    CHECK(store.update_item_quantity(i1.id, 450));
    const auto items_after_qty = store.list_items(created_wke.id);
    CHECK(!items_after_qty.empty() && items_after_qty[0].quantity == 450);

    std::map<std::string, int64_t> rep{{"rebellion", 3500}, {"empire", -1750}, {"republic", 0}, {"separatists", 0}};
    store.save_reputation(created_wke.id, rep);
    CHECK(store.list_reputation(created_wke.id) == rep);

    CHECK(store.remove_item(i2.id));
    CHECK(store.list_items(created_wke.id).size() == 1);

    CHECK(store.remove(created_wke.id));
    CHECK(!store.remove(created_wke.id));
    CHECK(!store.find(created_wke.id));
    CHECK(store.list_items(created_wke.id).empty());
    CHECK(store.list_loadouts(created_wke.id).empty());

    // A batch with an unknown character saves nothing.
    CharacterRecord ghost = b;
    ghost.id = b.id + 100;
    CharacterRecord b_moved = b;
    b_moved.position = {9, 9, 9};
    const CharacterRecord batch[] = {b_moved, ghost};
    CHECK(throws([&] { store.save(batch); }));
    CHECK(store.find(b.id) == b);
}

void test_sqlite_file() {
    TempDatabase database;
    PersistentId first = 0;
    {
        SqliteCharacterStore store(database.path);
        test_store(store);
        first = store.find_by_name("alpha")->id;
    }
    // Reopened: everything is still there, and new ids keep counting up.
    {
        SqliteCharacterStore store(database.path);
        const auto alpha = store.find_by_name("alpha");
        CHECK(alpha && alpha->id == first && alpha->position == (Vec3{1, 2, 3}) && alpha->yaw == -2.25f);
        CHECK(store.create("gamma", {}, 0).id > store.find_by_name("beta")->id);
    }
    // A database from a newer build is refused rather than misread.
    {
        sqlite3* db = nullptr;
        sqlite3_open(database.path.string().c_str(), &db);
        sqlite3_exec(db, "PRAGMA user_version = 99", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        CHECK(throws([&] { SqliteCharacterStore store(database.path); }));
    }
    // A v8 database's cosmetic-only items become the equipment they showed,
    // and its cosmetic override's slots the worn slots (docs/ARMOR.md).
    {
        TempDatabase v8;
        PersistentId owner = 0;
        {
            SqliteCharacterStore fresh(v8.path);
            owner = fresh.create("dresser", {}, 0).id;
            const auto suit = fresh.grant_item({0, owner, "cosmetic_rep_clone_p1", 0.5, "", "{}"});
            const auto legs = fresh.grant_item({0, owner, "cosmetic_imp_stormtrooper_pants", 0.5, "", "{}"});
            const auto boots = fresh.grant_item({0, owner, "cloth_boots_leather", 0.5, "", "{}"});
            CharacterLoadout old;
            old.appearance_ids = {{"full_body", suit.id}, {"pants", legs.id}, {"boots_cosmetic", boots.id}};
            fresh.save_loadout(owner, old);
        }
        sqlite3* db = nullptr;
        CHECK(sqlite3_open(v8.path.string().c_str(), &db) == SQLITE_OK);
        sqlite3_exec(db, "PRAGMA user_version = 8", nullptr, nullptr, nullptr);
        sqlite3_close(db);

        SqliteCharacterStore migrated(v8.path);
        std::vector<std::string> defs;
        for (const ItemInstance& item : migrated.list_items(owner)) defs.push_back(item.def_id);
        CHECK((defs == std::vector<std::string>{"suit_rep_clone_p1", "armor_stormtrooper_leggings", "cloth_boots_leather"}));
        const auto los = migrated.list_loadouts(owner);
        CHECK(los.size() == 1 && los[0].appearance_ids.size() == 3 && los[0].appearance_ids.contains("full_body") &&
              los[0].appearance_ids.contains("leggings") && los[0].appearance_ids.contains("boots"));
    }
    // A v1 database migrates cleanly to v2, preserving existing characters.
    {
        TempDatabase v1;
        sqlite3* db = nullptr;
        CHECK(sqlite3_open(v1.path.string().c_str(), &db) == SQLITE_OK);
        CHECK(sqlite3_exec(db,
                           "CREATE TABLE characters ("
                           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                           " name TEXT NOT NULL UNIQUE,"
                           " x REAL NOT NULL, y REAL NOT NULL, z REAL NOT NULL,"
                           " yaw REAL NOT NULL) STRICT;"
                           "INSERT INTO characters (name, x, y, z, yaw) VALUES ('legacy', 1.5, 2.0, -3.5, 0.5);"
                           "PRAGMA user_version = 1;",
                           nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);

        SqliteCharacterStore migrated(v1.path);
        const auto legacy = migrated.find_by_name("legacy");
        CHECK(legacy && legacy->species == "human" && legacy->gender == "male" && legacy->location == "Mos Eisley");
    }
    // A v2 database migrates cleanly to v3, adding economy columns and item/loadout tables.
    {
        TempDatabase v2;
        sqlite3* db = nullptr;
        CHECK(sqlite3_open(v2.path.string().c_str(), &db) == SQLITE_OK);
        CHECK(sqlite3_exec(db,
                           "CREATE TABLE characters ("
                           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                           " name TEXT NOT NULL UNIQUE,"
                           " x REAL NOT NULL, y REAL NOT NULL, z REAL NOT NULL,"
                           " yaw REAL NOT NULL,"
                           " species TEXT NOT NULL DEFAULT 'human',"
                           " gender TEXT NOT NULL DEFAULT 'male',"
                           " location TEXT NOT NULL DEFAULT 'Mos Eisley',"
                           " appearance TEXT NOT NULL DEFAULT '{}') STRICT;"
                           "INSERT INTO characters (name, x, y, z, yaw, species) VALUES ('v2char', 0, 0, 0, 0, 'wookiee');"
                           "PRAGMA user_version = 2;",
                           nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);

        SqliteCharacterStore migrated(v2.path);
        const auto ch = migrated.find_by_name("v2char");
        CHECK(ch && ch->species == "wookiee" && ch->credits == 0 && ch->faction_points == 0);
        const auto granted = migrated.grant_item({0, ch->id, "psg_mark1", 0.5, "", "{}", 1, 3});
        CHECK(granted.id > 0 && migrated.list_items(ch->id).size() == 1);
        CHECK(migrated.list_items(ch->id)[0].mark == 3);
    }
}

std::vector<float> flat_heights() {
    return std::vector<float>(101 * 101, 2.0f);  // game x and z: -50..50
}

// One server "process" lifetime: its own store connection, link and server.
struct ServerProcess {
    std::unique_ptr<SqliteCharacterStore> store;
    std::unique_ptr<net::LoopbackLink> link;
    std::unique_ptr<GameServer> server;

    ServerProcess(const std::filesystem::path& db, net::LoopbackHub& hub, const World& world) {
        store = std::make_unique<SqliteCharacterStore>(db);
        link = std::make_unique<net::LoopbackLink>(hub, net::Endpoint{1, 1});
        ServerConfig config;
        config.save_interval = 1;
        server = std::make_unique<GameServer>(*link, world, config, store.get());
    }
};

// Runs a client named `name` for `seconds`, running forward (yaw 0, toward -Z).
struct Player {
    net::LoopbackLink link;
    GameClient client;
    Player(net::LoopbackHub& hub, uint16_t port, const World& world, const std::string& name)
        : link(hub, {2, port}), client(link, world, ClientConfig{.server = {1, 1}, .name = name}) {}
};

void run(double& now, double seconds, GameServer& server, std::initializer_list<Player*> players, bool forward) {
    for (const double end = now + seconds; now < end; now += 1.0 / 60) {
        for (Player* player : players) {
            player->client.set_input({.forward = forward ? 1.0 : 0.0});
            player->client.update(now);
        }
        server.update(now);
    }
}

void test_restart() {
    TempDatabase database;
    const Heightfield ground(101, 1.0, -50, -50, flat_heights());
    // A low platform (0.3 m up a step) from z = -10 to -30, open toward the
    // spawn: running forward ends up standing on it.
    BuildingCollision platform;
    platform.floors.push_back({{-5, 2.3, -10}, {5, 2.3, -10}, {5, 2.3, -30}});
    platform.floors.push_back({{-5, 2.3, -10}, {5, 2.3, -30}, {-5, 2.3, -30}});
    World world(ground);
    world.add_building(platform, {});
    net::LoopbackHub hub;
    double now = 0;

    // First run: a new character runs 2 s toward -Z and leaves.
    PersistentId id = 0;
    Vec3 left_at;
    {
        ServerProcess process(database.path, hub, world);
        Player trooper(hub, 1, world, "trooper");
        run(now, 0.5, *process.server, {&trooper}, false);
        CHECK(trooper.client.spawned() && trooper.client.character_id() != 0);
        id = trooper.client.character_id();
        const Vec3 spawn = trooper.client.predicted().position;
        run(now, 3.0, *process.server, {&trooper}, true);
        run(now, 0.5, *process.server, {&trooper}, false);
        left_at = trooper.client.predicted().position;
        CHECK(spawn.z - left_at.z > 12 && near(left_at.y, 2.3, 1e-5));  // up on the platform

        // The same name can't join twice.
        Player impostor(hub, 9, world, "trooper");
        run(now, 0.5, *process.server, {&trooper, &impostor}, false);
        CHECK(impostor.client.status() == GameClient::Status::refused && !impostor.client.refusal().empty());

        trooper.client.disconnect();
        run(now, 0.2, *process.server, {}, false);
        CHECK(process.server->client_count() == 0);
    }

    // Restart: the character comes back where it left, on the platform, with
    // the same id.
    {
        ServerProcess process(database.path, hub, world);
        Player trooper(hub, 2, world, "trooper");
        Player rookie(hub, 3, world, "rookie");
        run(now, 0.5, *process.server, {&trooper, &rookie}, false);
        CHECK(trooper.client.spawned() && trooper.client.character_id() == id);
        CHECK(trooper.client.predicted().position == left_at);
        CHECK(rookie.client.character_id() != 0 && rookie.client.character_id() != id);

        // Then the server dies without saving at shutdown: the periodic save
        // (every second here) has kept the character's place.
        run(now, 1.0, *process.server, {&trooper, &rookie}, true);
        run(now, 1.2, *process.server, {&trooper, &rookie}, false);
        left_at = trooper.client.predicted().position;
    }
    {
        SqliteCharacterStore store(database.path);
        const auto saved = store.find(id);
        CHECK(saved && near(saved->position.z, left_at.z) && near(saved->position.x, left_at.x));
    }
}

} // namespace

int main() {
    MemoryCharacterStore memory;
    test_store(memory);
    test_sqlite_file();
    test_restart();
    return swm::test::finish("persistence_test");
}
