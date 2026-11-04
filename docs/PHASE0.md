# Phase 0: Feasibility Spikes

Goal: prove the project is technically viable. If all five spikes work, everything after is content and systems.

## 1. Toolchain

- [x] Build the Godot editor from source with `precision=double`: 4.7.2-stable (see [BUILDING.md](BUILDING.md))
- [x] Build godot-cpp with matching precision: 10.0.0-stable, bindings from our editor's API dump
- [x] Root CMake project with presets, zlib via FetchContent, tests via CTest
- [x] Add `core/` (static lib, no Godot) and `godot/` (GDExtension DLL)
- [x] "Hello world" GDExtension class (`SwmInfo`) that calls into `core/`; headless smoke test passes
- [x] Unit test target for `core/`
- [ ] Confirm hot reload works on Windows (needs a manual check in the editor)

## 2. Character: Galaxies body, Battlefront animation

Characters and equipment are Galaxies models (for the full Galaxies character creator and loadout variety). Battlefront II supplies animations, weapon handling, effects, sounds, vehicles, and props. Assets are read from the munged `.lvl` files every Battlefront II owner has, not from the Mod Tools.

- [x] Battlefront `ucfb` (`.lvl`) container reader: `bf2tool`; all 191 `.lvl` files in the Steam install parse
- [x] Galaxies skinned character → glTF (`swgtool export-character`): skeleton (`.skt`), skinned meshes (`.mgn`), appearance (`.sat`/`.lmg`). A human male in the full stormtrooper armor set renders correctly in Godot.
- [x] Battlefront animation skeleton (`zaf_`) and animation bank (`zaa_`) decoding: all 15,016 streams in the `human_0`–`human_4` banks decode exactly; bone and clip names hash with an MSB-first CRC-32
- [x] Retarget Battlefront animations onto the Galaxies skeleton (offline, `tools/retarget/`): rifle idle and run play correctly on the stormtrooper, with the E-11 following Battlefront's weapon hardpoint
- [x] Galaxies weapon model → glTF, attached to the `hold_r` joint (E-11 carbine)
- [ ] Generated materials with modern lighting, source files untouched (base color only so far; normal and specular maps not yet used)
- [ ] Occlusion zones: hide body triangles under worn items (not visibly needed for full armor, but needed for partial outfits)

Provisional decisions:
- Galaxies joint rotation order (`post * bind * pre`): best fit to the vertex weights, and retargeted animations look right with it. Its conjugate fits the weights almost as well and hasn't been tried with animation. A native Galaxies animation (`.ans`) would settle it.
- Archive priority (patch archives over data archives, by patch level): the client's real search order is still unknown.
- Retargeting aligns bone directions only, so twist can drift (wrists, feet). Fine for the spike; revisit for final quality.
- Battlefront animations are assumed to run at 30 fps.

## 3. Galaxies world

- [x] Extract the Total Experience reference from the disc cabinets (unshield)
- [x] TRE archive reader: `0005` verified on the disc set; plain `0006` supported but not yet tested on real data
- [x] IFF chunk parser (all 130,954 IFF files in the reference set parse)
- [x] Build the Total Experience reference manifest (path + SHA-256 of uncompressed contents), commit to `tools/manifests/`
- [ ] Determine archive override order (which copy of a path the client loads). Supporting evidence for the current guess (higher patch levels win): later patches delete files by shipping them empty, e.g. `patch_11_00` empties the test-falcon building files from `patch_05`. `pob-check` counts such files as deleted; `TreLibrary` doesn't special-case them yet.
- [ ] Allowlist extraction: use only files matching the manifest, from any install
- [x] ~~Measure SWG Restoration coverage~~: not possible. Restoration encrypts its archives, and we don't decrypt them. `swgtool coverage` exists for other installs.
- [x] Bake a Tatooine region from `.trn` to a heightmap (`swgtool bake-terrain`). A 4 km region around Mos Eisley bakes in about 2 s. Checked against the 1,905 objects in the world snapshot (`.ws`): median height error 0.00 m, 77% within 1 m.
- [x] Render it at its real world coordinates in the double-precision engine (generated mesh, `game/tools/render_terrain.gd`)
- [x] Ground textures from the shader affectors: shader families (`SGRP`), constant and replace affectors, and shader filters. The bake writes a texture-family map plus each family's textures, and `game/rendering/swg_terrain.gdshader` blends them. Mos Eisley comes out as a light-sand plateau with paved streets, surrounded by gravel, sand and rock. Assumptions are in `swg/terrain.h`:
  - A family applies where the layer's amount reaches the feather clamp. Tatooine's "Flat_Mos_texture" layer uses clamp 0 to paint the city, which a blend-weight reading can't explain.
  - The shader size is meters per texture repeat.
  - Only each family's first variant is used.
  - Edges show the 4 m bake spacing.
  - `swgtool terrain-trace` lists every layer and affector touching a point, which is how the clamp reading was settled.
- [ ] Exact fractal noise: the generator uses a stand-in gradient noise, so fine detail differs from the original (see [Architecture](ARCHITECTURE.md#asset-pipeline))
- [x] Roads (`AROA`): flatten to their stored height profile and paint their texture within their width, feathered. The shader and direction filters are implemented. Assumptions:
  - A road's width is its full width.
  - Direction is the downhill compass bearing.
  - The heights near Mos Eisley are unchanged (its streets are texture layers, not road affectors), so the snapshot check doesn't exercise roads yet.
  - Not yet: rivers (`ARIV`, 6 uses, not on Tatooine) and the bitmap filter (`FBIT`, expansion Kashyyyk maps only).
- [x] Terrain3D integration (v1.1.0, built with double precision; see [BUILDING.md](BUILDING.md#4-terrain3d-optional)). `game/tools/terrain3d_bake.gd` imports a bake: heights, plus one texture asset per ground-texture family, painted through the control map. The Mos Eisley region renders the same as the plain mesh, with smoother texture blends and finer distant shading. `render_terrain.gd` and `walk_building.gd` take `--terrain3d`. Terrain3D only draws the ground: collision and, later, the server use heights from our own generator.
- [x] Import one `.pob` building and walk through its interior (`swgtool export-building`, `game/tools/walk_building.gd`). The Mos Eisley cantina, placed where the world snapshot has it on the baked terrain. A stormtrooper runs from the street through the front door, both foyers and the main room, up onto the band stage, and on another route into the back office. It uses physics and Godot navigation, and every cell is lit by its own Galaxies lights. 223 of 228 buildings and every floor in the reference set load (`swgtool pob-check`). The rest are 3 buildings in an older layout version (`PRTO 0001`) and 2 deleted by patches.
  - Floors are the collision and the navigation mesh: Galaxies floors are convex triangles joined at shared edges. Uncrossable boundary edges become walls, while doorway edges and exits to open ground stay open.
  - Not yet: doors (styles are listed, no door objects placed), the furniture and other objects the snapshot places inside cells, alpha-tested textures, and rendering the outside only through portals (for now the terrain is hidden while the camera is indoors).

## 4. Networked controller

- [x] Third-person controller in C++: movement in `core/` (`simulate_move`: run, sprint, strafe, jump, slope limit, ledges) on the baked terrain heights (`core::Heightfield`, the same triangles the terrain mesh draws). `godot/` exposes it as `SwmClient`/`SwmServer`; drawing, camera and key input are a GDScript dev tool for now (`game/tools/net_demo.gd`).
- [x] Server-authoritative movement with client prediction and reconciliation (`core::GameServer`, `core::GameClient`, protocol version 1 over UDP):
  - Clients send inputs only, each tick, repeating the ones not yet acknowledged, so lost packets cost nothing. The server simulates every character from its inputs and sends a snapshot every tick (30 Hz).
  - The client predicts its own character with the same code and replays unacknowledged inputs on each snapshot. Because the server only advances a character when its input arrives, predictions match exactly: no corrections at 200 ms round trip with jitter and 10% loss. Server-side moves (e.g. a teleport) are corrected and eased in.
  - Other characters are drawn 100 ms in the past, interpolated between snapshots.
  - `core/tests/net_test.cpp` runs a server and two clients over a simulated laggy, lossy network.
- [x] Two local clients that see each other on the Tatooine terrain: the standalone `swm_server` (core only, no Godot) plus two `net_demo.gd` windows, or one window hosting with `--host`. Playable with WASD, mouse, Shift and Space; `--bot circle|chase` drives a client for automated runs.

  ```powershell
  build\msvc\server\Release\swm_server.exe --bake game\imported_assets\terrain\tatooine_mos_eisley
  godot --path game --script res://tools/net_demo.gd -- imported_assets/terrain/tatooine_mos_eisley imported_assets/characters/stormtrooper/stormtrooper.gltf
  ```

Provisional decisions:
- Movement tuning (run 5.5 m/s, sprint 8, jump 4.6 m/s) is a guess at Battlefront's feel, until it comes from data.
- Snapshots carry full-precision state for every character. Quantizing and interest management come with larger player counts.
- Movement only collides with terrain so far. Building floors and walls (spike 3) need to move into `core/` for the server to use them.
- Server and clients must use the same bake and movement settings; the handshake doesn't check that yet.

## 5. Persistence

- [x] Persistence interface in `core/` (`CharacterStore`, `swm/core/persistence.h`), with an in-memory implementation for tests. Game code never touches a database.
- [x] SQLite implementation (`SqliteCharacterStore`, in its own `swm_sqlite_store` library so `core/` doesn't depend on SQLite). SQLite 3.54.1 is fetched at configure time and checked against its published SHA3-256. WAL mode, a versioned schema (`PRAGMA user_version`; a database from a newer build is refused), and atomic batch saves.
- [x] Save and load a character's position across restarts using a persistent 64-bit ID:
  - The server loads a joining player's character by name (the stand-in for an account until there are accounts), or creates it with a new id. The id is never reused and travels to the client in the handshake (protocol version 2).
  - Characters are saved when they leave or time out, every 10 s, and when the server stops (`--seconds` or Ctrl+C). A second login with a name already playing is refused.
  - Checked with real processes: `swm_server --db world.sqlite` stopped while a client was running, saving it at (3527.85, -4797.21). After a restart the same name came back as character 1 at exactly (3527.85, -4797.21). `core/tests/persistence_test.cpp` covers the same restart in-process, plus a crash (no shutdown save) recovered from the periodic save.

Provisional decisions:
- Saves are synchronous on the server thread. Fine for a handful of characters; a save queue on its own thread comes with more state to save.
- A saved character respawns on the ground at its saved x and z.

## Exit criteria

A Galaxies character, lit and holding a Galaxies blaster, runs with Battlefront animations across Galaxies' Tatooine next to a second networked player, walks into a Galaxies building, and is still there after restarting the server.

Status: every part works, but not yet all in one run. The networked, saved character runs on Tatooine next to a second player (spikes 4 and 5), and the single-player walk-through enters the cantina (spike 3). What's left is moving building floors and walls into `core/` movement, so the server lets a networked character walk inside.
