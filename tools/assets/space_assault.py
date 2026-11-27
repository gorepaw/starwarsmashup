"""Space Assault map exports, read off Battlefront II's mission scripts.

  python -I tools/assets/space_assault.py <_lvl_pc dir> <out maps dir>   print the commands

For each space map it decodes the mission script (spa<N>g_ass on the Galactic
Civil War maps, spa<N>c_ass on the Clone Wars ones) and the shared
ObjectiveSpaceAssault script (each critical system's points), and builds
bf2tool export-map's options:

  --score-limit       180 (the engine's SpaceAssaultGetScoreLimit)
  --extents           SetWorldExtents
  --fly-height        SetMinFlyHeight, SetMaxFlyHeight
  --landing           AddLandingRegion
  --death             AddDeathRegion
  --turret-mainframe  LinkedTurrets: its mainframe and the turrets it runs
  --shield            LinkedShields: its controller and what it shields
  --link              LinkedDestroyables: a set of one object takes the other sets with it
  --system            SetupAllCriticalSystems: each critical system with its owner
                      (the team other than the side scoring it), its kind
                      (engines, lifesupport, bridge, comm, sensors, frigates;
                      lifesupport_int, engines_int inside), and points (the
                      gSpaceAssaultSystems_template's for the scoring side;
                      internal systems 18). Plus each shield generator
                      (shieldgen) and turret mainframe (autodefense), worth 0.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bf2_lua  # noqa: E402

# Map, the era of its mission script (g: Galactic Civil War, c: Clone Wars):
# the era game/battlefields/space_*.ini fights it in.
MAPS = [("spa1", "g"), ("spa3", "c"), ("spa6", "c"), ("spa7", "c"), ("spa8", "g"), ("spa9", "g")]
SCORE_LIMIT = 180
INTERNAL_POINTS = 18  # ObjectiveSpaceAssault: AddCriticalSystem(internal, 18)
LABELS = {"engines": "Engine", "lifesupport": "Life Support", "bridge": "Bridge", "comm": "Communications",
          "sensors": "Sensors", "frigates": "Frigate"}
GLOBALS = {}  # the script's global assignments, for @name references


def get(t, key, default=None):
    if isinstance(t, bf2_lua.Table):
        for k, v in t.items:
            if k == key:
                return v
    return default


def resolve(t):
    if isinstance(t, str) and t.startswith("\x00@") and t[2:] in GLOBALS:
        return GLOBALS[t[2:]]
    return t


def values(t):
    # A table's array part, or a single string as a list.
    t = resolve(t)
    if isinstance(t, bf2_lua.Table):
        return [v for k, v in t.items if isinstance(k, float)]
    return [t] if isinstance(t, str) and not t.startswith("\x00") else []


def points_template(lvl):
    for ev in bf2_lua.script_events(os.path.join(lvl, "mission.lvl"), script="objectivespaceassault"):
        if ev[0] == "global" and ev[1] == "gSpaceAssaultSystems_template":
            return ev[2]
    raise RuntimeError("no gSpaceAssaultSystems_template in mission.lvl's objectivespaceassault")


def internal_kind(name):
    n = name.lower()
    if "life" in n:
        return "lifesupport_int", "Life Support Console"
    if "engine" in n:
        return "engines_int", "Engine Console"
    return "internal", "Internal System"


def map_options(lvl, mp, era, tmpl):
    script = "%s%s_ass" % (mp, era)
    events = bf2_lua.script_events(os.path.join(lvl, "mission.lvl"), sublevel=script)
    if not events:
        raise RuntimeError("mission.lvl has no " + script)
    GLOBALS.clear()
    teams = {}  # side ("imp", "all", "rep", "cis") -> team number
    for ev in events:
        if ev[0] == "global":
            GLOBALS[ev[1]] = ev[2]
            if ev[1] in ("IMP", "ALL", "REP", "CIS") and isinstance(ev[2], float):
                teams[ev[1].lower()] = int(ev[2])

    owners = {}  # object (lowercase) -> its team

    def symbol_team(v):
        return teams.get(v.lstrip("\x00@").lower()) if isinstance(v, str) else None

    opts = ["--score-limit", str(SCORE_LIMIT)]
    lo = hi = None
    systems, seen = [], set()
    mainframes, shields = [], []

    def system(obj, kind, points, label, team):
        n = obj.lower()
        if n in seen:
            return
        seen.add(n)
        if team is None:
            team = owners.get(n) or teams[n.split("_")[0]]
        owners[n] = team
        systems.append("%s=%d:%s:%d:%s" % (obj, team, kind, points, label))

    for ev in events:
        if ev[0] != "call":
            continue
        name, args = ev[1].lstrip("@"), ev[2]
        if name == "SetWorldExtents":
            opts += ["--extents", "%g" % args[0]]
        elif name == "SetMinFlyHeight":
            lo = args[0]
        elif name == "SetMaxFlyHeight":
            hi = args[0]
        elif name == "AddLandingRegion":
            opts += ["--landing", args[0]]
        elif name == "AddDeathRegion":
            opts += ["--death", args[0]]
        elif name.endswith("LinkedTurrets:New"):
            t = args[1]
            mf = get(t, "mainframe")
            opts += ["--turret-mainframe", "%s=%s" % (mf, ",".join(dict.fromkeys(values(get(t, "turrets")))))]
            mainframes.append((mf, symbol_team(get(t, "team"))))
        elif name.endswith("LinkedShields:New"):
            t = args[1]
            ctrl = get(t, "controllerObject")
            objs = values(get(t, "objs"))
            opts += ["--shield", "%s=%s" % (ctrl, ",".join(objs))]
            shields.append((ctrl, objs))
        elif name.endswith("LinkedDestroyables:New"):
            sets = [values(s) for s in values(get(args[1], "objectSets"))]
            for i, a in enumerate(sets):
                if len(a) == 1:  # a set of several goes only when all of it goes
                    opts += ["--link", "%s=%s" % (a[0], ",".join(o for j, b in enumerate(sets) if j != i for o in b))]
        elif name.endswith(":SetupAllCriticalSystems"):
            side, t = args[1], args[2]
            side_tmpl = get(tmpl, side)
            other = [n for k, n in teams.items() if k != side.lower()]
            ship_team = other[0] if len(other) == 1 else None
            for key in ["engines", "lifesupport", "bridge", "comm", "sensors", "frigates"]:
                objs = values(get(t, key))
                pts = int(get(get(side_tmpl, key), "pts", 0))
                for n, obj in enumerate(objs, 1):
                    system(obj, key, pts, LABELS[key] + (" %d" % n if len(objs) > 1 else ""), ship_team)
            for obj in values(get(t, "internalSys")):
                kind, label = internal_kind(obj)
                system(obj, kind, INTERNAL_POINTS, label, ship_team)
    for mf, team in mainframes:
        system(mf, "autodefense", 0, "Turret Mainframe", team)
    for ctrl, objs in shields:
        known = [owners[o.lower()] for o in objs if o.lower() in owners]
        system(ctrl, "shieldgen", 0, "Shield Generator", known[0] if known else None)
    if lo is not None and hi is not None:
        opts += ["--fly-height", "%g,%g" % (lo, hi)]
    for s in systems:
        opts += ["--system", s]
    return opts


def commands(lvl, out_maps, bf2tool="bf2tool"):
    """[(map, argv)] for every space map."""
    tmpl = points_template(lvl)
    return [(mp, [bf2tool, "export-map", lvl, mp, "-o", os.path.join(out_maps, mp), "--mode", "assault"] +
             map_options(lvl, mp, era, tmpl)) for mp, era in MAPS]


if __name__ == "__main__":
    for mp, argv in commands(sys.argv[1], sys.argv[2]):
        print(" ".join('"%s"' % a if " " in a else a for a in argv))
