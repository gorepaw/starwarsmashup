"""Publish a staged build to a release channel on the update server (docs/DISTRIBUTION.md).

  set SWBFO_PUBLISH_TOKEN=...
  python -I tools/release/publish.py --stage E:\\swm_stage\\player --channel stable --version 2026.11.28.1
  python -I tools/release/publish.py --stage E:\\swm_stage\\portable --channel portable --version ...

stable    the player build: game, tools, recipe, launcher; never imported_assets.
          Served from the Worker's /public area.
portable  the developer's portable build: the same plus game/imported_assets.
          Served from /private, only with the portable token. Never public.

Every file becomes a blob named by its SHA-256 (uploaded once, ever); the
channel's manifest lists path, hash and size, is signed with the release key
(swbfo_sign), and goes up last, so a half-finished publish is never live. Also
writes the channel's bootstrap zip (launcher.exe + launcher.cfg [+ portable.flag])
next to the stage folder: unzip it anywhere and run launcher.exe. The portable
token is never in it: copy portable.token next to the launcher by hand.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_URL = os.environ.get("SWBFO_UPDATE_URL", "")
DEFAULT_KEY = os.path.join(os.path.expanduser("~"), ".swm", "release_ed25519.key")
DEFAULT_SIGN = r"E:\swm_build\release\launcher\Release\swbfo_sign.exe"
AREAS = {"stable": "public", "portable": "private"}

# Never published: per-install state, local databases, the bootstrap-only files.
SKIP_TOP = {"state", "launcher.cfg", "portable.flag", "portable.token", "launcher.old.exe"}
SKIP_EXT = (".sqlite", ".sqlite-shm", ".sqlite-wal", ".log", ".pdb", ".tmp", ".part")


def files_to_publish(stage, with_assets):
    out = []
    for d, dirs, files in os.walk(stage):
        rel_dir = os.path.relpath(d, stage).replace("\\", "/")
        rel_dir = "" if rel_dir == "." else rel_dir + "/"
        if rel_dir == "":
            dirs[:] = [x for x in dirs if x not in SKIP_TOP]
        if rel_dir == "game/":
            dirs[:] = [x for x in dirs if with_assets or x != "imported_assets"]
        for f in files:
            rel = rel_dir + f
            if rel in SKIP_TOP or f.lower().endswith(SKIP_EXT):
                continue
            out.append(rel)
    return sorted(out)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


class Server:
    def __init__(self, url, area, token):
        self.base = url.rstrip("/") + "/" + area
        self.token = token

    def _req(self, method, key, data=None, length=None):
        req = urllib.request.Request(self.base + "/" + key, data=data, method=method)
        req.add_header("Authorization", "Bearer " + self.token)
        req.add_header("User-Agent", "swbfo-publish/1")
        if length is not None:
            req.add_header("Content-Length", str(length))
        return urllib.request.urlopen(req, timeout=300)

    def existing_blobs(self):
        keys, cursor = set(), None
        while True:
            with self._req("GET", "list" + ("?cursor=" + urllib.request.quote(cursor) if cursor else "")) as r:
                page = json.load(r)
            keys |= {k.rsplit("/", 1)[-1] for k in page["keys"]}
            cursor = page.get("cursor")
            if not cursor:
                return keys

    def put_file(self, key, path):
        size = os.path.getsize(path)
        for attempt in range(3):
            try:
                with open(path, "rb") as f:
                    with self._req("PUT", key, data=f, length=size) as r:
                        r.read()
                return
            except (urllib.error.URLError, ConnectionError, TimeoutError) as e:
                if attempt == 2:
                    raise RuntimeError("upload of %s failed: %s" % (key, e))

    def put_bytes(self, key, data):
        with self._req("PUT", key, data=data, length=len(data)) as r:
            r.read()


def bootstrap_zip(stage, channel, url, area):
    out = os.path.join(os.path.dirname(os.path.abspath(stage)), "SWBFO-%s.zip" % channel)
    cfg = "# SWBFO launcher\nchannel = %s\nurl = %s/%s\n" % (channel, url.rstrip("/"), area)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(os.path.join(stage, "launcher.exe"), "SWBFO/launcher.exe")
        z.writestr("SWBFO/launcher.cfg", cfg)
        if channel == "portable":
            z.writestr("SWBFO/portable.flag", "")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--stage", required=True, help="a folder tools/release/stage.py made")
    ap.add_argument("--channel", required=True, choices=sorted(AREAS))
    ap.add_argument("--version", required=True)
    ap.add_argument("--url", default=DEFAULT_URL, help="the Worker's base URL (or SWBFO_UPDATE_URL)")
    ap.add_argument("--key", default=DEFAULT_KEY, help="the release secret key (swbfo_sign keygen)")
    ap.add_argument("--sign-tool", default=DEFAULT_SIGN)
    ap.add_argument("--min-launcher", type=int, default=1)
    ap.add_argument("--threads", type=int, default=8)
    ap.add_argument("--dry-run", action="store_true", help="hash and list, upload nothing")
    args = ap.parse_args()

    stage = os.path.abspath(args.stage)
    if not os.path.exists(os.path.join(stage, "launcher.exe")):
        sys.exit("%s has no launcher.exe: stage it with tools/release/stage.py" % stage)
    with_assets = args.channel == "portable"
    if with_assets and not os.path.isdir(os.path.join(stage, "game", "imported_assets")):
        sys.exit("the portable channel needs game/imported_assets in the stage (stage.py --assets)")
    token = os.environ.get("SWBFO_PUBLISH_TOKEN", "")
    if not args.dry_run and (not token or not args.url):
        sys.exit("set SWBFO_PUBLISH_TOKEN and --url (or SWBFO_UPDATE_URL)")

    rels = files_to_publish(stage, with_assets)
    print("hashing %d files ..." % len(rels), flush=True)
    with concurrent.futures.ThreadPoolExecutor(args.threads) as pool:
        hashes = list(pool.map(lambda r: sha256_file(os.path.join(stage, r)), rels))
    files = [{"path": r, "sha256": h, "size": os.path.getsize(os.path.join(stage, r))} for r, h in zip(rels, hashes)]
    manifest = {"format": 1, "channel": args.channel, "version": args.version, "min_launcher": args.min_launcher,
                "files": files}
    body = (json.dumps(manifest, indent=1) + "\n").encode()
    total = sum(f["size"] for f in files)
    print("%s %s: %d files, %.0f MB" % (args.channel, args.version, len(files), total / 1e6))

    out_dir = os.path.join(os.path.dirname(stage), "publish_%s" % args.channel)
    os.makedirs(out_dir, exist_ok=True)
    manifest_file = os.path.join(out_dir, "latest.json")
    with open(manifest_file, "wb") as f:
        f.write(body)
    if not os.path.exists(args.key):
        sys.exit("no release key at %s (swbfo_sign keygen)" % args.key)
    if subprocess.run([args.sign_tool, "sign", args.key, manifest_file]).returncode != 0:
        sys.exit("signing failed")
    with open(manifest_file + ".sig", "rb") as f:
        sig = f.read()
    if args.dry_run:
        print("dry run: manifest and signature in", out_dir)
        return

    server = Server(args.url, AREAS[args.channel], token)
    have = server.existing_blobs()
    by_hash = {}
    for f in files:
        by_hash.setdefault(f["sha256"], f["path"])
    missing = [(h, p) for h, p in by_hash.items() if h not in have]
    size = sum(os.path.getsize(os.path.join(stage, p)) for _, p in missing)
    print("uploading %d new blobs (%.0f MB); %d already there" % (len(missing), size / 1e6, len(by_hash) - len(missing)),
          flush=True)

    done = [0]

    def upload(item):
        h, p = item
        server.put_file("blobs/%s/%s" % (h[:2], h), os.path.join(stage, p))
        done[0] += 1
        if done[0] % 200 == 0:
            print("  %d / %d" % (done[0], len(missing)), flush=True)

    with concurrent.futures.ThreadPoolExecutor(args.threads) as pool:
        list(pool.map(upload, missing))

    # The manifest last: the versioned copy, then latest's signature and itself.
    ch = "channels/%s/" % args.channel
    server.put_bytes(ch + "%s.json" % args.version, body)
    server.put_bytes(ch + "%s.json.sig" % args.version, sig)
    server.put_bytes(ch + "latest.json.sig", sig)
    server.put_bytes(ch + "latest.json", body)
    print("published %s %s" % (args.channel, args.version))
    print("bootstrap:", bootstrap_zip(stage, args.channel, args.url, AREAS[args.channel]))


if __name__ == "__main__":
    main()
