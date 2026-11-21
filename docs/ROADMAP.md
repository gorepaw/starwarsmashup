# Roadmap

Battles come first, the world after. A match has clear boundaries and can be tested. The persistent world is the hardest part, so it comes last and grows out of working battles.

| Stage | Focus | Outcome |
|---|---|---|
| [Phase 0](PHASE0.md) | Feasibility spikes | The tech works |
| 1 | Battlefront match queued from a Galaxies city | The core loop is fun |
| 2 | Galaxies weapons and equipment | Loadouts give players something to chase |
| 3+ | World systems | The planets fill up |

The battlefield evolves alongside: **instanced matches on Battlefront II's own maps → much later, command-post battles built from Galaxies assets in the open world.**

---

## Stage 1: A Battlefront match from a Galaxies city

### Goal

Walk around Mos Eisley, queue at a recruiter, load into a 32v32 conquest match on a Battlefront II map (bots filling empty slots), play to the end, and return to the city with the results.

### Decisions

- **Matches are played on Battlefront II's own maps** (decided 2026-11-18, reversing the earlier plan to build battlefields on Galaxies terrain). Players queue into the real maps, imported from their own install: terrain, props, collision, command posts and spawns. Command-post battles built from Galaxies assets come much later, with the open world. The Galaxies battlefield already made (`mos_eisley_outskirts.ini`) stays as a test field for match rules.
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
- [x] Battlefield definition format (terrain bounds, command posts, spawns, out-of-bounds): an INI file (`core::load_battlefield`) with teams and their test loadouts, posts, tickets and timings. Vehicle spawns come with vehicles.
- [x] A test battlefield: `game/battlefields/mos_eisley_outskirts.ini`, five command posts on Galaxies terrain east of the cantina. It tests the match rules until the Battlefront maps work.
- [x] Conquest rules (`core::Conquest`): posts are neutralized and captured by soldiers standing in them (Battlefront's NeutralizeTime 12 s and CaptureTime 10 s, read from its command-post class), and frozen while contested. Each death costs a ticket, and holding more posts bleeds the other team. A team wins when the other runs out, or by holding every post for 20 s.
- [x] Loadout selection and spawning at owned command posts. Leaving the battlefield kills after 10 s. Until the hub sends each player's own gear (Stage 2), battlefields offer test loadouts.

**Battlefront II maps**
- [x] Level importer (`bf2tool export-map`), from the player's own `.lvl` files: terrain heights, texture layers with their blend weights and baked lighting; every object's placement and model (glTF, with its textures as PNGs, including shared props from `ingame.lvl`); regions, paths, sky, fog and lights. Not yet: water, foliage patches (grass, hanging leaves), a fourth texture layer on patches that use more than three, and bump maps.
- [x] Collision in `core::World`: every prop's collision mesh and primitives (boxes, cylinders), placed and written as one triangle file per map (`<map>.collision`, next to the bake). Gentle surfaces are ground to stand on, steep ones block like walls, and shots stop at them; a primitive's name says whether it stops soldiers, shots or both. On Dagobah: 51,565 triangles, under a microsecond per character per tick. The client's camera keeps clear of them too. Dagobah has no boundary path (its playable area is the terrain).
- [x] Conquest data from the map: command posts (starting team, capture region and how high it reaches, spawn path with heights) from the mode's sublevel, written as a map file that a battlefield includes (`Map = ...`), with the boundaries the map lists (the bounds shrink to them) the death regions the mode's mission script adds (named with `--death`), the map's own damage regions (Battlefront's "damageregion" class, always on: so much health a second; Mustafar's lava, the vacuum round Polis Massa's halls, some of Felucia's plants) and each post's console (its object, on the floor the post is fought over).
- [x] Bot navigation (`core::NavGrid`): a half-meter grid of where a soldier fits, worked out from the world's own collision (walls, overhangs, steep ground, steps), lazily in tiles where bots search. Bots follow A* routes cut down to their corners, look ahead to cut more, and plan again when the destination moves or they're stuck. Battlefront's AI barriers come along in the map file (`Barrier = ...`) as places bots avoid when a detour is short. Its hubs and connections aren't used: the grid sees the real obstacles. Each cell holds the surfaces stacked in it (up to six), so bots walk under walkways, drop off ledges and go down into caves and bunkers. Routes keep to the middle of ramps and walkways (edges cost more, and straight lines don't run along them) and off slopes a soldier would barely climb; a climb steeper than a step counts only if a soldier really walks it (a coarse mesh, a Wookiee tree's trunk, can be steeper between the cells' middles). Bots move on from a corner only once the way to the next is clear from where they stand, and come to a drop slowly, to land near its foot rather than past a narrow ledge. The ground shut inside a model (models are skins, solid only from outside: Yavin's minitemple, a stepped pyramid) is no place to stand, nor is ground with a rise steeper than a soldier climbs within a body's reach (movement stops a soldier stepping onto one: at the foot of the hill Geonosis's hangar is dug into), nor ground a soldier's middle reaches under a floor too high to step onto and too low to duck under (a rail's coping along Naboo's terraces, narrower than a cell). A bot shut in where the grid sees no way out (inside a hollow prop) heads straight out to the nearest place that leads on. A barrier a route starts or ends in (a post inside one: Hoth's CP3) costs no more than open ground. A search that runs out on the way tries again back from the goal: toward Hoth's main hangar, under a hill, a search covers the hill first, all of it near the goal as the crow flies, while back from the hangar it keeps to the base's tunnels, out to the open. On Dagobah, of 252 trips between posts and from spawn points (CP4 now down in its cave), all arrive; on Endor, 294 of 294 (CP1 in the bunker, spawns on the Ewok walkways); on Felucia, 222 of 222 (CP2 up the ramp onto its cliff top); on Kashyyyk, 148 of 148 (down from the tree platforms); on Yavin 4, 672 of 672 (TFlank on the minitemple's roof); on Hoth, 88 of 88 (into Echo Base, and to CP3 deep inside an AI barrier); on Geonosis, 399 of 399 (in and out of the hangar, over the sliver of terrain left where its cutter ends, with the hop a bot makes when stuck); on Naboo, 252 of 252 (CP1 on a building's upper floor, CP6 down off its terrace); on Polis Massa, 240 of 240 (through its corridors' doors, left open); on Tantive IV, 172 of 172; on the Death Star, 101 of 108 (the 7 left start from CP3's and CP4's consoles, on a pillar out in the reactor's pit and on the ring round its column, places no one walks out of); on Coruscant, 306 of 306; on Jabba's Palace, 170 of 170 (down into the rancor pit and out); on Mustafar, 336 of 336 (CP5's middle out over the lava past a wall); on Kamino, 486 of 486 (routes up to 400 m between its platforms and pods); on Mygeeto, 270 of 270; on Utapau, 257 of 258 (the one left drops off a deck's slanting edge onto the walkway under it, and the bench's walker, with no look over the edge as bots have, carries on over the walkway's far lip). Trips are from a post's console and to its middle at its console's height, as bots aim. The grid looks for surfaces from over everything built, not only over the terrain: the Death Star's floors stand far over its stand-in ground. A drop through a death region on the way down (the Death Star's thin one over a chasm's floor) is no way, nor one through a gap narrower than a body (a crack between Kamino's walkway and a pod's floor), nor one landing on an edge, nor one landing nearer a deadly edge than a soldier carries on falling (half a meter, and a fifth of the fall: off a deck's slanting edge onto Utapau's walkway, a meter short of its lip over the pit), and a route doesn't end at the bottom of a pit with no way out (the ring round the Death Star's reactor's column, a drop down from its catwalks): if all there is down there is a small pocket without the way back, it ends at the nearest place outside it. Bots don't step over a deadly edge (no ground below, or a fall onto or through a death region), looking further on at a run: not sidestepping in a fight off the Death Star's catwalks and its hub's bridge without rails, nor jumping free when stuck. Their route cutting over one (pushed off it in a fight), they stand and plan again from there; off their route's leg with no straight way on to its corner, they go back onto the leg first (not across a pod's roof beside Kamino's tube bridges). A fall onto safe ground is no edge: soldiers come to no harm; unless there's no way back from it (Kamino's pod roofs, under its walkways and bridges, lead nowhere: bots sidestepping off onto them stood at their edges, nearest their goals, until they went over). A search back from where it lands that sees everything it can reach without getting back marks all of it a pocket, known at once after; one that gets back is remembered for that spot. Bots pick where in a post to stand on ground within its height, at the height of its console where the map gives it (the floor it's fought over: Mustafar's CP5's middle height is nearer the roof over its room than its floor): not in the rock beside a post in a cave (Polis Massa's CP3), where the ground nearest is the hillside over it; and over or under a post isn't there yet. A spot no route reaches (on a ledge, behind a wall) gives way to the post's console (or middle), rather than a bot standing at the nearest place for good. Damage regions are kept out of as death regions are, and hurt so much a second, armor or not. In a post, now and then they move somewhere else in it: holding one spot, a post's defenders and attackers could stand either side of a pillar and never meet (Tantive IV's CP4). A route takes 10 to 16 ms on average (the search leans toward the goal a little: routes about 1.1 times the straight line). The longest (some 400 m on Kashyyyk) can run out of search before the goal: they end nearer it, and the bot plans again from there.
- [x] Rendering in Godot (`game/rendering/bf2_map.gd`): textured terrain (`bf2_terrain.gdshader`), props with alpha-tested foliage, the sky dome and the models drawn with it, each over the one before (Hoth's mountains, Endor's moon and Death Star, Yavin's gas giant under drifting clouds, Mygeeto's mountains, Mustafar's volcano, Coruscant's skyline), fog (none where the sky gives no fog color: Coruscant, the Death Star, Naboo, Tantive IV and Utapau, whose pit went black past 120 m; there, under the dome's lower edge is its horizon's color), the sun, a second global light where the map names one (on soldiers: Coruscant's, with no ambient light, shows them indoors) and lamps. The terrain and baked-lit props show Battlefront's baked lighting instead of Godot's; where the sun isn't baked in (Endor's), it lights the terrain as it's drawn, with shadows (`bf2_terrain_sunlit.gdshader`). The terrain has holes where it's cut away (terrain cutters).
- [x] First map: Dagobah (`dag1`): small, outdoor, infantry only. A conquest match with bots plays on it (`game/battlefields/dagobah.ini`), drawn with its own models, textures, sky, swamp water and fog banks; its trees, roots and rocks block movement and shots, and bots find their way around them. The water is shallow (a meter over the pools' floors): soldiers wade through it at full speed, as in Battlefront. Deep water (Kamino's sea, Yavin's river) has no rule yet. Its Dark Side cave (CP4) is open: the terrain over its mouths is cut away.
- [ ] More maps, in order of what each needs (vehicle maps are fought on foot until vehicles come), and the hub picking one for each battle. Mos Eisley (`tat2`) comes last: queuing in Mos Eisley to fight in another Mos Eisley would be odd.
  - [x] Endor (`end1`, `game/battlefields/endor.ini`): its boundary, the death region by the shield generator, the bunker (CP1) under its hill through a cut in the terrain, the Ewok walkways the Rebellion spawns on.
  - [x] Felucia (`fel1`, `game/battlefields/felucia.ini`): its boundary, four overlapping rings the map lists (not a path named for it); spawn paths its posts name in another case; the ramp up to CP2's cliff top, which has a stretch at 50.2 degrees (soldiers now walk up to 55).
  - [x] Kashyyyk (`kas2`, `game/battlefields/kashyyyk.ini`): its sea is shallow enough to wade; the fort's wooden barricades, fought open in Battlefront, are left out as if destroyed (`--open`) until things can be destroyed; CP4's spawns on the Wookiee tree platforms; the minimap fitted by its sea.
  - [ ] Destructible objects: Kashyyyk's barricades and the gate panel that opens its sea wall, and the like on other maps.
  - [x] Yavin 4 (`yav1`, `game/battlefields/yavin.ini`): its death region (the plateau over the valley's west wall); its river shallow enough to wade; TFlank's post on the minitemple's roof; the minimap (`yavin1_map`, named for the map's layers) placed by what it draws.
  - [x] Hoth (`hot1`, `game/battlefields/hoth.ini`): as its mission script sets it up, CP3 and CP6 the Empire's (on Hoth, the attacker, team 1), CP7 gone, the walkers' and bombers' barriers off; its two death regions (both under the ground, for whoever falls through it); the minimap placed by Echo Base's hangars and tunnels, the shield generator and the bunkers.
  - [x] Geonosis (`geo1`, `game/battlefields/geonosis.ini`), the first Clone Wars map, with the Republic's and the Separatists' test loadouts (exported as the Rebels' and the Empire's were; the jet trooper without its jetpack): its five death regions (the canyon's floor, and four small ones under the ground by the Geonosian vents); the minimap placed by the round bunkers, the hangar and the core ship's hull. The Geonosians, a third side, wait for a third team.
  - [x] Naboo (`nab2`, `game/battlefields/naboo.ini`): as its mission script sets it up, the Empire team 1, the turret and camera barriers off; its death region (over the falls); objects' collision as their classes list it (`SoldierCollision`, `OrdnanceCollision`), so CP1's floor isn't shut inside its building's vehicle boxes; the minimap placed by the rotunda, the fountains and the palace. The Gungans, a third side, wait for a third team.
  - [x] Polis Massa (`pol1`, `game/battlefields/polis_massa.ini`): its death region (under the ground below the round hall); its damage regions, the vacuum round its halls (over the corridors' tubes, the roofs); its doors, which open as soldiers come, left open until they move; the minimap fitted to the boundary, which what it draws agrees with. Its airlock, which the script shuts while a console is destroyed, stays as the map has it.
  - [ ] Doors that open as soldiers come, and the airlocks and gates scripts work.
  - [x] Tantive IV (`tan1`, `game/battlefields/tantive_iv.ini`), the first map without terrain: a stand-in ground under the ship, not drawn, with a death region over it; as its mission script sets it up, the turbine's death region, a blast door removed, the barracks' and Leia's barriers off; the minimap (drawn mirrored) placed by the posts' rooms and the doorways. The turbine stops when its console is destroyed, once consoles can be.
  - [x] The Death Star (`dea1`, `game/battlefields/death_star.ini`), on its floors far over the stand-in ground: as its mission script sets it up, the chasms' death regions, the AI barriers it takes down at the start (among them its two bridges', which it extends); without its boundary, which cuts through the station's south end where two of its posts are (sealed, with its falls death regions, it needs none); the minimap placed by the hub's round room, the long corridor and the reactor room's walls. The trash compactor's walls close in on a timer in Battlefront, its death region with them: they don't move yet, so it's left out.
  - [x] Coruscant (`cor1`, `game/battlefields/coruscant.ini`), in the Jedi Temple: as its mission script sets it up, the death regions over the drops outside the temple and under its floors, the doors' AI barriers down; the doors themselves are only in the campaign's layer, so in conquest the doorways stand open. The temple has no ambient light: its soldiers are lit by the map's second global light ("BACK"), out of the sun indoors. The minimap placed by the statue corridor, the round rooms and the grand hall's dome.
  - [x] Jabba's Palace (`tat3`, `game/battlefields/jabbas_palace.ini`): no death regions or barriers from its mission script; the Empire team 1, CP4 down in the rancor pit, its pool shallow enough to wade; the minimap fitted to the palace's outline. Jabba's Gamorrean guards, a third side, wait for a third team.
  - [x] Mustafar (`mus1`, `game/battlefields/mustafar.ini`): as its mission script sets it up, the AI barriers it takes down, the lava bridge's among them, raised (it drops while a console is destroyed, once consoles can be); its lava through the map's own damage regions; no boundary; the minimap placed by the landing platform.
  - [x] Kamino (`kam1`, `game/battlefields/kamino.ini`), on Tipoca City's platforms: as its mission script sets it up, each post's team, the AI barriers it takes down, the pod rooms' doors it locks (shut; the others left open), CP2's spawn path and capture region; the sea under it, where soldiers die once they're under the water; the minimap placed by the pods. Known: past a kerb by cp6 (its chamfer steeper than routes take, though a soldier walks it), the pod roofs under the walkway lead nowhere the grid sees; bots pushed over it in a fight now and then end up there, and some go off a roof's edge into the sea.
  - [x] Mygeeto (`myg1`, `game/battlefields/mygeeto.ini`), in the ruined city's plazas and over its bridges, on foot: as its mission script sets it up, its death region under the city's floors and the AI barriers it takes down; no boundary; CP4 starts neutral; the minimap placed by the round platform and the great plaza's ring. A spawn point by CP2 stands over a slit in the floor narrower than a body: soldiers stand on the floor either side of it.
  - [x] Utapau (`uta1`, `game/battlefields/utapau.ini`), on the sinkhole city's platforms and walkways round the pit, on foot: as its mission script sets it up, its death region and the AI barrier it takes down; the Rebellion's CON_CP1 and the Empire's CON_CP5, the other four neutral; the minimap placed by the city's floors against the walkways it draws light. A spawn point by CON_CP2 lies a meter under its floor: soldiers spawn on the floor over a spawn point up to a meter under it. Where a ramp meets a walkway over a seam, the walkway a little more than a step over the feet, soldiers stand on the ramp's side.
  - [ ] Mos Eisley.

**Combat**
- [ ] Third-person shooter controller with Battlefront feel: sprint, roll, crouch, zoom. Sprint (with energy), jump and over-the-shoulder aiming are done; roll, crouch and zoom aren't.
- [x] First-person view (V toggles): from your own character's eyes, its head hidden, so the arms and gun you see are the armor and weapon you carry. Dying pulls the camera out to third person.
- [x] Soldiers animated with Battlefront's clips: legs run (forward or backward, turned toward a strafe) while the upper body raises the rifle to fire, reloads or throws; the spine bends with the aim pitch (now networked); hits make them flinch, and they fall when killed. Bolts are drawn leaving the shooter's muzzle and join their true path (from the eyes, where the server fires them) within 20 m.
- [x] Projectile weapons with clips and overheating: blaster bolts fly at their real speed with gravity. Stats come from the player's own Battlefront II files (`bf2tool export-loadout`): the Imperial rifleman's E-11 (60 damage, a shot every 0.166 s, 50-round clips) and pistol (overheats).
- [x] Grenades and explosives: thermal detonators (right mouse) bounce and explode on a fuse. Rockets accelerate and explode on impact, and mortar shells lob and burst on contact. Explosions do full damage within an inner radius, falling to nothing at the outer one (the thermal detonator: 450 within 4.5 m, nothing past 6.5 m). Weapons carry the ammo Battlefront gives them: spare clips, 2–4 grenades, 7 rockets.
- [x] Health, damage, death, respawn (400 health, respawn after 5 s at a spawn point)
- [x] Lag-compensated hit detection: the server rewinds targets to where the shooter saw them (up to 0.5 s). In tests, a shooter 100 ms from the server lands 7 of its shots on a strafing target with rewinding and none without.
- [x] Loadouts instead of classes (decided 2026-11-18: there are no character classes). A loadout is a primary, a sidearm, a secondary and gear; weapons and gear are item files, and any of them can change any character statistic (`core::stats_for`). Damage is mitigated by the target's general and per-type mitigation (ballistic, laser, electro, bio, explosive, melee). For testing, Battlefront's kits are exported as loadouts: rifleman, rocketeer, sniper, engineer (shotgun: 8 pellets a shot) and the Imperial officer (sonic blaster and mortar), plus `game/loadouts/test_heavy_trooper.ini` with test armor. Mines, detpacks, turrets, droids, buffs and flamethrowers aren't supported yet.

Combat notes:
- In a match, teammates can't hurt each other (`ServerConfig::friendly_fire`). Outside one, everyone is hostile.
- Spread uses the weapon's documented values, but how Battlefront combines `SpreadPerShot`, `SpreadThreshold` and `SpreadLimit` is a guess (marked ASSUMPTION in `swm/core/combat.h`). So are a grenade's fuse (an unnamed ordnance property), explosions hurting the thrower, and the linear falloff. Grenades bounce as if off the ground even from walls, explosions aren't blocked by walls, and they don't push. Ammo pickups and headshots aren't in yet. Every soldier holds the E-11 model whatever its weapon, and plays rifle animations.
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
- [x] HUD: Battlefront II's own, drawn from its layout, textures, models and fonts (`bf2tool export-hud`, `game/ui/bf2_hud.gd`): reticle with clip and heat arcs, target color and hit marker; health and stamina bars; weapon panels with names and ammo; reinforcements with the sides' icons; the round minimap (command posts with their map icon, your side, enemies seen firing); kill messages; victory and defeat timers; the capture ring on the right while you're in a command post's range (from the per-player group of Battlefront's two-player layout, which the single-player one lacks). Command posts look like Battlefront's: no beams or rings, the holder's insignia turning over the post's projector in a cone of light (the post class's hologram settings; the cone is drawn, as Battlefront does), blue for yours, red for the enemy's, white while neutral or disputed. F3 shows network stats. Missing: the weapon models in the panels (soldiers all hold the E-11 for now).
- [x] Screens in Battlefront II's style, from its menu art and fonts (`game/ui/`): the spawn screen (units, the battlefield's map with its command posts to click, Spawn), scores (Tab) and the end of a match, and the pause menu with options: mouse sensitivity and inversion, field of view, first or third person, fullscreen, vertical sync and rebindable controls, saved in `user://settings.cfg`. `game/tools/preview_ui.gd` screenshots them with sample data.
- [x] Scoreboard and end-of-match screen (dev client: kills, deaths and captures per team)
- [ ] Battlefront audio: weapons, voice-overs, music

### Exit criteria

A full 32v32 conquest match with bots on a Battlefront II map, launched from and returning to Mos Eisley, that is fun to play solo.

---

## Stage 2: Weapons and equipment

### Goal

Galaxies' weapon and armor library becomes loadouts you choose in the hub and take into matches, balanced for a shooter. See [Design: Weapons and loadouts](DESIGN.md#weapons-and-loadouts).

### Tasks

**Data and items**
- [ ] Weapon archetype definitions (handling, fire mode, projectile, heat)
- [ ] Map Galaxies weapons to archetypes, with sidegrade stat profiles
- [ ] Armor from Galaxies, with stats on the gear statistics model (mitigation general and by damage type, health, movement)
- [ ] Item definitions (data files) separate from item instances (persistent ID + stat values)
- [ ] Persistence for inventory and loadouts (SQLite via the persistence interface)

**Hub infrastructure**
- [ ] Inventory UI
- [ ] Equipment / paperdoll UI (appearance separate from stats)
- [ ] Loadout editor: gear slots filled from the inventory
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
