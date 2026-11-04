# Tools

Offline asset tools. Nothing here contains game data.

## Build

Requires Visual Studio 2022 (C++ workload) and CMake 3.28+. zlib is fetched at configure time.

```powershell
cmake --preset msvc
cmake --build --preset msvc-release
ctest --preset msvc-release
```

## swgtool

```
swgtool list <archive.tre>
swgtool manifest <dir> --name <name> --source <text> -o <out.tsv>
swgtool coverage <manifest.tsv> <dir> [--missing <out.tsv>]
swgtool groups <manifest.tsv> [--list <group>]
swgtool iff <archive.tre> <path>
swgtool iff-check <dir>
swgtool export-character <dir> <out_dir> --name <name> --sat <path.sat>... [--weapon <path.lod>]
                         [--bf2-lvl <ingame.lvl> --bf2-bank <bank> --anim <name>...]
swgtool bake-terrain <dir> <out_prefix> --trn <path.trn> --center <x> <z> --samples <n> --spacing <m>
                     [--snapshot <path.ws>]
swgtool terrain-trace <dir> --trn <path.trn> --at <x> <z>
swgtool export-building <dir> <out_dir> --pob <path.pob> --name <name>
swgtool pob-check <dir>
swgtool snapshot <dir> <path.ws> [--match <text>] [--near <x> <z> <radius>]
```

- **list**: print every file in a TRE archive (uncompressed size, stored size, compressed flag, path).
- **manifest**: hash the uncompressed contents of every file in every `.tre` under `<dir>` and write a reference manifest. Refuses to write if any archive or file fails to read.
- **coverage**: compare an install against a reference manifest. For each reference file it reports `matched` (byte-identical at the same path), `moved` (identical, but only under another path), `changed` (path exists, contents differ), or `absent`, plus a per-folder breakdown and a count of install files that aren't in the reference.

- **groups**: count a manifest's unique files per content group (`base`, `jtl`, `rotw`, `tow`), or list one group's paths with `--list`. `coverage` also reports matches per group.
- **iff**: print the chunk tree of one IFF file inside an archive, with a hex preview of each chunk's data.
- **iff-check**: parse every IFF file in every archive under `<dir>` and print per-extension counts, failures, and root form names. On the Total Experience set, all 130,954 IFF files parse with no failures.

- **export-character**: build a glTF character from Galaxies files in every `.tre` under `<dir>`. The first `.sat` (e.g. a species body) defines the skeleton. Every further `.sat` (armor, clothing) is skinned to it. An optional weapon `.lod` attaches to the `hold_r` joint. Writes `<name>.gltf`, `<name>.bin`, and one PNG per texture (decoded from DDS) into `<out_dir>`. Write output only to gitignored folders such as `game/imported_assets/`. Example: a human male in stormtrooper armor with an E-11:

  ```powershell
  swgtool export-character E:\swg_te\extracted\Star_Wars_files game\imported_assets\characters\stormtrooper `
    --name stormtrooper --sat appearance/hum_m.sat --sat appearance/armor_stormtrooper_helmet_m.sat ... `
    --weapon appearance/lod/wp_carbine_e11.lod
  ```

  With `--bf2-lvl`, `--bf2-bank`, and one or more `--anim`, Battlefront II animations are retargeted onto the character and written as glTF animations, e.g. `--bf2-lvl <Battlefront>\GameData\Data\_lvl_pc\ingame.lvl --bf2-bank human_0 --anim human_rifle_stand_idle_emote_full --anim human_rifle_stand_runforward`. Clips play in place.

  Not handled yet: occlusion zones (hiding body triangles under armor), customization (skin palettes, blend shapes), and LOD levels below the most detailed.

- **bake-terrain**: evaluate a planet's procedural terrain (`.trn`) over a square region. Heights, ground textures and roads are evaluated; flora, colors, environments and rivers aren't. It writes:
  - `<out_prefix>.r32`: float32 heights, row-major, rows along +Z.
  - `<out_prefix>.shaders`: one byte per sample, the ground-texture family as an index into the JSON's `shader_families`, 255 for none.
  - `<out_prefix>.json`: grid origin, spacing, height range, and the texture families with their texture file names.
  - `<out_prefix>_textures/`: the families' textures as PNG.
  - `<out_prefix>_preview.png` (shaded relief) and `<out_prefix>_ground_preview.png` (relief tinted by each family's map color), both north up.

  With `--snapshot`, compares the baked heights against the world snapshot's placed objects. Example, 4 km around Mos Eisley at 4 m:

  ```powershell
  swgtool bake-terrain E:\swg_te\extracted\Star_Wars_files game\imported_assets\terrain\tatooine_mos_eisley `
    --trn terrain/tatooine.trn --center 3500 -4800 --samples 1025 --spacing 4 --snapshot snapshot/tatooine.ws
  ```

  The fractal noise is a stand-in for SWG's own, so dune-scale detail is approximate. The rule semantics that were inferred rather than read from the files are marked ASSUMPTION in `swg/terrain.h`.

- **terrain-trace**: print every layer and affector that touches one point, in order, with the layer's amount there and the resulting height and ground texture. For working out what the rules do, e.g. `--trn terrain/tatooine.trn --at 3550 -4780` (central Mos Eisley).

- **export-building**: export a portalized building (`.pob`) to `<name>.gltf` with one node per cell. Each cell holds its render mesh plus two collision meshes without materials: `<cell>_floor` (walkable triangles from the cell's `.flr`) and `<cell>_walls` (3 m quads rising from the floor's uncrossable edges). The `<name>.json` sidecar lists the portal polygons and, per cell, its portal links, door styles, and lights. Cell meshes may be meshes, LOD lists, component appearances, or redirects (`.msh`, `.lod`, `.cmp`, `.apt`). Example, the Mos Eisley cantina:

  ```powershell
  swgtool export-building E:\swg_te\extracted\Star_Wars_files game\imported_assets\buildings\thm_tato_cantina `
    --pob appearance/thm_tato_cantina.pob --name thm_tato_cantina
  ```

  It also writes `<name>.collision`: the floor triangles and walls again, in a compact binary for `core::World` (the game server and clients' movement). Its format belongs to `core/` (`swm/core/world.h`).

  Not handled yet: doors and the objects placed inside cells.
- **pob-check**: parse every building and floor, and load every cell mesh. On the Total Experience set: 223 buildings (2,669 cells, 12,617 lights) and 3,896 floors load. 93 files are deleted by later patches (shipped empty), and 3 buildings use an older layout (`PRTO 0001`) that isn't supported.
- **snapshot**: list a world snapshot's top-level objects (id, position, yaw in degrees, number of objects inside, template), filtered by template substring and/or distance. For example, `--match cantina` finds the Mos Eisley cantina at (3432.35, 5.00, -4818.13), yaw 38.09. A building's object template names its `.pob` (`portalLayoutFilename`).

Supported archives: TRE `0005` (retail discs) and plain `0006`. SWG Restoration's `0006` archives encrypt their file data and are reported as unsupported. We don't decrypt them.

## bf2tool

```
bf2tool tree <file.lvl> [--depth <n>]
bf2tool list <file.lvl> <tag>
bf2tool check <dir>
bf2tool skeleton <file.lvl> <bank>
bf2tool anims <file.lvl> <bank> [name...]
```

Battlefront II munged files (`.lvl`) are `ucfb` chunk trees: `lvl_` sub-levels holding models (`modl`), textures (`tex_`), skeletons (`skel`), animations (`zaa_`/`zaf_`), entity classes (`entc`/`ordc`/`wpnc`/`expc`), and more.

- **tree**: print the chunk tree with sizes and `NAME`s, to `--depth` levels (default 3).
- **list**: print the size and name of every chunk with a given tag, e.g. `bf2tool list side\rep.lvl modl`.
- **check**: parse every `.lvl` under `<dir>` and count content chunk types. On the Steam install, all 191 files (4.7 GB) parse with no failures.
- **skeleton**: print an animation bank's skeleton (`zaf_`) with rest world positions, e.g. `bf2tool skeleton ingame.lvl human_0`.
- **anims**: list an animation bank's clips (`zaa_`): name hash, frames, bones. Names given on the command line are matched by hash, e.g. `bf2tool anims ingame.lvl human_0 human_rifle_stand_runforward`.

Animation data uses its own block format inside `BIN_` (byte-reversed tags) and identifies bones and clips by name hash (`bf2/hash.h`). See `bf2/animation.h` for the stream encoding.

## Manifests

`manifests/` holds reference manifests: one line per file with `sha256 size archive path`. See [Supported game versions](../docs/ARCHITECTURE.md#supported-game-versions).

| Manifest | Source | Files |
|---|---|---|
| `swg-total-experience-2005.tsv` | *Star Wars Galaxies: The Total Experience* (LucasArts, 2005), 5 CDs, cabinets extracted with unshield 1.6.2 | 173,289 entries, 140,103 unique paths, 5.4 GiB uncompressed |

To reproduce it from your own discs:

```powershell
unshield -d <extracted> x <cabs>\data1.hdr
swgtool manifest "<extracted>\Star_Wars_files" --name swg-total-experience-2005 --source "..." -o out.tsv
```
