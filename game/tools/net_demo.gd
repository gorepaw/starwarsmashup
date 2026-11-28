# Dev CLI wrapper around res://scenes/game_session.tscn.
#
#   godot --path game --script res://tools/net_demo.gd -- <bake_prefix> <character.gltf>
#         [--server a.b.c.d:port] [--host <port>] [--db <file>] [--spawn <x> <z>] [--name <name>]
#         [--building <building.gltf> <x> <y> <z> <yaw>] [--bot circle|chase|route|conquest] [--route <x,z;x,z;...>] [--look <x,z>]
#         [--loadout <file.ini>] [--battlefield <file.ini>] [--bots <n>] [--hub <file.ini>] [--lag <ms>] [--jitter <ms>]
#         [--loss <0..1>] [--seconds <n>] [--shots <output_prefix>] [--solo] [--terrain3d] [--first-person]
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2 or not ClassDB.class_exists("SwbfoClient"):
		printerr("usage: -- <bake_prefix> <character.gltf> [--server addr:port] [--host port] [--spawn x z] ",
				"[--name n] [--building gltf x y z yaw] [--bot circle|chase|route|conquest] [--route x,z;...] ",
				"[--loadout ini] [--battlefield ini] [--bots n] [--hub ini] [--lag ms] ",
				"[--jitter ms] [--loss 0..1] [--seconds n] [--shots prefix] [--solo] [--terrain3d]",
				"   (needs the swbfo GDExtension)")
		quit(2)
		return

	var route: Array[Vector2] = []
	for point in _option(args, "--route", "").split(";", false):
		var xz := point.split(",")
		route.append(Vector2(-float(xz[0]), float(xz[1])))

	var look := Vector2.INF
	var look_parts := _option(args, "--look", "").split(",")
	if look_parts.size() == 2:
		look = Vector2(-float(look_parts[0]), float(look_parts[1]))

	var building: Array = []
	var b_at := args.find("--building")
	if b_at >= 0 and b_at + 5 < args.size():
		building = [args[b_at + 1], args[b_at + 2], args[b_at + 3], args[b_at + 4], args[b_at + 5]]

	var spawn: Variant = null
	var s_at := args.find("--spawn")
	if s_at >= 0 and s_at + 2 < args.size():
		spawn = Vector2(float(args[s_at + 1]), float(args[s_at + 2]))

	var cfg := {
		"bake": args[0],
		"character_path": args[1],
		"server": _option(args, "--server", "127.0.0.1:7777"),
		"host": _option(args, "--host", ""),
		"db": _option(args, "--db", ""),
		"spawn": spawn,
		"name": _option(args, "--name", "player"),
		"building": building,
		"bot": _option(args, "--bot", ""),
		"route": route,
		"look": look,
		"loadout": _option(args, "--loadout", ""),
		"battlefield": _option(args, "--battlefield", ""),
		"bots": int(_option(args, "--bots", "0")),
		"hub": _option(args, "--hub", ""),
		"lag": float(_option(args, "--lag", "0")),
		"jitter": float(_option(args, "--jitter", "0")),
		"loss": float(_option(args, "--loss", "0")),
		"seconds": float(_option(args, "--seconds", "0")),
		"shots": _option(args, "--shots", ""),
		"solo": args.has("--solo"),
		"terrain3d": args.has("--terrain3d"),
		"first_person": args.has("--first-person"),
		"return_to_menu": false,
	}

	var packed := load("res://scenes/game_session.tscn") as PackedScene
	if packed == null:
		printerr("cannot load res://scenes/game_session.tscn")
		quit(1)
		return
	GameSession.pending_config = cfg
	var session: GameSession = packed.instantiate() as GameSession
	session.finished.connect(func(code: int) -> void: quit(code))
	root.add_child(session)


func _option(args: PackedStringArray, flag: String, fallback: String) -> String:
	var at := args.find(flag)
	return args[at + 1] if at >= 0 and at + 1 < args.size() else fallback
