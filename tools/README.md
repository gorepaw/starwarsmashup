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
bf2tool odf <file.lvl> [class]
bf2tool export-loadout <_lvl_pc dir> <side> <soldier class> -o <loadout.ini> [--items <dir>]
bf2tool export-map <_lvl_pc dir> <map> -o <out dir> [--mode conquest] [--skip <layer>]...
```

Battlefront II munged files (`.lvl`) are `ucfb` chunk trees: `lvl_` sub-levels holding models (`modl`), textures (`tex_`), skeletons (`skel`), animations (`zaa_`/`zaf_`), entity classes (`entc`/`ordc`/`wpnc`/`expc`), and more.

- **tree**: print the chunk tree with sizes and `NAME`s, to `--depth` levels (default 3).
- **list**: print the size and name of every chunk with a given tag, e.g. `bf2tool list side\rep.lvl modl`.
- **check**: parse every `.lvl` under `<dir>` and count content chunk types. On the Steam install, all 191 files (4.7 GB) parse with no failures.
- **skeleton**: print an animation bank's skeleton (`zaf_`) with rest world positions, e.g. `bf2tool skeleton ingame.lvl human_0`.
- **anims**: list an animation bank's clips (`zaa_`): name hash, frames, bones. Names given on the command line are matched by hash, e.g. `bf2tool anims ingame.lvl human_0 human_rifle_stand_runforward`.
- **odf**: list a file's ODF classes (munged into `entc`/`ordc`/`wpnc`/`expc` chunks: a `TYPE` name, a `BASE` parent, and `PROP` entries of a 32-bit FNV-1a hash of the lowercase property name plus a string value), or print one class's properties. Names are resolved from a list of documented ODF keys; unknown ones print as hashes.
- **export-loadout**: write a Battlefront soldier's kit as a test loadout. Each weapon becomes an item file (`core::load_weapon`) in `--items` (default: `items` beside the loadout's folder), and the loadout file (`core::load_loadout`) names them: the first two primary weapons as the primary and the sidearm, the first grenade (Battlefront's weapon channel 1) as the secondary. Inheritance is resolved across `side/<side>.lvl` and `ingame.lvl`. The soldier's own health and speed aren't exported: characters have no classes, and their statistics come from their gear. Supported: cannons firing bolts or bullets (including shotguns' pellets per shot), launchers firing missiles, and grenades (thrown or lobbed, like mortars). Each weapon's ordnance and explosion are followed, and its spare ammo (`WeaponAmmo`) is kept. Mines, detpacks, turrets, droids, buffs and flamethrowers are skipped and listed. The output is game data from your install: write it to `game/imported_assets/`. Example, the Imperial rifleman's kit:

  ```powershell
  bf2tool export-loadout "<Battlefront>\GameData\Data\_lvl_pc" imp imp_inf_rifleman -o game\imported_assets\loadouts\imp_inf_rifleman.ini
  ```

  Then pass it to the server and clients with `--loadout`. The battlefield `game/battlefields/mos_eisley_outskirts.ini` uses nine kits; export them all with:

  ```powershell
  foreach ($side in "imp", "all") { foreach ($class in "rifleman", "rocketeer", "sniper", "engineer", "officer") {
    bf2tool export-loadout "<Battlefront>\GameData\Data\_lvl_pc" $side "$($side)_inf_$class" -o "game\imported_assets\loadouts\$($side)_inf_$class.ini" } }
  ```

  (The Rebel officer's kit has no supported weapons, so it fails and isn't used. Weapons shared between kits, like pistols and thermal detonators, are one item each: the last kit exported sets their spare ammo.)

- **export-map**: write a Battlefront II map (`<map>.lvl`, e.g. `dag1`) for the game. Out come:
  - its terrain as a heightfield bake (`<map>.r32`/`.json`, the format `swgtool bake-terrain` writes), with its texture layers (`terrain_layers`: texture and tiling), their weights (`<map>.splat0.png`: RGBA = four layers' weights per sample), its baked lighting (`<map>.light.png`), and the sky, fog and lights (`environment`);
  - every object's placement in game space and its model (`<map>.objects.json`, models in `models/` as glTF in game space, textures in `textures/` as PNG). Models come from the map's file, then `ingame.lvl` for shared props like command posts;
  - a game mode's command posts as a map file for battlefields (`<map>_conquest.ini`: starting teams, capture regions and spawn points).

  Other game modes' layers are left out, and so is any `--skip` layer, e.g. the other era's props. Game data from your install: write it to `game/imported_assets/maps/`. Dagobah:

  ```powershell
  bf2tool export-map "<Battlefront>\GameData\Data\_lvl_pc" dag1 -o game\imported_assets\maps\dag1 --skip dag1_gunship
  ```

  Then `swm_server --battlefield game/battlefields/dagobah.ini --bots 12` runs a match on it, and `net_demo.gd -- imported_assets/maps/dag1/dag1 ...` draws it.

  How the map is read (`bf2/level.h`): the terrain chunk (`tern`) gives the grid (cells, meters per cell), the texture layers (`LTEX`, tiling in `SCAL`) and 8x8-cell patches. A patch's vertices carry the heights and baked light; its `INFO` lists the layers it uses (up to three), and a compressed copy of its vertices holds their weights in spare bytes (color alpha, normal w, position w, for the first, second and third listed layer). World layers (`wrld`, also inside the `lvl_` sublevels that hold game modes and era props) give object instances and regions (type, name, a 3x3 rotation and position, size, and hashed properties). `path` chunks are config records grouped by sublevel, and store z the other way around from the world layers; so do the lights (`lght`). `sky_` holds the fog and the sky dome.

  Models (`bf2/model.h`): a `modl` has segments, each with a material (flags: alpha-tested, transparent, double-sided, glowing, ...; up to four textures), Direct3D triangle strips, and vertex buffers in several layouts, plain and compressed (the plain ones are read). Strips keep Direct3D's winding; with x mirrored each triangle is turned over. Skinned props (e.g. recharge droids) are drawn in their bind pose. Textures (`tex_`) store each mip level in one or more Direct3D formats (DXT1/3/5, 32-, 16- and 8-bit); the best one decodable is used.

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
