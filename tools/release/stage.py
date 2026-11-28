"""Assemble the installed layout of the game in a staging folder (docs/DISTRIBUTION.md).

  python -I tools/release/stage.py --out E:\\swm_stage [--assets]

The game ships as a loose Godot project: a release template that finds no .pck
loads project.godot from its own folder (release templates take no --path and
always start in the executable's folder), and res:// is that real folder, so
every path the game and its C++ core build from res:// works as in the editor.

  <out>/game/                       game/ without imported_assets, tests,
                                    databases, editor caches or Debug dlls; its
                                    .godot import cache made fresh by a headless import
  <out>/game/SWBFO.exe     the double-precision release template
  <out>/launcher.exe                the launcher (launcher/)
  <out>/tools/                      bf2tool.exe, swgtool.exe, swbfo_extract.exe, the SWG
                                    reference manifest, recipe.json

--assets also copies game/imported_assets (the portable build; never public).
Run from the repository root. Nothing it writes goes into the repo.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GAME = os.path.join(ROOT, "game")

# Top-level entries of game/ that never ship.
# game/tools stays: game scripts preload some of it (render_terrain.gd, terrain3d_bake.gd).
SKIP_TOP = {"imported_assets", "tests", ".godot"}
# Files that never ship, anywhere.
SKIP_EXT = (".sqlite", ".sqlite-shm", ".sqlite-wal", ".pdb", ".tmp", ".log")
DEBUG_DLLS = ("template_debug", ".debug.")
# Terrain3D's editor-only parts (menus, tools, docks): the game only needs its
# extension, its gdextension file and the shaders/scripts the terrain node uses.
TERRAIN3D_SKIP = {"csharp", "extras"}


def copy_game(dst):
    def ignore(dir_, names):
        rel = os.path.relpath(dir_, GAME)
        out = set()
        for n in names:
            low = n.lower()
            if rel == "." and n in SKIP_TOP:
                out.add(n)
            elif low.endswith(SKIP_EXT):
                out.add(n)
            elif low.endswith(".dll") and any(d in low for d in DEBUG_DLLS):
                out.add(n)
            elif rel == os.path.join("addons", "terrain_3d") and n in TERRAIN3D_SKIP:
                out.add(n)
        return out

    shutil.copytree(GAME, dst, ignore=ignore)


def unimported(game):
    """The import outputs (.import files' dest_files) that don't exist."""
    missing = []
    for d, _, fs in os.walk(game):
        for f in fs:
            if not f.endswith(".import"):
                continue
            for ln in open(os.path.join(d, f), encoding="utf-8", errors="replace"):
                if ln.startswith("dest_files="):
                    for res in re.findall(r'"res://([^"]+)"', ln):
                        if not os.path.exists(os.path.join(game, *res.split("/"))):
                            missing.append(res)
    return missing


def need(path, hint):
    if not os.path.exists(path):
        sys.exit("missing %s\n  %s" % (path, hint))
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", required=True, help="staging folder (emptied first)")
    ap.add_argument("--godot", default=r"E:\godot\godot-4.7.2", help="Godot source with bin/ built")
    ap.add_argument("--terrain3d", default=r"E:\godot\terrain3d-1.1.0", help="Terrain3D source, built")
    ap.add_argument("--build", default=r"E:\swm_build\release",
                    help="CMake build dir configured with -DGODOTCPP_TARGET=template_release")
    ap.add_argument("--assets", action="store_true", help="also copy game/imported_assets (portable build)")
    ap.add_argument("--bf2", help="Battlefront II's _lvl_pc, for emitting the recipe (default: the Steam install's)")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    if os.path.abspath(out).startswith(ROOT + os.sep) or out == ROOT:
        sys.exit("--out must be outside the repository")
    gbin = os.path.join(args.godot, "bin")
    template = need(os.path.join(gbin, "godot.windows.template_release.double.x86_64.exe"),
                    "scons platform=windows target=template_release precision=double d3d12=no accesskit=no")
    editor = need(os.path.join(gbin, "godot.windows.editor.double.x86_64.console.exe"), "build the editor first")
    swbfo_dll = need(os.path.join(GAME, "bin", "swbfo.windows.template_release.double.x86_64.dll"),
                   "cmake --build %s --config Release --target swbfo_godot" % args.build)
    t3d_dll = need(os.path.join(args.terrain3d, "project", "addons", "terrain_3d", "bin",
                                "libterrain.windows.release.x86_64.dll"),
                   "scons platform=windows target=template_release precision=double in " + args.terrain3d)
    tools_dir = os.path.join(args.build, "tools", "Release")
    tools = [need(os.path.join(tools_dir, t + ".exe"), "cmake --build %s --config Release --target %s" % (args.build, t))
             for t in ("bf2tool", "swgtool", "swbfo_extract")]
    launcher = need(os.path.join(args.build, "launcher", "Release", "launcher.exe"),
                    "cmake --build %s --config Release --target launcher" % args.build)

    # An existing staged imported_assets is set aside and kept: it's 5 GB.
    game = os.path.join(out, "game")
    kept = os.path.join(os.path.dirname(out), os.path.basename(out) + ".assets")
    if os.path.isdir(os.path.join(game, "imported_assets")):
        os.rename(os.path.join(game, "imported_assets"), kept)
    if os.path.exists(out):
        shutil.rmtree(out)
    print("copying game/ ...", flush=True)
    copy_game(game)
    shutil.copy2(swbfo_dll, os.path.join(game, "bin"))
    t3d_bin = os.path.join(game, "addons", "terrain_3d", "bin")
    os.makedirs(t3d_bin, exist_ok=True)
    shutil.copy2(t3d_dll, t3d_bin)

    # A fresh import of the project's own resources (UI art, fonts, shaders),
    # without imported_assets in view: its cache would be derived game data.
    # The editor needs Terrain3D's debug dll to load the addon while importing;
    # it's removed again afterwards.
    t3d_debug = os.path.join(GAME, "addons", "terrain_3d", "bin", "libterrain.windows.debug.x86_64.dll")
    swbfo_debug = os.path.join(GAME, "bin", "swbfo.windows.template_debug.double.x86_64.dll")
    shutil.copy2(t3d_debug, t3d_bin)
    shutil.copy2(swbfo_debug, os.path.join(game, "bin"))
    try:
        print("importing ...", flush=True)
        p = subprocess.Popen([editor, "--headless", "--path", game, "--import"], stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
        try:
            rc = p.wait(timeout=300)
        except subprocess.TimeoutExpired:
            # The editor sometimes finishes importing and then never exits.
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(p.pid)], capture_output=True)
            p.wait()
            missing = unimported(game)
            if missing:
                sys.exit("headless import hung with %d resources not imported (first: %s)" % (len(missing), missing[0]))
            print("note: the editor didn't exit after importing; every resource was imported, so carrying on")
            rc = 0
        if rc != 0:
            sys.exit("headless import failed (%d)" % rc)
    finally:
        os.remove(os.path.join(t3d_bin, os.path.basename(t3d_debug)))
        os.remove(os.path.join(game, "bin", os.path.basename(swbfo_debug)))
    for cache in ("editor", "shader_cache"):
        shutil.rmtree(os.path.join(game, ".godot", cache), ignore_errors=True)

    if os.path.isdir(kept):
        os.rename(kept, os.path.join(game, "imported_assets"))
        print("kept the staged imported_assets")
    elif args.assets:
        print("copying imported_assets (5 GB) ...", flush=True)
        shutil.copytree(os.path.join(GAME, "imported_assets"), os.path.join(game, "imported_assets"))

    shutil.copy2(template, os.path.join(game, "SWBFO.exe"))
    shutil.copy2(launcher, os.path.join(out, "launcher.exe"))
    os.makedirs(os.path.join(out, "tools"))
    for t in tools:
        shutil.copy2(t, os.path.join(out, "tools"))
    shutil.copy2(os.path.join(ROOT, "tools", "manifests", "swg-total-experience-2005.tsv"), os.path.join(out, "tools"))
    # The extraction recipe (worked out from this machine's Battlefront II and
    # imported_assets: see rebuild_imported_assets.py --emit-recipe).
    emit = [sys.executable, "-I", "-B", os.path.join("tools", "assets", "rebuild_imported_assets.py"),
            "--emit-recipe", os.path.join(out, "tools", "recipe.json")]
    if args.bf2:
        emit += ["--bf2", args.bf2]
    if subprocess.run(emit, cwd=ROOT).returncode != 0:
        sys.exit("emitting the recipe failed")

    size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(out) for f in fs
               if "imported_assets" not in d)
    print("staged %s (%.0f MB without imported_assets)" % (out, size / 1e6))
    print("run: %s" % os.path.join(game, "SWBFO.exe"))


if __name__ == "__main__":
    main()
