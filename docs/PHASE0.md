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

## 2. Battlefront II character

- [ ] `.msh` → glTF converter (start from the Mod Tools source assets)
- [ ] Import one clone trooper: mesh, textures, skeleton
- [ ] Import idle and run animations
- [ ] Generated material with modern lighting, source files untouched

## 3. Galaxies world

- [x] Extract the Total Experience reference from the disc cabinets (unshield)
- [x] TRE archive reader: `0005` verified on the disc set; plain `0006` supported but not yet tested on real data
- [x] IFF chunk parser (all 130,954 IFF files in the reference set parse)
- [x] Build the Total Experience reference manifest (path + SHA-256 of uncompressed contents), commit to `tools/manifests/`
- [ ] Determine archive override order (which copy of a path the client loads)
- [ ] Allowlist extraction: use only files matching the manifest, from any install
- [x] ~~Measure SWG Restoration coverage~~: not possible. Restoration encrypts its archives, and we don't decrypt them. `swgtool coverage` exists for other installs.
- [ ] Bake Tatooine (or one region of it) from `.trn` to heightmap and splat maps
- [ ] Render it in Terrain3D at double precision
- [ ] Import one `.pob` building and walk through its interior

## 4. Networked controller

- [ ] Third-person controller in C++ (`core/` movement sim, `godot/` presentation)
- [ ] Server-authoritative movement with client prediction and reconciliation
- [ ] Two local clients that see each other on the Tatooine terrain

## 5. Persistence

- [ ] Persistence interface in `core/`
- [ ] SQLite implementation
- [ ] Save and load a character's position across restarts using a persistent 64-bit ID

## Exit criteria

A Battlefront trooper, rigged and lit, runs across Galaxies' Tatooine next to a second networked player, walks into a Galaxies building, and is still there after restarting the server.
