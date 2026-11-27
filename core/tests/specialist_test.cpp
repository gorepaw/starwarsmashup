// Stage 12: the Wave 7B and 7C professions. The shipped data (abilities,
// companions, modules, slices), the rules (effects, taming, eggs, droids,
// the bazaar, slicing), the wire, and a server playing them: abilities from
// what's learned, a bio grenade, a performance, taming and
// fielding a companion, DNA and eggs, the bazaar between two players.

#include <algorithm>
#include <filesystem>
#include <vector>

#include "swm/core/client.h"
#include "swm/core/crafting.h"
#include "swm/core/heightfield.h"
#include "swm/core/items.h"
#include "swm/core/net/link.h"
#include "swm/core/net/protocol.h"
#include "swm/core/persistence.h"
#include "swm/core/profession.h"
#include "swm/core/server.h"
#include "swm/core/specialist.h"
#include "swm/core/world.h"
#include "test.h"

using namespace swm::core;
using swm::test::near;

namespace {

std::filesystem::path repo_root() {
    auto p = std::filesystem::current_path();
    while (!p.empty() && !std::filesystem::exists(p / "game" / "professions" / "specialists.ini")) {
        auto parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return p;
}

bool has(const std::vector<std::string>& list, std::string_view id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

void test_shipped_data(const SpecialistBook& book, const ProfessionBook& professions, const ItemCatalog& catalog) {
    CHECK(book.abilities().size() >= 40);
    // Every ability belongs to a real, available profession, at one of its milestones.
    for (const AbilityDef& a : book.abilities()) {
        const ProfessionDef* def = professions.find(a.profession);
        CHECK(def != nullptr && def->available);
        if (def == nullptr) continue;
        CHECK(std::any_of(def->milestones.begin(), def->milestones.end(),
                          [&](const ProfessionMilestone& m) { return m.level == a.level; }));
    }
    // All 12 Wave 7B/7C professions are live (Image Designer and Combat Medic are gone); 29 in all.
    int live = 0;
    for (const ProfessionDef& def : professions.all()) {
        if (def.wave == "7B" || def.wave == "7C") {
            CHECK(def.available);
            ++live;
        }
    }
    CHECK(live == 12);
    CHECK(professions.all().size() == 29);
    // Companions: creatures by their wildlife look, droids with bays; mounts with items in the catalog.
    const CompanionTemplate* dewback = book.companion_for_look("object/mobile/shared_dewback.iff");
    CHECK(dewback && dewback->id == "dewback" && dewback->mountable && !dewback->tactical());
    CHECK(dewback && catalog.find(dewback->mount_item) && catalog.find(dewback->mount_item)->kind == ItemKind::vehicle);
    const CompanionTemplate* rat = book.companion("womp_rat");
    CHECK(rat && rat->tactical() && rat->kind == CompanionKind::creature);
    const CompanionTemplate* b1 = book.companion("droid_battle");
    CHECK(b1 && b1->kind == CompanionKind::droid && b1->bays == 3 && !b1->materials.empty());
    for (const char* item : {"bio_creature_egg", "companion_droid", "bio_tissue_organic", "bio_stim_additive",
                             "droid_repair_kit", "doc_antidote", "doc_performance_stim"}) {
        CHECK(catalog.find(item) != nullptr);
    }
    CHECK(book.slice("slice_overclock") && book.slice("slice_featherweight")->applies == "armor");
}

void test_rules(const SpecialistBook& book, const ProfessionBook& professions) {
    // Abilities: every profession's in every loadout (no slots), each once its
    // milestone is learned (docs/PERKS.md).
    const int64_t master = professions.xp_for_level(100);
    std::vector<ProfessionProgress> progress{{"bounty_hunter", master, 100}, {"entertainer", master, 25},
                                             {"doctor", master, 0}};
    auto world = unlocked_abilities(book, professions, progress, false);
    CHECK(has(world, "ent_perform") && has(world, "ent_flourish") && !has(world, "ent_holo_emitter"));
    CHECK(has(world, "bh_bolas") && has(world, "bh_seeker_probe") && !has(world, "bh_recon_probe"));
    CHECK(!has(world, "doc_heal_dart"));  // not learned
    auto match = unlocked_abilities(book, professions, progress, true);
    CHECK(has(match, "bh_recon_probe") && has(match, "bh_hvt") && !has(match, "bh_seeker_probe") && !has(match, "ent_perform"));

    // Effects: the same id refreshes; modifiers become gear; time runs out.
    std::vector<TimedEffect> effects;
    TimedEffect rally{"sl_rally", "Rally", 1, 10, 10, {{Stat::run_speed, 0, 1.05}}};
    CHECK(add_effect(effects, rally));
    rally.remaining = 4;
    CHECK(!add_effect(effects, rally));
    CHECK(effects.size() == 1 && near(effects[0].remaining, 10));
    TimedEffect toxin{"bh_toxin_dart", "Toxin", 2, 3, 3, {}, 0, 14, DamageType::bio, effect_flag::debuff};
    CHECK(!add_effect(effects, toxin));
    CHECK(has_effect_flag(effects, effect_flag::debuff));
    const auto gear = effects_gear(effects);
    CHECK(gear && gear->slot == "effects" && gear->modifiers.size() == 1);
    CHECK(!tick_effects(effects, 3.5) && effects.size() == 1);  // the toxin ran out
    CHECK(tick_effects(effects, 7) && effects.empty());
    effects.push_back(toxin);
    CHECK(!remove_effects(effects, effect_flag::debuff) && effects.empty());

    // Taming: tiers by level, helped by a tranquilizer and wounds.
    CHECK(tameable_tier(1) == 1 && tameable_tier(25) == 2 && tameable_tier(100) == 5);
    const CompanionTemplate& rat = *book.companion("womp_rat");
    const CompanionTemplate& ronto = *book.companion("ronto");
    CHECK(tame_chance(ronto, 30, true, 0.1) == 0);
    CHECK(tame_chance(rat, 1, false, 1) > 0.3 && tame_chance(rat, 1, true, 0.2) > tame_chance(rat, 1, false, 1));

    // Eggs: traits from samples; a second creature's DNA makes it hardier.
    const DnaSample a{1, "womp_rat", 0.8};
    const DnaSample b{2, "bantha", 0.6};
    const CompanionRecord plain = engineer_egg(a, nullptr, 50, 7);
    const CompanionRecord spliced = engineer_egg(a, &b, 50, 7);
    CHECK(plain.template_id == "womp_rat" && plain.origin == "hatched" && plain.quality > 0.8);
    CHECK(spliced.health > plain.health && spliced.quality < plain.quality);
    CHECK(dna_quality(100, 1) > dna_quality(1, 1) && dna_quality(1, 0) >= 0.2);

    // Records survive saving, names with separators too.
    CompanionRecord r{42, "droid_battle", "Rex|One, 100%", "assembled", 0.7, 1.1, 1.2, 1, 99,
                      {"dmod_blaster", "dmod_armor_plating"}, true};
    const auto back = parse_companion(format_companion(r));
    CHECK(back && back->name == r.name && back->modules == r.modules && back->mount && near(back->damage, 1.2, 1e-3));
    CHECK(parse_samples(format_samples(std::vector<DnaSample>{a, b})).size() == 2);

    // Droid modules: bays, levels, one primary function.
    const CompanionTemplate& b1 = *book.companion("droid_battle");
    const std::vector<std::string> fit{"dmod_blaster", "dmod_armor_plating"};
    CHECK(check_droid_modules(book, b1, fit, 50).empty());
    const std::vector<std::string> two_primaries{"dmod_blaster", "dmod_repeater"};
    CHECK(!check_droid_modules(book, b1, two_primaries, 100).empty());
    const std::vector<std::string> too_many{"dmod_blaster", "dmod_armor_plating", "dmod_servo_motors", "dmod_scanner"};
    CHECK(!check_droid_modules(book, b1, too_many, 100).empty());
    const std::vector<std::string> above{"dmod_repeater"};
    CHECK(!check_droid_modules(book, b1, above, 30).empty());
    CompanionRecord droid{1, "droid_battle", "B1", "assembled", 0.5, 1, 1, 1, 0, {"dmod_blaster", "dmod_bacta_sprayer"}};
    const CompanionStats st = companion_stats(book, b1, droid, 100);
    CHECK(near(st.max_health, b1.health * 1.5) && st.role == CompanionRole::support);
    CHECK(near(st.damage, b1.damage * 1.25 * 1.5));

    // The bazaar: shop slots and fees by Merchant level; price trends.
    CHECK(bazaar_listing_limit(0) == 5 && bazaar_listing_limit(100) == 25);
    CHECK(bazaar_fee(1000, 0) == 60 && bazaar_fee(1000, 100) == 20 && bazaar_fee(1, 0) == 1);
    const std::vector<BazaarSale> sales{{"x", 100, 1}, {"y", 5, 2}, {"x", 300, 3}};
    const PriceStats ps = price_stats(sales, "x");
    CHECK(ps.count == 2 && ps.low == 100 && ps.high == 300 && ps.average == 200 && ps.last == 300);
    BazaarListing l{7, 3, "Seller", "Seller's Shop", {11, 3, "wp_rifle_e11", ItemGrade::fine, "My \"Rifle\"|", "{\"slice\":\"slice_overclock\"}", 1},
                    500, 123};
    const auto listings = parse_listings(format_listings(std::vector<BazaarListing>{l}));
    CHECK(listings.size() == 1 && listings[0] == l);
    CHECK(listing_matches(l, "E-11 Carbine", "e11 shop") && !listing_matches(l, "E-11 Carbine", "bowcaster"));

    // Slicing: on an item's color JSON, one at a time; applied to what it resolves to.
    ItemInstance item{1, 1, "wp_rifle_e11", ItemGrade::standard, "", "{\"primary\":3}"};
    set_item_slice(item, "slice_overclock");
    CHECK(item_slice(item) == "slice_overclock" && item.color_json.find("\"primary\":3") != std::string::npos);
    set_item_slice(item, "slice_hot_shot");
    CHECK(item_slice(item) == "slice_hot_shot" && item.color_json.find("overclock") == std::string::npos);
    set_item_slice(item, "");
    CHECK(item_slice(item).empty() && item.color_json == "{\"primary\":3}");
    WeaponDef w;
    const double delay = w.shot_delay, heat = w.heat_per_shot;
    apply_slice(w, *book.slice("slice_overclock"));
    CHECK(near(w.shot_delay, delay / 1.12) && near(w.heat_per_shot, heat * 1.2));

    // Inspiration: 5 minutes watched is an hour; a band and a flourish build
    // faster; a novice's is 60% as strong; saved as text; shown as an effect.
    CHECK(near(inspiration_gain(300, 1, false, false), 3600));
    CHECK(near(inspiration_gain(5, 3, false, false), 90) && near(inspiration_gain(5, 3, true, true), 240));
    CHECK(near(inspiration_strength(1), 0.604) && near(inspiration_strength(100), 1.0));
    CHECK(book.inspirations().size() == 7);
    const InspirationDef* wind = book.inspiration("insp_second_wind");
    const InspirationDef* studious = book.inspiration("insp_studious");
    CHECK(wind && wind->kind == "combat" && wind->level == 1);
    CHECK(studious && studious->kind == "progression" && studious->modifiers[0].stat == Stat::xp_gain);
    const Inspiration half{"insp_second_wind", 600, 0.5};
    CHECK(parse_inspiration(format_inspiration(half)) == half);
    CHECK(!parse_inspiration("") && format_inspiration(Inspiration{}).empty());
    if (wind != nullptr) {
        const TimedEffect e = inspiration_effect(*wind, half);
        CHECK(e.id == "inspiration" && near(e.remaining, 600) && (e.flags & effect_flag::inspired) != 0);
        CHECK(e.modifiers.size() == 1 && near(e.modifiers[0].multiply, 1.04));
    }
}

void test_wire() {
    net::SpecialistSync sync;
    sync.revision = 9;
    sync.abilities = {{"bh_bolas", 3.5f}};
    sync.effects = {{"inspired", "Inspired", 12, 20, effect_flag::inspired}};
    sync.companions = {{3, "womp_rat", "Biter", "tamed", 0.5, 1, 1, 1, 4, {}, false}};
    sync.active_companion = 3;
    sync.companion_entity = 77;
    sync.samples = {{1, "dewback", 0.75}};
    Contract c;
    c.id = 5;
    c.kind = ContractKind::track;
    c.title = "Track: a worrt";
    c.reward = 120;
    Contract order;
    order.id = 6;
    order.kind = ContractKind::craft_order;
    order.item = "wp_medpack_stim";
    order.needed = 3;
    sync.contracts = {c, order};
    sync.prices = {{"wp_rifle_e11", PriceStats{2, 100, 300, 200, 300}}};
    sync.performing = true;
    sync.inspiration_choice = "insp_fleet";
    const auto decoded = std::get<net::SpecialistSync>(net::decode(net::encode(sync)));
    CHECK(decoded.revision == 9 && decoded.abilities == sync.abilities && decoded.effects == sync.effects);
    CHECK(decoded.companions.size() == 1 && decoded.companions[0].name == "Biter");
    CHECK(decoded.contracts.size() == 2 && decoded.contracts[0].kind == ContractKind::track && decoded.contracts[0].reward == 120);
    CHECK(decoded.contracts.size() == 2 && decoded.contracts[1] == order);
    CHECK(decoded.prices.size() == 1 && decoded.prices[0].second.average == 200);
    CHECK(decoded.performing && decoded.inspiration_choice == "insp_fleet");

    net::SpecialistRequest request;
    request.action = net::SpecialistRequest::bazaar_list;
    request.item_id = 12;
    request.amount = 999;
    request.text = std::string(600, 'x');
    const auto req = std::get<net::SpecialistRequest>(net::decode(net::encode(request)));
    CHECK(req.action == request.action && req.item_id == 12 && req.amount == 999 && req.text.size() == 600);

    net::Snapshot snap;
    snap.field.marks = {{4, 2.5f, net::FieldMark::bounty}};
    snap.field.cloaked = {6};
    snap.field.companions = {{8, 4}};
    snap.field.deployables = {{1, uint8_t(DeployKind::spawn_beacon), 4, true, {1, 2, 3}, 3, 80}};
    snap.field.hazards = {{2, "bio", {0, 0, 0}, 5, 9}};
    snap.field.flair = {{4u, uint8_t(1)}};
    const auto back = std::get<net::Snapshot>(net::decode(net::encode(snap)));
    CHECK(back.field == snap.field);
}

void test_server(const std::filesystem::path& root, const SpecialistBook& book, const ProfessionBook& professions,
                 const ItemCatalog& catalog) {
    CraftingBook crafting;
    crafting.load_resources(root / "game" / "crafting" / "resources.ini");
    crafting.load_schematics(root / "game" / "crafting" / "schematics.ini");
    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    MemoryCharacterStore store;
    // A seasoned specialist and a buyer.
    const int64_t master = professions.xp_for_level(100);
    const CharacterRecord vet = store.create("Veteran", {0, 0, 0}, 0);
    std::vector<ProfessionProgress> progress;
    for (const char* id : {"bounty_hunter", "creature_handler", "entertainer", "bio_engineer", "merchant", "smuggler"}) {
        progress.push_back({id, master, 0});
        store.set_extra(vet.id, std::string("trained:") + id, "100");
    }
    store.save_professions(vet.id, progress);
    store.create("Buyer", {3, 0, 3}, 0);

    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink vet_link{links, {2, 1}};
    net::LoopbackLink buyer_link{links, {3, 1}};
    ServerConfig config;
    config.catalog = &catalog;
    config.professions = &professions;
    config.specialists = &book;
    config.crafting = &crafting;
    config.pvp_enabled = false;
    config.spawn = {0, 0, 0};
    GameServer server(srv_link, world, config, &store);
    server.set_looks({"object/mobile/shared_womp_rat.iff"});
    server.set_vendors({{"Bazaar Terminal", "bazaar", {0, 0, 0}, 6, ""}});
    GameServer::NpcCombatSpec rat_spec;
    rat_spec.actor_kind = 2;
    rat_spec.max_health = 135;
    const uint32_t rat = server.add_combat_npc("Womp Rat", {4, 0, 0}, 0, 1, rat_spec);
    const uint32_t target = server.add_combat_npc("Womp Rat", {-12, 0, 0}, 0, 1, rat_spec);
    GameClient vet_client(vet_link, world, ClientConfig{.server = server_ep, .name = "Veteran"});
    GameClient buyer(buyer_link, world, ClientConfig{.server = server_ep, .name = "Buyer"});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            vet_client.update(now);
            buyer.update(now);
        }
    };
    run(0.6);
    CHECK(vet_client.status() == GameClient::Status::connected && buyer.status() == GameClient::Status::connected);
    const uint32_t me = vet_client.entity_id();
    const auto use = [&](const char* ability, uint32_t at_entity, Vec3 at) {
        net::SpecialistRequest r;
        r.action = net::SpecialistRequest::use_ability;
        r.id = ability;
        r.target = at_entity;
        r.position = at;
        vet_client.specialist_request(r);
    };
    const auto act = [&](GameClient& who, uint8_t action, int64_t amount, uint64_t item = 0, std::string text = {}) {
        net::SpecialistRequest r;
        r.action = action;
        r.amount = amount;
        r.item_id = item;
        r.text = std::move(text);
        who.specialist_request(r);
    };

    // What's learned is there from the start, in every loadout (no slots).
    run(1.0);
    const auto& kit = vet_client.specialist().abilities;
    const auto in_kit = [&](std::string_view id) {
        return std::any_of(kit.begin(), kit.end(), [&](const net::AbilityStatus& a) { return a.id == id; });
    };
    CHECK(in_kit("ent_perform") && in_kit("bio_sample") && in_kit("bh_toxin_dart") && in_kit("ch_tame") &&
          in_kit("ch_tranq_dart"));
    CHECK(!in_kit("doc_heal_dart") && !in_kit("doc_cleanse"));  // never learned

    // A toxin dart: it poisons the creature, not us.
    use("bh_toxin_dart", target, {-12, 0, 0});
    run(2.0);
    const auto hurt = server.npc_info(target);
    CHECK(hurt && hurt->health < 135 && hurt->health > 50);
    CHECK(server.health_of(me) >= 399);
    const auto* mine = server.professions_of(me);
    CHECK(mine && profession_xp(*mine, "bounty_hunter") > master);

    // A performance: the audience (the buyer, near) builds Inspiration, a
    // master's at full strength; the performer doesn't, but is tipped.
    CHECK(performance_tips(1, 0) == 1 && performance_tips(50, 0) == 2 && performance_tips(100, 12) == 11);
    const int64_t before_tips = vet_client.player_status().credits;
    use("ent_perform", 0, {});
    run(1.5);
    const SpecialistState* spec = server.specialist_state(me);
    const SpecialistState* fan = server.specialist_state(buyer.entity_id());
    CHECK(spec && spec->performing && spec->inspiration.id.empty());
    CHECK(fan && fan->inspiration.id == "insp_second_wind" && fan->inspiration.remaining > 50 &&
          near(fan->inspiration.strength, 1.0));
    CHECK(fan && has_effect_flag(fan->effects, effect_flag::inspired));
    CHECK(std::any_of(vet_client.field().performers.begin(), vet_client.field().performers.end(),
                      [&](uint32_t e) { return e == me; }));
    // The buyer chooses Composure (a Band Leader's, which a master offers): it's built instead.
    net::SpecialistRequest choose;
    choose.action = net::SpecialistRequest::choose_inspiration;
    choose.id = "insp_composure";
    buyer.specialist_request(choose);
    run(5.5);
    CHECK(fan && fan->inspiration.id == "insp_composure" && fan->inspiration.remaining > 50 &&
          fan->inspiration.remaining < 70);
    CHECK(buyer.specialist().inspiration_choice == "insp_composure");
    CHECK(vet_client.player_status().credits == before_tips + 2 * performance_tips(100, 1));  // two pulses, one watching
    run(0.6);  // its cooldown
    use("ent_perform", 0, {});
    run(2.1);
    CHECK(spec && !spec->performing);

    // DNA from the wounded rat, then taming the other: a tranquilizer, a bonding.
    use("bio_sample", rat, {});
    run(3.5);
    CHECK(spec && spec->samples.size() == 1 && spec->samples[0].template_id == "womp_rat");
    use("ch_tranq_dart", rat, {});
    run(0.5);
    for (int attempt = 0; attempt < 5 && spec && spec->companions.empty(); ++attempt) {
        use("ch_tame", rat, {});
        run(11);
    }
    CHECK(spec && spec->companions.size() == 1 && spec->companions[0].template_id == "womp_rat");
    const auto tamed = server.npc_info(rat);
    CHECK(tamed && tamed->hidden);

    // Fielded: one companion, an NPC at our side; our own shots don't hurt it.
    act(vet_client, net::SpecialistRequest::companion_summon, int64_t(spec ? spec->companions[0].id : 0));
    run(0.5);
    const auto companion = server.companion_of(me);
    CHECK(companion.has_value());
    const auto pet = companion ? server.npc_info(companion->entity) : std::nullopt;
    CHECK(pet && pet->actor_kind == 4 && pet->life == Life::alive);
    CHECK(vet_client.specialist().active_companion == (spec ? spec->companions[0].id : 0));
    CHECK(std::any_of(vet_client.field().companions.begin(), vet_client.field().companions.end(),
                      [&](const auto& c) { return c.second == me; }));
    act(vet_client, net::SpecialistRequest::companion_dismiss, 0);
    run(0.3);
    CHECK(!server.companion_of(me));

    // An egg from the sample; hatched into the datapad.
    act(vet_client, net::SpecialistRequest::engineer_egg, int64_t(spec->samples[0].id));
    run(0.5);
    const auto egg = std::find_if(vet_client.items().begin(), vet_client.items().end(),
                                  [](const ItemInstance& i) { return i.def_id == "bio_creature_egg"; });
    CHECK(egg != vet_client.items().end() && spec->samples.empty());
    if (egg != vet_client.items().end()) {
        act(vet_client, net::SpecialistRequest::hatch_egg, 0, egg->id);
        run(0.5);
        CHECK(spec->companions.size() == 2 && spec->companions[1].origin == "hatched");
    }

    // The bazaar: the veteran lists a spare item; the buyer buys it.
    server.award_credits(buyer.entity_id(), 5000);
    run(0.2);
    const int64_t buyer_before = buyer.player_status().credits;
    const auto spare = std::find_if(vet_client.items().begin(), vet_client.items().end(), [](const ItemInstance& i) {
        return i.def_id == "bio_creature_egg" || i.def_id.starts_with("supply");
    });
    const ItemInstance* listed = nullptr;
    for (const ItemInstance& i : vet_client.items()) {
        bool equipped = false;
        for (const CharacterLoadout& cl : vet_client.char_loadouts()) {
            equipped = equipped || cl.primary_id == i.id || cl.sidearm_id == i.id || cl.secondary_id == i.id;
            for (const auto& [slot, id] : cl.gear_ids) equipped = equipped || id == i.id;
        }
        if (!equipped) {
            listed = &i;
            break;
        }
    }
    (void)spare;
    CHECK(listed != nullptr);
    if (listed != nullptr) {
        const std::string def_id = listed->def_id;
        const int64_t seller_before = vet_client.player_status().credits;
        act(vet_client, net::SpecialistRequest::bazaar_list, 1000, listed->id);
        run(0.5);
        CHECK(server.bazaar_listings().size() == 1);
        CHECK(vet_client.specialist().my_listings.size() == 1);
        act(buyer, net::SpecialistRequest::bazaar_browse, 0, 0, "");
        run(0.5);
        CHECK(buyer.specialist().bazaar.size() == 1);
        const uint64_t id = buyer.specialist().bazaar.empty() ? 0 : buyer.specialist().bazaar[0].id;
        act(buyer, net::SpecialistRequest::bazaar_buy, int64_t(id));
        run(0.5);
        CHECK(server.bazaar_listings().empty());
        CHECK(buyer.player_status().credits == buyer_before - 1000);
        CHECK(vet_client.player_status().credits == seller_before + 1000 - bazaar_fee(1000, 100));
        CHECK(std::any_of(buyer.items().begin(), buyer.items().end(), [&](const ItemInstance& i) { return i.def_id == def_id; }));
        CHECK(!store.get_extra(0, "bazaar_sales").empty());
    }

    // Opting in to bounty hunters is saved with the character.
    act(buyer, net::SpecialistRequest::bounty_flag, 1);
    run(0.3);
    CHECK(buyer.specialist().bounty_flag);
    vet_client.disconnect();
    buyer.disconnect();
    run(0.3);
    const auto saved = store.find_by_name("Veteran");
    CHECK(saved && !store.get_extra(saved->id, "datapad").empty());
    const auto saved_buyer = store.find_by_name("Buyer");
    CHECK(saved_buyer && store.get_extra(saved_buyer->id, "bounty_flag") == "1");
    // Its Inspiration is kept, counting down only while it played.
    const auto kept = saved_buyer ? parse_inspiration(store.get_extra(saved_buyer->id, "inspiration")) : std::nullopt;
    CHECK(kept && kept->id == "insp_composure" && kept->remaining > 20 && kept->remaining < 70);
}

// Crafting orders at an artisan terminal (docs/PROFESSIONS.md): a Doctor
// takes one, and standing at the terminal with what it asks for (not in a
// loadout), hands it in for credits and Doctor experience.
void test_craft_orders(const std::filesystem::path& root, const SpecialistBook& book,
                       const ProfessionBook& professions, const ItemCatalog& catalog) {
    CraftingBook crafting;
    crafting.load_resources(root / "game" / "crafting" / "resources.ini");
    crafting.load_schematics(root / "game" / "crafting" / "schematics.ini");
    Heightfield ground{101, 2.0, -100, -100, std::vector<float>(101 * 101, 0.0f)};
    World world{ground};
    MemoryCharacterStore store;
    const CharacterRecord medic = store.create("Medic", {0, 0, 0}, 0);
    const int64_t xp = professions.xp_for_level(60);
    store.save_professions(medic.id, std::vector<ProfessionProgress>{{"doctor", xp, 0}});
    store.set_extra(medic.id, "trained:doctor", "50");
    // Three of everything a Doctor makes, whatever the order asks for.
    for (const SchematicDef& s : crafting.schematics()) {
        if (s.profession != "doctor") continue;
        for (int i = 0; i < 3; ++i) store.grant_item(ItemInstance{0, medic.id, s.output_item});
    }
    const size_t held_before = store.list_items(medic.id).size();

    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink link{links, {2, 1}};
    ServerConfig config;
    config.catalog = &catalog;
    config.professions = &professions;
    config.specialists = &book;
    config.crafting = &crafting;
    config.spawn = {0, 0, 0};
    GameServer server(srv_link, world, config, &store);
    server.set_quests(nullptr, {}, {{"Artisan Terminal", {2, 0, 0}, 3, "artisan"}});
    GameClient client(link, world, ClientConfig{.server = server_ep, .name = "Medic"});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            client.update(now);
        }
    };
    const auto act = [&](uint8_t action, int64_t amount, uint32_t target = 0) {
        net::SpecialistRequest r;
        r.action = action;
        r.amount = amount;
        r.target = target;
        client.specialist_request(r);
    };
    run(1.0);
    CHECK(client.status() == GameClient::Status::connected);
    act(net::SpecialistRequest::contracts, 0, 0);
    run(0.5);
    const auto& offers = client.specialist().offers;
    const auto order = std::find_if(offers.begin(), offers.end(),
                                    [](const Contract& c) { return c.kind == ContractKind::craft_order; });
    CHECK(order != offers.end());
    if (order == offers.end()) return;
    CHECK(order->profession == "doctor" && order->needed >= 1 && order->needed <= 3 && order->reward > 0);
    const int needed = int(order->needed);
    const int64_t credits = client.player_status().credits;
    act(net::SpecialistRequest::contract_accept, int64_t(order->id));
    run(1.0);
    // At the terminal, holding them: handed in at once.
    CHECK(client.specialist().contracts.empty());
    CHECK(client.player_status().credits > credits);
    CHECK(store.list_items(medic.id).size() == held_before - size_t(needed));
    const auto* mine = server.professions_of(client.entity_id());
    CHECK(mine && profession_xp(*mine, "doctor") > xp);
}

// In a match: a loadout from a hub brings its kit and Companion Slot; a
// cloak hides you from the other side (they aren't even sent you); a squad
// beacon is somewhere to spawn; a big creature stays home.
void test_match(const SpecialistBook& book, const ProfessionBook& professions) {
    Battlefield field;
    field.name = "Test Field";
    field.teams[0] = {"Rebellion", {Loadout{}}};
    field.teams[1] = {"Empire", {Loadout{}}};
    field.min_x = -100;
    field.max_x = 100;
    field.min_z = -60;
    field.max_z = 60;
    field.posts = {{.name = "West", .position = {-40, 0, 0}, .owner = 0},
                   {.name = "Middle", .position = {0, 0, 0}},
                   {.name = "East", .position = {40, 0, 0}, .owner = 1}};
    field.tickets = 50;
    field.warmup = 0.5;
    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink a_link{links, {2, 1}};
    net::LoopbackLink b_link{links, {3, 1}};
    ServerConfig config;
    config.battlefield = field;
    config.professions = &professions;
    config.specialists = &book;
    config.respawn_delay = 0.5;
    GameServer server(srv_link, world, config);
    GameClient a(a_link, world, ClientConfig{.server = server_ep, .name = "Leader", .battlefield = field});
    GameClient b(b_link, world, ClientConfig{.server = server_ep, .name = "Enemy", .battlefield = field});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            a.update(now);
            b.update(now);
        }
    };
    run(0.6);
    CHECK(a.status() == GameClient::Status::connected && b.status() == GameClient::Status::connected);
    const uint32_t ea = a.entity_id(), eb = b.entity_id();
    // What a hub hands over: the kit, the levels, a womp rat in the Companion Slot.
    Loadout kit;
    kit.abilities = {"rg_cloak", "sl_beacon", "sl_rally", "rg_track"};
    kit.specialist_levels = {{"ranger", 100}, {"squad_leader", 100}, {"creature_handler", 60}};
    kit.companion = format_companion({7, "womp_rat", "Biter", "tamed", 0.5, 1, 1, 1, 0, {}, false});
    kit.inspiration = format_inspiration({"insp_endurance", 600, 1.0});  // built in the hub
    server.set_player_loadouts(ea, {kit});
    a.request_spawn(0, 0);
    b.request_spawn(0, 2);
    run(1.5);
    CHECK(server.alive(ea) && server.alive(eb));
    run(0.6);  // the kit refreshes
    const auto in_kit = [&](std::string_view id) {
        return std::any_of(a.specialist().abilities.begin(), a.specialist().abilities.end(),
                           [&](const net::AbilityStatus& s) { return s.id == id; });
    };
    CHECK(in_kit("rg_cloak") && in_kit("sl_beacon") && !in_kit("rg_track"));  // tracking is world-only
    const auto inspired = [&] {
        const SpecialistState* s = server.specialist_state(ea);
        return s != nullptr && s->inspiration.id == "insp_endurance" && has_effect_flag(s->effects, effect_flag::inspired);
    };
    CHECK(inspired());  // the hub's Inspiration came along
    const auto sees = [&](GameClient& c, uint32_t e) {
        return std::any_of(c.remotes().begin(), c.remotes().end(), [&](const GameClient::Remote& r) { return r.id == e; });
    };
    CHECK(sees(b, ea));
    const auto use = [&](const char* id, Vec3 at) {
        net::SpecialistRequest r;
        r.action = net::SpecialistRequest::use_ability;
        r.id = id;
        r.position = at;
        a.specialist_request(r);
    };
    use("rg_cloak", {});
    run(0.5);
    CHECK(server.hidden_from(eb, ea) && !server.hidden_from(ea, eb));
    CHECK(!sees(b, ea));  // not sent at all
    // The beacon, and the womp rat from the Companion Slot.
    const MoveState* at = server.state_of(ea);
    const Vec3 beacon = at ? at->position + Vec3{4, 0, 0} : Vec3{};
    use("sl_beacon", beacon);
    net::SpecialistRequest summon;
    summon.action = net::SpecialistRequest::companion_summon;
    a.specialist_request(summon);
    run(0.5);
    CHECK(server.deployable_count() == 1);
    const auto pet = server.companion_of(ea);
    CHECK(pet && pet->template_id == "womp_rat");
    const auto beacon_seen = [&](GameClient& c, bool friendly) {
        return std::any_of(c.field().deployables.begin(), c.field().deployables.end(), [&](const net::FieldDeployable& d) {
            return d.kind == uint8_t(DeployKind::spawn_beacon) && d.friendly == friendly;
        });
    };
    CHECK(beacon_seen(a, true) && beacon_seen(b, false));
    // Killed, and back at the beacon.
    server.npc_melee(eb, ea, 5000, DamageType::melee);
    run(1.5);
    CHECK(!server.alive(ea));
    CHECK(!server.companion_of(ea));  // a companion leaves with its owner
    a.request_spawn(0, beacon_spawn_post);
    run(1.0);
    const MoveState* back = server.state_of(ea);
    CHECK(server.alive(ea) && back && std::hypot(back->position.x - beacon.x, back->position.z - beacon.z) < 3);
    CHECK(inspired());  // a death in a match doesn't end it
    // A bantha is too big for a battle.
    kit.companion = format_companion({8, "bantha", "Big", "tamed", 0.5, 1, 1, 1, 0, {}, false});
    server.set_player_loadouts(ea, {kit});
    run(31);  // the womp rat's cooldown, had it fallen (it was only sent home)
    a.specialist_request(summon);
    run(0.5);
    CHECK(!server.companion_of(ea));
}

} // namespace

int main() {
    const auto root = repo_root();
    ProfessionBook professions;
    professions.load(root / "game" / "professions" / "professions.ini");
    SpecialistBook book;
    book.load(root / "game" / "professions" / "specialists.ini");
    ItemCatalog catalog;
    catalog.load(root / "game" / "items" / "swg_catalog.ini");
    test_shipped_data(book, professions, catalog);
    test_rules(book, professions);
    test_wire();
    test_server(root, book, professions, catalog);
    test_craft_orders(root, book, professions, catalog);
    test_match(book, professions);
    return swm::test::finish("specialist_test");
}
