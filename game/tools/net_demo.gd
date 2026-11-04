# Dev tool: a networked third-person client on baked terrain. Movement is
# simulated in C++ (core/): the server is authoritative and this client
# predicts its own character (SwmClient). This script only draws the result
# and turns keys or a bot into input.
#
#   godot --path game --script res://tools/net_demo.gd -- <bake_prefix> <character.gltf>
#         [--server a.b.c.d:port] [--host <port>] [--spawn <x> <z>] [--name <name>]
#         [--bot circle|chase] [--lag <ms>] [--jitter <ms>] [--loss <0..1>]
#         [--seconds <n>] [--shots <output_prefix>] [--terrain3d]
#
# Connects to a running server (default 127.0.0.1:7777; see server/ for
# swm_server), or with --host runs one in this process and joins it. --spawn
# is the server's spawn point in Galaxies coordinates (default: the bake's
# center). Both server and client need the same bake.
#
# Controls: WASD to move, mouse to turn, Shift to sprint, Space to jump, Esc
# to free the mouse (Esc again quits). --bot drives the character instead:
# "circle" runs in circles, "chase" runs after the nearest other player.
# --lag/--jitter/--loss simulate a bad connection on this client.
#
# With --seconds, quits after that long and prints network stats. Exits 1 if
# it never spawned or never saw another player. --shots saves four evenly
# spaced screenshots. Needs a real window.
extends SceneTree

const TerrainTool = preload("res://tools/render_terrain.gd")
const Terrain3DBake = preload("res://tools/terrain3d_bake.gd")

const RUN_ANIMATION := "human_rifle_stand_runforward"
const IDLE_ANIMATION := "human_rifle_stand_idle_emote_full"
const ANIMATION_RUN_SPEED := 4.5  # m/s the run cycle matches
const MOUSE_SENSITIVITY := 0.003
const CAMERA_DISTANCE := 4.5
const CAMERA_HEIGHT := 2.0
const CAMERA_SHOULDER := 0.7  # meters right of the character
const SHOT_COUNT := 4

var _client: Object
var _server: Object
var _character_path := ""
var _bot := ""
var _seconds := 0.0
var _shots := ""
var _shots_taken := 0
var _elapsed := 0.0
var _yaw := 0.0
var _pitch := -0.2
var _mouse_captured := false
var _own: Dictionary = {}       # {model, player}
var _remotes: Dictionary = {}   # entity id -> {model, player}
var _saw_remote := false
var _camera: Camera3D
var _hud: Label
var _pending_shot := ""
var _shot_delay := 0


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2 or not ClassDB.class_exists("SwmClient"):
		printerr("usage: -- <bake_prefix> <character.gltf> [--server addr:port] [--host port] [--spawn x z] ",
				"[--name n] [--bot circle|chase] [--lag ms] [--jitter ms] [--loss 0..1] [--seconds n] ",
				"[--shots prefix] [--terrain3d]   (needs the swm GDExtension)")
		quit(2)
		return
	var bake := args[0]
	_character_path = args[1]
	var meta: Variant = JSON.parse_string(FileAccess.get_file_as_string(bake + ".json"))
	if meta == null:
		printerr("cannot load terrain ", bake)
		quit(1)
		return
	_bot = _option(args, "--bot", "")
	_seconds = float(_option(args, "--seconds", "0"))
	_shots = _option(args, "--shots", "")
	var bake_path := ProjectSettings.globalize_path("res://" + bake) if not bake.is_absolute_path() else bake

	var server := _option(args, "--server", "127.0.0.1:7777")
	var host := _option(args, "--host", "")
	if host != "":
		_server = ClassDB.instantiate("SwmServer")
		var spawn := _spawn(args, meta)
		if _server.start(int(host), bake_path, spawn) != OK:
			quit(1)
			return
		server = "127.0.0.1:%d" % _server.get_port()
		print("hosting on port ", _server.get_port())

	_client = ClassDB.instantiate("SwmClient")
	_client.set_simulated_network(float(_option(args, "--lag", "0")), float(_option(args, "--jitter", "0")),
			float(_option(args, "--loss", "0")))
	if _client.start(server, bake_path, _option(args, "--name", "player")) != OK:
		quit(1)
		return

	_add_terrain(bake, meta, args.has("--terrain3d"))
	_own = _add_character()
	if _own.is_empty():
		quit(1)
		return
	_setup_scene()
	root.window_input.connect(_on_input)
	DisplayServer.window_set_size(Vector2i(1280, 720))
	if _bot == "":
		_capture_mouse(true)


func _process(delta: float) -> bool:
	_elapsed += delta
	if _server != null:
		_server.update()
	_client.set_input.callv(_controls())
	_client.update()

	if _client.is_spawned():
		var own: Dictionary = _client.get_presented()
		_pose(_own, own)
		_update_remotes()
		_update_camera(own["position"])
	_update_hud()

	if _pending_shot != "":
		_shot_delay -= 1
		if _shot_delay <= 0:
			root.get_texture().get_image().save_png(_pending_shot)
			print("wrote ", _pending_shot)
			_pending_shot = ""
	elif _shots != "" and _seconds > 0 and _shots_taken < SHOT_COUNT \
			and _elapsed >= _seconds * (_shots_taken + 1) / (SHOT_COUNT + 0.5):
		_shots_taken += 1
		_pending_shot = "%s%02d.png" % [_shots, _shots_taken]
		_shot_delay = 2

	if _seconds > 0 and _elapsed >= _seconds and _pending_shot == "":
		return _finish()
	if _client.get_status() == "timed_out":
		printerr("connection timed out")
		return _finish()
	return false


func _finish() -> bool:
	var stats: Dictionary = _client.get_stats()
	print("status %s, entity %d, %d ticks, %d snapshots, %d corrections (max %.3f m), rtt %.0f ms, saw other players: %s" % [
			_client.get_status(), _client.get_entity_id(), stats.get("ticks", 0), stats.get("snapshots", 0),
			stats.get("corrections", 0), stats.get("max_correction", 0.0), stats.get("rtt_ms", 0.0), _saw_remote])
	var spawned: bool = _client.is_spawned()
	_leave()
	quit(0 if spawned and _saw_remote else 1)
	return true


# Says goodbye, giving a simulated-lag link a moment to send it.
func _leave() -> void:
	_client.leave()
	for _i in 20:
		_client.update()
		if _server != null:
			_server.update()
		OS.delay_msec(10)


# [forward, right, yaw, sprint, jump] for SwmClient.set_input.
func _controls() -> Array:
	match _bot:
		"circle":
			_yaw = _elapsed * 0.5
			return [1.0, 0.0, _yaw, fmod(_elapsed, 8.0) < 4.0, fmod(_elapsed, 5.0) < 0.05]
		"chase":
			var target := _nearest_remote()
			if target.is_empty():
				return [0.0, 0.0, _yaw, false, false]
			var to: Vector3 = target["position"] - _client.get_presented()["position"]
			_yaw = atan2(-to.x, -to.z)
			var far := Vector2(to.x, to.z).length() > 6.0
			return [1.0 if far else 0.0, 0.0, _yaw, Vector2(to.x, to.z).length() > 20.0, false]
	if not _mouse_captured:
		return [0.0, 0.0, _yaw, false, false]
	var forward := float(Input.is_physical_key_pressed(KEY_W)) - float(Input.is_physical_key_pressed(KEY_S))
	var right := float(Input.is_physical_key_pressed(KEY_D)) - float(Input.is_physical_key_pressed(KEY_A))
	return [forward, right, _yaw, Input.is_physical_key_pressed(KEY_SHIFT), Input.is_physical_key_pressed(KEY_SPACE)]


func _on_input(event: InputEvent) -> void:
	if _bot != "":
		return
	if event is InputEventMouseMotion and _mouse_captured:
		var motion := event as InputEventMouseMotion
		_yaw -= motion.relative.x * MOUSE_SENSITIVITY
		_pitch = clampf(_pitch - motion.relative.y * MOUSE_SENSITIVITY, -1.2, 0.6)
	elif event is InputEventMouseButton and event.pressed and not _mouse_captured:
		_capture_mouse(true)
	elif event is InputEventKey and event.pressed and event.physical_keycode == KEY_ESCAPE:
		if _mouse_captured:
			_capture_mouse(false)
		else:
			_leave()
			quit(0)


func _capture_mouse(captured: bool) -> void:
	_mouse_captured = captured
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if captured else Input.MOUSE_MODE_VISIBLE


func _nearest_remote() -> Dictionary:
	var own_position: Vector3 = _client.get_presented()["position"]
	var best: Dictionary = {}
	for remote: Dictionary in _client.get_remotes():
		if best.is_empty() or own_position.distance_to(remote["position"]) < own_position.distance_to(best["position"]):
			best = remote
	return best


func _update_remotes() -> void:
	var seen := {}
	for remote: Dictionary in _client.get_remotes():
		var id: int = remote["id"]
		seen[id] = true
		_saw_remote = true
		if not _remotes.has(id):
			var character := _add_character()
			if character.is_empty():
				continue
			_remotes[id] = character
		_pose(_remotes[id], remote)
	for id: int in _remotes.keys():
		if not seen.has(id):
			(_remotes[id]["model"] as Node).queue_free()
			_remotes.erase(id)


func _pose(character: Dictionary, state: Dictionary) -> void:
	var model: Node3D = character["model"]
	model.position = state["position"]
	model.rotation.y = float(state["yaw"]) + PI  # the model faces +Z; yaw 0 faces -Z
	var player: AnimationPlayer = character["player"]
	if player == null:
		return
	var velocity: Vector3 = state["velocity"]
	var speed := Vector2(velocity.x, velocity.z).length()
	var animation := RUN_ANIMATION if speed > 0.5 else IDLE_ANIMATION
	if player.current_animation != animation and player.has_animation(animation):
		player.play(animation, 0.15)
	player.speed_scale = clampf(speed / ANIMATION_RUN_SPEED, 0.6, 1.9) if speed > 0.5 else 1.0


func _update_camera(target: Vector3) -> void:
	# Over the shoulder, facing where the character aims (as in Battlefront,
	# the character always faces the camera's direction).
	var basis := Basis(Vector3.UP, _yaw) * Basis(Vector3.RIGHT, _pitch)
	var pivot := target + Vector3.UP * CAMERA_HEIGHT * 0.8
	var position := pivot + basis * Vector3(CAMERA_SHOULDER, 0.3, CAMERA_DISTANCE)
	# Keep the camera above the ground.
	position.y = maxf(position.y, _client.get_ground_height(position.x, position.z) + 0.4)
	_camera.global_position = position
	_camera.look_at(pivot + basis * Vector3(CAMERA_SHOULDER, 0, -10), Vector3.UP)


func _update_hud() -> void:
	var stats: Dictionary = _client.get_stats()
	_hud.text = "%s | entity %d | %d other player(s) | rtt %.0f ms | corrections %d (max %.2f m)" % [
			_client.get_status(), _client.get_entity_id(), _remotes.size(), stats.get("rtt_ms", 0.0),
			stats.get("corrections", 0), stats.get("max_correction", 0.0)]


func _add_character() -> Dictionary:
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(_character_path, state) != OK:
		printerr("cannot load ", _character_path)
		return {}
	var model: Node3D = document.generate_scene(state)
	root.add_child(model)
	var player := model.find_child("AnimationPlayer", true, false) as AnimationPlayer
	if player != null:
		for animation_name in [RUN_ANIMATION, IDLE_ANIMATION]:
			if player.has_animation(animation_name):
				player.get_animation(animation_name).loop_mode = Animation.LOOP_LINEAR
	return {"model": model, "player": player}


func _add_terrain(bake: String, meta: Dictionary, use_terrain3d: bool) -> void:
	if use_terrain3d and Terrain3DBake.available():
		var terrain: Node3D = Terrain3DBake.create(root, bake, meta)
		terrain.collision_mode = 0  # movement uses the server's heights, not physics
		return
	if use_terrain3d:
		printerr("Terrain3D isn't installed (tools/install_terrain3d.ps1); using a mesh")
	var heights := FileAccess.get_file_as_bytes(bake + ".r32").to_float32_array()
	var mesh := MeshInstance3D.new()
	# Every sample, so the mesh matches the heights characters stand on.
	mesh.mesh = TerrainTool.build_mesh(heights, meta["samples"], meta["spacing"],
			Vector2(meta["origin_x"], meta["origin_z"]), 1)
	mesh.material_override = TerrainTool.build_material(bake, meta)
	root.add_child(mesh)


func _setup_scene() -> void:
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.62, 0.72, 0.85)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.5, 0.5, 0.55)
	environment.fog_enabled = true
	environment.fog_light_color = Color(0.75, 0.72, 0.68)
	environment.fog_density = 0.0004
	var world := WorldEnvironment.new()
	world.environment = environment
	root.add_child(world)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, -40, 0)
	sun.light_energy = 1.3
	sun.shadow_enabled = true
	root.add_child(sun)

	_camera = Camera3D.new()
	_camera.fov = 65
	_camera.near = 0.05
	_camera.far = 8000
	root.add_child(_camera)

	var layer := CanvasLayer.new()
	_hud = Label.new()
	_hud.position = Vector2(12, 8)
	_hud.add_theme_color_override("font_outline_color", Color.BLACK)
	_hud.add_theme_constant_override("outline_size", 4)
	layer.add_child(_hud)
	root.add_child(layer)


# The server's spawn point (Galaxies x, z): --spawn, or the bake's center.
func _spawn(args: PackedStringArray, meta: Dictionary) -> Vector2:
	var at := args.find("--spawn")
	if at >= 0 and at + 2 < args.size():
		return Vector2(float(args[at + 1]), float(args[at + 2]))
	var half: float = (int(meta["samples"]) - 1) * float(meta["spacing"]) / 2
	return Vector2(float(meta["origin_x"]) + half, float(meta["origin_z"]) + half)


func _option(args: PackedStringArray, flag: String, fallback: String) -> String:
	var at := args.find(flag)
	return args[at + 1] if at >= 0 and at + 1 < args.size() else fallback
