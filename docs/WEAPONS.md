# Weapons

Every weapon model in Galaxies, sorted into our seven categories, and how we turn them into items. Gear rules (Mk tiers, quality, the 2x curve, ammo effects) are in [Design](DESIGN.md#weapons-and-loadouts); weapon professions are in [Professions](PROFESSIONS.md).

## The categories

| Category | Slot | Profession | Archetypes (handling from Battlefront II) |
|---|---|---|---|
| Sniper | Primary | Rifles | `sniper` |
| Rifle | Primary | Rifles | `rifle_auto`, `beam` |
| Carbine / shotgun | Primary | Carbines | `carbine_burst`, `scatter` |
| Heavy | Primary | Heavy Weapons | `heavy_launcher`, `beam_cannon`, `flamer` |
| Sidearm | Sidearm | Pistols | `pistol`, `scatter_pistol` |
| Melee | Sidearm if one-handed, primary if two-handed | Melee | `melee` (one-handed), `melee_2h` (two-handed) |
| Tactical | Secondary | Heavy Weapons | `grenade` (new: `placed` for mines and traps) |

Melee goes by size, the same way shotguns do: knives, batons, one-handed swords and knucklers are sidearms; two-handed swords, axes and polearms are primaries.

## The models

Galaxies has 409 weapon templates but only 166 models: variants (quest, Corsec, "noob", Lithitanium) reuse a base model. Leaving out creature attacks, turrets, droids and lightsabers, **133 models** remain. ✅ marks the ones already in our catalog.

Model names are the files under `appearance/` in the Galaxies archives. Where a gun has a `_combined` model, that is the complete gun with its scope and stock, and it's the one Galaxies itself uses.

### Sniper (6)

| Model | Galaxies names | Notes |
|---|---|---|
| `wp_rifle_dlt20` ✅ | DLT-20, DLT-20A, CDEF Rifle | In game as the DLT-20A Longblaster |
| `wp_rifle_tusken_s01` | Tusken Rifle, Lithitanium Rifle | Slug cycler rifle (ballistic) |
| `wp_rifle_adventurer` | Explosive Shell Sniper Rifle | Explosive rounds: small blast on hit |
| `wp_rifle_ld1_combined` | LD-1 Rifle | Scoped |
| `wp_rifle_trando_hunter` | Trandoshan Hunting Rifle | |
| `wp_carbine_dxr6_combined` | Tenloss DXR-6 Disruptor Rifle, DXR-6 Carbine | Disruptor: one heavy shot, long cooldown. Galaxies gates it behind counter-sniping |

### Rifle (14)

| Model | Galaxies names | Notes |
|---|---|---|
| `wp_rifle_t21_combined` ✅ | T-21 | Heavy repeating rifle (it carries a run-speed penalty) |
| `wp_rifle_laser` ✅ | Light Laser Rifle, Laser Rifle | |
| `wp_rifle_laser_combined` | Advanced Laser Rifle | Scoped version |
| `wp_rifle_bowcaster` ✅ | Light Bowcaster, Recon Bowcaster | Battlefront II has a bowcaster base (`wok_weap_inf_bowcaster`) |
| `wp_rifle_bowcaster_combined` | Assault Bowcaster | |
| `wp_rifle_dc15` | DC-15 Rifle | Clone Wars |
| `wp_rifle_berserker` | Berserker Rifle | Slugthrower (ballistic) |
| `wp_rifle_proton` | Proton Rifle | |
| `wp_rifle_jawa_ion` | Jawa Ion Rifle | Ion damage; strong against droids and vehicles |
| `wp_rifle_massassi_ink` | Massassi Ink Rifle | |
| `wp_rifle_ewok_crossbow` | Ewok Crossbow | Slow, heavy bolt |
| `wp_rifle_lightning_cannon` | Lightning Rifle, Light Lightning Cannon | `beam`: electro |
| `wp_rifle_beam_rifle` | Beam Rifle, Heavy Acid Rifle | `beam`: bio (acid) |
| `wp_pistol_spraystick` | Spraystick | Built as a shotgun (`scatter`): a short poison spray |

The E-11 rifle has no model of its own: Galaxies uses the carbine's model for it. Our E-11 keeps Battlefront's rifle handling (`rifle_auto`), as the soldier's standard weapon.

### Carbine / shotgun (20)

| Model | Galaxies names | Notes |
|---|---|---|
| `wp_carbine_e11` ✅ | E-11 Carbine, Enhanced E-11, Lithitanium Carbine | Our E-11 |
| `wp_carbine_e11_combined` | E-11 Mark II | Scoped and stocked |
| `wp_carbine_dh17` ✅ | DH-17 Carbine, CDEF Carbine | |
| `wp_carbine_dh17_black` | DH-17 Carbine (black) | A recolor; could be a faction or Mk look instead |
| `wp_carbine_dh17_snubnose` | DH-17 Short Carbine | |
| `wp_carbine_ee3_combined` ✅ | EE-3 | Burst |
| `wp_carbine_laser_combined` ✅ | Laser Carbine | |
| `wp_carbine_elite_combined` | Elite Carbine | |
| `wp_carbine_dc15` | DC-15 Carbine | Clone Wars |
| `wp_carbine_e5` | E-5 Carbine | Clone Wars droid blaster |
| `wp_carbine_geonosian` | Geonosian Carbine | |
| `wp_carbine_proton` | Proton Carbine | |
| `wp_carbine_wookiee_s01` | Wookiee Bowcaster Carbine | |
| `wp_carbine_alliance_needler` | Alliance Needler | |
| `wp_carbine_czerka_dart` | Czerka Dart Carbine | Darts: a natural poison or ammo-effect gun |
| `wp_carbine_bothan_bola` | Bothan Bola Carbine | Snares: slows instead of damage |
| `wp_carbine_slug_carbine_s01` | Nym's Slugthrower | **Shotgun**: ballistic slugs |
| `wp_carbine_underslung` | Underslung Carbine, Rotary Underslung | **Shotgun** or burst |
| `wp_rifle_sg82` ✅ | SG-82 | **Shotgun**, already our Sonic Scattergun (Galaxies calls it a sonic rifle) |
| `wp_rifle_geonosian_drill` | Geonosian Drill | A mining tool; a short-range shotgun-like oddity |

### Heavy (6)

| Model | Galaxies names | Notes |
|---|---|---|
| `wp_hvy_launcher` ✅ | Rocket Launcher | Our Plex launcher |
| `wp_hvy_beam_cannon` | Heavy Particle Beam Cannon | `beam`: pierces in a line |
| `wp_hvy_stun_cannon` | Lightning Beam Cannon | `beam`: electro, chains to nearby targets |
| `wp_hvy_acid_launcher` | Acid Stream Launcher | `flamer`-style stream, bio |
| `wp_rifle_flamer` | Flame Thrower, Plasma Flame Thrower | `flamer`: short cone, sets burning |
| `wp_pistol_launcher_pistol` | Launcher Pistol (three tiers) | Small homing rockets: a light heavy weapon, or a tactical item |

### Sidearm (30)

| Model | Galaxies names | Notes |
|---|---|---|
| `wp_pistol_dl44_combined` ✅ | DL-44 | |
| `wp_pistol_d18_combined` ✅ | D-18 | Also stands in for our DH-17 and ion pistols (wrong models) |
| `wp_pistol_scout_combined` ✅ | Scout Blaster | |
| `wp_pistol_scout` | Corsec Scout Blaster | |
| `wp_carbine_dh17_combined` | DH-17 Pistol | The real DH-17 pistol model, despite the file name |
| `wp_pistol_dl44_metal` | DL-44 Metal | |
| `wp_pistol_dl44_metal_combined` | DL-44 XT | |
| `wp_pistol_cdef` | CDEF Pistol | Galaxies' starter pistol |
| `wp_pistol_de10_combined` | DE-10 | |
| `wp_pistol_deathhammer_combined` | Death-Hammer | |
| `wp_pistol_dx2` | DX-2 | |
| `wp_pistol_fwg5` | FWG-5 | |
| `wp_pistol_intimidator` | Intimidator | |
| `wp_pistol_kyd21` | KYD-21 | |
| `wp_pistol_power5` | Power5 | |
| `wp_pistol_renegade_combined` | Renegade | |
| `wp_pistol_republic_blaster` | Republic Blaster | Naboo security |
| `wp_pistol_srcombat_combined` | SR Combat Pistol | |
| `wp_pistol_striker` | Striker | |
| `wp_pistol_alliance_disruptor` | Alliance Disruptor | Disruptor |
| `wp_pistol_ionstunner` | Ion Stunner | **Ion**: the right model for our ion pistol |
| `wp_pistol_jawa` | Jawa Ion Pistol | Ion |
| `wp_pistol_geonosian` | Geonosian Sonic Blaster | Sonic burst; Battlefront II has a sonic blaster base |
| `wp_pistol_scatter_pistol` | Scatter Pistol | **Scatter** at sidearm size |
| `wp_pistol_flechette` | Flechette Pistol | Bleed |
| `wp_pistol_flare` | Flare Pistol | Utility: a flare or marker |
| `wp_pistol_tangle` | Tangle Pistol | Utility: snare |
| `wp_pistol_trando_suppressor` | Trandoshan Suppressor | Stun |
| `wp_pistol_wookiee_s01` | Wookiee Pistol | |
| `wp_pistol_wookiee_bowcaster` | Wookiee Bowcaster Pistol | |

The Flare and Tangle pistols are utility items, so they come with tactical, not as sidearms.

### Melee (47)

**Sidearm (one-handed, 22):**

| Group | Models |
|---|---|
| Knives | `wp_mle_sword_vibroblade` ✅ (Vibroblade, Jagged Vibroblade), `wp_mle_knife_dagger`, `wp_mle_knife_stone` (Stone, Janta and Donkuwah knives), `wp_mle_knife_survival`, `wp_mle_knife_trando_skinner_blade` |
| Batons | `wp_mle_baton_stun` ✅, `wp_mle_baton_gaderiffi` (and its black-metal version) |
| One-handed swords and maces | `wp_mle_sword_s01` (Sword, Nyax Sword), `wp_mle_sword_s02` (Curved Sword), `wp_mle_sword_acid_sword`, `wp_mle_sword_junti_mace`, `wp_mle_sword_marauder`, `wp_mle_sword_massassi`, `wp_mle_sword_rantok`, `wp_mle_sword_rsf`, `wp_mle_sword_ryyk_blade`, `wp_sword_1h_wookiee_sword` |
| Knucklers | `wp_sp_vibroknuckler` ✅, `wp_sp_blacksun_knuckler`, `wp_sp_massassi_knuckler`, `wp_sp_blasterfist`, `wp_sword_1h_wookiee_knuckler` |

The Vibroknuckler is now a sidearm, by the size rule. The Naktra crystal knife (`wp_melee_knife_naktra_crystal`) has no name in Galaxies and isn't an item.

**Primary (two-handed, 25):**

| Group | Models |
|---|---|
| Two-handed swords | `wp_mle_2h_sword_s01` (Curved, Grooved), `wp_mle_2h_sword_s02` (Cleaver), `wp_mle_2h_sword_s03` (Scythe), `wp_mle_2h_sword_sith`, `wp_mle_2h_sword_kashyyk`, `wp_mle_2h_sword_blacksun_hack`, `wp_mle_2h_sword_maul_s01` (Power Hammer), `wp_mle_2h_sword_axe_s01` (Two-handed Axe, Gamorrean Battleaxe) |
| Axes | `wp_mle_axe_heavy_duty`, `wp_mle_axe_vibroax` |
| Polearms | `wp_mle_lance_vibrolance` ✅, `wp_mle_lance_s01` (Cryo Lance), `wp_mle_lance_shocklance`, `wp_mle_lance_electric_polearm`, `wp_mle_lance_controller` (Lance, Nightsister Lance), `wp_mle_lance_nightsister` (Energy Lance), `wp_mle_lance_kaminoan_greatstaff`, `wp_mle_lance_kashyyk_bladestick`, `wp_mle_lance_massassi`, `wp_mle_lance_vibroaxe` (Long Vibro Axe), `wp_mle_lance_trando_pike`, `wp_mle_lance_wookiee_polearm`, `wp_mle_lance_staff_metal_s01`, `wp_mle_lance_staff_wood_s01` (Janta, Wood), `wp_mle_lance_staff_wood_s02` (Combat Staff) |

The Cryo Lance and Shock Lance are natural homes for chilled and ionized effects on melee.

### Tactical (10, plus 6 scout traps)

| Model | Galaxies names | Notes |
|---|---|---|
| `wp_grenade_c22` ✅ | C22 and C12 Fragmentation Grenades | |
| `wp_grenade_glop` ✅ | Glop Grenade | |
| `wp_grenade_class_a` | Thermal Detonator, Zicx Bug Bomb | |
| `wp_grenade_imp_detonator` | Imperial Detonator | |
| `wp_grenade_7prg` | Proton Grenade | |
| `wp_grenade_ww41` | CryoBan Grenade | Chilled |
| `wp_mine_anti_vehicle` | Anti-Vehicle Mine | `placed` |
| `wp_mine_drx55` | DRX Mine | `placed` |
| `wp_mine_xg` | XG Mine | `placed` |
| `eqp_weapon_auto_laser_trap` | Auto Laser Trap | `placed`: a small turret |

Galaxies' scout traps have their own models too: `wp_grenade_flash_bomb`, `wp_grenade_noise_maker`, `wp_grenade_sonic_pulse`, `wp_grenade_tranquilizing_dart`, `wp_grenade_webber` and `wp_grenade_enraging_spur`. They wait for the scout rework. Other utility items, such as the Flare and Tangle pistols, are tactical for now. `wp_sp_ppack_bomb` has no template; it may make a detpack.

### Left out

- **Lightsabers:** 13 hilts as one-handed, two-handed and double-bladed (about 40 `.lsb` models). They wait on the Jedi decision (DESIGN's open questions).
- Creature attacks, turrets, droid and vehicle weapons: those belong to NPCs and vehicles, not items.

## Rules

- **Era:** Clone Wars weapons (DC-15, E-5) are in, available like any other. They're left over from the war.
- **Species weapons** (bowcasters, Wookiee pistols, the Trandoshan hunting rifle and pike) are locked to their species: `Species = wookiee` in the catalog. Only that species can equip one or fire it. In return a species weapon gets a bigger sidegrade budget (see Balance), so it's a little stronger than a common weapon of its archetype. NPCs aren't bound by this; a looted bowcaster can still be traded to a Wookiee.
- **Armor fits every species.** Every species wears every armor slot for its statistics. A Wookiee's or Ithorian's body doesn't show armor, and a Trandoshan's doesn't show gloves or boots; that's the cosmetic layer only. Their species traits are now small, like everyone else's.
- **Utility items** are tactical for now. Anything that was a scout's (traps, perhaps the flare) waits for the scout rework.

## Balance: the sidegrade budget

Within an archetype, weapons are different but not better. `sidegrade_score` (`core/include/swm/core/items.h`) turns a weapon's multipliers over its archetype into one number. The archetype itself scores 1.

| Term | Weight |
|---|---|
| Damage, rate of fire | in full (damage × rate) |
| Crits | their expected damage: 1 + chance × (multiplier − 1) |
| Spread, range | power −0.35 and 0.35 |
| Heat per shot | power −0.3 |
| Clip, reload time | power 0.15 and −0.15 |
| Headshot, Deflector Shield, vehicle damage | power 0.25, 0.15, 0.1 |
| Bolt velocity, swap time, aim speed | power 0.1, −0.05, 0.05 |
| Recoil | ÷ (1 + 0.1 × degrees) |
| An on-hit effect | × 1.1 |
| A carry penalty (run or sprint speed) | that speed to the power 1.5, so a slower weapon gets budget back |
| Melee reach, stagger | power 0.5 and 0.25, against the melee base |
| Melee arc, lunge, shield disruption | power 0.15 each, against the melee base |

Every weapon and ammo in the catalog must score within ±6% of its budget: 1, or 1.1 for a species weapon. `items_test` checks the whole catalog and prints each score. For example, the T-21 hits 1.42x as hard but fires at 0.78x, builds 1.15x the heat and slows its carrier to 0.94x run speed: 0.97.

## How we build them

### 1. One source of truth: the catalog names the Galaxies template (done)

Each catalog weapon names its Galaxies object:

```ini
[item]
Id = wp_melee_vibrolance
Template = object/weapon/melee/polearm/shared_lance_vibrolance.iff
ModelForward = -y   ; a melee model standing along Y, laid along +Z with the guns
ModelMuzzle = front ; a model with no muzzle point: one is made at its front
```

`swgtool export-items <tre_dir> game/imported_assets` reads the catalog (`--catalog` to name another, `--only <id>` for one item). For each weapon it follows the template to the model Galaxies shows, the `_combined` one with scope and stock where there is one, and exports it to `weapons/<id>/<id>.gltf`. That path is the item's `Model` unless it names another. Adding a weapon is one catalog entry.

This fixed the six guns that were exported without their scopes and stocks (T-21, EE-3, laser carbine, DL-44, D-18, scout blaster), and the DH-17 and ion pistols, which had been borrowing the D-18's model. The DLT-20A now uses the model Galaxies uses for it, `wp_rifle_dlt20`. Galaxies has no ion grenade, so ours shows as a glop grenade.

### 2. A contact sheet to check every model (done)

```
godot --path game --script res://tools/weapon_sheet.gd -- <output_prefix> [category | item id]...
```

It renders every catalog weapon in a soldier's hands, one image per category, six to a page, with a red ball on each gun's muzzle and the id underneath. Run it after adding weapons. (The first soldier on a page can be caught mid-pose; that's the sheet, not the weapon.)

### 3. One item per model, with variants as tiers

Galaxies' variants of one model (quest, Corsec, noob, Lithitanium, "light" or "heavy" versions) don't become separate items. They become Mk tiers and quality, or faction quartermaster stock. Recolors like the black DH-17 can be appearance options later. That keeps the catalog at about 130 weapons.

### 4. New archetypes only where handling really differs

Most weapons fit the archetypes we had. These handling styles didn't:

- **`beam`** (Rifles) and **`beam_cannon`** (Heavy Weapons): a near-instant stream that runs on heat, built on Battlefront II's chaingun (`rep_weap_inf_chaingun`: 20,000 m/s, 20 shots a second). The cannons hit harder and slow their carrier. Done.
- **`flamer`** (Heavy Weapons): a cone of slow pellets that drop and die out at about 11 m, on heat, setting its effect (the flamethrower burns, the acid launcher poisons). Battlefront II has no flamethrower, so the numbers are our own. Done.
- **`scatter_pistol`** (Pistols): a shotgun's spread at sidearm size, on the pistol's heat instead of a clip. About a pistol's damage a second up close; five pellets that spread 5° and die out at 24 m. Battlefront II has none, so the numbers are our own. Done.
- **`melee_2h`** (Melee): two-handed swords, axes, hammers and polearms, a primary. A primary should outweigh a sidearm, so its swings are slower but hit harder: about a fifth more damage a second than one-handed `melee` (165 every 0.65 s against 115 every 0.55 s), 3.2 m of reach against 2.6, a 90° arc against 70, more stagger (0.5 against 0.35) and shield disruption (3 against 2.5), and a shorter lunge (4.5 m against 5). Done.
- **`placed`:** a mine or trap set on the ground that triggers on proximity. Battlefront II has landmines and detpacks; we'd import their definitions, since only 37 Battlefront II weapon definitions are imported today. Comes with tactical.

The rest is the sidegrade multipliers we already have, plus effects through `OnHit`, the same way ammo carries burning, chilled and ionized. The primaries added **poisoned** (bio, 8 a second for 4 s, 35% of hits) and **snared** (60% run speed for 2 s, 30% of hits). The sidearms added **bleeding** (ballistic, 6 a second for 5 s, 30% of hits: the flechette pistol, the skinner blade, the razor knuckler) and **dazed** (70% run and sprint speed and no energy regained for 1.5 s, 25% of hits: the Trandoshan suppressor).

Still to come for these archetypes: their look. Beams and flames are drawn as bolts for now, and the particle beam doesn't pierce or the lightning cannon chain, as Galaxies' do.

### 5. Order of work

As planned in itemsideas: primaries, then sidearms, then tactical.

1. ~~**Exporter and catalog plumbing** (steps 1 and 2), fixing the existing weapons.~~ Done.
2. ~~**Primaries:** snipers (6), rifles (14), carbines and shotguns (20), heavy (6), and the `beam`, `beam_cannon` and `flamer` archetypes.~~ Done: 45 items (35 new), every one within budget. The black DH-17 (a recolor) and the DXR-6 carbine (the disruptor's model) aren't separate items. The laser carbine moved to carbine handling.
3. ~~**Sidearms:** pistols (30) and one-handed melee (22).~~ Done: 28 pistols (23 new) and 22 one-handed melee (19 new), every one within budget, and the `scatter_pistol` archetype. One-handed melee is balanced against the melee archetype as it is; two-handed melee may get its own, stronger archetype with step 4, since a primary should outweigh a sidearm.
4. ~~**Two-handed melee** (25).~~ Done: 25 items (24 new) on the new `melee_2h` archetype, every one within budget. The Vibrolance moved to it and was retuned (longer reach, a wider arc). Quest and recolor variants (the Grooved sword, the Gamorrean battleaxe, the Nightsister and Janta versions) aren't separate items. The Cryo Lance chills and the Shock Lance ionizes; the Executioner's Hack bleeds. The Trandoshan pike and Wookiee polearm are species weapons.
5. **Tactical:** grenades (6), mines and the laser trap (4) with `placed`, and scout traps (6).

