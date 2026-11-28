"""Pack the part of imported_assets the game server reads into a tar, for
deploy_server.py --assets: heightfields, collision, water, terrain holes, town
and battlefield data, items, loadouts, vehicles. No textures, meshes, shaders or
audio: about 0.9 GB of the 5 GB. (Checked with strace: the galaxy server opens
nothing else under imported_assets.)

  python -I tools/release/server_assets.py <imported_assets dir> <out.tar>
"""
import os
import sys
import tarfile

FOLDERS = ("terrain", "maps", "vehicles", "loadouts", "items")
CLIENT_ONLY = (".png", ".shaders", ".bin", ".gltf", ".glb", ".wav", ".ogg", ".mp3")


def pack(src, out):
    n = size = 0
    with tarfile.open(out, "w") as tar:
        for top in FOLDERS:
            for d, _, fs in os.walk(os.path.join(src, top)):
                for f in sorted(fs):
                    if f.lower().endswith(CLIENT_ONLY):
                        continue
                    p = os.path.join(d, f)
                    tar.add(p, arcname="imported_assets/" + os.path.relpath(p, src).replace(os.sep, "/"))
                    n += 1
                    size += os.path.getsize(p)
    return n, size


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    n, size = pack(sys.argv[1], sys.argv[2])
    print("%d files, %.0f MB -> %s" % (n, size / 1e6, sys.argv[2]))


if __name__ == "__main__":
    main()
