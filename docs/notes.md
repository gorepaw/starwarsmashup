[done] lets have the shuttleport questgivers stand near the shuttleport instead of on the suttle landing pad. also can we get a shuttle model on those landing pads so they feel less empty
  -> the eight givers standing inside a shuttleport (Dela Tarrow, Corvin Hale, Tamsin Orr, Sabbo Keel, Jel Corrin, Old Vessa, Ree Hadrel, Brenna Sol) now stand on open ground 19-25 m out, on the town's side, facing it.
  -> every shuttleport (and small shuttle landing pad) has Galaxies' shuttle (the snapshot's own static one) in its middle, turned with it, where the snapshot didn't already have one. It's drawn as the city loads, so it has no collision.

[done] lets give the arms a pose for riding the speeder bike with weapon put away
  -> speeder and swoop bikes ride with Battlefront's own speeder-bike pose (human_speederbike_sit: hands on the bars, no weapon), not the BARC rider's first frame (an up-left aim with the arms raised). The seat is raised 0.118 m to make up for the pose's lower hips, so riders sit where they did.

[done] lets have hopping on the speederbike automatically put us in 3rd person if we were in first, and have it go back when we dismount
  -> getting on any Galaxies vehicle or mount from first person switches to third, and getting off switches back. If you change the view while riding, your choice stays after you get off.

[done] i would like it if drops from creatures persisted in the world longer. if i take out all but 1 of a pack of mobs, i should be able to run back for my loot. also lets do dynamic respawn timers, the current respawn timers are too fast but if there were more players it would be perfect
  -> loot now outlasts the body: when the corpse hides or the creature respawns, what's left stays where it fell (the Loot marker) for 5 minutes after the kill. (It used to vanish when the creature came back, 25-30 s after the kill.) Harvesting is per kill now; before, a re-killed creature said "Already harvested".
  -> respawns wait RespawnTime x (4 / players near the lair), from 1x to 3x: alone it's 3x (75-90 s), two players 2x, four or more the current timers.

[done] dont have quests grant faction points. faction points are our pvp currency sort of, match currency. we can almost think of FP having dual meaning as Faction Points and Fight Points. if ever rewarded for quests they will likely be pvp oriented faction quests
  -> every quest's FactionPoints line is gone (all ten planets). The key still works, for PvP faction quests later. Crafting work orders and supplying quartermasters still give FP; say if those should stop too.

[done] on the second more sparse plateau of anchorhead, there are machines floating in the air that need to be put on the ground
  -> two wind generators stood 80 m up at the foot of the plateau's cliff (Galaxies' heights, over our terrain). They're on the ground now. A hub file's new [ground] section (Template + Position) tells export-city to do this, so a re-export keeps it.
  -> found while looking: a whole quarter of a Corellian town (around 3400-3700, 3000-3300: about 140 walls, columns, lamps and buildings) stands 80 m over our bake's terrain, as do Mos Taike's hillside lamps. That's the bake missing Galaxies' terrain flattening there, a bigger fix for later.

[partly] we need to improve animations for our overworld enemies especially humanoid ones. im assuming there are swg animations that arent hooked up probably. i dont really want to make a bunch of custom animations
  -> humanoid bandits on every planet already animate with Battlefront's soldier clips (run, aim, fire, reload, die); only Endor's duloks don't. Which humanoids look wrong to you, and doing what?
  -> creatures: export-people now finds attack, death, run and walk clips under a skeleton's own names (a droideka's cbt_attack_ranged, a mouse droid's loc_move_fast) and exports a get-hit clip; creatures flinch with it when hit. This needs export-people re-run for each planet to take effect.

[done] i would like city names on the main map
  -> the map names each town (from its shuttleports' and starports' names) on the planet view and the widest local zoom, in white with a shadow.

[done] our action bar/active ability par needs to be moved down a tad, its overlapping some popups
  -> 13 units lower: it was under Battlefront's tooltip bar ("Press E to ..."), now clear of it.

[done] the anchorhead cloning bay icon doesnt turn blue even when im in the same room as it and it is thus not behind a wall
  -> a cloning facility's hologram stands over its roof, to be seen from the street, so from inside, the ceiling hid it. With the camera under a roof within 22 m of it, it now comes down into the room at normal size.

[done] the server has been a bit slower in moment to moment gameoplay seemingly ever since we added the other p;anets. but also we are sunning it on my personal machine, are we ram limited? or do we need to find a fix? i dont see my gpu or cpu getting stressed in task manager just ram
  -> measured: all ten planets' servers take about 1.4 GB and 0.1 ms a tick idle, so neither RAM nor server CPU. The cause: the game ran every hosted planet's server on its main thread, once per rendered frame, so any slow frame or hitch held up every server tick (past 0.25 s it dropped the time). The hosted servers now run on their own thread at a steady 30 Hz. One busy thread barely shows in Task Manager's overall CPU on a many-core machine.

[done] the ground battle terminal is too low, heavily clipped into the ground
  -> export-city put hub terminals on the terrain, but Mos Eisley's stood in the Combat Guild's meeting room, whose floor is 1.13 m up (and Nym's Stronghold's, on a landing pad, 0.33 m). Both have a Height now and stand on their floors.

[done] i want us to fill that building the ground battle terminal is in with our combat prof trainers. instead of clustering them in threes, in each standard room of that building, put one on each wall that doesnt have a door, back facing the wall. this will create new issues of interference with whoever is on the other side of that wall, but it will be better than clusters so we will start with that. keep the gathering trainers relatively together but move them near that ground combat building instead of inside it
  -> the Combat Guild's three identical back meeting rooms hold the nine combat trainers, one per wall without a door, 0.9 m off it, facing in: armor (light, medium, heavy) on the left, Pistols, Carbines, Rifles in the middle, Heavy Weapons, Melee, Unarmored on the right. Through a shared wall, the nearer trainer is the one you talk to.
  -> the Ground Battle Terminal moved from a meeting room's corner (it would have overlapped two trainers) to the middle of the main hall, facing the way in.
  -> Surveying, Foraging and Salvaging stand together outside the hall's east wall.

[done] at the trainers with A/B options, remove the letters A and B, instead lets have some other simple visual indicator that these are binary mutually exclusive choices, like an OR inbetween each one, unless you can think of a more elegant solution
  -> at a trainer, a level's two perks sit side by side, joined by a line through an "OR" disc; "Give up A" now names the perk. In the datapad's profession page, "- or -" sits between them.

[done] i dont think increasing zoom speed is  a good perk in this game, zoom already happens quite fast. lets pick something else for any perks like that
  -> Carbines' Sprint-Ready is now Burst Control (spread grows 35% slower while firing). Rifles' Quick Scope is now High Velocity (bolts 40% faster, same range: less lead on moving targets). These were the only two aim-speed perks. A character who'd learned one has its replacement, already paid for.

[done] my e-11 carbine apears to be using and leveling my rifles skill instead of my carbine skill
  -> the E-11 was the one carbine on the rifle archetype. It's a carbine now (it levels Carbines, gets Carbines perks and is crafted at the carbine schematic) and keeps the Battlefront E-11's exact stats.

[partly] doing a clone wars era ground battle on tantive IV a lot of bots are just standing still, not appearing, or appearing but not really there. minimap in this battle seems off by a bit too. similar issues as rebellion on utapau. also ammo numbers arent carrying over from bf2 matches to obenworld it seems
  -> ammo: it was saved only for players still there when a match ended. Leaving early (Leave Match, quitting, timing out) now saves what you had left too, and in a vehicle it counts your own ammo (it used to count the vehicle's, which could empty your stack).
  -> bots: a headless Tantive IV Clone Wars match (16 bots, 90 s) plays properly on the server: everyone moves, 59 kills, posts captured. So it's what the client shows: frozen, missing, ghosts. Not the cause: snapshot culling (matches send everything), snapshot size, missing soldier models, height snapping of others. Next step is watching one in play; a screenshot or a clip of a stuck one would help.
  -> minimap: its placement (Minimap = in tantive_iv_cw.ini) was set by eye. Which way is it off, and roughly how far?

[done] burn marks on doors in bf2 battles still float in the air when doors open instead of moving with door
  -> a Battlefront door's panels are nodes its clip moves inside the door, and the mark was stuck to the whole door, which doesn't move. It now sticks to the panel it's on (the smallest of the door's meshes there). door_scorch_test covers it.

[open] lots of bf2 ground vehicles unplayable do to camera perspective
  -> not changed: the chase camera follows each vehicle's own TrackCenter, TrackOffset and size, and the numbers look sane on paper (hover tank 7.5 m back, 2.9 m up; AT-TE 17 m back, 11 m up). Which vehicles, and what's wrong: too close, too low, inside the model, or the aim?
