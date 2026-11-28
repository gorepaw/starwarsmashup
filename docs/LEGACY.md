# The Legacy Questline

**Status: Phase 1 under way (2026-11-28).** This is the plan for porting Galaxies' Legacy Questline, the long multi-planet story that starts in Mos Eisley, into the game.

## Decisions

These are the user's choices (2026-11-28):

- **A faithful port.** We keep Galaxies' quests, NPC names, places, step order and branches.
- **Text is shortened** from what the original quest text must have said, so it fits our UI. Each quest gets a two-to-three sentence summary, one line per step for the tracker, and a short turn-in line.
- **The user writes custom starting quests** that come before the Legacy and fill its gaps. The Ilsa Marn chain in `tatooine_quests.ini` came from Stage 5 and is a candidate to be replaced by them.
- **Space legs queue one of our space battles**, with an option to **skip** them until space play is more solid. Every space leg in the original also has a ground version, and skipping leads to that version rather than just passing the step.

## Source

The Legacy came out after *The Total Experience*, so its quest tables aren't in our Galaxies files. The encrypted Restoration archives and the leaked server source are off-limits. So the port is rebuilt from walkthroughs: SWGAide's 18-part *NGE Legacy Quest Guide*, its Jabba's Theme Park page, and swgr.org.

A full inventory, quest by quest, is kept **outside the repo** because it's compiled from a third-party guide. It has every quest's giver, steps, coordinates (in Galaxies coordinates, like our `Position =`) and rewards, with the guide's typos marked. It lives in `E:\swm_work\legacy\`:

- `README.md`
- `tatooine.md`
- `naboo.md`
- `corellia_talus_rori.md`

## Scope

| Part | Where | Levels | Quest log entries |
|---|---|---|---|
| 1. The Beginning | Tatooine: Mos Eisley | 1-14 | 31 |
| Jabba's Theme Park (required before part 2's "Tracking the Head") | Jabba's Palace, Wayfar | 18-22 | 7 parts |
| 2. Rebel/Imperial route | Tosche Station, Lars homestead, Mos Espa, Mos Entha, Jabba's Palace | 14-22 | 23 |
| 3-5. Royal Security Forces, droid modules | Naboo: Kaadara, Keren, Theed, Moenia, Dee'ja Peak | 22-30 | 54 |
| *The original Legacy ends here (Medal of Bothawui, or the Emperor's Fist)* | | | |
| 6-12. CorSec | Corellia: Coronet, Tyrena, Kor Vella, Doaba Guerfel | 30-39 | 72 |
| 13-16 | Talus: Nashal, Dearic | 40-43 | 26 |
| 17. Droid factory, then the Right Handless Man case | Corellia, then Rori: Narmle | 45-54 | 18 |
| 18 | Coronet, Kor Vella, Lok | 65 | 4 |

That's about 235 quest log entries. Roughly 40 are "talk to X" hand-offs and 45 are optional side quests.

Nym's Themepark is **not** part of it (only swgr.org links them). Part 18 needs the Meatlump Themepark's opening quest, which the guide doesn't cover.

## How it's built

### Data layout

- `game/hubs/legacy/quests/<planet>.ini` holds the Legacy's quests, one file per planet, in the usual `[quest]`/`[step]` format. Every planet's hub loads the whole folder (`Quests = legacy/quests`), so a Legacy quest is known wherever the player is.
- `game/hubs/legacy/sites/<planet>.ini` lists the planet's Legacy sites out in the country. Each `[site]` is exported with the planet's towns as an `export-city --site` area. Its `[place]`s are Total Experience templates (caves, bunkers) standing where Galaxies' servers spawned them. A site's rooms more than 6 m under the terrain keep the terrain over them.
- Each quest says where it happens: `Planet = tatooine` on the `[quest]`, overridden by a step's own `Planet =` (for example, "report to Tchorshel on Naboo").
- Legacy NPCs stand in their town's file (`anchorhead.ini`, `kaadara.ini`, ...) as `[npc]`s, positioned from the guide's coordinates and checked against the city export.

### Quest system additions (core::QuestBook)

| Feature | For | Phase |
|---|---|---|
| Quests that span planets: definitions known everywhere, steps only counted on their own planet, waypoints only shown there | everything after part 2 | 1a |
| Finished quests kept on other planets (`tidy` deleted any whose definition the planet didn't know: a live bug) | everything | 1a |
| Several quest files per hub, and a folder (`Quests =` repeated) | the legacy folder | 1a |
| `Excludes =`: quests that rule each other out | Bib's offer, the mayor's three chains, Rebel/Imperial contacts, Typho's routes, Runaways space/ground | 1a |
| `Next =`: completing a quest starts the next by itself | the chain's hand-offs; quests given by comm | 1a |
| `Comm =` on a quest or step: a message from a named speaker when it starts | Han, Bib, Jabba, Panaka, Joth, Taarna, ... | 1a (shown as a notice); a comm window comes in 1b |
| `use` steps: interact with a quest object placed in the world (terminal, datapad, crate, bomb, panel) | in most quests | 1b (protocol, client, objects) |
| Choosing one reward item from several | about 20 quests | 1c |
| `battle` steps: queue a space battle, with Skip | the 4 space legs | 1d |
| Timers, checkpoint races, follow-without-attacking, fights that end in surrender, don't-kill rules, click-to-spawn ambushes, photographing a target, using an item on an NPC, quests offered only to Rebels or Imperials | where they occur | 1e, when the content needs them |

### Content conventions

- **Ids:** `legacy_<part>_<slug>`, e.g. `legacy_1_a_speeder_for_me`. Jabba's are `jabba_<n>_<slug>`.
- **Levels:** Galaxies' level goes into `Level` unchanged. Our combat level and XP curve still need checking against it (open item).
- **Rewards:**
  - Credits come from the guide.
  - XP is scaled to our curve (open item).
  - Items map onto our catalog. Under the no-cosmetic-only-items rule, hats and costume rewards become equipment (or are dropped).
  - Jabba, RSF and CorSec faction rewards need reputations to exist first (open item; for now they become city reputation through the quest's giver).

## Phases

1. **The quest system**
   - [x] 1a. Quests across planets, `Excludes`, `Next`, `Comm`, several quest files per hub, and the `tidy` fix. Core only, with tests (`quest_test`: `test_legacy_book`, `test_server_chain`).
     - New keys: `[quest]` takes `Planet`, `Excludes`, `Next`; `[step]` takes `Planet`, `CommFrom`, `Comm`. A hub's `Quests =` can repeat and can name a folder.
     - A hub run on its own, without the galaxy, has no planet. It treats every quest as local, so another planet's Legacy steps would count there.
   - [ ] 1b. `use` steps and quest objects. This needs a protocol message, client interaction, and objects spawned from quest data. A comm window too.
   - [ ] 1c. Reward choice.
   - [ ] 1d. `battle` steps and Skip.
   - [ ] 1e. The special mechanics, each when its content comes up.
2. **Sites and data checks**
   - [x] Check every site against the *Total Experience* snapshots. The checker is `E:\swm_work\legacy\site_check.py` and writes `sites.md` next to it. Results, 2026-11-28, for 97 sites:
     - **About 14 exist as the Legacy's site.**
       - 9 interiors: Jabba's palace, Theed palace, the Squill cave, Lok's droid-engineer cave, Coronet's Capitol, the Wayfar and Moenia cantinas, Tosche Station, the hermit's house.
       - About 5 outdoor spots: the krayt skeleton, the Sarlacc, the Lars homestead's harvesters, the Gungan ruins by Boss Nass, a tent camp on Lok.
     - **Everything else was server-spawned in Galaxies.** That's about 75 sites, including nearly every bunker, cave, camp and lab. The other snapshot "hits" are coincidences (Jedi shrines, gravestones, guild halls). Only Kashyyyk has buildout tables in *The Total Experience*.
     - **Templates exist to place them with.** *The Total Experience* has generic interiors: caves `general/cave_01..08` (plus the damprock, ice and mirror variants) and `tatooine/cave_tatooine_style_*`; bunkers `general/bunker_imperial_*`, `bunker_rebel_*`, `bunker_small_outpost_*`, `bunker_research_facility_01`, `bunker_warren_01`. It also has some that match by name: `naboo_pirate_bunker`, `rori_cobral_bunker`, `rori_garyn_bunker`, `corellia_nyax_bunker`, `corellia_drall_cave`, `talus_*_bunker`, `lok_evil_droid_engineer_cave`, `tatooine_tusken_bunker`.
     - **City exports only cover towns** (the hub and its `Town =` areas). So even the sites that are in the snapshot but outside a town (Jabba's palace, the Squill cave, Lok's cave) aren't in the game yet.
   - [x] **Site export tooling** (2026-11-28):
     - `export-city --site`, a hub file's `[place]`, deep rooms keeping their terrain (`site_room_depth`, 6 m), and site areas winning over a town's.
     - `rebuild_imported_assets.py` reads `game/hubs/legacy/sites/<planet>.ini`.
     - Tested on Tatooine with the Squill cave, Jabba's palace and a placed small-outpost bunker for Viagon Shinn. All three export with their interiors and doors. The bunker opens only an 11 × 18 m entrance patch; the cave opens about 1,300 m² over its shallow parts (still to be checked in game).
   - [ ] **Every Legacy site in the export.** The site files are written: 74 sites on Tatooine, Naboo, Corellia, Talus, Rori and Lok, 67 of them with placed objects.
     - They come from a generator kept with the inventory (`E:\swm_work\legacy\make_sites.py`), which picks templates by size (see the inventory README) and reports each placement's terrain.
     - A building or bunker on more than 6 m of relief moves to the flattest spot within 80 m, at least 25 m from the site's other buildings. Galaxies flattened the ground under its points of interest; our bake keeps the terrain as it was. Quest steps inside a site get positions from our placed building, not the guide's.
     - Caves stay where the guide puts them, in their hillsides. So do small props.
     - Camps aren't placed: Galaxies' camp templates name no model (its server assembled them from props), so camps are our bandit-camp spawns, with props later if wanted.
     - Still to do: the export of every planet, then a look at each site in game. One data file (planned: `game/hubs/legacy/sites.ini`) with each site's planet, centre and radius, and for a missing site the template to place, its position and its yaw. `rebuild_imported_assets.py` exports them like towns: `export-city` gets each site as an `--area`, plus a new option to place templates the snapshot lacks. That gives them the same collision, interior rooms and terrain holes as town buildings. Caves may need the terrain shaped around their mouths; check with the first one. `tools/swg` is shared with the distribution/backend session, so coordinate with it first.
   - [ ] The enemies the Legacy names (swoop gangs, Valarians, Skaak Tippers, Mummers, ...) against our spawn files.
   - [ ] Legacy rewards against the item catalog.
3. **Content, part by part.** Each part ships playable: NPCs placed, quests written, sites and enemies in place.
   - [ ] Part 1 (Mos Eisley; follows the user's starting quests)
   - [ ] Jabba's Theme Park
   - [ ] Part 2
   - [ ] Parts 3-5 (Naboo)
   - [ ] Parts 6-12 (Corellia)
   - [ ] Parts 13-16 (Talus)
   - [ ] Parts 17-18 (Corellia, Rori, Lok), plus the Meatlump Themepark's opening quest

## Open items

- Level and XP mapping (Galaxies' level 1-65 against our combat levels).
- Faction reputations for Jabba, the RSF, CorSec and Swoop (only the four war factions and cities have one now).
- Missing from the guide: Bib's "Find Trehla" and pilot-trainer branches, the Jonni Skaak side quest, the Meatlump Themepark opening quest, and a few spots without coordinates.
- Mission ids (`mission:lair:3`) mean a different lair on each planet. A mission taken on one planet and carried to another is wrong today. This is unrelated to the Legacy, but turns up next to the planet work.
