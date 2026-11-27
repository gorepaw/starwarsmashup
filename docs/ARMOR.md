# Armor

Every armor set in Galaxies, sorted into our three armor classes, and how we turn them into items. Gear rules (Mk tiers, quality, the 2x curve) are in [Design](DESIGN.md#weapons-and-loadouts); the armor professions are in [Professions](PROFESSIONS.md). Weapons, done first, are in [Weapons](WEAPONS.md).

## The classes and slots

| Class | Profession | What it's for | Today's sets |
|---|---|---|---|
| None (clothing) | Unarmored | The fastest feet and deepest stamina | tunic, jacket, pants, boots |
| Light / Recon | Light Armor | Mobility, stamina; electro and bio | Ubese ✅ |
| Medium / Battle | Medium Armor | Balanced; laser and ballistic | Padded ✅, Stormtrooper ✅ |
| Heavy / Assault | Heavy Armor | Protection at a slower pace; explosive, melee, ballistic | Composite ✅ |

A loadout's armor class is its pieces' majority, and that class's profession is the one that levels and applies (`loadout_armor_class`).

The worn slots are helmet, chest, leggings, boots, gloves, biceps, bracers and belt; the Deflector Shield has its own. The Cosmetic Override also has a Bodysuit slot (below). Biceps and bracers are one item each, worn as a left and right pair. Every species wears every slot for its statistics; what a body can't show (a Wookiee's or Ithorian's armor, a Trandoshan's gloves and boots) only affects what shows.

### Everything is equipment

There are no cosmetic-only items but one, the bodysuit. Armor and clothing are equipment, and what one does depends on where it's slotted:

- **In the gear panel** it's worn for its statistics, and shows.
- **In the Cosmetic Override panel** it only shows, in place of what the gear has in that slot. The override has the same worn slots as the gear.

Clothing is worn like armor, with no armor's statistics: a loadout mostly in clothes is unarmored (its pieces count toward the majority as "none"). Quartermasters sell their faction's equipment for Faction Points. (`item_fits_slot`, `resolve_worn_models`; old saves' cosmetic items become the equipment, or bodysuit, they showed.)

**The one exception: bodysuits.** A bodysuit (`Kind = bodysuit`) is a whole-body model, one of Battlefront II's soldiers, that can't be split into pieces. It's a look only: it goes only in the Cosmetic Override's Bodysuit slot, and shows in place of everything else, gear and override alike. It has no statistics, so what a loadout plays like is always its pieces. No other item is look-only: a new look is a piece of equipment.

## The sets

Galaxies has 285 armor templates but 234 models: crafted, quest, "reinforced" and "insulated" variants reuse a base model. They make **26 sets** (and two fragments, below), most with 9 or 10 pieces. ✅ marks the sets in our catalog: every light, medium and heavy set, one item for each model (a pair of biceps or bracers is one item), 150 pieces in all. Their item ids are `armor_<set>_<slot>` (`armor_katarn_helmet`, `armor_marauder_raider_belt`); a set's second chest is `armor_ubese_jacket` or `armor_marine_chest_rebel` (from the Alliance quartermaster).

The class each set is in comes from our armor professions where they name it, and from Galaxies' own descriptions otherwise ("heavy armor", "lightweight", "light, modern combat"). Galaxies' own category (recon, battle, assault) is on the server, not in the client files we have. ASSUMPTION marks a guess.

### Light / Recon (7)

| Set | Models | Pieces | Notes |
|---|---|---|---|
| Ubese ✅ | `armor_ubese_*` | helmet, shirt or jacket, pants, boots, gloves, bracers, bandolier | Two chest pieces (shirt, jacket); no biceps. The bandolier fills the belt slot |
| Tantel ✅ | `armor_tantel_skreej_*` | 9: no belt | "Padded Tantel" variants reuse the models |
| Scout Trooper ✅ | `armor_scout_trooper_*` | 10 | Empire |
| Bone ✅ | `armor_bone_s01_*` | 9: no belt | Crafted from creature bone |
| Chitin ✅ | `armor_chitin_s01_*` | 9: no belt | Crafted from insect shells |
| Marauder (nomad) ✅ | `armor_marauder_s02_*` | 9: no belt | "Lightweight, cheap, mobile" in Galaxies |
| Mabari Armorweave ✅ | `armor_zam_wesell_*` | helmet, chest, pants, boots, gloves, belt | Zam Wesell's; no biceps or bracers. ASSUMPTION: light (a mercenary's) |

### Medium / Battle (8)

| Set | Models | Pieces | Notes |
|---|---|---|---|
| Padded ✅ | `armor_padded_s01_*` | 10 | |
| Stormtrooper ✅ | `armor_stormtrooper_*` | 10 | Empire. "Reinforced" variants reuse the models |
| Marine ✅ | `armor_marine_*` | 9, plus a backpack | A Rebel chest plate too (`armor_marine_chest_plate_rebel`) |
| Rebel Battle ✅ | `armor_rebel_s02_*` | 9: no belt | Rebellion |
| Bounty Hunter ✅ | `armor_bounty_hunter_*` | 10 | Crafted variants reuse the models |
| Katarn (clone commando) ✅ | `armor_clonetrooper_*` | 10 | Clone Wars. Three paint schemes in Galaxies (Imperial, Rebel, neutral) on one model; ASSUMPTION: medium |
| Marauder (raider) ✅ | `armor_marauder_s03_*` | 9, plus a double bandolier | ASSUMPTION: medium |
| R.I.S. ✅ | `armor_ris_s01_*` | 9: no belt | A high-end crafted set. ASSUMPTION: medium |

### Heavy / Assault (5)

| Set | Models | Pieces | Notes |
|---|---|---|---|
| Composite ✅ | `armor_composite_s01_*` | 9: no belt | "A heavy armor type" in Galaxies |
| Rebel Assault ✅ | `armor_rebel_s03_*` | 9: no belt | Rebellion |
| Shock Trooper ✅ | `armor_shocktrooper_*` | 10 | Empire (Galaxies' assault trooper) |
| Mandalorian ✅ | `armor_mandalorian_*` | 10, with shoes for boots | |
| Marauder (warlord) ✅ | `armor_marauder_s01_*` | 10 | ASSUMPTION: heavy, the third of the three marauder sets |

### Species sets (6) ✅

Galaxies gives Wookiees and Ithorians, whose bodies can't show the standard armor, their own sets: three each, one per class. They aren't items: each piece is a catalog `[species_armor]` section (species, class, slot, model), exported on its species' own body.

| Set | Species | Class | Pieces |
|---|---|---|---|
| Kashyyykian Ceremonial | Wookiee | Light ("some protective value") | chest, leggings, bracers |
| Kashyyykian Black Mountain | Wookiee | Medium ("moderate") | chest, leggings, bracers |
| Kashyyykian Hunting | Wookiee | Heavy ("a high level of protection") | chest, leggings, bracers |
| Ithorian Sentinel | Ithorian | Light (ASSUMPTION) | 7: no belt; biceps and bracers a pair each |
| Ithorian Guardian | Ithorian | Medium (ASSUMPTION) | 7 |
| Ithorian Defender | Ithorian | Heavy (ASSUMPTION) | 7 |

### Bodysuits (10)

Battlefront II's soldiers, worn whole, as looks (above), from their faction's quartermaster: the Stormtrooper (Empire); the Phase I and Phase II Clone, Clone Heavy Trooper, Jet Trooper, ARC Trooper and Clone Commander (Republic); the B1 Battle Droid, B2 Super Battle Droid and IG-100 MagnaGuard (Separatists).

### Left out

- **Nightsister** (one bicep) and **Singing Mountain Clan** (biceps and bracers): fragments of Force-user outfits. They wait with the Jedi decision.
- `armor_marine_backpack`: cosmetic only (no backpack slot).

## Rules

- **Classes are sidegrades, as weapon archetypes are.** Heavier armor protects more and moves slower; no class is better. Within a class, every full set has the same budget; sets differ in which damage types they resist and in a small perk of their own.
- **Species sets are how their species looks armored.** A Wookiee or Ithorian wearing armor of any class shows their own species' set of that class (a Wookiee in heavy armor looks like Kashyyykian Hunting armor), on the slots that set has. Gearing stays the same for every species, and Wookiees and Ithorians finally look armored. It goes piece by piece: each slot shows the species' piece of the class of what's worn there (or of what the Cosmetic Override has there), so mixed classes show mixed sets; clothing, and a slot the species' sets don't have (a Wookiee's helmet), show nothing. Where a Galaxies piece leaves the wearer's skin showing (a `skin_body` surface: the Ceremonial leggings), it's drawn with the wearer's own skin.
- **Faction sets** (Stormtrooper, Scout Trooper, Shock Trooper; Rebel Battle and Assault, the Rebel marine chest), and the bodysuits, sell from their faction's quartermaster, for Faction Points. The Katarn's three paint schemes are one item, painted by faction.
- **One item per model.** Crafted, quest, reinforced and insulated variants become Mk tiers and quality, as weapon variants did.
- **Any piece can be worn as a look:** owning it, you can put it in the Cosmetic Override, so you can wear Mandalorian looks over light-armor statistics.

## Balance: the armor budget ✅

The sets were uneven (a full Stormtrooper kit gave 42% general mitigation, 18% more against lasers and 125 health; a full Ubese kit, 14% and 45 health). Now the budget fixes each class's full kit, and each slot gets its share by the weights the Mk tiers already use (renormalized over the eight worn slots: chest 2/7, leggings 1/5, helmet 1/7, boots 3/35, gloves, biceps, bracers and belt 1/14 each).

| Class | General mitigation | Typed resistance (spread as the set likes) | Health | Mobility |
|---|---|---|---|---|
| None | 0 | 0 | 0 | the Unarmored profession's perks |
| Light | 12% | 10% | +60 | run speed x1.02, stamina regeneration x1.05 |
| Medium | 20% | 12% | +100 | run speed x0.96 |
| Heavy | 28% | 15% | +140 | run speed x0.92, sprint x0.95 |

ASSUMPTION: these are a starting point, to be tuned in play (`armor_budget` in core). Mk tiers scale health on top, as they do now.

A catalog armor item doesn't list its statistics: the loader gives it its slot's share of its class's budget. The item says only how its typed resistance is spread (`Resist = laser:0.6, ballistic:0.4`) and its one small perk, if any (a `Modifier`: a multiplier within 5%, or a little energy). The test `items_test` checks every piece (its Resist adds up, at most one perk) and that a whole Stormtrooper kit is the medium budget.

A set's identity is its typed resistance and that perk: Stormtrooper against lasers (a utility belt's energy), Composite against explosives, Padded against ballistics and explosives, Ubese against electro and bio (a little more jump), Chitin against bio, Bounty Hunter a little of everything, Mandalorian a jump boost, Scout Trooper quieter footsteps (when there's a stealth system), and so on.

## How we build them

### 1. One source of truth: the catalog names the Galaxies template ✅

As for weapons: each armor item names its Galaxies template (`Template = object/tangible/wearables/armor/ubese/shared_armor_ubese_jacket.iff`; biceps and bracers name both sides). `swgtool export-items` follows it to the model, exports the male and female versions with their colors and body-hiding data, and writes `armor/<set>/<piece>_{g}.gltf`. That replaced the hard-coded list of 58 pieces in the exporter (it exports them byte for byte the same). Galaxies names one gender's model; the other is the same name with `_m` and `_f` swapped.

### 2. A contact sheet for armor ✅

`game/tools/armor_sheet.gd`: every set worn whole by a human man and woman from the front and the back, a Wookiee and a Trandoshan (to check what they don't show), one image per set; the bodysuits and the clothing on sheets of their own.

### 3. The budget, then the sets

The class budgets and set identities first, rebalancing the four sets we have ✅; then light, medium and heavy sets ✅ (128 new items, and the pieces the first four sets were missing); then the species sets ✅ (30 pieces; the contact sheet's "species" page shows a Wookiee and an Ithorian in a light, a medium and a heavy set).

Each set's resistances and perk are in the catalog; ASSUMPTION: the ones not named above (Tantel laser and electro, Bone melee and bio, Mabari laser and ballistic with a little acceleration, Marine ballistic and laser, Katarn laser and explosive, the Marauders ballistic and laser, melee and ballistic, melee and ballistic, R.I.S. laser, electro and bio, the Rebel sets laser with ballistic or explosive, Shock Trooper explosive and laser). Scout Trooper boots make sprinting a little cheaper until there's stealth for quieter footsteps.
