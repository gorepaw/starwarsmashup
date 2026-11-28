#!/bin/bash
# Runs on the game server, as ops, for tools/release/deploy_server.py.
#
#   deploy_remote.sh <commit> [--assets]
#
# Expects ~/deploy/src.tar (a `git archive` of <commit>) and, with --assets,
# ~/deploy/server_assets.tar (tools/release/server_assets.py). Layout:
#
#   /opt/swm/assets/imported_assets     the server's game data (shared by releases)
#   /opt/swm/releases/<commit>/bin      swbfo_server, swbfo_probe, swm-backup
#   /opt/swm/releases/<commit>/game     the repo's game/ (imported_assets: a link to the shared one)
#   /opt/swm/current -> releases/<commit>
#   /var/lib/swm                        the database (never touched here)
#
# Builds in ~/src and ~/build (incrementally: only changed files are copied
# over), installs a release, switches `current`, restarts swm-galaxy and joins
# it with swbfo_probe. If the server doesn't come up or the probe fails, it
# switches back to the previous release and exits 1.
set -euo pipefail
COMMIT=$1
WITH_ASSETS=${2:-}
export DEBIAN_FRONTEND=noninteractive
say() { echo "[deploy] $*"; }

need=()
for pkg in build-essential g++-14 cmake sqlite3 zstd rsync; do
  dpkg -s "$pkg" >/dev/null 2>&1 || need+=("$pkg")
done
if [ ${#need[@]} -gt 0 ]; then
  say "installing ${need[*]}"
  sudo apt-get update -qq && sudo apt-get install -y -qq "${need[@]}" >/dev/null
fi

# Source: copy over only what changed, so the build stays incremental.
rm -rf ~/deploy/src && mkdir -p ~/deploy/src ~/src
tar -xf ~/deploy/src.tar -C ~/deploy/src
rsync -rc --delete ~/deploy/src/ ~/src/
say "building $COMMIT"
export CC=gcc-14 CXX=g++-14
# Configure every time (a second when nothing changed): make won't build a
# target added since the last configure.
if ! cmake -S ~/src -B ~/build -DCMAKE_BUILD_TYPE=Release -DSWBFO_BUILD_GODOT=OFF > ~/deploy/configure.log 2>&1; then
  tail -20 ~/deploy/configure.log
  say "configure failed"
  exit 1
fi
if ! cmake --build ~/build -j "$(nproc)" --target swbfo_server swbfo_probe > ~/deploy/build.log 2>&1; then
  { grep -E 'error|Error|No rule' ~/deploy/build.log || tail -20 ~/deploy/build.log; } | sort -u | head -20
  say "build failed"
  exit 1
fi

# Game data shared by releases. The first deploy moves the old layout's.
sudo install -d -m 755 /opt/swm/assets /opt/swm/releases
if [ ! -d /opt/swm/assets/imported_assets ] && [ -d /opt/swm/game/imported_assets ]; then
  say "moving imported_assets to /opt/swm/assets"
  sudo mv /opt/swm/game/imported_assets /opt/swm/assets/imported_assets
fi
if [ "$WITH_ASSETS" = "--assets" ]; then
  say "installing the uploaded game data"
  sudo rm -rf /opt/swm/assets/new && sudo mkdir -p /opt/swm/assets/new
  sudo tar -xf ~/deploy/server_assets.tar -C /opt/swm/assets/new
  sudo chown -R root:root /opt/swm/assets/new && sudo chmod -R a+rX,go-w /opt/swm/assets/new
  sudo rm -rf /opt/swm/assets/old
  [ -d /opt/swm/assets/imported_assets ] && sudo mv /opt/swm/assets/imported_assets /opt/swm/assets/old
  sudo mv /opt/swm/assets/new/imported_assets /opt/swm/assets/imported_assets
  sudo rm -rf /opt/swm/assets/new /opt/swm/assets/old
fi
[ -d /opt/swm/assets/imported_assets ] || { say "no game data on the server: deploy with --assets"; exit 1; }

# The release.
REL=/opt/swm/releases/$COMMIT
sudo rm -rf "$REL.new" && sudo install -d -m 755 "$REL.new/bin"
sudo install -m 755 ~/build/server/swbfo_server ~/build/server/swbfo_probe "$REL.new/bin/"
sudo install -m 755 ~/src/server/deploy/swm-backup.sh "$REL.new/bin/swm-backup"
sudo cp -r ~/src/game "$REL.new/game"
sudo rm -rf "$REL.new/game/tests" "$REL.new/game/imported_assets"
sudo ln -s /opt/swm/assets/imported_assets "$REL.new/game/imported_assets"
sudo chown -R root:root "$REL.new" && sudo chmod -R a+rX,go-w "$REL.new"
sudo rm -rf "$REL" && sudo mv "$REL.new" "$REL"

PREVIOUS=$(readlink /opt/swm/current 2>/dev/null || true)
switch_to() {
  sudo ln -sfn "$1" /opt/swm/current.new && sudo mv -T /opt/swm/current.new /opt/swm/current
}
switch_to "$REL"
sudo install -m 644 ~/src/server/deploy/swm-galaxy.service ~/src/server/deploy/swm-backup.service \
  ~/src/server/deploy/swm-backup.timer /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --quiet swm-galaxy swm-backup.timer

# Restart (SIGTERM saves everyone) and wait until every planet is up.
started_ok() {
  sudo systemctl restart swm-galaxy
  local since
  since=$(date '+%Y-%m-%d %H:%M:%S')
  for _ in $(seq 1 90); do
    sleep 2
    systemctl is-active --quiet swm-galaxy || return 1
    if sudo journalctl -u swm-galaxy --since "$since" -o cat | grep -q 'UDP port'; then
      return 0
    fi
  done
  return 1
}
probe_ok() {
  /opt/swm/current/bin/swbfo_probe 127.0.0.1:7777 --bake /opt/swm/assets/imported_assets/terrain/tatooine \
    --name deploy-probe --seconds 5
}
if started_ok && probe_ok; then
  say "live: $COMMIT"
  sudo rm -rf /opt/swm/game /opt/swm/bin  # the layout before releases, if still there
  # Keep the five newest releases (and whatever is current).
  cur=$(readlink /opt/swm/current)
  ls -1dt /opt/swm/releases/*/ 2>/dev/null | tail -n +6 | while read -r old; do
    [ "${old%/}" = "$cur" ] || sudo rm -rf "$old"
  done
  exit 0
fi
say "the new release didn't come up; journal:"
sudo journalctl -u swm-galaxy -n 20 --no-pager -o cat || true
if [ -n "$PREVIOUS" ] && [ "$PREVIOUS" != "$REL" ]; then
  say "rolling back to $PREVIOUS"
  switch_to "$PREVIOUS"
  started_ok && say "rolled back" || say "ROLLBACK FAILED TOO"
fi
exit 1
