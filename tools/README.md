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

  Not handled yet: occlusion zones (hiding body triangles under armor), customization (skin palettes, blend shapes), and LOD levels below the most detailed.

Supported archives: TRE `0005` (retail discs) and plain `0006`. SWG Restoration's `0006` archives encrypt their file data and are reported as unsupported. We don't decrypt them.

## bf2tool

```
bf2tool tree <file.lvl> [--depth <n>]
bf2tool list <file.lvl> <tag>
bf2tool check <dir>
```

Battlefront II munged files (`.lvl`) are `ucfb` chunk trees: `lvl_` sub-levels holding models (`modl`), textures (`tex_`), skeletons (`skel`), animations (`zaa_`/`zaf_`), entity classes (`entc`/`ordc`/`wpnc`/`expc`), and more.

- **tree**: print the chunk tree with sizes and `NAME`s, to `--depth` levels (default 3).
- **list**: print the size and name of every chunk with a given tag, e.g. `bf2tool list side\rep.lvl modl`.
- **check**: parse every `.lvl` under `<dir>` and count content chunk types. On the Steam install, all 191 files (4.7 GB) parse with no failures.

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
