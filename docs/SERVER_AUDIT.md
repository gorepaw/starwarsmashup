# Server Resource & Cost Audit (Stage 13)

What the open-world server (`swbfo_server --hub`, or `SwbfoServer` inside Godot) costs to run, what Stage 13 changed, and what's left before public hosting. Measured on 2026-11-25 on the dev machine (Windows 11, Release build), on the full 16 × 16 km Tatooine bake with all seven towns exported.

## How to measure

`swbfo_server` logs a perf line every 10 seconds (`GameServer::take_perf`, `core::ServerPerf`):

```
perf: tick 1.36 ms avg, 3.06 ms worst (4% of budget), npcs 78 simulated / 86 frozen per tick,
      snapshots 3483.6 KB/s out (9431 B largest), strings in 1% of entities,
      wildlife 4/85 groups awake, 284 mob(s) asleep
```

- **tick**: time inside `GameServer::step()` (simulation, AI input, snapshots); the budget is one tick (33 ms at 30 Hz).
- **npcs simulated / frozen**: NPCs moved this tick vs left standing because no player is near.
- **snapshots**: bytes sent to clients and the largest single snapshot (a UDP datagram tops out at 64 KB).
- **strings**: how often an entity carried its rarely changing strings.
- **wildlife**: lairs and camps awake, and creatures/bandits asleep (out of the world).

Load test: `swbfo_server --bake imported_assets/terrain/tatooine --hub hubs/mos_eisley.ini --catalog items/swg_catalog.ini --bf2-items imported_assets/items --bots 16` (run from `game/`). The bots are in-process clients, so process CPU includes them.

## Results

| Scenario | Before Stage 13 | After Stage 13 |
|---|---|---|
| Idle (nobody online), process CPU | 9.7% of a core (Mos Eisley wildlife only) | **0.6%** (7 towns, 3× the wildlife) |
| Idle server tick | not instrumented | **0.07 ms** |
| 16 clients in Mos Eisley, server tick | not instrumented | **1.2–1.4 ms** (4% of budget) |
| 16 clients, process CPU (incl. 16 bot clients) | 16% of a core | 13–18% of a core |
| 16 clients, snapshot bandwidth out | 6.99 MB/s | **3.48 MB/s** (−50%) |
| Largest snapshot | 15.0 KB | **9.4 KB** |
| Memory (working set, same 7-town collision loaded) | 314 MB | **217 MB** |
| Population loaded | 55 people, 84 creatures, 29 bandits | 147 people, 233 creatures, 68 bandits |

"Before" is the pre-Stage-13 binary on the same data. Bandwidth "before" is this build with the old encoding forced, so the load is identical.

## What changed

### CPU: simulate only what players are near

1. **The hub's AI ran about 1,000 times a second.** `Hub::update` (crowd walking, all wildlife and bandit AI) was called on every pass of the server loop (1 ms sleeps), but the server only reads NPC input once a tick (30 Hz). It now steers once per server tick.
2. **NPC lookup was a linear scan.** `GameServer::find_npc` walked every NPC; `WorldPopulation` called it for every mob on every update, which is quadratic in NPC count. It's now an index (`npc_index_`). `remove_npc` keeps that index valid with swap-and-pop.
3. **NPCs no player is near aren't simulated** (`ServerConfig::npc_sim_radius`, 350 m, open world only). A spatial grid of players (`core::PointGrid`, `spatial.h`) answers "is anyone near" in constant time. The hub's walkers don't path or count as stuck while frozen.
4. **Lairs and camps sleep** (`core::PopulationActivity`). A group wakes when a player comes within 300 m of its edge, and sleeps after 15 s with nobody within 400 m and none of its members fighting or going home. Asleep, its living members are reset home, healed and taken out of the world; its dead wait to respawn until it wakes. Sleeping groups cost one proximity check every 0.5 s.

### Bandwidth: interest management and leaner snapshots

1. **Rarely changing strings are sent rarely.** Every snapshot used to carry each entity's name, appearance JSON, worn-gear list, weapon model and anim set, 30 times a second. Now each entity carries a 4-byte key (`net::static_key`). The strings come only during the first 8 ticks a client sees it, when the key changes (gear swap, Image Designer), and every 3 s as a refresh. The client caches them per entity (`GameClient::fill_static`), so Godot sees no difference. A crowd of real players, whose appearance strings run to hundreds of bytes, would overflow the 64 KB datagram without this.
2. **Others' states are compact.** Only a client's own entity carries its full predicted state (ammunition, heat, energy, timers, in doubles). Everyone else's is sent as drawn: floats (about a millimetre at a planet's edge), posture bits, held weapon, reloading flag. That's about 48 bytes instead of about 137.
3. **Per-client culling with hysteresis** (open world only; matches send everything): NPCs within 240 m, other players within 1,200 m, bolts/explosions/impacts within 450 m (your own always). Once sent, an entity is kept until it's 30 m past its radius, so it doesn't blink at the edge (clients drop entities missing from a snapshot).
4. **Built once, not once per client.** Entity descriptions and their keys are made once a tick. Selecting nearby NPCs uses a grid instead of testing every NPC against every client.

### Memory

- **The world's collision grid** (4 m cells over the bounding box of all collision) held a `std::vector` per cell, 24 bytes each even when empty. With towns 10 km apart the box is mostly empty: about 4.2 million cells. It's now packed (one offset per cell plus one array of indices), saving about 97 MB.
- **Terrain holes** (rooms' floors at 25 cm) are one patch per town instead of one box over all of them. A single box spanning Wayfar to Mos Entha would be about 1 GB; seven patches are 20 MB.
- The terrain heightfield is 67 MB (4,097² float32); a server doesn't need its texture-family map, and doesn't load it.

## Persistence

SQLite runs in WAL mode with `synchronous = NORMAL`, so a commit costs no fsync except at checkpoints. Characters are saved together every 10 s (`save_interval`, one `store_->save` batch). Economy changes (credits after a purchase, a loot pickup, a bank deposit) are written right away from about 11 call sites, synchronously on the tick thread. That's fine for SQLite at this scale.

**Before public hosting (Postgres):** queue those writes and flush them off the tick thread in batches (a write-behind queue in front of `CharacterStore`). A network round-trip per credit change would otherwise stall ticks.

## What's left (recommended next steps)

1. **Delta snapshots.** Snapshots are still full states at 30 Hz: about 220 KB/s per client in a crowded Mos Eisley. The next step is delta-compressing against the client's last acknowledged snapshot (inputs already carry `view_tick`), then sending far entities less often (needs a client that interpolates across gaps instead of dropping missing entities).
2. **Write-behind persistence** (above), before Postgres.
3. **Zone handoff.** One process owns the planet. The interest grid and sleeping groups are the groundwork for splitting a planet into cells owned by different processes.
4. **Navigation tiles are never evicted.** `NavGrid` builds 32 × 32-cell tiles lazily and keeps them. Bandits pathing across the planet will grow it; evict tiles not used for minutes.
5. **Battle servers run in the hub's process** (`Hub` launches them). For hosting, move them to their own processes, as the architecture plans.
