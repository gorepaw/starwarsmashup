# Roadmap

Battles come first, the world after. A match has clear boundaries and can be tested. The persistent world is the hardest part, so it comes last and grows out of working battles.

| Stage | Focus | Outcome |
|---|---|---|
| [Phase 0](PHASE0.md) | Feasibility spikes | The tech works |
| 1 | Battlefront match queued from a Galaxies city | The core loop is fun |
| 2 | Galaxies weapons and equipment | Loadouts give players something to chase |
| 3+ | World systems | The planets fill up |

The battlefield evolves alongside: **instanced match on a chunk of Galaxies terrain → more and larger battlefields → live conquest zones in the open world.**

---

## Stage 1: A Battlefront match from a Galaxies city

### Goal

Walk around Mos Eisley, queue at a recruiter, load into a 32v32 conquest match on Tatooine terrain (bots filling empty slots), play to the end, and return to the city with the results.

### Decisions

- **Battle maps are built on Galaxies terrain**, not imported Battlefront levels. A battlefield is a bounded chunk of a planet with command posts, Galaxies structures, and Battlefront props. That way we never need an importer for Battlefront's level/world format, only for its characters, weapons, vehicles, props, effects, and sounds.
- **Bots are part of Stage 1.** They make solo testing possible and fill matches when few players are online. They use the same input/command path as players.
- **The match server is the first real server.** It's the "instance server" from the [architecture](ARCHITECTURE.md#networking). The hub can run locally with one player for now.

### Tasks

**Hub**
- [ ] Mos Eisley region loaded as a walkable hub. It's walkable terrain with the cantina; the rest of the city's buildings aren't placed yet.
- [x] Recruiter / war terminal interaction that opens the queue (`game/hubs/mos_eisley.ini`). Stand at the terminal and choose a side. It's drawn as a placeholder pillar for now.
- [x] Queue → match handoff (hub sends player and faction to the match server): when the first player has waited 10 s, the hub (`core::Hub`) launches a battle server with bots filling both teams and transfers the queued players to it, each with a ticket the battle server checks. Later players join a battle that's still on.
- [x] Return from match to hub with results: after the results screen everyone is sent back, to where they stood, and greeted with how it went ("Victory at Mos Eisley Outskirts: 3 kills, 1 deaths, 2 captures"). Rewards come in Stage 2.

Hub notes:
- Battle servers run in the hub's process, each on its own UDP port. Moving them to their own processes or machines keeps the same messages: tickets in, results out.
- Hub and battles must use the same terrain for now (clients keep one world). Battlefield files will name their terrain when battles move elsewhere on the planet.

**Match server**
- [x] Match lifecycle: warmup → live → end → results, then the next match. Warmup is practice: deaths are free and posts can't change hands. Results show in the game; sending them back to the hub comes with the handoff.
- [x] Battlefield definition format (terrain bounds, command posts, spawns, out-of-bounds): an INI file (`core::load_battlefield`) with teams and their classes, posts, tickets and timings. Vehicle spawns come with vehicles.
- [x] First battlefield: `game/battlefields/mos_eisley_outskirts.ini`, five command posts in the basin east of the cantina, up to the mesa slope.
- [x] Conquest rules (`core::Conquest`): posts are neutralized and captured by soldiers standing in them (Battlefront's NeutralizeTime 12 s and CaptureTime 10 s, read from its command-post class), and frozen while contested. Each death costs a ticket, and holding more posts bleeds the other team. A team wins when the other runs out, or by holding every post for 20 s.
- [x] Class selection and spawning at owned command posts. Leaving the battlefield kills after 10 s.

**Combat**
- [ ] Third-person shooter controller with Battlefront feel: sprint, roll, crouch, zoom. Sprint (with energy), jump and over-the-shoulder aiming are done; roll, crouch and zoom aren't.
- [x] Projectile weapons with clips and overheating: blaster bolts fly at their real speed with gravity. Stats come from the player's own Battlefront II files (`bf2tool export-soldier`): the Imperial rifleman's E-11 (60 damage, a shot every 0.166 s, 50-round clips) and pistol (overheats).
- [x] Grenades and explosives: thermal detonators (right mouse) bounce and explode on a fuse. Rockets accelerate and explode on impact, and mortar shells lob and burst on contact. Explosions do full damage within an inner radius, falling to nothing at the outer one (the thermal detonator: 450 within 4.5 m, nothing past 6.5 m). Soldiers carry the ammo Battlefront gives them: spare clips, 2–4 grenades, 7 rockets.
- [x] Health, damage, death, respawn (400 health, respawn after 5 s at a spawn point)
- [x] Lag-compensated hit detection: the server rewinds targets to where the shooter saw them (up to 0.5 s). In tests, a shooter 100 ms from the server lands 7 of its shots on a strafing target with rewinding and none without.
- [x] 4–5 classes with fixed kits (loadouts come in Stage 2): rifleman, rocketeer, sniper, engineer (shotgun: 8 pellets a shot) and, for the Empire, the officer (sonic blaster and mortar). The Rebel officer's incinerator and buffs, and every class's mines, detpacks, turrets and droids, aren't supported yet.

Combat notes:
- In a match, teammates can't hurt each other (`ServerConfig::friendly_fire`). Outside one, everyone is hostile.
- Spread uses the weapon's documented values, but how Battlefront combines `SpreadPerShot`, `SpreadThreshold` and `SpreadLimit` is a guess (marked ASSUMPTION in `swm/core/combat.h`). So are a grenade's fuse (an unnamed ordnance property), explosions hurting the thrower, and the linear falloff. Grenades bounce as if off the ground even from walls, explosions aren't blocked by walls, and they don't push. Ammo pickups, headshots and firing animations aren't in yet.
- Bolts are cheap on the server (a position and velocity, tested as a line segment each tick). How they look is up to each client: color, length and width come from the weapon's ordnance, scaled by graphics settings.

**Vehicles**
- [ ] One ground vehicle (e.g. speeder bike)
- [ ] One armored vehicle (walker or tank)
- [ ] Vehicle spawns tied to command posts

**Bots**
- [ ] Navigation on Galaxies terrain. For now bots run straight and jump when stuck.
- [x] Objective selection: attack and defend command posts (`core::Bot`): spawn at the post nearest the fighting, head for the nearest post the team doesn't hold (some go elsewhere, so a team spreads out), guard one when the team holds them all.
- [ ] Combat behaviors: engage, take cover, retreat. Bots engage (line of sight, leading the target, sidestepping); they don't take cover or retreat.
- [ ] Vehicle use (basic)
- [ ] Difficulty settings (`core::BotSkill` has aim error, reaction time and range; not exposed yet)

Bots play through a `GameClient`, with a player's controls and view. `swm_server --bots N` connects them over UDP from the server process, and a whole match runs in `match_test`.

**Presentation**
- [ ] HUD: health, ammo/heat, command-post status, tickets, minimap. All but the minimap, in the dev client (`net_demo.gd`).
- [x] Scoreboard and end-of-match screen (dev client: kills, deaths and captures per team)
- [ ] Battlefront audio: weapons, voice-overs, music

### Exit criteria

A full 32v32 conquest match with bots on Tatooine terrain, launched from and returning to Mos Eisley, that is fun to play solo.

---

## Stage 2: Weapons and equipment

### Goal

Galaxies' weapon and armor library becomes loadouts you choose in the hub and take into matches, balanced for a shooter. See [Design: Weapons and loadouts](DESIGN.md#weapons-and-loadouts).

### Tasks

**Data and items**
- [ ] Weapon archetype definitions (handling, fire mode, projectile, heat)
- [ ] Map Galaxies weapons to archetypes, with sidegrade stat profiles
- [ ] Armor classes and a simplified damage type model (2–3 types)
- [ ] Item definitions (data files) separate from item instances (persistent ID + stat values)
- [ ] Persistence for inventory and loadouts (SQLite via the persistence interface)

**Hub infrastructure**
- [ ] Inventory UI
- [ ] Equipment / paperdoll UI (appearance separate from stats)
- [ ] Loadout editor with per-class slots
- [ ] Shooting range in the city for testing weapons
- [ ] Faction quartermaster: standard-issue gear for faction points
- [ ] Squads / groups and squad queueing

**Match integration**
- [ ] Match server receives authoritative loadouts from the hub (never trusts the client)
- [ ] Standard queue (quality flattened) and gear queue (quality matters)
- [ ] Rewards: experience, faction points, currency

### Exit criteria

Choose a weapon at the quartermaster, try it at the range, put it in a loadout, take it into a match, and earn faction points to buy the next one.

---

## Stage 3+: World systems

Added in order of how directly they feed the battles. Each is a streamlined, transparent version of its Galaxies counterpart.

1. **Weaponsmith crafting:** supplies the loadouts. Crafted items are just item instances with other stat values, so nothing in Stage 2 has to change.
2. **Resources and harvesting:** supply the crafters. Spawning resources with visible stats. Readable surveying.
3. **Housing and player cities:** structures placed on the terrain. Player cities can host their own war terminals and become hubs.
4. **Open-world player-vs-AI content, missions, and more professions.**
5. **Open-world Galactic Civil War:** battles move out of instances into live conquest zones in the world.

Each of these gets a staged breakdown like Stage 1 once the stage before it is finished.
