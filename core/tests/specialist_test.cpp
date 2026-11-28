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
#include "swm/core/vehicle.h"
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
    CHECK(book.abilities().size() >= 30);
    // Every ability belongs to a real, available profession: at one of its
    // milestones, or taught by one of its perks (at the perk's level).
    for (const AbilityDef& a : book.abilities()) {
        const ProfessionDef* def = professions.find(a.profession);
        CHECK(def != nullptr && def->available);
        if (def == nullptr) continue;
        if (def->chooses()) {
            CHECK(std::any_of(def->perks.begin(), def->perks.end(), [&](const ProfessionPerkDef& p) {
                return p.level == a.level && std::find(p.abilities.begin(), p.abilities.end(), a.id) != p.abilities.end();
            }));
        } else {
            CHECK(std::any_of(def->milestones.begin(), def->milestones.end(),
                              [&](const ProfessionMilestone& m) { return m.level == a.level; }));
        }
    }
    // ...and every perk's ability is one.
    for (const ProfessionDef& def : professions.all()) {
        for (const ProfessionPerkDef& p : def.perks) {
            for (const std::string& id : p.abilities) CHECK(book.ability(id) != nullptr);
        }
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
    std::vector<ProfessionProgress> progress{
        {"bounty_hunter", master, 0, 0, {"bh_perk_seeker", "bh_perk_toxin_dart"}},
        {"entertainer", master, 0, 0, {"ent_perk_dancer", "ent_perk_flourish"}},
        {"doctor", master, 0},
        {"squad_leader", master, 0, 0, {"sl_perk_steady_rally", "sl_perk_shields_up", "sl_perk_supply"}}};
    auto world = unlocked_abilities(book, professions, progress, false);
    CHECK(has(world, "ent_perform") && has(world, "ent_flourish") && !has(world, "ent_holo_emitter"));
    // Perks teach their abilities: this Squad Leader's order and supply drop
    // (Shields Up is no button: it rides on the order), not the other rally.
    CHECK(has(world, "sl_steady_rally") && has(world, "sl_supply") && !has(world, "sl_rally"));
    CHECK(!has(world, "sl_shields_up") && !has(world, "sl_strike"));
    // Two buttons at most, whatever a character learns in a profession.
    for (const ProfessionDef& def : professions.all()) {
        if (!def.chooses() || def.category == ProfessionCategory::weapon || def.category == ProfessionCategory::armor) continue;
        std::vector<int> tiers_with_buttons;
        for (const ProfessionPerkDef& perk : def.perks) {
            if (!perk.abilities.empty() && std::find(tiers_with_buttons.begin(), tiers_with_buttons.end(), perk.level) ==
                                               tiers_with_buttons.end()) {
                tiers_with_buttons.push_back(perk.level);
            }
        }
        CHECK(tiers_with_buttons.size() <= 2);
    }
    // A Bounty Hunter's two buttons: the probe (the world's or a battle's) and the dart.
    CHECK(has(world, "bh_seeker_probe") && !has(world, "bh_recon_probe") && has(world, "bh_toxin_dart"));
    CHECK(!has(world, "bh_hvt_world") && !has(world, "bh_wrist_rocket"));
    CHECK(!has(world, "doc_heal_dart"));  // not learned
    auto match = unlocked_abilities(book, professions, progress, true);
    CHECK(has(match, "bh_recon_probe") && !has(match, "bh_hvt") && !has(match, "bh_seeker_probe") && !has(match, "ent_perform"));

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
    // A Master Geneticist's better; Hybrid Vigor's spliced egg hardier and harder-hitting still.
    CHECK(engineer_egg(a, nullptr, 50, 7, 0.1).quality > plain.quality);
    const CompanionRecord vigor = engineer_egg(a, &b, 50, 7, 0, 0.1);
    CHECK(vigor.health > spliced.health && vigor.damage > spliced.damage);
    CompanionRecord bred = plain;
    bred.tactical = true;
    const auto bred_back = parse_companion(format_companion(bred));
    CHECK(bred_back && bred_back->tactical && !parse_companion(format_companion(plain))->tactical);

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
    // Anyone's 10 listings, more with Merchant levels; a Trader's 2 points off, a Tycoon's half.
    CHECK(bazaar_listing_limit(0) == 10 && bazaar_listing_limit(100) == 30);
    CHECK(bazaar_fee(1000, 0) == 60 && bazaar_fee(1000, 100) == 20 && bazaar_fee(1, 0) == 1);
    CHECK(bazaar_fee(1000, 0, 0.02) == 40 && bazaar_fee(1000, 0, 0, true) == 30 && bazaar_fee(1000, 100, 0.02, true) == 1);
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
    set_item_slice(item, "slice_long_coil", 1.5);
    CHECK(item_slice(item) == "slice_long_coil" && std::abs(item_slice_power(item) - 1.5) < 1e-9);
    set_item_slice(item, "slice_long_coil");
    CHECK(item_slice_power(item) == 1.0);
    set_item_slice(item, "");
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
    const std::vector<PostedBounty> posted{{7, "Poster", "Mark", 500, 1234}, {9, "Other", "Mark Two", 2500, 99}};
    CHECK(parse_posted_bounties(format_posted_bounties(posted)) == posted);
    CHECK(parse_posted_bounties("junk\n1\tA\tB\tnope\t3\n").empty());
    CHECK(!bounty_mark_name(0.0, 0.99).empty() && bounty_mark_name(0.5, 0.5) == bounty_mark_name(0.5, 0.5));
    if (wind != nullptr) {
        const TimedEffect e = inspiration_effect(*wind, half);
        CHECK(e.id == "inspiration" && near(e.remaining, 600) && (e.flags & effect_flag::inspired) != 0);
        CHECK(e.modifiers.size() == 1 && near(e.modifiers[0].multiply, 1.04));
    }
}

void test_wire() {
    net::SpecialistSync sync;
    sync.revision = 9;
    sync.abilities = {{"bh_toxin_dart", 3.5f}};
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
        if (std::string(id) != "entertainer" && std::string(id) != "bounty_hunter") {
            store.set_extra(vet.id, std::string("trained:") + id, "100");
        }
    }
    // Bounty Hunter teaches perks: a probe, a dart that holds two charges.
    store.set_extra(vet.id, "perks:bounty_hunter", "bh_perk_seeker,bh_perk_toxin_dart,bh_perk_twin_charges");
    // Entertainer teaches perks: a musician who leads bands and holo-records.
    store.set_extra(vet.id, "perks:entertainer",
                    "ent_perk_musician,ent_perk_flourish,ent_perk_holo_emitter,ent_perk_band_leader,ent_perk_virtuoso");
    // Bio-Engineer's licence: the sampling button.
    store.set_extra(vet.id, "perks:bio_engineer", "bio_perk_sampling");
    // Creature Handler's: the taming button.
    store.set_extra(vet.id, "perks:creature_handler", "ch_perk_tamer,ch_perk_steady");
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
    CHECK(in_kit("ent_perform") && in_kit("bio_sample") && in_kit("bh_toxin_dart") && in_kit("ch_tame"));
    CHECK(!in_kit("ch_tranq_dart"));  // (folded into Steady Hand)
    CHECK(!in_kit("doc_heal_dart") && !in_kit("doc_cleanse"));  // never learned

    // A toxin dart: it poisons the creature, not us.
    use("bh_toxin_dart", target, {-12, 0, 0});
    run(2.0);
    const auto hurt = server.npc_info(target);
    CHECK(hurt && hurt->health < 135 && hurt->health > 50);
    CHECK(server.health_of(me) >= 399);
    const auto* mine = server.professions_of(me);
    CHECK(mine && profession_xp(*mine, "bounty_hunter") > master);
    // Twin Charges: a second dart is ready (the cooldown shows none), a third isn't.
    const auto dart_cooldown = [&] {
        for (const net::AbilityStatus& a : vet_client.specialist().abilities) {
            if (a.id == "bh_toxin_dart") return double(a.cooldown);
        }
        return -1.0;
    };
    CHECK(dart_cooldown() == 0.0);
    use("bh_toxin_dart", target, {-12, 0, 0});
    run(0.5);
    CHECK(dart_cooldown() > 10.0);
    // A loadout carries its perks' switches into a match.
    const std::vector<Loadout> carried = server.build_player_loadouts(me);
    CHECK(!carried.empty() && std::find(carried[0].grants.begin(), carried[0].grants.end(),
                                        "bounty_hunter:twin_charges") != carried[0].grants.end());

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

    // DNA from the wounded rat, then taming it: Steady Hand drugs it as the bonding begins.
    use("bio_sample", rat, {});
    run(3.5);
    CHECK(spec && spec->samples.size() == 1 && spec->samples[0].template_id == "womp_rat");
    for (int attempt = 0; attempt < 5 && spec && spec->companions.empty(); ++attempt) {
        use("ch_tame", rat, {});
        run(3.4);  // (Steady Hand's 3 s bonding)
        if (spec && spec->companions.empty()) run(7.5);  // its cooldown
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
        // The player's listing, among the general store's (the basics).
        uint64_t id = 0;
        size_t players = 0;
        for (const BazaarListing& l : buyer.specialist().bazaar) {
            if (l.id >= general_store_listing_base) {
                CHECK(l.seller_name == general_store_seller);
                continue;
            }
            id = l.id;
            ++players;
        }
        CHECK(players == 1);
        act(buyer, net::SpecialistRequest::bazaar_buy, int64_t(id));
        run(0.5);
        CHECK(server.bazaar_listings().empty());
        CHECK(buyer.player_status().credits == buyer_before - 1000);
        CHECK(vet_client.player_status().credits == seller_before + 1000 - bazaar_fee(1000, 100));
        CHECK(std::any_of(buyer.items().begin(), buyer.items().end(), [&](const ItemInstance& i) { return i.def_id == def_id; }));
        CHECK(!store.get_extra(0, "bazaar_sales").empty());
    }

    // The general store: a basic at its price, never sold out, nobody paid,
    // and not a sale for the price trends.
    if (const ItemDef* d18 = catalog.find("wp_pistol_d18"); d18 != nullptr && d18->basic) {
        act(buyer, net::SpecialistRequest::bazaar_browse, 0, 0, "D-18");
        run(0.5);
        const auto stocked = [&]() -> const BazaarListing* {
            for (const BazaarListing& l : buyer.specialist().bazaar) {
                if (l.id >= general_store_listing_base && l.item.def_id == "wp_pistol_d18") return &l;
            }
            return nullptr;
        };
        CHECK(stocked() != nullptr && stocked()->price == d18->cost_credits);
        if (stocked() != nullptr) {
            const int64_t credits_before = buyer.player_status().credits;
            const std::string sales_before = store.get_extra(0, "bazaar_sales");
            const auto held = [&]() {
                return std::count_if(buyer.items().begin(), buyer.items().end(),
                                     [](const ItemInstance& i) { return i.def_id == "wp_pistol_d18"; });
            };
            const auto held_before = held();
            const uint64_t store_id = stocked()->id;
            act(buyer, net::SpecialistRequest::bazaar_buy, int64_t(store_id));
            run(0.5);
            CHECK(buyer.player_status().credits == credits_before - d18->cost_credits);
            CHECK(held() == held_before + 1);
            CHECK(server.bazaar_listings().empty() && store.get_extra(0, "bazaar_sales") == sales_before);
            act(buyer, net::SpecialistRequest::bazaar_browse, 0, 0, "D-18");
            run(0.5);
            CHECK(stocked() != nullptr);  // still there
        }
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

// A Doctor's heals: Field Triage reaching everyone near, Lingering Bacta
// after, and the Defibrillator bringing back the just-fallen.
void test_doctor_heals(const SpecialistBook& book, const ProfessionBook& professions, const ItemCatalog& catalog) {
    Heightfield ground{101, 2.0, -100, -100, std::vector<float>(101 * 101, 0.0f)};
    World world{ground};
    MemoryCharacterStore store;
    const CharacterRecord medic = store.create("Medic", {0, 0, 0}, 0);
    store.save_professions(medic.id, std::vector<ProfessionProgress>{{"doctor", professions.xp_for_level(90), 0}});
    store.set_extra(medic.id, "perks:doctor",
                    "doc_perk_heal_dart,doc_perk_lingering_bacta,doc_perk_defibrillator,doc_perk_triage");
    store.create("Left", {6, 0, 0}, 0);
    store.create("Right", {6, 0, 4}, 0);
    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink m_link{links, {2, 1}}, l_link{links, {3, 1}}, r_link{links, {4, 1}};
    ServerConfig config;
    config.catalog = &catalog;
    config.professions = &professions;
    config.specialists = &book;
    config.spawn = {0, 0, 0};
    config.respawn_delay = 8;
    GameServer server(srv_link, world, config, &store);
    GameClient m(m_link, world, ClientConfig{.server = server_ep, .name = "Medic"});
    GameClient l(l_link, world, ClientConfig{.server = server_ep, .name = "Left"});
    GameClient r(r_link, world, ClientConfig{.server = server_ep, .name = "Right"});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            m.update(now);
            l.update(now);
            r.update(now);
        }
    };
    run(1.0);
    CHECK(m.status() == GameClient::Status::connected && l.status() == GameClient::Status::connected &&
          r.status() == GameClient::Status::connected);
    const uint32_t el = l.entity_id(), er = r.entity_id();
    const auto dart = [&](uint32_t at) {
        net::SpecialistRequest q;
        q.action = net::SpecialistRequest::use_ability;
        q.id = "doc_heal_dart";
        q.target = at;
        m.specialist_request(q);
    };
    server.npc_melee(0, el, 200, DamageType::ballistic);
    server.npc_melee(0, er, 200, DamageType::ballistic);
    run(0.5);
    const double hurt_l = server.health_of(el), hurt_r = server.health_of(er);
    CHECK(hurt_l < 350 && hurt_r < 350);
    dart(el);
    run(0.5);
    // Triage: both healed, though only one was aimed at; Lingering Bacta: and still healing.
    CHECK(server.health_of(el) > hurt_l + 80 && server.health_of(er) > hurt_r + 80);
    const SpecialistState* ls = server.specialist_state(el);
    CHECK(ls && std::any_of(ls->effects.begin(), ls->effects.end(), [](const TimedEffect& e) { return e.id == "doc_lingering_bacta"; }));
    run(7.0);  // the dart's cooldown
    // Fallen: the next dart brings them back, with a third of their health.
    server.npc_melee(0, el, 5000, DamageType::ballistic);
    run(0.5);
    CHECK(!server.alive(el));
    dart(el);
    run(0.5);
    CHECK(server.alive(el) && server.health_of(el) > 100);
}

// Bounties: marks of their own, spawned on accepting and gone after; Big
// Game's harder one; players' posted bounties paid out on top, or given back
// when the mark opts out or nobody collects in time.
void test_bounties(const SpecialistBook& book, const ProfessionBook& professions, const ItemCatalog& catalog) {
    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    MemoryCharacterStore store;
    const CharacterRecord hunter_rec = store.create("Hunter", {0, 0, 0}, 0);
    store.save_professions(hunter_rec.id, std::vector<ProfessionProgress>{{"bounty_hunter", professions.xp_for_level(60), 0}});
    store.set_extra(hunter_rec.id, "perks:bounty_hunter", "bh_perk_seeker,bh_perk_toxin_dart,bh_perk_big_game");
    // ...and a Smuggler's flashbang, for the player mark.
    store.save_professions(hunter_rec.id, std::vector<ProfessionProgress>{{"bounty_hunter", professions.xp_for_level(60), 0},
                                                                          {"smuggler", professions.xp_for_level(10), 0}});
    store.set_extra(hunter_rec.id, "perks:smuggler", "smg_perk_flashbang");
    store.create("Mark", {3, 0, 0}, 0);
    const CharacterRecord poster_rec = store.create("Poster", {0, 0, 3}, 0);
    store.update_economy(poster_rec.id, 5000, 0, 0, 0);

    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink hunter_link{links, {2, 1}};
    net::LoopbackLink mark_link{links, {3, 1}};
    net::LoopbackLink poster_link{links, {4, 1}};
    int64_t wall = 1'000'000;
    ServerConfig config;
    config.catalog = &catalog;
    config.professions = &professions;
    config.specialists = &book;
    config.spawn = {0, 0, 0};
    config.bounty_post_seconds = 100;
    config.wall_clock = [&] { return wall; };
    GameServer server(srv_link, world, config, &store);
    server.set_quests(nullptr, {}, {{"Bounty Terminal", {0, 0, 0}, 8, "bounty"}});
    // The test's population: a frail outlaw beside the terminal, and two guards.
    std::vector<std::vector<uint32_t>> spawned, retired;
    server.on_spawn_mark = [&](const std::string& name, Vec3, int, int guards) {
        GameServer::NpcCombatSpec spec;
        spec.actor_kind = 3;
        spec.max_health = 1;
        std::vector<uint32_t> gang{server.add_combat_npc(name, {8, 0, 0}, 0, 0, spec)};
        for (int g = 0; g < guards; ++g) gang.push_back(server.add_combat_npc("Guard", {9, 0, double(g)}, 0, 0, spec));
        spawned.push_back(gang);
        return gang;
    };
    server.on_retire_mark = [&](const std::vector<uint32_t>& gang) {
        retired.push_back(gang);
        for (const uint32_t e : gang) server.remove_npc(e);
    };
    GameClient hunter(hunter_link, world, ClientConfig{.server = server_ep, .name = "Hunter"});
    GameClient mark(mark_link, world, ClientConfig{.server = server_ep, .name = "Mark"});
    GameClient poster(poster_link, world, ClientConfig{.server = server_ep, .name = "Poster"});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            hunter.update(now);
            mark.update(now);
            poster.update(now);
        }
    };
    const auto act = [&](GameClient& who, uint8_t action, int64_t amount, std::string id = {}, uint32_t target = 0) {
        net::SpecialistRequest r;
        r.action = action;
        r.amount = amount;
        r.id = std::move(id);
        r.target = target;
        who.specialist_request(r);
    };
    using R = net::SpecialistRequest;
    run(1.0);
    CHECK(hunter.status() == GameClient::Status::connected && mark.status() == GameClient::Status::connected &&
          poster.status() == GameClient::Status::connected);

    // Posting: on an opted-in player, at least 500, held from the poster.
    act(poster, R::post_bounty, 500, "Mark");
    run(0.3);
    CHECK(server.posted_bounties().empty());  // not opted in yet
    act(mark, R::bounty_flag, 1);
    run(0.3);
    act(poster, R::post_bounty, 100, "Mark");
    act(poster, R::post_bounty, 500, "Poster");
    run(0.3);
    CHECK(server.posted_bounties().empty());  // too little; yourself
    act(poster, R::post_bounty, 500, "Mark");
    run(0.3);
    CHECK(server.posted_bounties().size() == 1 && poster.player_status().credits == 4500);
    CHECK(parse_posted_bounties(store.get_extra(0, "posted_bounties")) == server.posted_bounties());

    // The terminal: three tier-3 marks (Hunter 60), Big Game's tier 4 at double pay, and the player.
    act(hunter, R::contracts, 0);
    run(0.5);
    std::vector<Contract> offers = hunter.specialist().offers;
    const auto count_kind = [&](ContractKind k) {
        return std::count_if(offers.begin(), offers.end(), [&](const Contract& c) { return c.kind == k; });
    };
    CHECK(count_kind(ContractKind::npc_mark) == 4 && count_kind(ContractKind::pvp_mark) == 1);
    const auto player = std::find_if(offers.begin(), offers.end(), [](const Contract& c) { return c.kind == ContractKind::pvp_mark; });
    CHECK(player != offers.end() && player->reward == 1500 && player->target_name == "Mark");
    CHECK(std::any_of(offers.begin(), offers.end(), [](const Contract& c) {
        return c.kind == ContractKind::npc_mark && c.title.find("tier 4, 3 guards") != std::string::npos;
    }));
    const auto first = std::find_if(offers.begin(), offers.end(), [](const Contract& c) {
        return c.kind == ContractKind::npc_mark && c.title.find("tier 3, 2 guards") != std::string::npos;
    });
    CHECK(first != offers.end() && first->target_entity == 0);
    if (first == offers.end() || player == offers.end()) return;

    // Taken: its outlaw and two guards spawn; abandoned: they go.
    act(hunter, R::contract_accept, int64_t(first->id));
    run(0.5);
    CHECK(spawned.size() == 1 && spawned[0].size() == 3);
    CHECK(hunter.specialist().contracts.size() == 1 && hunter.specialist().contracts[0].target_entity == spawned[0][0]);
    CHECK(server.npc_info(spawned[0][0]) && server.npc_info(spawned[0][0])->name == first->target_name);
    act(hunter, R::contract_abandon, int64_t(first->id));
    run(0.5);
    CHECK(retired.size() == 1 && !server.npc_info(spawned[0][0]));

    // Another, killed with a toxin dart: paid, and his body lies a while before going.
    act(hunter, R::contracts, 0);
    run(0.5);
    offers = hunter.specialist().offers;
    const auto second = std::find_if(offers.begin(), offers.end(), [](const Contract& c) { return c.kind == ContractKind::npc_mark; });
    CHECK(second != offers.end());
    if (second == offers.end()) return;
    const int64_t before = hunter.player_status().credits;
    act(hunter, R::contract_accept, int64_t(second->id));
    run(0.5);
    CHECK(spawned.size() == 2);
    if (spawned.size() != 2) return;
    net::SpecialistRequest dart;
    dart.action = R::use_ability;
    dart.id = "bh_toxin_dart";
    dart.target = spawned[1][0];
    dart.position = {8, 0, 0};
    hunter.specialist_request(dart);
    run(2.0);
    CHECK(hunter.specialist().contracts.empty());
    CHECK(hunter.player_status().credits == before + second->reward);
    CHECK(retired.size() == 1 && server.npc_info(spawned[1][0]));
    run(31.0);
    CHECK(retired.size() == 2 && !server.npc_info(spawned[1][0]));

    // The player mark: what's posted paid out on top of the base, and gone.
    act(hunter, R::contracts, 0);
    run(0.5);
    offers = hunter.specialist().offers;
    const auto pvp = std::find_if(offers.begin(), offers.end(), [](const Contract& c) { return c.kind == ContractKind::pvp_mark; });
    CHECK(pvp != offers.end());
    if (pvp == offers.end()) return;
    const int64_t before_pvp = hunter.player_status().credits;
    act(hunter, R::contract_accept, int64_t(pvp->id));
    run(0.5);
    // Hunted, they're an enemy: a flashbang blinds them (no slow), not the poster beside them.
    net::SpecialistRequest bang;
    bang.action = R::use_ability;
    bang.id = "smg_flashbang";
    bang.position = server.state_of(mark.entity_id()) ? server.state_of(mark.entity_id())->position : Vec3{};
    hunter.specialist_request(bang);
    run(0.3);
    const auto blinded = [&](uint32_t e) {
        const SpecialistState* st = server.specialist_state(e);
        return st && std::any_of(st->effects.begin(), st->effects.end(), [](const TimedEffect& x) {
            return (x.flags & effect_flag::blinded) != 0 && x.modifiers.empty();
        });
    };
    CHECK(blinded(mark.entity_id()) && !blinded(poster.entity_id()) && !blinded(hunter.entity_id()));
    server.npc_melee(hunter.entity_id(), mark.entity_id(), 1e6, DamageType::ballistic);
    run(0.5);
    CHECK(hunter.player_status().credits == before_pvp + 1500);
    CHECK(server.posted_bounties().empty());

    // Given back: when the mark opts out, and when nobody collects in time.
    run(6.0);  // (the mark back on their feet)
    act(poster, R::post_bounty, 700, "Mark");
    run(0.3);
    CHECK(poster.player_status().credits == 3800);
    act(mark, R::bounty_flag, 0);
    run(0.5);
    CHECK(server.posted_bounties().empty() && poster.player_status().credits == 4500);
    act(mark, R::bounty_flag, 1);
    act(poster, R::post_bounty, 600, "Mark");
    run(0.5);
    CHECK(poster.player_status().credits == 3900);
    wall += 101;
    run(1.5);
    CHECK(server.posted_bounties().empty() && poster.player_status().credits == 4500);
}

// A Smuggler: slicing another's rifle for a fee (a Master Slicer's, half
// again as strong), Lucky, and a contraband run: one package at a time,
// pirates at the drop, the package lost with its runner.
void test_smuggler(const SpecialistBook& book, const ProfessionBook& professions, const ItemCatalog& catalog) {
    Heightfield ground{401, 2.0, -400, -400, std::vector<float>(401 * 401, 0.0f)};
    World world{ground};
    MemoryCharacterStore store;
    const CharacterRecord runner_rec = store.create("Runner", {0, 0, 0}, 0);
    store.save_professions(runner_rec.id, std::vector<ProfessionProgress>{{"smuggler", professions.xp_for_level(80), 0}});
    store.set_extra(runner_rec.id, "perks:smuggler", "smg_perk_smoke,smg_perk_getaway,smg_perk_master_slicer,smg_perk_lucky");
    store.update_economy(runner_rec.id, 1000, 0, 0, 0);
    const CharacterRecord owner_rec = store.create("Owner", {3, 0, 0}, 0);
    store.update_economy(owner_rec.id, 2000, 0, 0, 0);
    const ItemInstance rifle = store.grant_item({0, owner_rec.id, "wp_rifle_e11"});

    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink r_link{links, {2, 1}}, o_link{links, {3, 1}};
    ServerConfig config;
    config.catalog = &catalog;
    config.professions = &professions;
    config.specialists = &book;
    config.spawn = {0, 0, 0};
    config.respawn_delay = 1;
    GameServer server(srv_link, world, config, &store);
    server.set_quests(nullptr, {}, {{"Mission Terminal", {0, 0, 0}, 8, "general"}});
    int ambushes = 0;
    server.on_spawn_mark = [&](const std::string& name, Vec3 at, int, int guards) {
        ++ambushes;
        GameServer::NpcCombatSpec spec;
        spec.actor_kind = 3;
        spec.max_health = 100;
        std::vector<uint32_t> gang{server.add_combat_npc(name, at, 0, 0, spec)};
        for (int g = 0; g < guards; ++g) gang.push_back(server.add_combat_npc("Pirate", at + Vec3{3, 0, double(g)}, 0, 0, spec));
        return gang;
    };
    GameClient runner(r_link, world, ClientConfig{.server = server_ep, .name = "Runner"});
    GameClient owner(o_link, world, ClientConfig{.server = server_ep, .name = "Owner"});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            runner.update(now);
            owner.update(now);
        }
    };
    using R = net::SpecialistRequest;
    const auto act = [&](GameClient& who, uint8_t action, int64_t amount, uint64_t item = 0, std::string id = {},
                         std::string text = {}, uint32_t target = 0) {
        net::SpecialistRequest r;
        r.action = action;
        r.amount = amount;
        r.item_id = item;
        r.id = std::move(id);
        r.text = std::move(text);
        r.target = target;
        who.specialist_request(r);
    };
    run(1.0);
    CHECK(runner.status() == GameClient::Status::connected && owner.status() == GameClient::Status::connected);

    // Asked, and done: the fee to the Smuggler (less the parts), the slice half again as strong.
    act(owner, R::slice_request, 0, rifle.id, "slice_overclock", "Runner");
    run(0.5);
    CHECK(runner.specialist().slice_jobs.size() == 1);
    if (runner.specialist().slice_jobs.empty()) return;
    act(runner, R::slice_answer, int64_t(runner.specialist().slice_jobs[0].id), 0, {}, {}, 1);
    run(0.5);
    CHECK(runner.specialist().slice_jobs.empty());
    CHECK(owner.player_status().credits == 2000 - slice_job_fee);
    CHECK(runner.player_status().credits == 1000 + slice_job_fee - 250);
    const std::vector<ItemInstance> held = store.list_items(owner_rec.id);
    const auto sliced = std::find_if(held.begin(), held.end(), [&](const ItemInstance& i) { return i.id == rifle.id; });
    CHECK(sliced != held.end() && item_slice(*sliced) == "slice_overclock" && std::abs(item_slice_power(*sliced) - 1.5) < 1e-9);
    const SliceDef strong = scaled_slice(*book.slice("slice_overclock"), 1.5);
    CHECK(std::abs(strong.rate - 1.18) < 1e-9 && std::abs(strong.heat - 1.3) < 1e-9);

    // Lucky: the first killing blow leaves 1 health; the next kills.
    const uint32_t er = runner.entity_id();
    server.npc_melee(0, er, 1e6, DamageType::ballistic);
    run(0.2);
    CHECK(server.alive(er) && server.health_of(er) <= 1.0);
    server.npc_melee(0, er, 1e6, DamageType::ballistic);
    run(0.2);
    CHECK(!server.alive(er));
    run(3.0);
    CHECK(server.alive(er));

    // Contraband: one package at a time.
    act(runner, R::contracts, 0);
    run(0.5);
    std::vector<Contract> runs;
    for (const Contract& c : runner.specialist().offers) {
        if (c.kind == ContractKind::contraband) runs.push_back(c);
    }
    CHECK(runs.size() == 2);
    if (runs.size() < 2) return;
    act(runner, R::contract_accept, int64_t(runs[0].id));
    act(runner, R::contract_accept, int64_t(runs[1].id));
    run(0.5);
    CHECK(runner.specialist().contracts.size() == 1);
    // Near the drop, pirates wait.
    MoveState near = *server.state_of(er);
    near.position = runs[0].waypoint + Vec3{60, 0, 0};
    server.set_state(er, near);
    run(0.5);
    CHECK(ambushes == 1);
    // Fallen (Lucky spent first: it's back each life), the package is lost.
    server.npc_melee(0, er, 1e6, DamageType::ballistic);
    run(0.2);
    CHECK(server.alive(er) && runner.specialist().contracts.size() == 1);
    server.npc_melee(0, er, 1e6, DamageType::ballistic);
    run(0.3);
    CHECK(!server.alive(er) && runner.specialist().contracts.empty());
}

// A Ranger: a hunt from a scout terminal (its named quarry spawned, brought
// down by a snare trap), a Trapper's two traps, Survivalist, and an
// Architect's field camp kit pitching a camp.
void test_ranger(const SpecialistBook& book, const ProfessionBook& professions, const ItemCatalog& catalog) {
    Heightfield ground{401, 2.0, -400, -400, std::vector<float>(401 * 401, 0.0f)};
    World world{ground};
    MemoryCharacterStore store;
    const CharacterRecord rec = store.create("Scout", {0, 0, 0}, 0);
    store.save_professions(rec.id, std::vector<ProfessionProgress>{{"ranger", professions.xp_for_level(80), 0}});
    store.set_extra(rec.id, "perks:ranger", "rg_perk_tracking,rg_perk_snare_trap,rg_perk_keen_senses,rg_perk_trapper,rg_perk_survivalist");
    ensure_starter_kit(store, rec.id);  // (a loadout to carry the kit in, before the camp kit below)
    const ItemInstance kit = store.grant_item({0, rec.id, "arch_field_camp"});
    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink link{links, {2, 1}};
    ServerConfig config;
    config.catalog = &catalog;
    config.professions = &professions;
    config.specialists = &book;
    config.spawn = {0, 0, 0};
    GameServer server(srv_link, world, config, &store);
    server.set_quests(nullptr, {}, {{"Scout Terminal", {0, 0, 0}, 8, "scout"}});
    std::vector<std::vector<uint32_t>> retired;
    uint32_t quarry = 0;
    server.on_spawn_quarry = [&](const std::string& name, Vec3, int) {
        GameServer::NpcCombatSpec spec;
        spec.actor_kind = 2;
        spec.max_health = 10;
        quarry = server.add_combat_npc(name + " the Kreetle", {5, 0, 0}, 0, 0, spec);
        return quarry;
    };
    server.on_retire_mark = [&](const std::vector<uint32_t>& gone) {
        retired.push_back(gone);
        for (const uint32_t e : gone) server.remove_npc(e);
    };
    GameClient client(link, world, ClientConfig{.server = server_ep, .name = "Scout"});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            client.update(now);
        }
    };
    using R = net::SpecialistRequest;
    const auto act = [&](uint8_t action, int64_t amount) {
        net::SpecialistRequest r;
        r.action = action;
        r.amount = amount;
        client.specialist_request(r);
    };
    const auto trap = [&](Vec3 at) {
        net::SpecialistRequest r;
        r.action = R::use_ability;
        r.id = "rg_trip_trap";
        r.position = at;
        client.specialist_request(r);
    };
    run(1.0);
    CHECK(client.status() == GameClient::Status::connected);
    const uint32_t me = client.entity_id();

    // Survivalist: hurt, then out of the fight, it comes back.
    const double full = server.health_of(me);
    server.npc_melee(0, me, 300, DamageType::ballistic);
    run(0.5);
    const double hurt = server.health_of(me);
    CHECK(hurt > 0 && hurt < full - 50);
    run(12.0);
    CHECK(server.health_of(me) > hurt + 30);

    // A hunt: two offered (tier 4 at Ranger 80); taken, its quarry spawns.
    act(R::contracts, 0);
    run(0.5);
    const std::vector<Contract> offers = client.specialist().offers;
    const auto hunt = std::find_if(offers.begin(), offers.end(), [](const Contract& c) { return c.kind == ContractKind::track; });
    CHECK(std::count_if(offers.begin(), offers.end(), [](const Contract& c) { return c.kind == ContractKind::track; }) == 2);
    CHECK(hunt != offers.end() && hunt->title.find("tier 4") != std::string::npos && hunt->target_entity == 0);
    if (hunt == offers.end()) return;
    act(R::contract_accept, int64_t(hunt->id));
    run(0.5);
    CHECK(quarry != 0 && client.specialist().contracts.size() == 1 && client.specialist().contracts[0].target_entity == quarry);
    CHECK(client.specialist().contracts[0].target_name.find("the Kreetle") != std::string::npos);
    // A Trapper's first trap, out of the way; the second, under the quarry, brings it down.
    trap({-5, 0, 0});
    run(0.5);
    CHECK(server.deployable_count() == 1);
    run(30.5);  // the trap's cooldown
    const int64_t before = client.player_status().credits;
    trap({5, 0, 0});
    run(1.5);
    CHECK(client.specialist().contracts.empty() && client.player_status().credits == before + hunt->reward);
    CHECK(server.deployable_count() == 1);  // the first trap's still set (the second sprang)
    run(31.0);
    CHECK(retired.size() == 1);

    // The Architect's camp kit: used, a camp.
    client.use_item(kit.id);
    run(0.5);
    CHECK(server.deployable_count() == 2);
    const std::vector<ItemInstance> left = store.list_items(rec.id);
    CHECK(std::none_of(left.begin(), left.end(), [&](const ItemInstance& i) { return i.id == kit.id; }));
}

// A Merchant at a gear vendor: a Haggler pays 10% less, and buying and
// selling there earn Merchant experience; anyone sells on the bazaar.
void test_merchant(const SpecialistBook& book, const ProfessionBook& professions, const ItemCatalog& catalog) {
    Heightfield ground{101, 2.0, -100, -100, std::vector<float>(101 * 101, 0.0f)};
    World world{ground};
    MemoryCharacterStore store;
    const CharacterRecord rec = store.create("Trader", {0, 0, 0}, 0);
    const int64_t xp0 = professions.xp_for_level(30);
    store.save_professions(rec.id, std::vector<ProfessionProgress>{{"merchant", xp0, 0}});
    store.set_extra(rec.id, "perks:merchant", "mer_perk_trader,mer_perk_haggler");
    store.update_economy(rec.id, 1000, 0, 0, 0);
    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink link{links, {2, 1}};
    ServerConfig config;
    config.catalog = &catalog;
    config.professions = &professions;
    config.specialists = &book;
    config.spawn = {0, 0, 0};
    GameServer server(srv_link, world, config, &store);
    server.set_vendors({{"Gear Vendor", "gear_vendor", {0, 0, 0}, 6, ""}});
    GameClient client(link, world, ClientConfig{.server = server_ep, .name = "Trader"});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            client.update(now);
        }
    };
    run(1.0);
    CHECK(client.status() == GameClient::Status::connected);
    const ItemDef* kit = catalog.find("arch_field_camp");
    CHECK(kit != nullptr && kit->cost_credits == 95 && kit->sell_value == 32);
    const auto merchant_xp = [&] {
        const auto* p = server.professions_of(client.entity_id());
        return p ? profession_xp(*p, "merchant") : 0;
    };
    const int64_t credits = client.player_status().credits;
    client.buy_item("arch_field_camp", false);
    run(0.5);
    CHECK(client.player_status().credits == credits - (95 - 9));  // a Haggler's 10% off
    const int64_t after_buy = merchant_xp();
    CHECK(after_buy > xp0);
    uint64_t bought = 0;
    for (const ItemInstance& i : client.items()) {
        if (i.def_id == "arch_field_camp") bought = i.id;
    }
    CHECK(bought != 0);
    client.sell_item(bought);
    run(0.5);
    CHECK(client.player_status().credits == credits - 86 + 32);
    CHECK(merchant_xp() > after_buy);
}

// A handler with The Pack fields two creatures at once (a third sends the
// first back); Pack Leader's are tougher; one stored, the other stays out.
void test_creature_handler(const SpecialistBook& book, const ProfessionBook& professions, const ItemCatalog& catalog) {
    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    MemoryCharacterStore store;
    const CharacterRecord rec = store.create("Handler", {0, 0, 0}, 0);
    store.save_professions(rec.id, std::vector<ProfessionProgress>{{"creature_handler", professions.xp_for_level(100), 0}});
    store.set_extra(rec.id, "perks:creature_handler", "ch_perk_tamer,ch_perk_mend,ch_perk_pack_leader,ch_perk_recovery,ch_perk_pack");
    std::string datapad;
    for (uint64_t id = 1; id <= 3; ++id) {
        datapad += format_companion({id, "womp_rat", std::format("Biter {}", id), "tamed", 0.5, 1, 1, 1, 0, {}, false}) + "\n";
    }
    store.set_extra(rec.id, "datapad", datapad);
    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink link{links, {2, 1}};
    ServerConfig config;
    config.catalog = &catalog;
    config.professions = &professions;
    config.specialists = &book;
    config.pvp_enabled = false;
    config.spawn = {0, 0, 0};
    GameServer server(srv_link, world, config, &store);
    server.set_looks({"object/mobile/shared_womp_rat.iff"});
    GameClient client(link, world, ClientConfig{.server = server_ep, .name = "Handler"});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            client.update(now);
        }
    };
    run(1.0);
    CHECK(client.status() == GameClient::Status::connected);
    const SpecialistState* spec = server.specialist_state(client.entity_id());
    CHECK(spec && spec->companions.size() == 3);
    if (spec == nullptr || spec->companions.size() != 3) return;
    const auto order = [&](net::SpecialistRequest::Action action, int64_t amount) {
        net::SpecialistRequest r;
        r.action = action;
        r.amount = amount;
        client.specialist_request(r);
        run(0.4);
    };
    order(net::SpecialistRequest::companion_summon, 1);
    order(net::SpecialistRequest::companion_summon, 2);
    CHECK(spec->active_companion == 1 && spec->second_companion == 2);
    CHECK(spec->companion_entity != 0 && spec->second_entity != 0 && spec->companion_entity != spec->second_entity);
    CHECK(client.specialist().second_companion == 2);  // (sent)
    // Pack Leader's: 15% tougher than its handler's level alone makes it.
    const CompanionTemplate* rat = book.companion("womp_rat");
    const auto pet = server.npc_info(spec->companion_entity);
    CHECK(rat && pet && near(pet->max_health, rat->health * 1.0 * 1.5 * 1.15, 0.5));
    // A third: the first goes back to the datapad.
    order(net::SpecialistRequest::companion_summon, 3);
    CHECK(spec->active_companion == 2 && spec->second_companion == 3);
    CHECK(!server.npc_info(pet ? pet->entity : 0) || server.npc_info(pet->entity)->hidden ||
          server.npc_info(pet->entity)->life != Life::alive);
    // Storing one leaves the other out.
    order(net::SpecialistRequest::companion_dismiss, 2);
    CHECK(spec->active_companion == 3 && spec->second_companion == 0 && spec->companion_entity != 0);
    order(net::SpecialistRequest::companion_dismiss, 0);
    CHECK(spec->active_companion == 0 && spec->second_companion == 0);
}

// A Field Harvester shoots banthas for their DNA (half of them give a sample),
// engineers a Tactical Strain egg and hatches a bantha bred small.
void test_bio_engineer(const SpecialistBook& book, const ProfessionBook& professions, const ItemCatalog& catalog) {
    Heightfield ground{201, 2.0, -200, -200, std::vector<float>(201 * 201, 0.0f)};
    World world{ground};
    MemoryCharacterStore store;
    const CharacterRecord rec = store.create("Geneticist", {0, 0, 0}, 0);
    store.save_professions(rec.id, std::vector<ProfessionProgress>{{"bio_engineer", professions.xp_for_level(90), 0},
                                                                   {"creature_handler", professions.xp_for_level(100), 0}});
    store.set_extra(rec.id, "perks:creature_handler", "ch_perk_breeder");
    store.set_extra(rec.id, "perks:bio_engineer", "bio_perk_harvester,bio_perk_hybrid,bio_perk_culture,bio_perk_notes,bio_perk_strain");
    ensure_starter_kit(store, rec.id);
    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink link{links, {2, 1}};
    ServerConfig config;
    config.catalog = &catalog;
    config.professions = &professions;
    config.specialists = &book;
    config.pvp_enabled = false;
    config.spawn = {0, 0, 0};
    GameServer server(srv_link, world, config, &store);
    server.set_looks({"object/mobile/shared_bantha.iff"});
    GameClient client(link, world, ClientConfig{.server = server_ep, .name = "Geneticist"});
    double now = 0;
    const auto run = [&](double seconds, auto steer) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            steer();
            server.update(now);
            client.update(now);
        }
    };
    run(1.0, [] {});
    CHECK(client.status() == GameClient::Status::connected);
    // No sampling button: Field Harvester is passive (the Creature Handler's buttons are its own).
    CHECK(std::none_of(client.specialist().abilities.begin(), client.specialist().abilities.end(),
                       [](const net::AbilityStatus& s) { return s.id == "bio_sample"; }));
    const auto bio_xp = [&] {
        const auto* p = server.professions_of(client.entity_id());
        return p ? profession_xp(*p, "bio_engineer") : 0;
    };
    const int64_t xp0 = bio_xp();
    // Eight banthas round about, shot one by one.
    GameServer::NpcCombatSpec bantha;
    bantha.actor_kind = 2;
    bantha.max_health = 10;
    bantha.hit_radius = 1.35;
    bantha.hit_height = 2.8;
    int killed = 0;
    for (int i = 0; i < 8; ++i) {
        const double angle = i * 0.785;
        const Vec3 at{std::sin(angle) * 7, 0, std::cos(angle) * 7};
        const uint32_t beast = server.add_combat_npc("Bantha", at, 0, 1, bantha);
        for (int t = 0; t < 120; ++t) {
            const auto info = server.npc_info(beast);
            if (!info || info->life != Life::alive) break;
            const Vec3 d = info->state.position + Vec3{0, 1.4, 0} - (client.predicted().position + Vec3{0, 1.6, 0});
            const double yaw = std::atan2(-d.x, -d.z), pitch = std::atan2(d.y, std::hypot(d.x, d.z));
            run(1.0 / 30.0, [&] { client.set_input({.yaw = yaw, .pitch = pitch, .fire = (t % 6) < 3}); });
        }
        const auto info = server.npc_info(beast);
        if (info && info->life != Life::alive) ++killed;
    }
    run(0.3, [&] { client.set_input({}); });
    CHECK(killed == 8);
    const SpecialistState* spec = server.specialist_state(client.entity_id());
    const size_t sampled = spec ? spec->samples.size() : 0;
    CHECK(sampled >= 1 && sampled < 8);  // about half
    CHECK(bio_xp() > xp0);
    if (spec == nullptr || spec->samples.empty()) return;
    CHECK(spec->samples[0].template_id == "bantha");
    // A Tactical Strain egg, hatched: a bantha bred small.
    net::SpecialistRequest egg_request;
    egg_request.action = net::SpecialistRequest::engineer_egg;
    egg_request.amount = int64_t(spec->samples[0].id);
    client.specialist_request(egg_request);
    run(0.5, [] {});
    const auto egg = std::find_if(client.items().begin(), client.items().end(),
                                  [](const ItemInstance& i) { return i.def_id == "bio_creature_egg"; });
    CHECK(egg != client.items().end());
    if (egg == client.items().end()) return;
    net::SpecialistRequest hatch;
    hatch.action = net::SpecialistRequest::hatch_egg;
    hatch.item_id = egg->id;
    client.specialist_request(hatch);
    run(0.5, [] {});
    spec = server.specialist_state(client.entity_id());
    CHECK(spec && !spec->companions.empty() && spec->companions.back().tactical);
    if (spec == nullptr || spec->companions.empty()) return;
    net::SpecialistRequest summon;
    summon.action = net::SpecialistRequest::companion_summon;
    summon.amount = int64_t(spec->companions.back().id);
    client.specialist_request(summon);
    run(0.5, [] {});
    const auto pet = server.companion_of(client.entity_id());
    CHECK(pet.has_value());
    bool small = false;
    for (const net::EntityState& e : client.remotes()) {
        if (pet && e.id == pet->entity) small = e.scale < 100 && e.scale >= 50;
    }
    CHECK(small);
}

// Pilots on speeder bikes: a Hotshot's goes faster, Afterburner boosts on no
// energy, the kit is two buttons, and anyone riding earns Pilot experience.
void test_pilot(const std::filesystem::path& root, const SpecialistBook& book, const ProfessionBook& professions,
                const ItemCatalog& catalog) {
    const std::vector<VehicleClass> classes = load_vehicle_classes(root / "game" / "vehicles");
    Heightfield ground{401, 2.0, -400, -400, std::vector<float>(401 * 401, 0.0f)};
    World world{ground};
    MemoryCharacterStore store;
    const CharacterRecord ace = store.create("Ace", {0, 0, 0}, 0);
    const CharacterRecord rookie = store.create("Rookie", {0, 0, 20}, 0);
    store.save_professions(ace.id, std::vector<ProfessionProgress>{{"pilot", professions.xp_for_level(90), 0}});
    store.set_extra(ace.id, "perks:pilot",
                    "pl_perk_hotshot,pl_perk_field_repair,pl_perk_reactor,pl_perk_afterburner,pl_perk_ace");
    ensure_starter_kit(store, ace.id);  // (loadouts to carry the perks in, before the bikes below)
    ensure_starter_kit(store, rookie.id);
    const ItemInstance ace_bike = store.grant_item({0, ace.id, "vehicle_speeder_bike", ItemGrade::standard, "", "{}"});
    const ItemInstance rookie_bike =
        store.grant_item({0, rookie.id, "vehicle_speeder_bike", ItemGrade::standard, "", "{}"});
    net::LoopbackHub links;
    net::Endpoint server_ep{1, 1};
    net::LoopbackLink srv_link{links, server_ep};
    net::LoopbackLink link_a{links, {2, 1}};
    net::LoopbackLink link_b{links, {3, 1}};
    ServerConfig config;
    config.catalog = &catalog;
    config.professions = &professions;
    config.specialists = &book;
    config.world_vehicles = classes;
    config.pvp_enabled = false;
    config.spawn = {0, 0, 0};
    GameServer server(srv_link, world, config, &store);
    ClientConfig ca{.server = server_ep, .name = "Ace"};
    ca.world_vehicles = classes;
    ClientConfig cb{.server = server_ep, .name = "Rookie"};
    cb.world_vehicles = classes;
    GameClient a(link_a, world, ca);
    GameClient b(link_b, world, cb);
    double now = 0;
    const auto run = [&](double seconds, auto steer) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            steer();
            server.update(now);
            a.update(now);
            b.update(now);
        }
    };
    run(1.0, [] {});
    CHECK(a.status() == GameClient::Status::connected && b.status() == GameClient::Status::connected);
    CHECK(near(a.character_stats().vehicle_speed, 1.1, 1e-9) && near(b.character_stats().vehicle_speed, 1.0, 1e-9));
    // Two buttons: Field Repair and Afterburner.
    std::vector<std::string> kit;
    for (const net::AbilityStatus& s : a.specialist().abilities) kit.push_back(s.id);
    CHECK(kit.size() == 2 && std::find(kit.begin(), kit.end(), "pl_field_repair") != kit.end() &&
          std::find(kit.begin(), kit.end(), "pl_afterburner") != kit.end());
    // On their bikes.
    a.use_item(ace_bike.id);
    b.use_item(rookie_bike.id);
    run(0.5, [] {});
    run(0.1, [&] { a.set_input({.use = true}); b.set_input({.use = true}); });
    run(0.5, [&] { a.set_input({}); b.set_input({}); });
    CHECK(a.predicted().vehicle >= 0 && b.predicted().vehicle >= 0);
    // Flat out for 4 s: the Hotshot's further.
    const Vec3 a0 = a.predicted().position, b0 = b.predicted().position;
    run(4, [&] { a.set_input({.forward = 1, .yaw = 0}); b.set_input({.forward = 1, .yaw = 0}); });
    const double da = (a.predicted().position - a0).length(), db = (b.predicted().position - b0).length();
    CHECK(db > 40 && da > db * 1.06);
    // Afterburner: boosting burns no energy while it lasts; the rookie's runs down.
    const auto pilot_xp = [&](const GameClient& c) {
        const auto* p = server.professions_of(c.entity_id());
        return p ? profession_xp(*p, "pilot") : 0;
    };
    const int64_t before_burn = pilot_xp(a);
    net::SpecialistRequest use;
    use.action = net::SpecialistRequest::use_ability;
    use.id = "pl_afterburner";
    a.specialist_request(use);
    run(0.3, [&] { a.set_input({.forward = 1, .yaw = 0}); b.set_input({.forward = 1, .yaw = 0}); });
    CHECK(pilot_xp(a) > before_burn);  // (using it earns experience)
    run(3, [&] { a.set_input({.forward = 1, .yaw = 0, .sprint = true}); b.set_input({.forward = 1, .yaw = 0, .sprint = true}); });
    CHECK(a.predicted().energy > 95 && b.predicted().energy < 60);
    // Riding a minute on the move: Pilot experience, the rookie's too.
    const int64_t a_xp = pilot_xp(a), b_xp = pilot_xp(b);
    run(62, [&] {
        const double turn = now * 0.4;
        a.set_input({.forward = 1, .yaw = turn});
        b.set_input({.forward = 1, .yaw = turn});
    });
    CHECK(pilot_xp(a) > a_xp && pilot_xp(b) > b_xp);
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
    net::LoopbackLink c_link{links, {4, 1}};
    ServerConfig config;
    config.battlefield = field;
    config.professions = &professions;
    config.specialists = &book;
    config.respawn_delay = 0.5;
    GameServer server(srv_link, world, config);
    GameClient a(a_link, world, ClientConfig{.server = server_ep, .name = "Leader", .battlefield = field, .team = 0});
    GameClient b(b_link, world, ClientConfig{.server = server_ep, .name = "Enemy", .battlefield = field, .team = 1});
    GameClient c(c_link, world, ClientConfig{.server = server_ep, .name = "Trooper", .battlefield = field, .team = 0});
    double now = 0;
    const auto run = [&](double seconds) {
        for (const double end = now + seconds; now < end; now += 1.0 / 30.0) {
            server.update(now);
            a.update(now);
            b.update(now);
            c.update(now);
        }
    };
    run(0.6);
    CHECK(a.status() == GameClient::Status::connected && b.status() == GameClient::Status::connected);
    const uint32_t ea = a.entity_id(), eb = b.entity_id(), ec = c.entity_id();
    // What a hub hands over: the kit, the levels, its perks' switches (Squad
    // Spawn Point), a womp rat in the Companion Slot.
    Loadout kit;
    kit.abilities = {"rg_cloak", "sl_strike", "sl_rally", "rg_track"};
    kit.grants = {"squad_leader:spawn_point"};
    kit.specialist_levels = {{"ranger", 100}, {"squad_leader", 100}, {"creature_handler", 60}};
    kit.companion = format_companion({7, "womp_rat", "Biter", "tamed", 0.5, 1, 1, 1, 0, {}, false});
    kit.inspiration = format_inspiration({"insp_endurance", 600, 1.0});  // built in the hub
    server.set_player_loadouts(ea, {kit});
    a.request_spawn(0, 0);
    b.request_spawn(0, 2);
    run(1.5);
    CHECK(server.alive(ea) && server.alive(eb));
    const bool c_with_a = true;  // (asked for Leader's side)
    run(0.6);  // the kit refreshes
    const auto in_kit = [&](std::string_view id) {
        return std::any_of(a.specialist().abilities.begin(), a.specialist().abilities.end(),
                           [&](const net::AbilityStatus& s) { return s.id == id; });
    };
    CHECK(in_kit("rg_cloak") && in_kit("sl_strike") && !in_kit("rg_track"));  // tracking is world-only
    const auto inspired = [&] {
        const SpecialistState* s = server.specialist_state(ea);
        return s != nullptr && s->inspiration.id == "insp_endurance" && has_effect_flag(s->effects, effect_flag::inspired);
    };
    CHECK(inspired());  // the hub's Inspiration came along
    const auto sees = [&](GameClient& who, uint32_t e) {
        const std::vector<GameClient::Remote> remotes = who.remotes();  // (a copy: once)
        return std::any_of(remotes.begin(), remotes.end(), [&](const GameClient::Remote& r) { return r.id == e; });
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
    // Their spawn point goes where they go, and the womp rat from the Companion Slot.
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
    // A trooper of their side spawns on them.
    const MoveState* leader_at = server.state_of(ea);
    const Vec3 leader = leader_at ? leader_at->position : Vec3{};
    if (c_with_a) {
        c.request_spawn(0, beacon_spawn_post);
        run(1.0);
        const MoveState* by = server.state_of(ec);
        CHECK(server.alive(ec) && by && std::hypot(by->position.x - leader.x, by->position.z - leader.z) < 3);
    }
    // Killed: their spawn point is gone with them.
    server.npc_melee(eb, ea, 5000, DamageType::melee);
    run(1.5);
    CHECK(!server.alive(ea));
    CHECK(!server.companion_of(ea));  // a companion leaves with its owner
    CHECK(server.deployable_count() == 0);
    a.request_spawn(0, 0);
    run(1.0);
    CHECK(server.alive(ea) && server.deployable_count() == 1);
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
    test_bounties(book, professions, catalog);
    test_doctor_heals(book, professions, catalog);
    test_smuggler(book, professions, catalog);
    test_ranger(book, professions, catalog);
    test_merchant(book, professions, catalog);
    test_pilot(root, book, professions, catalog);
    test_bio_engineer(book, professions, catalog);
    test_creature_handler(book, professions, catalog);
    test_match(book, professions);
    return swm::test::finish("specialist_test");
}
