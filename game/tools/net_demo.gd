# Dev tool: a networked third-person client on baked terrain. Movement is
# simulated in C++ (core/): the server is authoritative and this client
# predicts its own character (SwmClient). This script only draws the result
# and turns keys or a bot into input.
#
#   godot --path game --script res://tools/net_demo.gd -- <bake_prefix> <character.gltf>
#         [--server a.b.c.d:port] [--host <port>] [--db <file>] [--spawn <x> <z>] [--name <name>]
#         [--building <building.gltf> <x> <y> <z> <yaw>] [--bot circle|chase|route] [--route <x,z;x,z;...>]
#         [--soldier <file.ini>] [--battlefield <file.ini>] [--bots <n>] [--lag <ms>] [--jitter <ms>]
#         [--loss <0..1>] [--seconds <n>] [--shots <output_prefix>] [--solo] [--terrain3d]
#
# Connects to a running server (default 127.0.0.1:7777; see server/ for
# swm_server), or with --host runs one in this process and joins it; --db
# makes that server save characters in a SQLite file (relative paths are
# relative to game/, like the bake's). --spawn is where new
# characters start, in Galaxies coordinates (default: the bake's center).
# Both server and client need the same bake. --name picks the saved
# character: the same name returns to where it was last saved.
#
# --building places a building exported by swgtool export-building at a
# Galaxies position and yaw in degrees (as `swgtool snapshot` lists it): drawn
# with its cells' own lights, and its floors and walls (<name>.collision) used
# by the movement. The server must place the same building.
#
# --soldier is the soldier class (bf2tool export-soldier: health, movement,
# weapons); the server must use the same file.
#
# --battlefield joins (or with --host, hosts) a conquest match on a
# battlefield file (game/battlefields/); the server must use the same file.
# --bots adds bots to a hosted server. In a match you choose a class (1-9)
# and a command post your team holds (Q/E), and spawn with Space or a click.
# Posts show as beams: blue yours, red the enemy's, white neutral.
#
# Controls: WASD to move, mouse to aim, Shift to sprint, Space to jump, left
# mouse to fire, R to reload, 1 and 2 for weapons, Esc to free the mouse (Esc
# again quits). Shots fly as bolts; the server decides what they hit. --bot
# drives the character instead:
# "circle" runs in circles, "chase" runs after the nearest enemy and
# shoots at it, "conquest" is the C++ bot (core::Bot) playing the match,
# "route" runs through --route's points (Galaxies x,z, as walk_building.gd
# prints them) from the nearest one, then stands.
# --lag/--jitter/--loss simulate a bad connection on this client.
#
# With --seconds, quits after that long and prints network stats. Exits 1 if
# it never spawned, or never saw another player (unless --solo). --shots saves
# four evenly spaced screenshots. Needs a real window.
extends SceneTree

const TerrainTool = preload("res://tools/render_terrain.gd")
const Terrain3DBake = preload("res://tools/terrain3d_bake.gd")
const SwgBuilding = preload("res://rendering/swg_building.gd")

const RUN_ANIMATION := "human_rifle_stand_runforward"
const IDLE_ANIMATION := "human_rifle_stand_idle_emote_full"
const ANIMATION_RUN_SPEED := 4.5  # m/s the run cycle matches
const MOUSE_SENSITIVITY := 0.003
const CAMERA_DISTANCE := 4.5
const CAMERA_HEIGHT := 2.0
const CAMERA_SHOULDER := 0.7  # meters right of the character
const SHOT_COUNT := 4
const CAMERA_MIN_DISTANCE := 0.6
const EYE_HEIGHT := 1.6       # matches core: shots start here
const AIM_DISTANCE := 40.0    # the crosshair's ray converges with the shot here
const BOT_FIRE_RANGE := 40.0
const BOLT_GLOW := 6.0        # emission strength; a graphics setting, like everything about bolt looks
const BOLT_STRETCH := 3.0     # drawn length times LaserLength (Battlefront adds a longer glow), and width times half that
const FRIEND_COLOR := Color(0.35, 0.6, 1.0)
const ENEMY_COLOR := Color(1.0, 0.3, 0.25)
const NEUTRAL_COLOR := Color(0.9, 0.9, 0.9)
const POST_BEAM_HEIGHT := 30.0

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
var _solo := false
var _announced := false
var _camera: Camera3D
var _camera_yaw := 0.0
var _camera_distance := CAMERA_DISTANCE  # pulled in when a wall is behind the character
var _route: Array[Vector2] = []  # game x, z
var _route_index := -1
var _hud: Label
var _crosshair: Label
var _feed: Label
var _feed_lines: PackedStringArray = []
var _hit_flash := 0.0
var _weapons: Array = []          # from SwmClient.get_weapons: the class being played
var _weapon := 0                  # slot wanted
var _bolt_materials := {}         # bolt color (html) -> material
var _bolt_mesh: BoxMesh           # a unit cube, scaled per bolt
var _battlefield: Dictionary = {} # from SwmClient.get_battlefield; empty outside a match
var _posts: Array = []            # per command post: {beam, ring, label, material}
var _spawn_class := 0
var _spawn_post := -1
var _spawn_requested := false
var _center: Label                # spawn menu, results, warnings
var _match_label: RichTextLabel
var _outside := 0.0               # seconds out of bounds
var _own_bolts: Array = []        # [{node, velocity, life, gravity}] drawn from predicted shots
var _remote_bolt_nodes: Array[MeshInstance3D] = []
var _kills := 0
var _deaths := 0
var _hits := 0
var _pending_shot := ""
var _shot_delay := 0


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2 or not ClassDB.class_exists("SwmClient"):
		printerr("usage: -- <bake_prefix> <character.gltf> [--server addr:port] [--host port] [--spawn x z] ",
				"[--name n] [--building gltf x y z yaw] [--bot circle|chase|route|conquest] [--route x,z;...] ",
				"[--soldier ini] [--battlefield ini] [--bots n] [--lag ms] ",
				"[--jitter ms] [--loss 0..1] [--seconds n] [--shots prefix] [--solo] [--terrain3d]",
				"   (needs the swm GDExtension)")
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
	_solo = args.has("--solo")
	var bake_path := _os_path(bake)
	for point in _option(args, "--route", "").split(";", false):
		var xz := point.split(",")
		_route.append(Vector2(-float(xz[0]), float(xz[1])))
	var building := PackedStringArray()
	var at := args.find("--building")
	if at >= 0 and at + 5 < args.size():
		building = args.slice(at + 1, at + 6)
	var collision := ""
	var building_position := Vector3.ZERO
	var building_yaw := 0.0
	if not building.is_empty():
		collision = _os_path(building[0]).get_basename() + ".collision"
		building_position = Vector3(float(building[1]), float(building[2]), float(building[3]))
		building_yaw = float(building[4])

	var server := _option(args, "--server", "127.0.0.1:7777")
	var battlefield := _option(args, "--battlefield", "")
	var host := _option(args, "--host", "")
	if host != "":
		_server = ClassDB.instantiate("SwmServer")
		var database := _option(args, "--db", "")
		if database != "":
			_server.set_database(_os_path(database))
		if collision != "" and _server.add_building(collision, building_position, building_yaw) != OK:
			quit(1)
			return
		if _option(args, "--soldier", "") != "" and _server.set_soldier(_os_path(_option(args, "--soldier", ""))) != OK:
			quit(1)
			return
		if battlefield != "" and _server.set_battlefield(_os_path(battlefield)) != OK:
			quit(1)
			return
		_server.set_bots(int(_option(args, "--bots", "0")))
		var spawn := _spawn(args, meta)
		if _server.start(int(host), bake_path, spawn) != OK:
			quit(1)
			return
		server = "127.0.0.1:%d" % _server.get_port()
		print("hosting on port ", _server.get_port())

	_client = ClassDB.instantiate("SwmClient")
	_client.set_simulated_network(float(_option(args, "--lag", "0")), float(_option(args, "--jitter", "0")),
			float(_option(args, "--loss", "0")))
	if collision != "" and _client.add_building(collision, building_position, building_yaw) != OK:
		quit(1)
		return
	if _option(args, "--soldier", "") != "" and _client.set_soldier(_os_path(_option(args, "--soldier", ""))) != OK:
		quit(1)
		return
	if battlefield != "" and _client.set_battlefield(_os_path(battlefield)) != OK:
		quit(1)
		return
	_client.set_bot(_bot == "conquest")
	_weapons = _client.get_weapons()
	if _client.start(server, bake_path, _option(args, "--name", "player")) != OK:
		quit(1)
		return
	_battlefield = _client.get_battlefield()

	_add_terrain(bake, meta, args.has("--terrain3d"))
	if not building.is_empty() and not _add_building(building[0], building_position, building_yaw):
		quit(1)
		return
	_own = _add_character()
	if _own.is_empty():
		quit(1)
		return
	_setup_scene()
	_add_posts()
	root.window_input.connect(_on_input)
	DisplayServer.window_set_size(Vector2i(1280, 720))
	if _bot == "":
		_capture_mouse(true)


func _process(delta: float) -> bool:
	_elapsed += delta
	if _server != null:
		_server.update()
	if _bot != "conquest":
		_client.set_input(_controls())
	_client.update()

	if _client.is_spawned():
		var own: Dictionary = _client.get_presented()
		if not _announced:
			_announced = true
			var at: Vector3 = own["position"]
			print("spawned as character %d at Galaxies (%.2f, %.2f, %.2f)" % [_client.get_character_id(), -at.x, at.y, at.z])
		_weapons = _client.get_weapons()
		_pose(_own, own)
		_update_remotes(int(own["team"]))
		var game: Dictionary = _client.get_match()
		_update_posts(game, int(own["team"]))
		if own["life"] == "waiting" and not game.is_empty():
			_choose_spawn(game, own)
		else:
			_spawn_requested = false
			_update_camera(own["position"], delta)
		_update_bolts(delta)
		_update_events()
	_update_hud(delta)

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
	if _client.get_status() == "refused":
		printerr("refused: ", _client.get_refusal())
		return _finish()
	return false


func _finish() -> bool:
	var stats: Dictionary = _client.get_stats()
	var at: Vector3 = _client.get_presented().get("position", Vector3.ZERO)
	print("left at Galaxies (%.2f, %.2f, %.2f)" % [-at.x, at.y, at.z])
	print("combat: %d hits, %d kills, %d deaths" % [_hits, _kills, _deaths])
	var game: Dictionary = _client.get_match()
	if not game.is_empty():
		var own: Dictionary = _client.get_presented()
		print("match: %s, tickets %d to %d; team %s, %d captures" % [game["phase"], ceili(game["tickets"][0]),
				ceili(game["tickets"][1]), _team_name(int(own["team"])), own["captures"]])
	print("status %s, entity %d, %d ticks, %d snapshots, %d corrections (max %.3f m), rtt %.0f ms, saw other players: %s" % [
			_client.get_status(), _client.get_entity_id(), stats.get("ticks", 0), stats.get("snapshots", 0),
			stats.get("corrections", 0), stats.get("max_correction", 0.0), stats.get("rtt_ms", 0.0), _saw_remote])
	var spawned: bool = _client.is_spawned()
	_leave()
	quit(0 if spawned and (_saw_remote or _solo) else 1)
	return true


# Says goodbye, giving a simulated-lag link a moment to send it.
func _leave() -> void:
	_client.leave()
	for _i in 20:
		_client.update()
		if _server != null:
			_server.update()
		OS.delay_msec(10)
	if _server != null:
		_server.stop()  # saves everyone still connected


# Controls for SwmClient.set_input: {forward, right, yaw, pitch, sprint, jump,
# fire, reload, weapon}.
func _controls() -> Dictionary:
	match _bot:
		"circle":
			_yaw = _elapsed * 0.5
			return {"forward": 1.0, "yaw": _yaw, "sprint": fmod(_elapsed, 8.0) < 4.0,
					"jump": fmod(_elapsed, 5.0) < 0.05}
		"route":
			return _follow_route()
		"chase":
			var target := _nearest_enemy()
			if target.is_empty() or not _client.is_spawned():
				return {"yaw": _yaw}
			var eye: Vector3 = _client.get_presented()["position"] + Vector3.UP * EYE_HEIGHT
			var to: Vector3 = target["position"] + Vector3.UP * 1.0 - eye
			_yaw = atan2(-to.x, -to.z)
			var distance := Vector2(to.x, to.z).length()
			return {"forward": 1.0 if distance > 8.0 else 0.0, "yaw": _yaw,
					"pitch": atan2(to.y, distance), "sprint": distance > 25.0,
					"fire": distance < BOT_FIRE_RANGE and bool(target["alive"])}
	if not _mouse_captured:
		return {"yaw": _yaw, "weapon": _weapon}
	var forward := float(Input.is_physical_key_pressed(KEY_W)) - float(Input.is_physical_key_pressed(KEY_S))
	var right := float(Input.is_physical_key_pressed(KEY_D)) - float(Input.is_physical_key_pressed(KEY_A))
	var aim := _aim()
	return {"forward": forward, "right": right, "yaw": aim.x, "pitch": aim.y,
			"sprint": Input.is_physical_key_pressed(KEY_SHIFT), "jump": Input.is_physical_key_pressed(KEY_SPACE),
			"fire": Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT), "reload": Input.is_physical_key_pressed(KEY_R),
			"weapon": _weapon}


# Yaw and pitch from the character's eyes to where the crosshair (the
# screen's center) points, AIM_DISTANCE away: shots start at the eyes, the
# camera sits over the shoulder.
func _aim() -> Vector2:
	if not _client.is_spawned():
		return Vector2(_yaw, 0)
	var eye: Vector3 = _client.get_presented()["position"] + Vector3.UP * EYE_HEIGHT
	var target := _camera.global_position - _camera.global_basis.z * AIM_DISTANCE
	var to := target - eye
	return Vector2(atan2(-to.x, -to.z), atan2(to.y, Vector2(to.x, to.z).length()))


func _on_input(event: InputEvent) -> void:
	if _bot != "":
		return
	if event is InputEventMouseMotion and _mouse_captured:
		var motion := event as InputEventMouseMotion
		_yaw -= motion.relative.x * MOUSE_SENSITIVITY
		_pitch = clampf(_pitch - motion.relative.y * MOUSE_SENSITIVITY, -1.2, 0.6)
	elif event is InputEventMouseButton and event.pressed and not _mouse_captured:
		_capture_mouse(true)
	elif event is InputEventMouseButton and event.pressed and _choosing_spawn():
		_request_spawn()
	elif event is InputEventKey and event.pressed and _choosing_spawn() and event.physical_keycode != KEY_ESCAPE:
		var key: int = event.physical_keycode
		var classes: Array = _battlefield["teams"][int(_client.get_presented()["team"])]["classes"]
		if key >= KEY_1 and key <= KEY_9:
			_spawn_class = mini(key - KEY_1, classes.size() - 1)
		elif key in [KEY_Q, KEY_LEFT]:
			_cycle_post(-1)
		elif key in [KEY_E, KEY_RIGHT]:
			_cycle_post(1)
		elif key in [KEY_SPACE, KEY_ENTER]:
			_request_spawn()
	elif event is InputEventKey and event.pressed and event.physical_keycode in [KEY_1, KEY_2]:
		_weapon = mini(int(event.physical_keycode - KEY_1), _weapons.size() - 1)
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


# The nearest living character on the other team (anyone, outside a match).
func _nearest_enemy() -> Dictionary:
	var own: Dictionary = _client.get_presented()
	var best: Dictionary = {}
	for remote: Dictionary in _client.get_remotes():
		var enemy := int(own["team"]) < 0 or int(remote["team"]) != int(own["team"])
		if not enemy or not remote["alive"]:
			continue
		var position: Vector3 = remote["position"]
		if best.is_empty() or position.distance_to(own["position"]) < (best["position"] as Vector3).distance_to(own["position"]):
			best = remote
	return best


func _update_remotes(own_team: int) -> void:
	var seen := {}
	for remote: Dictionary in _client.get_remotes():
		var id: int = remote["id"]
		seen[id] = true
		_saw_remote = true
		if not _remotes.has(id):
			var character := _add_character()
			if character.is_empty():
				continue
			var tag := Label3D.new()
			tag.billboard = BaseMaterial3D.BILLBOARD_ENABLED
			tag.fixed_size = true
			tag.pixel_size = 0.0012
			tag.font_size = 24
			tag.outline_size = 6
			tag.no_depth_test = true
			tag.position = Vector3.UP * 2.3
			(character["model"] as Node3D).add_child(tag)
			character["tag"] = tag
			_remotes[id] = character
		_pose(_remotes[id], remote)
		var tag: Label3D = _remotes[id]["tag"]
		var friend := own_team >= 0 and int(remote["team"]) == own_team
		tag.text = "player %d" % id
		tag.modulate = FRIEND_COLOR if friend else ENEMY_COLOR
	for id: int in _remotes.keys():
		if not seen.has(id):
			(_remotes[id]["model"] as Node).queue_free()
			_remotes.erase(id)


func _pose(character: Dictionary, state: Dictionary) -> void:
	var model: Node3D = character["model"]
	model.visible = bool(state.get("alive", true))  # no death animations yet
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


# Steers through the route's points; starts at the nearest, so a character
# that already finished (e.g. after a server restart) just stands.
func _follow_route() -> Dictionary:
	if not _client.is_spawned():
		return {"yaw": _yaw}
	var position: Vector3 = _client.get_presented()["position"]
	var here := Vector2(position.x, position.z)
	if _route_index < 0 and not _route.is_empty():
		_route_index = 0
		for i in _route.size():
			if here.distance_to(_route[i]) < here.distance_to(_route[_route_index]):
				_route_index = i
	while _route_index >= 0 and _route_index < _route.size() and here.distance_to(_route[_route_index]) < 0.4:
		_route_index += 1
	if _route_index < 0 or _route_index >= _route.size():
		# Done: stand and face the nearest other player, if any.
		var other := _nearest_remote()
		if not other.is_empty():
			var to_other: Vector3 = other["position"] - position
			_yaw = atan2(-to_other.x, -to_other.z)
		return {"yaw": _yaw}
	var to := _route[_route_index] - here
	_yaw = atan2(-to.x, -to.y)
	return {"forward": 1.0, "yaw": _yaw}


# Camera collision against the building: how far behind the character the
# camera fits. Physics queries belong in the physics step.
func _physics_process(_delta: float) -> bool:
	if _client == null or not _client.is_spawned():
		return false
	var pivot := _camera_pivot(_client.get_presented()["position"])
	var wanted := pivot + _camera_basis() * Vector3(CAMERA_SHOULDER, 0.3, CAMERA_DISTANCE)
	var query := PhysicsRayQueryParameters3D.create(pivot, wanted)
	var hit := root.get_world_3d().direct_space_state.intersect_ray(query)
	if hit.is_empty():
		_camera_distance = CAMERA_DISTANCE
	else:
		_camera_distance = maxf(CAMERA_MIN_DISTANCE, pivot.distance_to(hit["position"]) - 0.25)
	return false


func _camera_basis() -> Basis:
	return Basis(Vector3.UP, _camera_yaw) * Basis(Vector3.RIGHT, _pitch)


func _camera_pivot(target: Vector3) -> Vector3:
	return target + Vector3.UP * CAMERA_HEIGHT * 0.8


func _update_camera(target: Vector3, delta: float) -> void:
	# Over the shoulder, facing where the character aims (as in Battlefront,
	# the character always faces the camera's direction). Bots turn sharply,
	# so their camera follows smoothly.
	_camera_yaw = _yaw if _bot == "" else lerp_angle(_camera_yaw, _yaw, minf(1.0, 4.0 * delta))
	var basis := _camera_basis()
	var pivot := _camera_pivot(target)
	var offset := Vector3(CAMERA_SHOULDER, 0.3, CAMERA_DISTANCE) * (_camera_distance / CAMERA_DISTANCE)
	var position := pivot + basis * offset
	# Keep the camera above the ground.
	position.y = maxf(position.y, _client.get_ground_height(position.x, position.z) + 0.4)
	_camera.global_position = position
	_camera.look_at(pivot + basis * Vector3(CAMERA_SHOULDER, 0, -10), Vector3.UP)


func _update_hud(delta: float) -> void:
	var stats: Dictionary = _client.get_stats()
	var lines := PackedStringArray()
	lines.append("%s | character %d | %d other player(s) | rtt %.0f ms | corrections %d (max %.2f m)" % [
			_client.get_status(), _client.get_character_id(), _remotes.size(), stats.get("rtt_ms", 0.0),
			stats.get("corrections", 0), stats.get("max_correction", 0.0)])
	if _client.is_spawned():
		var own: Dictionary = _client.get_presented()
		var slot: int = own["weapon"]
		var weapon: Dictionary = _weapons[slot] if slot < _weapons.size() else {}
		var ammo := ""
		if int(weapon.get("rounds_per_clip", 0)) > 0:
			ammo = "%d / %d%s" % [own["rounds"], weapon["rounds_per_clip"], "  reloading" if own["reloading"] else ""]
		else:
			ammo = "heat %d%%%s" % [roundi(float(own["heat"]) * 100), "  OVERHEATED" if own["overheated"] else ""]
		lines.append("health %d / %d | %s: %s | energy %d" % [roundi(own["health"]), roundi(_client.get_max_health()),
				weapon.get("name", "?"), ammo, roundi(own["energy"])])
	_hud.text = "\n".join(lines)
	_update_match_hud(delta)
	_hit_flash = maxf(0.0, _hit_flash - delta)
	_crosshair.text = "x" if _hit_flash > 0 else "+"
	_crosshair.modulate = Color(1, 0.3, 0.3) if _hit_flash > 0 else Color.WHITE
	_feed.text = "\n".join(_feed_lines)


# Hit markers and the kill feed.
func _update_events() -> void:
	var me: int = _client.get_entity_id()
	for event: Dictionary in _client.take_events():
		var mine := int(event["attacker"]) == me
		if event["kind"] == "hit" and mine:
			_hits += 1
			_hit_flash = 0.15
		elif event["kind"] == "kill":
			_kills += 1 if mine and int(event["victim"]) != me else 0
			_deaths += 1 if int(event["victim"]) == me else 0
			if int(event["attacker"]) == 0:
				_feed_lines.append("%s died" % _who(event["victim"]))
			else:
				_feed_lines.append("%s eliminated %s" % [_who(event["attacker"]), _who(event["victim"])])
		elif event["kind"] in ["captured", "neutralized"] and not _battlefield.is_empty():
			_feed_lines.append("%s %s %s" % [_team_name(int(event["attacker"])), event["kind"],
					_battlefield["posts"][int(event["victim"])]["name"]])
		if _feed_lines.size() > 5:
			_feed_lines.remove_at(0)


func _who(id: int) -> String:
	return "you" if id == _client.get_entity_id() else "player %d" % id


# Bolts: own shots fly from the moment they're fired (predicted), others'
# come from the server. Only looks: what they hit is the server's call.
func _update_bolts(delta: float) -> void:
	for shot: Dictionary in _client.take_shots():
		var weapon: Dictionary = _weapons[mini(int(shot["weapon"]), _weapons.size() - 1)]
		for direction: Vector3 in shot["directions"]:
			var node := _bolt_node(shot)
			var velocity: Vector3 = direction * float(weapon["bolt_speed"])
			_place_bolt(node, shot["origin"], velocity)
			_own_bolts.append({"node": node, "velocity": velocity, "life": float(weapon["bolt_life"])})
	for i in range(_own_bolts.size() - 1, -1, -1):
		var bolt: Dictionary = _own_bolts[i]
		bolt["life"] = float(bolt["life"]) - delta
		bolt["velocity"] = bolt["velocity"] + Vector3.DOWN * 9.8 * delta
		var node: MeshInstance3D = bolt["node"]
		if float(bolt["life"]) <= 0:
			node.queue_free()
			_own_bolts.remove_at(i)
			continue
		_place_bolt(node, node.position + bolt["velocity"] * delta, bolt["velocity"])

	var remote: Array = _client.get_bolts()
	while _remote_bolt_nodes.size() < remote.size():
		_remote_bolt_nodes.append(_bolt_node(remote[_remote_bolt_nodes.size()]))
	for i in _remote_bolt_nodes.size():
		var node := _remote_bolt_nodes[i]
		node.visible = i < remote.size()
		if node.visible:
			var bolt: Dictionary = remote[i]
			_style_bolt(node, bolt)
			_place_bolt(node, bolt["position"], bolt["velocity"])


# `look` has the firing weapon's {color, length, width}.
func _bolt_node(look: Dictionary) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	node.mesh = _bolt_mesh
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_style_bolt(node, look)
	root.add_child(node)
	return node


# Battlefront's bolt color, length and width; how they're drawn (glow,
# stretch) is up to the graphics settings.
func _style_bolt(node: MeshInstance3D, look: Dictionary) -> void:
	var color: Color = look["color"]
	var key := color.to_html()
	if not _bolt_materials.has(key):
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.albedo_color = Color(color.r, color.g, color.b)
		material.emission_enabled = true
		material.emission = Color(color.r, color.g, color.b)
		material.emission_energy_multiplier = BOLT_GLOW
		_bolt_materials[key] = material
	node.material_override = _bolt_materials[key]
	var width := float(look["width"]) * BOLT_STRETCH / 2
	node.set_meta("size", Vector3(width, width, float(look["length"]) * BOLT_STRETCH))


# A bolt is a thin glowing box along its velocity.
func _place_bolt(node: MeshInstance3D, position: Vector3, velocity: Vector3) -> void:
	node.position = position
	if velocity.length_squared() > 0.0001:
		var forward := velocity.normalized()
		var up := Vector3.UP if absf(forward.y) < 0.99 else Vector3.RIGHT
		node.basis = Basis.looking_at(forward, up) * Basis.from_scale(node.get_meta("size", Vector3.ONE))


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


# Draws a building where the world snapshot places it. Its cell meshes also
# get physics collision, for the camera only: movement is in core/.
func _add_building(path: String, galaxies_position: Vector3, yaw_degrees: float) -> bool:
	var building := SwgBuilding.load_lit(path)
	if building == null:
		printerr("cannot load ", path, " and its .json sidecar")
		return false
	# SWG is left-handed: mirror X, which also reverses the yaw.
	building.position = Vector3(-galaxies_position.x, galaxies_position.y, galaxies_position.z)
	building.rotation.y = deg_to_rad(-yaw_degrees)
	root.add_child(building)
	for mesh in building.find_children("*_mesh", "MeshInstance3D", true, false):
		(mesh as MeshInstance3D).create_trimesh_collision()
	return true


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
	environment.glow_enabled = true  # blaster bolts bloom
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
	_hud.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_LEFT)
	_hud.grow_vertical = Control.GROW_DIRECTION_BEGIN
	_hud.offset_left = 12
	_hud.offset_bottom = -8
	_hud.add_theme_color_override("font_outline_color", Color.BLACK)
	_hud.add_theme_constant_override("outline_size", 4)
	layer.add_child(_hud)
	_crosshair = Label.new()
	_crosshair.text = "+"
	_crosshair.add_theme_font_size_override("font_size", 28)
	_crosshair.add_theme_color_override("font_outline_color", Color.BLACK)
	_crosshair.add_theme_constant_override("outline_size", 4)
	_crosshair.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	_crosshair.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_crosshair.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	layer.add_child(_crosshair)
	_feed = Label.new()
	_feed.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	_feed.position = Vector2(-320, 8)
	_feed.size = Vector2(310, 140)
	_feed.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_feed.add_theme_color_override("font_outline_color", Color.BLACK)
	_feed.add_theme_constant_override("outline_size", 4)
	layer.add_child(_feed)
	_center = Label.new()
	_center.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	_center.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_center.grow_vertical = Control.GROW_DIRECTION_BOTH
	_center.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_center.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_center.add_theme_font_size_override("font_size", 20)
	_center.add_theme_color_override("font_outline_color", Color.BLACK)
	_center.add_theme_constant_override("outline_size", 6)
	layer.add_child(_center)
	_match_label = RichTextLabel.new()
	_match_label.bbcode_enabled = true
	_match_label.fit_content = true
	_match_label.scroll_active = false
	_match_label.set_anchors_and_offsets_preset(Control.PRESET_CENTER_TOP)
	_match_label.position = Vector2(-300, 8)
	_match_label.size = Vector2(600, 90)
	_match_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_match_label.add_theme_constant_override("outline_size", 4)
	layer.add_child(_match_label)
	root.add_child(layer)

	_bolt_mesh = BoxMesh.new()
	_bolt_mesh.size = Vector3(1, 1, 1)


# Command posts: a beam, a ring around the capture region and a name, in the
# owner's color.
func _add_posts() -> void:
	if _battlefield.is_empty():
		return
	for post: Dictionary in _battlefield["posts"]:
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		material.albedo_color = NEUTRAL_COLOR
		var position: Vector3 = post["position"]
		var beam := MeshInstance3D.new()
		var cylinder := CylinderMesh.new()
		cylinder.top_radius = 0.25
		cylinder.bottom_radius = 0.25
		cylinder.height = POST_BEAM_HEIGHT
		beam.mesh = cylinder
		beam.material_override = material
		beam.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		beam.position = position + Vector3.UP * POST_BEAM_HEIGHT / 2
		root.add_child(beam)
		var ring := MeshInstance3D.new()
		var torus := TorusMesh.new()
		torus.outer_radius = float(post["radius"])
		torus.inner_radius = float(post["radius"]) - 0.3
		ring.mesh = torus
		ring.material_override = material
		ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		ring.position = position + Vector3.UP * 0.15
		root.add_child(ring)
		var label := Label3D.new()
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.fixed_size = true
		label.pixel_size = 0.0015
		label.font_size = 28
		label.outline_size = 8
		label.no_depth_test = true
		label.position = position + Vector3.UP * 6
		root.add_child(label)
		_posts.append({"material": material, "label": label})


func _post_color(owner: int, own_team: int) -> Color:
	if owner < 0:
		return NEUTRAL_COLOR
	return FRIEND_COLOR if owner == own_team else ENEMY_COLOR


func _update_posts(game: Dictionary, own_team: int) -> void:
	if game.is_empty():
		return
	for i in mini(_posts.size(), (game["posts"] as Array).size()):
		var state: Dictionary = game["posts"][i]
		var color := _post_color(int(state["owner"]), own_team)
		var material: StandardMaterial3D = _posts[i]["material"]
		material.albedo_color = Color(color, 0.75)
		var label: Label3D = _posts[i]["label"]
		var text: String = _battlefield["posts"][i]["name"]
		if bool(state["contested"]):
			text += " (contested)"
		elif float(state["charge"]) < 1.0 and int(state["holder"]) >= 0:
			text += " %d%%" % roundi(float(state["charge"]) * 100)
		label.text = text
		label.modulate = color


func _team_name(team: int) -> String:
	if _battlefield.is_empty() or team < 0 or team > 1:
		return "nobody"
	return _battlefield["teams"][team]["name"]


func _owned_posts(game: Dictionary, team: int) -> Array[int]:
	var owned: Array[int] = []
	for i in (game["posts"] as Array).size():
		if int(game["posts"][i]["owner"]) == team:
			owned.append(i)
	return owned


func _choosing_spawn() -> bool:
	return _client.is_spawned() and not _battlefield.is_empty() and _client.get_presented()["life"] == "waiting"


func _cycle_post(step: int) -> void:
	var owned := _owned_posts(_client.get_match(), int(_client.get_presented()["team"]))
	if owned.is_empty():
		return
	var at := owned.find(_spawn_post)
	_spawn_post = owned[posmod(at + step, owned.size())]


func _request_spawn() -> void:
	if _spawn_post >= 0:
		_client.request_spawn(_spawn_class, _spawn_post)
		_spawn_requested = true


# The spawn screen: an overview of the chosen post. Bots other than the C++
# one spawn as the first class at the first post their team holds.
func _choose_spawn(game: Dictionary, own: Dictionary) -> void:
	var owned := _owned_posts(game, int(own["team"]))
	if not owned.has(_spawn_post):
		_spawn_post = owned[0] if not owned.is_empty() else -1
	if _bot != "" and _bot != "conquest" and not _spawn_requested:
		_request_spawn()
	if _spawn_post >= 0:
		var target: Vector3 = _battlefield["posts"][_spawn_post]["position"]
		var view := target + Basis(Vector3.UP, _yaw) * Vector3(0, 28, 38)
		_camera.global_position = _camera.global_position.lerp(view, 0.1)
		_camera.look_at(target, Vector3.UP)


func _update_match_hud(delta: float) -> void:
	_center.text = ""
	var game: Dictionary = _client.get_match()
	if game.is_empty() or not _client.is_spawned():
		_match_label.text = ""
		var own_state: Dictionary = _client.get_presented() if _client.is_spawned() else {}
		if not own_state.is_empty() and not own_state["alive"]:
			_center.text = "Eliminated. Respawning..."
		return
	var own: Dictionary = _client.get_presented()
	var team := int(own["team"])
	var text := "[center][color=#%s]%s %d[/color]     [color=#%s]%s %d[/color]\n" % [
			_post_color(0, team).to_html(false), _team_name(0), ceili(game["tickets"][0]),
			_post_color(1, team).to_html(false), _team_name(1), ceili(game["tickets"][1])]
	match game["phase"]:
		"warmup":
			text += "Warmup: the match starts in %d s\n" % ceili(game["time_left"])
		"live":
			if int(game["dominant"]) >= 0:
				text += "%s holds every command post: victory in %d s\n" % [_team_name(int(game["dominant"])),
						ceili(game["victory_left"])]
			else:
				text += "\n"
	var names := PackedStringArray()
	for i in (game["posts"] as Array).size():
		names.append("[color=#%s]%s[/color]" % [_post_color(int(game["posts"][i]["owner"]), team).to_html(false),
				_battlefield["posts"][i]["name"]])
	_match_label.text = text + "  ".join(names) + "[/center]"

	var position: Vector3 = own["position"]
	var inside := position.x >= float(_battlefield["min_x"]) and position.x <= float(_battlefield["max_x"]) \
			and position.z >= float(_battlefield["min_z"]) and position.z <= float(_battlefield["max_z"])
	_outside = 0.0 if inside or not own["alive"] else _outside + delta

	if game["phase"] == "ended":
		_center.text = _results(game, team)
	elif own["life"] == "waiting":
		_center.text = _spawn_menu(game, team)
	elif own["life"] == "dead":
		_center.text = "Eliminated"
	elif _outside > 0:
		_center.text = "Return to the battlefield!"


func _spawn_menu(game: Dictionary, team: int) -> String:
	var lines := PackedStringArray()
	lines.append("%s: you fight for the %s" % [_battlefield["name"], _team_name(team)])
	lines.append("")
	var classes: Array = _battlefield["teams"][team]["classes"]
	_spawn_class = mini(_spawn_class, classes.size() - 1)
	for i in classes.size():
		var soldier: Dictionary = classes[i]
		lines.append("%s %d  %s  (%s, %d health)" % [">" if i == _spawn_class else "  ", i + 1, soldier["name"],
				", ".join(soldier["weapons"]), roundi(soldier["max_health"])])
	lines.append("")
	if _spawn_post < 0:
		lines.append("Your team holds no command posts")
	else:
		lines.append("Command post (Q / E):  < %s >" % _battlefield["posts"][_spawn_post]["name"])
		lines.append("")
		lines.append("Spawning..." if _spawn_requested else "Space or click to spawn")
	if game["phase"] == "live" and ceili(game["tickets"][team]) <= 0:
		lines.append("No reinforcements left")
	return "\n".join(lines)


func _results(game: Dictionary, team: int) -> String:
	var winner := int(game["winner"])
	var lines := PackedStringArray()
	lines.append("Draw" if winner < 0 else ("Victory! " if winner == team else "Defeat. ") + "%s wins" % _team_name(winner))
	lines.append("")
	var everyone: Array = _client.get_remotes()
	var own: Dictionary = _client.get_presented().duplicate()
	own["id"] = _client.get_entity_id()
	everyone.append(own)
	for side in 2:
		lines.append(_team_name(side))
		var players := everyone.filter(func(p: Dictionary) -> bool: return int(p["team"]) == side)
		players.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return int(a["kills"]) > int(b["kills"]))
		for p: Dictionary in players:
			lines.append("%-12s %3d kills %3d deaths %3d captures" % [_who(p["id"]), p["kills"], p["deaths"], p["captures"]])
		lines.append("")
	lines.append("Next match in %d s" % ceili(game["time_left"]))
	return "\n".join(lines)


# The server's spawn point (Galaxies x, z): --spawn, or the bake's center.
func _spawn(args: PackedStringArray, meta: Dictionary) -> Vector2:
	var at := args.find("--spawn")
	if at >= 0 and at + 2 < args.size():
		return Vector2(float(args[at + 1]), float(args[at + 2]))
	var half: float = (int(meta["samples"]) - 1) * float(meta["spacing"]) / 2
	return Vector2(float(meta["origin_x"]) + half, float(meta["origin_z"]) + half)


# Relative paths are relative to the project folder.
func _os_path(path: String) -> String:
	return path if path.is_absolute_path() else ProjectSettings.globalize_path("res://" + path)


func _option(args: PackedStringArray, flag: String, fallback: String) -> String:
	var at := args.find(flag)
	return args[at + 1] if at >= 0 and at + 1 < args.size() else fallback
