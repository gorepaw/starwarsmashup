# Roadmap

Battles come first, the world after. A match has clear boundaries and can be tested. The persistent world is the hardest part, so it comes last and grows out of working battles.

| Stage | Focus | Outcome |
|---|---|---|
| [Phase 0](PHASE0.md) | Feasibility spikes | The tech works |
| 1 | Battlefront match queued from a Galaxies city | The core loop works (done) |
| 2 | Match polish | Matches feel like Battlefront |
| 3 | Front end and characters | A main menu, and a character of any species |
| 4 | Items, weapons and armor | Galaxies gear in an inventory and three loadouts |
| 5 | Tatooine around Mos Eisley | A loop outside the city: hunt, loot, sell, quest |
| 6 | Vehicles | Battlefront's in matches, Galaxies' in the world |
| 7 | More cities | Theed, Coronet and the rest (on hold) |
| 8 | Professions | Simplified Galaxies professions |
| 9 | Space combat | Battlefront II's space battles |
| 10+ | World systems | The planets fill up |

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

What was left of Stage 1 moved to the stages after it: the remaining controls, bot behaviors, doors, destructibles and audio to Stage 2; vehicles to Stage 6; navigation on Galaxies terrain to Stage 5; other cities to Stage 7.

**Hub**
- [x] Mos Eisley region loaded as a walkable hub: the city the world snapshot places, 700 m around its middle (`swgtool export-city`): its buildings (with their interiors and lights), walls, props and terminals, 1,623 objects, drawn by the client and collided with on the server and clients alike (every model's collision extent and floors). Inside the buildings, their furniture (900 pieces), lit by their rooms' own lights, and no terrain: the cantina's main room is a meter under the street. Their doors (140) slide open as anyone comes near, as their door styles say. The third-person camera keeps inside their rooms, under their vaulted ceilings (collision from the drawn rooms, the camera's only). Its people: Galaxies' own Tatooine commoners (Rodians, Bith, Aqualish, Sullustans, Nikto, Devaronians, humans, Twi'leks, Zabraks), dressed as their templates' client data says, a crowd of 30 wandering the streets around the cantina and the war terminal and lingering; stormtroopers guarding the terminal and patrolling the plaza; a patron and a dancer in the cantina and a Bith sitting at its side room's console. Galaxies' own creatures where its snapshot places them (rontos, saddled dewbacks and banthas, worrts, an eopie, a cu pa: 13), breathing, solid to walk and shoot into. Ours too, on their own skeletons with their own clips: a dewback walking east of the machine yard, a bantha resting by the war terminal (kneeling: Galaxies' banthas lie on their sides) and one standing; they're simulated as a person is, with their own body against walls (their templates' collision radius, 1.7 m: a round body, not their length). They walk, stand and sit with Galaxies' own animations, run by the hub's server, so every player sees the same people. In Galaxies' colors: its shaders tint skin, hair, eyes and clothes by palettes; the set pieces wear their looks' own colors (stormtroopers white), and the crowd each their own, from the palettes' plain colors, the same for every player. Faces are composited as Galaxies' texture renderers do (skin, brows, makeup, freckles, a Zabrak's tattoos), and the crowd's vary too. Glowing things (the cantina's wall lights, terminals' screens) add their light, as Galaxies' additive effects do. Who stands where, and how the crowd's colors vary, is ours (it was Galaxies' server data). Its particle effects too, where the snapshot places them (15: steam from vents and a crashed ship, smoke plumes, a campfire's fire, embers and smoke), as Galaxies' effects describe them (their waveforms read by comparing effects: rate, speed, lifetime, gravity, spread over the emitter; color, alpha, size over a particle's life), drawn as Godot's particles; fire and embers add their light.
- [x] Recruiter / war terminal interaction that opens the queue (`game/hubs/mos_eisley.ini`). Stand at the terminal and choose a side. It's Galaxies' HQ terminal, standing in the open between where new characters start and the cantina.
- [x] The hub's UI in Battlefront's style (`game/ui/war_terminal_screen.gd`, `hub_hud.gd`): coming up to the war terminal, Battlefront's tooltip bar says to use it (E, rebindable, as is leaving the queue, X); its screen has the two sides under their icons to fight for, and while queued, where and when the battle is and leaving the queue. Queued, a panel at the top shows your side, the battlefield, the countdown and how many are queued; back from a battle, Victory, Defeat or Draw over how it went.
- [x] Queue → match handoff (hub sends player and faction to the match server): when the first player has waited 10 s, the hub (`core::Hub`) launches a battle server with bots filling both teams and transfers the queued players to it, each with a ticket the battle server checks. Later players join a battle that's still on.
- [x] Battles on Battlefront II's maps: the hub picks each battle's battlefield from its list at random (not the last one again), when the first player queues, and loads its world (terrain and map collision) in the background while the queue counts down; the queue shows where it's fought. Players queue for a side by name (`Sides = Rebellion, Empire`), and each battlefield's teams are matched to them by name, since Battlefront's team 1 is the Empire on some maps. Clients load the battle's world when they're sent to it, draw its map (the hub's terrain and buildings make way for it), and go back to the hub's when they return. Mos Eisley's list: the Galactic Civil War's maps, but its own (`tat2`) and Geonosis (the Clone Wars).
- [x] Return from match to hub with results: after the results screen everyone is sent back, to where they stood, and greeted with how it went ("Victory at Mos Eisley Outskirts: 3 kills, 1 deaths, 2 captures"). Rewards come in Stage 4.

Hub notes:
- Battle servers run in the hub's process, each on its own UDP port. Moving them to their own processes or machines keeps the same messages: tickets in, results out.
- Each battle's server keeps its own world, loaded once while battles use it (two battles on the same map share it).

**Match server**
- [x] Match lifecycle: warmup → live → end → results, then the next match. Warmup is practice: deaths are free and posts can't change hands. Results show in the game; sending them back to the hub comes with the handoff.
- [x] Battlefield definition format (terrain bounds, command posts, spawns, out-of-bounds): an INI file (`core::load_battlefield`) with teams and their test loadouts, posts, tickets and timings. Vehicle spawns come with vehicles.
- [x] A test battlefield: `game/battlefields/mos_eisley_outskirts.ini`, five command posts on Galaxies terrain east of the cantina. It tests the match rules until the Battlefront maps work.
- [x] Conquest rules (`core::Conquest`): posts are neutralized and captured by soldiers standing in them (Battlefront's NeutralizeTime 12 s and CaptureTime 10 s, read from its command-post class), and frozen while contested. Each death costs a ticket, and holding more posts bleeds the other team. A team wins when the other runs out, or by holding every post for 20 s.
- [x] Loadout selection and spawning at owned command posts. Leaving the battlefield kills after 10 s. Until the hub sends each player's own gear (Stage 4), battlefields offer test loadouts.

**Battlefront II maps**
- [x] Level importer (`bf2tool export-map`), from the player's own `.lvl` files: terrain heights, texture layers with their blend weights and baked lighting; every object's placement and model (glTF, with its textures as PNGs, including shared props from `ingame.lvl`); regions, paths, sky, fog and lights. Not yet: water, foliage patches (grass, hanging leaves), a fourth texture layer on patches that use more than three, and bump maps.
- [x] Collision in `core::World`: every prop's collision mesh and primitives (boxes, cylinders), placed and written as one triangle file per map (`<map>.collision`, next to the bake). Gentle surfaces are ground to stand on, steep ones block like walls, and shots stop at them; a primitive's name says whether it stops soldiers, shots or both. On Dagobah: 51,565 triangles, under a microsecond per character per tick. The client's camera keeps clear of them too. Dagobah has no boundary path (its playable area is the terrain).
- [x] Conquest data from the map: command posts (starting team, capture region and how high it reaches, spawn path with heights) from the mode's sublevel, written as a map file that a battlefield includes (`Map = ...`), with the boundaries the map lists (the bounds shrink to them) the death regions the mode's mission script adds (named with `--death`), the map's own damage regions (Battlefront's "damageregion" class, always on: so much health a second; Mustafar's lava, the vacuum round Polis Massa's halls, some of Felucia's plants) and each post's console (its object, on the floor the post is fought over).
- [x] Bot navigation (`core::NavGrid`): a half-meter grid of where a soldier fits, worked out from the world's own collision (walls, overhangs, steep ground, steps), lazily in tiles where bots search. Bots follow A* routes cut down to their corners, look ahead to cut more, and plan again when the destination moves or they're stuck. Battlefront's AI barriers come along in the map file (`Barrier = ...`) as places bots avoid when a detour is short. Its hubs and connections aren't used: the grid sees the real obstacles. Each cell holds the surfaces stacked in it (up to six), so bots walk under walkways, drop off ledges and go down into caves and bunkers. Routes keep to the middle of ramps and walkways (edges cost more, and straight lines don't run along them) and off slopes a soldier would barely climb; a climb steeper than a step counts only if a soldier really walks it (a coarse mesh, a Wookiee tree's trunk, can be steeper between the cells' middles). Bots move on from a corner only once the way to the next is clear from where they stand, and come to a drop slowly, to land near its foot rather than past a narrow ledge. The ground shut inside a model (models are skins, solid only from outside: Yavin's minitemple, a stepped pyramid) is no place to stand, nor is ground with a rise steeper than a soldier climbs within a body's reach (movement stops a soldier stepping onto one: at the foot of the hill Geonosis's hangar is dug into), nor ground a soldier's middle reaches under a floor too high to step onto and too low to duck under (a rail's coping along Naboo's terraces, narrower than a cell). A bot shut in where the grid sees no way out (inside a hollow prop) heads straight out to the nearest place that leads on. A barrier a route starts or ends in (a post inside one: Hoth's CP3) costs no more than open ground. A search that runs out on the way tries again back from the goal: toward Hoth's main hangar, under a hill, a search covers the hill first, all of it near the goal as the crow flies, while back from the hangar it keeps to the base's tunnels, out to the open. On Dagobah, of 252 trips between posts and from spawn points (CP4 now down in its cave), all arrive; on Endor, 294 of 294 (CP1 in the bunker, spawns on the Ewok walkways); on Felucia, 222 of 222 (CP2 up the ramp onto its cliff top); on Kashyyyk, 148 of 148 (down from the tree platforms); on Yavin 4, 672 of 672 (TFlank on the minitemple's roof); on Hoth, 88 of 88 (into Echo Base, and to CP3 deep inside an AI barrier); on Geonosis, 399 of 399 (in and out of the hangar, over the sliver of terrain left where its cutter ends, with the hop a bot makes when stuck); on Naboo, 252 of 252 (CP1 on a building's upper floor, CP6 down off its terrace); on Polis Massa, 240 of 240 (through its corridors' doors, left open); on Tantive IV, 172 of 172; on the Death Star, 101 of 108 (the 7 left start from CP3's and CP4's consoles, on a pillar out in the reactor's pit and on the ring round its column, places no one walks out of); on Coruscant, 306 of 306; on Jabba's Palace, 170 of 170 (down into the rancor pit and out); on Mustafar, 336 of 336 (CP5's middle out over the lava past a wall); on Kamino, 486 of 486 (routes up to 400 m between its platforms and pods); on Mygeeto, 270 of 270; on Utapau, 257 of 258 (the one left drops off a deck's slanting edge onto the walkway under it, and the bench's walker, with no look over the edge as bots have, carries on over the walkway's far lip); on Mos Eisley, 414 of 414. Trips are from a post's console and to its middle at its console's height, as bots aim. The grid looks for surfaces from over everything built, not only over the terrain: the Death Star's floors stand far over its stand-in ground. A drop through a death region on the way down (the Death Star's thin one over a chasm's floor) is no way, nor one through a gap narrower than a body (a crack between Kamino's walkway and a pod's floor), nor one landing on an edge, nor one landing nearer a deadly edge than a soldier carries on falling (half a meter, and a fifth of the fall: off a deck's slanting edge onto Utapau's walkway, a meter short of its lip over the pit), nor one landing beside something a falling soldier drifts onto partway down (off a Mos Eisley roof into the street, a dome's steep flank beside it), and a route doesn't end at the bottom of a pit with no way out (the ring round the Death Star's reactor's column, a drop down from its catwalks): if all there is down there is a small pocket without the way back, it ends at the nearest place outside it. Bots don't step over a deadly edge (no ground below, or a fall onto or through a death region), looking further on at a run: not sidestepping in a fight off the Death Star's catwalks and its hub's bridge without rails, nor jumping free when stuck. Their route cutting over one (pushed off it in a fight), they stand and plan again from there; off their route's leg with no straight way on to its corner, they go back onto the leg first (not across a pod's roof beside Kamino's tube bridges). A fall onto safe ground is no edge: soldiers come to no harm; unless there's no way back from it (Kamino's pod roofs, under its walkways and bridges, lead nowhere: bots sidestepping off onto them stood at their edges, nearest their goals, until they went over). A search back from where it lands that sees everything it can reach without getting back marks all of it a pocket, known at once after; one that gets back is remembered for that spot. Bots pick where in a post to stand on ground within its height, at the height of its console where the map gives it (the floor it's fought over: Mustafar's CP5's middle height is nearer the roof over its room than its floor): not in the rock beside a post in a cave (Polis Massa's CP3), where the ground nearest is the hillside over it; and over or under a post isn't there yet. A spot no route reaches (on a ledge, behind a wall) gives way to the post's console (or middle), rather than a bot standing at the nearest place for good. Damage regions are kept out of as death regions are, and hurt so much a second, armor or not. In a post, now and then they move somewhere else in it: holding one spot, a post's defenders and attackers could stand either side of a pillar and never meet (Tantive IV's CP4). A route takes 10 to 16 ms on average (the search leans toward the goal a little: routes about 1.1 times the straight line). The longest (some 400 m on Kashyyyk) can run out of search before the goal: they end nearer it, and the bot plans again from there.
- [x] Rendering in Godot (`game/rendering/bf2_map.gd`): textured terrain (`bf2_terrain.gdshader`), props with alpha-tested foliage, the sky dome and the models drawn with it, each over the one before (Hoth's mountains, Endor's moon and Death Star, Yavin's gas giant under drifting clouds, Mygeeto's mountains, Mustafar's volcano, Coruscant's skyline), fog (none where the sky gives no fog color: Coruscant, the Death Star, Naboo, Tantive IV and Utapau, whose pit went black past 120 m; there, under the dome's lower edge is its horizon's color), the sun, a second global light where the map names one (on soldiers: Coruscant's, with no ambient light, shows them indoors) and lamps. The terrain and baked-lit props show Battlefront's baked lighting instead of Godot's; where the sun isn't baked in (Endor's), it lights the terrain as it's drawn, with shadows (`bf2_terrain_sunlit.gdshader`). The terrain has holes where it's cut away (terrain cutters).
- [x] First map: Dagobah (`dag1`): small, outdoor, infantry only. A conquest match with bots plays on it (`game/battlefields/dagobah.ini`), drawn with its own models, textures, sky, swamp water and fog banks; its trees, roots and rocks block movement and shots, and bots find their way around them. The water is shallow (a meter over the pools' floors): soldiers wade through it at full speed, as in Battlefront. Deep water (Kamino's sea, Yavin's river) has no rule yet. Its Dark Side cave (CP4) is open: the terrain over its mouths is cut away.
- [x] More maps, in order of what each needs (vehicle maps are fought on foot until vehicles come). Mos Eisley (`tat2`) comes last: queuing in Mos Eisley to fight in another Mos Eisley would be odd.
  - [x] Endor (`end1`, `game/battlefields/endor.ini`): its boundary, the death region by the shield generator, the bunker (CP1) under its hill through a cut in the terrain, the Ewok walkways the Rebellion spawns on.
  - [x] Felucia (`fel1`, `game/battlefields/felucia.ini`): its boundary, four overlapping rings the map lists (not a path named for it); spawn paths its posts name in another case; the ramp up to CP2's cliff top, which has a stretch at 50.2 degrees (soldiers now walk up to 55).
  - [x] Kashyyyk (`kas2`, `game/battlefields/kashyyyk.ini`): its sea is shallow enough to wade; the fort's wooden barricades, fought open in Battlefront, are left out as if destroyed (`--open`) until things can be destroyed; CP4's spawns on the Wookiee tree platforms; the minimap fitted by its sea.
  - [x] Yavin 4 (`yav1`, `game/battlefields/yavin.ini`): its death region (the plateau over the valley's west wall); its river shallow enough to wade; TFlank's post on the minitemple's roof; the minimap (`yavin1_map`, named for the map's layers) placed by what it draws.
  - [x] Hoth (`hot1`, `game/battlefields/hoth.ini`): as its mission script sets it up, CP3 and CP6 the Empire's (on Hoth, the attacker, team 1), CP7 gone, the walkers' and bombers' barriers off; its two death regions (both under the ground, for whoever falls through it); the minimap placed by Echo Base's hangars and tunnels, the shield generator and the bunkers.
  - [x] Geonosis (`geo1`, `game/battlefields/geonosis.ini`), the first Clone Wars map, with the Republic's and the Separatists' test loadouts (exported as the Rebels' and the Empire's were; the jet trooper without its jetpack): its five death regions (the canyon's floor, and four small ones under the ground by the Geonosian vents); the minimap placed by the round bunkers, the hangar and the core ship's hull. The Geonosians, a third side, wait for a third team.
  - [x] Naboo (`nab2`, `game/battlefields/naboo.ini`): as its mission script sets it up, the Empire team 1, the turret and camera barriers off; its death region (over the falls); objects' collision as their classes list it (`SoldierCollision`, `OrdnanceCollision`), so CP1's floor isn't shut inside its building's vehicle boxes; the minimap placed by the rotunda, the fountains and the palace. The Gungans, a third side, wait for a third team.
  - [x] Polis Massa (`pol1`, `game/battlefields/polis_massa.ini`): its death region (under the ground below the round hall); its damage regions, the vacuum round its halls (over the corridors' tubes, the roofs); its doors, which open as soldiers come, left open until they move; the minimap fitted to the boundary, which what it draws agrees with. Its airlock, which the script shuts while a console is destroyed, stays as the map has it.
  - [x] Tantive IV (`tan1`, `game/battlefields/tantive_iv.ini`), the first map without terrain: a stand-in ground under the ship, not drawn, with a death region over it; as its mission script sets it up, the turbine's death region, a blast door removed, the barracks' and Leia's barriers off; the minimap (drawn mirrored) placed by the posts' rooms and the doorways. The turbine stops when its console is destroyed, once consoles can be.
  - [x] The Death Star (`dea1`, `game/battlefields/death_star.ini`), on its floors far over the stand-in ground: as its mission script sets it up, the chasms' death regions, the AI barriers it takes down at the start (among them its two bridges', which it extends); without its boundary, which cuts through the station's south end where two of its posts are (sealed, with its falls death regions, it needs none); the minimap placed by the hub's round room, the long corridor and the reactor room's walls. The trash compactor's walls close in on a timer in Battlefront, its death region with them: they don't move yet, so it's left out.
  - [x] Coruscant (`cor1`, `game/battlefields/coruscant.ini`), in the Jedi Temple: as its mission script sets it up, the death regions over the drops outside the temple and under its floors, the doors' AI barriers down; the doors themselves are only in the campaign's layer, so in conquest the doorways stand open. The temple has no ambient light: its soldiers are lit by the map's second global light ("BACK"), out of the sun indoors. The minimap placed by the statue corridor, the round rooms and the grand hall's dome.
  - [x] Jabba's Palace (`tat3`, `game/battlefields/jabbas_palace.ini`): no death regions or barriers from its mission script; the Empire team 1, CP4 down in the rancor pit, its pool shallow enough to wade; the minimap fitted to the palace's outline. Jabba's Gamorrean guards, a third side, wait for a third team.
  - [x] Mustafar (`mus1`, `game/battlefields/mustafar.ini`): as its mission script sets it up, the AI barriers it takes down, the lava bridge's among them, raised (it drops while a console is destroyed, once consoles can be); its lava through the map's own damage regions; no boundary; the minimap placed by the landing platform.
  - [x] Kamino (`kam1`, `game/battlefields/kamino.ini`), on Tipoca City's platforms: as its mission script sets it up, each post's team, the AI barriers it takes down, the pod rooms' doors it locks (shut; the others left open), CP2's spawn path and capture region; the sea under it, where soldiers die once they're under the water; the minimap placed by the pods. Known: past a kerb by cp6 (its chamfer steeper than routes take, though a soldier walks it), the pod roofs under the walkway lead nowhere the grid sees; bots pushed over it in a fight now and then end up there, and some go off a roof's edge into the sea.
  - [x] Mygeeto (`myg1`, `game/battlefields/mygeeto.ini`), in the ruined city's plazas and over its bridges, on foot: as its mission script sets it up, its death region under the city's floors and the AI barriers it takes down; no boundary; CP4 starts neutral; the minimap placed by the round platform and the great plaza's ring. A spawn point by CP2 stands over a slit in the floor narrower than a body: soldiers stand on the floor either side of it.
  - [x] Utapau (`uta1`, `game/battlefields/utapau.ini`), on the sinkhole city's platforms and walkways round the pit, on foot: as its mission script sets it up, its death region and the AI barrier it takes down; the Rebellion's CON_CP1 and the Empire's CON_CP5, the other four neutral; the minimap placed by the city's floors against the walkways it draws light. A spawn point by CON_CP2 lies a meter under its floor: soldiers spawn on the floor over a spawn point up to a meter under it. Where a ramp meets a walkway over a seam, the walkway a little more than a step over the feet, soldiers stand on the ramp's side.
  - [x] Mos Eisley (`tat2`, `game/battlefields/mos_eisley.ini`), in the spaceport's streets and docking bays: no death regions or barriers from its mission script (the door it locks isn't in the map); the Empire team 1, holding CP1, CP2 and CP8, the Rebellion CP3, CP6 and CP7; its turrets' layer, props until turrets can be manned; the minimap fitted to the boundary. The Jawas, a third side, wait for a third team.

**Combat**
- [x] Third-person shooter controls: sprint (with energy), jump and over-the-shoulder aiming. Crouch, roll and zoom are in Stage 2.
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

**Bots**
- [x] Engaging: line of sight, leading the target, sidestepping. Cover and retreat are in Stage 2.
- [x] Objective selection: attack and defend command posts (`core::Bot`): spawn at the post nearest the fighting (choosing again if it's lost while they wait: they waited for it to be won back, up to minutes), head for the nearest post the team doesn't hold (some go elsewhere, so a team spreads out), guard one when the team holds them all.

Bots play through a `GameClient`, with a player's controls and view. `swm_server --bots N` connects them over UDP from the server process, and a whole match runs in `match_test`.

**Presentation**
- [x] HUD: Battlefront II's own, drawn from its layout, textures, models and fonts (`bf2tool export-hud`, `game/ui/bf2_hud.gd`): reticle with clip and heat arcs, target color and hit marker; health and stamina bars; weapon panels with names and ammo; reinforcements with the sides' icons; the round minimap (command posts with their map icon, your side, enemies seen firing); kill messages; victory and defeat timers; the capture ring on the right while you're in a command post's range (from the per-player group of Battlefront's two-player layout, which the single-player one lacks). Command posts look like Battlefront's: no beams or rings, the holder's insignia turning over the post's projector in a cone of light (the post class's hologram settings; the cone is drawn, as Battlefront does), blue for yours, red for the enemy's, white while neutral or disputed. F3 shows network stats. Missing: the weapon models in the panels (soldiers all hold the E-11 for now).
- [x] Screens in Battlefront II's style, from its menu art and fonts (`game/ui/`): the spawn screen (units, the battlefield's map with its command posts to click, Spawn), scores (Tab) and the end of a match, and the pause menu with options: mouse sensitivity and inversion, field of view, first or third person, fullscreen, vertical sync and rebindable controls, saved in `user://settings.cfg`. `game/tools/preview_ui.gd` screenshots them with sample data.
- [x] Scoreboard and end-of-match screen (dev client: kills, deaths and captures per team)

### Exit criteria

A full 32v32 conquest match with bots on a Battlefront II map, launched from and returning to Mos Eisley, that is fun to play solo. Met on 2026-11-22: a solo battle on Naboo against 15 bots, queued from Mos Eisley, played to the results.

---

## Stage 2: Match polish

### Goal

Matches look, sound and feel finished before Galaxies' gear changes what's in them.

### Tasks

**First person**
- [x] Steady arms (`game/rendering/soldier.gd`). The camera had been placed at the head as the animation left it, before the spine bent with the aim (since Godot 4.3, a skeleton modifier's pose lasts only while the skeleton updates), so the gun swung through the view with every look up or down, and the recoil and reload clips jolted the whole body. In first person now:
  - the chest rides on the hips' average over the run cycle, not their every bob and turn, and steps up and down are smoothed out;
  - the spine takes only 30% of the aim pitch and the arms turn the rest about the eyes, as a shooter's view model does: the muzzle stays put in view at any pitch, and looking down lowers the eyes 7 cm, not 26;
  - the camera is at the head as drawn, pushed out along the view;
  - the recoil and flinch clips give way to a small kick, a bob with the stride (under a centimeter) and a sway lagging behind turns;
  - reloads and throws dip the gun about the hands rather than playing the clips, which swing it out of view.

  The legs keep the locomotion: looking down still shows your own body and feet. Others see the third-person animation as before.
- [ ] The third-person hands look wrong too (fingers and wrist on the grip). Fixed in Stage 4 with each weapon's grip points and hand poses. First-person IK uses the same points, so the two stay in step.

**Over soldiers' heads**
- [x] Faction holograms instead of names (`game/rendering/team_tags.gd`): a small copy of the command post hologram's insignia (the map's own model), glowing and flickering as the post's does, turned toward the camera and swaying a little either way, sized by distance so it reads at range. Blue for your side, red for the enemy's. Names stay in the hub, and on the scoreboard.
- [x] When the map stands between the camera and one, your side's ease to grey and faint, and the enemy's disappear. Eight rays a frame against the map's collision, round-robin.

**Health and ammo droids**
- [x] Battlefront's droids work (`core::PowerupStation`), on every map (10 to 36 a map, in its conquest layers). `bf2tool export-map` writes them into the map file from their class (`powerupstation`): every PowerupDelay seconds, each soldier within reach gets SoldierHealth back (the health droid: 25 a second) or SoldierAmmo of its full ammunition (the ammo droid: all of it, every 2 s; grenades go straight into the hand). ASSUMPTIONS: SoldierAmmo is that fraction, and their classes set no Radius, so they reach 3 m. They show on the minimap as Battlefront's icon (a small square), and idle as their classes say: the health droid turns to look somewhere else and waits, the GONK rocks from foot to foot. Their models come as one piece, so the whole droid moves.
- [x] Bots go to a droid when they're hurt (under half health) or short of ammo (under 30%), if one is within 45 m, and stay until they're patched up or rearmed. In the bot match test, 5 of 6 bots went to one.
- [x] Battlefront's other pickups: health and ammo dropped by the dead (`core::PickupKind`, `core::resupply_clips`, `game/rendering/pickups.gd`). Dying in a match rolls Battlefront's `com_inf_default` drop table from `ingame.lvl` (small health +100 at 15%, large health +200 at 8%, ammo +1 clip at 25%, dual +200 health and +2 clips at 8%, energy +150 stamina at 10%; offense and defense buffs wait until buffs exist), resting on the floor beneath the fallen soldier (nothing drops in death/damage regions or out of bounds) for 30 s. Walking within 1.2 m (ASSUMPTION: `powerupitem` sets no radius) consumes a canister only if the soldier can use part of it; bots detour to grab nearby helpful canisters (6 of 6 in the bot match test). `bf2tool export-hud` exports all `powerupitem` models from `ingame.lvl`, and clients hover, spin and blink them during their last 5 s.

**Projectiles and effects**
- [ ] Bolts drawn as Battlefront draws them: the ordnance's own textures and glow, not only its color, length and width.
- [ ] Battlefront's effects: muzzle flashes, sparks and scorch marks where bolts hit, smoke trails and explosions. They come from its effect files (a new bf2tool importer) and are drawn as Godot particles, like Galaxies' effects in the hub.
- [ ] Grenade models: thermal detonators drawn as their model, spinning in flight. Battlefront's model for now; Galaxies' grenades replace it in Stage 4. Rockets and mortar shells are drawn as their models with their trails.

**Controls and rules**
- [ ] Crouch, roll and zoom (scopes with their zoom levels), the Battlefront controls not done yet.
- [ ] Headshots.
- [ ] Explosions blocked by walls and pushing soldiers. Grenades bouncing off walls as walls, not as ground.

**Bots**
- [ ] Take cover and retreat.
- [ ] Difficulty in the options (`core::BotSkill`: aim error, reaction time, range).

**Maps**
- [ ] Doors that open as soldiers come, and the airlocks and gates that scripts work.
- [ ] Destructible objects: Kashyyyk's barricades and sea-wall gate, the consoles that stop Tantive IV's turbine and drop Mustafar's bridge.
- [ ] Deep water (Kamino's sea, Yavin's river): soldiers swim, or die.
- [ ] Third teams: the locals (Jawas, Gungans, Geonosians, Gamorreans) as Battlefront's natives.

**Audio**
- [ ] Battlefront's sound: weapons, impacts, explosions, voice-overs ("Command post captured") and music, from its sound banks (a new importer).

### Exit criteria

A match with a steady first-person view, teammates shown by holograms, droids that heal and rearm, and Battlefront's flashes and sounds.

---

## Stage 3: Front end and characters

### Goal

Start the game from a main menu, make a character of any Galaxies species, and come back to them later.

### Tasks

**Main menu**
- [ ] A main menu scene, set as the project's main scene so opening the game starts there: New Character, Load Character, Settings, Quit. Battlefront's menu style (`bf2_screen.gd`) over a Galaxies backdrop.
- [ ] Load Character: the characters saved in the world database (name, species, where they are). Choose one to enter the hub where they left, as `net_demo.gd`'s arguments do now.
- [ ] Settings: the pause menu's options as their own screen.
- [ ] `net_demo.gd` (2,000 lines) split into scenes the menu can start: the game client, the hub's HUD and screens, the battle's. Solo play still hosts its own hub. Joining a server by address comes when there's one to join.

**Species and rigging**
- [ ] Galaxies' ten playable species, male and female: Human, Twi'lek, Zabrak, Rodian, Bothan, Sullustan, Mon Calamari, Trandoshan, Wookiee and Ithorian. Each comes with its body, heads, hair and customization data.
- [ ] Battlefront's clips on every species. The humanoid skeleton the soldier uses now covers most of them. Those with their own skeleton or proportions (Wookiees' long limbs, Ithorians' head and neck, and any the data shows) need their own retarget. Each is checked in the pose tools (`pose_soldier.gd`, `pose_clips.gd`). Meshes that need fixing are fixed in Blender, the results kept with the other imported assets (never committed).
- [ ] Bodies per species: radius and height from the species' template, as the hub's creatures do, and the eye height for first person.

**Character creator**
- [ ] Species and gender picker, the model turning on a stand.
- [ ] Customization as Galaxies does it, read from each species' data:
  - body sliders (height, build) and face sliders (blend shapes);
  - skin, hair and eye palettes;
  - hair styles;
  - markings (a Zabrak's tattoos, a Twi'lek's lekku patterns).
  
  These are the same palettes and texture renderers the hub's crowd already uses.
- [ ] Name, with a random option, and a starting outfit.
- [ ] Saved with the character. Every client draws other players from it: snapshots carry species and customization values (the crowd's color seed, made explicit).

**Species traits**
- [ ] Some species can't wear some gear, as in Galaxies: Wookiees and Ithorians most armor (Wookiees footwear too), Trandoshans footwear. The exact rules get checked online before they're built.
- [ ] In return, built-in traits through the stat model. A species becomes one more stat source in `core::stats_for`. For example: a Wookiee's health and melee mitigation, a Trandoshan's mitigation and regeneration. A Wookiee with empty slots should come out about even with an armored human. The values are tuned in Stage 4, once armor has values.

### Exit criteria

Launch the game, make a Wookiee, play a match as it, quit, and load it again.

---

## Stage 4: Items, weapons and armor

### Goal

Galaxies' weapons and armor become items you own, wear, and put into three loadouts. In a match, your loadouts are your classes. See [Design: Weapons and loadouts](DESIGN.md#weapons-and-loadouts).

### Tasks

**Data**
- [ ] Item definitions (data files) separate from item instances (a persistent id and its stat values). Inventory and loadouts saved in SQLite through the persistence interface.
- [ ] Weapon archetypes (handling, fire mode, projectile, heat, animation set), from Battlefront's weapon classes.
- [ ] Galaxies weapons imported: models with their hardpoints (muzzle, scope), each mapped to an archetype with a sidegrade stat profile. Pistols, carbines, rifles, heavy weapons and grenades.
- [ ] Galaxies armor imported: every set's pieces in their slots (helmet, chest, biceps, bracers, gloves, belt, leggings, boots), with the species variants where Galaxies has them, and stats on the gear model.
- [ ] Clothing as appearance items. Appearance is separate from stats: wear what you like.

**Holding weapons**
- [ ] Each weapon held properly: its grip points, hand poses by archetype, and Battlefront's animation set for its kind (rifle, pistol, bazooka). This fixes the third-person hands, and first person uses the same points.
- [ ] Soldiers hold their own weapon's model, not all the E-11. The HUD's weapon panels show the models.

**Inventory and equipment**

MMO-style screens in Battlefront II's frames, fonts and colors:
- [ ] Inventory: what you carry, with tooltips showing every stat and a comparison with what's equipped. Drag to equip; sort; drop or destroy.
- [ ] Equipment: a paperdoll of your character, its weapon, armor and appearance slots, and the model updating as you change them. Slots your species can't use are greyed out.

**Loadouts**
- [ ] Up to three loadouts per character. Each is a full kit: primary, sidearm, secondary, armor and other gear. An item can be in more than one, since only one is worn at a time.
- [ ] In the world, switching loadouts takes several seconds. The HUD shows a bar, and taking damage interrupts it.
- [ ] In a match, the spawn screen lists your three loadouts in place of Battlefront's classes, and you pick one each time you spawn. The match server gets them from the hub, never from the client. Bots carry stock loadouts for their side.
- [ ] Standard queue (quality flattened) and gear queue (quality counts).

**Getting gear**
- [ ] Faction quartermaster in each hub: standard-issue gear for faction points.
- [ ] Shooting range in Mos Eisley.
- [ ] Battle rewards: experience, faction points and credits.

### Exit criteria

Choose a weapon at the quartermaster, try it at the range, put it in a loadout, take it into a match, and earn faction points to buy the next one.

---

## Stage 5: Tatooine around Mos Eisley

### Goal

Room outside the city to try a basic looter-shooter loop: hunt creatures and bandits, loot them, sell the junk, take quests.

### Tasks

**The whole planet's terrain**
- [ ] All of Tatooine (16 × 16 km) in place of the 4 km chunk around Mos Eisley. It's generated from the planet's terrain file at 4 m, as the chunk is (4,097 × 4,097 heights), with Mos Eisley where it stands.
  - The client streams the terrain in regions around the player.
  - The server keeps the height field (64 MB as floats), or tiles where people are.
  - The terrain holes for Mos Eisley's rooms work as before.
  - No other towns or points of interest yet.
- [ ] The far view: terrain detail by distance, the sky and haze to the horizon.
- [ ] Bot navigation on open terrain. `core::NavGrid` already works lazily from collision; this checks it on dunes, rocks and canyons.

**Creatures and bandits**

What lived around Mos Eisley in Galaxies, researched online (wikis and guides). The client data has no spawns anyway.
- [ ] Wild creatures in areas and lairs, weaker near the walls and tougher further out. Each on its own skeleton with its own clips, like the hub's dewback. Behavior by kind: wander, herd, flee, attack.
- [ ] Bandits and raiders in camps, the gangs found in that research. They run on the battle bots' code (aim, sidestep, and cover from Stage 2) and wear Galaxies' NPC looks.
- [ ] Respawning, leashing back home, and who they attack.
- [ ] Dying in the world: respawn at Mos Eisley's cloning facility, as in Galaxies. Players can't hurt each other outside matches; faction PvP is a later question.

**Loot, credits and vendors**
- [ ] Loot tables for each creature and NPC: credits, junk (hides, broken parts: Galaxies had junk loot and junk dealers to sell it to), and now and then gear with stats rolled within its sidegrade band.
- [ ] Looting the dead, Galaxies-style: open the corpse with E and take what you want; credits are taken automatically.
- [ ] Vendors in Mos Eisley: a junk dealer who buys junk, and vendors who sell basic gear and supplies. Battlefront-style buy and sell screens, with prices in data files.
- [ ] Credits saved with the character.

**Quests**
- [ ] A quest system: quests in data files, made of steps (go to, kill so many, collect, talk to, deliver). A quest log, a tracker on the HUD, waypoints on the minimap, and quest givers among the hub's people.
- [ ] Mission terminals giving generated destroy missions (a lair so many meters out), as Galaxies' terminals did.
- [ ] Quests inspired by the ones Galaxies' later updates added around Mos Eisley, researched online, not taken from the game data, and rewritten for this game.

### Exit criteria

Leave Mos Eisley, take a mission, kill creatures and bandits, loot them, sell the junk, buy something with it, and come back.

---

## Stage 6: Vehicles

### Goal

Battlefront's vehicles in matches, and Galaxies' vehicles for getting around Tatooine.

### Tasks

**In matches**
- [ ] Vehicles simulated on the server and predicted for their driver: hover (speeder bikes, hover tanks) and walkers (AT-ST first, AT-AT later). Stats from their Battlefront classes. Getting in and out, seats (driver, gunners), their weapons, health, destruction and ejecting.
- [ ] Their models animated (walkers' legs), with their effects and sounds.
- [ ] Vehicle spawns from the maps' own spawn objects (Endor's AT-STs), tied to command posts, with respawn timers.
- [ ] The maps' turrets manned (Mos Eisley's turret layer, props for now).
- [ ] Bots drive and gun.
- [ ] Flyers over ground maps after the ground vehicles. They lead into Stage 9.

**In the world**
- [ ] Galaxies' personal vehicles (speeder bikes, swoops, landspeeders) as items you call and ride across Tatooine. Creature mounts later.

---

## Stage 7: More cities

On hold until you say go. Each city becomes a hub file with its war terminals and the objects its snapshot places, on its planet's terrain: Theed, Coronet, and the rest. Each is stitched into its whole planet as Mos Eisley is in Stage 5. Then Tatooine's other towns (Anchorhead, Bestine, Mos Espa, Mos Entha) fill out the planet.

---

## Stage 8: Professions

Simplified versions of Galaxies' professions. The specifics are worked out with you before this starts. Two points from the design:
- Skills widen what you can do (gear, gadgets, vehicles, abilities) rather than opening stat gaps.
- Crafted items are just item instances with other stat values, so nothing from Stage 4 has to change.

---

## Stage 9: Space combat

Battlefront II's space battles, queued from the hubs like ground battles:
- its space maps imported;
- fighters, bombers and capital ships;
- landing in an enemy hangar and fighting aboard.

Builds on Stage 6's flyers.

---

## Stage 10+: World systems

Added in order of how directly they feed the battles. Each is a streamlined, transparent version of its Galaxies counterpart.

1. **Resources and harvesting**, if the crafting professions need more than Stage 8 gives them: spawning resources with visible stats, and readable surveying.
2. **Housing and player cities:** structures placed on the terrain. Player cities can host their own war terminals and become hubs.
3. **Open-world Galactic Civil War:** battles move out of instances into live conquest zones in the world.

Each of these gets a staged breakdown like Stage 1 once the stage before it is finished.

---

## Decisions for the stages ahead

Decided 2026-11-22:

1. **Holograms behind walls:** your side's grey out and fade when something blocks the view; the enemy's disappear.
2. **Looting:** Galaxies-style. Open the corpse with E and take what you want; credits are taken automatically.
3. **Ithorians** get the same treatment as Wookiees: kept out of standard armor, with built-in traits in return.
4. **Switching loadouts in the world** takes several seconds and is interrupted only by taking damage.
5. **Order:** Stage 2 → 3 → 4 → 5 → 6, then 7 when you say go, 8, 9. Species come before armor so armor is imported for every species from the start. Items come before the open world, since loot and vendors need them.
