// Stage 11: Battlefront II's other game modes (Capture the Flag, 1-Flag CTF,
// Hunt, XL) on a battlefield's mode layers, battle ids, the wire format of
// flags and queue modes, and match telemetry with its stores.

#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <functional>
#include <thread>
#include <fstream>
#include <set>
#include <variant>

#include "swm/core/conquest.h"
#include "swm/core/hub.h"
#include "swm/core/net/protocol.h"
#include "swm/core/persistence.h"
#include "swm/core/sqlite_store.h"
#include "swm/core/telemetry.h"
#include "swm/core/client.h"
#include "swm/core/world.h"
#include "test.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#undef near
#undef far
#endif

using namespace swm::core;
using swm::test::near;

namespace {

constexpr double dt = 1.0 / 30;

// A battlefield with a conquest map and ctf, 1flag, hunt and xl layers.
std::filesystem::path write_field() {
    const auto dir = std::filesystem::temp_directory_path() / "swm_modes_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "items");
    std::filesystem::create_directories(dir / "loadouts");
    std::filesystem::create_directories(dir / "maps");
    std::ofstream(dir / "items" / "rifle.ini") << "[weapon]\nName = rifle\n";
    std::ofstream(dir / "loadouts" / "trooper.ini") << "[loadout]\nName = trooper\nPrimary = rifle\n";
    const char* region = "box 0 0 0 4 4 4 1 0 0 0 1 0 0 0 1";
    std::ofstream(dir / "maps" / "m_conquest.ini")
        << "[map]\nName = m\nBounds = -512 -512 512 512\n\n"
        << "[post]\nName = CP1\nPosition = -100 0\nOwner = 1\n\n"
        << "[post]\nName = CP2\nPosition = 0 0\n\n"
        << "[post]\nName = CP3\nPosition = 100 0\nOwner = 2\n";
    // Two-flag CTF: team 1's flag at x -100 (game x 100), team 2's at x 100;
    // each scores at its own home (Battlefront's TeamNFlagCapture).
    std::ofstream(dir / "maps" / "m_ctf.ini")
        << "[map]\nName = m\n"
        << "Flag = 1 -100 0 0\nFlag = 2 100 0 0\n"
        << "FlagCapture = 1 " << "box -100 0 0 4 4 4 1 0 0 0 1 0 0 0 1\n"
        << "FlagCapture = 2 " << "box 100 0 0 4 4 4 1 0 0 0 1 0 0 0 1\n\n"
        << "[post]\nName = Base1\nPosition = -110 0\nOwner = 1\nCapturable = 0\n\n"
        << "[post]\nName = Base2\nPosition = 110 0\nOwner = 2\n";
    std::ofstream(dir / "maps" / "m_1flag.ini")
        << "[map]\nName = m\nFlag = 0 0 0 0\n"
        << "FlagCapture = 1 box 100 0 0 4 4 4 1 0 0 0 1 0 0 0 1\n"
        << "FlagCapture = 2 box -100 0 0 4 4 4 1 0 0 0 1 0 0 0 1\n\n"
        << "[post]\nName = A\nPosition = -110 0\nOwner = 1\n\n"
        << "[post]\nName = B\nPosition = 110 0\nOwner = 2\n";
    std::ofstream(dir / "maps" / "m_hunt.ini")
        << "[map]\nName = m\n\n[post]\nName = H1\nPosition = -50 0\nOwner = 1\n\n[post]\nName = H2\nPosition = 50 0\nOwner = 2\n";
    std::ofstream(dir / "maps" / "m_xl.ini")
        << "[map]\nName = m\n\n[post]\nName = X1\nPosition = -50 0\nOwner = 1\n\n[post]\nName = X2\nPosition = 0 0\n\n"
        << "[post]\nName = X3\nPosition = 50 0\nOwner = 2\n";
    std::ofstream(dir / "field_cw.ini")
        << "[battlefield]\nName = Plains\nMap = maps/m_conquest.ini\nWarmup = 1\nCaptureLimit = 2\nHuntLimit = 3\n"
        << "TimeLimit = 60\n"
        << "Mode = ctf maps/m_ctf.ini\nMode = 1flag maps/m_1flag.ini\nMode = hunt maps/m_hunt.ini\nMode = xl maps/m_xl.ini\n\n"
        << "[team]\nName = Separatists\nLoadout = loadouts/trooper.ini\n\n"
        << "[team]\nName = Republic\nLoadout = loadouts/trooper.ini\n";
    (void)region;
    return dir;
}

void test_loading_modes(const std::filesystem::path& dir) {
    const Battlefield conquest = load_battlefield(dir / "field_cw.ini");
    CHECK(conquest.mode == GameMode::conquest && conquest.posts.size() == 3 && conquest.flags.empty());
    CHECK(conquest.modes.size() == 5 && conquest.modes[0] == GameMode::conquest && conquest.modes[1] == GameMode::ctf);
    CHECK(conquest.teams[0].side == "cis" && conquest.teams[1].side == "rep");

    const Battlefield ctf = load_battlefield(dir / "field_cw.ini", GameMode::ctf);
    CHECK(ctf.mode == GameMode::ctf && ctf.posts.size() == 2 && ctf.flags.size() == 2 && ctf.flag_captures.size() == 2);
    CHECK(ctf.flags[0].owner == 0 && ctf.flags[0].home == Vec3(100, 0, 0) && ctf.flags[1].owner == 1);
    CHECK(!ctf.posts[0].capturable && !ctf.posts[1].capturable);  // spawn points only
    CHECK(ctf.capture_limit == 2 && ctf.time_limit == 60);

    const Battlefield xl = load_battlefield(dir / "field_cw.ini", GameMode::xl);
    CHECK(xl.posts.size() == 3 && !xl.posts[1].capturable && xl.team_size == 32 && xl.posts[0].name == "X1");

    // Ids: the file, and @mode for another mode's.
    CHECK(battlefield_id("field_cw", GameMode::conquest) == "field_cw");
    CHECK(battlefield_id("field_cw", GameMode::one_flag) == "field_cw@1flag");
    const Battlefield one = load_battlefield_id(dir, "field_cw@1flag");
    CHECK(one.mode == GameMode::one_flag && one.flags.size() == 1 && one.flags[0].owner == -1);
    bool threw = false;
    try {
        load_battlefield_id(dir, "../field_cw");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        load_battlefield(dir / "field_cw.ini", GameMode::space_assault);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    for (size_t i = 0; i < game_mode_count; ++i) {
        CHECK(parse_game_mode(game_mode_id(GameMode(i))) == GameMode(i));
    }
}

// Runs warmup out: the match goes live.
void go_live(Conquest& match) {
    for (int i = 0; i < 60 && match.state().phase != Phase::live; ++i) {
        match.step(dt, {}, 1);
    }
}

void test_ctf_rules(const std::filesystem::path& dir) {
    Conquest match(load_battlefield(dir / "field_cw.ini", GameMode::ctf));
    go_live(match);
    CHECK(match.state().phase == Phase::live && match.state().mode == GameMode::ctf);
    CHECK(match.state().flags.size() == 2 && match.state().flags[0].home);
    CHECK(match.state().score_limit == 2 && near(match.state().time_left, 60, 0.1));

    // A soldier of team 1 (index 1) walks onto team 0's flag at game x 100: takes it.
    std::vector<Conquest::Unit> units{{1, {100, 0, 0}, 7}};
    auto events = match.step(dt, units, 1);
    CHECK(match.state().flags[0].carrier == 7 && !match.state().flags[0].home);
    CHECK(!events.empty() && events[0].kind == MatchEvent::flag_taken && events[0].entity == 7);
    // Its own side can't take its flag; it's carried along.
    units = {{1, {50, 0, 0}, 7}, {0, {100, 0, 0}, 3}};
    match.step(dt, units, 2);
    CHECK(match.state().flags[0].position == Vec3(50, 0, 0) && match.state().flags[1].carrier == 0);
    // The carrier dies (not among the living): the flag lies where it fell.
    events = match.step(dt, std::vector<Conquest::Unit>{}, 2);
    CHECK(match.state().flags[0].carrier == 0 && !match.state().flags[0].home);
    CHECK(events.size() == 1 && events[0].kind == MatchEvent::flag_dropped);
    // Its owner touches it: it goes home.
    units = {{0, {50, 0, 0}, 3}};
    events = match.step(dt, units, 2);
    CHECK(match.state().flags[0].home && match.state().flags[0].position == Vec3(100, 0, 0));
    CHECK(events.size() == 1 && events[0].kind == MatchEvent::flag_returned);

    // Taken again and carried home (team 1's capture region at game x -100): a capture.
    match.step(dt, std::vector<Conquest::Unit>{{1, {100, 0, 0}, 7}}, 1);
    events = match.step(dt, std::vector<Conquest::Unit>{{1, {-100, 0, 0}, 7}}, 1);
    CHECK(match.state().scores[1] == 1 && match.state().flags[0].home);
    CHECK(events.size() == 1 && events[0].kind == MatchEvent::flag_captured && events[0].team == 1);
    // Kills cost nothing in CTF.
    match.killed(0);
    CHECK(match.state().tickets[0] == match.battlefield().tickets);
    // A second capture wins (the capture limit is 2).
    match.step(dt, std::vector<Conquest::Unit>{{1, {100, 0, 0}, 7}}, 1);
    events = match.step(dt, std::vector<Conquest::Unit>{{1, {-100, 0, 0}, 7}}, 1);
    match.step(dt, {}, 1);
    CHECK(match.state().phase == Phase::ended && match.state().winner == 1);

    // A soldier in a vehicle can't take a flag.
    Conquest riding(load_battlefield(dir / "field_cw.ini", GameMode::ctf));
    go_live(riding);
    Conquest::Unit driver{1, {100, 0, 0}, 9};
    driver.on_foot = false;
    riding.step(dt, std::vector<Conquest::Unit>{driver}, 1);
    CHECK(riding.state().flags[0].home && riding.state().flags[0].carrier == 0);
}

void test_one_flag_and_return(const std::filesystem::path& dir) {
    Conquest match(load_battlefield(dir / "field_cw.ini", GameMode::one_flag));
    go_live(match);
    match.step(dt, std::vector<Conquest::Unit>{{0, {0, 0, 0}, 4}}, 1);
    CHECK(match.state().flags[0].carrier == 4);
    match.step(dt, std::vector<Conquest::Unit>{}, 1);  // dropped
    // Lying long enough, it goes home on its own.
    for (int i = 0; i < int(flag_return_time / dt) + 2; ++i) {
        match.step(dt, std::vector<Conquest::Unit>{}, 1);
    }
    CHECK(match.state().flags[0].home);
    // Team 0 scores at its capture region (game x -100).
    match.step(dt, std::vector<Conquest::Unit>{{0, {0, 0, 0}, 4}}, 1);
    match.step(dt, std::vector<Conquest::Unit>{{0, {-100, 0, 0}, 4}}, 1);
    CHECK(match.state().scores[0] == 1);
}

void test_hunt_and_time_limit(const std::filesystem::path& dir) {
    Conquest match(load_battlefield(dir / "field_cw.ini", GameMode::hunt));
    go_live(match);
    match.killed(1);
    match.killed(1);
    CHECK(match.state().scores[0] == 2 && match.state().tickets[1] == match.battlefield().tickets);
    match.killed(1);
    match.step(dt, {}, 1);
    CHECK(match.state().phase == Phase::ended && match.state().winner == 0);

    // Time runs out: the higher score wins.
    Conquest timed(load_battlefield(dir / "field_cw.ini", GameMode::hunt));
    go_live(timed);
    timed.killed(0);
    for (int i = 0; i < int(61 / dt) && timed.state().phase == Phase::live; ++i) {
        timed.step(dt, {}, 1);
    }
    CHECK(timed.state().phase == Phase::ended && timed.state().winner == 1);
}

void test_wire() {
    MatchState state;
    state.mode = GameMode::ctf;
    state.flags = {{{1, 2, 3}, 42, false, 4.5f}, {{-1, 0, 2}, 0, true, 0}};
    net::Snapshot snapshot;
    snapshot.match = state;
    const auto decoded = std::get<net::Snapshot>(net::decode(net::encode(snapshot)));
    CHECK(decoded.match && decoded.match->mode == GameMode::ctf && decoded.match->flags == state.flags);
    // Queued for the Separatists (a hub's fourth side): the status still reads.
    net::Snapshot queued;
    queued.you.queue = net::PlayerStatus::queued;
    queued.you.team = 3;
    const auto back_queued = std::get<net::Snapshot>(net::decode(net::encode(queued)));
    CHECK(back_queued.you.queue == net::PlayerStatus::queued && back_queued.you.team == 3);

    const auto queue = std::get<net::QueueRequest>(net::decode(net::encode(net::QueueRequest{2, uint8_t(GameMode::hunt)})));
    CHECK(queue.team == 2 && queue.mode == uint8_t(GameMode::hunt));
    const auto any = std::get<net::QueueRequest>(net::decode(net::encode(net::QueueRequest{0, net::QueueRequest::any_mode})));
    CHECK(any.mode == net::QueueRequest::any_mode);

    net::Welcome welcome;
    welcome.recruiters = {{"Space Terminal", {1, 2, 3}, 4.5, net::Recruiter::space}};
    const auto back = std::get<net::Welcome>(net::decode(net::encode(welcome)));
    CHECK(back.recruiters.size() == 1 && back.recruiters[0].kind == net::Recruiter::space);
}

MatchTelemetry sample(int winner, const std::string& map, const std::string& mode) {
    MatchTelemetry t;
    t.battlefield = "kamino_cw";
    t.map = map;
    t.mode = mode;
    t.teams = {"Separatists", "Republic"};
    t.era = era_of(t.teams);
    t.winner = winner;
    t.tickets = {12, 0};
    t.duration = 300;
    t.captures = 4;
    t.participants = {{"Ann", 0, false, 5, 2, 1, "rifle", {{"rifle", uint16_t(5)}}},
                      {"CT-1234", 1, true, 3, 5, 0, "dc15", {{"dc15", uint16_t(2)}, {"vehicle", uint16_t(1)}}}};
    return t;
}

void test_telemetry() {
    CHECK(era_of({"Rebellion", "Empire"}) == "gcw" && era_of({"Republic", "Separatists"}) == "cw");
    const std::map<std::string, uint16_t> kills{{"rifle", 3}, {"vehicle", 1}};
    CHECK(decode_weapon_kills(encode_weapon_kills(kills)) == kills);

    const std::vector<MatchTelemetry> matches{sample(0, "Kamino", "conquest"), sample(0, "Kamino", "conquest"),
                                              sample(1, "Kamino", "ctf"), sample(-1, "Geonosis", "conquest")};
    const TelemetryReport report = summarize_telemetry(matches);
    CHECK(report.matches == 4 && report.player_slots == 4 && report.bot_slots == 4);
    const auto separatists = std::find_if(report.factions.begin(), report.factions.end(),
                                          [](const WinStats& w) { return w.key == "Separatists"; });
    CHECK(separatists != report.factions.end() && separatists->wins == 2 && separatists->losses == 1 &&
          separatists->draws == 1 && near(separatists->win_rate(), 0.5));
    CHECK(report.maps.size() == 3);  // Kamino conquest, Kamino ctf, Geonosis conquest
    CHECK(!report.weapons.empty() && report.weapons[0].weapon == "rifle" && report.weapons[0].kills == 20);
    CHECK(near(report.player_kd, 2.5) && near(report.bot_kd, 0.6));
    CHECK(format_telemetry_report(report).find("Separatists") != std::string::npos);

    // Stores: memory, and SQLite (and back after reopening).
    MemoryCharacterStore memory;
    memory.record_match(matches[0]);
    memory.record_match(matches[2]);
    const auto listed = memory.list_matches(10);
    CHECK(listed.size() == 2 && listed[0].mode == "ctf" && listed[1].id == 1);

    const auto db = std::filesystem::temp_directory_path() / "swm_modes_test.sqlite";
    std::filesystem::remove(db);
    {
        SqliteCharacterStore store(db);
        for (const MatchTelemetry& m : matches) {
            CHECK(store.record_match(m) > 0);
        }
    }
    {
        SqliteCharacterStore store(db);
        const auto back = store.list_matches(3);
        CHECK(back.size() == 3 && back[0].map == "Geonosis" && back[0].winner == -1);
        CHECK(back[1].mode == "ctf" && back[1].era == "cw" && back[1].teams[1] == "Republic");
        CHECK(back[2].participants.size() == 2 && back[2].participants[1].bot &&
              back[2].participants[1].weapon_kills.at("vehicle") == 1 && back[2].ended_at > 0);
        CHECK(near(back[2].tickets[0], 12) && back[2].captures == 4 && near(back[2].duration, 300));
    }
    std::filesystem::remove(db);
}

// The game's own roster (with your exported Battlefront II assets): every
// battlefield the Mos Eisley hub launches loads in every mode it offers,
// and both eras have ground and space battles.
void test_real_roster() {
    std::filesystem::path game;
    for (auto dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty() && dir != dir.parent_path();
         dir = dir.parent_path()) {
        if (std::filesystem::exists(dir / "game" / "hubs" / "mos_eisley.ini")) {
            game = dir / "game";
            break;
        }
    }
    if (game.empty() || !std::filesystem::exists(game / "imported_assets" / "maps" / "kam1" / "kam1_1flag.ini")) {
        std::printf("modes_test: no exported Battlefront II modes; skipping the real roster\n");
        return;
    }
    const HubConfig hub = load_hub(game / "hubs" / "mos_eisley.ini");
    // [era][space]: battlefields; and the modes offered per era.
    std::array<std::array<int, 2>, 2> counts{};
    std::array<std::set<GameMode>, 2> modes;
    for (const HubBattlefield& bf : hub.battlefields) {
        const int era = era_of({bf.field.teams[0].name, bf.field.teams[1].name}) == "cw" ? 1 : 0;
        ++counts[size_t(era)][bf.field.is_space() ? 1 : 0];
        for (const GameMode mode : bf.field.modes) {
            modes[size_t(era)].insert(mode);
            try {
                const Battlefield field = load_battlefield(bf.path, mode);
                CHECK(field.mode == mode && !field.posts.empty());
                if (mode == GameMode::ctf || mode == GameMode::one_flag || mode == GameMode::hunt ||
                    mode == GameMode::xl) {
                    // Each side has somewhere to spawn.
                    for (int team = 0; team < 2; ++team) {
                        const bool has = std::any_of(field.posts.begin(), field.posts.end(),
                                                     [&](const CommandPost& p) { return p.owner == team; });
                        if (!has) {
                            std::printf("FAIL %s %s: team %d has no post\n", bf.id.c_str(),
                                        std::string(game_mode_id(mode)).c_str(), team + 1);
                        }
                        CHECK(has);
                    }
                }
            } catch (const std::exception& error) {
                std::printf("FAIL %s %s: %s\n", bf.id.c_str(), std::string(game_mode_id(mode)).c_str(), error.what());
                CHECK(false);
            }
        }
    }
    std::printf("roster: GCW %d ground + %d space, CW %d ground + %d space\n", counts[0][0], counts[0][1],
                counts[1][0], counts[1][1]);
    CHECK(counts[0][0] >= 17 && counts[1][0] >= 16 && counts[0][1] >= 3 && counts[1][1] >= 3);
    for (const GameMode mode : {GameMode::conquest, GameMode::ctf, GameMode::one_flag, GameMode::hunt, GameMode::xl}) {
        CHECK(modes[0].contains(mode) && modes[1].contains(mode));
    }
}

// Opt-in (SWM_STAGE11_E2E=1, with your exported Battlefront II assets): the
// real Mos Eisley hub config (its terminals moved onto a flat test plaza).
// A player queues at the Ground Battle Terminal for the Clone Wars and
// Capture the Flag, follows the hub's transfer to a real Clone Wars map in
// that mode, waits there while the hub's bots fight it out, comes back, and
// the battle's telemetry is in the store; then at the Space Terminal, a GCW
// space battle launches. Slow: it plays whole matches.
void test_live_hub_e2e() {
    const char* wanted = std::getenv("SWM_STAGE11_E2E");
    if (wanted == nullptr || std::string(wanted) != "1") {
        return;
    }
    std::filesystem::path game;
    for (auto dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty() && dir != dir.parent_path();
         dir = dir.parent_path()) {
        if (std::filesystem::exists(dir / "game" / "hubs" / "mos_eisley.ini")) {
            game = dir / "game";
            break;
        }
    }
    if (game.empty()) {
        return;
    }
    HubConfig config = load_hub(game / "hubs" / "mos_eisley.ini");
    config.spawns.reset();  // no wildlife on the test plaza
    config.people.clear();
    config.crowd = {};
    config.recruiters = {{"Ground Battle Terminal", {0, 0, 0}, 8, net::Recruiter::ground},
                         {"Space Terminal", {60, 0, 0}, 8, net::Recruiter::space}};
    config.team_size = 4;
    config.queue_time = 1;
    config.return_delay = 1;
    for (HubBattlefield& bf : config.battlefields) {
        bf.field.warmup = 3;
        bf.field.results = 2;
        bf.field.tickets = 25;  // shorter matches
        bf.field.capture_limit = 2;
    }

    struct LoopNet : HubNetwork {
        net::LoopbackHub hub;
        uint16_t next_port = 100;
        std::unique_ptr<net::Link> open_server(uint16_t& port) override {
            port = next_port++;
            return std::make_unique<net::LoopbackLink>(hub, net::Endpoint{1, port});
        }
        std::unique_ptr<net::Link> open_client() override {
            return std::make_unique<net::LoopbackLink>(hub, net::Endpoint{2, next_port++});
        }
        net::Endpoint local(uint16_t port) const override { return {1, port}; }
    } network;
    Heightfield flat{401, 1.0, -200, -200, std::vector<float>(401 * 401, 0.0f)};
    World world{flat};
    MemoryCharacterStore store;
    uint16_t hub_port = 0;
    auto hub_link = network.open_server(hub_port);
    ServerConfig server_config;
    server_config.spawn = Vec3{0, 0, 0};
    GameServer hub_server(*hub_link, world, server_config, &store);
    Hub hub(hub_server, hub_port, world, config, network);
    std::vector<std::string> log;
    hub.log = [&](const std::string& line) {
        log.push_back(line);
        if (line.find("spawned") == std::string::npos && line.find("got into") == std::string::npos &&
            line.find(" killed ") == std::string::npos) {
            std::fprintf(stderr, "  hub: %s\n", line.c_str());
            std::fflush(stderr);
        }
    };
    const auto dir = game / "battlefields";
    auto client_link = network.open_client();
    std::string battle_id;
    ClientSession session(
        *client_link, world, ClientConfig{.server = {1, hub_port}, .name = "Tester"},
        [&](const std::string& id) -> std::optional<Battlefield> {
            battle_id = id;
            try {
                return load_battlefield_id(dir, id);
            } catch (const std::exception& error) {
                std::printf("FAIL loading %s: %s\n", id.c_str(), error.what());
                return std::nullopt;
            }
        },
        [](const Battlefield& field) -> std::shared_ptr<const World> {
            return field.terrain.empty() ? nullptr : load_world(field.terrain);
        });
    double now = 0;
    const auto step = [&](double seconds, const std::function<bool()>& done) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30) {
            session.update(now);
            hub_server.update(now);
            hub.update(now);
            if (done()) return true;
        }
        return false;
    };
    step(1, [] { return false; });
    CHECK(session.client().spawned());

    // At the Ground Battle Terminal (the hub file spawns players in Mos Eisley).
    config.spawn.reset();
    MoveState at_ground = *hub_server.state_of(session.client().entity_id());
    at_ground.position = {0, 0, 1};
    hub_server.set_state(session.client().entity_id(), at_ground);
    step(1, [] { return false; });
    // Clone Wars Capture the Flag, for the Republic.
    session.client().queue(2, uint8_t(GameMode::ctf));
    // Battle worlds load on another thread, in real time: wait in real time.
    const auto wait_real = [&](const std::function<bool()>& done) {
        for (int i = 0; i < 600; ++i) {
            if (step(0.2, done)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    };
    const bool went = wait_real([&] { return session.generation() == 1 && session.client().match().has_value(); });
    CHECK(went);
    std::printf("  e2e: transferred to %s\n", battle_id.c_str());
    CHECK(battle_id.find("@ctf") != std::string::npos);
    const bool back = step(2400, [&] { return session.generation() >= 2 && !session.client().match(); });
    CHECK(back);
    std::printf("  e2e: back after %.0f s; notice: %s\n", now, session.client().player_status().notice.c_str());
    const auto recorded = store.list_matches(10);
    CHECK(recorded.size() == 1);
    if (!recorded.empty()) {
        const MatchTelemetry& t = recorded.front();
        std::printf("  e2e telemetry: %s %s %s, %s %g - %g %s, winner %d, %.0f s, %d players, %d bots\n",
                    t.map.c_str(), t.era.c_str(), t.mode.c_str(), t.teams[0].c_str(), t.scores[0], t.scores[1],
                    t.teams[1].c_str(), t.winner, t.duration, t.player_count(), t.bot_count());
        CHECK(t.mode == "ctf" && t.era == "cw" && !t.space && t.player_count() == 1 && t.bot_count() >= 6);
    }

    if (!back) {
        std::printf("  e2e: not back: generation %d, status %d, error \"%s\"\n", session.generation(),
                    int(session.client().status()), session.error().c_str());
        return;
    }
    // The Space Terminal: a Galactic Civil War space battle, for the Rebellion.
    step(10, [&] { return session.client().spawned() && hub_server.state_of(session.client().entity_id()); });
    const auto state = hub_server.state_of(session.client().entity_id());
    CHECK(state != nullptr);
    if (!state) {
        return;
    }
    MoveState at_space = *state;
    at_space.position = {60, 0, 0};
    hub_server.set_state(session.client().entity_id(), at_space);
    step(1, [] { return false; });
    session.client().queue(0, uint8_t(GameMode::ctf));  // a space terminal makes it space assault
    const int before = session.generation();
    const bool flew = wait_real([&] { return session.generation() > before && session.client().match().has_value(); });
    CHECK(flew);
    std::printf("  e2e: transferred to %s\n", battle_id.c_str());
    CHECK(session.client().config().battlefield && session.client().config().battlefield->is_space());
}

} // namespace

#ifdef _WIN32
// For the opt-in live run: a crash prints where (with debug info).
LONG WINAPI print_crash(EXCEPTION_POINTERS* info) {
    std::fprintf(stderr, "CRASH: exception 0x%08lx at %p\n", info->ExceptionRecord->ExceptionCode,
                 info->ExceptionRecord->ExceptionAddress);
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(process, nullptr, TRUE);
    void* frames[48];
    const USHORT count = CaptureStackBackTrace(0, 48, frames, nullptr);
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
    for (USHORT i = 0; i < count; ++i) {
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD line_displacement = 0;
        const DWORD64 address = DWORD64(frames[i]);
        const bool named = SymFromAddr(process, address, &displacement, symbol);
        const bool lined = SymGetLineFromAddr64(process, address, &line_displacement, &line);
        std::fprintf(stderr, "  #%u %s  %s:%lu\n", i, named ? symbol->Name : "?", lined ? line.FileName : "?",
                     lined ? line.LineNumber : 0);
    }
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main() {
#ifdef _WIN32
    if (std::getenv("SWM_STAGE11_E2E") != nullptr) {
        AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* info) -> LONG {
            if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
                print_crash(info);
            }
            return EXCEPTION_CONTINUE_SEARCH;
        });
    }
#endif
    const auto dir = write_field();
    test_loading_modes(dir);
    test_ctf_rules(dir);
    test_one_flag_and_return(dir);
    test_hunt_and_time_limit(dir);
    test_wire();
    test_telemetry();
    test_real_roster();
    test_live_hub_e2e();
    return swm::test::finish("modes_test");
}
