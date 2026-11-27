# Crafting

What crafters make, and how, next to the equipment in the item catalog. The
gathering and one-step crafting loop (resources, the Material Vault, presets,
batches, work orders, salvage) is in [PROFESSIONS.md](PROFESSIONS.md); this
covers what gets made: schematics for the whole catalog, Mk tiers, modules
and dyes.

## Who makes what ✅

| Equipment | Crafter | Station | From |
|---|---|---|---|
| Weapons: pistols, carbines, rifles, scatterguns, sniper rifles, melee, two-handed melee, beams and flamers, heavy weapons | Weaponsmith | Weapon & Droid | every neutral weapon |
| Grenades, mines and utility shots | Weaponsmith | Weapon & Droid | the auto laser trap waits for the scout rework |
| Ammunition, weapon modules | Weaponsmith | Weapon & Droid | hand-written schematics |
| Armor | Armorsmith | Armor & Clothing | the sets armorsmiths make in Galaxies (below) |
| Deflector Shields, armor and shield modules | Armorsmith | Armor & Clothing | every neutral shield |
| Clothing | Tailor | Armor & Clothing | Galaxies' clothing (below) |
| Speeders | Droid Engineer | Weapon & Droid | every speeder |
| Mounts, bio modules | Bio-Engineer | Food & Chemical | every mount |
| Food, medicine, camp kits, droid parts | Chef, Doctor, Architect, Droid Engineer | their own | hand-written schematics |

Not crafted:

- **Faction issue:** stormtrooper, scout trooper, shock trooper, marine and Rebel armor; faction weapons and shields. These come from their quartermasters.
- **The Katarn:** quartermasters sell it in their paint ([ARMOR.md](ARMOR.md), Paint).
- **Bodysuits:** cosmetic only, from quartermasters.
- **Species looks:** not items at all.

**Crafted armor sets:**

| Class | Sets |
|---|---|
| Light | Ubese, Tantel, Bone, Chitin, Nomad Marauder, Mabari Armorweave |
| Medium | Padded, Bounty Hunter, Raider Marauder, R.I.S. |
| Heavy | Composite, Mandalorian, Warlord Marauder |

## Schematics ✅

`game/crafting/schematics.ini` has two kinds of entries.

**Hand-written `[schematic]`s** cover:

- ammunition;
- modules;
- food, medicine, camp kits and droid parts;
- the first few weapons, whose ids tests and presets use.

**`[family]` sections** make the rest. Each family makes one schematic for every catalog item it matches:

- **Matching:**
  - `Kind`, then any of `Archetype`, `ArmorClass` or `ItemSlot`.
  - ids that match `Match` globs and not `Exclude` globs.
  - `Faction`: `neutral` unless the family says otherwise.
- **Precedence:** an item that a `[schematic]` (or an earlier family) already makes keeps that schematic.
- **The schematic it makes:**
  - id `sch_<item id>`, named after the item, filed under the family's `Group`;
  - the family's level, materials, weights, XP and module slots.
- **Armor costs:** with `ScaleBySlot = yes`, a piece costs its slot's share of a chest's materials, the same share as its stats (gloves cost a quarter of a chest).

The families need the catalog, which the file names in `[catalog] Path`. So a new catalog item that fits a family is craftable with no schematic written for it.

Levels:

| Group | Level |
|---|---|
| Every weapon, armor class and Deflector Shield | 1 |
| Shirts, trousers and skirts | 1 |
| Footwear, gloves, belts and hats | 5 |
| Jackets and vests | 10 |
| Speeders, robes | 30 |
| Mounts | 40 |

Weapons and armor aren't gated by level (playnotes, 2026-11-26): what a crafter can make grows with the blueprints they find in the world and buy from reputation vendors (below), not with their level. Until those sources exist every weapon type and armor class is open at level 1. Ammunition, modules, food, medicine and camp kits keep their levels.

ASSUMPTION: a starting point, to tune in play.

## Blueprints ✅

A schematic can be hidden until learned:

- **Marking:** `Blueprint = yes` on a `[schematic]` or a `[family]` (all of its schematics), or `BlueprintMatch = <id globs>` on a family (only the matching items: the exotic ones).
- **Learning:** a blueprint is a supply with `Teaches = <schematic id>`. Using it from the inventory teaches it for good and spends it. Known blueprints are kept per character (`character_extras` "blueprints") and sent in `CraftingSync`.
- **Effect:** a blueprint schematic you don't know isn't listed at the workbench, and the server refuses to make it.
- **Plan:** in time more than half the weapon and armor types come from blueprints (loot drops, reputation vendors). No schematic is marked yet: none moves out of the default pool until it has a drop source somewhere.

## Mk tiers ✅

A crafted weapon, armor piece or Deflector Shield can be made at any Mk up to its crafter's level's:

- **Ladder:** Mk I at 10 up to Mk X at 100 (`craft_max_mark`), the same tiers loot and vendors have ([DESIGN.md](DESIGN.md), Power curve).
- **Materials:** each mark costs a quarter more (`mark_material_amount`; Mk X is 3.5 times Mk 0).
- **XP:** each mark gives 15% more (`mark_craft_xp`).
- **Workbench:** offers the highest tier first, and lowers it for a lower-level buyer.
- **No tiers:** clothing, speeders, mounts and consumables have none (`item_takes_marks`).

## Grades ✅

A crafted weapon, armor piece or Deflector Shield rolls its grade ([DESIGN.md](DESIGN.md), Grades): Flawed, Standard, Fine or Exceptional. A craft never makes a Legendary item.

- **Odds:** `CraftingBook::grade_odds` places the roll's center from Flawed (0) to Exceptional (3), and spreads it as a bell (0.75 wide). The center starts at 0.3 and goes up with:
  - **materials:** +1.2 × their score, 0 to 1 (their stats, as the schematic weighs them)
  - **crafter level:** +0.6 from level 1 to 100, and +0.15 more from the Lv 75 Experimentation milestone
  - **modules:** +0.2 each, up to 2
  - **Experimentation Focus:** +0.8 at 100%

  A level-1 crafter on average materials makes mostly Standard (about 26% Flawed, 54% Standard, 19% Fine, 1% Exceptional). A master on great materials with two modules and full focus makes mostly Exceptional. The workbench shows the four odds.
- **Experimentation Focus costs materials:** 0 to 100% in steps of 10, multiplying every slot's amount by up to 10 (`focus_material_mult`: 1 + 9 × focus), on top of the mark's (`craft_material_amount`). It starts at 0. It does nothing, and costs nothing, on an output without a grade.
- **Batches:** each item rolls its own grade. The notice lists them ("Crafted 3x ... (1 Exceptional, 2 Fine)").

## Modules ✅

Crafted into an item at the workbench: up to its schematic's module slots (0 to 2). Each module betters the grade odds (Grades, above). One with a `ModuleFor` also tunes the item: something better for something worse, within the item's budget. The owned copy lists its modules in its `color_json` (`"mods"`), and `ItemCatalog::tuned` applies them wherever its stats are worked out (loadouts, tooltips).

| Module | Fits | Tuning | Crafter |
|---|---|---|---|
| Precision Barrel | ranged weapons | spread x0.85, rate of fire x0.94 | Weaponsmith |
| Heat Sink | ranged weapons | heat a shot x0.8, damage x0.935 | Weaponsmith |
| Extended Power Cell | ranged weapons | clip x1.3, reload x1.15, damage x0.98 | Weaponsmith |
| Weighted Blade | melee weapons | damage x1.05, swings x0.95 | Weaponsmith |
| Reinforced Armor Plating | armor | a quarter of the typed resistance to ballistic and melee | Armorsmith |
| Blast Padding | armor | a quarter to explosive | Armorsmith |
| Ballistic Armor-Weave Lining | armor | a quarter to laser and electro | Tailor |
| Organic Armor Tissue | armor | a quarter to bio | Bio-Engineer |
| Capacitor Bank | Deflector Shields | capacity x1.15, recharge x0.85 | Armorsmith |
| Stim Additive | anything (medicine and food) | nothing yet: medicine has no grade | Bio-Engineer |

**Budget:**

- **Weapons:** a weapon module never raises a weapon's sidegrade score ([WEAPONS.md](WEAPONS.md), Balance), alone or paired. `crafting_test` checks every weapon against every pair.
- **Armor:** an armor module moves typed resistance but leaves the class budget whole (`module_resist_share`).

A module that doesn't fit the schematic's output is refused, not wasted.

## Dyes ✅

**Who:** a Tailor dyes clothing (and a bodysuit) and an Armorsmith dyes armor, at an Armor & Clothing station.

**What a dye is:**

- The colors come from the piece's own model palettes: the Dye tab offers each of its customization variables, with the palette's first 96 entries.
- A dye costs two units of a chemical and gives a little XP.

**How it's stored:** the owned copy keeps its dye in its `color_json` (`"dye"`, a query string like a paint's). `resolve_worn_models` writes it after the piece's paint, so a dye wins over a paint scheme. Removing the dye restores the piece's own colors (or its paint).

## The Tailor's clothing ✅

The Tailor has 152 pieces of Galaxies' clothing to make, in addition to the original four:

- **What's included:** every shirt, jacket, vest, apron, pair of trousers or skirt, robe, pair of boots or shoes, pair of gloves, belt, bandolier and hat whose model both genders have.
- **What's left out:** quest, faction, Jedi and species pieces, and female-only models (gowns and most skirts).
- **Selling:** none of them is sold at a vendor (`CostCredits = 0`), so only Tailors make them.
- **Robes:** Galaxies' robes and cloaks are worn over trousers, like a coat. Its long robes on the dress pattern take the trousers' slots, so they cover the legs (`Covers = leggings`): what's worn on the legs isn't drawn under one. Its stats still count, so a robe in the cosmetic override can go over armor.

## Open

- Work orders for the new families (only the first five exist).
- Female-only clothing (gowns, skirts) wants a gender rule for items.
- Tuning: levels, material amounts and the module trade-offs, in play.
