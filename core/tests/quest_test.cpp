// Stage 5 quests: the quest book's steps (go to, kill, collect, talk to,
// deliver), prerequisites, missions from lairs and camps, the shipped
// Tatooine quests, and a quest taken, advanced and turned in through a
// server, its journal saved.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include "swbfo/core/client.h"
#include "swbfo/core/heightfield.h"
#include "swbfo/core/hub.h"
#include "swbfo/core/items.h"
#include "swbfo/core/net/link.h"
#include "swbfo/core/profession.h"
#include "swbfo/core/quest.h"
#include "swbfo/core/server.h"
#include "swbfo/core/sqlite_store.h"
#include "swbfo/core/wildlife.h"
#include "swbfo/core/world.h"
#include "test.h"

using namespace swbfo::core;

namespace {

std::filesystem::path repo_root() {
    auto p = std::filesystem::current_path();
    while (!p.empty() && !std::filesystem::exists(p / "game" / "items" / "swg_catalog.ini")) {
        auto parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return p;
}

QuestDef hunt_quest() {
    QuestDef def;
    def.id = "hunt";
    def.title = "The Hunt";
    def.giver = "Hunter";
    QuestStep go;
    go.kind = QuestStepKind::go_to;
    go.where = Vec3{100, 0, 0};
    go.radius = 10;
    QuestStep kill;
    kill.kind = QuestStepKind::kill;
    kill.target = "womp_rat";
    kill.count = 2;
    kill.where = Vec3{100, 0, 0};
    kill.radius = 50;
    QuestStep collect;
    collect.kind = QuestStepKind::collect;
    collect.target = "trophy";
    collect.count = 1;
    collect.drop_from = "Rat Gang";
    collect.drop_chance = 1;
    QuestStep deliver;
    deliver.kind = QuestStepKind::deliver;
    deliver.target = "trophy";
    deliver.npc = "Hunter";
    def.steps = {go, kill, collect, deliver};
    def.reward.credits = 100;
    return def;
}

void test_book_steps() {
    QuestBook book;
    book.add(hunt_quest());
    QuestDef next;
    next.id = "next";
    next.giver = "Hunter";
    next.prerequisites = {"hunt"};
    QuestStep talk;
    talk.kind = QuestStepKind::talk_to;
    talk.npc = "Elder";
    next.steps = {talk};
    next.repeatable = true;
    book.add(next);

    std::vector<QuestProgress> journal;
    CHECK(book.offered_by("hunter", journal).size() == 1);  // names ignore case; "next" needs "hunt"
    CHECK(book.offered_by("Elder", journal).empty());
    CHECK(book.why_none("Hunter", journal).empty());  // it offers "hunt"
    CHECK(book.why_none("Elder", journal) == "Elder has no more work for you.");
    CHECK(book.can_accept(*book.find("hunt"), journal));
    CHECK(!book.can_accept(*book.find("next"), journal));
    book.accept(journal, *book.find("hunt"));
    CHECK(journal.size() == 1 && journal[0].step == 0);
    // On it: the hunter says so ("next" waits for it).
    CHECK(book.why_none("Hunter", journal).starts_with("You're already doing "));
    CHECK(!book.can_accept(*book.find("hunt"), journal));  // already taken

    // Go to: not there yet, then there.
    CHECK(!book.on_position(journal, {50, 0, 0}).changed);
    QuestEffects arrived = book.on_position(journal, {95, 0, 3});
    CHECK(arrived.changed && journal[0].step == 1);

    // Kill: the wrong kind, and the right kind outside the area, don't count.
    const auto never = [] { return 0.99; };
    CHECK(!book.on_kill(journal, {"worrt", "", false}, {100, 0, 0}, never).changed);
    CHECK(!book.on_kill(journal, {"womp_rat", "", false}, {300, 0, 0}, never).changed);
    CHECK(book.on_kill(journal, {"womp_rat", "", false}, {120, 0, 0}, never).changed);
    CHECK(journal[0].step == 1 && journal[0].count == 1);
    book.on_kill(journal, {"womp_rat", "", false}, {110, 0, 0}, never);
    CHECK(journal[0].step == 2 && journal[0].count == 0);

    // Collect: a kill by the gang drops it; the inventory then counts.
    int trophies = 0;
    const auto held = [&](const std::string& item) { return item == "trophy" ? trophies : 0; };
    QuestEffects dropped = book.on_kill(journal, {"rat_boss", "Rat Gang", true}, {0, 0, 0}, never);
    CHECK(dropped.grant.size() == 1 && dropped.grant[0] == "trophy");  // chance 1
    CHECK(journal[0].step == 2);
    trophies = 1;
    CHECK(book.on_inventory(journal, held).changed && journal[0].step == 3);

    // Deliver: talking to someone else does nothing; to the hunter hands it over.
    CHECK(book.on_talk(journal, "Elder", held).take.empty());
    QuestEffects delivered = book.on_talk(journal, "Hunter", held);
    CHECK(delivered.take.size() == 1 && delivered.take[0].first == "trophy" && delivered.take[0].second == 1);
    CHECK(delivered.completed.size() == 1 && delivered.completed[0] == "hunt");
    CHECK(journal.size() == 1 && journal[0].done);
    CHECK(!book.can_accept(*book.find("hunt"), journal));  // done, not repeatable

    // Its follow-up, repeatable: done, it leaves the journal and can be taken again.
    CHECK(book.offered_by("Hunter", journal).size() == 1);
    book.accept(journal, *book.find("next"));
    QuestEffects talked = book.on_talk(journal, "Elder", held);
    CHECK(talked.completed.size() == 1);
    CHECK(journal.size() == 1 && journal[0].id == "hunt");
    CHECK(book.can_accept(*book.find("next"), journal));

    // Abandoning.
    book.accept(journal, *book.find("next"));
    CHECK(book.abandon(journal, "next").changed && journal.size() == 1);
    CHECK(!book.abandon(journal, "hunt").changed);  // a finished quest stays finished
}

// What a kill step means in the world: the names to mark, where to go, and
// who still has work.
void test_targets_and_waiting_givers() {
    WorldSpawns spawns;
    spawns.creatures.push_back({.id = "womp_rat", .name = "Lesser Womp Rat", .tier = 1});
    spawns.creatures.push_back({.id = "worrt", .name = "Worrt", .tier = 1});
    spawns.lairs.push_back({"womp_rat", {0, 0, 300}, 25, 4});
    spawns.lairs.push_back({"womp_rat", {0, 0, 900}, 25, 4});
    spawns.bandits.push_back({.id = "thug", .name = "Syndicate Thug", .gang = "Syndicate", .tier = 1});
    spawns.bandits.push_back({.id = "boss", .name = "Syndicate Boss", .gang = "Syndicate", .tier = 2});
    spawns.camps.push_back({"Syndicate Camp", {600, 0, 0}, 22, {"thug", "boss"}});
    QuestBook book;
    book.set_world(&spawns, {0, 0, 0});

    CHECK(book.target_names("womp_rat") == std::vector<std::string>{"Lesser Womp Rat"});
    CHECK(book.target_names("Syndicate").size() == 2);
    CHECK(book.target_names("creature").size() == 2);
    CHECK(book.target_names("bandit").size() == 2);
    CHECK(book.target_names("nothing").empty());
    const auto near = book.nearest_of("womp_rat", {0, 0, 800});
    CHECK(near && near->z == 900);
    const auto camp = book.nearest_of("Syndicate", {0, 0, 0});
    CHECK(camp && camp->x == 600);
    CHECK(!book.nearest_of("worrt", {0, 0, 0}));

    QuestDef first;
    first.id = "first";
    first.giver = "Ana";
    first.steps = {QuestStep{}};
    QuestDef second;
    second.id = "second";
    second.giver = "Ben";
    second.prerequisites = {"first"};
    second.steps = {QuestStep{}};
    book.add(first);
    book.add(second);
    std::vector<QuestProgress> journal;
    CHECK(book.has_work_left("Ana", journal) && book.has_work_left("Ben", journal));
    CHECK(book.offered_by("Ben", journal).empty());  // waiting on Ana's
    journal.push_back({"first", 0, 0, true});
    CHECK(!book.has_work_left("Ana", journal));       // all done: nothing more
    CHECK(book.has_work_left("Ben", journal) && !book.offered_by("Ben", journal).empty());
}

void test_missions() {
    WorldSpawns spawns;
    spawns.creatures.push_back({.id = "womp_rat", .name = "Lesser Womp Rat", .tier = 1, .xp_reward = 30});
    spawns.lairs.push_back({"womp_rat", {0, 0, -100}, 25, 4});     // too close
    spawns.lairs.push_back({"womp_rat", {0, 0, 800}, 25, 4});      // 800 m north
    spawns.lairs.push_back({"womp_rat", {-1500, 0, 0}, 25, 5});    // 1.5 km east (x is negated)
    spawns.lairs.push_back({"womp_rat", {0, 0, -9000}, 25, 4});    // too far
    spawns.bandits.push_back({.id = "thug", .name = "Thug", .gang = "Thugs", .tier = 2, .xp_reward = 50});
    spawns.camps.push_back({"Thug Camp", {600, 0, -600}, 22, {"thug", "thug", "thug"}});
    QuestBook book;
    book.set_world(&spawns, {0, 0, 0});

    std::vector<QuestProgress> journal;
    const auto offered = book.missions(0, journal);
    CHECK(offered.size() == 3);
    CHECK(offered.size() == 3 && offered[0]->id == "mission:lair:1");  // nearest first
    const QuestDef* east = book.find("mission:lair:2");
    CHECK(east != nullptr && east->mission && east->steps.size() == 2);
    CHECK(east && east->steps[1].kind == QuestStepKind::kill && east->steps[1].count == 5);
    CHECK(east && east->summary.find("1.5 km east") != std::string::npos);
    CHECK(east && east->reward.credits == 30 * 1 * 5 + 375);
    const QuestDef* camp = book.find("mission:camp:0");
    CHECK(camp && camp->steps[1].target == "Thugs" && camp->steps[1].count == 3);
    CHECK(camp && camp->summary.find("south-west") != std::string::npos);
    CHECK(book.find("mission:lair:99") == nullptr && book.find("mission:junk") == nullptr);
    CHECK(describe_direction({0, 0, 0}, {0, 0, 300}) == "300 m north");

    // At most two missions; a finished one leaves the journal.
    book.accept(journal, *book.find("mission:lair:1"));
    book.accept(journal, *east);
    CHECK(!book.can_accept(*camp, journal));
    CHECK(book.missions(0, journal).size() == 1);  // what's taken isn't offered again
    book.on_position(journal, {-1500, 0, 10});
    for (int i = 0; i < 5; ++i) {
        book.on_kill(journal, {"womp_rat", "", false}, {-1510, 0, 0}, [] { return 0.5; });
    }
    CHECK(journal.size() == 1 && journal[0].id == "mission:lair:1");
    CHECK(book.can_accept(*camp, journal));
}

void test_tatooine_quests() {
    const auto root = repo_root();
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    const HubConfig hub = load_hub(root / "game" / "hubs" / "mos_eisley.ini");
    CHECK(hub.quests.quests().size() >= 5);
    CHECK(hub.mission_terminals.size() >= 10);
    bool has_bounty = false, has_artisan = false, has_imperial = false, has_rebel = false;
    for (const net::MissionTerminal& t : hub.mission_terminals) {
        has_bounty |= (t.kind == "bounty");
        has_artisan |= (t.kind == "artisan");
        has_imperial |= (t.kind == "imperial");
        has_rebel |= (t.kind == "rebel");
    }
    CHECK(has_bounty && has_artisan && has_imperial && has_rebel);
    const QuestDef* stewpot = hub.quests.find("something_for_the_stewpot");
    CHECK(stewpot != nullptr && !stewpot->steps.empty() && stewpot->steps[0].track == "worrt");
    CHECK(hub.spawns.has_value());
    const auto person = [&](const std::string& name) {
        return std::any_of(hub.people.begin(), hub.people.end(), [&](const HubPerson& p) { return p.name == name; });
    };
    const auto known_target = [&](const std::string& target) {
        if (target == "any" || target == "creature" || target == "bandit") return true;
        for (const auto& c : hub.spawns->creatures) if (c.id == target) return true;
        for (const auto& b : hub.spawns->bandits) if (b.id == target || b.gang == target) return true;
        return false;
    };
    // Its services: trainers are people (of the same name) who teach real professions.
    ProfessionBook professions;
    professions.load(root / "game" / "professions" / "professions.ini");
    int trainers = 0, banks = 0, clones = 0, clinics = 0;
    for (const net::Vendor& v : hub.vendors) {
        trainers += v.kind == "trainer";
        banks += v.kind == "bank";
        clones += v.kind == "cloning";
        clinics += v.kind == "clinic";
        if (v.kind != "trainer") continue;
        CHECK(person(v.name));
        CHECK(!v.detail.empty());
        for (size_t at = 0, next = 0; at < v.detail.size(); at = next + 1) {
            next = v.detail.find(',', at);
            if (next == std::string::npos) next = v.detail.size();
            const ProfessionDef* def = professions.find(v.detail.substr(at, next - at));
            CHECK(def != nullptr && def->available && def->chooses());
        }
    }
    CHECK(trainers >= 5 && banks >= 4 && clones >= 1 && clinics >= 1);
    // The opening chain asks for what the services do.
    const QuestDef* opening = hub.quests.find("stranger_in_mos_eisley");
    CHECK(opening != nullptr && std::any_of(opening->steps.begin(), opening->steps.end(), [](const QuestStep& st) {
        return st.kind == QuestStepKind::event && st.target == "bank";
    }));
    for (const QuestDef& def : hub.quests.quests()) {
        CHECK(person(def.giver));
        for (const std::string& required : def.prerequisites) CHECK(hub.quests.find(required) != nullptr);
        for (const std::string& item : def.reward.items) CHECK(catalog.find(item) != nullptr);
        for (const QuestStep& step : def.steps) {
            if (!step.npc.empty()) CHECK(person(step.npc));
            if (step.kind == QuestStepKind::kill) CHECK(known_target(step.target));
            if (step.kind == QuestStepKind::collect || step.kind == QuestStepKind::deliver) {
                CHECK(catalog.find(step.target) != nullptr);
            }
            if (!step.drop_from.empty()) CHECK(known_target(step.drop_from));
            CHECK(!step.text.empty());
        }
    }
    const ItemDef* ledger = catalog.find("quest_valarian_ledger");
    CHECK(ledger != nullptr && ledger->kind == ItemKind::quest && ledger->sell_value == 0);

    QuestBook book = hub.quests;
    book.set_world(&*hub.spawns, *hub.spawn);
    const auto missions = book.missions(1234, {});
    CHECK(missions.size() == 6);
    for (const QuestDef* m : missions) CHECK(m->reward.credits > 0 && !m->summary.empty());
    // Another ten minutes, another set (most likely: 22 lairs and camps).
    const auto later = book.missions(1234 + 600, {});
    CHECK(later.size() == 6);
}

void test_server_quest() {
    const auto root = repo_root();
    const auto db_path = std::filesystem::temp_directory_path() / "swbfo_quest_test.sqlite";
    std::filesystem::remove(db_path);
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};

    QuestBook book;
    {
        QuestDef def;
        def.id = "rats";
        def.title = "Rats";
        def.giver = "Hunter";
        def.summary = "Kill two rats and bring me a ledger.";
        QuestStep kill;
        kill.kind = QuestStepKind::kill;
        kill.target = "womp_rat";
        kill.count = 2;
        kill.text = "Kill womp rats";
        QuestStep collect;
        collect.kind = QuestStepKind::collect;
        collect.target = "quest_valarian_ledger";
        collect.drop_from = "womp_rat";
        collect.drop_chance = 1;
        collect.text = "Find the ledger";
        QuestStep deliver;
        deliver.kind = QuestStepKind::deliver;
        deliver.target = "quest_valarian_ledger";
        deliver.npc = "Hunter";
        deliver.text = "Bring it back";
        def.steps = {kill, collect, deliver};
        def.reward.credits = 250;
        def.reward.experience = 40;
        def.reward.items = {"supply_bacta_stim"};
        book.add(def);
    }
    WorldSpawns spawns;
    spawns.creatures.push_back({.id = "womp_rat", .name = "Lesser Womp Rat", .tier = 1, .xp_reward = 30});
    spawns.lairs.push_back({"womp_rat", {0, 0, 500}, 25, 4});
    book.set_world(&spawns, {0, 0, 0});

    {
        SqliteCharacterStore store(db_path);
        net::LoopbackHub links;
        net::Endpoint server_ep{1, 1}, client_ep{2, 1};
        net::LoopbackLink srv_link{links, server_ep};
        net::LoopbackLink cli_link{links, client_ep};
        ServerConfig config;
        config.catalog = &catalog;
        config.pvp_enabled = false;
        config.spawn = {0, 0, 0};
        GameServer server(srv_link, world, config, &store);
        server.set_looks({"object/mobile/shared_dressed_commoner_tatooine_rodian_male_01.iff"});
        const uint32_t hunter = server.add_npc("Hunter", {3, 0, 0}, 0, 1);
        server.set_quests(&book, {{hunter, "Hunter", {3, 0, 0}, true}}, {{"Mission Terminal", {0, 0, 3}, 3}});
        // Two towns: the hunter's, and one by the lair (city reputation).
        server.set_towns({{"home", "Home", {0, 0, 0}, 100}, {"far", "Far", {0, 0, 650}, 100}});
        CHECK(server.town_at({0, 0, 500}) && server.town_at({0, 0, 500})->id == "far");
        CHECK(server.town_at({3, 0, 0}) && server.town_at({3, 0, 0})->id == "home");
        GameClient client(cli_link, world, ClientConfig{.server = server_ep, .name = "Questor"});
        double now = 0;
        const auto run = [&](double seconds) {
            for (const double end = now + seconds; now < end; now += 1.0 / 60.0) {
                server.update(now);
                client.update(now);
            }
        };
        run(0.5);
        CHECK(client.status() == GameClient::Status::connected);
        CHECK(client.quest_npcs().size() == 1 && client.quest_npcs()[0].giver);
        CHECK(client.mission_terminals().size() == 1);
        const int64_t credits = client.player_status().credits;
        const size_t items = client.items().size();

        // Talk to the hunter: his offer arrives.
        const uint16_t serial = client.quests().offer_serial;
        client.talk_to(hunter);
        run(0.3);
        CHECK(client.quests().offer_serial != serial);
        CHECK(client.quests().offer_source == "Hunter");
        CHECK(client.quests().offers.size() == 1 && client.quests().offers[0].steps.size() == 3);
        // Accepting what wasn't offered does nothing; what was, takes it.
        client.accept_quest("mission:lair:0");
        client.accept_quest("rats");
        run(0.3);
        CHECK(client.quests().active.size() == 1);
        CHECK(client.quests().active.size() == 1 && client.quests().active[0].needed == 2 &&
              client.quests().active[0].steps.size() == 1);

        // Two kills, then the ledger drops on the next.
        server.quest_kill(client.entity_id(), {"womp_rat", "", false}, {10, 0, 10});
        run(0.2);
        CHECK(client.quests().active.size() == 1 && client.quests().active[0].count == 1);
        server.quest_kill(client.entity_id(), {"womp_rat", "", false}, {10, 0, 10});
        server.quest_kill(client.entity_id(), {"womp_rat", "", false}, {10, 0, 10});
        run(0.8);
        CHECK(client.items().size() == items + 1);
        CHECK(client.quests().active.size() == 1 && client.quests().active[0].step == 2);
        CHECK(client.quests().active.size() == 1 && client.quests().active[0].has_waypoint);  // the hunter

        // Hand it over: the ledger goes, the reward comes.
        client.talk_to(hunter);
        run(0.3);
        CHECK(client.quests().active.empty());
        CHECK(client.quests().offer_note == "__completed__");
        CHECK(client.quests().completed.size() == 1 && client.quests().completed[0] == "Rats");
        CHECK(client.player_status().credits == credits + 250);
        // ... and standing with the giver's town, saved.
        CHECK(client.reputation().contains("town:home") &&
              client.reputation().at("town:home") == quest_town_reputation(1, false));
        CHECK(!client.reputation().contains("town:far"));
        CHECK(store.list_reputation(client.character_id())["town:home"] == quest_town_reputation(1, false));
        CHECK(client.items().size() == items + 1);  // the stim, not the ledger
        CHECK(std::none_of(client.items().begin(), client.items().end(),
                           [](const ItemInstance& i) { return i.def_id == "quest_valarian_ledger"; }));

        // The mission terminal: offers, then one taken.
        client.use_mission_terminal(0);
        run(0.3);
        CHECK(client.quests().offer_source == "Mission Terminal");
        CHECK(client.quests().offers.size() == 1 && client.quests().offers[0].mission);
        client.accept_quest("mission:lair:0");
        run(0.3);
        CHECK(client.quests().active.size() == 1 && client.quests().active[0].has_waypoint);
        client.disconnect();
        run(0.1);
    }
    {
        SqliteCharacterStore store(db_path);
        const auto saved = store.find_by_name("Questor");
        CHECK(saved.has_value());
        const auto journal = saved ? store.list_quests(saved->id) : std::vector<QuestProgress>{};
        CHECK(journal.size() == 2);
        CHECK(journal.size() == 2 && journal[0].id == "rats" && journal[0].done);
        CHECK(journal.size() == 2 && journal[1].id == "mission:lair:0" && !journal[1].done);
    }
    std::filesystem::remove(db_path);
}

// The Legacy's needs (docs/LEGACY.md, phase 1a): quests across planets,
// branches that rule each other out, comms when a step starts, quest files
// from a folder, and finished quests kept on planets that don't know them.
void test_legacy_book() {
    const auto dir = std::filesystem::temp_directory_path() / "swbfo_legacy_quests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        std::ofstream a(dir / "a_tatooine.ini");
        a << "[quest]\nId = search_rebel\nTitle = Starting the Search\nGiver = Reimos\nPlanet = tatooine\n"
             "Excludes = search_imperial\nNext = to_naboo\n"
             "[step]\nKind = talk_to\nNpc = Captain Bastra\nCommFrom = Jabba\nComm = Find the head.\n"
             "[quest]\nId = search_imperial\nTitle = Starting the Search\nGiver = Reimos\nPlanet = tatooine\n"
             "[step]\nKind = talk_to\nNpc = Commander Barrezz\n";
        std::ofstream b(dir / "b_naboo.ini");
        b << "[quest]\nId = to_naboo\nTitle = To Naboo\nPlanet = tatooine\nRequires = search_rebel\n"
             "[step]\nKind = go_to\nPosition = 10 0\nRadius = 5\nText = Leave Tatooine\n"
             "[step]\nKind = talk_to\nNpc = Tchorshel\nPlanet = Naboo\nCommFrom = Panaka\nComm = Come to Kaadara.\n"
             "[step]\nKind = kill\nTarget = skaak\nCount = 1\nPlanet = naboo\n";
        std::ofstream ignored(dir / "notes.txt");
        ignored << "not a quest file";
    }
    QuestBook tatooine;
    tatooine.load(dir);
    tatooine.set_planet("Tatooine");
    QuestBook naboo;
    naboo.load(dir);
    naboo.set_planet("naboo");
    CHECK(tatooine.quests().size() == 3 && naboo.quests().size() == 3);
    CHECK(normalize_planet("Yavin 4") == "yavin4");
    const QuestDef* to_naboo = naboo.find("to_naboo");
    CHECK(to_naboo != nullptr && to_naboo->steps[0].planet == "tatooine" && to_naboo->steps[1].planet == "naboo");
    CHECK(to_naboo != nullptr && to_naboo->steps[1].comm_from == "Panaka");

    // Givers are only looked for on their own planet.
    CHECK(tatooine.givers() == std::vector<std::string>{"Reimos"});
    CHECK(naboo.givers().empty());

    // A branch: taking one rules the other out, both ways, and isn't work left.
    std::vector<QuestProgress> journal;
    CHECK(tatooine.offered_by("Reimos", journal).size() == 2);
    QuestEffects took = tatooine.accept(journal, *tatooine.find("search_rebel"));
    CHECK(took.comms.size() == 1 && took.comms[0].from == "Jabba" && took.comms[0].text == "Find the head.");
    CHECK(tatooine.offered_by("Reimos", journal).empty());
    CHECK(!tatooine.can_accept(*tatooine.find("search_imperial"), journal));
    tatooine.on_talk(journal, "Captain Bastra", {});
    CHECK(journal[0].done);
    CHECK(!tatooine.has_work_left("Reimos", journal));  // the Imperial branch isn't owed
    CHECK(tatooine.why_none("Reimos", journal) == "Reimos has no more work for you.");

    // The chain's next quest; its steps only advance on their planet.
    CHECK(tatooine.can_accept(*to_naboo, journal));
    tatooine.accept(journal, *tatooine.find("to_naboo"));
    CHECK(naboo.on_position(journal, {-10, 0, 0}).changed == false);  // Tatooine's spot, but on Naboo
    QuestEffects left = tatooine.on_position(journal, {-10, 0, 0});
    CHECK(journal[1].step == 1);
    CHECK(left.comms.size() == 1 && left.comms[0].from == "Panaka");  // the step that started calls
    CHECK(!tatooine.on_talk(journal, "Tchorshel", {}).changed);  // a namesake on Tatooine doesn't count
    CHECK(naboo.on_talk(journal, "Tchorshel", {}).changed && journal[1].step == 2);
    CHECK(!tatooine.on_kill(journal, {"skaak", "", false}, {}, {}).changed);
    CHECK(naboo.on_kill(journal, {"skaak", "", false}, {}, {}).changed && journal[1].done);

    // A finished quest a planet doesn't know is kept there (it was erased).
    QuestBook elsewhere;
    QuestDef other = hunt_quest();
    elsewhere.add(other);
    std::vector<QuestProgress> kept = {{"search_rebel", 1, 0, true}, {"mission:lair:1", 2, 0, true}};
    elsewhere.on_event(kept, "bank");
    CHECK(kept.size() == 1 && kept[0].id == "search_rebel");

    // Lairs grow for this planet's kill steps only.
    WorldSpawns spawns;
    spawns.lairs.push_back({"skaak", {0, 0, 0}, 20, 0});
    tatooine.fit_lairs(spawns);
    CHECK(spawns.lairs[0].count == 0);
    naboo.fit_lairs(spawns);
    CHECK(spawns.lairs[0].count == 1);
    std::filesystem::remove_all(dir);
}

// Legacy phase 1b: use steps (quest objects), each object once, in reach.
void test_use_steps() {
    const auto dir = std::filesystem::temp_directory_path() / "swbfo_use_quests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto load = [&](const std::string& text) {
        std::ofstream(dir / "q.ini") << text;
        QuestBook book;
        book.load(dir / "q.ini");
        return book;
    };
    QuestBook book = load(
        "[quest]\nId = crates\nTitle = Crates\nPlanet = tatooine\n"
        "[step]\nKind = use\nObject = Supply Crate\nTemplate = object/tangible/container/shared_crate.iff\n"
        "Gives = datadisk\nCount = 2\nPosition = 10 0\nPosition = 20 0\nPosition = 30 0\n"
        "[step]\nKind = use\nObject = Bomb\nPosition = 0 50 -12\nRadius = 4\n");
    book.set_planet("tatooine");
    const QuestDef* def = book.find("crates");
    CHECK(def != nullptr && def->steps[0].kind == QuestStepKind::use && def->steps[0].spots.size() == 3);
    CHECK(def != nullptr && def->steps[0].count == 2 && def->steps[0].radius == 3 && !def->steps[0].spot_heights);
    CHECK(def != nullptr && def->steps[0].spots[1].x == -20 && def->steps[0].look.ends_with("shared_crate.iff"));
    CHECK(def != nullptr && def->steps[1].spot_heights && def->steps[1].spots[0].y == -12 && def->steps[1].radius == 4);
    if (def == nullptr) return;

    std::vector<QuestProgress> journal;
    book.accept(journal, *def);
    CHECK(!book.on_use(journal, "crates", 0, {-15, 0, 0}).changed);  // 5 m away: out of reach
    CHECK(!book.on_use(journal, "crates", 3, {-10, 0, 0}).changed);  // no such object
    CHECK(!book.on_use(journal, "other", 0, {-10, 0, 0}).changed);
    QuestEffects first = book.on_use(journal, "crates", 0, {-11, 0, 1});
    CHECK(first.changed && first.grant == std::vector<std::string>{"datadisk"} && journal[0].step == 0);
    CHECK(QuestBook::objects_used(journal[0].count) == 1 && QuestBook::object_used(journal[0].count, 0));
    CHECK(!book.on_use(journal, "crates", 0, {-10, 0, 0}).changed);  // each once
    QuestEffects second = book.on_use(journal, "crates", 2, {-30, 0, 0});
    CHECK(second.changed && journal[0].step == 1 && journal[0].count == 0);  // 2 of 3 is enough
    // An object with a height: a floor above or below it is out of reach.
    CHECK(!book.on_use(journal, "crates", 0, {0, 0, 50}).changed);
    CHECK(book.on_use(journal, "crates", 0, {0, -11, 50}).completed == std::vector<std::string>{"crates"});

    // On another planet nothing is used.
    QuestBook naboo = load(
        "[quest]\nId = far\nTitle = Far\nPlanet = naboo\n[step]\nKind = use\nObject = Panel\nPosition = 0 0\n");
    naboo.set_planet("tatooine");
    std::vector<QuestProgress> far;
    naboo.accept(far, *naboo.find("far"));
    CHECK(!naboo.on_use(far, "far", 0, {0, 0, 0}).changed);

    // Malformed use steps.
    const auto fails = [&](const std::string& text) {
        try {
            load("[quest]\nId = bad\nTitle = Bad\n[step]\nKind = use\n" + text);
        } catch (const std::runtime_error&) {
            return true;
        }
        return false;
    };
    CHECK(fails("Object = Panel\n"));                                   // no Position
    CHECK(fails("Position = 1 2\n"));                                   // no Object
    CHECK(fails("Object = Panel\nPosition = 1 2\nPosition = 1 2 3\n"));  // a height on only one
    std::filesystem::remove_all(dir);
}

// Next = through a server: completing a quest starts the next one by itself,
// with its comm.
void test_server_chain() {
    const auto root = repo_root();
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    QuestBook book;
    {
        QuestDef first;
        first.id = "short_walk";
        first.title = "Short Walk";
        first.giver = "Vourk";
        first.next = {"respect"};
        QuestStep talk;
        talk.kind = QuestStepKind::talk_to;
        talk.npc = "Vourk";
        first.steps = {talk};
        book.add(first);
        QuestDef second;
        second.id = "respect";
        second.title = "Respect";
        second.prerequisites = {"short_walk"};
        QuestStep kill;
        kill.kind = QuestStepKind::kill;
        kill.target = "swooper";
        kill.text = "Kill swoopers";
        kill.comm_from = "Bib Fortuna";
        kill.comm = "You work for Jabba now.";
        second.steps = {kill};
        book.add(second);
    }
    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1}, client_ep{2, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink cli_link{links, client_ep};
    ServerConfig config;
    config.catalog = &catalog;
    config.pvp_enabled = false;
    config.spawn = {0, 0, 0};
    GameServer server(srv_link, world, config, nullptr);
    server.set_looks({"object/mobile/shared_dressed_commoner_tatooine_rodian_male_01.iff"});
    const uint32_t vourk = server.add_npc("Vourk", {3, 0, 0}, 0, 1);
    server.set_quests(&book, {{vourk, "Vourk", {3, 0, 0}, true}}, {});
    GameClient client(cli_link, world, ClientConfig{.server = server_ep, .name = "Chainer"});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 60.0) {
            server.update(now);
            client.update(now);
        }
    };
    run(0.5);
    client.talk_to(vourk);
    run(0.3);
    CHECK(client.quests().offers.size() == 1 && client.quests().offers[0].id == "short_walk");
    client.accept_quest("short_walk");
    run(0.3);
    client.talk_to(vourk);  // completes it: Respect starts by itself
    run(0.3);
    CHECK(client.quests().active.size() == 1 && client.quests().active[0].id == "respect");
    CHECK(client.quests().completed.size() == 1);
    CHECK(client.player_status().notice.find("Short Walk") != std::string::npos ||
          client.player_status().notice.find("Respect") != std::string::npos);
    const auto journal = server.quests_of(client.entity_id());
    CHECK(journal != nullptr && journal->size() == 2 && journal->at(1).id == "respect" && !journal->at(1).done);
    // Respect's comm reaches the comm window, once.
    const auto comms = client.take_comms();
    CHECK(comms.size() == 1 && comms[0].from == "Bib Fortuna" && comms[0].text == "You work for Jabba now." &&
          comms[0].quest_id == "respect");
    run(0.3);
    CHECK(client.take_comms().empty());
    client.disconnect();
    run(0.1);
}

// A use step through a server: its objects in the log, used in reach.
void test_server_use() {
    const auto root = repo_root();
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    QuestBook book;
    {
        QuestDef search;
        search.id = "search";
        search.title = "The Search";
        search.giver = "Vourk";
        QuestStep use;
        use.kind = QuestStepKind::use;
        use.target = "Datapad";
        use.look = "object/tangible/loot/shared_datapad.iff";
        use.spots = {Vec3{1, 0, 0}, Vec3{60, 0, 0}};
        use.where = use.spots[0];
        use.radius = 3;
        use.count = 1;
        search.steps = {use};
        book.add(search);
    }
    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1}, client_ep{2, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink cli_link{links, client_ep};
    ServerConfig config;
    config.catalog = &catalog;
    config.pvp_enabled = false;
    config.spawn = {0, 0, 0};
    GameServer server(srv_link, world, config, nullptr);
    server.set_looks({"object/mobile/shared_dressed_commoner_tatooine_rodian_male_01.iff"});
    const uint32_t vourk = server.add_npc("Vourk", {3, 0, 0}, 0, 1);
    server.set_quests(&book, {{vourk, "Vourk", {3, 0, 0}, true}}, {});
    GameClient client(cli_link, world, ClientConfig{.server = server_ep, .name = "Searcher"});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 60.0) {
            server.update(now);
            client.update(now);
        }
    };
    run(0.5);
    client.talk_to(vourk);
    run(0.3);
    client.accept_quest("search");
    run(0.3);
    CHECK(client.quests().active.size() == 1);
    if (client.quests().active.size() == 1) {
        const net::QuestInfo& q = client.quests().active[0];
        CHECK(q.objects.size() == 2 && q.objects[0].name == "Datapad" && q.objects[1].index == 1);
        CHECK(q.objects.size() == 2 && q.objects[0].look.ends_with("shared_datapad.iff") && !q.objects[0].has_height);
        CHECK(q.has_waypoint && q.waypoint.x == 1);  // the nearer
        CHECK(q.steps.size() == 1 && q.steps[0] == "Use the Datapad");
    }
    client.use_quest_object("search", 1);  // 60 m away
    run(0.3);
    CHECK(client.quests().active.size() == 1 && client.quests().completed.empty());
    client.use_quest_object("search", 0);
    run(0.3);
    CHECK(client.quests().active.empty() && client.quests().completed.size() == 1);
    client.disconnect();
    run(0.1);
}

} // namespace

int main() {
    test_book_steps();
    test_missions();
    test_targets_and_waiting_givers();
    test_tatooine_quests();
    test_server_quest();
    test_legacy_book();
    test_server_chain();
    test_use_steps();
    test_server_use();
    return swbfo::test::finish("quest_test");
}
