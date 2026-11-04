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
}

std::vector<float> flat_heights() {
    return std::vector<float>(101 * 101, 2.0f);  // game x and z: -50..50
}

// One server "process" lifetime: its own store connection, link and server.
struct ServerProcess {
    std::unique_ptr<SqliteCharacterStore> store;
    std::unique_ptr<net::LoopbackLink> link;
    std::unique_ptr<GameServer> server;

    ServerProcess(const std::filesystem::path& db, net::LoopbackHub& hub, const Heightfield& ground) {
        store = std::make_unique<SqliteCharacterStore>(db);
        link = std::make_unique<net::LoopbackLink>(hub, net::Endpoint{1, 1});
        ServerConfig config;
        config.save_interval = 1;
        server = std::make_unique<GameServer>(*link, ground, config, store.get());
    }
};

// Runs a client named `name` for `seconds`, running forward (yaw 0, toward -Z).
struct Player {
    net::LoopbackLink link;
    GameClient client;
    Player(net::LoopbackHub& hub, uint16_t port, const Heightfield& ground, const std::string& name)
        : link(hub, {2, port}), client(link, ground, ClientConfig{.server = {1, 1}, .name = name}) {}
};

void run(double& now, double seconds, GameServer& server, std::initializer_list<Player*> players, bool forward) {
    for (const double end = now + seconds; now < end; now += 1.0 / 60) {
        for (Player* player : players) {
            player->client.set_input(forward ? 1 : 0, 0, 0, false, false);
            player->client.update(now);
        }
        server.update(now);
    }
}

void test_restart() {
    TempDatabase database;
    const Heightfield ground(101, 1.0, -50, -50, flat_heights());
    net::LoopbackHub hub;
    double now = 0;

    // First run: a new character runs 2 s toward -Z and leaves.
    PersistentId id = 0;
    Vec3 left_at;
    {
        ServerProcess process(database.path, hub, ground);
        Player trooper(hub, 1, ground, "trooper");
        run(now, 0.5, *process.server, {&trooper}, false);
        CHECK(trooper.client.spawned() && trooper.client.character_id() != 0);
        id = trooper.client.character_id();
        const Vec3 spawn = trooper.client.predicted().position;
        run(now, 2.0, *process.server, {&trooper}, true);
        run(now, 0.5, *process.server, {&trooper}, false);
        left_at = trooper.client.predicted().position;
        CHECK(spawn.z - left_at.z > 9);

        // The same name can't join twice.
        Player impostor(hub, 9, ground, "trooper");
        run(now, 0.5, *process.server, {&trooper, &impostor}, false);
        CHECK(impostor.client.status() == GameClient::Status::refused && !impostor.client.refusal().empty());

        trooper.client.disconnect();
        run(now, 0.2, *process.server, {}, false);
        CHECK(process.server->client_count() == 0);
    }

    // Restart: the character comes back where it left, with the same id.
    {
        ServerProcess process(database.path, hub, ground);
        Player trooper(hub, 2, ground, "trooper");
        Player rookie(hub, 3, ground, "rookie");
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
