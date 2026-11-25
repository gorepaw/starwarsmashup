# Roadmap

Battles come first, the world after. A match has clear boundaries and can be tested. The persistent world is the hardest part, so it comes last and grows out of working battles.

This roadmap combines the completed foundation (**Stages 1–8**) with all newly identified fixes, redesigns, and remaining system milestones (**Stages 9–14**).

## Stage Overview

| Stage | Focus | Status | Outcome |
|---|---|---|---|
| [Phase 0](PHASE0.md) | Feasibility spikes | Done | The tech works |
| 1 | Battlefront match queued from a Galaxies city | Done | The core loop works |
| 2 | Match polish | Done | Matches feel like Battlefront |
| 3 | Front end and characters | Done | A main menu, and a character of any species |
| 4 | Items, weapons and armor | Done | Galaxies gear in an inventory and loadouts |
| 5 | Tatooine around Mos Eisley | Done | A loop outside the city: hunt, loot, sell, quest |
| 6 | Vehicles | Partial (see Stage 11 & 12) | Battlefront's in matches, Galaxies' in the world |
| 7 | Professions (Wave 7A + framework) | Done (Waves 7B & 7C in Stage 12) | Core 1–100 framework & 17 foundation professions live |
| 8 | Space combat | Done (base maps/ships) | Battlefront II's space battles |
| **9** | **Bug Fixes, UI/UX Polish & Authentic Mos Eisley** | **Done (1 item open)** | Clean visuals, authentic SWG city layout, terminal audit, and tight open-world QoL (Starport boulder clipping open) |
| **10** | **4 Factions, 5 Loadouts, Cosmetics, Ammo & Bot Roster** | **Done** | 4-faction system, cosmetic FP & WoW-style reputation, unified ammo, and clothed stock bots |
| **11** | **Ground/Space Terminals, Clone Wars, Modes, Weather & Telemetry** | **Next** | Dual battle terminals, full Clone Wars roster, extra BF2 modes, Kamino rain, and match analytics |
| **12** | **Remaining Professions (Waves 7B & 7C) & Creature Mounts** | **Done (follow-ups listed)** | All 14 remaining specialist, bio/creature, and droid professions + creature mounts |
| **13** | **Server Scalability & Multi-City Expansion** | **Planned (Cities on hold)** | Proximity-based open-world AI simulation, resource audit, and additional cities/planets |
| **14+** | **Endgame World Systems** | **Planned** | Deep resources & harvesters, housing & player cities, open-world GCW, and full-loot zones |

The battlefield evolves alongside: **instanced matches on Battlefront II's own maps → much later, command-post battles built from Galaxies assets in the open world.**

---

## Updated Design & System Decisions

These decisions supersede or extend the earlier Stage 1–8 notes below:

1. **Single Gear-Matters Queue & Locked Hard Bots:** Remove the normalized/Standard queue (`QueueMode::standard`) and adjustable bot difficulty. Matches use **Gear-Matters** rules only, and match bots are locked to **Hard**.
2. **Five Loadouts per Character:** Expand per-character loadouts from 3 to **5** (`core::max_character_loadouts = 5`). Each loadout stores both functional gear choices and a **Cosmetic Override** configuration.
3. **Four Factions & Currency Split:** Expand from 2 sides to **4 factions** (**Rebellion, Empire, Republic, Separatists**). **Faction Points (FP)** become a **cosmetic-only currency** spent at faction quartermasters (cosmetic gear and full-body overrides), while **Credits** are the power/gear currency.
4. **Faction Reputation & Ranks:** Earning FP grants reputation with that faction and subtracts **half** as much from the opposing faction (allowing players to eventually max every faction). Standing tiers and numeric thresholds follow World of Warcraft's 7-tier ladder (**Hated, Unfriendly, Neutral, Friendly, Honored, Revered, Exalted**), to be tuned for theme and balance later.
5. **Unified Ammo Economy:** Replace per-weapon ammo pools with equipped stackable ammo items by ammo type (**999** standard / **99** heavy), craftable by Weaponsmiths with horizontal specialty sidegrades (no power-scaling/expensive endgame ammo). Roll out in the open world first, then extend to BF2 matches. Tactical/secondary weapons refresh **1 every 20 s** in the open world (reducible by stats/perks later) while still requiring ammo droids in BF2 matches.
6. **City No-Extract Rule & Open-World Death Penalty:** Resource extraction is blocked within a radius of any city or building (including future player cities). Dying in the open world incurs a **10% credit penalty** (with full-loot zones planned later).

---

## Stage 9: Bug Fixes, UI/UX Polish & Authentic Mos Eisley

### Goal

Fix immediate visual, collision, HUD, and gameplay bugs, polish the open-world loop, and move Mos Eisley's vendors, trainers, and terminals into authentic Star Wars Galaxies positions (verified strictly from SWG gamefiles/snapshots or verified references—never guessed).

### Tasks

**Character, Model & Animation Fixes**
- [x] **Fix Z-fighting clothing & armor meshes:** Resolve skin/mesh clipping and Z-fighting on:
  - Stormtrooper armor
  - Female Bith walking around in dress
  - Artisan Tuvi
  - Wookiee player character
  - Teska Vo
  - Bolo Dreel
  - Rebel Quartermaster
- [x] **Fix female Twi'lek lekku:** Male lekku replacement was fixed (`PlayerAppearance.hide_replaced_head_parts`), but female Twi'lek heads still render overlapping/double lekku. Fix female head-part swapping in both character creator and in-world rendering.
- [x] **Stagger NPC idle animations:** Offset/randomize idle animation start phases on hub NPCs and `[crowd]` walkers so nearby characters do not animate in lockstep.
- [x] **Fix turret character model disappearance:** Prevent the player's character model from vanishing when occupying a turret in BF2 matches.

**World Collision, Loading & Weather (Pre-Stage 11 Quick Fix)**
- [ ] **Fix Mos Eisley Starport boulder clipping:** Remove or adjust the giant boulder intersecting the Mos Eisley Starport structure/terrain.
- [x] **Battlefront II–style loading screens:** Obscure world/map loads with a BF2-style loading screen across all major transitions (main menu → hub, hub → multiplayer/bot match, match → hub).

**HUD, Cursor & Text Polish**
- [x] **Battlefront II mouse cursor:** Replace the default OS cursor with Battlefront II's mouse cursor across menus and UI screens (`bf2_art.gd` / Godot cursor settings).
- [x] **Shield bar dark outline:** Add a dark outline to the Deflector Shield bar in `game/ui/bf2_hud.gd` to match the health and stamina bars.
- [x] **Brighter weapon silhouette/icon in HUD:** Increase contrast/visibility of the equipped weapon image in the bottom-left weapon panel (`bf2_hud.gd`).
- [x] **Profession level-up HUD notification:** Display a BF2-style banner/notification element whenever any profession gains a level.
- [x] **Fix truncated/clipped HUD & quest text:** Ensure long notification strings wrap or fit cleanly inside screen margins without clipping on left, right, or bottom edges—specifically:
  - Crafting status/result text at the bottom (`workbench_screen.gd` / `hub_hud.gd`)
  - Starting quest notification when reaching the cloning facility (`tatooine_quests.ini`)
  - Wort hides quest (*Something for the Stewpot*) completion text

**Quests, Looting, Harvesting & Open-World Rules**
- [x] **Orange `!` icon for ready-to-turn-in quests:** Add a distinct orange exclamation-point hologram/map icon when an active quest is complete and ready to turn in.
- [x] **Streamline quest completion dialogue:** Talking to a quest giver (`quest_screen.gd`) when turning in a completed quest should display the completion text and reward directly instead of opening an empty quest-giver menu afterward.
- [x] **Fix Wort Hides quest marker tracking:** Fix objective tracking (`tatooine_quests.ini` / `core::QuestBook` / radar & world markers) so *Something for the Stewpot* properly guides the player to worrt packs during the collection step (turn-in tracking already works).
- [x] **Mission terminal icons:** Add a small mission/quest chevron hologram over `[mission_terminal]` objects (`place_symbols.gd`).
- [x] **Loot All + Auto-Harvest on second `E` press (and fix harvesting):**
  - Investigate and fix corpse harvesting not working properly.
  - While the loot window (`loot_screen.gd`) is open, pressing `E` (or the bound interact/loot key) a second time triggers **Loot All** and automatically **harvests** the corpse.
- [x] **Speeder / Mount equipment slot & keybind:** Move personal vehicles/mounts out of consumables/bag-use into a dedicated **Speeder/Mount equipment slot** with a rebindable **Summon/Unsummon** keybind.
- [x] **No resource extraction in cities:** Enforce a building-proximity check on extraction (`ServerConfig` / `hub.cpp`) so players cannot extract resources within a defined radius of any city building (covering Mos Eisley now and player-placed cities in Stage 14).
- [x] **Open-world death penalty (10% credits):** Deduct **10% of carried credits** on open-world death (safe credits remain in the Bank; full-loot danger/PvP zones planned for Stage 14).

**Authentic Mos Eisley Placements & Terminal Audit**
*Rule: Every placement must be verified from actual SWG gamefiles (`tatooine.objects.json`, `.pob` portals/cells, TRE snapshots) or verified SWG documentation—never guessed.*

- [x] **Relocate War Terminal:** Move off the middle of spawn; integrate with the Stage 11 split into a dedicated **Ground Battle Terminal** (at a fitting plaza/faction spot) and **Space Terminal** (`shared_terminal_space.iff` inside the Starport, handling both Space Battles and interplanetary travel). *(Terminals relocated and labeled; Ground vs Space queue split remains in Stage 11.)*
- [x] **Relocate Faction Quartermasters:** Move Imperial and Rebel Quartermasters out of the middle of the spawn road to appropriate, readable locations (expanded to 4 factions in Stage 10).
- [x] **Audit & relocate Skill Trainers + fix building access:** Verify authentic SWG trainer locations; fix any trainers placed inside or behind inaccessible buildings (enable building doors/entry or place outside their authentic guild/hall doorways).
- [x] **Move Junk Dealer & Gear Vendor out of the Starport:** Relocate `Mos Eisley Junk Dealer` and `Mos Eisley Gear & Supplies` out of the Starport to authentic SWG bazaar/market positions, reserving the Starport for space terminals and travel.
- [x] **Align & differentiate Public Crafting Stations:** Confirm the courtyard terminal near Artisan Tuvi / Tailor Nenna is a crafting station, center its interaction radius and purple wrench hologram directly over the physical terminal, and wire all differentiated public crafting stations in the snapshot (`shared_public_weapon_station.iff` [2], `shared_public_clothing_station.iff` [2], `shared_public_food_station.iff` [2], `shared_public_structure_station.iff` [2], and `shared_food_station.iff` [2]) so each is labeled and opens/filters to its corresponding crafting discipline (Weapons/Munitions, Clothing/Armor, Food/Chef, Structures/Architect).
- [x] **Align Bacta Tank icon & activation:** Snap the `clinic` service trigger and icon directly onto `shared_item_medic_bacta_tank.iff` (`3521.01, -4773.86` in `tatooine.objects.json`, currently offset at `3518.5, -4774` in `game/hubs/mos_eisley.ini`).
- [x] **Individual Bank terminal icons & triggers:** Inspect inside `shared_bank_tatooine.iff` / Mos Eisley bank snapshot and place separate icons and activation triggers directly over each individual bank terminal (e.g., all 4 terminals if 4 exist) instead of one generic point.
- [x] **Wire remaining Mos Eisley terminals (specialized, stubs, and Space Terminal):** Every terminal/kiosk present in `game/imported_assets/terrain/tatooine.objects.json` gets an in-game label, hologram/marker, and interaction:
  - *Mission Terminals (specialized & remaining):* Label and wire each specialized mission terminal type separately with its own functional screen/feed (showing its distinct mission category and a clean placeholder state until profession/faction quests of that type are added): `shared_terminal_mission_artisan.iff` (8), `shared_terminal_mission_bounty.iff` (6), `shared_terminal_mission_entertainer.iff` (10), `shared_terminal_mission_scout.iff` (10), `shared_terminal_mission_imperial.iff` (2, interior), `shared_terminal_mission_rebel.iff` (2), plus the remaining 6 general `shared_terminal_mission.iff` (16 exist vs 10 currently wired in `mos_eisley.ini`).
  - *Bazaar Terminals (`shared_terminal_bazaar.iff`, 8):* Label and implement as an interactive **Auction House / Bazaar stub** until full Merchant & player-economy functionality is built in Stage 12.
  - *NewsNet Terminals (`shared_terminal_newsnet.iff`, 2 inside Starport building 358):* Label and implement as a **Dev Newsboard / Galactic News** terminal reading developer-authored news/patch messages from a data file.
  - *Space Terminals (`shared_terminal_space.iff`, 7 inside Starport building 358):* Confirmed as the sole space/travel terminal template in the Mos Eisley snapshot; unify **Space Battle queueing** (Stage 11) and **interplanetary travel** (Stage 13, stubbed until additional planets open) at these terminals.
  - *Gambling & Generic Data Terminals (stubs):* Label and add stub interactions for `gambling/slot/shared_standard.iff` (7), `gambling/table/shared_table_base.iff` (4), `gambling/wheel/shared_roulette.iff` (4) in building 47, plus generic `shared_data_terminal_*` consoles across interiors.

### Exit Criteria

Mos Eisley has zero Z-fighting on player/NPC outfits, fixed female Twi'lek lekku, staggered NPC idles, BF2 cursor and loading screens, working corpse harvesting with double-`E` loot-all/harvest, clean HUD text, authentic verified terminal/NPC placements, differentiated crafting stations, and labeled/interactive specialized terminals and stubs.

---

## Stage 10: 4 Factions, 5 Loadouts, Cosmetics, Unified Ammo & Bot Roster

### Goal

Overhaul loadouts, queues, factions, currency progression, ammo logistics, and bot visuals so matches are fair, visually authentic, and deeply connected to open-world crafting and faction identity.

### Tasks

**Loadout & Queue Restructuring**
- [x] **Expand from 3 to 5 loadouts:** Increase `core::max_character_loadouts` from 3 to 5 across `core/loadout.h`, SQLite persistence, hub quick-switch bindings, inventory/equipment UI, and battle spawn screen (`spawn_screen.gd`).
- [x] **Remove Standard Queue & Bot Difficulty Settings:**
  - Remove `QueueMode::standard` (quality-flattened queue); all BF2 matches run under **Gear-Matters** rules.
  - Remove adjustable bot difficulty from options and server UI; lock match bots to **Hard**.

**4 Factions, Cosmetic Overrides & WoW-Style Reputation**
- [x] **4 Playable Factions:** Support **Rebellion, Empire, Republic, and Separatists** across hub queueing, quartermasters, reputation storage, and battlefields.
- [x] **Cosmetic Override Tab per Loadout:** Add a **Cosmetic Override** tab in the Equipment/Gear screen (`inventory_screen.gd` / `core::CharacterLoadout`) so each of the 5 loadouts maintains separate functional gear slots and cosmetic appearance slots (including full-body overrides).
- [x] **FP = Cosmetic Currency / Credits = Power Currency:**
  - Rework Faction Quartermasters (`quartermaster_screen.gd`) so Faction Points (FP) buy **cosmetic-only rewards** (armor appearances, uniforms, banners/trims, and full-body **CIS battle droid overrides**).
  - Move combat gear, weapons, and power purchases to **Credits** (and crafting/loot).
- [x] **Faction Reputation & Ranks (WoW Standing Model):**
  - Track persistent reputation per character for all 4 factions using WoW's numeric standing thresholds: **Hated, Unfriendly, Neutral, Friendly, Honored, Revered, Exalted**.
  - Earning FP in a match or faction activity adds an equal amount of reputation to that faction and subtracts **half** that amount from its opposing faction (Rebellion ↔ Empire, Republic ↔ Separatists), allowing players to eventually max all 4.
  - Gate cosmetic quartermaster tiers on faction rank; refine custom faction rank titles and balance later.

**Unified Ammo System (Open World First → BF2 Matches)**
- [x] **Equipped Ammo Pool & Stackable Ammo Items:**
  - Replace per-weapon ammo counts with equipped ammo inventory items keyed by **ammo type**.
  - Cap equipped/stored ammo at **999** rounds for standard weapons and **99** for heavy weapons (`WeaponDef` / `core::Loadout`).
  - Add Weaponsmith schematics so players craft standard and future specialty ammo types (e.g., burning, cryo) as horizontal sidegrades—explicitly avoiding raw DPS power-scaling or expensive endgame ammo.
- [x] **Open-World Tactical/Secondary Recharge:**
  - In the open world, tactical/secondary weapons (grenades, etc.) regenerate **1 charge every 20 seconds** (reducible by gear/stats/perks later) up to normal BF2 carry caps (unless increased by perks/abilities).
- [x] **Extend Ammo to BF2 Matches:**
  - Once balanced in the open world, switch BF2 matches to consume the player's own carried ammo reserves; BF2 **ammo droids** remain required in matches to replenish tactical/secondary charges.

**Stock Bot Character Pool ("Hordes of Naked Men" Fix) & BF2 Character Imports**
- [x] **Import missing BF2 character/armor/droid assets:** Where SWG lacks assets (Clone Trooper armor phases/legions, CIS battle droids—B1, B2, MagnaGuard variants), import BF2 character and clothing/armor models for use as player cosmetic overrides and bot gear. *(Exported Phase I/II Clones, ARC, Jet, Heavy, Commander, B1, B2, and MagnaGuard via `bf2tool export-character`; non-humanoid Droideka walker rig not yet imported as a player override.)*
- [x] **Generate Stock Bot Character Roster:** Build a large, varied pool of generated stock characters per faction:
  - Faction-appropriate names, species distributions (Rebels spanning diverse SWG species; Empire human/imperial uniforms; Republic clones; Separatists using full-body CIS droid overrides), and varied combat strength/gear tiers.
  - Each stock bot carries **5 complete, clothed, faction-appropriate loadouts** so bot matches never spawn unclothed placeholder soldiers.

### Exit Criteria

Players configure 5 functional + cosmetic loadouts, queue into Gear-Matters matches for any of the 4 factions against Hard-locked clothed stock bots, earn cosmetic FP and WoW-tiered reputation, and manage craftable ammo stacks in the open world and matches.

---

## Stage 11: Battle Terminals, Clone Wars Roster, Modes, Weather & Telemetry

### Goal

Split Ground and Space matchmaking with full Clone Wars map coverage, expand playable Battlefront II modes and planetary weather, finish remaining Stage 6 vehicle mechanics, and capture match analytics.

### Tasks

**Separate Terminals & Clone Wars Map Roster**
- [ ] **Ground Battle Terminal & Space Terminal (Battles + Interplanetary Travel):**
  - Split `[recruiter]` into distinct **Ground Battle Terminal** and **Space Terminal** (`shared_terminal_space.iff` inside Mos Eisley's Starport) types.
  - Both battle queues let the player choose **Galactic Civil War** or **Clone Wars** before queueing; the Space Terminal also hosts interplanetary travel once Stage 13 planets open.
- [ ] **Complete Clone Wars Ground & Space Map Roster:**
  - Audit all BF2 ground maps (`cor1`, `fel1`, `geo1`, `kam1`, `kas2`, `mus1`, `myg1`, `nab2`, `pol1`, `tat2`, `uta1`, `yav1`, `dea1`, etc.) and space maps (`spa1`, `spa3`, `spa6`, `spa7`, `spa8`, `spa9`, plus check `_lvl_pc` for any remaining BF2 Clone Wars space maps such as Coruscant/Utapau if shipped).
  - Export/wire Clone Wars (`Republic` vs `Separatists`) conquest and space assault battlefield configs alongside GCW configs so both eras have complete map pools on both terminals.

**Game Modes, Weather & Remaining Vehicle Polish**
- [ ] **Additional BF2 Game Modes:** Design and implement BF2 modes beyond Conquest and Space Assault (e.g., Capture the Flag, Hunt, Assault / Hero Assault, XL) in `core/` and mission-script export (`bf2tool`).
- [ ] **Kamino Rain & Planetary Weather:**
  - Implement rain particles, wet/storm ambience, and lighting for **Kamino** (`kam1`).
  - Audit and add weather effects for other planets (sandstorms/dust on Tatooine & Geonosis, blowing snow on Hoth, ash/embers on Mustafar, mist/rain on Dagobah & Felucia).
- [ ] **Snowspeeder Rear Gunner & Tow Cable** *(carried over from Stage 6)*: Implement the snowspeeder's rear gunner tow-cable mechanic for tripping AT-AT walkers on Hoth.

**Match Telemetry & Analytics**
- [ ] **Match Data Logging:** Record structured match telemetry in `core/` + persistence (SQLite now, Postgres later): map, mode, era, faction win/loss, remaining tickets, match duration, player vs bot counts, loadout/weapon usage, K/D, and objective captures to surface faction and map balance trends over time.

### Exit Criteria

Players choose Ground or Space and GCW or Clone Wars at dedicated terminals, play across full Clone Wars and GCW rosters with rain on Kamino and additional BF2 modes, and inspect stored match telemetry showing faction/map win rates.

---

## Stage 12: Remaining Professions (Waves 7B & 7C) & Creature Mounts

### Goal

Complete the remaining 14 professions from [Professions](PROFESSIONS.md) (specialist combat, support, social, economy, animal taming, and modular combat droids) plus open-world creature mounts.

### How it's built

- **Data:** [`game/professions/specialists.ini`](../game/professions/specialists.ini) holds every ability (45), creature and droid companion template, droid module and slice. All 31 professions in `professions.ini` are now `Available`. Slotted professions' kits work only from an Active Specialization Slot; Entertainer, Image Designer, Merchant, Bio-Engineer and Droid Engineer are unslotted (always on). Each ability unlocks at a milestone, which must be learned from a trainer (six new trainers stand in Mos Eisley).
- **Core:** `core/include/swm/core/specialist.h` holds the rules (abilities, timed effects, companions, taming, eggs, droid modules, contracts, bazaar fees and price stats, slicing, inspiration). `core/src/server_specialists.cpp` plays them on the server. A loadout carries its abilities, specialist levels and Companion Slot companion (`core::Loadout`), so a hub's match server gets them through the usual `set_player_loadouts` handoff.
- **Persistence:** datapads, DNA samples and the bounty opt-in use per-character extras. The bazaar's listings and sales history use world extras. No new tables. Slices and dyes rewrite an item's color JSON in place (`CharacterStore::update_item_colors`).
- **Wire (protocol 37):** `SpecialistRequest`, `SpecialistSync` and `SpecialistAck`, plus a `FieldState` at the end of every snapshot (marks, your side's cloaked soldiers, performers, companions, deployables, clouds, incoming strikes, holo flair). Cloaked or smoked enemies aren't sent to you at all, so bots can't see them either.
- **Client:** `game/scenes/specialist_ui.gd` drives the ability bar (keys 3–8, aimed with the crosshair), the datapad (P), the bazaar (E at a bazaar terminal), companion keys (T: context ping, Y: field/store), squad beacon spawning (N) and field effects in the world (`game/rendering/field_effects.gd`).
- **Tests:** `core/tests/specialist_test.cpp` covers the data, the rules, the wire and a live server: a bio grenade, a performance, DNA sampling, taming and fielding a companion, an engineered egg hatched, and a bazaar trade between two players.

### Tasks

**Wave 7B: Specialist Combat, Support, Social & Economy (10 Professions)**
- [x] **Bounty Hunter:** Bounty terminal contracts on live outlaws and opted-in players (PvP is allowed only between a hunter and their mark, both ways). Seeker probes mark and update your marks; you collect on the kill. In matches: recon probes, HVT spotting. Everywhere: bolas, capture nets, wrist rockets, jump packs.
- [x] **Smuggler:** Contraband runs to drop points, offered at general, artisan and bounty terminals. Permanent **sidegrade slicing** of weapons and armor (7 slices, 250 cr each, from Smuggler 50). In matches a slotted Smuggler captures posts as fast as two soldiers. Flashbangs (electro damage plus daze), smoke (hides your side inside it) and EMP disruptors (bonus against droids). *Not done: slicing loot containers and faction terminals.*
- [x] **Squad Leader:** Squad Cohesion aura (passive), Rally, target marking, **squad spawn beacon** (spawn there with N), supply drops and orbital strikes.
- [x] **Doctor:** Pharmaceutical schematics (bacta stims, antidotes, performance stims, advanced medpacs). Medpacks, bio cleanse, stims, a **bacta dispenser**, a defibrillator revive and field triage.
- [x] **Combat Medic:** Healing darts, bacta grenades, toxin darts (bio damage over time) and `DamageType::bio` area denial (bio grenades, bio payloads).
- [x] **Ranger:** Tracking (creatures within 250 m shown on your map) and scout-terminal tracking jobs. Motion sensors, a camouflage **cloak broken by firing** (hides you from enemies, bots and wildlife), trip traps, and **forward camps** that heal and make loadout swaps quick.
- [x] **Entertainer:** Performances that Inspire everyone within 14 m every 5 s, for 15–30 s by level; performers near each other form a stronger band. Flourishes, a holo-emitter, and gigs from entertainer terminals. *Not done: synced band/dance animations. Performers are tagged in the world; no dance clips are exported yet.*
- [x] **Image Designer:** Restyle another player (they accept from their datapad) or yourself. Custom trim (dye) on armor and clothing from level 50, and a holographic flair aura from level 75. *Not done: the design tab adjusts the numeric values in a look; picking hair or species styling still needs the character creator's pickers. Dyes are saved but not drawn yet (the client doesn't tint worn models).*
- [x] **Merchant:** Bazaar terminals (`shared_terminal_bazaar.iff`) take listings: search, buy, take back. Shop slots run 5–25 and fees 6%–2%, both by level. Sellers are paid even when offline. Market Analytics shows price trends from level 50, and faction supply contracts unlock at level 100. *Not done: placed player vendors. Listings carry a shop name instead.*
- [x] **Pilot:** Experience from vehicle kills, field repair, evasive maneuvers, Reactor Tuning (less vehicle damage) and emergency power. *Not done: handling and speed perks for vehicles.*

**Wave 7C: Animal Taming & Creature Mounts (2 Professions)**
- [x] **Bio-Engineer:** Sample DNA from Tatooine's wildlife, living or dead. Engineer **creature eggs** from one or two samples (a second creature's DNA makes it hardier; colors are seeded). Grow organic armor tissues and stim additives (workbench enhancements).
- [x] **Creature Handler:** Tranquilizer darts, a **bonding channel** (tier by level; wounds and tranquilizers help), hatching eggs, a **datapad** of up to 20 companions, and one-key context pings: **attack** (on an enemy), **guard** (on the ground), **follow** (double tap). Frenzy and mending. Only **tactical-sized** creatures may enter matches. One companion is active per player, with a 60 s (world) or 30 s (match) cooldown after it dies.
- [x] **Open-World Creature Mounts:** At Creature Handler 50, a tamed eopie, bantha, dewback or ronto can be trained into a mount item. That item goes in the Mount slot and is called with H, riding as a world vehicle (`game/vehicles/swg_mount_*.ini`). The creature's own walk and run clips play as it moves. *Assumption: riders use the speeder-bike straddle pose, because Galaxies' creature ride clips are masks, not poses.*

**Wave 7C: Combat Robots (2 Professions)**
- [x] **Droid Engineer:** Assemble six chassis (MSE-6, probe, R2, 2-1B, B1, droideka) from Material Vault resources, with core, primary-function and utility modules (8). Droids are sellable items. Droid repair kits are a schematic.
- [x] **Droid Operations:** Register droids into the datapad and field them by certification level. Remote repair and overcharge. Recon droids spot enemies, support droids heal their side, and combat droids fight. Droids take 50% more `DamageType::electro` damage. They share the 1-active-companion limit.

*Deferred / Parked:* **Politician** waits for Stage 14 (Housing & Player Cities); **Jedi / Force** remains parked per [Design](DESIGN.md#open-questions).

**Still to do (found while building Stage 12)**
- [ ] Re-run `swgtool export-people` so it exports the companion looks: it now reads `specialists.ini` too, including the droid chassis. Check that every droid template exists in your Galaxies files.
- [ ] Check in the game that creature mount models face forward, and that riders' seats line up.
- [ ] Draw dyed trim on worn gear, and add dance and music clips for performers.

### Exit Criteria

Every one of the 31 planned non-deferred professions levels 1–100 with live gameplay loops. Players tame or craft tactical companions and take them into matches, ride creature mounts in the open world, and trade through Bazaar terminals.

---

## Stage 13: Server Scalability & Multi-City Expansion

### Goal

Ensure the open-world server scales economically before going online, and (when greenlit) expand beyond Mos Eisley to Tatooine's other towns and additional SWG planets/cities.

### Tasks

**Server Resource Economy & Spatial Simulation**
- [ ] **Player-proximity active mob simulation:** Partition open-world wildlife, lairs, and bandit camps so only entities within an active simulation radius of online players tick full AI, pathing, and combat; put distant zones into low-cost sleep/respawn timers so full 16 × 16 km planets don't waste CPU/memory.
- [ ] **Server resource & cost audit:** Profile and optimize `swm_server` CPU tick budget, terrain/collision memory footprint, SQLite/Postgres write batching, and network snapshot/interest-management bandwidth ahead of public hosting.

**More Cities & Planets** *(On hold until greenlit)*
- [ ] **Additional Tatooine towns:** Export and wire Anchorhead, Bestine, Mos Espa, and Mos Entha onto Tatooine's 16 × 16 km terrain with their own hubs, terminals, quests, and shuttleport travel.
- [ ] **Additional SWG planets & capitals:** Export Naboo (Theed), Corellia (Coronet), and remaining planets/cities as full-scale hubs stitched into their planet terrains, connected via Starport fast travel.

### Exit Criteria

A 16 × 16 km planet populated across its regions simulates with minimal idle server cost when players are clustered, and additional cities/planets can be visited via Starport travel when enabled.

---

## Stage 14+: Endgame World Systems

### Goal

Deliver the full persistent MMO sandbox layered onto working battles and towns: deep resource economics, player housing and cities, and live open-world Galactic Civil War territory battles.

### Tasks

1. **Deep Resources & Harvester Network:**
   - Spawning resource concentrations with visible rated stats, finite lifespans, and planetary shifts.
   - Physical open-world harvesters that must be defended, fueled, and maintained, paired with a centralized **Harvester Network terminal** for management.
2. **Housing, Player Cities & Politician:**
   - Place Galaxies structures (houses, shops, guild halls, factories, civic buildings) directly onto planet terrain with full free-placement interior decoration.
   - Implement the **Politician** profession (city founding, zoning, civic structures, taxation/upkeep).
   - Player cities host their own Ground/Space War Terminals and automatically establish a **no-resource-extraction radius** around placed buildings.
3. **Open-World Galactic Civil War & High-Risk Zones:**
   - Transition command-post battles from instanced BF2 maps into live contested open-world territory over faction bases and player cities built from Galaxies assets.
   - Introduce **full-loot PvP / high-risk danger zones** building on Stage 9's open-world death penalty system.

---

## Completed Foundation (Stages 1–8)

*Historical implementation record of completed Stages 1–8. Where an item has been superseded by Stages 9–14 (e.g., 3 → 5 loadouts, Standard queue removal, bot difficulty lock), the updated rule in the sections above governs.*

### Stage 1: A Battlefront match from a Galaxies city (Done)

**Goal:** Walk around Mos Eisley, queue at a recruiter, load into a 32v32 conquest match on a Battlefront II map (bots filling empty slots), play to the end, and return to the city with the results. Met on 2026-11-22.

- [x] **Hub:** Mos Eisley region loaded as a walkable hub (1,623 objects, 900 furniture pieces, 140 sliding doors, indoor lighting, Galaxies NPCs, crowd of 30, creatures, particle effects); War Terminal queue UI (`war_terminal_screen.gd`, `hub_hud.gd`); Queue → match handoff (`core::Hub`); battles on Battlefront II maps; return from match to hub with results.
- [x] **Match server:** Match lifecycle (`warmup → live → end → results`), `core::load_battlefield` INI format, `game/battlefields/mos_eisley_outskirts.ini` test field, `core::Conquest`, loadout selection & spawning.
- [x] **Battlefront II maps:** `bf2tool export-map` (terrain, textures, props, regions, paths, sky, fog, lights; deferred: water, foliage patches, 4th texture layer, bump maps), `core::World` collision, Conquest data from sublevels, `core::NavGrid`, Godot rendering (`bf2_map.gd`), and 18 ground maps exported (`dag1`, `end1`, `fel1`, `kas2`, `yav1`, `hot1`, `geo1`, `nab2`, `pol1`, `tan1`, `dea1`, `cor1`, `tat3`, `mus1`, `kam1`, `myg1`, `uta1`, `tat2`).
- [x] **Combat:** Third-person controls, first-person toggle (`V`), Battlefront soldier animations, projectile weapons with clips & overheating, grenades & explosives, health/damage/respawn, lag-compensated hit detection, data-driven loadouts (`core::stats_for`).
- [x] **Bots:** Line-of-sight aiming, target leading, sidestepping, objective selection (`core::Bot`).
- [x] **Presentation:** Battlefront II HUD (`bf2_hud.gd`), menu/spawn/pause/score screens (`game/ui/`).

### Stage 2: Match polish (Done)

- [x] **First person:** Steady view-model arms & chest smoothing (`soldier.gd`); third-person grip/hand poses completed in Stage 4.
- [x] **Over soldiers' heads:** Faction holograms (`team_tags.gd`) with occlusion fading/hiding.
- [x] **Health and ammo droids & pickups:** `core::PowerupStation`, destructible droids, bot resupply routing, `core::PickupKind` death drops (`pickups.gd`).
- [x] **Projectiles and effects:** Axial laser bolts, `bf2tool export-effects` (56 effects, 31 textures, muzzle flashes, scorch marks, sparks), grenade/rocket ordnance models & trails.
- [x] **Controls and rules:** Crouch, roll, zoom/scopes, headshots, ordnance damage falloff, explosion line-of-sight & push, grenade bouncing & fuses.
- [x] **Bots:** Cover seeking and low-health retreat (`core::Bot`); difficulty presets implemented (locked to Hard in Stage 10).
- [x] **Maps:** Animated doors (`core::Door`), Lua 5.0 mission-script gates/bridges/traps, destructible objects (`core::Destructible`) with world animations & smoke/chunks, deep-water death regions, third-team natives (`core::Natives`: Jawas, Gamorrean guards, Ewoks, Geonosians, Wookiees).
- [x] **Audio:** `bf2tool export-audio` & `game/audio/bf2_audio.gd` (496 sounds across SFX, VO, music, plus volume buses).

### Stage 3: Front end and characters (Done)

- [x] **Main menu:** `main_menu.gd`, `load_character_screen.gd`, `settings_screen.gd`, scene split out of `net_demo.gd`.
- [x] **Species and rigging:** All 10 playable SWG species (male & female) with Battlefront clip retargeting and species-specific collision/eye heights.
- [x] **Character creator:** Species/gender picker, body & face blend sliders, palettes, hair styles, markings, saved with character snapshots.
- [x] **Species traits:** Armor/footwear restrictions for Wookiees, Ithorians, Trandoshans paired with innate stat traits in `core::stats_for`.

### Stage 4: Items, weapons and armor (Done)

- [x] **Data:** Separated item definitions and persistent instances in SQLite; Battlefront weapon archetypes; Galaxies weapons, armor sets, Deflector Shield (PSG) slot, and appearance clothing imported.
- [x] **Holding weapons:** Animation sets (`rifle`, `pistol`, `bazooka`), left-hand fore-grip IK, held weapon models (`Soldier.set_weapon`), HUD weapon panel profiles.
- [x] **Inventory and equipment:** `inventory_screen.gd`, `item_tooltip.gd`, live 3D `SubViewport` paperdoll.
- [x] **Loadouts:** 3 loadouts (expanding to 5 in Stage 10), timed world loadout switching (`3.0 s`), battle spawn screen loadout selection, Standard vs Gear queue (Standard removed in Stage 10).
- [x] **Getting gear:** Faction quartermasters (becoming cosmetic-only in Stage 10), Mos Eisley shooting range (`[target]`), battle rewards (`core::compute_battle_reward`).

### Stage 5: Tatooine around Mos Eisley (Done)

- [x] **Planet terrain:** Full 16 × 16 km Tatooine heightfield (`4,097 × 4,097`), streaming regions, horizon haze, `core::NavGrid` on dunes/canyons.
- [x] **Creatures and bandits:** Wild creatures in lairs and bandit/raider camps (`tatooine_spawns.ini`), respawning/leashing, cloning facility respawn.
- [x] **Loot, credits and vendors:** `tatooine_loot.ini`, `loot_screen.gd`, junk dealer & gear vendor (`vendor_screen.gd`), persistent credits.
- [x] **Quests:** `core::QuestBook`, `tatooine_quests.ini`, `quest_screen.gd`, HUD tracker, hub radar & full world map (`world_map_screen.gd`), 10 `[mission_terminal]`s with generated destroy missions.

### Stage 6: Vehicles (Done, except tow cable & creature mounts)

- [x] **In matches:** Hover vehicles & walkers (`core::simulate_vehicle`), gunner seats & turning turrets (`MoveState::roll_yaw`), walker leg animation & stomp sounds, damage effects & `EngineSound`, `[vehicle_spawn]`s, manned `[turret]`s, bot vehicle driving & gunning, flyers (Hoth snowspeeders). *(Snowspeeder rear-gunner tow cable moved to Stage 11.)*
- [x] **In the world:** Galaxies personal vehicles (`swgtool export-vehicles`), Garage vendor, summon/put away in world. *(Creature mounts moved to Stage 12; speeder equipment slot + keybind moved to Stage 9.)*

### Stage 7: Professions (Framework + Wave 7A Done; Waves 7B & 7C in Stage 12)

See [Professions](PROFESSIONS.md) for the complete 31-profession roster and SWG crosswalk.

- [x] **Core profession framework:** Data-driven 1–100 definitions & 1/25/50/75/100 milestones (`core/profession.h`, `game/professions/professions.ini`), SQLite persistence, Active Specialization Slots & Companion Slot, `profession_screen.gd` (`K` keybind), skill trainers & combat level calculation.
- [x] **Wave 7A (17 live professions):** 4 armor professions + universal PSG mechanics, 5 weapon professions (including consolidated Melee), 3 gathering professions, 5 core crafting professions.
- [x] **Streamlined gathering & crafting UX:** Unified Multi-Scanner with 3D holographic contours, dedicated Material Vault, Single-Screen Unified Workbench (`workbench_screen.gd`) with Auto-Fill, enhancement slots, Recipe Presets, batch crafting, and Work Orders/Salvaging.
- *(Waves 7B and 7C moved to Stage 12.)*

### Stage 8: Space combat (Done)

- [x] All 6 base space maps imported (`spa1` Yavin, `spa3` Kashyyyk, `spa6` Mygeeto, `spa7` Felucia, `spa8` Hoth, `spa9` Tatooine, plus `spa_sky.lvl` sky domes).
- [x] 16 starfighters/bombers/transports, 27 defense/roof turrets, mobile transport spawns, shield-linked & chain-linked capital ship systems, automated defense turrets, turret control chairs, and ScoreLimit scoring.
- [x] Hangar landing (`LandingRegion`) and on-foot boarding/sabotage combat inside capital ships.
