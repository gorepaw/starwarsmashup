# Phase 0: Feasibility Spikes

Goal: prove the project is technically viable. If all five spikes work, everything after is content and systems.

## 1. Toolchain

- [ ] Build the Godot 4 editor from source with `precision=double` (pin the version)
- [ ] Build godot-cpp with matching precision
- [ ] CMake project with `core/` (static lib, no Godot) and `godot/` (GDExtension DLL)
- [ ] "Hello world" GDExtension node that calls into `core/`
- [ ] Unit test target for `core/`
- [ ] Confirm hot reload works on Windows

## 2. Battlefront II character

- [ ] `.msh` → glTF converter (start from the Mod Tools source assets)
- [ ] Import one clone trooper: mesh, textures, skeleton
- [ ] Import idle and run animations
- [ ] Generated material with modern lighting, source files untouched

## 3. Galaxies world

- [ ] TRE archive reader (disc-era and final-client formats, including TOC indexes)
- [ ] IFF chunk parser
- [ ] Build the Total Experience reference manifest (path + SHA-256 of uncompressed contents), commit to `tools/manifests/`
- [ ] Allowlist extraction: use only files matching the manifest, from any install
- [ ] Measure SWG Restoration coverage: what percentage of baseline files match byte-for-byte, and which are missing
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
