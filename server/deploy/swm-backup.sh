#!/bin/bash
# Backs up the game server's database to R2 through the update Worker's
# write-only /backups route (docs/BACKEND.md). Run by swm-backup.service.
#
#   /etc/swm/backup.url      the Worker's backups URL, e.g. https://swm-updates.<account>.workers.dev/backups
#   /etc/swm/backup.header   "Authorization: Bearer <BACKUP_TOKEN>" (root:swm, 0640)
#
# SQLite's online backup copies a consistent snapshot while the server runs.
# Every run uploads daily/<stamp>; Sundays also weekly/<stamp>. The bucket's
# lifecycle rules expire them.
set -euo pipefail
DB=${1:-/var/lib/swm/swm.sqlite}
URL=$(cat /etc/swm/backup.url)
stamp=$(date -u +%Y-%m-%dT%H%MZ)
work=$(mktemp -d /var/lib/swm/backup.XXXXXX)
trap 'rm -rf "$work"' EXIT

sqlite3 "$DB" ".backup '$work/swm.sqlite'"
check=$(sqlite3 "$work/swm.sqlite" "PRAGMA integrity_check;")
if [ "$check" != "ok" ]; then
  echo "integrity check failed: $check" >&2
  exit 1
fi
zstd -q -19 "$work/swm.sqlite" -o "$work/swm.sqlite.zst"

put() {
  curl -fsS --retry 3 --retry-all-errors -X PUT -H @/etc/swm/backup.header \
    --data-binary @"$work/swm.sqlite.zst" "$URL/$1" >/dev/null
  echo "stored $1 ($(stat -c %s "$work/swm.sqlite.zst") bytes)"
}
put "daily/swm-$stamp.sqlite.zst"
if [ "$(date -u +%u)" = 7 ]; then
  put "weekly/swm-$stamp.sqlite.zst"
fi
