# Professions

Stage 7 brings Star Wars Galaxies' profession breadth into a Battlefront-paced shooter-MMO without the 4×4 box maze, nested crafting sub-components, or modal inventory clutter. See [Design](DESIGN.md) and [Roadmap](ROADMAP.md#stage-7-professions).

---

## Goal & Core Rules

- **1–100 per profession:** Every profession levels from **1 to 100**, replacing SWG's 4×4 skill-box grid and ~30 fragmented XP pools.
- **Direct XP:** Each profession earns XP by doing its activity (fighting with that weapon, mitigating hits in that armor, harvesting, filling work orders, healing, scouting, performing, commanding companions) plus mission, contract, and match XP routed to the professions active in your loadout.
- **Horizontal milestones:** Major unlocks sit at **1 / 25 / 50 / 75 / 100** (gear certifications, gadgets, active abilities, masterwork schematics). Intermediate levels (every 5–10) grant small handling, efficiency, yield, or experimentation perks rather than raw DPS or HP bloat.
- **Standard vs. Gear queue:** In **Standard Queue**, crafted item quality is flattened so only sidegrade choices and active kit options matter. In **Gear Queue** (and open-world GCW), crafted quality counts within each item's sidegrade band.
- **Uncapped leveling + Active Specialization Slots:** One character can level every profession to 100. Combat balance is enforced per loadout: each of a character's **3 loadouts** holds weapons, armor, the universal **Deflector Shield**, a fixed number of **Active Specialization Slots** (active combat/utility profession perks), and **1 Companion Slot**. Non-combat professions (gathering, crafting, entertaining, trading) apply whenever you perform those activities without unslotting combat perks.
- **Universal Deflector Shield slot:** Every character has a **Personal Shield Generator (PSG)** equipment slot regardless of armor class. Shields provide a recharging buffer absorbed before health, with sidegrade profiles (capacity, recharge rate, recharge delay, resistance) and built-in counterplay: `electro` drains/overloads shields, and `melee` pierces or disrupts them.

---

## Streamlined Gathering & Crafting (Anti-Clutter Tweaks)

SWG's resource-stat economy and experimentation stay intact, while the repetitive UI and bag management are stripped out:

### Gathering
1. **Unified Multi-Scanner:** One scanner gadget/key switches between Mineral, Chemical, Gas, Flora, and Water (scroll wheel or tabs). Instead of opening a modal survey window every 50 m, it projects **3D holographic contour lines and directional pings** onto the terrain in-world.
2. **Active Field Extraction:** Manual gathering uses quick, physical interactions on visible nodes, vents, wrecks, and corpses (laser cutter, core sampler, field skinning) with skill-based yield bonuses—no AFK kneeling macros.
3. **Harvester Network (Stage 10+):** Automated harvesters are still placed and defended physically in the open world, but fueling, maintenance, and hopper collection can be managed from a **centralized Harvester Network terminal** instead of visiting every hopper across multiple planets.
4. **Dedicated Material Vault:** Harvested resources go into a separate **Material Vault / Resource Bin** rather than gear inventory. Resources are grouped by class and sortable by plain-English stat names (e.g., *Conductivity*, *Quality*, *Decay Resistance*) instead of cryptic two-letter codes.

### Crafting
1. **Single-Screen Unified Workbench:** Replaces SWG's 5-step crafting wizard with one screen:
   - **Left:** searchable, pinnable schematics.
   - **Center:** material slots with one-click **"Auto-Fill Best for [Stat]"** and **"Auto-Fill Cheapest"** pulling directly from the Material Vault (with manual override).
   - **Right:** live stat preview showing exact stat contribution weights, **visible experimentation odds**, and experimentation sliders.
2. **1-Step Base Crafting + Optional Enhancement Slots:** Base items craft in a single step from raw or refined materials—eliminating SWG's multi-layer sub-component crafting pyramids. Cross-profession depth lives in **1–2 optional Enhancement/Module Slots** per schematic (e.g., a Bio-Engineered tissue, a Weaponsmith precision barrel, a Droid targeting chip, or rare creature loot).
3. **Recipe Presets & Batch Crafting:** Save a configured schematic (materials + experimentation split) as a reusable **Recipe Preset**, then craft 1 or *N* items in a single action.
4. **Work Orders & Salvage (No Grind-Trash):** Leveling comes from **Faction Requisitions / Hub Work Orders** (supplying real kits to quartermasters and NPCs for credits + XP), player orders, and **Salvaging** failed prototypes or unwanted loot back into partial materials—never crafting hundreds of throwaway junk items just to destroy them.

---

## Complete Profession Roster (31 Professions, 1–100)

Professions are grouped into three implementation waves. SWG's six starter professions (*Artisan, Marksman, Brawler, Scout, Medic, Entertainer*) dissolve into these 1–100 lines rather than acting as throwaway prerequisite gates.

### Wave 7A — Foundation (17 Professions)

#### Decoupled Armor Professions (4)
Armor proficiency is completely decoupled from weapon and advanced professions. Appearance stays a separate cosmetic layer.

| Profession | Role & Stat Profile | Baseline SWG Sets | Key 1–100 Unlocks |
|---|---|---|---|
| **Unarmored** | Clothing only; fastest movement, roll/stamina efficiency, natural synergy with Wookiee/Ithorian innate traits | Civilian clothing + innate species traits | Roll distance/recovery, sprint efficiency, clothing-weave utility, evasion timing |
| **Light / Recon Armor** | High mobility, fast stamina regen, strong `electro` / `bio` mitigation, low general mitigation | `ubese`, `tantel`, `scout_trooper`, `chitin`, `bone`, `zam`, `nightsister`, `singing_mountain_clan`, `kashyyykian_hunting`, `ithorian_sentinel` | Reduced scanner/minimap signature, sprint/roll efficiency, mantle/mobility perks, extra utility pouches |
| **Medium / Battle Armor** | Balanced general + `laser` / `ballistic` mitigation, standard soldier movement | `padded`, `marine`, `rebel_battle`, `stormtrooper`, `clone_trooper`, `bounty_hunter`, `marauder`, `kashyyykian_ceremonial`, `ithorian_guardian` | Negates medium movement/acceleration drag, balanced resistance tuning, extra secondary/grenade bandolier capacity |
| **Heavy / Assault Armor** | High general + `explosive` / `melee` / `ballistic` mitigation, slower sprint | `composite`, `rebel_assault`, `assault_trooper`, `ris`, `mandalorian`, `kashyyykian_black_mtn`, `ithorian_defender` | Reduces speed/stamina penalties, **explosion knockback (`explosion_push`) resistance**, braced firing stability |

#### Weapon Professions (5)
- **Pistols:** Fast draw/swap, low movement penalty while firing, tight hipfire, sidearm utility, stun/disruptor bolts.
- **Carbines:** Run-and-gun CQB, burst/scatter/automatic carbines, fast reload on the move, close-range breaching.
- **Rifles:** Precision mid-to-long range (automatic, battle, and sniper rifles), crouch/scope steadiness, bloom recovery, range-drop discipline.
- **Heavy Weapons:** Launchers, mortars, flamethrowers, mines, detpacks, and thermal detonators; splash control, anti-vehicle/anti-destructible punch, recoil bracing.
- **Melee:** Consolidated 1H, 2H, polearm, and unarmed/Teräs Käsi styles; lunge, block/parry, stagger, and deflector-shield piercing/disruption.

#### Gathering Professions (3)
- **Surveying & Mining:** Minerals, ores, gases, chemicals, energy, and water; scanner contour depth, yield, rare node detection.
- **Foraging & Harvesting:** Flora/wood plus field-harvesting hides, meat, bone, and rare organic components from Stage 5 wildlife.
- **Salvaging:** Recovering alloys, electronics, power cells, and weapon/droid components from defeated droids, bandits, wrecks, and unwanted gear.

#### Core Crafting Professions (5)
- **Weaponsmith:** Ranged and melee weapons, weapon enhancement modules (barrels, scopes, cooling cores), ammo/ordnance packs, weapon repair kits.
- **Armorsmith:** Light/Medium/Heavy armor pieces, universal **Deflector Shields (PSGs)**, armor plating modules, armor repair kits.
- **Tailor:** Cosmetic clothing/appearance outfits, armor-weave linings, bags/bandoliers, dye kits.
- **Chef:** Rations, meals, and drinks providing clean, readable timed buffs (stamina regen, heat dissipation, crafting/gathering focus) without SWG's 50-item stacking clutter.
- **Architect:** Field camps, portable cover, and interior furniture/decor now; full player housing, shops, factories, and harvesters in Stage 10+.

### Wave 7B — Specialist Combat, Support, Social & Economy (10 Professions)

*Live since Stage 12: each ability, its milestone and its numbers are in [`game/professions/specialists.ini`](../game/professions/specialists.ini); what's done and what's still open is in the [Roadmap](ROADMAP.md#stage-12-remaining-professions-waves-7b--7c--creature-mounts).*

- **Bounty Hunter:** Open-world bounty terminal contracts (NPC marks + opt-in PvP marks), seeker probe tracking, capture payouts; match recon probes, bolas/net/wrist-rocket/jump-pack gadgets, and HVT spotting.
- **Smuggler:** Slicing loot containers, faction terminals, and contraband missions plus permanent **sidegrade weapon/armor slicing**; match faster objective/turret/door slicing, flashbangs, smoke, and EMP disruptors.
- **Squad Leader:** Battlefront Officer role—team aura buffs (stamina/reload/morale), tactical ping/target marking, **squad spawn beacon**, and supply/strike call-ins.
- **Doctor:** Pharmaceutical crafting (stims, medpacks, bio-antidotes) plus field bacta dispenser, revive/defib tool, and bio cleanse—removing SWG's tedious wound/battle-fatigue hospital lockout.
- **Combat Medic:** Frontline ranged healing darts/grenades paired with **`DamageType::bio`** area-denial grenades and payloads.
- **Ranger:** Wilderness tracking, short-duration stealth cloak (broken on firing), motion sensors, trip-traps, and deployable **forward camps** for field healing and loadout swaps.
- **Entertainer:** Unifies SWG *Musician* and *Dancer*—cantina and camp performances grant quick **15–30 s Inspiration/focus buffs** (not 10-minute AFK sitting), synced band/dance emotes, and an optional portable morale holo-emitter.
- **Image Designer:** Live character re-customization (hair, markings, blend-shape sliders, species styling) plus custom armor/clothing trim and holographic flair.
- **Merchant:** Player vendors, shop slots, bazaar listing limits, trade analytics, market fee reductions, and faction supply contracts.
- **Pilot:** Ground vehicle handling and field repair (Stage 6) plus starfighter maneuvers and reactor tuning in Space Battles (Stage 8).

*Deferred / Parked:* **Politician** is stubbed/deferred to Stage 10+ (player city founding, zoning, civic structures, and city war terminals). **Jedi / Force** remains parked/undecided per [Design](DESIGN.md#open-questions).

### Wave 7C — Companions & Bio/Robotics Ecosystem (4 Professions)

*Live since Stage 12: companion templates (10 creatures, 6 droid chassis), droid modules and mounts are in `specialists.ini` too.*

- **Bio-Engineer:** Samples DNA from Stage 5 wildlife to craft engineered **creature eggs**, organic armor tissues, and enhanced stim/chef additives.
- **Creature Handler:** Active wild taming, hatching/raising Bio-Engineered eggs, companion commands, behavior modules, and **creature mounts** (connecting to Stage 6).
- **Droid Engineer:** Modular droid assembly (chassis + plug-in Core, Primary Function, and Utility modules) and repair kits.
- **Droid Operations:** Field deployment, remote repair, overcharge abilities, and combat chassis certification—mirroring Creature Handler so pure crafters sell combat droids to frontline operators.

### SWG Profession Crosswalk

| Classic SWG Profession | Destination in Stage 7 |
|---|---|
| Artisan, Marksman, Brawler, Scout, Medic | Dissolved into Wave 7A/7B 1–100 professions (no throwaway starter gates) |
| Pistoleer, Carbineer, Rifleman, Commando | **Pistols**, **Carbines**, **Rifles**, **Heavy Weapons** |
| Fencer, Swordsman, Pikeman, Teräs Käsi Artist | **Melee** (consolidated) |
| Weaponsmith, Armorsmith, Tailor, Chef, Architect | **Weaponsmith**, **Armorsmith**, **Tailor**, **Chef**, **Architect** |
| Bounty Hunter, Smuggler, Squad Leader, Ranger | **Bounty Hunter**, **Smuggler**, **Squad Leader**, **Ranger** |
| Doctor, Combat Medic | **Doctor**, **Combat Medic** |
| Musician, Dancer | **Entertainer** (unified) |
| Image Designer, Merchant | **Image Designer**, **Merchant** |
| Bio-Engineer, Creature Handler, Droid Engineer | **Bio-Engineer**, **Creature Handler**, **Droid Engineer** + **Droid Operations** |
| Pilot (JTL) | **Pilot** (Stages 6 & 8) |
| Politician | Deferred to Stage 10+ (Player Cities) |
| Jedi | Parked / Undecided ([Design](DESIGN.md#open-questions)) |
| *(New)* | **Unarmored**, **Light/Medium/Heavy Armor** (4), **Surveying & Mining**, **Foraging & Harvesting**, **Salvaging**, **Droid Operations** |

---

## Special Attention: Animal Taming

Animal taming bridges open-world Tatooine exploration and instanced Battlefront matches without breaking corridor pathing or flooding the screen with pets.

1. **Bio-Engineer → Creature Handler Pipeline:**
   - **Bio-Engineer** samples DNA from Stage 5 creatures and crafts customized **creature eggs** (inheriting traits, resistances, and colorations), alongside organic armor tissues and stim additives.
   - **Creature Handler** tames wild creatures via an active loop—approach calmly, use bait or tranquilizer darts, and complete a short bonding sequence (profession level gates max creature tier/ferocity)—or hatches Bio-Engineered eggs.
   - Tamed creatures live in a **Datapad / Stables roster** and can be swapped out of combat.
2. **Context Ping UX (No `/tellpet` Macros):**
   - A single pet-command key uses Battlefront-style context pings: ping an enemy = *Attack / Harass*; ping ground = *Hold / Guard*; double-tap = *Follow / Recall*.
   - Leveling 1–100 unlocks **behavior modules** (Flank, Sniff Stealth/Mines, Fetch Loot, Interception/Stagger) and **Creature Mounts** (connecting to Stage 6).
3. **Open World vs. 32v32 Match Rules:**
   - **One active companion at a time** (pet *or* combat droid—never multi-pet swarms).
   - **Open World:** Full roster allowed, including mounts (dewback, bantha, kaadu) and large predators (rancor, kimogila, etc.) for hunting, lairs, and open-world GCW.
   - **Instanced 32v32 Matches:** Deployable pets are restricted to **tactical-sized beasts** (humanoid-or-smaller footprint such as vornskr, sand panther, gurreck, nexu) so they path cleanly through doors and corridors on `core::NavGrid` without body-blocking teammates. Match pets have clear team silhouettes, readable HP, and a respawn cooldown on death.

---

## Special Attention: Combat Robots

Combat and support droids run directly on the existing `core::Bot` + `core::NavGrid` simulation and fit Battlefront's sandbox naturally.

1. **Modular Droid Crafting (Droid Engineer, 1–100):**
   - **Chassis** (from SWG droid models): compact recon (MSE-6, Probe), support (R2/R4, Gonk, Surgical), light combat (Probot, Battle Droid), and heavy combat (Droideka / Heavy Combat Chassis).
   - **Plug-in Bays** (bay count scales with chassis tier, keeping assembly to 1 step + modules):
     - *Core / Armor:* HP, movement speed, damage-type mitigation.
     - *Primary Function:* light blaster, repeater, stun/EMP emitter, repair beam, bacta spray, ammo dispenser, or scanner/jammer.
     - *Utility / World:* portable workbench, mobile salvage/storage, survey assist, or detpack carrier.
2. **Crafting vs. Field Command (Droid Operations, 1–100):**
   - **Droid Engineer** builds and upgrades chassis and modules.
   - **Droid Operations** certifies fielding higher-tier combat chassis, remote repair, tactical overcharges, and battlefield positioning—ensuring a real market between engineers and frontline players.
3. **Open World vs. 32v32 Match Rules:**
   - Shares the **1 active companion limit** and **cooldown on destruction** with tamed pets.
   - Uses the same **single-button context ping** (*Attack*, *Guard Point*, *Follow*, *Recall/Repair*).
   - **Clear Battlefield Roles & Counterplay:**
     - *Recon / Probe:* spots enemies on minimap, light harassing fire, low HP.
     - *Support:* mobile mini health/ammo/repair unit (weaker than fixed map droids, destructible).
     - *Combat:* sustained fire or point defense. All droids take bonus **`DamageType::electro` (EMP/ion)** damage so anti-droid counterplay works natively through Stage 1/4 damage types.

---

## Architecture & Persistence Hooks

- **Data-driven definitions (`core/`):** Profession trees, 1–100 milestone tables, schematics, resource classes, and companion templates live in data files and simulate in C++ `core/` (see [Architecture](ARCHITECTURE.md#persistence)).
- **Persistence (`core::CharacterStore`):** Character profession levels/XP, Material Vault contents, Recipe Presets, and stabled pets/droids persist through the persistence interface (SQLite now, Postgres later). Since Stage 12, datapads (companions), DNA samples and the bounty opt-in are per-character extras, and the bazaar is world extras (`get_extra(0, ...)`).
- **Specialist rules (`core/specialist.h`, Stage 12):** abilities and their effects, timed buffs and debuffs as loadout gear (so clients predict with them), companions, taming, eggs, droid modules, contracts, bazaar fees and price trends, and slicing. A `core::Loadout` carries its abilities, specialist levels and Companion Slot companion into matches.
- **Loadouts (`core/loadout.h`):** Extends Stage 4 loadouts with the universal **Deflector Shield** slot, **Active Specialization Slots**, and **1 Companion Slot**.
- **Item compatibility:** Crafted weapons, armor, shields, and enhancement modules remain standard Stage 4 item instances with rolled/experimented stat values inside their sidegrade band.
