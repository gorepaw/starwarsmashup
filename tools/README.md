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
swgtool iff <archive.tre|dir> <path> [--full]
swgtool iff-check <dir>
swgtool datatable <dir> <path.iff> [--match <text>]
swgtool extract <dir> <path> <out_file>
swgtool export-character <dir> <out_dir> --name <name> --sat <path.sat>... [--weapon <path.lod>]
                         [--bf2-lvl <ingame.lvl> --bf2-bank <bank> --anim <name>...]
swgtool bake-terrain <dir> <out_prefix> --trn <path.trn> --center <x> <z> --samples <n> --spacing <m>
                     [--snapshot <path.ws>]
swgtool terrain-trace <dir> --trn <path.trn> --at <x> <z>
swgtool export-building <dir> <out_dir> --pob <path.pob> --name <name>
swgtool pob-check <dir>
swgtool snapshot <dir> <path.ws> [--match <text>] [--near <x> <z> <radius>] [--inside <id>]
swgtool export-city <dir> <bake_prefix> --snapshot <path.ws> --center <x> <z> --radius <m> [--hub <hub.ini>]
```

- **list**: print every file in a TRE archive (uncompressed size, stored size, compressed flag, path).
- **manifest**: hash the uncompressed contents of every file in every `.tre` under `<dir>` and write a reference manifest. Refuses to write if any archive or file fails to read.
- **coverage**: compare an install against a reference manifest. For each reference file it reports `matched` (byte-identical at the same path), `moved` (identical, but only under another path), `changed` (path exists, contents differ), or `absent`, plus a per-folder breakdown and a count of install files that aren't in the reference.

- **groups**: count a manifest's unique files per content group (`base`, `jtl`, `rotw`, `tow`), or list one group's paths with `--list`. `coverage` also reports matches per group.
- **iff**: print the chunk tree of one IFF file inside an archive (or, given a directory, in whichever of its archives has it), with a hex preview of each chunk's data, and its text where it's mostly text (template parameters, file names).
- **iff-check**: parse every IFF file in every archive under `<dir>` and print per-extension counts, failures, and root form names. On the Total Experience set, all 130,954 IFF files parse with no failures.

- **export-character**: build a glTF character from Galaxies files in every `.tre` under `<dir>`. The first `.sat` (e.g. a species body) defines the skeleton. Every further `.sat` (armor, clothing) is skinned to it. An optional weapon `.lod` attaches to the `hold_r` joint, with its hardpoints as child nodes (`hp_muzzle`, `hp_scope`, ...). Writes `<name>.gltf`, `<name>.bin`, and one PNG per texture (decoded from DDS) into `<out_dir>`. Write output only to gitignored folders such as `game/imported_assets/`. Example: a human male in stormtrooper armor with an E-11:

  ```powershell
  swgtool export-character E:\swg_te\extracted\Star_Wars_files game\imported_assets\characters\stormtrooper `
    --name stormtrooper --sat appearance/hum_m.sat --sat appearance/armor_stormtrooper_helmet_m.sat ... `
    --weapon appearance/lod/wp_carbine_e11.lod
  ```

  With `--bf2-lvl`, `--bf2-bank`, and one or more `--anim`, Battlefront II animations are retargeted onto the character and written as glTF animations, e.g. `--bf2-lvl <Battlefront>\GameData\Data\_lvl_pc\ingame.lvl --bf2-bank human_0 --anim human_rifle_stand_idle_emote_full --anim human_rifle_stand_runforward`. Clips play in place. `--bf2-bank` can be given more than once (clips are looked for in each; the first gives the skeleton). The client (`game/rendering/soldier.gd`) uses these: from `human_0`, `human_rifle_stand_*` (`idle_emote_full`, `runforward`, `runbackward`, `shoot_full`, `reload_full`, `shoot_secondary_full`, `hitfront`, `death_forward`, `death_backward`) and `human_rifle_crouch_*` (`idle_emote_full`, `walkforward`, `walkbackward`, `shoot_full`, `reload_full`, `hitfront`); from `human_1`, `human_rifle_diveforward` (the roll), `human_rifle_jump`, `human_rifle_fall` and `human_rifle_landsoft`.

  With `--template <object/mobile/...iff>` instead of `--name` and `--sat`, a person by their object template: its appearance (a body) and what its client data file (`.cdf`) says it wears (`WEAR`: clothes, hair, a suit of armor's pieces), named by the template's file.

  With `--swg-anim <path.ans>` (repeatable), Galaxies' own animations are sampled onto the skeleton and written as glTF animations named by their files (`all_b_loc_walk_male`, `all_b_idl_sitting_chair_idle`, ...): both the plain format (`KFAT`) and the compressed one (`CKAT`), whose rotations were worked out from the files (`swg/keyframe_animation.cpp`). `game/tools/pose_clips.gd` screenshots a character's clips.

  Colors: Galaxies' customizable shaders (`CSHD`) tint their texture by palette colors (`TFAC`: a customization variable, a palette, a default index), one (`MAIN`) or, for a "color2" effect, two (`MAIN` and `HUEB`) blended by the texture's alpha. The character's materials keep their textures with the default `MAIN` color as their base color, and `<name>.colors.json` describes each one's colors and palettes for the client to choose (`game/rendering/swg_colors.gd`). A garment's bare skin (an owner proxy shader, `OPST` "body") is drawn with the body's own material. A face is a texture Galaxies composites at runtime (a mesh's `TRTS`: a texture renderer, `.trt`): layers of skin, highlights, brows, makeup, freckles or tattoos, each a texture (a style variable's choice) times a palette's color, copied or blended into its rectangle of the face's texture; the colors file lists them (`"layers"`) and the client composites them as it draws (`game/rendering/swg_face.gdshader`). Transparency is the shader's effect's (hair blends): blended, cut out at alpha 0.5 (a "punchout" pixel program), or added to what's behind (blend factors one and one: glowing panels, screens), which glTF has no mode for, so such a material is BLEND, unlit, with extras `{"blend": "add"}`, and the client draws it so (`game/rendering/swg_add.gdshader`).

  Not handled yet: occlusion zones (hiding body triangles under armor), blend shapes, and LOD levels below the most detailed.

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
- **extract**: write one file out of the archives as stored, for looking into a format. Only to gitignored places.
- **datatable**: print a datatable (`DTII`): its columns with their types, then a row per line, those containing `--match` text only.
- **snapshot**: list a world snapshot's top-level objects (id, position, yaw in degrees, number of objects inside, template), filtered by template substring and/or distance. For example, `--match cantina` finds the Mos Eisley cantina at (3432.35, 5.00, -4818.13), yaw 38.09. A building's object template names its `.pob` (`portalLayoutFilename`). `--inside <id>` lists what's inside an object instead, with the cell it's in: a building holds its cells, a cell its furniture.

- **export-city**: export a city: the objects the world snapshot places within `--radius` of `--center` (buildings, walls, props, terminals), next to the terrain bake it stands on. Each object's template names its look (`appearanceFilename`, or `portalLayoutFilename` for a building), set in it or in a template it derives from. It writes:
  - `<bake_prefix>_city/`: a glTF per model with its textures (a customizable shader's colors baked at their defaults: a base color, or a two-color one's texture as `<material>.hue.png`). Buildings as `export-building` writes them (every cell, its lights and sidecar); the rest as their appearance's most detailed mesh.
  - `<bake_prefix>.objects.json`: each object's model, position and rotation in game space (bf2tool's map objects' format), buildings and the hub's war terminals marked. A building's furniture (what the snapshot puts in its cells) follows it, with the cell whose lights light it; and its doors, one per doorway naming a door style (`datatables/appearance/door_style.iff`: the door's model, how far it slides, how fast, and how near someone must come), placed at the doorway's hardpoint.
  - `<bake_prefix>.holes`: where the buildings' rooms are (every cell's floor but the outside's), on a 25 cm grid: there's no terrain there (`core::TerrainHoles`), as in Galaxies a room has none; the cantina's main room is a meter under the street. Taken away under the walked floors (`.flr`); hidden, drawn away only, where the terrain stands above a drawn floor (the cell's render mesh, facing up: it reaches the walls, where the walked floor stops short) or above part of a cell a floor covers, so it doesn't show along a sunken room's walls.
  - `<bake_prefix>.collision`: what stops characters and shots, in the map collision format `core::World` loads next to a bake (so the hub's server and clients collide with the city with nothing else to load): each model's collision extent (its collision mesh, boxes, cylinders; composite and detail extents), the floors it names (roofs, stairs, a building's every cell), facing up, and walls where a floor can't be left, facing both ways.

  With `--hub`, the hub's war terminals stand in the city too, on the terrain: each `[recruiter]` with a `Template` (an object template) and `Yaw`. And its people (`[npc]` and `[crowd]` templates) are exported to `<bake_prefix>_city/people/`, a character each (as `export-character --template`) with Galaxies' clips `walk` (a woman's walk for a female body), `idle` and `sit`. A creature's clips are named by its skeleton (`bantha_loc_walk.ans`): its walk, standing breathe, and for `sit` its kneel (else its lie: a bantha lies on its side). The log gives a creature's collision radius (its creature template's `collisionRadius`), for the hub file's `[npc]` `Radius`. The creatures it places (a skeletal appearance: rontos, saddled dewbacks) are exported as people are, into `people/`, marked `"creature": true`, each a solid upright prism: its template's collision radius and height, or (a static creature's template gives none) its meshes' bind-pose bounds, half the narrower width and the height. The client draws them breathing (their idle, looped). Its particle effects (`.prt`: PEFT, emitter groups of emitters) are described in `particles/<name>.json` (`swg/particle_export.h`), their textures beside them, and listed `"particles": true`; the client draws each emitter as a `CPUParticles3D` (`game/rendering/swg_particles.gd`). Read from the effects, not documented (ASSUMPTION, `swg/particle_effect.h`): an emitter's 16 waveforms (four floats a point: when, the value, how far it randomly falls below and rises above) in a fixed order, the 7th its radius, 9th spread, 10th rate, 11th speed, 15th lifetime, 16th gravity (below 0: rising); its particle's color ramp, alpha and speed over its life, its quad's size; its texture as a sprite sheet (frames, grid, frames a second). Sound markers (`pt_sound*`, a note in Galaxies' tools) aren't drawn. Mos Eisley, 700 m around its middle (2,663 objects: 900 of them furniture, 140 doors; 314 models, 93,886 collision triangles, in 10 seconds):

  ```powershell
  swgtool export-city E:\swg_te\extracted\Star_Wars_files game\imported_assets\terrain\tatooine_mos_eisley `
    --snapshot snapshot/tatooine.ws --center 3500 -4800 --radius 700 --hub game\hubs\mos_eisley.ini
  ```

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
                   [--death <region>]... [--open <object>]... [--team <post>=<n>]... [--disable <barrier>]...
                   [--drop-boundary <path>]... [--set <object>.<property>=<value>]...
bf2tool export-hud <_lvl_pc dir> -o <out dir>
bf2tool export-effects <_lvl_pc dir> -o <out dir>
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

  The Clone Wars battlefields (`game/battlefields/geonosis.ini`) use the Republic's and the Separatists' kits:

  ```powershell
  foreach ($class in "ep2_rifleman", "ep2_rocketeer", "ep2_sniper", "ep2_engineer", "ep3_officer", "ep2_jettrooper") {
    bf2tool export-loadout "<Battlefront>\GameData\Data\_lvl_pc" rep "rep_inf_$class" -o "game\imported_assets\loadouts\rep_inf_$class.ini" }
  foreach ($class in "rifleman", "rocketeer", "sniper", "engineer", "officer") {
    bf2tool export-loadout "<Battlefront>\GameData\Data\_lvl_pc" cis "cis_inf_$class" -o "game\imported_assets\loadouts\cis_inf_$class.ini" }
  ```

- **export-map**: write a Battlefront II map (`<map>.lvl`, e.g. `dag1`) for the game. Out come:
  - its terrain as a heightfield bake (`<map>.r32`/`.json`, the format `swgtool bake-terrain` writes), with its texture layers (`terrain_layers`: texture and tiling), their weights (`<map>.splat0.png`: RGBA = four layers' weights per sample), its baked lighting (`<map>.light.png`), the sky (`sky_domes`: the dome and the models drawn with it, in order, each with its Offset, MovementScale and RotationSpeed), fog (`fog`: whether the sky gives a fog color; Utapau's doesn't, and has none), fog cloud and lights (`environment`: the sun, the second global light where the map names one, lamps), and its water (`water`: each layer's height and its surface as rectangles in game space, with the look: textures, animated normal maps, colors, fresnel);
  - every object's placement in game space and its model (`<map>.objects.json`, models in `models/` as glTF in game space, textures in `textures/` as PNG). Models come from the map's file, then `ingame.lvl` for shared props like command posts;
  - every object's collision, placed, as one triangle file (`<map>.collision`, `core::MapCollision`), each triangle marked for what it stops: characters, shots or both. The server and clients load it next to the bake;
  - a game mode's command posts as a map file for battlefields (`<map>_conquest.ini`: starting teams, capture regions, spawn points and the post object's place, `Console = `), the map's damage regions (`DamageRegion = <health a second>` and a shape: Battlefront's "damageregion" class, damagerate times personscale; Mustafar's lava), with the map's AI barriers that stop soldiers (`Barrier = `, four corners), which bots avoid where they can.

  A map without terrain (aboard Tantive IV: its `tern` chunk is just a name) gets a flat stand-in ground 60 m under its lowest object, square around everything, marked `hidden` in the json (the client doesn't draw it), with a death region over it in the map file for whoever falls off. Its water has nothing under it (Kamino's sea): soldiers die 2 m under the surface (a death region down to the stand-in), as they drown in Battlefront; a pool soldiers stand in (Jabba's rancor pit's) is shallower than that.

  Other game modes' layers are left out, and so is any `--skip` layer, e.g. the other era's props. What the mode's mission script changes comes as options, read off the script (compiled Lua 5.0 in `mission.lvl`, its numbers 4-byte floats): `--death <region>` for each `AddDeathRegion`, `--open <object>` for each `KillObject` (left out, and a command post with it; also objects fought open, Kashyyyk's barricades), `--team <post>=<n>` for each `SetObjectTeam` and `--disable <barrier>` for each `DisableBarriers`. `--drop-boundary <path>` leaves a boundary path out: the Death Star's cuts through the station's south end, where two of its posts are; sealed, with its falls death regions, it needs none. `--set <object>.<property>=<value>` for each `SetProperty` on an object: Kamino's script locks some of its pod rooms' doors (`IsLocked`; doors, otherwise left open until they move, are kept shut), and names CP2's spawn path and capture region. Hoth's conquest, for one, hands CP3 and CP6 to the Empire (on Hoth, team 1), removes CP7 and turns off the walkers' and bombers' barriers; `game/battlefields/hoth.ini` has its command line. Game data from your install: write it to `game/imported_assets/maps/`. Dagobah:

  ```powershell
  bf2tool export-map "<Battlefront>\GameData\Data\_lvl_pc" dag1 -o game\imported_assets\maps\dag1 --skip dag1_gunship
  ```

  Then `swm_server --battlefield game/battlefields/dagobah.ini --bots 12` runs a match on it, and `net_demo.gd -- imported_assets/maps/dag1/dag1 ...` draws it.

  How the map is read (`bf2/level.h`): the terrain chunk (`tern`) gives the grid (cells, meters per cell), the texture layers (`LTEX`, tiling in `SCAL`) and 8x8-cell patches, row-major from the -x,-z corner. The grid is centred on the origin in x but lies one cell further along +z: placed so, the maps' path nodes stand on it (on Endor 187 of 219 within 10 cm, against 85 centred; the same one-cell shift on every map with terrain) and their posts, rocks and barriers sit on it. A patch's vertices carry the heights and baked light; its `INFO` lists the layers it uses (up to three), and a compressed copy of its vertices holds their weights in spare bytes (color alpha, normal w, position w, for the first, second and third listed layer). World layers (`wrld`, also inside the `lvl_` sublevels that hold game modes and era props) give object instances and regions (type, name, a 3x3 rotation and position, size, and hashed properties). `path` chunks are config records grouped by sublevel, and store z the other way around from the world layers; so do the lights (`lght`). `sky_` holds the fog and the sky dome.

  Water: the terrain chunk's `WATR` is a grid of square cells centred on the map, which may reach past the terrain: `INFO` (cells per side, an unknown 2, the cell size, half the grid's extent along x and z, an unknown 1), a `LAYR` per water layer (name, an unknown 10, height, U and V velocity, U and V repeat, RGBA color) and `WMAP` (4 bits per cell, two cells a byte, the first in the high nibble; rows along +z from the -x,-z corner). Dagobah's is 128 cells of 16 m. The grid's size, centring and nibble order were settled by matching Dagobah's wet cells to its swamp pools; heights agree with other maps' (Yavin's river at -72.5 m over terrain from -74.5). The map's world effects are `fx__` config records, `Effect("Water")` (main texture, animated normal map frames, refraction and reflection colors, fresnel) and `Effect("FogCloud")` (sprite texture, a ring's range, color, drift, height, size, count) among them.

  Models (`bf2/model.h`): a `modl` has segments, each with a material (flags: alpha-tested, transparent, double-sided, glowing, ...; up to four textures), Direct3D triangle strips, and vertex buffers in several layouts, plain and compressed (the plain ones are read). Strips keep Direct3D's winding; with x mirrored each triangle is turned over. Skinned props (e.g. recharge droids) are drawn in their bind pose.

  Transforms (`XFRM`, in world layers, skeletons and primitives) store a 3x3 rotation as its three columns (the local axes), then a position. That was settled by fitting collision boxes to their models, the crashed X-wing to the ground, and the cave to the terrain's cut holes.

  Collision: a `coll` is a model's collision mesh, as vertices (`POSI`) and an AABB tree (`TREE`) whose leaves are convex polygons of vertex indices. A `prim` lists a model's primitives (box: half extents; cylinder: radius and full height along local y; each on a bone). A primitive's name flags what it stops: `p_-so-trunk` (or `p_so-collision`, Geonosis's hangar) stops soldiers and ordnance, and a name without flags (`p_cube`, `p_vehicle`) stops everything. ASSUMPTION: collision meshes stop both. Unless the object's class says otherwise: `SoldierCollision` and `OrdnanceCollision` list what each meets (`CollisionMesh`, primitives' names, or `none`), `VehicleCollision` likewise for vehicles. Naboo's buildings list their boxes for vehicles and the mesh for soldiers and ordnance (CP1's spawn points are on a floor inside one's boxes); the invisible walls (`com_inv_col_*`) stop no ordnance. Doors (class `door`, Polis Massa's) open as soldiers come; until they move in the game, they're exported without collision, open. Textures (`tex_`) store each mip level in one or more Direct3D formats (DXT1/3/5, 32-, 16- and 8-bit); the best one decodable is used.

  AI data: a world layer's `BARR` chunks are barriers (`INFO`: `NAME`, `XFRM`, `SIZE` with half extents along local x and z, `FLAG` with the classes kept out: 1 soldiers, 2 hover, 4 small, 8 medium, 16 huge, 32 flyers). The `plan` chunk is the AI's hub graph: `INFO` (hub count, connection count, class count), `NODE` (per hub: a 16-byte name, position, radius, 8 connection slots, then per class a count of tables of one byte per hub) and `ARCS` (per connection: a 16-byte name, from and to hubs, class flags, 4 more bytes). Only barriers are exported; bots plan on their own grid.

- **export-hud**: write Battlefront II's HUD for the game's UI (`game/ui/bf2_hud.gd` draws it). Out comes `hud.json`: its layouts (`layouts`: `hudtransforms`, `1playerhud` ... `4playerhud`, each a tree of elements and properties), its fonts (`fonts`: glyph metrics, pages as PNG), its English strings (`strings`, by key hash) and the textures and models the layouts name (`textures/` as PNG, `models/` as glTF), plus `ingame.lvl`'s `powerupitem` models (dropped health, ammo, dual and energy canisters) and every texture in `common.lvl` (the menus' buttons, borders and icons). Game data from your install: write it to `game/imported_assets/hud/`:

  ```powershell
  bf2tool export-hud "<Battlefront>\GameData\Data\_lvl_pc" -o game\imported_assets\hud
  ```

  How the HUD is read (`bf2/hud.h`): a `hud_` chunk in `ingame.lvl` is a tree of config records, as world effects are: keywords are hashes of their names, recovered by hashing candidate words (`Group`, `BarBitmap`, `BitmapRect`, `TextFont`, `FadeInTime`, ...; a few remain unknown). A record's values are floats or strings, a string's value being its offset to a block of NUL-terminated strings after the values: `Rect(0.088, 0.111, "Center", "Center", "Viewport")`. Fonts (`font` in `core.lvl`): `HEAD` gives the glyph count and line height, `FBOD` 24 bytes a glyph (character, page, advance, x offset, width, top and bottom rows, then u0 u1 v0 v1), `FTEX` the pages. Strings (`Locl`, one per language): entries of a key hash (FNV-1a of the dotted key, e.g. `common.victoryin`), a size and UTF-16 text.

  `export-map` also writes the health and ammo droids (class "powerupstation") into the map file (`Powerup = x y z radius delay health ammo`, from PowerupDelay, SoldierHealth, SoldierAmmo and Radius, 3 m where the class sets none) and with their objects (`powerup` in `<map>.objects.json`: also IdleRotateSpeed, IdleWaitTime, IdleWobbleFactor and the map icon), for drawing.

  `export-map` also writes each command post's hologram with its object (`hologram` in `<map>.objects.json`): the insignia models per side (`HoloImageGeometry`), and the hologram class's (`HoloOdf`) height, size, turn rate, light radius and friendly, enemy and neutral colors, plus the post's map icon (`MapTexture`). Property names were recovered by hashing candidates, as the HUD's were.

  `export-map` also writes the map's minimap image (`minimap` in its json: `<map>_map`, or named for the map's world layers: Yavin's `yavin1_map`). Where it lies isn't stored with it; a battlefield file says (`Minimap`). Dagobah's was fitted to its playable outline (the `hell_bush` path), which the image's light area matches.

- **export-effects**: export Battlefront II's weapon, ordnance and explosion effects (`fx__` chunks in `ingame.lvl` and `side/*.lvl`) as `<name>.json` for `game/rendering/bf2_effects.gd`, along with their particle and laser textures (`textures/` as PNG) and shared ordnance and muzzle-flash models (`models/`: `com_weap_missile`, `com_weap_inf_grenadethermal_ord`, `com_sfx_muzzleflash` as glTF). Effect chunks identify themselves by a 4-byte `NAME` hash using FNV-1a over `c | 0x20` (`bf2/effect.h`). Game data from your install: write it to `game/imported_assets/effects/`:

  ```powershell
  bf2tool export-effects "<Battlefront>\GameData\Data\_lvl_pc" -o game\imported_assets\effects
  ```

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
