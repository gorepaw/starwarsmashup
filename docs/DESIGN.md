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
- Battlefront's classes map onto Galaxies-style combat professions. Examples:

  | Battlefront role | Profession direction |
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

## Professions and skills

- Galaxies-style skill trees: novice → four branches → master, with a skill point budget, so characters are hybrids built from multiple professions.
- Combat, crafting, social/entertainment, and gathering professions are all first-class.
- **Less opaque than the original:** every skill shows exactly what it grants, and an in-game planner shows full builds before you commit.
- Respec is possible but has a cost (time or credits), so builds still mean something.

## Crafting and economy

- **Spawning resources:** each resource has rated stats (e.g. overall quality, conductivity, decay resistance), spawns in specific areas for a limited time, then depletes.
- Survey → harvest → craft → experiment. Experimentation odds and stat contributions are shown.
- Items are mostly player-crafted. Loot supplements crafting instead of replacing it.
- Item decay and repair keep demand alive.
- Vendors and player-run shops.

## Housing and cities

- Place Galaxies structures (houses, shops, guild halls, factories, harvesters) directly on the planet terrain.
- Full interior decoration with free placement.
- Player cities form from clusters of structures, with civic buildings and governance.
- Cities in faction zones can contain command posts and be contested.

## Character customization

- Galaxies-style character creation: species, body shape sliders (blend shapes), hue/palette choices for skin, hair, and markings.
- Armor and clothing appearance as a separate layer from stats (wear what you like).

## World

- Galaxies planets at full scale (16 × 16 km each), starting with Tatooine.
- Day/night cycle, weather, creatures, NPC settlements.
- Battlefront maps and props become landmarks and battle sites on the planets where they make sense.

## Open questions

- Space flight: out of scope for now.
- Jedi / Force users: how they fit a shooter without breaking balance. Undecided.
- Death and penalties: cloning, item decay on death, PvP loss rules.
- Population target per planet and per server.
