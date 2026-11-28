#include "swbfo/core/species.h"

#include "test.h"

using namespace swbfo::core;
using swbfo::test::near;

int main() {
    CHECK(all_species().size() == 10);
    CHECK(species_from_name("wookiee") == Species::wookiee);
    CHECK(species_from_name("WKE") == Species::wookiee);
    CHECK(species_from_name("ithorian") == Species::ithorian);
    CHECK(!species_from_name("ewok").has_value());

    CHECK(gender_from_name("male") == Gender::male);
    CHECK(gender_from_name("F") == Gender::female);
    CHECK(!gender_from_name("droid").has_value());

    // Gear slot restrictions.
    CHECK(species_shows_slot(Species::human, "boots"));
    CHECK(species_shows_slot(Species::human, "chest"));
    CHECK(!species_shows_slot(Species::trandoshan, "boots"));
    CHECK(!species_shows_slot(Species::trandoshan, "gloves"));
    CHECK(species_shows_slot(Species::trandoshan, "chest"));
    CHECK(species_shows_slot(Species::trandoshan, "deflector_shield"));
    CHECK(!species_shows_slot(Species::wookiee, "chest"));
    CHECK(!species_shows_slot(Species::wookiee, "boots"));
    CHECK(species_shows_slot(Species::wookiee, "deflector_shield"));
    CHECK(!species_shows_slot(Species::ithorian, "helmet"));
    CHECK(species_shows_slot(Species::ithorian, "deflector_shield"));

    // A Wookiee's traits are small, like any species': its armor is what
    // makes it tough.
    Loadout wookiee_empty;
    const CharacterStats wke_stats = stats_for(wookiee_empty, Species::wookiee);
    CHECK(near(wke_stats.max_health, 440));
    CHECK(near(wke_stats.mitigation, 0));

    // Armor in a slot a Wookiee doesn't show still counts in full.
    Loadout armored;
    armored.gear.push_back(Gear{.name = "heavy", .slot = "chest", .modifiers = {{Stat::max_health, 100, 1}}});
    armored.gear.push_back(Gear{.name = "psg", .slot = "deflector_shield", .modifiers = {{Stat::mitigation_laser, 0.15, 1}}});
    const CharacterStats worn = stats_for(armored, Species::wookiee);
    CHECK(near(worn.max_health, wke_stats.max_health + 100));
    CHECK(near(worn.mitigation_by_type[size_t(DamageType::laser)], 0.15));
    CHECK(near(stats_for(armored, Species::human).max_health, 500));

    // Body dimensions scale with height slider.
    const SpeciesBody tall_wke = species_body(Species::wookiee, Gender::male, 1.0);
    const SpeciesBody short_sul = species_body(Species::sullustan, Gender::female, 0.0);
    CHECK(tall_wke.height > 2.3);
    CHECK(tall_wke.eye_height > 2.1);
    CHECK(short_sul.height < 1.4);

    return swbfo::test::finish("species_test");
}
