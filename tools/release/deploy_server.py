"""Deploy a commit to the game server (docs/BACKEND.md).

  python -I tools/release/deploy_server.py [--host <address>] [--ref HEAD]
                                           [--assets <imported_assets dir>]

Uploads `git archive <ref>`, then runs that commit's
server/deploy/deploy_remote.sh on the server over SSH: an incremental build,
a new release in /opt/swm/releases, `current` switched to it, the galaxy
restarted (it saves everyone first) and joined with swm_probe; if that fails,
the previous release comes back. Builds from the commit, never from
uncommitted changes.

--assets also uploads the server's part of a local imported_assets
(server_assets.py), for the first deploy or after re-exporting.

The host comes from --host or SWM_SERVER_HOST; SSH uses --key (default
~/.ssh/swm_vps_ed25519) as --user (default ops).
"""
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(__file__))
import server_assets  # noqa: E402


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True).stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--host", default=os.environ.get("SWM_SERVER_HOST"), help="the server's address")
    ap.add_argument("--user", default="ops")
    ap.add_argument("--key", default=os.path.join(os.path.expanduser("~"), ".ssh", "swm_vps_ed25519"))
    ap.add_argument("--ref", default="HEAD", help="the commit to deploy")
    ap.add_argument("--assets", help="imported_assets to upload the server's part of")
    args = ap.parse_args()
    if not args.host:
        sys.exit("no host: --host or SWM_SERVER_HOST")

    commit = git("rev-parse", "--short=12", args.ref).decode().strip()
    subject = git("log", "-1", "--format=%s", commit).decode().strip()
    print("deploying %s  %s" % (commit, subject), flush=True)
    target = "%s@%s" % (args.user, args.host)
    ssh = ["ssh", "-i", args.key, "-o", "BatchMode=yes", "-o", "ConnectTimeout=20"]
    scp = ["scp", "-q", "-i", args.key, "-o", "BatchMode=yes", "-o", "ConnectTimeout=20"]

    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "src.tar")
        git("archive", commit, "-o", src)
        subprocess.run(ssh + [target, "mkdir -p ~/deploy"], check=True)
        uploads = [src]
        if args.assets:
            packed = os.path.join(tmp, "server_assets.tar")
            n, size = server_assets.pack(args.assets, packed)
            print("game data: %d files, %.0f MB" % (n, size / 1e6), flush=True)
            uploads.append(packed)
        print("uploading ...", flush=True)
        subprocess.run(scp + uploads + [target + ":deploy/"], check=True)
        remote = git("show", "%s:server/deploy/deploy_remote.sh" % commit)
        cmd = "bash -s %s%s" % (commit, " --assets" if args.assets else "")
        rc = subprocess.run(ssh + [target, cmd], input=remote).returncode
    if rc != 0:
        sys.exit("deploy failed (%d)" % rc)


if __name__ == "__main__":
    main()
