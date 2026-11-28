# Crafting

What crafters make, and how, next to the equipment in the item catalog. The
gathering and one-step crafting loop (resources, the Material Vault, presets,
batches, artisan terminal orders, salvage) is in [PROFESSIONS.md](PROFESSIONS.md); this
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

Weapons and armor aren't gated by level (playnotes, 2026-11-26): what a crafter can make grows with the blueprints they buy from city quartermasters (below), not with their level. A new weaponsmith or armorsmith makes only the basics. Ammunition, modules, food, medicine and camp kits keep their levels.

ASSUMPTION: a starting point, to tune in play.

## Blueprints ✅

A schematic can be hidden until learned:

- **Marking:** `Blueprint = yes` on a `[schematic]` or a `[family]` (all of its schematics), or `BlueprintMatch = <id globs>` on a family (only the matching items: the exotic ones).
- **Learning:** a blueprint is a supply with `Teaches = <schematic id>, ...`. Using it from the inventory teaches them for good and spends it (an armor set's blueprint teaches every piece). Known blueprints are kept per character (`character_extras` "blueprints") and sent in `CraftingSync`.
- **Effect:** a blueprint schematic you don't know isn't listed at the workbench, and the server refuses to make it. Crafting orders never ask for one.
- **What's marked:** every weapon, armor and Deflector Shield family (and the hand-written EE-3 and Energy Deflector) is `Blueprint = yes`, but a basic item's schematic never is (below). Everything else (ammunition, modules, clothing, consumables, speeders, mounts, the jump pack) is open as before.

### The basics

One item in each crafting group stays craftable from level 1 by anyone holding the licence. They're listed in `game/items/city_quartermasters.ini` (`[basic] Items = <id globs>`, `ItemDef::basic`):

| Group | Basic |
|---|---|
| Pistols | D-18 Blaster Pistol |
| Carbines | Laser Carbine |
| Rifles | Light Laser Rifle |
| Scatterguns | Spraystick |
| Sniper Rifles | DLT-20A Longblaster |
| Melee Weapons | Vibroblade |
| Two-Handed Melee | Metal Staff |
| Beams & Flamers | Flame Thrower |
| Heavy Weapons | Plex Rocket Launcher |
| Grenades & Mines | C-22 Frag Detonator |
| Light Armor | Bone |
| Medium Armor | Padded |
| Heavy Armor | Composite |
| Deflector Shields | Personal Shield Generator |

Where they're bought:

- **Gear vendors** sell the basics, and no weapon, armor or shield a city blueprint teaches (those are crafters'). What no crafter makes, like the Katarn, they still sell, and supplies, ammunition and modules as before (`gear_vendor_sells`).
- **The bazaar's general store:** every bazaar terminal always lists the basics, sold by "Galactic Supply" at their vendor price. It never sells out and pays nobody, and its sales aren't counted in price trends. Its listings aren't saved; their ids start at `general_store_listing_base`. A player listing one for less sells first. It's a stopgap until there are enough players posting goods.

ASSUMPTION: the picks are the plainest, cheapest generic item of each group (the starter schematics where one existed). Change them in that one file.

## City quartermasters ✅

Every town has a reputation and a quartermaster: the 10 planet hubs and their 34 towns, Ewok villages and outposts included.

**Reputation:**

- Kept with the factions' (`faction_reputation`), keyed `town:<id>`. A town's id is its hub or town file's stem (`anchorhead`, `lok_imperial_outpost`).
- Same seven standings as the factions (Hated to Exalted, Friendly at 3000).
- Nothing lowers it yet.

**How it's earned:**

| Source | Town | Amount |
|---|---|---|
| A quest | the town nearest its giver | 150 + 50 x its level |
| A mission | the town nearest its target (its lair or camp) | 75 + 15 x its level |
| A mission terminal's contract | the town of the terminal that offered it | 60 |

The server knows each planet's towns (`GameServer::set_towns`, `town_at`: the nearest by its edge).

ASSUMPTION: the amounts are a starting point, to tune in play. A town's quest chain earns most of Friendly; missions do the rest.

**The quartermaster:**

- A `[quartermaster]` with no `Faction` in a town's file is that town's (`Quartermaster::town`, `town_name`).
- It sells only the town's blueprints, for credits, each at a standing with the town (`buy_city_blueprint`).
- Each stands 3 m to the right of the town's quest giver (or its bazaar terminal, where it has no quest giver), dressed like the town's crowd. Lok's Pirate Outpost and Endor's Research Outpost already had quartermasters (Jessa Tarn, Dessa Rook), so those NPCs are the city quartermasters there.
- Its screen lists the blueprints from the lowest standing up, with the town's standing bar. Hovering one shows what it makes. A blueprint you know is marked Learned.

**The blueprints** (`game/items/city_quartermasters.ini`):

- Each `[blueprint]` (Town, Rank, Items = id globs, Name, Cost) makes an item `bp_<item id>` (a set's: `bp_armor_ubese`).
- 116 of them teach every weapon, armor set and shield that isn't a basic, each exactly once (`crafting_test` checks this). Every town sells two to seven.
- Stock is themed by place: Tusken rifles at Anchorhead, Jawa ion guns at Mos Taike, CorSec at Coronet, Trandoshan gear at Lok's Pirate Outpost, Massassi blades and the Sith sword on Yavin 4, Ewok crossbows in the Ewok villages, the marauder sets at Endor's Smuggler Outpost, Wookiee goods at Dathomir's Trade Outpost, the Nightsister lance at the Imperial Prison.
- **Cost by standing** (`city_blueprint_cost`; an armor set's is twice):

  | Standing | Credits |
  |---|---|
  | Neutral | 800 |
  | Friendly | 1,500 |
  | Honored | 4,000 |
  | Revered | 9,000 |
  | Exalted | 20,000 |

  A blueprint's standing follows its item's price and how exotic it is. Most towns have one at Neutral or Friendly; the frontier outposts (Yavin 4, Dathomir's Science Outpost and Imperial Prison, Dantooine's Imperial Outpost, and a few others) start at Honored or higher.

ASSUMPTION: the standings, prices and which town sells what are a starting point, to tune in play.

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
  - **crafter level:** +0.6 from level 1 to 100, and +0.15 for each Experimentation perk ([Perks](PERKS.md#gathering-and-crafting)); a Masterwork perk doubles the Exceptional odds
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

- City quartermasters' spots are 3 m off each town's quest giver (or bazaar terminal), a measured offset rather than a placement checked in the city; some may stand in a wall or off an Ewok platform. Check each in play and move it.
- Blueprints from loot drops, and city reputation from more sources (supplying the quartermaster, hunting nearby).
- Female-only clothing (gowns, skirts) wants a gender rule for items.
- Tuning: levels, material amounts and the module trade-offs, in play.
