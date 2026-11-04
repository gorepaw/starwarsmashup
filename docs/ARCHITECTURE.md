# Architecture

## Goals and constraints

- **End goal:** a real persistent shared world (MMO-style).
- **For now:** one developer, no backend testers for a long time. Build the architecture for the end goal without building the infrastructure early.
- **Assets:** never modified, never redistributed. Imported at runtime or install time from the player's own game files.

## Engine

- **Godot 4.7.2-stable**, custom build with `precision=double` for planet-scale coordinates. Upgrade on purpose, never implicitly.
- **godot-cpp 10.0.0-stable**, double precision, with bindings generated from an API file dumped from our own editor build (committed in `third_party/godot-api/`). Mismatched precision fails in confusing ways, so the extension enforces it with a `static_assert`, and its `.gdextension` file only lists double-precision libraries.
- **GDExtension** (not engine modules), for faster rebuilds and hot reload.
- **CMake** build, Windows-first (MSVC), kept cross-platform. Static MSVC runtime (`/MT`) everywhere. C++ exceptions stay enabled; godot-cpp's default would disable them for everything that links it.
- Setup steps: [BUILDING.md](BUILDING.md).

## Language

C++ everywhere it matters.

**Rule:** if it persists, replicates, or affects gameplay, it goes in C++ `core/`. GDScript is only for throwaway prototypes, editor tooling, and UI layout glue.

## Code layering

```
core/      Pure C++. No Godot headers.
           Simulation, entities/components, combat math, skills, crafting,
           resources, networking protocol, serialization, persistence interfaces.
godot/     GDExtension binding layer.
           Nodes that present core state: rendering, input, audio, UI hookup.
server/    Standalone game server executable: core/ without Godot.
tools/     Offline asset converters and the extractor.
game/      Godot project (scenes, UI, generated import data).
docs/      Design and architecture.
```

Why `core/` has no Godot dependency:
- If Godot's headless server can't scale to MMO size, `core/` runs inside a standalone server process without rewriting game rules.
- It can be unit tested without launching the engine.

## Networking

**Server-authoritative from day one.** Even solo play runs a local server (in the same process or a child process). The client sends input and never owns state.

Target topology:
- **World servers:** each owns one planet or one zone of a planet and simulates authoritatively.
- **Instance servers:** run conquest battles (e.g. 32v32) launched from the world, and report results back.
- **Backend service:** accounts, characters, inventory, resources, crafting, housing, economy. All durable state lives here, not in game servers.

Built in now, even with one player:
- **Interest management:** the server decides what each client can see.
- **Zones/cells:** planets split into cells with handoff boundaries. One process can own every cell for now.
- **Lag compensation:** server-side rewind for hit detection.
- **Shared protocol** defined in `core/`, versioned.

Built so far (Phase 0 spike 4): `core::GameServer` and `core::GameClient` over UDP (`core/net/`). Clients send inputs; the server simulates characters from them and sends snapshots every tick. Clients predict their own character with the same movement code and reconcile against snapshots, and interpolate everyone else. The server runs standalone (`swm_server`) or inside Godot (`SwmServer`), and tests run it with simulated latency and loss (`net::ImpairedLink`).

Combat (Stage 1): weapon state (clip, heat, cooldown, spread) and sprint energy are predicted like movement, from the same inputs. Shots become server-side bolts: plain position-and-velocity records advanced each tick and tested as segments against the world (`World::raycast`) and against characters rewound to the tick the shooter's client was showing (`MoveInput::view_tick`), so what a player hits is what they saw. Snapshots carry health, bolts in flight, and hit and kill events. Clients draw their own bolts the moment they fire and others' from snapshots; bolt looks are presentation only.

Matches (Stage 1): `core::Conquest` is the conquest rules with no networking (posts, tickets, warmup/live/ended); it's stepped by the server with the positions of living soldiers. With a battlefield (`ServerConfig::battlefield`), the server puts joining players on the smaller team; they wait until their client sends a `SpawnRequest` for a class and a post their team holds. Snapshots carry each character's team, class, life (waiting, alive, dead) and score, plus the match state. Clients load the same battlefield file for its classes, so they predict with the right movement and weapons. Bots (`core::Bot`) are ordinary clients driven by code instead of keys.

Hubs (Stage 1): a hub is an open-world `GameServer` plus `core::Hub`. Clients learn the war terminals from the `Welcome` and send a `QueueRequest` at one. The hub launches battle servers (single-match `GameServer`s, in its process for now), sends players there with a `Transfer` carrying a ticket and the battlefield's name, and transfers them back when the results have shown, with a notice in their `PlayerStatus`. The battle server admits only ticket holders (`GameServer::admit`) and reports its result (`on_match_ended`). Clients follow transfers with `core::ClientSession`, which starts a fresh `GameClient` for each server.

## Persistence

- **64-bit persistent entity IDs** for everything that can be saved (characters, items, structures, resource spawns).
- **Persistence interface** in `core/`, with a SQLite implementation now and Postgres later. Gameplay code never talks to a database directly.
- Built so far (Phase 0 spike 5): `core::CharacterStore` with in-memory and SQLite implementations (`swm_sqlite_store`). The game server loads characters when players join and saves them when they leave, periodically, and at shutdown.
- **Data-driven definitions:** items, skills, professions, resource types, and weapons live in data files, not code. Battlefront ODFs and Galaxies datatables convert into this format. First case: `bf2tool export-soldier` reads a soldier class from the player's munged `.lvl` files (ODF classes are `entc`/`wpnc`/`ordc` chunks of FNV-1a-hashed properties), resolves inheritance, and writes an INI that `core::load_soldier` reads.

## Asset pipeline

```
player's game install  →  extractor/converters (tools/)  →  glTF + JSON sidecars  →  Godot import
```

| | Battlefront II (2005) | Star Wars Galaxies |
|---|---|---|
| Containers | `.lvl` (munged). The official Mod Tools also include many source `.msh`/`.tga`/`.odf` files | `.tre` archives of IFF-based files |
| Meshes | `.msh` | `.msh`, `.mgn` (skinned, with customization blend shapes), `.pob` (portalized buildings with interior cells) |
| Terrain | heightmap worlds | procedural `.trn` (layered rules, 16 × 16 km planets) |
| Data | `.odf` | datatables (`.iff`), string files (`.stf`) |

Battlefront's level/world format is **not** imported: battlefields are built on Galaxies terrain (see [Roadmap](ROADMAP.md)).

**Who supplies what:**
- **Galaxies:** all characters (players and AI soldiers alike), so the full Galaxies character creator works for everyone. All equipment the player owns: weapons, armor, and clothing, since that's where the item variety lives. Also terrain, buildings, creatures, and crafting-era items.
- **Battlefront II:** shooter animations, weapon handling and feel (fire modes, overheating), weapon and blaster effects, sounds, vehicles, and props. Read from the munged `.lvl` files in the player's install (`ucfb` chunk trees), not from the Mod Tools.

JSON sidecars carry the data glTF can't: ODF stats, Galaxies customization variables and palettes, POB portal/cell graphs.

Known hard problems:
- **Terrain:** Galaxies planets have no stored heightmaps. Heights come from a procedural rule tree in the `.trn` file (layers of boundaries, filters, and affectors). `tools/swg/terrain` implements that evaluator and bakes regions to heightmaps.
  - The rule structure, parameters, and operations are decoded from the files. Operation codes were inferred from layer names, e.g. "START-flat" uses op 0 = replace.
  - The fractal noise is a stand-in: the parameters are SWG's, but the noise function isn't, so dune-scale detail differs from the original. Everything set by boundaries and constant affectors (city plateaus, mesas, flattened areas) matches: baked heights agree with the world snapshot's placed objects (median error 0.00 m around Mos Eisley).
  - Matching the original noise exactly would mean recovering SWG's noise and random-number functions. That's a separate decision.
  - The same rule tree assigns ground textures (shader families) and carves roads. Bakes include a per-sample texture-family map that the terrain shader blends.
- **Buildings:** a `.pob` is a set of cells (rooms) joined by portals (doorway polygons). Cell 0 is the exterior shell. `tools/swg/building_export` writes one glTF per building, with a node per cell, plus a JSON sidecar of portals, cell links and cell lights.
  - Movement follows Galaxies' floors (`.flr`). They're convex triangles with neighbor links and per-edge flags. A character can't cross an uncrossable boundary edge, so those edges become collision walls, and the floors double as the navigation mesh. Furniture is cut out of the floor and stairs are ramps.
  - Each cell is lit only by its own lights (ambient, parallel, point). `game/rendering/swg_cell.gdshader` reproduces that for cell geometry, and characters indoors switch to their current cell's lights.
  - The same floors and walls are written to `<name>.collision` for `core/`. `core::World` combines them with the terrain for movement, so the server and clients collide with buildings identically.
- **Skeletons:** the two games use different rigs. Battlefront's shooter animations are retargeted onto the Galaxies skeleton (`all_b.skt`) offline, in `tools/retarget/`, not with Godot's import-time humanoid retargeting. That keeps the mapping, rest-pose alignment, and weapon-hardpoint handling in our own tested code. For each mapped joint, the Battlefront bone's world rotation away from its rest pose is applied to the Galaxies rest pose after aligning bone directions. Battlefront's data is mirrored in X relative to Galaxies.
- **Customization:** rebuild Galaxies' `.mgn` blend-shape and palette-hue system in Godot.

## Supported game versions

### Battlefront II

- **Target:** the original 2005 game. The Steam, GOG, and retail disc versions share the same `.lvl` data, so all are supported.
- The official Mod Tools (source `.msh`/`.tga`/`.odf`) pair with the 2005 game and are used where available.
- **Battlefront Classic Collection (2024, Aspyr):** a different build whose data may have changed. Not supported for now. Might be evaluated later.

### Star Wars Galaxies

SWG was patched live from 2003 to 2011, so there is no single version of its files. The project defines one **reference baseline** and accepts files from any install, but only uses files that match that baseline exactly.

- **Reference baseline:** the *Total Experience* retail discs (LucasArts, 2005, 5 CDs). It's the most obtainable legitimate source and bundles the base game with its expansions.
  - The discs are a multi-volume InstallShield setup (`data1.hdr`, `data1.cab`–`data6.cab`). The reference is extracted straight from the cabinets with [unshield](https://github.com/twogood/unshield) (`unshield -d <out> x data1.hdr`), with no installer and no launcher patching, so it stays byte-exact to the discs.
  - Contents: 58 TRE archives (format version `0005`, about 3.3 GB) in the `Star Wars files` group, from `bottom.tre` and `data_*` through `patch_15_*` and the `patch_sku*` archives.
- **Reference manifest:** built once from a baseline disc install. It lists every file path and a hash (SHA-256) of each file's **uncompressed** contents. Hashes contain no game data, so the manifest is committed to the repo.
- **Per-file allowlist:** the extractor reads any install with readable SOE-format archives and checks every file inside every TRE archive against the manifest. **Only files byte-identical to SOE originals are used.** Everything else is ignored.
- **Why per-file and not per-archive:** emulator-community clients repackage everything into their own archives, mixed with their own edits and custom content. Their work is theirs. We never extract it without that team's explicit permission.
- **SWG Restoration installs can't be used.** Restoration's archives (`SwgRestoration_*.tre`, TRE `0006` with 32-byte table entries) encrypt every file's data. We do not decrypt them: that would mean circumventing their protection. A Restoration player needs another source of SOE files (e.g. retail discs) unless the Restoration team chooses to help.
- **Other community clients** (SWG Legends, SWGEmu) are untested.
- **Game content may only depend on baseline assets.** Anything a player's install lacks is reported clearly, not silently substituted.
- **Content groups.** Each reference file belongs to a group, derived from the archive it ships in (`swgtool groups`):

  | Group | Archives | Unique files (Total Experience) | Contents |
  |---|---|---|---|
  | `base` | everything not below | 125,416 (89.5%) | planets, cities, creatures, characters, weapons, armor |
  | `jtl` | `*sku1*` (Jump to Lightspeed) | 11,937 (8.5%) | starships and space assets |
  | `rotw` | `*sku2*` (Rage of the Wookiees) | 2,750 (2.0%) | Kashyyyk |
  | `tow` | `*sku3*` (Trials of Obi-Wan) | not on these discs | |

  The sku-to-expansion mapping is inferred from archive contents. A file counts as `base` if any base archive contains it.
- **Core content requires `base` only.** Hubs, battles, weapons, and armor (roadmap Stages 1–2) must not depend on expansion files. Expansion content is an optional feature that switches off cleanly when its files are missing: for example `jtl` starships as set dressing or vehicles, or `rotw`'s Kashyyyk as a later planet.
- **Excluding expansions does not make every disc set work.** Disc releases also differ by patch level (Total Experience carries `patch_00` through `patch_15`), so older discs have different versions of many base files. Supporting another disc set means adding a reference manifest for it, and the importer handling older file-format versions.
- **More references later:** if a common install is missing baseline files because SOE changed them after the disc release, another verified SOE reference manifest can be added. Each manifest records where it came from.
- **Current manifest:** [`tools/manifests/swg-total-experience-2005.tsv`](../tools/manifests/swg-total-experience-2005.tsv), built with `swgtool` (see [tools/README.md](../tools/README.md)). It has 173,289 entries covering 140,103 unique paths. A path that appears in several archives is listed once per archive. The override order between archives (which copy the client actually loads) is not yet determined.
- **Policy:** "Use files from your own install." The project never links to or distributes client packages.

The extractor reports what it found, for example: `detected: SOE client files, 97% of baseline matched (12 files missing)`.

## Rendering

- Source files are never edited. The importer **generates** Godot materials from the original textures and shader data (Galaxies normal/specular maps where present, Battlefront `.msh` material flags). Problems are fixed in the generator or in per-asset override files.
- Forward+ renderer, SDFGI for large outdoor GI, volumetric fog, SSAO, real-time sun and sky, day/night cycle.
- Terrain drawn by **Terrain3D** (clipmap terrain, MIT, built with double precision), fed from our terrain bakes. It's presentation only: gameplay and the server take heights from our own bakes. Physics via **Jolt**.

## Legal and hygiene rules

- No game assets in the repo, ever. `.gitignore` blocks common asset formats and extraction folders.
- No leaked Galaxies source code, and nothing derived from it. Clean-room work from file formats and public community documentation only.
- Non-commercial. No donations tied to access.
