# Backend

**Status: sketch (2026-11-28). Nothing here is built yet.** This plans the first hosted version of the game: player accounts, a website, and game servers on a paid VPS. It follows [Distribution](DISTRIBUTION.md), which ships the client and its updates, and [Architecture](ARCHITECTURE.md#networking), which describes the target topology (world servers, instance servers, a backend owning all durable state).

## Where things stand

- **Identity is a typed name.** `Hello { name, ticket, team }` picks a saved character by name; there are no accounts or passwords. A hub's battle servers already admit only holders of a `ticket` the hub issued (`GameServer::admit`).
- **One process runs the galaxy.** `swm_server --galaxy` runs every planet as a `GameServer` in one process, sharing one `CharacterStore`; battles run in the same process.
- **State is SQLite** behind `core::CharacterStore` (`persistence.h`: characters, items, loadouts, quests, professions, materials, presets, reputation, telemetry). WAL mode, characters saved in batches every 10 s, economy changes written immediately from about 11 call sites on the tick thread.
- **Costs are measured** ([Server audit](SERVER_AUDIT.md)): an idle planet costs 0.6% of a core; 16 players in Mos Eisley cost 1.3 ms of a 33 ms tick; Tatooine with seven towns is about 217 MB; snapshots are **about 220 KB/s per client** in a crowd, because they're still full states.
- **The client doesn't learn why it can't connect** on a version mismatch: packets with another `protocol_version` are dropped.
- **The server has never been built on Linux.** `core/net/udp.cpp` has a POSIX branch; `server/main.cpp` has Windows-only bits.
- **Release infrastructure exists**: one Cloudflare Worker, R2 buckets, Ed25519-signed manifests, a launcher that updates itself and the game.

## Goal of the first hosted version

An **open alpha**: anyone can make an account and play on one server, one galaxy, with characters that persist and can't be duplicated or lost, a website to sign in and download, and an operator (the developer) who can deploy, back up, restore and ban without logging into a database by hand. Because sign-up is open, the server has a player cap with a login queue, and the alpha label stays until persistence and moderation have held up under strangers.

Out of scope for now: multiple shards, zone handoff between processes, paid anything (the project is non-commercial: no paid access, no donations tied to access, hosting paid by the developer).

## Shape

```
  Player's PC                         Cloudflare (free plan)                   VPS (paid)
  ───────────                         ──────────────────────                   ──────────
  launcher ── sign in, server list ──▶ Worker  /api/*  ──── D1 (accounts)
     │                                  │      /public, /private (updates; exists)
     │                                  └──── Pages (website)   Turnstile, Email
     │  join token (signed, 10 min)
     ▼
  game ──────────── UDP 7777 ─────────────────────────────────────────▶ swm_server --galaxy
                                                                          battle servers
                                                                          Postgres (characters)
                                                                          backups ──▶ R2 swm-backups
```

The split is deliberate: **everything about accounts lives on Cloudflare; everything about characters lives on the VPS**, and they're joined by a signed token, not a network call. The website and sign-in stay up when the game server is down or being redeployed, and the game server never needs to reach the account database while a player joins.

## Accounts and sign-in

- **Accounts live in D1**, served by the existing Worker under `/api/` (the update routes stay as they are). Tables `accounts(id, email, password_hash, display_name, created, email_verified, banned_until, role)`, `sessions` (refresh tokens, hashed), `email_tokens` (verification and reset links, hashed, single use, 1 hour).
- **Sign-up on the website**: email, display name (unique, filtered), password. A [Turnstile](https://developers.cloudflare.com/turnstile/) check, then a verification email (Cloudflare Email Service, sending from the project's domain). An account can't join a server until its email is verified.
- **Passwords** are hashed with Argon2id (WebAssembly in the Worker), with a per-account salt. The Workers free plan allows 10 ms of CPU per request and caps WebCrypto's PBKDF2 at 100,000 iterations, both too little for a sound password hash, so accounts need **Workers Paid** ($5 a month, 30 s CPU). Minimum length 10, checked against a list of common passwords; no other composition rules.
- **Password reset** by emailed link; it revokes every session. Changing the email address confirms with both the old and the new address.
- **Abuse limits for open sign-up**: Turnstile on sign-up, sign-in and reset; per-address and per-account rate limits on sign-in (lockout grows after failures); one account per verified email; a blocklist of disposable email domains; a server player cap with a login queue. A ban is on the account; repeat offenders get their email domain or address range reviewed by hand.
- **The launcher signs in** with email and password over HTTPS (`POST /api/auth/login`) and keeps a **refresh token** (random, stored hashed in D1, revocable, 30 days, rotated on use) in `state/`, encrypted with Windows DPAPI. "Sign out everywhere" on the website revokes them all.
- **Two-factor (TOTP)**: required for admin accounts from the start; optional for players later.
- **Joining a server**: the launcher trades the refresh token for a **join token**: `{account_id, display_name, role, server_id, expires (10 min), nonce}`, signed with Ed25519 by the Worker. It passes it to the game on the command line (or a one-shot file), and the game sends it in `Hello`.
- **The game server verifies the token offline** with the Worker's public key (the same Monocypher code the launcher uses), checks `server_id` and expiry, and remembers nonces until expiry so a token can't be replayed. No call to Cloudflare on join.
- **Bans** take effect within a token's lifetime: the server pulls `/api/bans` every minute (signed list) and kicks anyone on it; the Worker refuses new join tokens at once.
- **Account deletion** (GDPR-style, and simply decent): the website deletes the account in D1 and queues its id; the VPS pulls `/api/deleted` and removes that account's characters.
- **Admin role**: `role = admin` in the token unlocks server-side operator commands (kick, ban, teleport, give), so moderation happens in game, not in SQL.

Protocol change: `Hello` gains `join_token` and `character_id` (a character chosen from the account's list, which the server sends after verifying), and `name` stops being identity. Local and LAN play keep working without tokens: a server started without `--auth <public key>` accepts names as today.

## Characters and durable state (Postgres)

- **`PostgresCharacterStore`** implements `core::CharacterStore` (as `SqliteCharacterStore` does), via libpq. Gameplay code doesn't change. Characters gain an `account_id`; names become unique per galaxy, not identity.
- **Write-behind queue first** (the [audit](SERVER_AUDIT.md#persistence)'s recommendation): the ~11 immediate economy writes go into a queue flushed on a worker thread in batches, in order, every 250 ms or so. With Postgres a round trip per credit change on the tick thread would stall ticks. Build and test it against SQLite before Postgres.
- **No duplication, no loss**:
  - Economy changes that move value between characters (trades, bazaar, bank, bounties) are **one transaction** each, not two independent writes.
  - A character is **leased** to one server process at a time (`characters.owner_server`, `lease_expires`, renewed every save). A second server refuses to load it while the lease is live. This matters once battles or planets run in separate processes.
  - On shutdown, every character saves and its lease is released; on crash, leases expire in 60 s.
- **Telemetry** (match records) moves to Postgres as well, in its own schema.
- **Migrations**: numbered SQL files in `server/migrations/`, applied at server start inside a transaction, recorded in `schema_version`.
- **Why Postgres and not SQLite on the VPS**: SQLite would do for one process. Postgres is for several processes sharing state (battle servers out of process, planets split later), point-in-time recovery, and inspecting a live database safely. If the alpha stays one process, SQLite + the write-behind queue + nightly backups is an acceptable first step; the interface makes the switch later a swap, not a rewrite.

## Game servers on the VPS

- **Linux: Ubuntu 26.04 LTS.** The server, core and tools build with GCC 15 and pass all 39 tests (2026-11-28). The [server audit](SERVER_AUDIT.md)'s load test (Tatooine hub, 16 bots) runs at 1.1 ms a tick, 14% of a core including the bots, and 254 MB. Binaries built on 26.04 need its C library, so the VPS runs 26.04 too. `.github/workflows/linux.yml` builds and tests every push (GCC 14 on GitHub's Ubuntu 24.04 runner). The game server handles SIGTERM by saving everyone, so `systemctl stop` is safe.
- **Server data**: the server needs terrain bakes, collision, hub and battlefield data, and item exports, all derived from Battlefront II and Galaxies files. It's the developer's own extraction on the developer's own server, never served to players. It goes up through a third update channel, **`server`** (private bucket, its own token), with the same manifests and blobs, applied by a small headless `swm_update` command (the launcher's update code without the UI). The server channel holds only what the server reads (no textures, meshes or audio: a fraction of the 5 GB).
- **Processes**: `systemd` runs `swm-galaxy.service` (restart on failure, `LimitNOFILE`, its own user). Battle servers move out of the hub's process ([audit](SERVER_AUDIT.md#whats-left-recommended-next-steps), step 5): the hub launches `swm_server --battlefield ... --ticket-key ...` children on a port range and reaps them.
- **Before inviting anyone: delta snapshots.** At 220 KB/s per client, 30 players in a crowd is 6.6 MB/s, about 50 Mbit/s sustained and roughly 15 TB a month at that rate. Delta compression against the last acknowledged snapshot (inputs already carry `view_tick`) should cut that several times over and decides how big a plan is needed.
- **Version gate**: client and server share `protocol_version`. Add a version-neutral first exchange (a fixed-layout `Hello` header the server always parses) so an outdated client is told "update your game" instead of timing out. Releases publish the `server` channel and restart the server first, then publish `stable`; the launcher's update runs before play, so players are rarely behind.

## Joining a server

- **Server list**: each server posts a heartbeat to `/api/servers/heartbeat` every 30 s (name, address, players, version, uptime; signed with a per-server secret). The launcher shows the list; one server during the alpha, so it can simply be "Play".
- **Connect**: the launcher gets a join token for that server and starts `StarWarsMashup.exe --connect <address> --token-file <file>`. The game's main menu keeps local play and direct connect (for LAN and testing).

## Website

- **Cloudflare Pages**, a static site built from `web/` in the repo (plain HTML/CSS or a small static generator; no framework needed). Pages: home (what it is, the legal notes: unofficial, non-commercial, bring your own games), download (the `stable` bootstrap zip from the public bucket), patch notes (generated from release manifests plus a short changelog per version), server status (from the heartbeats), and account pages: sign up, sign in, verify email, reset password, your characters, sign out everywhere, delete account.
- **Account pages** use a session cookie set by the Worker (HttpOnly, Secure, SameSite=Lax), which is why the site and the API should share a domain.

## Operations

- **Backups**: nightly `pg_dump` (or the SQLite `.backup`), compressed, to a private R2 bucket `swm-backups`, kept 30 days; weekly copy kept 6 months. **A restore drill before the alpha**: restore last night's backup into a scratch database and start a server on it.
- **Deploys**: `tools/release/deploy_server.sh`: publish the `server` channel, announce a restart in game (a server message, 5 minutes), save everyone, stop, `swm_update`, start, check the heartbeat. Rolling back is republishing the previous version.
- **Monitoring**: the server's perf line goes to the systemd journal; the heartbeat carries tick time and player count; a Worker cron alerts (an email to the operator) when a heartbeat is 2 minutes late or tick time passes 50% of budget.
- **Logs and privacy**: keep IP addresses only in short-lived logs (7 days) for abuse handling; keep no more personal data than an email address and display name. A short privacy page on the website says what's kept and how to delete it.
- **Access**: SSH keys only, no passwords; a firewall allowing SSH (from the developer) and the game's UDP ports; Postgres listens only on localhost.

## Security notes

- **The server already owns all game state**, which closes most cheating: clients send inputs, never results.
- **UDP amplification**: a `Welcome` is far larger than a `Hello`. Someone spoofing a victim's address could make the server flood it. Before a public address: a stateless challenge (the server answers a first `Hello` with a small cookie the client must echo, like DTLS's HelloVerifyRequest), and rate limits per address.
- **Session hijacking**: packets aren't authenticated after the handshake, so someone on the path could inject inputs. Later: a session key from the handshake and a short MAC on every packet. Acceptable for a closed alpha.
- **DDoS**: Cloudflare can't sit in front of game UDP on the free plan, so choose a VPS host with network-level DDoS filtering for UDP.
- **Secrets**: the Worker's signing key for join tokens is a Worker secret; its public key is in the server's config. Separate from the release signing key, which stays offline.

## Costs (approximate; check current prices when ordering)

| Item | Cost |
|---|---|
| Domain | about $10–15 a year |
| Cloudflare (Workers, D1, Pages, R2, Email, Turnstile) | Workers Paid, $5 a month (needed for password hashing; also lifts the request budget); the rest within free tiers |
| VPS: 4+ dedicated cores, 8–16 GB RAM, generous or unmetered traffic, UDP DDoS filtering | roughly $20–60 a month |
| Code signing for the launcher (optional) | about $10 a month |

## Milestones

1. **Server on Linux.** Done: builds and passes the tests on Linux; CI job. Still to do: headless `swm_update`; `server` channel.
2. **Write-behind persistence.** The queue in front of `CharacterStore`, on SQLite; trades as single transactions; tests that kill the server mid-trade and check nothing is lost or duplicated.
3. **Delta snapshots.** Bandwidth measured again with 16 bots; the audit updated.
4. **Accounts.** D1 schema, sign-up / sign-in / verify / reset routes, Argon2id, email, Turnstile and rate limits, admin TOTP; launcher sign-in; join tokens; `Hello` with a token; `--auth` on the server; a character picker by account; bans and admin role.
5. **Postgres.** `PostgresCharacterStore`, migrations, leases; the same persistence tests against both stores.
6. **Website.** Pages site: home, download, patch notes, account pages, status.
7. **VPS.** Ordered, hardened, systemd units, backups and a restore drill, monitoring, the deploy script; battle servers in their own processes; the UDP handshake challenge.
8. **Open alpha.** Open sign-in with a low player cap; watch the perf line, bandwidth and the database; fix; raise the cap.

Milestones 1–3 need no accounts, domain or money and are useful on their own (Linux CI, safer saves, a cheaper server), so they can start any time.

## Decisions

Made (2026-11-28):

- **Accounts: the project's own**, email and password, with email verification. No third-party sign-in.
- **Sign-up: open**, with a player cap, a login queue and the abuse limits above.
- **A domain and a VPS** are being bought now. The domain is registered through Cloudflare (DNS is already there). It shouldn't contain "Star Wars" or imply official status. **VPS: OVHcloud US** (unlimited traffic, network DDoS filtering included); the plan size can change once delta snapshots have measured the real bandwidth.

Open:

- **SQLite or Postgres for the alpha** (Postgres recommended if battles move out of process for the alpha; SQLite acceptable if not).
