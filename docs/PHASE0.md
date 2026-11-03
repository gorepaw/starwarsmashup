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
- [ ] Splat maps from the shader affectors (heights only so far)
- [ ] Exact fractal noise: the generator uses a stand-in gradient noise, so fine detail differs from the original (see [Architecture](ARCHITECTURE.md#asset-pipeline))
- [ ] Roads (`AROA`), and direction and shader filters (currently ignored)
- [ ] Terrain3D integration (needs a double-precision build of the addon)
- [x] Import one `.pob` building and walk through its interior (`swgtool export-building`, `game/tools/walk_building.gd`). The Mos Eisley cantina, placed where the world snapshot has it on the baked terrain. A stormtrooper runs from the street through the front door, both foyers and the main room, up onto the band stage, and on another route into the back office. It uses physics and Godot navigation, and every cell is lit by its own Galaxies lights. 223 of 228 buildings and every floor in the reference set load (`swgtool pob-check`). The rest are 3 buildings in an older layout version (`PRTO 0001`) and 2 deleted by patches.
  - Floors are the collision and the navigation mesh: Galaxies floors are convex triangles joined at shared edges. Uncrossable boundary edges become walls, while doorway edges and exits to open ground stay open.
  - Not yet: doors (styles are listed, no door objects placed), the furniture and other objects the snapshot places inside cells, alpha-tested textures, and rendering the outside only through portals (for now the terrain is hidden while the camera is indoors).

## 4. Networked controller

- [ ] Third-person controller in C++ (`core/` movement sim, `godot/` presentation)
- [ ] Server-authoritative movement with client prediction and reconciliation
- [ ] Two local clients that see each other on the Tatooine terrain

## 5. Persistence

- [ ] Persistence interface in `core/`
- [ ] SQLite implementation
- [ ] Save and load a character's position across restarts using a persistent 64-bit ID

## Exit criteria

A Galaxies character, lit and holding a Galaxies blaster, runs with Battlefront animations across Galaxies' Tatooine next to a second networked player, walks into a Galaxies building, and is still there after restarting the server.
