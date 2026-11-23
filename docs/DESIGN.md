# Design

## Premise

Battlefront's combat never had a persistent world behind it. Galaxies' world never had good combat. This project joins the two: Battlefront II-style third-person shooting on Galaxies' full-size planets, inside a persistent world with deep customization, crafting, housing, and a player economy.

## Pillars

1. **The world is the map.** Conquest command posts sit on real planet terrain: faction bases, player cities, outposts. The Galactic Civil War becomes literal Battlefront matches over territory players built.
2. **Horizontal progression.** Aim matters in a shooter, so professions widen your options (weapons, gadgets, vehicles, abilities) instead of creating large stat gaps between players.
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
- **Bots:** AI soldiers fill battles, as in Battlefront. They make solo play and testing possible and keep matches full when few players are online.

## Battles and hubs

- **Hubs:** players queue for battles from Galaxies cities, at faction recruiters or war terminals. Mos Eisley is the first hub. Later, player cities can host their own war terminals.
- **Queued matches are played on Battlefront II's maps**, imported from the player's install.
- **Progression of the battlefield:** instanced matches on Battlefront's maps → much later, command-post battles built from Galaxies assets, and live conquest zones in the open world.

## Weapons and loadouts

Galaxies has hundreds of weapons and dozens of armor sets. Battlefront has about five weapons per class. The goal is Galaxies' variety without stat gaps that decide fights before they start.

- **Models from Galaxies, feel from Battlefront:** every weapon is a Galaxies model. Battlefront II defines how it handles, plus its effects and sounds.
- **Archetypes:** each weapon maps to a Battlefront handling type (automatic blaster, burst rifle, sniper, scatter, launcher, etc.). The archetype decides how it feels: fire mode, projectile, overheating.
- **Sidegrades:** weapons within an archetype trade off rate of fire, damage, heat, accuracy, and range within a narrow band. Different, not strictly better.
- **Quality:** crafted quality (later) moves stats slightly within the band. This is where Galaxies-style experimentation lives.
- **Two queue types:**
  - **Standard:** quality is flattened. Only the sidegrade choice matters.
  - **Gear:** quality counts. The Galactic Civil War, and eventually open-world conquest.
- **Three loadouts:** each character keeps up to three, each a full kit. In a match they're the classes you choose from when you spawn. In the world, switching between them takes several seconds.
- **Overheating** from Battlefront stays.
- **Damage types:** ballistic, laser, electro, bio, explosive and melee.
- **Gear slots, not classes:** a loadout is a primary weapon, a sidearm, a secondary (throwables and gadgets, used with their own button), armor, a universal **Deflector Shield (PSG)**, active profession specialization slots, and an optional companion slot, filled from the player's inventory and roster.
- **Any gear can change any character statistic.** Armor mostly sets general damage mitigation, mitigation per damage type, movement speed and health; a heavy weapon could slow its carrier too.
- **Getting gear before crafting exists:** faction quartermasters sell standard-issue gear for faction points earned in battle. Crafted gear supplements it later.
- **Shooting range:** every hub has one for trying weapons before committing to them.

## Professions and skills

- **1–100 per profession:** Every profession levels 1–100 with milestone unlocks at 1 / 25 / 50 / 75 / 100 instead of SWG's 4×4 box grids and fragmented XP pools. See [Professions](PROFESSIONS.md).
- **Uncapped leveling, loadout-scoped power:** One character can level every profession to 100; each loadout carries a fixed number of Active Specialization Slots so combat builds stay balanced.
- **Decoupled armor:** Four armor professions (Unarmored, Light/Recon, Medium/Battle, Heavy/Assault) are completely separate from weapons and specialist professions, while everyone shares a universal Deflector Shield slot.
- Combat, crafting, gathering, companion, social/entertainment, and economy professions are all first-class.
- **Less opaque than the original:** every milestone shows exactly what it grants, and an in-game planner shows full loadout builds before you commit.

## Crafting and economy

- **Spawning resources:** each resource has rated stats (e.g. overall quality, conductivity, decay resistance), spawns in specific areas for a limited time, then depletes.
- **Streamlined UX:** one Unified Multi-Scanner with 3D terrain contours, a dedicated Material Vault with plain-English stat sorting, and a single-screen workbench (auto-fill, 1-step base crafting + optional enhancement slots, visible experimentation odds, Recipe Presets, and work orders/salvage instead of grind-trash).
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
- All ten of Galaxies' playable species. Those that can't wear some gear, as in Galaxies (Wookiees, Trandoshans, Ithorians), get built-in traits in return, so an empty slot isn't a handicap.
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
