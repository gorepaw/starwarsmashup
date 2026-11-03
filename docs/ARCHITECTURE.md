# Architecture

## Goals and constraints

- **End goal:** a real persistent shared world (MMO-style).
- **For now:** one developer, no backend testers for a long time. Build the architecture for the end goal without building the infrastructure early.
- **Assets:** never modified, never redistributed. Imported at runtime or install time from the player's own game files.

## Engine

- **Godot 4**, custom build with `precision=double` for planet-scale coordinates.
- Pin one Godot version and upgrade on purpose.
- **godot-cpp** built with matching precision. Mismatched precision builds fail in confusing ways.
- **GDExtension** (not engine modules), for faster rebuilds and hot reload.
- **CMake** build, Windows-first (MSVC), kept cross-platform.

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

## Persistence

- **64-bit persistent entity IDs** for everything that can be saved (characters, items, structures, resource spawns).
- **Persistence interface** in `core/`, with a SQLite implementation now and Postgres later. Gameplay code never talks to a database directly.
- **Data-driven definitions:** items, skills, professions, resource types, and weapons live in data files, not code. Battlefront ODFs and Galaxies datatables convert into this format.

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

JSON sidecars carry the data glTF can't: ODF stats, Galaxies customization variables and palettes, POB portal/cell graphs.

Known hard problems:
- **Terrain:** bake `.trn` planets to heightmaps and splat maps for Terrain3D first. Port the procedural evaluator later only if needed.
- **Skeletons:** the two games use different rigs. Retarget both onto one shared humanoid skeleton so Battlefront's shooter animations drive Galaxies' customizable bodies.
- **Customization:** rebuild Galaxies' `.mgn` blend-shape and palette-hue system in Godot.

## Rendering

- Source files are never edited. The importer **generates** Godot materials from the original textures and shader data (Galaxies normal/specular maps where present, Battlefront `.msh` material flags). Problems are fixed in the generator or in per-asset override files.
- Forward+ renderer, SDFGI for large outdoor GI, volumetric fog, SSAO, real-time sun and sky, day/night cycle.
- Terrain via **Terrain3D** (clipmap terrain). Physics via **Jolt**.

## Legal and hygiene rules

- No game assets in the repo, ever. `.gitignore` blocks common asset formats and extraction folders.
- No leaked Galaxies source code, and nothing derived from it. Clean-room work from file formats and public community documentation only.
- Non-commercial. No donations tied to access.
