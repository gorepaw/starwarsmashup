// A hub's service points: the bank (credits and items, kept across visits), a
// clinic's patch-up, a cloning terminal (where the clone is kept), and the
// quest steps that ask for something done at them.

#include <algorithm>
#include <filesystem>
#include <vector>

#include "swm/core/client.h"
#include "swm/core/heightfield.h"
#include "swm/core/items.h"
#include "swm/core/net/link.h"
#include "swm/core/profession.h"
#include "swm/core/quest.h"
#include "swm/core/server.h"
#include "swm/core/sqlite_store.h"
#include "swm/core/world.h"
#include "test.h"

using namespace swm::core;

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

void test_services() {
    const auto root = repo_root();
    const auto db_path = std::filesystem::temp_directory_path() / "swm_services_test.sqlite";
    std::filesystem::remove(db_path);
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    ProfessionBook book;
    book.load(root / "game" / "professions" / "professions.ini");
    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};

    uint64_t character = 0;
    {
        SqliteCharacterStore store(db_path);
        net::LoopbackHub links;
        net::Endpoint server_ep{1, 1}, client_ep{2, 1};
        net::LoopbackLink srv_link{links, server_ep};
        net::LoopbackLink cli_link{links, client_ep};
        ServerConfig config;
        config.catalog = &catalog;
        config.professions = &book;
        config.pvp_enabled = false;
        config.spawn = {0, 0, 0};
        GameServer server(srv_link, world, config, &store);
        server.set_vendors({{"Bank", "bank", {0, 0, 0}, 4, ""},
                            {"Clinic", "clinic", {0, 0, 0}, 4, ""},
                            {"Clone Vat", "cloning", {0, 0, 0}, 4, ""}});
        GameClient client(cli_link, world, ClientConfig{.server = server_ep, .name = "Banker"});
        double now = 0;
        const auto run = [&](double seconds) {
            for (const double end = now + seconds; now < end; now += 1.0 / 60.0) {
                server.update(now);
                client.update(now);
            }
        };
        run(0.5);
        CHECK(client.status() == GameClient::Status::connected);
        character = client.character_id();
        const int64_t start = client.player_status().credits;
        CHECK(start > 0);

        // Credits go in and come out.
        client.service(net::ServiceRequest::bank_deposit_credits, "", 200, 0);
        run(0.3);
        CHECK(client.bank_credits() == 200 && client.player_status().credits == start - 200);
        client.service(net::ServiceRequest::bank_deposit_credits, "", 1'000'000'000, 0);  // all there is
        run(0.3);
        CHECK(client.bank_credits() == start && client.player_status().credits == 0);
        client.service(net::ServiceRequest::bank_withdraw_credits, "", 50, 0);
        run(0.3);
        CHECK(client.bank_credits() == start - 50 && client.player_status().credits == 50);

        // An item goes in, and the same one comes out. (Not an equipped one.)
        uint64_t spare = 0;
        const auto& loadout = client.char_loadouts().at(0);
        for (const ItemInstance& item : client.items()) {
            const ItemDef* def = catalog.find(item.def_id);
            const bool worn = item.id == loadout.primary_id || item.id == loadout.sidearm_id || item.id == loadout.secondary_id;
            if (def != nullptr && !worn && def->kind == ItemKind::clothing &&
                std::none_of(loadout.appearance_ids.begin(), loadout.appearance_ids.end(),
                             [&](const auto& slot) { return slot.second == item.id; })) {
                spare = item.id;
            }
        }
        CHECK(spare != 0);
        const size_t before = client.items().size();
        client.service(net::ServiceRequest::bank_deposit_item, "", 0, spare);
        run(0.3);
        CHECK(client.items().size() == before - 1 && client.bank_items().size() == 1);
        client.service(net::ServiceRequest::bank_deposit_item, "", 0, spare);  // gone: nothing happens
        run(0.2);
        CHECK(client.bank_items().size() == 1);

        // A clinic patches you up for a fee (nothing to patch: no charge).
        client.service(net::ServiceRequest::heal, "", 0, 0);
        run(0.2);
        CHECK(client.player_status().credits == 50);

        // A cloning terminal keeps your clone.
        client.service(net::ServiceRequest::bind, "", 0, 0);
        run(0.2);
        client.disconnect();
        run(0.2);
    }
    {
        // Kept across visits.
        SqliteCharacterStore store(db_path);
        CHECK(!store.get_extra(character, "bank_items").empty());
        CHECK(std::atoll(store.get_extra(character, "bank_credits").c_str()) > 0);
        CHECK(!store.get_extra(character, "bound").empty());
        net::LoopbackHub links;
        net::Endpoint server_ep{1, 1}, client_ep{2, 1};
        net::LoopbackLink srv_link{links, server_ep};
        net::LoopbackLink cli_link{links, client_ep};
        ServerConfig config;
        config.catalog = &catalog;
        config.professions = &book;
        config.pvp_enabled = false;
        config.spawn = {0, 0, 0};
        GameServer server(srv_link, world, config, &store);
        GameClient client(cli_link, world, ClientConfig{.server = server_ep, .name = "Banker"});
        double now = 0;
        for (const double end = 0.5; now < end; now += 1.0 / 60.0) {
            server.update(now);
            client.update(now);
        }
        CHECK(client.bank_items().size() == 1 && client.bank_credits() > 0);
    }
    std::filesystem::remove(db_path);
}

void test_event_steps() {
    QuestBook book;
    QuestDef def;
    def.id = "doing";
    def.title = "Doing";
    def.giver = "Someone";
    QuestStep twice;
    twice.kind = QuestStepKind::event;
    twice.target = "extract";
    twice.count = 2;
    twice.text = "Extract twice";
    QuestStep once;
    once.kind = QuestStepKind::event;
    once.target = "craft";
    once.text = "Craft";
    def.steps = {twice, once};
    book.add(def);
    std::vector<QuestProgress> journal;
    book.accept(journal, *book.find("doing"));
    CHECK(!book.on_event(journal, "craft").changed);        // not yet what it waits on
    CHECK(book.on_event(journal, "extract").changed && journal[0].step == 0 && journal[0].count == 1);
    const QuestEffects second = book.on_event(journal, "Extract");  // (any case)
    CHECK(second.changed && journal[0].step == 1);
    const QuestEffects done = book.on_event(journal, "craft");
    CHECK(done.completed.size() == 1 && journal[0].done);
}

} // namespace

int main() {
    test_event_steps();
    test_services();
    return swm::test::finish("services_test");
}
