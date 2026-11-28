# Trainer Perks

The rework of what professions teach. It replaces milestones at 1 / 25 / 50 / 75 / 100 ([Professions](PROFESSIONS.md)) with two perks to choose between at each tier. Weapons and armor were first; then Squad Leader, Doctor and Entertainer, gathering and crafting, Bounty Hunter, Smuggler, Ranger, Merchant, Pilot, Bio-Engineer, Creature Handler and Droid Engineer (2026-11-27). The other advanced professions follow one by one and keep their milestones until their turn.

Status: **built** (see Order of work): the rules, the cap, trainers, licences, and all 90 perks. The tags say what each needed: nothing new, a trigger (**T**), or a new mechanic (**N**; [Mechanics](#mechanics)).

## Why

The old milestones were a fixed chain of small multipliers that every character eventually owned. Some were straight upgrades nobody would skip (reload ×0.95 on your main gun, training away your armor's drag). Others were too small to feel (strafe ×1.03, jump ×1.05). None of them were a choice.

## Rules

- **Perk levels: 5, 25, 45, 65, 85.** They fall between the Mk gear levels (10, 20 … 100), so every five levels brings either new gear or a new perk.
- **Choose A or B.** Each perk level offers two perks, and a character can know one of them. That's at most five perks per profession.
- **No chain.** A perk needs only its profession's level. Dropping one never takes others with it, so a Melee 85 perk can be learned alone. That's why the 85 perks are the most defining, not the strongest.
- **Ten combat perks and ten non-combat perks per character** (2026-11-27; it was ten in all), across all three loadouts. Combat: weapons, armor, Bounty Hunter, Smuggler, Squad Leader, Doctor, Ranger, Pilot, Creature Handler, Droid Operations. Non-combat: gathering, crafting, Entertainer, Merchant, Bio-Engineer, Droid Engineer (`Pool =` in professions.ini). Milestones count toward their profession's pool the same way, level 1 included. A fighter no longer gives up gathering or crafting to fight well, but within a pool nothing is free.
- **Advanced trees.** An advanced profession's perks are abilities (`Ability =`), switches the server looks for (`Grants =`), or stat changes. Its tiers are 5 / 25 / 45 / 65 / 85, or **1** / 25 / 45 / 65 / 85 (`FirstPerk = 1`) when you can't do it at all without its first perk: then either level-1 perk is its licence, and it's free (Squad Leader: two kinds of rally; Doctor: a healing dart or field dressing, which also licenses medicine; Entertainer: Musician or Dancer). Tiers from 5 (`FirstPerk = 5`, the default) are for a profession you can practise without a perk; none does now.
- **Two buttons at most.** No profession adds more than two abilities to a character's bar: two of its tiers are a pair of buttons (one of each pair learned), the rest passives. Using a profession's abilities earns its experience. A perk's switches travel with a loadout into a match (`Loadout::grants`), so they work there too. Squad Leader's Shields Up and Press On ride on its orders; Doctor's Splash, Lingering Bacta, Defibrillator and Field Triage are what its heal does besides; Spotter and Squad Spawn Point replace Mark Target and the beacon.
- **The level-1 perk is the licence.** A non-combat or specialist profession's level-1 milestone is what lets you do its activity at all: survey and harvest, craft its schematics, heal, tame, perform. Without it you can't, and you earn none of its experience. To level a profession you hold its level-1 perk while you do it; give it up later and you keep the level, and learning it again is free.
- **No slots.** Active Specialization Slots are gone. What a character has learned works everywhere: a weapon perk whenever you fight with that profession's weapons, an armor perk whenever your armor is that class, a specialist's abilities in every loadout. The cap does the limiting the slots did. Levelling stays uncapped: every profession can reach 100.
- **Trainers.**
  - Learning a perk the first time costs credits (`milestone_cost` at the perk's level: 500, 500, 2,500, 7,500, 20,000).
  - Learning a perk you've paid for before is free.
  - Giving a perk up is free.
  - Both learning and giving up happen at that profession's trainer, so trying things costs nothing, but rebuilding a whole character means a trip around the trainers.
- **No more ranks** (`RankEvery`) and no level-1 certifications for weapon and armor professions. Mk gear is the reward for levelling.
- **Perks change how you play, or trade one thing for another.** None adds flat damage or health ([Design](DESIGN.md): gear spans about 2×).
- **Tacticals aren't Heavy Weapons.** Grenades, mines and traps (`grenade`, `placed`, `utility`) leave Heavy Weapons' archetypes. Their perks come with the advanced trees. Until then, kills with them level no profession, and their marks need the character's highest level, the way a Deflector Shield's do.

## The perks

Tags:
- No tag: today's stat and weapon fields are enough, so it's an ini entry.
- **T**: works on a trigger (below).
- **N**: a new mechanic (below).

### Pistols (`pistol`, `scatter_pistol`)

| Level | A | B |
|---|---|---|
| 5 | **Quick Draw**: swap to and from pistols 40% faster | **Cool Holster**: pistol heat drains twice as fast while holstered (N) |
| 25 | **Snap Shot**: the first shot after a 1 s pause is perfectly accurate (T) | **Stun Rounds**: 15% chance on a hit to slow the target 20% for 1.5 s (T: `Chance`) |
| 45 | **Fan the Hammer**: after a roll, pistols fire 30% faster for 3 s (T) | **Dead Man's Draw**: when your shield breaks, for 2.5 s the swap to your pistol is instant and it builds no heat (T) |
| 65 | **Roll Shot**: pistols fire while you roll (N) | **Execution**: pistol hits on a target under 25% health always crit (T) |
| 85 | **Deadeye**: a pistol headshot kill clears its heat and gives back a roll's stamina (T) | **Ricochet**: pistol bolts bounce once off walls (N) |

### Carbines (`carbine_burst`, `scatter`)

| Level | A | B |
|---|---|---|
| 5 | **Burst Control**: spread grows 35% slower as you keep firing (`bloom`) | **Tight Choke**: scatter guns' spread 15% tighter (its own `Archetypes`) |
| 25 | **Combat Reload**: sprinting doesn't hold up a reload (N; without it, sprinting pauses one) | **Adrenaline**: a kill gives back 40 stamina (T) |
| 45 | **Breacher**: 30% more damage to Deflector Shields (`shield`) | **Suppressing Burst**: hits slow the target's sprint 20% for 1 s (T) |
| 65 | **Close Quarters**: a hit within 12 m makes spread settle 40% faster for 1.5 s (T) | **Flanker**: 15% more damage on hits from behind (N) |
| 85 | **Overclock**: once every 20 s, an overheat vents at once (T) | **Chain Breach**: each kill within 5 s of the last restores 15% shield (T) |

### Rifles (`rifle_auto`, `sniper`, `beam`)

| Level | A | B |
|---|---|---|
| 5 | **Long Barrel**: full damage carries 20% further (`range`) | **High Velocity**: bolts fly 40% faster, as far (`velocity`) |
| 25 | **Braced**: crouching's accuracy bonus 1.8× instead of 1.4× (T) | **Suppression**: automatic-rifle hits stop the target's stamina coming back for 1.5 s (T) |
| 45 | **Spotter**: hits while zoomed mark the target for your side for 4 s (T: `Aimed`, `Mark`) | **Rhythm**: each hit on the same target within 1.5 s of the last takes heat off (T) |
| 65 | **Headhunter**: headshots ×1.25, body shots ×0.9 (N) | **Overwatch**: after 2 s crouched and still, 30% less heat and no bloom (T) |
| 85 | **One Shot**: a sniper kill cools the rifle completely (T) | **Piercing Round**: sniper bolts go through the first body (N) |

### Heavy Weapons (`heavy_launcher`, `beam_cannon`, `flamer`)

| Level | A | B |
|---|---|---|
| 5 | **Quick Loader**: launchers reload 20% faster | **Heavy Lifter**: a heavy weapon's run-speed penalty halved (N) |
| 25 | **Cool Running**: heavy weapons build 20% less heat | **Braced Launch**: crouched, 40% less launcher spread (T) |
| 45 | **Tank Buster**: 25% more damage to vehicles and turrets (`vehicle`) | **Proximity Fuse**: rockets burst when they pass within 1.5 m of an enemy (N) |
| 65 | **Shaped Charge**: 20% wider blasts, 10% less damage at their center (N) | **Pyro**: burns (and other hit effects) from your weapons last 50% longer (N) |
| 85 | **Bombardment**: a kill with a heavy weapon reloads it at once (T) | **Danger Close**: 60% less damage from your own blasts, enough to rocket-jump (N) |

### Melee (`melee`, `melee_2h`)

Block and parry already work against melee.

| Level | A | B |
|---|---|---|
| 5 | **Swift Strikes**: attack 8% faster (`speed`) | **Long Reach**: 15% more reach (`reach`) |
| 25 | **Lunge**: 30% longer lunge (`lunge`) | **Iron Guard**: blocking costs half the stamina, and a block lets 30% through instead of 45% (N) |
| 45 | **Deflect**: a block turns aside blaster bolts from the front, for 10 stamina each (N) | **Shield Breaker**: Deflector Shield disruption ×1.5 (`disrupt`) |
| 65 | **Riposte**: a 60% longer parry window, and your next hit within 1.5 s of a parry crits (N) | **Bloodrush**: a kill restores 25% shield and stamina (T) |
| 85 | **Teräs Käsi Master**: attack out of a roll, and the third hit in a row on one target staggers twice as long (N) | **Sweeping Blows**: two-handed hits knock back and go through a block, not a parry (N) |

### Unarmored

| Level | A | B |
|---|---|---|
| 5 | **Light on Your Feet**: run 4% faster | **Tumbler**: rolls cost 40% less stamina (`roll_cost`) |
| 25 | **Second Wind**: stamina comes back 15% faster | **Slide**: crouch out of a sprint to slide for 0.8 s instead of rolling, still shooting (N) |
| 45 | **Evasion**: 30% less damage while rolling (T) | **Double Roll**: roll again in the second half of a roll (N) |
| 65 | **Acrobat**: jump 15% higher | **Untouchable**: after 4 s without a hit, run 8% faster until you're hit (T) |
| 85 | **Ghost Step**: when your shield breaks, 2 s of sprinting 25% faster for no stamina (T) | **Featherweight**: sprint 6% faster for 15% less stamina |

### Light / Recon Armor

| Level | A | B |
|---|---|---|
| 5 | **Insulated Weave**: 5% more electro and bio resistance | **Scout's Pace**: sprinting drains 10% less stamina |
| 25 | **Low Profile**: your shots don't show you on enemy radar (N; everyone is off it unless firing) | **Quick Recharge**: your shield starts recharging 25% sooner (`shield_delay`) |
| 45 | **Antitoxin**: burns, poisons and slows last 40% less (`effect_resistance`) | **Quiet Step**: creatures and outlaws notice you from 40% closer (N; there are no footstep sounds to silence yet) |
| 65 | **Fleet**: sprint 6% faster, 15% less shield capacity | **Overcharge**: your shield recharges to 120%, and the extra drains away over 6 s (N) |
| 85 | **Pathfinder**: run 20% faster for 2 s after a roll (T) | **Vanish**: when your shield breaks, cloak for 1.5 s, once a minute (T) |

### Medium / Battle Armor

| Level | A | B |
|---|---|---|
| 5 | **Battle Drill**: medium armor's run penalty halved | **Conditioning**: 15 more stamina (`max_energy`) |
| 25 | **Adaptive Plating**: hit by a damage type, 10% more resistance to it for 6 s (T) | **Shield Harmonics**: your shield recharges 25% faster (`shield_recharge`) |
| 45 | **Field Dressing**: 20% more healing received (`healing_received`) | **Ammo Webbing**: your ammunition stacks hold 25% more (N) |
| 65 | **Rally**: a kill starts your shield recharging at once (T) | **Steady Stance**: half the stagger and knockback (N: a stagger stat) |
| 85 | **Second Chance**: once a life, a killing hit leaves you at 1 health with 1 s of half damage (T) | **Tactical Swap**: swap weapons twice as fast, and the holstered one reloads itself (N) |

### Heavy / Assault Armor

| Level | A | B |
|---|---|---|
| 5 | **Braced Frame**: 35% more blast knockback resistance (`explosion_push_resist`) | **Assault Stride**: heavy armor's run penalty halved |
| 25 | **Anchor**: crouched, 10% more mitigation and no knockback (T) | **Bulwark**: 20% more shield capacity, recharging 15% slower |
| 45 | **Blast Padding**: 8% more explosive resistance | **Shoulder Charge**: sprint into an enemy to stagger them for 0.8 s, every 4 s at most (N) |
| 65 | **Reactive Plating**: 25% of the explosive damage you take refills your shield (N) | **Ablative**: the first hit within 5 s of your shield breaking does half damage (T) |
| 85 | **Juggernaut**: immune to snares and stagger (N) | **Last Stand**: under 25% health, 20% more mitigation and faster reloads (T) |

### Gathering and crafting

Every one starts at **1**: either level-1 perk is its licence (gather, or craft its schematics and fill its work orders) and is free. Its perks are numbers the server sums by name (`Bonus = <key> <value>`), the same five per kind, named for each profession.

Gathering (Surveying, Foraging, Salvaging):

| Level | A | B |
|---|---|---|
| 1 | Licence, and 15% more per haul (`yield`) | Licence, and 25% more experience (`xp`) |
| 25 | 20% more per haul | 15% chance of a double haul (`double_haul`) |
| 45 | 25% more experience | 20% more per haul |
| 65 | 15% chance of a double haul | 25% more experience |
| 85 | 40% more per haul | 30% chance of a double haul |

Crafting (Weaponsmith, Armorsmith, Tailor, Chef, Architect):

| Level | A | B |
|---|---|---|
| 1 | Licence, and Experimentation: better grades (`grade` 0.15) | Licence, and 20% fewer materials (`materials`) |
| 25 | 15% chance of one more each craft (`extra_item`) | Work orders and artisan orders pay 50% more (`order_pay`) |
| 45 | 25% more experience | 20% fewer materials |
| 65 | Better grades again | 25% chance of one more |
| 85 | Masterwork: Exceptional twice as likely (`exceptional`) | 35% fewer materials |

- A haul is an extraction, a corpse harvested or salvaged, or an item broken down.
- `grade` moves the grade roll's center up ([Crafting](CRAFTING.md)); `exceptional` multiplies the Exceptional odds and scales the rest to fit.
- Fewer materials apply each in turn (20%, 20% and 35% together leave 42%), and a slot never takes less than 1.
- An extra item has the same grade as the one rolled.
- `xp` works for any profession's perk.
- Old saves' gathering and crafting milestones are refunded, and a licence milestone becomes the A licence perk.

### Bounty Hunter

Tiers from 1 (taking bounties needs the licence).

| Level | A | B |
|---|---|---|
| 1 | **Seeker Probe** (button): your marks on the map for 30 s; in a battle, a recon probe spotting within 25 m | **HVT Spotting** (button): in a battle, the enemy's deadliest marked for your side 15 s; in the world, your marks for 60 s |
| 25 | **Toxin Dart** (button): 14 bio a second for 8 s, slower stamina | **Wrist Rocket** (button): 150 explosive within 3 m |
| 45 | **Bloodhound**: marks within 150 m stay on your HUD; in a battle whoever shoots you shows to your side 5 s | **Relentless**: slows on you halved |
| 65 | **Big Game**: also a tier-harder mark with a bigger gang, double pay | **Twin Charges**: your level-25 button holds two charges |
| 85 | **Collector**: bounties pay half again, credits and experience | **Clean Getaway**: after a kill, 20% faster 4 s and the shield recharging at once |

### Smuggler

Tiers from 1 (contraband runs and slicing take the licence; slices themselves need Smuggler 50 / 75 / 100).

| Level | A | B |
|---|---|---|
| 1 | **Smoke Screen** (button): your side hidden inside it for 12 s | **Flashbang** (button): enemy players within 6 m blinded 3 s, no slow |
| 25 | **EMP Disruptor** (button): 180 electro within 6 m | **Getaway** (button): 30% faster 4 s, marks on you dropped |
| 45 | **Post Slicer**: take command posts as fast as two | **Smooth Talker**: contraband pays half again |
| 65 | **Master Slicer**: your slices half again as strong, both ways (saved on the item, `slice_id:1.50`) | **Hidden Compartments**: two packages at once, no pirates at the drop |
| 85 | **Lucky**: once a life, a killing blow leaves 1 health | **Black Market**: vendors pay a quarter more |

### Ranger

Tiers from 1 (hunts take the licence).

| Level | A | B |
|---|---|---|
| 1 | **Tracking** (button): creatures within 250 m on your map 30 s; in a battle, a motion sensor spotting 20 m | **Camouflage** (button): hidden 8 s, or until you fire |
| 25 | **Snare Trap** (button): 80 damage, held 3 s | **Bolas** (button): half speed 4 s |
| 45 | **Pathfinder**: sprinting drains 25% less stamina | **Keen Senses**: enemies within 15 m show through cloak and smoke |
| 65 | **Trapper**: traps hold 5 s, two set at once | **Hamstring**: hits slow enemies 10% for 2 s |
| 85 | **Survivalist**: out of a fight, 3% health back a second | **Scout Leader**: tracking and sensor finds show to your side, twice as long |

### Merchant

Tiers from 5: anyone sells on the bazaar, so there's no licence. No buttons.

| Level | A | B |
|---|---|---|
| 5 | **Shopkeeper**: 5 more listings (`Bonus = listings`) | **Trader**: fees 2 points lower (`fee_cut`) |
| 25 | **Shop Sign**: your shop's name, first at an equal price | **Haggler**: vendors and quartermasters 10% cheaper |
| 45 | **Market Analytics**: price trends | **Appraiser**: vendors pay you 15% more |
| 65 | **Bazaar Network**: the bazaar from anywhere | **Merchant's Ledger**: 25% more Merchant experience |
| 85 | **Tycoon**: fees halved (`fee_halved`) | **Trade Empire**: 15 more listings |

### Pilot

Tiers from 5: anyone drives, so there's no licence. Two buttons at most: one at 25, one at 65. Pilot experience comes from riding on the move (6 a minute: `GameServer::ride_xp`), kills from a vehicle in battle, and the buttons.

| Level | A | B |
|---|---|---|
| 5 | **Hotshot**: vehicles you drive 10% faster, speeders and mounts too (`Modifier = vehicle_speed * 1.1`, predicted: `simulate_vehicle`'s `speed`) | **Gunnery**: your vehicle's guns hit 10% harder (`gunnery`) |
| 25 | **Field Repair** *(button)*: a quarter of its health back, 45 s | **Evasive Maneuvers** *(button)*: half damage for 5 s, 30 s |
| 45 | **Reactor Tuning**: 10% less vehicle damage | **Ejection Seat**: thrown clear alive when it's destroyed, the blast kept off for 2 s (not in space) |
| 65 | **Afterburner** *(button)*: boost for 5 s on no energy (`vehicle_boost_drain * 0`), 30 s | **Flares** *(button)*: rockets, missiles and blasts do nothing to your vehicle for 3 s, 40 s |
| 85 | **Ace**: each kill from your vehicle gives back 10% of its health | **Armored Hull**: a further 15% less vehicle damage |

### Bio-Engineer

A licence at 1 (you can't do the profession without samples); one button at most. Eggs stay a level (25), not a perk; tissues and additives are a workbench's schematics now (the Lab's were dropped).

| Level | A | B |
|---|---|---|
| 1 | **DNA Sampling** *(button)*: sample a creature, living or dead | **Field Harvester**: no button; half the creatures you kill give a sample, 10% lower in quality |
| 25 | **Hybrid Vigor**: a spliced egg +10% health and damage | **Twin Clutch**: one egg in five comes as two |
| 45 | **Gene Splicer**: a two-sample egg takes the better quality | **Tissue Culture**: crafted tissues and additives two at a time |
| 65 | **Mutation**: one egg in seven +15% health, damage or speed | **Geneticist's Notes**: 25% more Bio-Engineer experience |
| 85 | **Master Geneticist**: eggs +10% quality, samples +5% | **Tactical Strain**: hatched creatures bred small (`CompanionRecord::tactical`; drawn at `EntityState::scale`), tactical-sized, so they go into battles |

### Creature Handler

A licence at 1 (no creature without it); two buttons at most. The tier you can tame and mounts (50) stay levels; commands are free keys. The Tranquilizer Dart button is gone (Steady Hand).

| Level | A | B |
|---|---|---|
| 1 | **Beast Tamer** *(button)*: Tame | **Breeder**: no taming; hatched creatures +10% health and damage |
| 25 | **Frenzy** *(button)*: your creatures hit half again as hard for 10 s | **Mend Companion** *(button)*: a third of their health back |
| 45 | **Pack Leader**: your creatures +15% health | **Fierce Bond**: your creatures +15% damage |
| 65 | **Steady Hand**: Tame drugs it as it begins, bonding in 3 s | **Quick Recovery**: half the wait after a companion falls (`companion_death_wait`) |
| 85 | **Beast Master**: taming +15% likelier, tamed +10% quality | **The Pack**: two companions out at once in the open world (`SpecialistSync::second_companion`; a third sends the first back) |

### Droid Engineer

A licence at 1 (no assembling without it); no buttons. The chassis and modules you can build stay levels. The quality and materials perks reach assembly and the Droid Engineer's schematics (kits, speeders) alike. Extra Bay and Hardened Circuits are baked into the droid (`CompanionRecord::extra_bay`, `hardened`), so a buyer gets them.

| Level | A | B |
|---|---|---|
| 1 | **Precision Assembly**: droids +5% quality (`Bonus = droid_quality`), better grades | **Salvage Engineer**: 20% fewer materials |
| 25 | **Extra Bay**: one more module bay on droids you build; one primary still | **Repair Bay**: Advanced Droid Repair Kits (60%), four at a time |
| 45 | **Hardened Circuits**: droids you build take ×1.25 ion damage, not ×1.5 | **Engineer's Notes**: 25% more Droid Engineer experience |
| 65 | **Refit**: change a carried droid's modules, paying for the new ones (`SpecialistRequest::refit_droid`, protocol 57) | **Speeder Mechanic**: speeders +0.15 grade odds, 25% fewer materials |
| 85 | **Masterwork Droids**: droids +10% quality, exceptional grades twice as likely | **Heavy Industry**: 35% fewer materials |

## Triggers

Most of the perks that matter are conditional, so one general system builds them all: a `Trigger`, what it changes while active (the same `Modifier` / `Weapon` lines other perks use), and what it does at once.

| Trigger | Fires or holds | Where |
|---|---|---|
| `crouched`, `crouched_still` (`After`) | while crouched / after `After` s crouched without moving | simulation |
| `paused` (`After`) | the first shot after `After` s without firing | simulation |
| `rolling` | while rolling | simulation |
| `roll` | when a roll begins | simulation |
| `overheat` | when the weapon overheats | simulation |
| `kill`, `headshot_kill` (`Chain`) | a kill with the perk's weapons (an armor perk: any); `Chain`: only within that many s of the last kill | server |
| `hit` (`Within`, `Chain`) | a hit with the perk's weapons; `Within` m at most; `Chain`: only on the same target within that many s of the last hit | server |
| `target_low_health` (`Below`) | a hit on a target under that share of health | server |
| `shield_broken` | when the Deflector Shield empties | server |
| `unhit` (`After`) | while not hurt for `After` s | server |
| `low_health` (`Below`) | while under that share of health | server |
| `lethal` | a killing blow | server |
| `hit_by` | a hit taken (`Resist` is for that hit's type) | server |

While active: `Modifier`, `Weapon` (in the simulation: spread, bloom, heat, reload, recovery, speed, swap), `DamageTaken` (damage taken times this), `Resist`. A stance is active while it holds; anything else for `Duration` s from its trigger, or, for `unhit` and `low_health`, while the condition holds. `OneHit` ends it with the next hit taken; `OncePerLife` lets it fire once a life. `Cooldown` (without `Duration`) is the least time between two triggers.

At once: `Stamina` (given back), `Shield` (a share of capacity), `Cool` (heat off the weapon; 1 is all), `Reload`, `Recharge` (the shield starts recharging), `Cloak` (s), `Crit` (with `target_low_health`), `TargetModifier` + `TargetDuration` (a debuff on what's hit; a creature only feels slows).

**How it stays predicted.** A loadout carries its triggered perks (`Loadout::triggered`, sent to its own client in full precision), and each has a timer in the character's state (`MoveState::perk_time`), with how long it's been crouched and still (`MoveState::still`) and since it last fired (`CombatState::idle`). The shared simulation (`combat.cpp`) runs the stances, the roll and the overheat, so the client predicts them exactly. The server (`server_perks.cpp`) fires the rest and sets the timer, or the stamina, heat, clip or shield, in the state it sends back, and the client's reconciliation picks it up a round trip later.

## Mechanics

The **N** perks are ini lines too, on new fields ([`professions.ini`](../game/professions/professions.ini)'s header lists them):

| Perk | Lines | Where it works |
|---|---|---|
| Cool Holster | `Weapon = holster_cool * 2` | simulation: holstered weapons cool |
| Tactical Swap | `Weapon = holster_reload + 1` (an armor perk's Weapon lines are for every weapon) | simulation: a weapon swapped away with a part-empty clip reloads itself over its reload time, and finishes in the hand if it's brought back first (`WeaponState::reload`) |
| Combat Reload | `Weapon = sprint_reload + 1` | simulation: sprinting pauses a reload, unless |
| Roll Shot, Teräs Käsi Master | `Weapon = roll_fire + 1` (and `combo * 2`) | simulation: fire (swing) while rolling; the server counts hits in a row on one target within 1.5 s |
| Slide, Double Roll | `Modifier = slide_time + 0.8`, `roll_chain + 1` | simulation (`MoveState::slide`, `rolls`): a slide costs half a roll's stamina |
| Iron Guard, Riposte | `Weapon = block * 0.5`, `guard * 0.6667`; `parry * 1.6`, `riposte + 1` | simulation (the held weapon's block cost and parry window) and the server (what a block lets through, the riposte) |
| Headhunter, Flanker | `Weapon = body * 0.9`; `flank * 1.15` | server: from behind is within 60° of the target's back |
| Ricochet, Piercing Round | `Weapon = ricochet + 1`; `pierce + 1` | server: bolts mirror off the world, or go on through a body |
| Proximity Fuse | `Weapon = proximity + 1.5` | server: a missile bursts at its closest to an enemy within reach |
| Shaped Charge, Pyro | `Weapon = blast * 1.2`, `blast_damage * 0.9`; `effect * 1.5` | the weapon's radii and damage; its hit effect's time |
| Heavy Lifter | `Weapon = carry * 0.5` | its movement penalties (only those) halved |
| Deflect | `Weapon = deflect + 10` | server: blocking, a bolt from within 60° of the front is gone for 10 stamina |
| Sweeping Blows | `Weapon = knockback + 6`, `unblockable + 1` | server: thrown back (less knockback resistance), through a block but not a parry |
| Spotter, Stun Rounds | `Trigger = hit` with `Aimed` + `Mark`; `Chance` | server; aiming is the input's (`MoveInput::aiming`, the client's zoom) |
| Danger Close | `Modifier = self_blast * 0.4` | server: your own blasts |
| Steady Stance, Juggernaut | `stagger_resist + 0.5`, `explosion_push_resist + 0.5`; `stagger_resist + 1`, `slow_resist + 1` | server: staggers, knockback, and the slows on a debuff |
| Reactive Plating, Overcharge | `blast_shield + 0.25`; `shield_overcharge + 0.2` | server: the shield |
| Shoulder Charge | `Modifier = charge + 0.8` | server: sprinting, an enemy within 1.4 m ahead is staggered and shoved; then 4 s' rest |
| Low Profile, Quiet Step | `radar_quiet + 1`; `notice * 0.6` | snapshots say its shots don't ping radar (`EntityState::quiet`); creatures and outlaws scan for it at 60% of their range |
| Ammo Webbing | `ammo_capacity * 1.25` | an ammunition stack grows to 1,248 standard rounds (123 heavy) while it's worn; a stack is never cut back, so giving it up loses nothing |

**Feedback.** The HUD lists active perks above the health bar (a held one by name, a timed one with its seconds left) and flashes a perk's name when it fires: a roll's or an overheat's as the client sees it, the server's by a `CombatEvent::perk` (its character, its index). A slide draws as a crouch gliding without stepping; a shot out of a roll cuts the roll clip short for the shooting pose.

Everything a client predicts with travels in full precision (the weapons and stat modifiers of its resolved loadouts, protocol 45), so its simulation matches the server's exactly.

## Data

`professions.ini`: a weapon or armor profession's `[milestone]`s become `[perk]`s:

```ini
[perk]
Id = pistols_fan_the_hammer
Level = 45
Choice = A
Name = Fan the Hammer
Description = After a roll, pistols fire 30% faster for 3 s.
Trigger = roll
Duration = 3.75
Weapon = speed * 0.77
```

Each of the nine professions has ten perks, two at each level. The loader checks that each level has exactly one A and one B, and that a triggered perk makes sense (something to do, a `Duration` for what it changes). Other keys: `Needs = mechanic` for a perk that waits (learnable, but it applies nothing; none does now), and `Archetypes` for a perk that's for only some of its profession's weapons (its handling, and which weapons' hits and kills trigger it).

Persistence: per character extras for each profession: `trained:` (the last milestone learned), `paid:` (the highest milestone ever paid for), `perks:` and `paid_perks:` (comma-separated perk ids). Old saves: the credits spent on weapon and armor milestones are refunded and those milestones dropped (ASSUMPTION: a clean start beats mapping old milestones onto new perks). An old save may know more than ten; it can't learn more until it's under the cap.

## Order of work

1. ✅ **Core and trainer.** `[perk]` loading, known and paid perks in persistence, learning, giving up and the cap at trainers, Active Specialization Slots removed (from loadouts, the protocol, saves and the loadout screen), tacticals out of Heavy Weapons, ranks and certifications gone. Untagged perks work from here. Tagged perks can be learned, and the trainer marks them as not working yet.
2. ✅ **Triggers,** which turn on every **T** perk.
3. ✅ **New mechanics** ([Mechanics](#mechanics)).

## Open questions

- **Harvesting needs a licence now** (corpses: Foraging, or Salvaging for bandits; surveying: Surveying or Foraging for flora); looting stays open to everyone. Check this feels right in play.
- Non-weapon, non-armor milestones stay a chain at 1 / 25 / 50 / 75 / 100 for now (each learned one counts as one perk) until their own trees are reworked.
- **Numbers:** every percentage above is an ASSUMPTION, to be tuned in play.
