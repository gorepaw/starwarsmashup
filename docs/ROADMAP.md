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
| 6 | Vehicles | Done (tow cable in Stage 11, creature mounts in Stage 12) | Battlefront's in matches, Galaxies' in the world |
| 7 | Professions (Wave 7A + framework) | Done (Waves 7B & 7C in Stage 12) | Core 1–100 framework & 17 foundation professions live |
| 8 | Space combat | Done (base maps/ships) | Battlefront II's space battles |
| **9** | **Bug Fixes, UI/UX Polish & Authentic Mos Eisley** | **Done** | Clean visuals, authentic SWG city layout, terminal audit, and tight open-world QoL |
| **10** | **4 Factions, 5 Loadouts, Cosmetics, Ammo & Bot Roster** | **Done** | 4-faction system, cosmetic FP & WoW-style reputation, unified ammo, and clothed stock bots |
| **11** | **Ground/Space Terminals, Clone Wars, Modes, Weather & Telemetry** | **Done (follow-ups listed)** | Dual battle terminals, full Clone Wars roster, extra BF2 modes, Kamino rain, and match analytics |
| **12** | **Remaining Professions (Waves 7B & 7C) & Creature Mounts** | **Done (follow-ups listed)** | All 14 remaining specialist, bio/creature, and droid professions + creature mounts |
| **13** | **Server Scalability & Multi-City Expansion** | **Done for Tatooine (other planets next)** | Proximity-based open-world simulation, interest management & audit; all of Tatooine's towns with shuttle travel |
| **14+** | **Endgame World Systems** | **Planned** | Deep resources & harvesters, housing & player cities, open-world GCW, and full-loot zones |

The battlefield evolves alongside: **instanced matches on Battlefront II's own maps → much later, command-post battles built from Galaxies assets in the open world.**

---

## Updated Design & System Decisions

These decisions supersede or extend the earlier Stage 1–8 notes below:

1. **Single Gear-Matters Queue & Locked Hard Bots:** Remove the normalized/Standard queue (`QueueMode::standard`) and adjustable bot difficulty. Matches use **Gear-Matters** rules only, and match bots are locked to **Hard**.
2. **Five Loadouts per Character:** Expand per-character loadouts from 3 to **5** (`core::max_character_loadouts = 5`). Each loadout stores both functional gear choices and a **Cosmetic Override** configuration.
3. **Four Factions & Currency Split:** Expand from 2 sides to **4 factions** (**Rebellion, Empire, Republic, Separatists**). **Faction Points (FP)** become a **cosmetic-only currency** spent at faction quartermasters (cosmetic gear and full-body overrides), while **Credits** are the power/gear currency. *Changed since (see [Armor](ARMOR.md)): there are no cosmetic-only items; quartermasters sell their faction's equipment for FP, and the cosmetic override takes any equipment.*
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
  - *Revisited (after Stage 13):* hub people's worn things were grown along their normals off the skin (`grow`), which split hard-edged armor at its seams (a stormtrooper's helmet came apart). Now `swgtool` leaves out what worn things cover, by Galaxies' occlusion zones (`OZN`/`OZC`/`OITL`/`ZTO`/`FOZC`, see `tools/README.md`), and hub people aren't grown. Nor are players' worn items: the export also writes each model's zones (`<name>.occlusion.json`: species, hair styles, armor, clothing), and the game (`rendering/swg_occlusion.gd`) leaves out the triangles what's put on covers, again whenever it changes (`tests/occlusion_test.gd`).
- [x] **Fix female Twi'lek lekku:** Male lekku replacement was fixed (`PlayerAppearance.hide_replaced_head_parts`), but female Twi'lek heads still render overlapping/double lekku. Fix female head-part swapping in both character creator and in-world rendering.
  - *Revisited (after Stage 13):* hub people (Mos Eisley's Teska Vo) still had both: their looks are exported whole, and a female Twi'lek's hair names its lekku as her head does. The occlusion zones above fix it at export: her hair covers her head's lekku and skull cap.
- [x] **Stagger NPC idle animations:** Offset/randomize idle animation start phases on hub NPCs and `[crowd]` walkers so nearby characters do not animate in lockstep.
- [x] **Fix turret character model disappearance:** Prevent the player's character model from vanishing when occupying a turret in BF2 matches.

**World Collision, Loading & Weather (Pre-Stage 11 Quick Fix)**
- [x] **Fix Mos Eisley Starport boulder clipping:** Remove or adjust the giant boulder intersecting the Mos Eisley Starport structure/terrain.
  - *Fixed (after Stage 13):* no boulder: the starport's outside cell (`r0`, its sandstone shell) cut through its lobby's doorways, drawn whole. Galaxies draws a building's outside from within only through the portals that lead out; now it isn't drawn while the camera is in one of the building's rooms (`SwgBuilding.update_exteriors`).
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
  - Rework Faction Quartermasters (`quartermaster_screen.gd`) so Faction Points (FP) buy **cosmetic-only rewards** (armor appearances, uniforms, banners/trims, and full-body **CIS battle droid overrides**). *Changed since: they're equipment now, except the full-body looks, which are bodysuits (the one look-only item), see [Armor](ARMOR.md).*
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
- [x] **Ground Battle Terminal & Space Terminal (Battles + Interplanetary Travel):**
  - Split `[recruiter]` into distinct **Ground Battle Terminal** and **Space Terminal** (`shared_terminal_space.iff` inside Mos Eisley's Starport) types.
  - Both battle queues let the player choose **Galactic Civil War** or **Clone Wars** before queueing; the Space Terminal also hosts interplanetary travel once Stage 13 planets open.
  - *Done:* `[recruiter] Type = ground|space|both` (`net::Recruiter::kind`, sent in `Welcome`). The hub keys its queues by era (the side's pair), ground or space (the terminal the player stands at) and game mode (`QueueRequest::mode`, or any). The terminal screen (`war_terminal_screen.gd`) has era tabs (GCW / Clone Wars), a mode row on the ground (Conquest, Capture the Flag, 1-Flag CTF, Hunt, XL, Any Mode) and Space Assault in space; the Space Terminal keeps its Interplanetary Travel tab (stubbed until Stage 13's planets). A queue no battlefield can serve is refused with a notice. Hub bots now walk to the Ground Battle Terminal (the Space Terminals stand inside the Starport) and queue for a random era and any mode.
- [x] **Complete Clone Wars Ground & Space Map Roster:**
  - Audit all BF2 ground maps (`cor1`, `fel1`, `geo1`, `kam1`, `kas2`, `mus1`, `myg1`, `nab2`, `pol1`, `tat2`, `uta1`, `yav1`, `dea1`, etc.) and space maps (`spa1`, `spa3`, `spa6`, `spa7`, `spa8`, `spa9`, plus check `_lvl_pc` for any remaining BF2 Clone Wars space maps such as Coruscant/Utapau if shipped).
  - Export/wire Clone Wars (`Republic` vs `Separatists`) conquest and space assault battlefield configs alongside GCW configs so both eras have complete map pools on both terminals.
  - *Audit (decoded from `mission.lvl`'s compiled Lua):* Clone Wars conquest ships for 16 ground maps (all but Endor and Hoth), GCW conquest for 17 (all but Geonosis). Space assault: Clone Wars `spa3c` (Kashyyyk), `spa6c` (Mygeeto), `spa7c` (Felucia); GCW `spa1g` (Yavin), `spa8g` (Hoth), `spa9g` (Tatooine). `spa2` (Coruscant), `spa4` (Mustafar) and `spa3g`/`spa5` exist only as campaign missions (`_c`), so the space roster was already complete.
  - *Done:* 15 new `game/battlefields/<map>_cw.ini` (Republic vs Separatists, Phase II clone kits `rep_inf_ep3_*`, the CIS kits) reuse each map's conquest export: the CW scripts set the same posts, regions, barriers and doors (checked script by script), with teams in the CW script's order (`SetupTeams`: the Separatists are team 1 on Coruscant, Kamino, Naboo, Polis Massa, Utapau and Yavin). Kashyyyk's Wookiees befriend the Republic, Mos Eisley's Jawas both sides, Jabba's Gamorreans neither, as the CW scripts do. Mos Eisley (`tat2`, GCW) joins the hub's list. Roster: GCW 17 ground + 3 space, Clone Wars 16 ground + 3 space. Audit fix: Felucia's GCW file had its sides swapped (its script makes the Empire team 1).

**Game Modes, Weather & Remaining Vehicle Polish**
- [x] **Additional BF2 Game Modes:** Design and implement BF2 modes beyond Conquest and Space Assault (e.g., Capture the Flag, Hunt, Assault / Hero Assault, XL) in `core/` and mission-script export (`bf2tool`).
  - *Done:* `core::GameMode` (conquest, space assault, **Capture the Flag**, **1-Flag CTF**, **Hunt**, **XL**). Each mode fights on Battlefront's own layer for it: `bf2tool export-map --mode <mode> --layer <the mission's sublevel>` exports its posts (spawn-only `com_bldg_controlzone_ctf` posts in flag and Hunt layers), vehicle spawns and death regions, and `--flag` / `--flag-capture` (read off the scripts' `ObjectiveCTF` / `ObjectiveOneFlagCTF` tables) its flags and capture regions; 27 mode layers exported (9 CTF, 9 1-flag, 6 Hunt, 3 XL), commands in `tools/README.md`. A battlefield offers them with `Mode = <mode> <map file>`; battle ids carry the mode (`kamino_cw@1flag`) so clients load the same. Rules (`core::Conquest`): flags taken by walking onto them (on foot), carried, dropped where the carrier falls, returned by touching your own or after 30 s, captured in your side's region; 5 captures (`CaptureLimit`) or the higher score at the 20-minute `TimeLimit`; no reinforcement cost in flag modes; Hunt is a race to `HuntLimit` kills; XL is Battlefront's `ObjectiveTDM` on the XL layer with 32 a side (reinforcements only, no bleed). Flags drawn in the world (`game/rendering/battle_flags.gd`), a mode banner (score, timer, where each flag is), flag events in the kill feed. Bots go for the flags and carry them home (most), or hold back and return their own.
  - *Follow-ups:* Hero Assault waits for heroes (Jedi / the Force are parked). Hunt pits the queued factions on Battlefront's hunt layers; its special hunter rosters (Wampas, Tuskens, Ewoks...) come with creatures as players. Space 1-flag CTF (`spa*_1flag`) isn't offered.
- [x] **Kamino Rain & Planetary Weather:**
  - Implement rain particles, wet/storm ambience, and lighting for **Kamino** (`kam1`).
  - Audit and add weather effects for other planets (sandstorms/dust on Tatooine & Geonosis, blowing snow on Hoth, ash/embers on Mustafar, mist/rain on Dagobah & Felucia).
  - *Done:* `game/rendering/weather.gd`: Kamino's storm (rain streaks, overcast fog and light, lightning with thunder), blowing sand on Tatooine (battlefields and, lighter, the open world) and Geonosis (with an orange haze), snow on Hoth and Mygeeto, ash and embers on Mustafar, mist on Felucia and mist with drizzle on Dagobah. Particles follow the camera; the ambience (rain hiss, wind gusts, thunder) is synthesized from noise (BF2's export has no weather sounds). Looks only.
- [x] **Snowspeeder Rear Gunner & Tow Cable** *(carried over from Stage 6)*: Implement the snowspeeder's rear gunner tow-cable mechanic for tripping AT-AT walkers on Hoth.
  - *Done:* `export-vehicle` keeps the snowspeeder's rear seat (Battlefront's `towcableweapon`, `TowCable = <LockOnRange 70> <LockTime 0.4>`). Its gunner's shot harpoons the nearest enemy AT-AT within 70 m while the snowspeeder flies; the pilot circles it, and three full turns trip it (destroyed, credited to the gunner). The cable snaps beyond 110 m, on landing, or after 40 s (`core/src/server_tow.cpp`, ASSUMPTION: Battlefront I's rule; II keeps the weapon only). Clients draw the cable (`VehicleState::towing`). Lock time isn't modeled (the lock is immediate).

**Match Telemetry & Analytics**
- [x] **Match Data Logging:** Record structured match telemetry in `core/` + persistence (SQLite now, Postgres later): map, mode, era, faction win/loss, remaining tickets, match duration, player vs bot counts, loadout/weapon usage, K/D, and objective captures to surface faction and map balance trends over time.
  - *Done:* `core::MatchTelemetry` (battle id, map, mode, era, ground/space, sides, winner, tickets and scores left, live duration, posts and flags captured, and per participant: side, player or bot, kills, deaths, captures, primary weapon, kills by weapon). Hubs record one per battle (`CharacterStore::record_match`: `match_telemetry` and `match_participants` tables in SQLite). `swm_server --telemetry <db> [--last <n>]` prints win rates by faction, mode and map (with tickets left and average length), player and bot K/D, and the deadliest weapons.

### Exit Criteria

Players choose Ground or Space and GCW or Clone Wars at dedicated terminals, play across full Clone Wars and GCW rosters with rain on Kamino and additional BF2 modes, and inspect stored match telemetry showing faction/map win rates.

---

## Stage 12: Remaining Professions (Waves 7B & 7C) & Creature Mounts

### Goal

Complete the remaining 14 professions from [Professions](PROFESSIONS.md) (specialist combat, support, social, economy, animal taming, and modular combat droids) plus open-world creature mounts.

### How it's built

- **Data:** [`game/professions/specialists.ini`](../game/professions/specialists.ini) holds every ability (40), creature and droid companion template, droid module and slice. All 29 professions in `professions.ini` are `Available`. Each ability unlocks at a milestone, which must be learned from a trainer, and works in every loadout ([Perks](PERKS.md): the slots are gone).
- **Core:** `core/include/swm/core/specialist.h` holds the rules (abilities, timed effects, companions, taming, eggs, droid modules, contracts, bazaar fees and price stats, slicing, inspiration). `core/src/server_specialists.cpp` plays them on the server. A loadout carries its abilities, specialist levels and Companion Slot companion (`core::Loadout`), so a hub's match server gets them through the usual `set_player_loadouts` handoff.
- **Persistence:** datapads, DNA samples and the bounty opt-in use per-character extras. The bazaar's listings and sales history use world extras. No new tables. Slices and dyes rewrite an item's color JSON in place (`CharacterStore::update_item_colors`).
- **Wire (protocol 37):** `SpecialistRequest`, `SpecialistSync` and `SpecialistAck`, plus a `FieldState` at the end of every snapshot (marks, your side's cloaked soldiers, performers, companions, deployables, clouds, incoming strikes, holo flair). Cloaked or smoked enemies aren't sent to you at all, so bots can't see them either.
- **Client:** `game/scenes/specialist_ui.gd` drives the ability bar (keys 3–8, aimed with the crosshair), the datapad (P), the bazaar (E at a bazaar terminal), companion keys (T: context ping, Y: field/store), squad beacon spawning (N) and field effects in the world (`game/rendering/field_effects.gd`).
- **Tests:** `core/tests/specialist_test.cpp` covers the data, the rules, the wire and a live server: a toxin dart, a performance, DNA sampling, taming and fielding a companion, an engineered egg hatched, and a bazaar trade between two players.

### Tasks

**Wave 7B: Specialist Combat, Support, Social & Economy (8 Professions)**
- [x] **Bounty Hunter:** Bounty terminal contracts on live outlaws and opted-in players (PvP is allowed only between a hunter and their mark, both ways). Seeker probes mark and update your marks; you collect on the kill. In matches: recon probes, HVT spotting. Everywhere: bolas, capture nets, toxin darts (bio damage over time), wrist rockets, jump packs.
- [x] **Smuggler:** Contraband runs to drop points, offered at general, artisan and bounty terminals. Permanent **sidegrade slicing** of weapons and armor (7 slices, 250 cr each, from Smuggler 50). In matches a Smuggler captures posts as fast as two soldiers. Flashbangs (electro damage plus daze), smoke (hides your side inside it) and EMP disruptors (bonus against droids). *Not done: slicing loot containers and faction terminals.*
- [x] **Squad Leader:** Squad Cohesion aura (passive), Rally, target marking, **squad spawn beacon** (spawn there with N), supply drops and orbital strikes.
- [x] **Doctor:** Pharmaceutical schematics: **medpacks** as tactical gear (Bacta Stimpack and Trauma Medpac at once, Bacta Infusion Pack over time), antidotes, performance stims and the bio grenade (Heavy Weapons ordnance). Healing darts, bacta grenades, bio cleanse, a **bacta dispenser**, a defibrillator revive and field triage. **Crafting orders** for Doctor items at artisan terminals. Food heals only out of combat (10 s).
- [x] ~~**Combat Medic**~~: retired 2026-11-27 to keep the support professions apart ([Professions](PROFESSIONS.md#support-scopes)). Healing darts and bacta grenades went to Doctor, the toxin dart to Bounty Hunter; bio grenades and payloads are to return as Doctor-crafted Heavy Weapons ordnance. Its trainers teach Doctor.
- [x] **Ranger:** Tracking (creatures within 250 m shown on your map) and scout-terminal tracking jobs. Motion sensors, a camouflage **cloak broken by firing** (hides you from enemies, bots and wildlife), trip traps, and **forward camps** that heal and make loadout swaps quick.
- [x] **Entertainer:** Reworked 2026-11-27 into a non-combat profession: an audience builds **Inspiration**, one long buff chosen from the performer's menu (5 combat, 2 progression: `xp_gain`, `faction_gain`) in the datapad's Inspiration tab, 5 minutes watched for an hour (up to 1.5 h from a Virtuoso). Saved with the character (`inspiration` extra), counting down only online; carried into queued matches (`Loadout::inspiration`, back in `PlayerResult`), surviving death there but not in the open world. Bands, Flourish (builds twice as fast), holo-emitters (half speed, anywhere), three-minute gigs to an audience. *Not done: synced band/dance animations. Performers are tagged in the world; no dance clips are exported yet.*
- [x] ~~**Image Designer**~~: removed after Stage 13; not worth a profession. Restyling becomes an NPC service one day.
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
- [x] Re-run `swgtool export-people` so it exports the companion looks: it now reads `specialists.ini` too, including the droid chassis. All 16 export for Tatooine (Mos Eisley's hub) and Naboo (Theed's). The MSE-6's template is `shared_mouse_droid.iff`.
- [x] Check in the game that creature mount models face forward, and that riders' seats line up (`game/tests/stage12_playtest.gd`: companions fielded and drawn, the bantha ridden, the datapad opened). Fixed along the way: big companions spawned inside their owner and their tags floated at a fixed height; the datapad and bazaar read the inventory as a dictionary (it's an array) and opened empty; riding any vehicle deleted the soldier's ammo stack (the ammo sync counted the vehicle's weapons).
- [x] Choose what goes on the ability bar: each ability in the datapad's Kit tab has a button per bar key (3–8; the one it's on takes it off), and "Reset bar" restores the default. Keys left alone fill themselves with the abilities not placed or taken off, in the kit's order, and a layout from another loadout's kit just fills itself. Saved in the player's settings file (`bar_layout`, `bar_off`), per player, not per loadout (`SpecialistUI.resolve_bar` / `edit_bar`, checked in `stage12_test.gd`).
- [ ] Draw dyed trim on worn gear, and add dance and music clips for performers.

### Exit Criteria

Every one of the 31 planned non-deferred professions levels 1–100 with live gameplay loops. Players tame or craft tactical companions and take them into matches, ride creature mounts in the open world, and trade through Bazaar terminals.

---

## Stage 13: Server Scalability & Multi-City Expansion

### Goal

Ensure the open-world server scales economically before going online, then expand beyond Mos Eisley: first the rest of Tatooine, then other SWG planets and cities.

### Tasks

**Server Resource Economy & Spatial Simulation**
- [x] **Player-proximity active mob simulation:** Outside a match, NPCs with no player within `ServerConfig::npc_sim_radius` (350 m) aren't simulated (players filed in a `core::PointGrid` each tick). Wildlife lairs and bandit camps sleep (`core::PopulationActivity`): a group wakes when a player comes within 300 m of its edge, and sleeps after 15 s with nobody within 400 m and none of its members fighting. Asleep, its living members are reset home, healed and taken out of the world, and its dead wait to respawn. With nobody online, all of Tatooine (151 people, 233 creatures, 68 bandits) costs 0.07 ms a tick.
- [x] **Server resource & cost audit:** See [Server audit](SERVER_AUDIT.md). CPU: the hub's AI ran about 1,000×/s (every loop pass) and now runs once per tick; NPC lookup was a linear scan (quadratic per update) and is now an index; idle CPU fell from 9.7% of a core to 0.6%. Bandwidth: rarely changing strings (appearance, worn gear, names) ride along only when a client may lack them (`EntityState::static_key`); others' states are compact (`full_state`); clients are sent nearby NPCs, players and effects with a hysteresis margin; −50% bytes with 16 clients, largest snapshot 15 → 9.4 KB. Memory: a packed collision grid (−97 MB) and terrain holes as a patch per town; 314 → 217 MB. `swm_server` logs a perf line every 10 s (`GameServer::take_perf`). SQLite write batching: already WAL + one batch per save; a write-behind queue is recommended before Postgres.

**More Cities & Planets**
- [x] **Additional Tatooine towns:** Anchorhead, Bestine, Mos Espa, Mos Entha, Mos Taike and Wayfar, with centers and extents from the world snapshot's building clusters, exported with Mos Eisley onto the planet's 16 × 16 km bake (`swgtool export-city --area`, a terrain-holes patch per town; 12,889 objects). Each is a town file (`game/hubs/<town>.ini`, `Town =` in the hub file) with its own crowd, cloning facility (the dead clone at the nearest), and terminals mined from the export's verified positions: mission terminals by type, banks, bazaars, NewsNet, public crafting stations, clinics, gambling. Shuttle travel: `[service] Type = travel` at every shuttleport and one terminal in each starport, `ServiceRequest::travel` (100 Cr), and a travel screen (`travel_screen.gd`); a loading screen covers the jump, and the map title follows the nearest town. 43 lairs and 13 camps around the new towns (tier rings, themed gangs). A quest chain in each main town (Dela Tarrow, Corvin Hale, Sabbo Keel, Tamsin Orr) and Ilsa Marn's *Shuttle Out*, which introduces travel. Clients draw buildings to 3 km, props to 1.2 km, furniture to 150 m. Tests: `wildlife_test` (dormancy, interest management), `services_test` (travel), `hub_test` (town files), `game/tests/stage13_towns_test.gd` (spawn in Anchorhead, fly to Mos Espa).
  - *Follow-ups:* skill trainers and named vendors in the new towns (SWG spawned them server-side, so their positions aren't in the client files; place them only from verified references); Space Terminal battle queues in the other starports (Stage 11's `[recruiter]`, positions listed in `tatooine.objects.json`); points of interest between towns (Jabba's Palace, Fort Tusken, the Krayt graveyard); Mos Taike and Wayfar quests.
- [x] **Additional SWG planets & capitals:** Export Naboo (Theed), Corellia (Coronet), and remaining planets/cities as full-scale hubs stitched into their planet terrains, connected via Starport fast travel (the travel service extends to other planets' starports; each planet is its own world server).
  - *The 10 ground planets on the Total Experience discs* (`snapshot/*.ws` + `terrain/*.trn`): Tatooine, Naboo, Corellia, Talus, Rori, Dantooine, Lok, Yavin 4, Endor, Dathomir. Kashyyyk (Rage of the Wookiees) has terrain but no world snapshot on these discs; Mustafar isn't on them. Each planet's town names, centers and extents are in `datatables/clientregion/<planet>.iff`, and the starship fares between planets in `datatables/travel/travel.iff`.
  - [x] *Interplanetary travel:* `core::Galaxy` (`game/hubs/galaxy.ini`, `swm_server --galaxy`, `SwmServer.set_galaxy`) runs each planet's hub on a world server of its own in one process. A starport (`[service] Type = starport`) sells passage to the other planets' starports (fares from `travel.iff`: Tatooine ↔ Naboo 500 Cr). The destination expects the player at its starport, and `net::Transfer::planet` makes the client load that planet's world. A character's location is its planet: logging in elsewhere sends them back to it. Mission terminals now offer missions around themselves, named from the nearest town. Tests: `galaxy_test`, `game/tests/stage13_planets_test.gd` (Mos Eisley Starport to Theed).
  - [x] *Naboo:* full-planet bake; Theed (the hub), Keren, Moenia, Deeja Peak and Kaadara, exported from `clientregion`'s areas (4,217 objects); each town's terminals, cloning facilities, shuttleports and starports from the export; Naboo crowds; 31 lairs and 10 camps of Naboo's creatures (nuna, ikopi, shaupaut, mott, kaadu, gualama, falumpaset, veermok, bolle bol, fambaa) and outlaws (swoopers, smugglers, Naboo dissidents, Maulers), their loot (`naboo_loot.ini`, 12 junk items); Theed's hub borrows Tatooine's battles (`BattlefieldsFrom`).
  - [x] *Naboo finished:*
    - *Water:* the terrain's water tables are read from the `.trn` (a boundary's local water table: BPOL 0005 and BREC 0003 carry enabled, height, shader size and shader after their feathering; and the header's global table). Naboo has 26: its plains, Theed's river and Lake Paonga at 3 m (sea level), the ocean at -210 m under Kaadara (-192 m), Keren's ponds, Deeja Peak's mountain lakes, Lake Retreat's at -160 m and the Emperor's Retreat's pond. Tatooine's file has four small tables ("Water!?!", at 0 and 1 m); three lie under 80-150 m of ground, and one fills a sunken basin east of the Dune Sea (6250, -385) with a shallow pond, a meter deep: waded, not swum. `swgtool export-water` (and `bake-terrain`) writes them to `<bake>.water` with their textures. `core::Heightfield::water_at` gives the water's surface, the highest table holding the point if the terrain is lower. Deep water is swum: `World::swim_floor_at` holds a swimmer's feet `World::swim_depth` (1.3 m) under the surface, `ground_at` floats anyone under it up to it, and `simulate_move` swims at half speed (`MoveParams::swim_factor`), without sprinting, rolling, crouching or jumping. Hover vehicles ride over the surface. Wildlife's wander points keep out of deep water, as crowds' already do. The client draws each table as a flat surface with Battlefront's water shader (`game/rendering/swg_water.gd`, its ripples borrowed from Battlefront's Naboo), and swimmers with Galaxies' own `all_b_loc_swim` and `all_b_idl_tread_water` (export-species adds them to every species), their weapons slung. Naboo's sky has one sun and a bluer haze (`PLANET_LOOKS` in `game_session.gd`).
    - *Lake Retreat:* a sixth town (`lake_retreat.ini`): its shuttleport, bank and bazaar terminal and houses by its lake are in the world snapshot (no client region names it), 352 more objects in the export.
    - *Trainers, vendors and services:* every town's guild halls, hospital, bank and garage (the buildings the snapshot places), with their people in the buildings' own rooms (centers and floor heights from the cells' floor meshes): the combat guild's five meeting rooms hold the combat trainers, the commerce guild's the crafting and trade trainers, the gear vendor and the junk dealer, the university's the medic, survey and ranger trainers, the theater's entrance the entertainer trainer: 51 trainers in Galaxies' `dressed_*_trainer_*` looks. Banks get a terminal (Galaxies' servers spawned theirs; `export-city --hub` now places a `[service]` or `[vendor]` with a `Template`), hospitals a bacta tank, garages an attendant. `[npc]` takes a `Height` (indoors), and placement looks for floors from the terrain's height (on Deeja Peak at 360 m, or by Kaadara's shore at -192 m).
    - *Quests:* `naboo_quests.ini`, 10 quests from 7 givers (Theed's steward, its police, the Royal Security Forces' captain and a cook; Lake Retreat's keeper, Keren's harbormaster and Moenia's constable): the city's services, the plains' nunas and ikopi, kaadu hides, the swoop gang, the dissidents' cipher, the Maulers' relic, a letter to Lake Retreat (its shuttle), its veermoks, Keren's smugglers and Moenia's swoopers. Two quest items. Royal Security guards the palace door and patrols the plaza.
    - *Fixes:* a building's terrain hole with no floor under it (beside a guild hall, under its raised foyer) no longer drops anyone into the void (`Heightfield::hole_at`); `export-people` reads a hub's `Town =` files for their people's looks.
    - Tests: `movement_test` (water, holes), `game/tests/stage13_water_test.gd` (swimming in Lake Paonga), `game/tests/stage13_naboo_test.gd` (Naboo's services, givers and a trainer on his room's floor).
  - [x] *The other eight planets*, each brought in by Naboo's recipe (an agent a planet, in parallel: full-planet bake with its water tables, its towns from `clientregion`'s areas and the snapshot, terminals and services from the export, trainers and vendors in the buildings' own rooms, wildlife, quests, a sky of its own, a test). Where `clientregion` only gives a placeholder string ("an outpost", "a pirate outpost", "a restricted area"), the town's name is ours, read from what stands there and from the planet's `*_region_names.stf`; towns the regions don't name (a shuttleport cluster, a pirate outpost, a prison) are ours too, their extents around the snapshot's buildings. Outposts without guild halls put trainers in their sheds and houses' rooms, or outdoors as Mos Eisley does.
    - *Corellia:* Coronet (the hub), Tyrena, Kor Vella, Doaba Guerfel, Bela Vistal and Vreni Island (4,672 objects, 9 shuttleports, 4 starports); 58 guild-hall trainers, banks, clinics, garages; Corsec, Selonian and Imperial people; 35 lairs and 12 camps of gubbur, vrelt, durni, bageraset, krahbu, dalyrake, slice hounds, gronda, sand panthers and sharnaff, and the Flail, Meatlumps, Hidden Daggers and Corsec deserters (gang names ours, Galaxies' `criminal_*`/`corsec_*` looks); 11 quests from 7 givers. Its shuttleports stand on solid 30 m platforms: their travel points are the open ground at their feet.
    - *Talus:* Dearic (the hub), Nashal and the Imperial Outpost (3,191 objects); 23 guild-hall trainers; 32 lairs and 11 camps of kima, fynock, decay mites, vynock, murra, sludge panthers, guf drolg, huurton and kahmurra, and Binayre Pirates, Aakuan Cultists, Lost Aqualish and Sif Mercenaries by the snapshot's caves and bunkers; 10 quests from 7 givers.
    - *Rori:* Narmle (the hub), Restuss and the Rebel Outpost (1,691 objects; a global water table at 75 m makes its swamp wading-deep); 24 guild-hall trainers; 32 lairs and 9 camps of flewts, squall, pugoriss, kai tok, gualama, bark mites, capper spineflaps, nightspiders, swamp tusk cats, borgle, vir vur and tortons, and the Cobral, Garyn Raiders, Kobola and Maulers at Galaxies' own bunkers and caves; 10 quests from 6 givers.
    - *Dantooine:* the Imperial Outpost (the hub), Mining Outpost and Agro Outpost (889 objects; the two latter named from what they hold); 17 trainers (8 outdoors on the parade ground); 29 lairs (5 at the Jedi Temple ruins) and 9 camps of piket, thune, bol, huurton, voritor lizards, quenkers, relic reapers and graul, and the Kunga and Janta tribes; 10 quests from 6 givers.
    - *Lok:* Nym's Stronghold (the hub), the Imperial Outpost, Scrapyard Landing and the Pirate Outpost (1,253 objects; an acid sea at 0 m); 12 trainers in the stronghold's meeting rooms, hotel, hospital and cantina; 33 lairs and 10 camps of flits, gurnaset, vesps, snorbals, spined snakes, gurks, langlatch, perlek, kusak and kimogila, and Nym's Pirates and the Canyon Corsairs; 10 quests from 7 givers.
    - *Yavin 4:* the Mining Outpost (the hub), Labor Outpost and Imperial Outpost, with the Great Massassi Temple, the Temple of Exar Kun, Woolamander Palace and Blueleaf Temple exported as landmarks (680 objects; 15 water tables); 23 trainers in sheds, cantina and clinic houses and the market plaza; 34 lairs and 9 camps of whisper birds, tanc mites, stintaril, spined puc, choku, gackle bats, crystal snakes, skreeg, tybis, kliknik, mamien, mawgax and anglers, and Jungle Pirates, Imperial Deserters and Yavin Mercenaries; 10 quests from 6 givers.
    - *Endor:* the Research Outpost (the hub) and Smuggler Outpost, and four Ewok villages (Central and Southeast Panshee, Gondula Lakeside and Fishing) from the snapshot's clusters (1,117 objects); 16 trainers in the outposts' sheds and house; 37 lairs and 11 camps of lantern birds, gurreck, squill, roba, merek, blurrg, bordok, boar wolves, remmer, bolma, mantigrue and gorax, and outpost pirates, Korga Duloks and marauders (two camps at the snapshot's caves); 11 quests from 7 givers. *Known gap:* our terrain evaluator leaves out the Gondula lake basins (the lake villages' posts stand on a lakebed at -9 m), so the bake's heights are lowered under the villages' own lake objects by hand (re-done after a re-bake by its `carve_lakes.py`; the planets' generator scripts are kept outside the repo, as they name local paths); the evaluator should learn whatever makes them.
    - *Dathomir:* the Science Outpost (the hub), Trade Outpost and the Imperial prison, with the Nightsister and Singing Mountain strongholds, caves and the slave mine exported as landmarks (1,701 objects); 12 trainers in the outposts' sheds and houses; 33 lairs and 10 camps of baz nitch, kwi, brackaset, bolma, purbole, shear mites, reptilian fliers, malkloc, gaping spiders, graul and rancors, and Nightsisters, the Singing Mountain clan and escaped prisoners; 10 quests from 7 givers; a red, dusky sky.
    - *Kashyyyk* has terrain on these discs but no world snapshot or client regions, so no verified towns: not brought in.
  - [x] *The galaxy:* `galaxy.ini` names all ten planets, with travel.iff's twelve routes (a fare serves both ways; where the table prices the two directions differently, the lower). The dedicated server loads all ten in 8 s, in 1.4 GB. A new character joins on the spawn point itself (it was staggered by entity id, which a hub's people take too, putting a lone player up to 21 m off a shuttleport's counter). The minimap (`<bake>_ground_preview.png`) is tinted by each ground texture's own color, water blue (it used the terrain editor's map colors, mostly its default yellow); `swgtool ground-preview` redraws it from a bake as it stands. Test: `game/tests/stage13_galaxy_test.gd` (Coronet's starport offers the seven planets travel.iff routes it to, and a ticket lands on Talus at Dearic's starport), and each planet's `stage13_<planet>_test.gd`.

### Playtest fixes (after Stage 13)

- [x] **Trainers:** each teaches one profession. The 248 hub trainers who taught two to four kept the one their outfit fits; 393 new ones (one per other profession, in Galaxies' trainer outfit for it: `dressed_pistol_trainer_*`, `dressed_doctor_trainer_*`...) stand beside them, each spot checked against the planet's collision (the same floor, clear of walls and people). A service is the nearest within reach, where several are.
- [x] **Places' holograms:** crafting stations and mission terminals stand on the floor round them (the floor under their middle was their own top, so their symbols floated a station's height too high); a wrench of our own for crafting (Battlefront's, 32 px, was blurry); a shuttle for travel (it was a trainer's star); a cloning facility's ringed cross, larger, over its roof; a starfighter over a space battle terminal the city doesn't draw (a yellow box and an unstyled label before).
- [x] **Space:** a starfighter's camera is set by its size (TrackCenter and TrackOffset put it inside every one but the X-wing), and the view key gives a cockpit view (EyePointOffset) in any vehicle. Starfighters take off with jump and land with crouch (the throttle alone launched them along the deck), rest on their lowest point (`LandedHeight` at least the model's depth under its origin), and fire their second weapon (torpedoes, missiles) on the secondary button. Their own shots are drawn from their guns (they were the pilot's rifle's). Space maps' posts show each side's insignia (`com_icon_swap` drawn as is was no side's), hangar ceiling turrets hang where the map has them (dropped to the floor, they hung in the air), turrets set in a hull take hits that strike the hull just in front of them, and a shield taking a shot says so (the reticle flashes blue).
- [x] **UI:** loadouts can be renamed; tooltips wrap; the options are two pages, Settings and Keybinds (scrolled); the ability bar has icons (our own, in the place symbols' style) and hides under full screens; a mount isn't worn; the HUD's held weapon is drawn once (its "shadow" was offset far down and right); the space terminal's placeholder travel tab is gone (other planets are reached from the starports); the first loading screen stays up until the world is in; the radar shows the buildings round you; the main menu looks over Mos Eisley with starfighters passing.
- [x] **Second pass:** crafting stations make only their own professions' schematics (Weapon & Droid, Armor & Clothing, Food & Chemical, Structure & Machinery: `station_makes`, held by the server), and Learning the Ropes sends you to the Weapon & Droid one; places' symbols stand over their terminal's or station's model as measured from the city (`SwgCity.prop_top`: the server's spot for one was its own top, or the ground beside its platform), and a quest step's waypoint (known across only) is put on the ground; trainers turned with their backs to the nearest wall (302; 5 stepped off one); ammo droids top up carried standard rounds under 200; a short or tall rider sits on the seat (the ride pose's hip lift is in the model's own size); NewsNet's own symbol; the character creator offers every colour (a Twi'lek's lekku pattern colour was past the four it showed) and dresses the stage in a new character's clothes (toggled).

### Trainer perks (after Stage 13)

- [x] **Step 1, core and trainer ([Perks](PERKS.md)):** weapon and armor professions teach 90 `[perk]`s, an A or a B at each of 5/25/45/65/85, instead of milestones and ranks; a character knows at most 10 perks and milestones in all; learning costs credits the first time, giving up and learning again are free, both at the profession's trainer (`learn_perk`, `learn_milestone`, `forget` in `core/profession.h`; protocol 44 adds `learn_perk` and `forget` service requests and perks in `ProfessionSync`). Active Specialization Slots are gone: what's learned applies to every loadout. Every non-combat and specialist profession's level-1 milestone is its licence (harvesting, surveying, salvaging, crafting and work orders check it, and only a licence holder earns its experience). Tacticals left Heavy Weapons. Old saves' weapon and armor milestones are refunded. Image Designer is removed (profession, ability, designing and dyeing, its datapad tab, 18 trainers). New: `roll_cost` statistic, `reach` weapon handling, a perk's own `Archetypes`; the protocol now accepts every statistic in a loadout (it refused healing received, ability cooldown and debuff resistance). Trainer screen and planner redone. Tests: `profession_test` (training rules, the cap, licences, perks applied, a live trainer), `specialist_test`, `crafting_test`, `stage7_profession_test.gd`, `stage7_crafting_test.gd`.
- [x] **Step 2, triggers:** the 29 triggered perks work (`Trigger = ...` in `professions.ini`; [Perks](PERKS.md#triggers)). Stances, a roll begun and an overheat run in the shared simulation (`combat.cpp`), with each perk's timer in `MoveState::perk_time` so the client predicts them; kills, hits, a shield broken, low health, a while unhurt, a killing blow and a hit's type on the server (`server_perks.cpp`). Protocol 45: the timers in the character's state, a loadout's triggered perks in full precision. Tests: `profession_test` (the simulation's triggers, the wire, and a live server: Untouchable, Ghost Step, a carbine's Chain Breach, Adaptive Plating, Second Chance).
- [x] **Step 3, new mechanics:** the last 30 perks ([Perks](PERKS.md#mechanics)): new weapon fields (body, blast, effect, carry, holstered cooling and reloading, flank, guard, parry, combo; ricochet, pierce, proximity fuse, deflect, knockback and switches) and stats (ammo capacity, self blast, stagger and slow resistance, blast to shield, overcharge, notice, slide, roll chain, charge, radar quiet), with Chance, Aimed and Mark for triggers. Sprinting now pauses a reload. The HUD lists active perks and flashes one that fires (`CombatEvent::perk`); a slide and a shot out of a roll have their own poses. Resolved loadouts travel in full precision (weapons and stat modifiers alike), so a client predicts exactly. Tests: `profession_test` (the simulation's, the wire, two players' melee: Iron Guard, Sweeping Blows through a block, Riposte, Steady Stance and Juggernaut; and a live server: bolts through bodies, off the ground and bursting near enemies, flanking, one's own blast, Deflect, a combo, knockback, a shoulder charge, Overcharge, Reactive Plating, Spotter, Low Profile, Quiet Step).

- [x] **Step 4, advanced, gathering and crafting trees (2026-11-27):** Squad Leader, Doctor and Entertainer, and the eight gathering and crafting professions, teach perks at 1/25/45/65/85, the level-1 pair the licence (`FirstPerk = 1`); perks add `Ability =`, `Grants =` and `Bonus =` lines (`perk_bonus`). The cap is 10 combat and 10 non-combat (`Pool =`). Gathering yields, double hauls, crafting grades, Masterwork, materials, extra items, order pay and profession experience come from perks; the level-50 yield bumps and the level-75 grade bump are gone. Tests: `profession_test`, `crafting_test`, `specialist_test`, `stage7_profession_test.gd`, `stage7_crafting_test.gd`.
- [x] **Step 5, Bounty Hunter (2026-11-27):** perks at 1/25/45/65/85 with two buttons at most (bolas, net and jump pack gone; the jump pack is to be Tactical gear); bounty marks spawn their own outlaw and gang through the hub's population (`on_spawn_mark`, `WorldPopulation::spawn_mark`) and leave when the contract ends; players post bounties on opted-in players (protocol 50, `post_bounty`); perk switches travel into matches (`Loadout::grants`). Tests: `specialist_test` (`test_bounties`, Twin Charges, grants carried). Squad Leader, Doctor and Entertainer reworked to two buttons too (orders carry Shields Up / Press On; Spotter; Squad Spawn Point; heals that splash, linger, revive and reach everyone near; Flourish or Holo-Emitter): `test_doctor_heals`, the spawn point in `test_match`, a two-button check over every profession.

### Exit Criteria

A 16 × 16 km planet populated across its regions simulates with minimal idle server cost when players are clustered *(met for Tatooine)*, and additional cities/planets can be visited via Starport travel when enabled *(Tatooine's towns met; other planets next)*.

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

### Stage 6: Vehicles (Done, except creature mounts; tow cable done in Stage 11)

- [x] **In matches:** Hover vehicles & walkers (`core::simulate_vehicle`), gunner seats & turning turrets (`MoveState::roll_yaw`), walker leg animation & stomp sounds, damage effects & `EngineSound`, `[vehicle_spawn]`s, manned `[turret]`s, bot vehicle driving & gunning, flyers (Hoth snowspeeders). *(Snowspeeder rear-gunner tow cable moved to Stage 11.)*
- [x] **In the world:** Galaxies personal vehicles (`swgtool export-vehicles`), Garage vendor, summon/put away in world. *(Creature mounts moved to Stage 12; speeder equipment slot + keybind moved to Stage 9.)*

### Stage 7: Professions (Framework + Wave 7A Done; Waves 7B & 7C in Stage 12)

See [Professions](PROFESSIONS.md) for the complete 30-profession roster and SWG crosswalk.

- [x] **Core profession framework:** Data-driven 1–100 definitions & 1/25/50/75/100 milestones (`core/profession.h`, `game/professions/professions.ini`), SQLite persistence, Companion Slot (the Active Specialization Slots it had are gone: [Perks](PERKS.md)), `profession_screen.gd` (`K` keybind), skill trainers & combat level calculation.
- [x] **Wave 7A (17 live professions):** 4 armor professions + universal PSG mechanics, 5 weapon professions (including consolidated Melee), 3 gathering professions, 5 core crafting professions.
- [x] **Streamlined gathering & crafting UX:** Unified Multi-Scanner with 3D holographic contours, dedicated Material Vault, Single-Screen Unified Workbench (`workbench_screen.gd`) with Auto-Fill, enhancement slots, Recipe Presets, batch crafting, and Work Orders/Salvaging.
- *(Waves 7B and 7C moved to Stage 12.)*

### Stage 8: Space combat (Done)

- [x] All 6 base space maps imported (`spa1` Yavin, `spa3` Kashyyyk, `spa6` Mygeeto, `spa7` Felucia, `spa8` Hoth, `spa9` Tatooine, plus `spa_sky.lvl` sky domes).
- [x] 16 starfighters/bombers/transports, 27 defense/roof turrets, mobile transport spawns, shield-linked & chain-linked capital ship systems, automated defense turrets, turret control chairs, and ScoreLimit scoring.
- [x] Hangar landing (`LandingRegion`) and on-foot boarding/sabotage combat inside capital ships.
  - *Fixed (after Stage 13):* starfighters couldn't leave their hangars: the hangar shield (`imp_cap_stardestroyer_shield` and the like, `VehicleCollision = CLEAR`) stopped them as it stops soldiers. Map collision now has a vehicles bit (`core::blocks_vehicles`, collision file version 3) from the classes' `VehicleCollision` and primitives' `v` flags, and vehicles meet only that; the six space maps' collision was re-exported (same triangles). `space_test` flies every starfighter out of its hangar on all six.
