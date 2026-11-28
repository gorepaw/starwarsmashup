# Distribution

How players get the game: a launcher that finds their Battlefront II and Galaxies installs (with their permission), extracts the assets the game needs, keeps everything up to date from Cloudflare R2, and starts the game. Plus a **portable build** with the assets already extracted, for the developer's own testing on other machines.

The backend (accounts, website, hosted servers) is sketched in [Backend](#backend-later), to be planned in detail once distribution works.

## Two builds

| | **Player build** | **Portable build** |
|---|---|---|
| Who | Anyone | The developer only |
| Contains | Launcher, game, extractor tools, extraction recipe | All of that **plus** `imported_assets` |
| Assets | Extracted on the player's machine from their own installs | Bundled (from the developer's own extraction) |
| Update channel | `stable` (public) | `portable` (private, needs a personal token) |
| First run | Find installs → extract (~10 min, ~5 GB) → play | Copy `launcher.exe` + token file to a machine → it downloads everything → play |

The portable build contains game assets, so it is **never** public: it lives in a private R2 bucket behind a Worker that checks a token only the developer holds. Testers use the player build. The README's promise ("no game assets are included... never will") stays true for everything the project distributes.

## Pieces

```
launcher.exe ──finds──▶ BF2 _lvl_pc, SWG archives/discs
     │                        │
     │ runs recipe.json ──▶ bf2tool.exe / swgtool.exe ──▶ game/imported_assets/
     │
     ├──checks──▶ R2: channels/<channel>/latest.json (+ .sig) ──▶ blobs/<sha256>
     │
     └──starts──▶ game/SWBFO.exe
```

### Install layout

```
<install dir>/                    default %LOCALAPPDATA%\SWBFO (no admin; player can pick another drive)
  launcher.exe
  launcher.cfg                    channel, update URL; portable.flag / portable.token in the portable build
  game/                           the project as loose files, like the repo's game/: project.godot, scripts,
                                  scenes, data INIs, .godot/ import cache, bin/ and Terrain3D's Release dlls
    SWBFO.exe            the custom double-precision Godot release template
    imported_assets/              extracted (player build) or downloaded (portable build)
  tools/  bf2tool.exe  swgtool.exe  recipe.json  swg-total-experience-2005.tsv
  state/                          extraction state, verified-source cache, logs, update staging
```

The launcher starts `game\SWBFO.exe`. Release templates ignore `--path` (they're built without path overrides) and always start in their own folder, so the exe sits next to `project.godot`. The install dir mirrors the repo root, so the recipe's `game\...` paths work as they are when it runs from there. Updates only touch files a manifest lists, so `imported_assets/` and the local `world.sqlite` survive them (the database moves to `user://` later).

## Milestones

### 1. An exported game that runs outside the editor

The game today only runs from the editor. A normal `.pck` export doesn't fit it: the C++ core reads the game's INIs by OS path (and they point at `../imported_assets/...`), and about 100 GDScript call sites turn `res://` paths into OS paths with `ProjectSettings.globalize_path`, which an exported pack can't serve.

Instead the game ships as a **loose project**. When a Godot release template finds no `.pck`, it loads `project.godot` from its own folder and maps `res://` to that real folder (`ProjectSettings::_setup`). Every existing path then works with no game code changed.

- **Release template.** Build `template_release` with `precision=double` from the same Godot 4.7.2 source as the editor (`E:\godot`, scons), and Terrain3D's `template_release`.
- **Release extension.** Configure a second CMake build dir with `-DGODOTCPP_TARGET=template_release`. It writes `game/bin/swbfo.windows.template_release.double.x86_64.dll`, which `swbfo.gdextension` already lists.
- **`tools/release/stage.py`**: runs the editor's headless import (so `.godot/imported` is current), then copies the install layout into a staging folder: `game/` without `imported_assets/`, `tests/`, `*.sqlite*`, the editor's caches and the Debug dlls, the template as `game/SWBFO.exe`, plus `tools/`. With `--assets` it also copies `imported_assets/`.
- Done when the staged folder runs from a path with no repo nearby.

### 2. The extraction recipe

End users have no Python, and the export logic must stay in one place. So `rebuild_imported_assets.py` gains `--emit-recipe <file>`, which writes every step it would run as JSON: argv with `{bf2}`, `{swg}`, `{out}`, `{scratch}` and `{tools}` placeholders, the step's phase and label, and its follow-ups (the mode-layer copies). The launcher runs that file, and the dev script stays the source of truth.

- **Space maps** need the Lua decoder (`space_assault.py` → `bf2_lua.py`) to work out their rules. Battlefront's data is identical across Steam, GOG and retail, so the results are recorded when the recipe is emitted. The space-vehicle steps, read off the space maps' output, are recorded the same way.
- **SWG audio** (`import_swg_audio.ps1`) becomes recipe steps: one `swgtool extract-list` step reading `game/audio/swg_audio_files.txt`.
- **The check phase** becomes a list of required paths in the recipe. The launcher reports missing ones by name.
- **Each step gets a key**: a hash of its argv template plus the version of the tool it runs. The launcher records the keys of the steps it has finished. After an update it reruns only new or changed steps, so a new map costs one export, not 10 minutes.

### 3. Verified SWG sources (manifest filtering)

[Supported game versions](ARCHITECTURE.md#supported-game-versions) promises that only files byte-identical to SOE's are used. The developer's Total Experience folder *is* the baseline, so today nothing enforces it. Players' folders mix SOE files with others', so:

- `swgtool` gets global `--manifest <tsv>` and multiple `--source <dir>` options. Its archive set then resolves a path only to an entry whose uncompressed SHA-256 matches the manifest. Other entries don't exist as far as exports are concerned. When several matching copies exist, any of them is right, since they're identical.
- Hashing every entry takes minutes, so results are cached in `state/` per archive (path, size, mtime).
- `swgtool verify --manifest <tsv> --source <dir>...` prints the coverage the launcher shows, per content group: `base 125,416/125,416 ✓  jtl 11,937/11,937 ✓  rotw 2,750/2,750 ✓`. If core (`base`) files are missing, extraction stops and lists them. A missing expansion only switches that content off.

### 4. Finding installs

The launcher never searches without asking. The first screen explains what it looks for and that nothing leaves the machine, then offers:

1. **Quick search** (default): known locations only, in seconds.
2. **Full search**: every fixed and optical drive, skipping `Windows`, `$Recycle.Bin`, `ProgramData\Microsoft` and the like. It can be cancelled and shows progress.
3. **Choose folders myself.**

| Looks for | Quick search | Recognised by |
|---|---|---|
| BF2 (Steam) | `HKCU\Software\Valve\Steam\SteamPath` → `steamapps\libraryfolders.vdf` → each library's `common\Star Wars Battlefront II Classic\GameData\Data\_lvl_pc` | `_lvl_pc\ingame.lvl` + `mission.lvl`, hashes of key `.lvl` files matching the 2005 data |
| BF2 (GOG, retail) | GOG Galaxy's and LucasArts' registry install paths, `Program Files (x86)` defaults | as above |
| BF2 Classic Collection (2024) | Steam library | detected and reported as unsupported (see Architecture) |
| SWG installs and folders | old SOE install paths and registry key, common SWGEmu/Legends folders, `Program Files (x86)` | `*.tre` with a `TRE` `0005` header; checked against the manifest (milestone 3) |
| Total Experience discs | optical drives and mounted ISOs; any folder holding `data1.hdr` + `data1.cab` | the InstallShield set's `Star Wars files` group |
| SWG Restoration | its install path | `SwgRestoration_*.tre` (TRE `0006`): reported as unusable (encrypted), with a pointer to the other sources |

All matching SWG sources are combined: an emulator folder plus discs fill each other's gaps. **Discs and cabs** are read with [unshield](https://github.com/twogood/unshield) (MIT, vendored into `third_party/`). The launcher extracts the TRE archives to `state/swg_cache/` once. Discs that span volumes prompt for the next CD.

### 5. Extractor core and CLI

`tools/extract/` (C++ library) holds discovery, verification, the unshield step and the recipe runner (child processes, a log per step, progress from step counts, cancel and resume). `swbfo_extract.exe` is a command line over it, so the whole flow is scriptable and testable without the UI:

```
swbfo_extract find [--full]
swbfo_extract classify <dir>
swbfo_extract cabs <cab_dir> <out_dir>
swbfo_extract verify --tools <dir> --swg <dir>...
swbfo_extract run --install <dir> --tools <dir> --bf2 <_lvl_pc> --swg <dir>...
```

Measured on the developer's machine: the quick search takes under a second. Reading the Total Experience cabinets takes 31 s. Verifying all 140,103 reference files takes 17 s (100% matched). A full extraction into an empty player build, from Steam's Battlefront II and the cabinets read through the manifest filter, ran all 196 steps in 15 minutes with no failures and no missing paths. It produced the same 33,027 files as the developer's `imported_assets`: 32,959 byte-identical, and 34 characters newer (the dev copies predated an exporter change; re-exports are deterministic).

### 6. The launcher

`launcher/` is a Win32 + Direct3D 11 + [Dear ImGui](https://github.com/ocornut/imgui) (MIT) app over the extractor core, about 1.3 MB, needing no admin rights. Its look is plain and neutral: no Battlefront art exists on disk until the first extraction.

Screens: **Updating** → **Welcome / permission** (look in the usual places, search every drive, or choose folders) → **Found installs** (one Battlefront II, any Galaxies sources, each with a note; unusable ones shown greyed with the reason; add a folder) → **Checking the files** (discs read out to `state/swg_discs/` and kept for later updates; coverage per content group) → **Extract** (progress, current step, log, pause; resumes) → **Ready** (Play, change game folders, open the logs). The chosen folders are saved in `state/setup.json`, so on later starts an update whose recipe has new or changed steps is extracted without asking again.

### 7. Updates on Cloudflare R2

Everything is published as **content-addressed blobs** plus a **signed manifest** per channel.

```
blobs/<sha256[0:2]>/<sha256>               any file, stored as is, immutable
channels/<channel>/latest.json             { format, channel, version, min_launcher, files: [{ path, sha256, size }] }
channels/<channel>/latest.json.sig         Ed25519 signature of latest.json (hex)
channels/<channel>/<version>.json(.sig)    kept for rollback
```

- **One Worker, two buckets** ([`tools/release/worker`](../tools/release/worker)). `swm-public` holds `stable` (no game assets): `GET /public/<key>`, open to anyone. `swm-private` holds `portable`: `GET /private/<key>` only with the `PORTABLE_TOKEN` secret as a bearer token. Uploads (`PUT`) and blob listings need the `PUBLISH_TOKEN` secret. Only `channels/...` and `blobs/...` keys exist; tokens are compared in constant time. The Worker runs on `workers.dev` for now, and `launcher.cfg`'s `url` names it, so moving to a real domain later is a config change.
- **Signing.** The launcher embeds an Ed25519 public key ([`launcher/release_key.h`](../launcher/release_key.h), [Monocypher](https://monocypher.org), CC0/BSD) and refuses any manifest that doesn't verify, so a compromised bucket, Worker or token can't push code. `swbfo_sign keygen` made the key; the secret is in `%USERPROFILE%\.swm\release_ed25519.key`, outside the repo. **Back it up**: losing it means shipping a new launcher by hand to every player.
- **Downloads** use WinHTTP, eight at a time. Hashing uses BCrypt's SHA-256. A file is downloaded only if its hash differs from what's installed (`state/installed.json`, the last applied manifest). Downloads go to `state/staging/`, are verified, then copied into place; files the new manifest drops are deleted. Files no manifest ever listed (extracted assets, saves) are never touched.
- **Launcher self-update.** The running exe renames itself to `launcher.old.exe` (Windows allows renaming a running exe), the new file moves into place, and the new launcher starts and carries on. `min_launcher` makes an older launcher stop instead of installing what it can't handle.
- **Publishing**: `tools/release/publish.py --stage <dir> --channel stable|portable --version <v>`. It hashes the staged files, asks the Worker which blobs exist, uploads the missing ones, signs the manifest with `swbfo_sign`, and uploads it last (the launcher retries once if it catches the manifest and signature mid-change). `portable` adds `game/imported_assets/`. It also writes the channel's bootstrap zip: `launcher.exe` + `launcher.cfg` (+ `portable.flag`).
- **Testing without Cloudflare**: [`tools/release/local_server.py`](../tools/release/local_server.py) serves the same routes from a folder.
- **Request budget.** Every file is one request. A player install is about 560, but a portable install is about 33,600, against the Workers free plan's 100,000 requests a day (uploads count too). If that starts to bind, bundle small asset files per folder, or move to Workers Paid ($5 a month).
- **Code signing** (later): unsigned exes get a SmartScreen "Windows protected your PC" prompt. Azure Trusted Signing (about $10 a month) removes it, once there are players.

#### Releasing

```powershell
cmake --build E:\swm_build\release --config Release --target swbfo_godot bf2tool swgtool swbfo_extract launcher swbfo_sign
python -I tools\release\stage.py --out E:\swm_stage\player              # + --assets for portable
$env:SWBFO_PUBLISH_TOKEN = "<publish token>"; $env:SWBFO_UPDATE_URL = "https://swm-updates.<account>.workers.dev"
python -I tools\release\publish.py --stage E:\swm_stage\player --channel stable --version 2026.11.28.1
```

`E:\swm_build\release` is a CMake build dir configured with `-DGODOTCPP_TARGET=template_release`. One-time Cloudflare setup: `npx wrangler login`, `npx wrangler r2 bucket create swm-public` and `swm-private`, then in `tools/release/worker`: `npx wrangler secret put PUBLISH_TOKEN`, `npx wrangler secret put PORTABLE_TOKEN`, `npx wrangler deploy`.

Deployed 2026-11-28 (tokens in `%USERPROFILE%\.swm\`, beside the signing key). First releases, both `2026.11.28.1`: `stable` (558 files, 117 MB) and `portable` (33,585 files, 5.3 GB in 15,008 unique blobs). Bootstrapping each from its zip into an empty folder over the internet installed every file with a matching hash; the portable install took 9.5 minutes.

### 8. The portable build

The same launcher with `portable.flag` and `portable.token` next to it. It skips discovery and extraction, follows the `portable` channel, and installs into its own folder (a USB drive works). To set up a new machine, unzip `SWBFO-portable.zip`, put `portable.token` (a text file holding the token) next to `launcher.exe`, and run it.

### Order

1. Exported game as a loose project (unblocks everything else)
2. Recipe emitter
3. `swgtool --manifest/--source`, `verify`
4. Discovery + unshield + extractor core + `swbfo_extract` CLI
5. Launcher UI
6. R2 buckets, Worker, signing keys, `publish.py`, launcher updater
7. Portable channel
8. Backend planning (below)

Each milestone keeps the dev workflow working: the editor still reads `game/imported_assets` and `rebuild_imported_assets.py` still runs as before.

## Backend

Accounts, the website and hosted game servers: see [Backend](BACKEND.md).
