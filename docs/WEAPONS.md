# Weapons

Every weapon model in Galaxies, sorted into our seven categories, and how we turn them into items. Gear rules (Mk tiers, quality, the 2x curve, ammo effects) are in [Design](DESIGN.md#weapons-and-loadouts); weapon professions are in [Professions](PROFESSIONS.md).

## The categories

| Category | Slot | Profession | Archetypes (handling from Battlefront II) |
|---|---|---|---|
| Sniper | Primary | Rifles | `sniper` |
| Rifle | Primary | Rifles | `rifle_auto` (new: `beam`) |
| Carbine / shotgun | Primary | Carbines | `carbine_burst`, `scatter` |
| Heavy | Primary | Heavy Weapons | `heavy_launcher` (new: `beam`, `flamer`) |
| Sidearm | Sidearm | Pistols | `pistol` (and `scatter` for scatter pistols) |
| Melee | Sidearm if one-handed, primary if two-handed | Melee | `melee` |
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
| `wp_pistol_spraystick` | Spraystick | A rifle despite the file name; short-range spray, so it could be a shotgun |

The E-11 rifle has no model of its own: Galaxies uses the carbine's model for it.

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

The Flare and Tangle pistols could be tactical items instead.

### Melee (47)

**Sidearm (one-handed, 22):**

| Group | Models |
|---|---|
| Knives | `wp_mle_sword_vibroblade` ✅ (Vibroblade, Jagged Vibroblade), `wp_mle_knife_dagger`, `wp_mle_knife_stone` (Stone, Janta and Donkuwah knives), `wp_mle_knife_survival`, `wp_mle_knife_trando_skinner_blade` |
| Batons | `wp_mle_baton_stun` ✅, `wp_mle_baton_gaderiffi` (and its black-metal version) |
| One-handed swords and maces | `wp_mle_sword_s01` (Sword, Nyax Sword), `wp_mle_sword_s02` (Curved Sword), `wp_mle_sword_acid_sword`, `wp_mle_sword_junti_mace`, `wp_mle_sword_marauder`, `wp_mle_sword_massassi`, `wp_mle_sword_rantok`, `wp_mle_sword_rsf`, `wp_mle_sword_ryyk_blade`, `wp_sword_1h_wookiee_sword` |
| Knucklers | `wp_sp_vibroknuckler` ✅, `wp_sp_blacksun_knuckler`, `wp_sp_massassi_knuckler`, `wp_sp_blasterfist`, `wp_sword_1h_wookiee_knuckler` |

Our Vibroknuckler is a primary today; by the size rule it becomes a sidearm.

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

Galaxies' scout traps have their own models too: `wp_grenade_flash_bomb`, `wp_grenade_noise_maker`, `wp_grenade_sonic_pulse`, `wp_grenade_tranquilizing_dart`, `wp_grenade_webber` and `wp_grenade_enraging_spur`. They're thrown utility items (blind, lure, stun, sleep, snare, enrage) and fit tactical well. `wp_sp_ppack_bomb` has no template; it may make a detpack.

### Left out

- **Lightsabers:** 13 hilts as one-handed, two-handed and double-bladed (about 40 `.lsb` models). They wait on the Jedi decision (DESIGN's open questions).
- Creature attacks, turrets, droid and vehicle weapons: those belong to NPCs and vehicles, not items.

## How we build them

### 1. One source of truth: the catalog names the Galaxies template

The exporter's weapon list is hard-coded in `tools/swgtool/main.cpp` today, separate from the catalog. Instead, each catalog weapon names its Galaxies template:

```ini
[item]
Id = wp_rifle_t21
Template = object/weapon/ranged/rifle/shared_rifle_t21.iff
ModelForward = +y   ; only where the model faces the other way
```

`swgtool export-items` reads the catalog. For each item it follows the template to its model (as Galaxies does, picking up `_combined` models), exports it to `weapons/<id>/<id>.gltf`, and fills in `Model` itself. Adding a weapon then means one catalog entry. This also fixes the six guns now exported without their scopes and stocks (T-21, EE-3, laser carbine, DL-44, D-18, scout blaster) and the three stand-ins (DH-17 pistol and ion pistol on the D-18 model).

### 2. A contact sheet to check every model

As with the melee fixes, every exported model goes through the render script: held in the soldier's hand, muzzle marked. One sheet per category catches backwards and sideways models before they reach the game. The script already exists; it needs to read the catalog.

### 3. One item per model, with variants as tiers

Galaxies' variants of one model (quest, Corsec, noob, Lithitanium, "light" or "heavy" versions) don't become separate items. They become Mk tiers and quality, or faction quartermaster stock. Recolors like the black DH-17 can be appearance options later. That keeps the catalog at about 130 weapons.

### 4. New archetypes only where handling really differs

Most weapons fit the eight archetypes we have. Three handling styles don't:

- **`beam`:** a continuous hitscan stream that builds heat (lightning, particle and acid beams). Battlefront II's arc caster and chaingun are the closest bases.
- **`flamer`:** a short cone that damages over time and applies burning.
- **`placed`:** a mine or trap set on the ground that triggers on proximity. Battlefront II has landmines and detpacks; we'd import their definitions, since only 37 Battlefront II weapon definitions are imported today.

The rest is the sidegrade multipliers we already have, plus effects: ion (droids and vehicles), sonic, bleed, snare, stun. They can go on guns through `OnHit`, the same way ammo carries burning, chilled and ionized.

### 5. Order of work

As planned in itemsideas: primaries, then sidearms, then tactical.

1. **Exporter and catalog plumbing** (steps 1 and 2), fixing the existing 26.
2. **Primaries:** snipers (6), rifles (14), carbines and shotguns (20), heavy (6). That's 46 items and the `beam` and `flamer` archetypes.
3. **Sidearms:** pistols (30) and one-handed melee (22).
4. **Two-handed melee** (25).
5. **Tactical:** grenades (6), mines and the laser trap (4) with `placed`, and scout traps (6).

## Open questions

1. **A sidegrade budget formula.** Within an archetype, how do we keep weapons different but not better? One option: a score per weapon (damage × rate × accuracy × range, each weighted) that must land within a few percent of 1.0. Every weapon spends the same budget differently.
2. **Era:** Clone Wars models (DC-15, E-5) sit next to Galactic Civil War ones. Are they all available everywhere, or tied to faction quartermasters and eras?
3. **Tactical profession:** grenades sit under Heavy Weapons now. Should scout traps and utility pistols (flare, tangle) go there too, or to Smuggler or a scout line?
4. **Species weapons** (bowcasters, Wookiee pistols, the Trandoshan hunting rifle): can anyone use them, or are they species-locked with a bonus for their species?
