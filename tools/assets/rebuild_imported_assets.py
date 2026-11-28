"""Rebuild game/imported_assets from your own Battlefront II and Galaxies files.

  python -I tools/assets/rebuild_imported_assets.py --swg <dir of .tre> [--bf2 <_lvl_pc>] [phase...]
  python -I tools/assets/rebuild_imported_assets.py --swg <dir> --list      print every command, run nothing
  python -I tools/assets/rebuild_imported_assets.py --emit-recipe <file>    write the launcher's recipe

Run from the repository root after building the tools (cmake --build build\\msvc
--config Release --target swgtool bf2tool). Phases, in order (all by default):

  bf2       Battlefront II's HUD, audio, effects, the soldiers' kits (loadouts and
            their weapons, items/) and the clone and droid models (characters/)
  maps      every conquest map, every other mode's layer (tools/assets/mode_exports.txt),
            and the space maps with their Space Assault rules (space_assault.py)
  vehicles  the vehicles and turrets the battlefields list, then the space maps' own
            (starfighters from the side files, capital ships' turrets from the map's level)
  swg       Galaxies' species, the stormtrooper and natives, the catalog's weapons,
            armor and clothing, personal vehicles, the Mos Eisley test region, and music
  planets   each planet in game/hubs/galaxy.ini: its 16 x 16 km bake, its towns
            (the hub file and its Town files) and their people
  check     every imported_assets path the repo's data and code name exists

Where each command comes from: battlefield files record their maps' export
lines and their vehicle lists; Loadout lines name the kits; the catalog names
character models; game/hubs/galaxy.ini and the hub files name the planets and
towns; the rest are tools/README.md's documented exports. Each command's
output goes to <log dir>/NNN_<step>.log; a failed step is reported and the
run goes on. Nothing here contains game data: it all comes from your files.
"""
import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import space_assault  # noqa: E402

DEFAULT_BF2 = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Battlefront II Classic\GameData\Data\_lvl_pc"
OUT = os.path.join("game", "imported_assets")
PHASES = ["bf2", "maps", "vehicles", "swg", "planets", "check"]

# The soldier clips game/rendering/soldier.gd plays: Battlefront's rifle
# (human_0, human_1), pistol (human_3) and bazooka (human_2) banks.
RIFLE = ["human_rifle_stand_idle_emote_full", "human_rifle_stand_runforward", "human_rifle_stand_runbackward",
         "human_rifle_stand_shoot_full", "human_rifle_stand_reload_full", "human_rifle_stand_shoot_secondary_full",
         "human_rifle_stand_hitfront", "human_rifle_stand_death_forward", "human_rifle_stand_death_backward",
         "human_rifle_crouch_idle_emote_full", "human_rifle_crouch_walkforward", "human_rifle_crouch_walkbackward",
         "human_rifle_crouch_shoot_full", "human_rifle_crouch_reload_full", "human_rifle_crouch_hitfront",
         "human_rifle_diveforward", "human_rifle_jump", "human_rifle_fall", "human_rifle_landsoft"]
PISTOL = ["human_pistol_stand_runforward", "human_pistol_stand_shoot", "human_pistol_stand_reload",
          "human_pistol_crouch_walkforward", "human_pistol_crouch_shoot", "human_pistol_crouch_reload"]
BAZOOKA = ["human_bazooka_stand_idle_emote", "human_bazooka_stand_runforward", "human_bazooka_stand_runbackward",
           "human_bazooka_stand_shoot_full", "human_bazooka_stand_reload_full", "human_bazooka_crouch_idle_emote",
           "human_bazooka_crouch_walkforward", "human_bazooka_crouch_walkbackward", "human_bazooka_crouch_shoot",
           "human_bazooka_crouch_reload"]
# Riders' and turret gunners' poses (human_4).
RIDER = ["human_speederbike_9pose", "human_barc_9pose", "human_stap_ride", "human_speederbike_sit",
         "human_minigun_9pose", "human_dishcannon_9pose", "human_drive", "human_drivesnowspeeder", "human_ride"]
# A human male in the full stormtrooper armor set, holding an E-11.
STORMTROOPER = ["hum_m"] + ["armor_stormtrooper_%s_m" % p for p in
                            ["helmet", "chest_plate", "chest_plate_pad", "bicep_l", "bicep_r", "bracer_l",
                             "bracer_r", "gloves", "utility_belt", "leggings", "boots"]]
NATIVES = ["jawa", "gamorrean", "ewok", "geonosian"]  # battlefields' [natives] Look
# Maps whose destructibles and walkers name effects (export-effects --map).
EFFECT_MAPS = ["kas2", "mus1", "tan1", "kam1", "cor1", "dea1", "nab2", "dag1", "hot1"]
# Planet bakes: 4,097 x 4,097 samples 4 m apart around the origin (16 x 16 km).
PLANET_SAMPLES, PLANET_SPACING = 4097, 4


def tool(name):
    return os.path.join("build", "msvc", "tools", "Release", name + ".exe")


def recorded(line, bf2):
    # A command recorded in a data file: its tokens, quoted ones whole, the
    # install placeholder replaced.
    toks = [t[1:-1] if t.startswith('"') and t.endswith('"') else t
            for t in re.findall(r'"[^"]*"|\S+', line.strip().lstrip(";").strip())]
    assert toks[0] == "bf2tool", line
    return [tool("bf2tool")] + [bf2 if t == r"<Battlefront>\GameData\Data\_lvl_pc" else t for t in toks[1:]]


def ini_values(path, key):
    return [m.group(1) for ln in open(path, encoding="utf-8", errors="replace")
            for m in [re.match(r"^\s*" + key + r"\s*=\s*(.+?)\s*$", ln)] if m]


def first_section(path, section):
    vals, cur = {}, None
    for ln in open(path, encoding="utf-8", errors="replace"):
        s = ln.strip()
        if s.startswith("["):
            if cur == section:
                break
            cur = s.strip("[]")
        elif cur == section and "=" in s and not s.startswith(";"):
            k, v = s.split("=", 1)
            vals.setdefault(k.strip(), v.strip())
    return vals


class Step:
    def __init__(self, phase, label, argv=None, after=None, expand=None):
        self.phase, self.label, self.argv, self.after, self.expand = phase, label, argv, after, expand


def plan(bf2, swg, scratch):
    steps = []
    bfs = sorted(glob.glob(os.path.join("game", "battlefields", "*.ini")))
    ingame = os.path.join(bf2, "ingame.lvl")

    # bf2
    steps.append(Step("bf2", "hud", [tool("bf2tool"), "export-hud", bf2, "-o", os.path.join(OUT, "hud")]))
    steps.append(Step("bf2", "audio", [tool("bf2tool"), "export-audio", bf2, "-o", os.path.join(OUT, "audio")]))
    eff = [tool("bf2tool"), "export-effects", bf2, "-o", os.path.join(OUT, "effects")]
    for m in EFFECT_MAPS:
        eff += ["--map", m]
    steps.append(Step("bf2", "effects", eff))
    kits = sorted({m.group(1) for bf in bfs for v in ini_values(bf, "Loadout")
                   for m in [re.search(r"imported_assets/loadouts/([A-Za-z0-9_]+)\.ini", v)] if m})
    for kit in kits:
        steps.append(Step("bf2", "loadout " + kit, [tool("bf2tool"), "export-loadout", bf2, kit.split("_")[0], kit,
                                                    "-o", os.path.join(OUT, "loadouts", kit + ".ini")]))
    models = sorted({m.group(1) for ln in open(os.path.join("game", "items", "swg_catalog.ini"), encoding="utf-8")
                     for m in [re.match(r"^Model\s*=\s*characters/([a-z0-9_]+)/", ln)] if m} - {"stormtrooper"})
    for name in models:
        steps.append(Step("bf2", "character " + name, [tool("bf2tool"), "export-character", bf2, name.split("_")[0],
                                                       name, "-o", os.path.join(OUT, "characters", name)]))

    # maps: conquest (the battlefield files' lines; a Galactic Civil War file's
    # wins over its Clone Wars copy), the other modes, space
    conquest = {}
    for bf in bfs:
        for ln in open(bf, encoding="utf-8", errors="replace"):
            if re.match(r"^;\s*bf2tool export-map ", ln) and "<scratch>" not in ln and "<map>" not in ln:
                argv = recorded(ln, bf2)
                out = argv[argv.index("-o") + 1]
                if out not in conquest or not bf.endswith("_cw.ini"):
                    conquest[out] = argv
    for out, argv in sorted(conquest.items()):
        steps.append(Step("maps", "map " + os.path.basename(out), argv))
    for ln in open(os.path.join(HERE, "mode_exports.txt"), encoding="utf-8"):
        if not ln.startswith("bf2tool export-map"):
            continue
        argv = recorded(ln, bf2)
        mp, mode = argv[3], argv[argv.index("--mode") + 1]
        o = argv.index("-o")
        argv[o + 1] = os.path.join(scratch, "%s_%s" % (mp, mode))
        steps.append(Step("maps", "mode %s %s" % (mp, mode), argv, after=("copy_mode", mp, mode, argv[o + 1])))
    steps.append(Step("maps", "space maps", expand="space_maps"))

    # vehicles: the battlefields' lists ("; for <side> <class>, ..."), then the space maps' own
    veh = set()
    for bf in bfs:
        for ln in open(bf, encoding="utf-8", errors="replace"):
            m = re.match(r"^;\s*for (.+?)\.\s*(A spawn|$)", ln)
            if m:
                veh |= {tuple(p.split()) for p in m.group(1).split(",") if len(p.split()) == 2}
            m = re.match(r'^;\s*bf2tool export-vehicle "[^"]+" ([a-z]+) ([a-z0-9_]+) ', ln)
            if m:
                veh.add((m.group(1), m.group(2)))
    for side, cls in sorted(veh):
        steps.append(Step("vehicles", "vehicle " + cls, [tool("bf2tool"), "export-vehicle", bf2, side, cls, "-o",
                                                         os.path.join(OUT, "vehicles", cls)]))
    steps.append(Step("vehicles", "space vehicles", expand="space_vehicles"))

    # swg
    anims = []
    for bank in ["human_0", "human_1", "human_2", "human_3", "human_4"]:
        anims += ["--bf2-bank", bank]
    for a in RIFLE + PISTOL + BAZOOKA + RIDER:
        anims += ["--anim", a]
    steps.append(Step("swg", "species", [tool("swgtool"), "export-species", swg, os.path.join(OUT, "species"),
                                         "--bf2-lvl", ingame]))
    st = [tool("swgtool"), "export-character", swg, os.path.join(OUT, "characters", "stormtrooper"), "--name",
          "stormtrooper"]
    for s in STORMTROOPER:
        st += ["--sat", "appearance/%s.sat" % s]
    steps.append(Step("swg", "stormtrooper", st + ["--weapon", "appearance/lod/wp_carbine_e11.lod", "--bf2-lvl",
                                                   ingame] + anims))
    for n in NATIVES:
        steps.append(Step("swg", "native " + n, [tool("swgtool"), "export-character", swg,
                                                 os.path.join(OUT, "characters", n), "--name", n, "--sat",
                                                 "appearance/%s_m.sat" % n, "--bf2-lvl", ingame] + anims))
    steps.append(Step("swg", "items", [tool("swgtool"), "export-items", swg, OUT]))
    steps.append(Step("swg", "swg vehicles", [tool("swgtool"), "export-vehicles", swg,
                                              os.path.join(OUT, "swg_vehicles")]))
    me = os.path.join(OUT, "terrain", "tatooine_mos_eisley")  # the 4 km test region (tests, net_demo)
    steps.append(Step("swg", "mos eisley bake", [tool("swgtool"), "bake-terrain", swg, me, "--trn",
                                                 "terrain/tatooine.trn", "--center", "3500", "-4800", "--samples",
                                                 "1025", "--spacing", "4"]))
    steps.append(Step("swg", "mos eisley city", [tool("swgtool"), "export-city", swg, me, "--snapshot",
                                                 "snapshot/tatooine.ws", "--center", "3500", "-4800", "--radius",
                                                 "700", "--hub", os.path.join("game", "hubs", "mos_eisley.ini")]))
    steps.append(Step("swg", "swg audio", [tool("swgtool"), "extract-list", swg,
                                           os.path.join("game", "audio", "swg_audio_files.txt"),
                                           os.path.join(OUT, "audio", "swg")]))

    # planets
    planets, cur = [], None
    for ln in open(os.path.join("game", "hubs", "galaxy.ini"), encoding="utf-8"):
        s = ln.strip()
        if s.startswith("["):
            cur = {} if s == "[planet]" else None
            if cur is not None:
                planets.append(cur)
        elif cur is not None and "=" in s and not s.startswith(";"):
            k, v = s.split("=", 1)
            cur.setdefault(k.strip(), v.strip())
    for p in planets:
        slug = p["Bake"].rsplit("/", 1)[-1]
        prefix = os.path.join(OUT, "terrain", slug)
        steps.append(Step("planets", "bake " + slug, [tool("swgtool"), "bake-terrain", swg, prefix, "--trn",
                                                      "terrain/%s.trn" % slug, "--center", "0", "0", "--samples",
                                                      str(PLANET_SAMPLES), "--spacing", str(PLANET_SPACING)]))
        hub = os.path.join("game", "hubs", p["Hub"])
        hv = first_section(hub, "hub")
        c = (hv.get("Center") or hv["Spawn"]).split()
        city = [tool("swgtool"), "export-city", swg, prefix, "--snapshot", "snapshot/%s.ws" % slug,
                "--area", hv["Name"], c[0], c[1], hv["Radius"]]
        hubs = [hub]
        for t in ini_values(hub, "Town"):
            tf = os.path.join("game", "hubs", t)
            if os.path.exists(tf):
                tv = first_section(tf, "town")
                tc = tv["Center"].split()
                city += ["--area", tv["Name"], tc[0], tc[1], tv["Radius"]]
                hubs.append(tf)
            else:
                print("warning: %s names a missing town file %s" % (hub, tf), file=sys.stderr)
        for h in hubs:
            city += ["--hub", h]
        steps.append(Step("planets", "city " + slug, city))
        steps.append(Step("planets", "people " + slug, [tool("swgtool"), "export-people", swg, prefix, "--hub", hub,
                                                        "--bf2-lvl", ingame]))
    steps.append(Step("check", "check", expand="check"))
    return steps


def space_map_steps(bf2):
    return [Step("maps", "space map " + mp, argv) for mp, argv in
            space_assault.commands(bf2, os.path.join(OUT, "maps"), tool("bf2tool"))]


def space_vehicle_steps(bf2, done=None):
    # Each space map's spawns (Class = <side> <atk|def> <class>) and turrets
    # (Class = <class>) not exported yet (or not in `done`). A starfighter is in
    # its side's file; a capital ship's turret is the map's own (spa/<map>.lvl).
    if done is None:
        done = {os.path.basename(p) for p in glob.glob(os.path.join(OUT, "vehicles", "*"))}
    found = {}
    for mp, _era in space_assault.MAPS:
        for f in glob.glob(os.path.join(OUT, "maps", mp, "*.ini")):
            for ln in open(f, encoding="utf-8", errors="replace"):
                m = re.match(r"^\s*Class\s*=\s*(.+?)\s*$", ln)
                if not m:
                    continue
                toks = m.group(1).split()
                if len(toks) >= 3:
                    found.setdefault(toks[2], (toks[0], mp))
                elif len(toks) == 1:
                    found.setdefault(toks[0], (None, mp))
    steps = []
    for cls, (side, mp) in sorted(found.items()):
        if cls in done:
            continue
        if side is None:
            # tur_*: side/tur.lvl's; anything else is a capital ship's, in the map's own level.
            side = "tur" if cls.startswith("tur_") else "spa/" + mp
        steps.append(Step("vehicles", "vehicle " + cls, [tool("bf2tool"), "export-vehicle", bf2, side, cls, "-o",
                                                         os.path.join(OUT, "vehicles", cls)]))
    return steps


def check():
    """The imported_assets paths the repo names that don't exist."""
    needed = required_paths()
    missing = [(p, why) for p, why in needed if not (os.path.exists(p) or os.path.exists(p + ".r32"))]
    return len(needed), sorted(set(missing))


def required_paths():
    """[(path, why)]: every imported_assets path the repo's data and code name
    (a terrain bake's prefix counts if <prefix>.r32 exists)."""
    needed = []

    def need(path, why):
        needed.append((path, why))

    for f in glob.glob(os.path.join("game", "**", "*.ini"), recursive=True):
        if os.path.abspath(f).startswith(os.path.abspath(OUT)):
            continue
        for n, ln in enumerate(open(f, encoding="utf-8", errors="replace"), 1):
            if ln.lstrip().startswith(";"):
                continue
            for m in re.finditer(r"((?:\.\./)+imported_assets/[A-Za-z0-9_./{}-]+)", ln):
                p = os.path.normpath(os.path.join(os.path.dirname(f), m.group(1)))
                for g in (["m", "f"] if "{g}" in p else [""]):
                    need(p.replace("{g}", g), "%s:%d" % (f, n))
    for f in glob.glob(os.path.join("game", "items", "*.ini")):
        for n, ln in enumerate(open(f, encoding="utf-8", errors="replace"), 1):
            m = re.match(r"^Model\s*=\s*(.+?)\s*$", ln)
            for model in (m.group(1).split(",") if m else []):
                for g in (["m", "f"] if "{g}" in model else [""]):
                    need(os.path.join(OUT, model.strip().replace("{g}", g)), "%s:%d" % (f, n))
    for f in glob.glob(os.path.join("game", "battlefields", "*.ini")):
        for ln in open(f, encoding="utf-8", errors="replace"):
            m = re.match(r"^;\s*for (.+?)\.\s*(A spawn|$)", ln)
            for p in (m.group(1).split(",") if m else []):
                if len(p.split()) == 2:
                    cls = p.split()[1]
                    need(os.path.join(OUT, "vehicles", cls, cls + ".ini"), f)
    for p in ["characters/stormtrooper/stormtrooper.gltf", "audio/sounds.json", "hud/hud.json",
              "vehicles/imp_fly_tiefighter_sc/imp_fly_tiefighter.gltf",
              "vehicles/all_fly_xwing_sc/all_fly_xwing.gltf", "terrain/tatooine_mos_eisley.objects.json"]:
        need(os.path.join(OUT, p), "game code")
    for sp in ["human", "zabrak", "twilek", "rodian", "moncal", "trandoshan", "wookiee", "ithorian", "bothan",
               "sullustan"]:
        for g in ["male", "female"]:
            need(os.path.join(OUT, "species", "%s_%s" % (sp, g), "%s_%s.gltf" % (sp, g)), "species")
    return needed


def run(steps, log_dir):
    os.makedirs(log_dir, exist_ok=True)
    n, failed = 0, []
    queue = list(steps)
    while queue:
        s = queue.pop(0)
        if s.expand == "space_maps":
            queue[0:0] = space_map_steps(ARGS.bf2)
            continue
        if s.expand == "space_vehicles":
            queue[0:0] = space_vehicle_steps(ARGS.bf2)
            continue
        if s.expand == "check":
            count, missing = check()
            for path, why in missing:
                print("missing %s (named in %s)" % (path, why))
            print("check: %d paths, %d missing" % (count, len(missing)))
            if missing:
                failed.append("check")
            continue
        n += 1
        log = os.path.join(log_dir, "%03d_%s.log" % (n, re.sub(r"[^A-Za-z0-9_.-]+", "_", s.label)))
        t = time.time()
        with open(log, "wb") as f:
            f.write((subprocess.list2cmdline(s.argv) + "\n\n").encode())
            f.flush()
            rc = subprocess.run(s.argv, stdout=f, stderr=subprocess.STDOUT).returncode
        note = ""
        if rc == 0 and s.after and s.after[0] == "copy_mode":
            _, mp, mode, tmp = s.after
            src = os.path.join(tmp, "%s_%s.ini" % (mp, mode))
            if os.path.exists(src):
                os.makedirs(os.path.join(OUT, "maps", mp), exist_ok=True)
                shutil.copy2(src, os.path.join(OUT, "maps", mp))
            else:
                rc, note = 1, " (it wrote no %s_%s.ini)" % (mp, mode)
        print("%s %-8s %-40s %s %5.1fs%s" % (time.strftime("%H:%M:%S"), s.phase, s.label,
                                             "ok  " if rc == 0 else "FAIL", time.time() - t, note), flush=True)
        if rc != 0:
            failed.append("%s (log: %s)" % (s.label, log))
    return failed


# Bump a tool's number when its exports change (new data in an existing export,
# a fixed exporter): every recipe step it runs gets a new key, so launchers
# export them again after updating. New or changed commands need no bump.
EXPORT_VERSIONS = {"bf2tool": 1, "swgtool": 1}
# Repo files a command reads without naming them.
IMPLICIT_INPUTS = {"export-items": [os.path.join("game", "items", "swg_catalog.ini")]}


def step_key(argv, copy):
    """A recipe step's key: its command, its tool's export version, and the
    contents of the repo files it reads (those its argv names, a hub's Town
    files, IMPLICIT_INPUTS), so a changed hub or catalog exports again."""
    h = hashlib.sha256(json.dumps([argv, copy]).encode())
    exe = os.path.splitext(os.path.basename(argv[0]))[0]
    h.update(("%s=%d" % (exe, EXPORT_VERSIONS.get(exe, 0))).encode())
    inputs = [t for t in argv[1:] if not t.startswith("{") and os.path.isfile(t)]
    for t in list(inputs):
        if os.path.normpath(t).startswith(os.path.join("game", "hubs") + os.sep):
            inputs += [os.path.join("game", "hubs", v) for v in ini_values(t, "Town")]
    inputs += IMPLICIT_INPUTS.get(argv[1] if len(argv) > 1 else "", [])
    for f in sorted(set(inputs)):
        if os.path.isfile(f):
            h.update(f.replace("\\", "/").encode())
            with open(f, "rb") as fh:
                h.update(hashlib.sha256(fh.read().replace(b"\r\n", b"\n")).digest())
    return h.hexdigest()[:16]


def emit_recipe(path, bf2):
    """The launcher's recipe (docs/DISTRIBUTION.md): every step as argv with
    placeholders, run from the install dir (laid out like the repo root):
    {tools} its tools folder, {bf2} Battlefront II's _lvl_pc, {swg} the Galaxies
    sources, {scratch} a temp folder. The space maps' options and the space
    vehicles are worked out here, from this machine's Battlefront II and
    imported_assets (Battlefront's data is the same on every release)."""
    tools_prefix = os.path.dirname(tool("bf2tool"))

    def template(argv):
        out = []
        for t in argv:
            t = t.replace(tools_prefix + os.sep, "{tools}" + os.sep)
            if t.startswith(bf2):
                t = "{bf2}" + t[len(bf2):]
            out.append(t)
        return out

    steps = plan(bf2, "{swg}", "{scratch}")
    planned_vehicles = {s.argv[4] for s in steps if s.argv and s.label.startswith("vehicle ")}
    entries, required = [], []
    for s in steps:
        if s.expand == "space_maps":
            group = space_map_steps(bf2)
        elif s.expand == "space_vehicles":
            group = space_vehicle_steps(bf2, done=planned_vehicles)
        elif s.expand == "check":
            required = sorted({p for p, _why in required_paths()})
            continue
        else:
            group = [s]
        for g in group:
            e = {"phase": g.phase, "label": g.label, "argv": template(g.argv)}
            if g.after and g.after[0] == "copy_mode":
                _, mp, mode, tmp = g.after
                e["copy"] = {"from": os.path.join(tmp, "%s_%s.ini" % (mp, mode)),
                             "to_dir": os.path.join(OUT, "maps", mp)}
            e["key"] = step_key(e["argv"], e.get("copy"))
            entries.append(e)
    try:
        commit = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    except OSError:
        commit = ""
    recipe = {"format": 1, "commit": commit, "out": OUT, "steps": entries, "required": required}
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(recipe, f, indent=1)
        f.write("\n")
    print("wrote %s: %d steps, %d required paths" % (path, len(entries), len(required)))


def main():
    global ARGS
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("phases", nargs="*", metavar="phase", help="any of: " + ", ".join(PHASES) + " (default: all)")
    ap.add_argument("--bf2", default=DEFAULT_BF2, help="Battlefront II's GameData\\Data\\_lvl_pc")
    ap.add_argument("--swg", help="a folder of Galaxies .tre archives swgtool reads (not encrypted ones)")
    ap.add_argument("--list", action="store_true", help="print the commands, run nothing")
    ap.add_argument("--logs", default=None, help="where each step's log goes (default: a temp folder)")
    ap.add_argument("--emit-recipe", metavar="FILE", help="write the launcher's recipe (JSON) and run nothing")
    ARGS = ap.parse_args()
    phases = ARGS.phases or PHASES
    if set(phases) - set(PHASES):
        sys.exit("unknown phase %s (phases: %s)" % (", ".join(sorted(set(phases) - set(PHASES))), ", ".join(PHASES)))
    if not os.path.exists(os.path.join("game", "battlefields")):
        sys.exit("run from the repository root")
    if not os.path.exists(os.path.join(ARGS.bf2, "ingame.lvl")):
        sys.exit("no Battlefront II at %s: pass --bf2 <...\\GameData\\Data\\_lvl_pc>" % ARGS.bf2)
    if ARGS.emit_recipe:
        return emit_recipe(ARGS.emit_recipe, ARGS.bf2)
    if ({"swg", "planets"} & set(phases)) and not ARGS.swg:
        sys.exit("the swg and planets phases need --swg <folder of Galaxies .tre archives>")
    scratch = tempfile.mkdtemp(prefix="swm_modes_")
    steps = [s for s in plan(ARGS.bf2, ARGS.swg or "<swg>", scratch) if s.phase in phases]
    if ARGS.list:
        for s in steps:
            if s.expand == "space_maps":
                for m in space_map_steps(ARGS.bf2):
                    print(m.phase, "|", m.label, "|", subprocess.list2cmdline(m.argv))
            else:
                print(s.phase, "|", s.label, "|", subprocess.list2cmdline(s.argv) if s.argv else
                      "(%s: worked out when it runs)" % s.expand)
        return
    for exe in ["bf2tool", "swgtool"]:
        if not os.path.exists(tool(exe)):
            sys.exit("no %s: build it first (cmake --build build\\msvc --config Release --target %s)" % (tool(exe), exe))
    log_dir = ARGS.logs or tempfile.mkdtemp(prefix="swm_rebuild_logs_")
    print("logs:", log_dir)
    failed = run(steps, log_dir)
    shutil.rmtree(scratch, ignore_errors=True)
    if failed:
        print("\n%d failed:" % len(failed))
        for f in failed:
            print("  " + f)
        sys.exit(1)
    print("\ndone")


if __name__ == "__main__":
    main()
