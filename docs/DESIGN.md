# Design

## Premise

Battlefront's combat never had a persistent world behind it. Galaxies' world never had good combat. This project joins the two: Battlefront II-style third-person shooting on Galaxies' full-size planets, inside a persistent world with deep customization, crafting, housing, and a player economy.

## Pillars

1. **The world is the map.** Conquest command posts sit on real planet terrain: faction bases, player cities, outposts. The Galactic Civil War becomes literal Battlefront matches over territory players built.
2. **Gear matters, within a flat curve.** Professions widen your options (weapons, gadgets, vehicles, abilities), and gear makes you stronger, but only so far: a player in maxed level-100 gear is about twice a fresh level-1 character (double the health, double the damage), not the hundredfold gap of a classic MMO. Aim still decides most fights.
3. **Galaxies depth, transparently.** Skill trees, spawning resources with stats, crafting experimentation, and a player economy, with the numbers visible: resource stat tooltips, shown experimentation odds, a skill planner in the game.
4. **Housing and cities matter.** Placed structures are real geography. In faction zones they can be defended or contested.
5. **Original assets, modern light.** Meshes and textures stay as shipped. Lighting, atmosphere, and rendering are modern.

## Combat

- Third-person shooter controls and feel, based on Battlefront II: sprint, roll, crouch, zoom, grenades, vehicles.
- Server-authoritative hit detection with lag compensation.
- **No character classes.** Nobody is locked into a role. What a player can do in a fight comes from the gear they bring: a primary weapon, a sidearm, a secondary (throwables and gadgets), and armor. Battlefront's classes survive only as play styles that gear (and, later, profession skills) make possible. Examples of where those styles lead:

  | Battlefront style | Profession direction |
  |---|---|
  | Soldier | Rifleman / Marksman |
  | Heavy | Commando |
  | Sniper | Marksman → Rifleman specialization |
  | Engineer | Combat Engineer (repair, turrets, mines) |
  | Pilot | Pilot / Vehicle operator |
  | Officer / Marine | Squad Leader |
  | Special classes | Bounty Hunter, Smuggler, etc. |

- **Open-world combat:** PvE (creatures, NPC factions) and opt-in faction PvP.
- **Conquest battles:** command-post battles fought in the world over contested territory, plus instanced battles (for example 32v32) launched from the world, whose results feed back into it.
- **Bots:** AI soldiers fill battles, as in Battlefront. They make solo play and testing possible and keep matches full when few players are online. Their AI always plays at Hard; **gear is the lever on their strength**. Matchmaking evens sides by gear: a team facing a player in level-100 gear gets a bot in level-100 gear.

## Battles and hubs

- **Hubs:** players queue for battles from Galaxies cities, at faction recruiters or war terminals. Mos Eisley is the first hub. Later, player cities can host their own war terminals.
- **Queued matches are played on Battlefront II's maps**, imported from the player's install.
- **Progression of the battlefield:** instanced matches on Battlefront's maps → much later, command-post battles built from Galaxies assets, and live conquest zones in the open world.

## Weapons and loadouts

Galaxies has hundreds of weapons and dozens of armor sets. Battlefront has about five weapons per class. The goal is Galaxies' variety, with gear that matters but never decides a fight on its own.

- **Models from Galaxies, feel from Battlefront:** every weapon is a Galaxies model. Battlefront II defines how it handles, plus its effects and sounds.
- **Archetypes:** each weapon maps to a Battlefront handling type (automatic blaster, burst rifle, sniper, scatter, launcher, etc.). The archetype decides how it feels: fire mode, projectile, overheating.
- **Sidegrades:** at the same level, weapons within an archetype trade off rate of fire, damage, heat, accuracy, and range. Different, not strictly better: a sidegrade budget formula holds every weapon to the same total ([Weapons](WEAPONS.md#balance-the-sidegrade-budget)).
- **Power curve:** gear has level requirements, and higher-level gear is stronger, but the whole range from level 1 to level 100 spans about 2x in each stat (health, damage and so on). An item's grade (below) moves its stats a little within its level, from -8% to +16%.
- **Mk tiers:** every item exists at every level without its own catalog entry. The plain E-11 is level 1; an E-11 Mk I needs level 10, up to Mk X at level 100. Each mark adds 10% of the item's base power: a weapon's damage; a Deflector Shield's capacity and recharge; and, for worn gear, a share of the character's maximum health (the Deflector Shield 30%, chest 20%, leggings 14%, helmet 10%, the rest less). A full Mk X kit doubles health. A mark needs the level of the item's own profession (Rifles for a rifle, Light Armor for light armor), or the character's highest level for a Deflector Shield. Sidegrade stats (rate of fire, spread, mitigation) don't change with the mark. Loot tables choose which marks drop (`GearMarkMin`, `GearMarkMax`).
- **Grades:** every weapon, armor piece and Deflector Shield has a grade, which scales its flat stats (damage; health, resistances, shield capacity and recharge) and shows as its color:

  | Grade | Stats | Color | From |
  |---|---|---|---|
  | Flawed | -8% | white | a crafting roll, loot |
  | Standard | base | green | vendors, quartermasters, quests; crafting, loot |
  | Fine | +8% | blue | crafting, loot |
  | Exceptional | +16% | purple | crafting, the best loot |
  | Legendary | not decided (+16% until then) | orange | never crafted: endgame and horizontal-progression items later (an item's `Grade = legendary`) |

  Every other item (junk, supplies, ammo, modules, clothing, vehicles) has no grade and shows white. Loot tables weigh the grades they drop (`GearGrades = 30 40 30 0`: Flawed, Standard, Fine, Exceptional). A lootable corpse shows a beam of light in its best item's color.
- **Matches count gear:** there is one queue, and gear counts in it. Bots' gear balances the sides (see Bots).
- **Open-world PvE widens the gap:** in the open world, a silent level-based modifier scales damage and toughness by the difference in level between a player and the creatures and NPCs they fight, so a level-1 character can't take on endgame content, even with good aim. It never applies between players: open-world PvP uses the same 2x curve as matches.
- **Five loadouts:** each character keeps up to five, each a full kit. In a match they're the classes you choose from when you spawn. In the world, switching between them takes several seconds.
- **Overheating** from Battlefront stays.
- **Damage types:** ballistic, laser, electro, bio, explosive and melee.
- **Gear slots, not classes:** a loadout is a primary weapon, a sidearm, a secondary (throwables and gadgets, used with their own button), armor, a universal **Deflector Shield (PSG)**, and an optional companion slot, filled from the player's inventory and roster. What the character has learned from trainers applies to every loadout.
- **Any gear can change any character statistic.** Armor mostly sets general damage mitigation, mitigation per damage type, movement speed and health; a heavy weapon could slow its carrier too. Gear can also change healing received, ability cooldowns, and resistance to debuffs.
- **What a weapon can vary:** damage and type, rate of fire, heat, spread, range and falloff, clip and reload, bolt speed, headshot multiplier, critical hit chance and multiplier, damage to vehicles and to Deflector Shields, swap time, recoil, aim-down-sights speed, and an on-hit effect. The keys are listed in `core/include/swbfo/core/items.h`.
- **Ammo carries effects:** burning (damage over time), chilled (slowed) and ionized (no shield recharge) come from the ammo equipped in a loadout's ammo slot; every gun firing that ammo applies its effect. An effect refreshes rather than stacks, and debuff resistance shortens it. Later, a weapon or profession ability could treat standard ammo as an effect ammo.
- **Plain ammo is not managed:** a gun with no effect ammo equipped (an empty ammo slot, or old Standard Tibanna Cells / Standard Heavy Warheads) never runs dry in the open world, and in a Battlefront match carries that weapon's own Battlefront load, topped up by ammo droids. Only effect and sidegrade ammo is spent from its stack (out in the world, a stack run dry falls back to plain at once; in a match, at the next respawn, and the stack counts as used up). Vehicle and turret guns never run dry. Grenades, mines and medpacks keep their counts.
- **Getting gear before crafting exists:** faction quartermasters sell standard-issue gear for faction points earned in battle. Crafted gear supplements it later.
- **Shooting range:** every hub has one for trying weapons before committing to them.

## Professions and skills

- **1–100 per profession:** Every profession levels 1–100 instead of SWG's 4×4 box grids and fragmented XP pools. Weapon and armor professions offer a choice of two perks at 5 / 25 / 45 / 65 / 85; the rest teach milestones at 1 / 25 / 50 / 75 / 100. See [Professions](PROFESSIONS.md) and [Perks](PERKS.md).
- **Uncapped leveling, capped learning:** One character can level every profession to 100, but knows at most 10 perks and milestones at once, across every profession, like mastering two trees in Galaxies. Giving one up at its trainer is free, and so is learning it again; the trips between trainers keep rebuilds slow.
- **Decoupled armor:** Four armor professions (Unarmored, Light/Recon, Medium/Battle, Heavy/Assault) are completely separate from weapons and specialist professions, while everyone shares a universal Deflector Shield slot.
- Combat, crafting, gathering, companion, social/entertainment, and economy professions are all first-class.
- **Less opaque than the original:** every milestone shows exactly what it grants, and an in-game planner shows full loadout builds before you commit.

## Crafting and economy

- **Spawning resources:** each resource has rated stats (e.g. overall quality, conductivity, decay resistance), spawns in specific areas for a limited time, then depletes.
- **Streamlined UX:** one Unified Multi-Scanner with 3D terrain contours, a dedicated Material Vault with plain-English stat sorting, and a single-screen workbench (auto-fill, 1-step base crafting + optional enhancement slots, visible experimentation odds, Recipe Presets, and artisan terminal orders/salvage instead of grind-trash).
- Items are mostly player-crafted. Loot supplements crafting instead of replacing it.
- Creatures and NPCs drop credits, junk to sell to junk dealers (as in Galaxies), and now and then gear.
- Item decay and repair keep demand alive.
- Vendors and player-run shops.

## Housing and cities

- Place Galaxies structures (houses, shops, guild halls, factories, harvesters) directly on the planet terrain.
- Full interior decoration with free placement.
- Player cities form from clusters of structures, with civic buildings and governance.
- Cities in faction zones can contain command posts and be contested.

## Character customization

- **Every character is a Galaxies model.** Players and AI soldiers alike use Galaxies species bodies and armor, animated with Battlefront II's shooter animations.
- Galaxies-style character creation: species, body shape sliders (blend shapes), hue/palette choices for skin, hair, and markings.
- All ten of Galaxies' playable species. Every species wears every armor slot for its statistics, so gearing is the same for all. Where a body can't show some armor, as in Galaxies (Wookiees, Trandoshans, Ithorians), that only affects what shows. Each species has small built-in traits.
- **Species weapons** (bowcasters, Wookiee and Trandoshan weapons) are locked to their species and are a little stronger than common weapons in return. See [Weapons](WEAPONS.md).
- Armor and clothing appearance as a separate layer from stats (wear what you like).

## World

- Galaxies planets at full scale (16 × 16 km each), starting with Tatooine.
- Day/night cycle, weather, creatures, NPC settlements.
- Battlefront props, vehicles, and structures appear across the planets as battle sites and landmarks where they make sense.

## Open questions

- Space: Battlefront II's space battles are planned as queued battles ([Roadmap](ROADMAP.md) Stage 8). Open-world space flight is out of scope.
- Jedi / Force users: how they fit a shooter without breaking balance. Undecided.
- Death and penalties: cloning, item decay on death, PvP loss rules.
- Population target per planet and per server.
