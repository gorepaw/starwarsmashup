# Dev tool: a networked third-person client on baked terrain. Movement is
# simulated in C++ (core/): the server is authoritative and this client
# predicts its own character (SwmClient). This script only draws the result
# and turns keys or a bot into input.
#
#   godot --path game --script res://tools/net_demo.gd -- <bake_prefix> <character.gltf>
#         [--server a.b.c.d:port] [--host <port>] [--db <file>] [--spawn <x> <z>] [--name <name>]
#         [--building <building.gltf> <x> <y> <z> <yaw>] [--bot circle|chase|route] [--route <x,z;x,z;...>]
#         [--loadout <file.ini>] [--battlefield <file.ini>] [--bots <n>] [--hub <file.ini>] [--lag <ms>] [--jitter <ms>]
#         [--loss <0..1>] [--seconds <n>] [--shots <output_prefix>] [--solo] [--terrain3d] [--first-person]
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
# --loadout is what you carry outside a match: weapons and gear (a loadout
# file, see game/loadouts/); the server must use the same file.
#
# --battlefield joins (or with --host, hosts) a conquest match on a
# battlefield file (game/battlefields/); the server must use the same file.
# --bots adds bots to a hosted server. In a match you choose a loadout (1-9)
# and a command post your team holds (Q/E), and spawn with Space or a click.
# Posts show as beams: blue yours, red the enemy's, white neutral.
#
# A Battlefront II map exported with bf2tool export-map is a bake too: pass
# its prefix (e.g. imported_assets/maps/dag1/dag1) and it's drawn with its
# terrain textures, models, sky, fog and lights (rendering/bf2_map.gd).
#
# --hub hosts a hub (game/hubs/) instead: walk to the war terminal (the
# yellow pillar) and press F or G to queue for a side, X to leave the queue.
# When the battle starts you're sent to it, and back to the hub after the
# results. Joining a hub server (swm_server --hub) works the same. Battles'
# battlefield files are looked up in game/battlefields/.
#
# Controls: WASD to move, mouse to aim, Shift to sprint, Space to jump, left
# mouse to fire, right mouse to throw a grenade, R to reload, 1 and 2 for
# weapons, V to switch between third and first person (from your own
# character's eyes; --first-person starts in it), Tab for the scores, Esc for
# the pause menu (options: mouse, view, display, key bindings; saved in
# user://settings.cfg). With Battlefront's HUD exported (bf2tool export-hud)
# the HUD and screens are Battlefront's (game/ui/). Characters are animated by rendering/soldier.gd. Shots fly as
# bolts, drawn leaving the shooter's muzzle; the server decides what they hit. --bot
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
const Bf2Map = preload("res://rendering/bf2_map.gd")
const Soldier = preload("res://rendering/soldier.gd")
const Bf2Hud = preload("res://ui/bf2_hud.gd")
const Settings = preload("res://ui/settings.gd")
const SpawnScreen = preload("res://ui/spawn_screen.gd")
const ScoreScreen = preload("res://ui/score_screen.gd")
const PauseMenu = preload("res://ui/pause_menu.gd")
const HUD_DIR := "res://imported_assets/hud"  # bf2tool export-hud
const MAX_ENERGY := 100.0     # core::MoveParams::max_energy
const TARGET_CONE := 0.035    # radians: a character this close to the crosshair is its target
const TARGET_RANGE := 150.0
const SEEN_FIRING := 2.0      # seconds an enemy shows on the minimap after firing
const AMMO_NUDGE := Vector2(0.025, 0)  # the ammo counts, right of Battlefront's place (4:3 frame units)

const MOUSE_SENSITIVITY := 0.003
const CAMERA_DISTANCE := 4.5
const CAMERA_HEIGHT := 2.0
const CAMERA_SHOULDER := 0.7  # meters right of the character
const SHOT_COUNT := 4
const CAMERA_MIN_DISTANCE := 0.6
const FIRST_PERSON_FORWARD := 0.12  # m the first-person camera sits in front of the head bone (out of the helmet)
const EYE_HEIGHT := 1.6       # matches core: shots start here
const AIM_DISTANCE := 40.0    # the crosshair's ray converges with the shot here
const MUZZLE_BLEND := 20.0    # m: a drawn bolt leaves the muzzle and joins its true path (from the eye) by here
const BOT_FIRE_RANGE := 40.0
const BOLT_GLOW := 6.0        # emission strength; a graphics setting, like everything about bolt looks
const BOLT_STRETCH := 3.0     # drawn length times LaserLength (Battlefront adds a longer glow), and width times half that
const FRIEND_COLOR := Color(0.35, 0.6, 1.0)
const ENEMY_COLOR := Color(1.0, 0.3, 0.25)
const NEUTRAL_COLOR := Color(0.9, 0.9, 0.9)
const POST_BEAM_HEIGHT := 30.0
const EXPLOSION_TIME := 0.6   # seconds an explosion's flash lasts

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
var _own: Dictionary = {}       # {model, soldier}
var _remotes: Dictionary = {}   # entity id -> {model, soldier, tag}
var _delta := 0.0
var _saw_remote := false
var _solo := false
var _announced := false
var _camera: Camera3D
var _sky_dome: Node3D  # follows the camera
var _camera_yaw := 0.0
var _camera_distance := CAMERA_DISTANCE  # pulled in when a wall is behind the character
var _first_person := false        # V toggles; --first-person starts in it
var _route: Array[Vector2] = []  # game x, z
var _route_index := -1
var _hud: Label
var _crosshair: Label
var _feed: Label
var _feed_lines: PackedStringArray = []
var _hit_flash := 0.0
var _weapons: Array = []          # from SwmClient.get_weapons: the held weapons, by slot
var _weapon := 0                  # slot wanted
var _bolt_materials := {}         # bolt color (html) -> material
var _bolt_mesh: BoxMesh           # a unit cube, scaled per bolt
var _battlefield: Dictionary = {} # from SwmClient.get_battlefield; empty outside a match
var _posts: Array = []            # per command post: {hologram} (its model's), or {material, label} (a beam)
var _spawn_loadout := 0
var _spawn_post := -1
var _spawn_requested := false
var _center: Label                # spawn menu, results, warnings
var _match_label: RichTextLabel
var _outside := 0.0               # seconds out of bounds
var _generation := 0              # transfers followed: hub -> battle -> hub
var _recruiter_nodes: Array = []
var _notice_id := 0
var _notice := ""
var _notice_time := 0.0           # seconds left showing it
var _explosions: Array = []       # [{node, light, age, radius}]
var _explosions_seen := 0
var _own_bolts: Array = []        # [{node, position, velocity, life, origin, offset}] drawn from predicted shots
var _remote_bolt_nodes: Array[MeshInstance3D] = []
var _remote_bolt_starts := {}     # bolt id -> {origin, offset}: where it was first seen drawn from
var _kills := 0
var _deaths := 0
var _hits := 0
var _pending_shot := ""
var _bf2_hud: Bf2Hud              # Battlefront's HUD; null if it isn't exported (the text HUD stands in)
var _settings: Settings
var _spawn_screen: SpawnScreen    # these three exist with Battlefront's HUD
var _score_screen: ScoreScreen
var _pause_menu: PauseMenu
var _was_choosing := false
var _view_setting := false         # the options' view when the pause menu opened
var _show_stats := false          # F3: network stats
var _fired := {}                  # entity id -> seconds since it last fired (for the minimap)
var _map_meta := {}               # the bake's json (its minimap texture)
var _shot_delay := 0


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2 or not ClassDB.class_exists("SwmClient"):
		printerr("usage: -- <bake_prefix> <character.gltf> [--server addr:port] [--host port] [--spawn x z] ",
				"[--name n] [--building gltf x y z yaw] [--bot circle|chase|route|conquest] [--route x,z;...] ",
				"[--loadout ini] [--battlefield ini] [--bots n] [--hub ini] [--lag ms] ",
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
	_settings = Settings.new()
	_settings.load_file()
	_first_person = args.has("--first-person") or _settings.first_person
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
		if _option(args, "--loadout", "") != "" and _server.set_loadout(_os_path(_option(args, "--loadout", ""))) != OK:
			quit(1)
			return
		if battlefield != "" and _server.set_battlefield(_os_path(battlefield)) != OK:
			quit(1)
			return
		_server.set_bots(int(_option(args, "--bots", "0")))
		if _option(args, "--hub", "") != "" and _server.set_hub(_os_path(_option(args, "--hub", ""))) != OK:
			quit(1)
			return
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
	if _option(args, "--loadout", "") != "" and _client.set_loadout(_os_path(_option(args, "--loadout", ""))) != OK:
		quit(1)
		return
	if battlefield != "" and _client.set_battlefield(_os_path(battlefield)) != OK:
		quit(1)
		return
	_client.set_bot(_bot == "conquest")
	_client.set_battlefield_dir(_os_path("battlefields"))
	_weapons = _client.get_weapons()
	if _client.start(server, bake_path, _option(args, "--name", "player")) != OK:
		quit(1)
		return
	_battlefield = _client.get_battlefield()

	_add_terrain(bake, meta, args.has("--terrain3d"))
	if Bf2Map.is_bf2(meta):
		Bf2Map.add_objects(root, bake)
	if not building.is_empty() and not _add_building(building[0], building_position, building_yaw):
		quit(1)
		return
	_own = _add_character()
	if _own.is_empty():
		quit(1)
		return
	_setup_scene(bake, meta)
	_add_posts()
	root.window_input.connect(_on_input)
	DisplayServer.window_set_size(Vector2i(1280, 720))
	if _bot == "":
		_capture_mouse(true)


func _process(delta: float) -> bool:
	_elapsed += delta
	_delta = delta
	if _server != null:
		_server.update()
	if _bot != "conquest":
		_client.set_input(_controls())
	_client.update()
	if _client.get_generation() != _generation:
		_generation = _client.get_generation()
		_arrive()
	if _client.get_session_error() != "":
		printerr(_client.get_session_error())
		return _finish()

	if _client.is_spawned():
		if _recruiter_nodes.is_empty() and not _client.get_recruiters().is_empty():
			_add_recruiters()
		_update_notice()
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
	if _sky_dome != null:
		_sky_dome.global_position = _camera.global_position

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
	print("combat: %d hits, %d kills, %d deaths, %d explosions seen" % [_hits, _kills, _deaths, _explosions_seen])
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
	var forward := Input.get_action_strength("move_forward") - Input.get_action_strength("move_back")
	var right := Input.get_action_strength("move_right") - Input.get_action_strength("move_left")
	var aim := _aim()
	return {"forward": forward, "right": right, "yaw": aim.x, "pitch": aim.y,
			"sprint": Input.is_action_pressed("sprint"), "jump": Input.is_action_pressed("jump"),
			"fire": Input.is_action_pressed("fire"), "reload": Input.is_action_pressed("reload"),
			"secondary": Input.is_action_pressed("grenade"), "weapon": _weapon}


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
	var escape: bool = event is InputEventKey and event.pressed and not event.echo \
			and (event as InputEventKey).physical_keycode == KEY_ESCAPE
	if _pause_menu != null and _pause_menu.visible:
		if escape and not _pause_menu.is_rebinding():
			_pause_menu.visible = false
			_resume()
		return  # the menu has the input
	if escape and _pause_menu != null:
		_capture_mouse(false)
		_pause_menu.in_match = not _battlefield.is_empty()
		_view_setting = _settings.first_person
		_pause_menu.open()
		return
	var screen_spawn := _spawn_screen != null and _choosing_spawn()
	if event is InputEventMouseMotion and _mouse_captured:
		var motion := event as InputEventMouseMotion
		var sensitivity := MOUSE_SENSITIVITY * _settings.mouse_sensitivity
		_yaw -= motion.relative.x * sensitivity
		var lowest := -1.35 if _first_person else -1.2
		var highest := 1.35 if _first_person else 0.6
		var up := -1.0 if _settings.invert_mouse else 1.0
		_pitch = clampf(_pitch - motion.relative.y * sensitivity * up, lowest, highest)
	elif event is InputEventMouseButton and event.pressed and not _mouse_captured and not screen_spawn:
		_capture_mouse(true)
	elif event is InputEventMouseButton and event.pressed and _choosing_spawn() and not screen_spawn:
		_request_spawn()
	elif event is InputEventKey and event.pressed and _choosing_spawn() and event.physical_keycode != KEY_ESCAPE:
		var key: int = event.physical_keycode
		var loadouts: Array = _battlefield["teams"][int(_client.get_presented()["team"])]["loadouts"]
		if key >= KEY_1 and key <= KEY_9:
			_spawn_loadout = mini(key - KEY_1, loadouts.size() - 1)
		elif key in [KEY_Q, KEY_LEFT]:
			_cycle_post(-1)
		elif key in [KEY_E, KEY_RIGHT]:
			_cycle_post(1)
		elif key in [KEY_SPACE, KEY_ENTER]:
			_request_spawn()
	elif event is InputEventKey and event.pressed and event.physical_keycode in [KEY_F, KEY_G, KEY_X] \
			and not _client.get_recruiters().is_empty():
		if event.physical_keycode == KEY_X:
			_client.queue(-1)
		elif not _at_recruiter().is_empty():
			_client.queue(0 if event.physical_keycode == KEY_F else 1)
	elif event is InputEventKey and event.pressed and event.physical_keycode == KEY_F3 and not event.echo:
		_show_stats = not _show_stats
	elif event.is_action_pressed("toggle_view") and not event.is_echo():
		_first_person = not _first_person
		_pitch = clampf(_pitch, -1.2, 0.6)
	elif event.is_action_pressed("primary"):
		_weapon = 0
	elif event.is_action_pressed("sidearm"):
		_weapon = mini(1, _weapons.size() - 1)
	elif event is InputEventKey and event.pressed and event.physical_keycode == KEY_ESCAPE:
		if _mouse_captured:
			_capture_mouse(false)
		else:
			_leave()
			quit(0)


# Back from the pause menu: settings may have changed.
func _resume() -> void:
	if _settings.first_person != _view_setting:
		_first_person = _settings.first_person  # changed in the options
	if not (_spawn_screen != null and _choosing_spawn()):
		_capture_mouse(true)


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
	(character["soldier"] as Soldier).pose(state, _delta)


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


# Camera collision: how far behind the character the camera fits, against
# buildings (physics) and the world's own collision (a map's props).
# Physics queries belong in the physics step.
func _physics_process(_delta: float) -> bool:
	if _client == null or not _client.is_spawned():
		return false
	var pivot := _camera_pivot(_client.get_presented()["position"])
	var wanted := pivot + _camera_basis() * Vector3(CAMERA_SHOULDER, 0.3, CAMERA_DISTANCE)
	var query := PhysicsRayQueryParameters3D.create(pivot, wanted)
	var hit := root.get_world_3d().direct_space_state.intersect_ray(query)
	var reach := CAMERA_DISTANCE + 1.0
	if not hit.is_empty():
		reach = pivot.distance_to(hit["position"])
	var along: float = _client.raycast(pivot, wanted)
	if along >= 0:
		reach = minf(reach, along * pivot.distance_to(wanted))
	_camera_distance = CAMERA_DISTANCE if reach > CAMERA_DISTANCE else maxf(CAMERA_MIN_DISTANCE, reach - 0.25)
	return false


func _camera_basis() -> Basis:
	return Basis(Vector3.UP, _camera_yaw) * Basis(Vector3.RIGHT, _pitch)


func _camera_pivot(target: Vector3) -> Vector3:
	return target + Vector3.UP * CAMERA_HEIGHT * 0.8


func _update_camera(target: Vector3, delta: float) -> void:
	# First person while alive; dying, the camera pulls out to watch the body.
	var soldier := _soldier_of(_client.get_entity_id())
	var from_eyes := _first_person and soldier != null and bool(_client.get_presented()["alive"])
	if soldier != null:
		soldier.first_person = from_eyes
	if from_eyes:
		# From the eyes, looking where the character aims.
		if _bot != "":
			var own: Dictionary = _client.get_presented()
			_yaw = float(own["yaw"])
			_pitch = float(own["pitch"])
		_camera_yaw = _yaw
		var look := _camera_basis()
		var flat := Vector3(-sin(_yaw), 0, -cos(_yaw))
		_camera.global_position = soldier.eye() + flat * FIRST_PERSON_FORWARD
		_camera.global_basis = look
		return
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
			var carried := "" if int(own["reserve"]) < 0 else " (+%d)" % own["reserve"]
			ammo = "%d / %d%s%s" % [own["rounds"], weapon["rounds_per_clip"], carried, "  reloading" if own["reloading"] else ""]
		else:
			ammo = "heat %d%%%s" % [roundi(float(own["heat"]) * 100), "  OVERHEATED" if own["overheated"] else ""]
		var secondary: Dictionary = _client.get_secondary()
		var grenades := "" if secondary.is_empty() else " | %s x%d" % [secondary["name"], own["grenades"]]
		lines.append("health %d / %d | %s: %s%s | energy %d" % [roundi(own["health"]), roundi(_client.get_max_health()),
				weapon.get("name", "?"), ammo, grenades, roundi(own["energy"])])
	if _bf2_hud != null:
		lines.resize(1 if _show_stats else 0)  # Battlefront's HUD shows the rest
		_update_bf2_hud(delta)
		_update_screens()
	_hud.text = "\n".join(lines)
	_update_match_hud(delta)
	_hit_flash = maxf(0.0, _hit_flash - delta)
	_crosshair.visible = _bf2_hud == null
	_feed.visible = _bf2_hud == null
	_crosshair.text = "x" if _hit_flash > 0 else "+"
	_crosshair.modulate = Color(1, 0.3, 0.3) if _hit_flash > 0 else Color.WHITE
	_feed.text = "\n".join(_feed_lines)


# Battlefront's HUD: its events (bindings) from the game's state each frame.
# Names are the layout's own (player1.*).
func _update_bf2_hud(delta: float) -> void:
	for id: int in _fired.keys():
		_fired[id] = float(_fired[id]) + delta
	var b := _bf2_hud.bindings
	b.clear()
	_bf2_hud.map_markers.clear()
	_bf2_hud.map_player = {}
	if not _client.is_spawned():
		_bf2_hud.queue_redraw()
		return
	var own: Dictionary = _client.get_presented()
	var team := int(own["team"])
	var game: Dictionary = _client.get_match()
	var alive := bool(own["alive"]) and str(own.get("life", "alive")) == "alive"
	if alive:
		b["player1.weaponsEnable"] = true
		b["player1.weapon1.reticule.position"] = true
		b["player1.health"] = true
		b["player1.healthFraction"] = clampf(float(own["health"]) / maxf(1.0, _client.get_max_health()), 0.0, 1.0)
		b["player1.energyFraction"] = clampf(float(own["energy"]) / MAX_ENERGY, 0.0, 1.0)
		var target := _aimed_at(team)
		var tint := Color8(220, 220, 220)
		if not target.is_empty():
			tint = FRIEND_COLOR if team >= 0 and int(target["team"]) == team else ENEMY_COLOR
		b["player1.weapon1.target.teamColor"] = tint
		b["player1.weapon1.target.teamColorBright"] = tint.lightened(0.3)
		if _hit_flash > 0:
			b["player1.weapon1.target.hit"] = true
			b["player1.weapon1.target.hitColor"] = Color8(223, 60, 60)
		var slot: int = own["weapon"]
		var weapon: Dictionary = _weapons[slot] if slot < _weapons.size() else {}
		if not weapon.is_empty():
			b["player1.weapon1.name"] = _display_name(str(weapon["name"]))
			b["player1.weapon1.change"] = true
			b["player1.weapon1.mesh"] = str(weapon["name"])
			if int(weapon.get("rounds_per_clip", 0)) > 0:
				b["player1.weapon1.totalClipFraction"] = float(own["rounds"]) / float(weapon["rounds_per_clip"])
				if int(own["reserve"]) < 0:
					b["player1.weapon1.totalAmmoBullets"] = str(own["rounds"])
				else:
					b["player1.weapon1.totalAmmoBullets"] = "%d/%d" % [own["rounds"], own["reserve"]]
			else:
				b["player1.weapon1.heat"] = clampf(float(own["heat"]), 0.0, 1.0)
				b["player1.weapon1.ammoInfinite"] = true
		var secondary: Dictionary = _client.get_secondary()
		if not secondary.is_empty():
			b["player1.weapon2.name"] = _display_name(str(secondary["name"]))
			b["player1.weapon2.change"] = true
			b["player1.weapon2.mesh"] = str(secondary["name"])
			b["player1.weapon2.totalAmmoBullets"] = str(own["grenades"])
		var position: Vector3 = own["position"]
		_bf2_hud.map_player = {"position": Vector2(position.x, position.z), "yaw": _yaw if _bot == "" else float(own["yaw"])}
	if not game.is_empty() and team >= 0:
		for side in 2:
			var slot := "player1.team%d" % (1 if side == team else 2)
			b[slot + ".texture"] = _team_icon(side)
			b[slot + ".reinforcements"] = ceili(game["tickets"][side])
		var dominant := int(game.get("dominant", -1))
		if game["phase"] == "live" and dominant >= 0:
			b["victorytimer" if dominant == team else "defeattimer"] = ceili(game["victory_left"])
	if alive and not game.is_empty():
		_update_capture_ring(own, team, game)
	_update_minimap(team, game)
	_bf2_hud.apply_tables()
	_bf2_hud.queue_redraw()


# The spawn and score screens, from the match's state; the camera's field of
# view from the settings.
func _update_screens() -> void:
	_camera.fov = _settings.field_of_view
	var game: Dictionary = _client.get_match() if _client.is_spawned() else {}
	var own: Dictionary = _client.get_presented() if _client.is_spawned() else {}
	var team := int(own.get("team", -1))
	var choosing: bool = _choosing_spawn() and not game.is_empty() and game["phase"] != "ended"
	_spawn_screen.visible = choosing and not _pause_menu.visible
	if choosing and not _was_choosing and _bot == "":
		_capture_mouse(false)  # to click on the screen
	elif not choosing and _was_choosing and _bot == "" and not _pause_menu.visible:
		_capture_mouse(true)
	_was_choosing = choosing
	if _spawn_screen.visible:
		var loadouts: Array = []
		for entry: Dictionary in _battlefield["teams"][team]["loadouts"]:
			var weapons: Array = []
			for weapon: String in entry["weapons"] + ([entry["secondary"]] if entry["secondary"] != "" else []):
				weapons.append(_display_name(weapon))
			loadouts.append({"name": _unit_name(str(entry["name"])), "weapons": weapons,
					"max_health": entry["max_health"]})
		var posts: Array = []
		var owned := _owned_posts(game, team)
		for i in (game["posts"] as Array).size():
			var at: Vector3 = _battlefield["posts"][i]["position"]
			posts.append({"name": _battlefield["posts"][i]["name"], "position": Vector2(at.x, at.z),
					"color": _post_color(int(game["posts"][i]["owner"]), team), "ours": owned.has(i)})
		_spawn_screen.title = str(_battlefield["name"])
		_spawn_screen.side = {"name": _team_name(team), "icon": _team_icon(team), "color": FRIEND_COLOR}
		_spawn_screen.loadouts = loadouts
		_spawn_screen.loadout = _spawn_loadout
		_spawn_screen.posts = posts
		_spawn_screen.post = _spawn_post
		_spawn_screen.minimap = _bf2_hud.minimap
		_spawn_screen.waiting = _spawn_requested
		_spawn_screen.notice = "No reinforcements left" \
				if game["phase"] == "live" and ceili(game["tickets"][team]) <= 0 else ""
		_spawn_screen.queue_redraw()
	var ended: bool = not game.is_empty() and game["phase"] == "ended"
	_score_screen.visible = not game.is_empty() and team >= 0 and not _pause_menu.visible \
			and (ended or (Input.is_action_pressed("scores") and _bot == ""))
	_bf2_hud.visible = not (_pause_menu.visible or _spawn_screen.visible or _score_screen.visible)
	if _score_screen.visible:
		var everyone: Array = _client.get_remotes()
		var me := own.duplicate()
		me["id"] = _client.get_entity_id()
		everyone.append(me)
		var sides: Array = []
		for side in 2:
			var players: Array = []
			for p: Dictionary in everyone:
				if int(p["team"]) == side:
					players.append({"name": _who(p["id"]).capitalize() if int(p["id"]) == _client.get_entity_id()
							else _who(p["id"]), "kills": p["kills"], "deaths": p["deaths"], "captures": p["captures"],
							"you": int(p["id"]) == _client.get_entity_id()})
			sides.append({"name": _team_name(side), "icon": _team_icon(side),
					"color": FRIEND_COLOR if side == team else ENEMY_COLOR, "tickets": game["tickets"][side],
					"players": players})
		_score_screen.sides = sides
		_score_screen.own_team = team
		_score_screen.ended = ended
		_score_screen.winner = int(game.get("winner", -1))
		_score_screen.next_in = float(game.get("time_left", 0.0))
		_score_screen.queue_redraw()


# Battlefront's capture ring (player1cpstatus): shown while you stand in a
# command post's capture region; it fills with the post's charge in the
# color of the side it belongs to, its ring yellow while disputed.
func _update_capture_ring(own: Dictionary, team: int, game: Dictionary) -> void:
	var at: Vector3 = own["position"]
	for i in (game["posts"] as Array).size():
		var post: Dictionary = _battlefield["posts"][i]
		var center: Vector3 = post["position"]
		var radius := float(post["radius"])
		if Vector2(at.x - center.x, at.z - center.z).length_squared() > radius * radius:
			continue
		var state: Dictionary = game["posts"][i]
		var holder := int(state["holder"])
		var b := _bf2_hud.bindings
		b["player1.commandPost.charge"] = clampf(float(state["charge"]), 0.0, 1.0) if holder >= 0 else 0.0
		b["player1.commandPost.color"] = NEUTRAL_COLOR if holder < 0 else (FRIEND_COLOR if holder == team else ENEMY_COLOR)
		b["player1.commandPost.disputeColor"] = NEUTRAL_COLOR if bool(state["contested"]) else Color(0.75, 0.75, 0.75)
		return


# The minimap: its image where the battlefield file places it, the command
# posts, your side's soldiers, and enemies seen firing.
func _update_minimap(team: int, game: Dictionary) -> void:
	if _battlefield.is_empty():
		return
	var file := str(_map_meta.get("minimap", ""))
	if _bf2_hud.minimap.get("file", "") != file and file != "":
		var image := Image.load_from_file(ProjectSettings.globalize_path("res://" + str(_map_meta["dir"]).path_join(file)))
		var edges: Array = _battlefield.get("minimap", [0, 0, 0, 0])
		if image == null:
			return
		if edges.is_empty() or (float(edges[0]) == 0 and float(edges[2]) == 0):
			# No placement given: the image spans the battlefield's bounds.
			edges = [_battlefield["min_x"], _battlefield["min_z"], _battlefield["max_x"], _battlefield["max_z"]]
		_bf2_hud.minimap = {"file": file, "texture": ImageTexture.create_from_image(image), "left_x": edges[0],
				"top_z": edges[1], "right_x": edges[2], "bottom_z": edges[3]}
	if _bf2_hud.map_player.is_empty():
		return
	_bf2_hud.bindings["player1.map.enable"] = true
	if not game.is_empty():
		for i in (game["posts"] as Array).size():
			var post: Dictionary = _battlefield["posts"][i]
			var at: Vector3 = post["position"]
			_bf2_hud.map_markers.append({"position": Vector2(at.x, at.z), "kind": "post",
					"color": _post_color(int(game["posts"][i]["owner"]), team)})
	for remote: Dictionary in _client.get_remotes():
		if not bool(remote["alive"]):
			continue
		var friend := team >= 0 and int(remote["team"]) == team
		if not friend and float(_fired.get(int(remote["id"]), 1e9)) > SEEN_FIRING:
			continue
		var at: Vector3 = remote["position"]
		_bf2_hud.map_markers.append({"position": Vector2(at.x, at.z), "kind": "soldier",
				"color": FRIEND_COLOR if friend else ENEMY_COLOR})


# The character nearest the crosshair's line, if one is close to it.
func _aimed_at(_team: int) -> Dictionary:
	var from := _camera.global_position
	var forward := -_camera.global_basis.z
	var best := {}
	var best_angle := TARGET_CONE
	for remote: Dictionary in _client.get_remotes():
		if not bool(remote["alive"]):
			continue
		var to: Vector3 = remote["position"] + Vector3.UP * 1.1 - from
		var distance := to.length()
		if distance > TARGET_RANGE or distance < 0.5:
			continue
		var angle := forward.angle_to(to)
		if angle < maxf(best_angle, 0.6 / distance) and angle < best_angle + 0.6 / distance:
			best = remote
			best_angle = angle
	return best


func _team_icon(team: int) -> String:
	var name := str(_battlefield["teams"][team]["name"]).to_lower() if not _battlefield.is_empty() else ""
	if name.contains("rebel"):
		return "all_icon"
	if name.contains("empire") or name.contains("imperial"):
		return "imp_icon"
	if name.contains("republic"):
		return "rep_icon"
	if name.contains("separatist") or name.contains("cis"):
		return "cis_icon"
	return "all_icon" if team == 0 else "imp_icon"


# A loadout's name for the spawn screen (imp_inf_rifleman -> Rifleman). Test
# loadouts only: gear, not classes, decides what a soldier is.
static func _unit_name(loadout: String) -> String:
	var at := loadout.find("_inf_")
	return (loadout.substr(at + 5) if at >= 0 else loadout).replace("_", " ").capitalize()


# A weapon's name for the HUD from its class (imp_weap_inf_rocket_launcher ->
# Rocket Launcher). Galaxies weapons will bring their own names.
static func _display_name(weapon_class: String) -> String:
	var name := weapon_class
	for prefix in ["_weap_inf_", "_weap_"]:
		var at := name.find(prefix)
		if at >= 0:
			name = name.substr(at + prefix.length())
			break
	return name.replace("thermaldetonator", "thermal_detonator").replace("_", " ").capitalize()


# Hit markers and the kill feed.
func _update_events() -> void:
	var me: int = _client.get_entity_id()
	for event: Dictionary in _client.take_events():
		var mine := int(event["attacker"]) == me
		if event["kind"] == "hit":
			var victim := _soldier_of(int(event["victim"]))
			if victim != null:
				victim.flinch()
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
		if _bf2_hud != null and not _feed_lines.is_empty() and event["kind"] in ["kill", "captured", "neutralized"]:
			_bf2_hud.messages.append({"text": _feed_lines[-1], "age": 0.0,
					"color": _message_color(event)})
			if _bf2_hud.messages.size() > 16:
				_bf2_hud.messages.remove_at(0)


# A kill-feed line's color: blue when your side scored, red when the enemy did.
func _message_color(event: Dictionary) -> Color:
	var own_team := int(_client.get_presented().get("team", -1)) if _client.is_spawned() else -1
	var scorer := -1
	if event["kind"] == "kill":
		var attacker := int(event["attacker"])
		if attacker == _client.get_entity_id():
			scorer = own_team
		else:
			for remote: Dictionary in _client.get_remotes():
				if int(remote["id"]) == attacker:
					scorer = int(remote["team"])
	else:
		scorer = int(event["attacker"])  # captures: the team
	if scorer < 0 or own_team < 0:
		return Color8(223, 223, 223)
	return Color8(80, 140, 255) if scorer == own_team else Color8(223, 60, 60)


# The soldier drawn for an entity: yours or another player's; null if none.
func _soldier_of(id: int) -> Soldier:
	if id == _client.get_entity_id():
		return _own.get("soldier")
	if _remotes.has(id):
		return _remotes[id].get("soldier")
	return null


func _who(id: int) -> String:
	return "you" if id == _client.get_entity_id() else "player %d" % id


# Bolts: own shots fly from the moment they're fired (predicted), others'
# come from the server. Only looks: what they hit is the server's call.
func _update_bolts(delta: float) -> void:
	var own_soldier := _soldier_of(_client.get_entity_id())
	for shot: Dictionary in _client.take_shots():
		var weapon: Dictionary = _weapons[mini(int(shot["weapon"]), _weapons.size() - 1)]
		var origin: Vector3 = shot["origin"]
		var offset := Vector3.ZERO
		if own_soldier != null:
			if int(shot["weapon"]) >= 2:
				own_soldier.throw()
			else:
				own_soldier.fire()
			offset = own_soldier.muzzle() - origin
		for direction: Vector3 in shot["directions"]:
			var node := _bolt_node(shot)
			var velocity: Vector3 = direction * float(weapon["bolt_speed"])
			var bolt := {"node": node, "position": origin, "velocity": velocity, "life": float(weapon["bolt_life"]),
					"origin": origin, "offset": offset}
			_place_drawn_bolt(node, bolt)
			_own_bolts.append(bolt)
	for i in range(_own_bolts.size() - 1, -1, -1):
		var bolt: Dictionary = _own_bolts[i]
		bolt["life"] = float(bolt["life"]) - delta
		bolt["velocity"] = bolt["velocity"] + Vector3.DOWN * 9.8 * delta
		var node: MeshInstance3D = bolt["node"]
		if float(bolt["life"]) <= 0:
			node.queue_free()
			_own_bolts.remove_at(i)
			continue
		bolt["position"] = bolt["position"] + bolt["velocity"] * delta
		_place_drawn_bolt(node, bolt)

	_update_explosions(delta)
	var remote: Array = _client.get_bolts()
	while _remote_bolt_nodes.size() < remote.size():
		_remote_bolt_nodes.append(_bolt_node(remote[_remote_bolt_nodes.size()]))
	var seen := {}
	for i in _remote_bolt_nodes.size():
		var node := _remote_bolt_nodes[i]
		node.visible = i < remote.size()
		if node.visible:
			var bolt: Dictionary = remote[i]
			var id := int(bolt["id"])
			seen[id] = true
			if not _remote_bolt_starts.has(id):
				_remote_bolt_starts[id] = _bolt_start(bolt)
			_style_bolt(node, bolt)
			var start: Dictionary = _remote_bolt_starts[id]
			_place_drawn_bolt(node, {"position": bolt["position"], "velocity": bolt["velocity"],
					"origin": start["origin"], "offset": start["offset"]})
	for id: int in _remote_bolt_starts.keys():
		if not seen.has(id):
			_remote_bolt_starts.erase(id)


# A projectile seen for the first time: its shooter fires (or throws), and it's
# drawn from that shooter's muzzle. Bolts start at the eye (core::eye_height).
func _bolt_start(bolt: Dictionary) -> Dictionary:
	var owner := int(bolt["owner"])
	var soldier := _soldier_of(owner)
	if soldier == null or not soldier.model.visible:
		return {"origin": bolt["position"], "offset": Vector3.ZERO}
	_fired[owner] = 0.0
	if owner != _client.get_entity_id():  # own shots animated when predicted
		if bolt["kind"] == "grenade":
			soldier.throw()
		else:
			soldier.fire()
	var origin := soldier.model.global_position + Vector3.UP * EYE_HEIGHT
	return {"origin": origin, "offset": soldier.muzzle() - origin}


# Draws a bolt at its true position, shifted toward the muzzle it left: fully
# at first, none once it's MUZZLE_BLEND from where it was fired.
func _place_drawn_bolt(node: MeshInstance3D, bolt: Dictionary) -> void:
	var position: Vector3 = bolt["position"]
	var travelled := position.distance_to(bolt["origin"])
	var shift: Vector3 = bolt["offset"] * maxf(0.0, 1.0 - travelled / MUZZLE_BLEND)
	_place_bolt(node, position + shift, bolt["velocity"])


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
	var length := float(look["length"]) * BOLT_STRETCH
	if look.get("kind", "bolt") == "grenade":
		width = 0.22
		length = 0.22
	elif length <= 0 or width <= 0:
		width = 0.15  # an invisible bolt (the sonic blaster's): draw something
		length = 1.0
	node.set_meta("size", Vector3(width, width, length))


# Explosions: a flash that swells to the damage radius and fades.
func _update_explosions(delta: float) -> void:
	for explosion: Dictionary in _client.take_explosions():
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		material.albedo_color = Color(1.0, 0.6, 0.2, 0.8)
		material.emission_enabled = true
		material.emission = Color(1.0, 0.5, 0.15)
		material.emission_energy_multiplier = 4.0
		var node := MeshInstance3D.new()
		var sphere := SphereMesh.new()
		sphere.radius = 1.0
		sphere.height = 2.0
		node.mesh = sphere
		node.material_override = material
		node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		node.position = explosion["position"]
		var light := OmniLight3D.new()
		light.light_color = Color(1.0, 0.6, 0.3)
		light.omni_range = float(explosion["radius"]) * 4
		light.position = explosion["position"] + Vector3.UP
		root.add_child(node)
		root.add_child(light)
		_explosions.append({"node": node, "light": light, "age": 0.0, "radius": float(explosion["radius"])})
		_explosions_seen += 1
	for i in range(_explosions.size() - 1, -1, -1):
		var e: Dictionary = _explosions[i]
		e["age"] = float(e["age"]) + delta
		var t := float(e["age"]) / EXPLOSION_TIME
		var node: MeshInstance3D = e["node"]
		var light: OmniLight3D = e["light"]
		if t >= 1.0:
			node.queue_free()
			light.queue_free()
			_explosions.remove_at(i)
			continue
		node.scale = Vector3.ONE * float(e["radius"]) * (0.3 + 0.7 * sqrt(t))
		(node.material_override as StandardMaterial3D).albedo_color.a = 0.8 * (1.0 - t)
		light.light_energy = 6.0 * (1.0 - t)


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
	return {"model": model, "soldier": Soldier.new(model)}


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
	if Bf2Map.is_bf2(meta):
		mesh.material_override = Bf2Map.build_terrain_material(bake, meta)
	else:
		mesh.material_override = TerrainTool.build_material(bake, meta)
	root.add_child(mesh)


func _setup_scene(bake: String, meta: Dictionary) -> void:
	_map_meta = meta.duplicate()
	_map_meta["dir"] = bake.get_base_dir()
	_map_meta["bake"] = bake
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
	if Bf2Map.is_bf2(meta):
		_sky_dome = Bf2Map.setup_environment(root, bake, meta, environment, sun)
		Bf2Map.add_water(root, bake, meta, sun)
		Bf2Map.add_fog_cloud(root, bake, meta)

	_camera = Camera3D.new()
	_camera.fov = 65
	_camera.near = 0.05
	_camera.far = 8000
	root.add_child(_camera)

	var layer := CanvasLayer.new()
	_bf2_hud = Bf2Hud.new()
	if _bf2_hud.load_layout(HUD_DIR):
		for ammo in ["player1weapon1ammo", "player1weapon2ammo", "player1weapon1ammoInfinite",
				"player1weapon2ammoInfinite"]:
			_bf2_hud.nudges[ammo] = AMMO_NUDGE
		layer.add_child(_bf2_hud)
		_spawn_screen = SpawnScreen.new(_bf2_hud.art)
		_spawn_screen.visible = false
		_spawn_screen.loadout_chosen.connect(func(i: int) -> void: _spawn_loadout = i)
		_spawn_screen.post_chosen.connect(func(i: int) -> void: _spawn_post = i)
		_spawn_screen.spawn_requested.connect(_request_spawn)
		_score_screen = ScoreScreen.new(_bf2_hud.art)
		_score_screen.visible = false
		_pause_menu = PauseMenu.new(_bf2_hud.art)
		_pause_menu.settings = _settings
		_pause_menu.visible = false
		_pause_menu.resumed.connect(_resume)
		_pause_menu.leave_requested.connect(func() -> void:
			_leave()
			quit(0))
	else:
		_bf2_hud = null
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
	for screen: Control in [_spawn_screen, _score_screen, _pause_menu]:
		if screen != null:
			layer.add_child(screen)
	root.add_child(layer)

	_bolt_mesh = BoxMesh.new()
	_bolt_mesh.size = Vector3(1, 1, 1)


# Command posts: a beam, a ring around the capture region and a name, in the
# owner's color.
func _add_posts() -> void:
	if _battlefield.is_empty():
		return
	var holograms := _post_models()
	for post: Dictionary in _battlefield["posts"]:
		var model: Dictionary = holograms.get(str(post["name"]).to_lower(), {})
		if not model.is_empty():
			_posts.append({"hologram": _add_hologram(model)})
			continue
		# No Battlefront post here (e.g. a Galaxies test field): a beam, the
		# capture region's ring and the name.
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
		_posts.append({"material": material, "label": label, "nodes": [beam, ring, label]})


# The map's command post models with a hologram (bf2tool export-map), by
# name (cp1 ...), which is the post's: {position, hologram}.
func _post_models() -> Dictionary:
	var out := {}
	var path := "res://" + str(_map_meta.get("bake", "")) + ".objects.json"
	if _map_meta.get("bake", "") == "" or not FileAccess.file_exists(path):
		return out
	var data: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not data is Dictionary:
		return out
	for object: Dictionary in data.get("objects", []):
		if object.has("hologram"):
			var at: Array = object["position"]
			out[str(object["name"]).to_lower()] = {"position": Vector3(at[0], at[1], at[2]), "hologram": object["hologram"]}
	return out


# Battlefront's command post hologram: the holding side's insignia turning
# over the post's projector, and its light (colored by _update_hologram).
func _add_hologram(model: Dictionary) -> Dictionary:
	var spec: Dictionary = model["hologram"]
	var pivot := Node3D.new()
	pivot.position = model["position"] + Vector3.UP * float(spec["height"])
	pivot.scale = Vector3.ONE * float(spec["size"])
	root.add_child(pivot)
	var images := {}
	for side: String in spec["images"]:
		var document := GLTFDocument.new()
		var state := GLTFState.new()
		var file := ProjectSettings.globalize_path("res://" + str(_map_meta["dir"]).path_join(spec["images"][side]))
		if document.append_from_file(file, state) != OK:
			continue
		var scene: Node3D = document.generate_scene(state)
		scene.visible = false
		pivot.add_child(scene)
		var materials: Array = []
		for mesh: MeshInstance3D in scene.find_children("*", "MeshInstance3D", true, false):
			mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			for surface in mesh.mesh.get_surface_count():
				var source := mesh.mesh.surface_get_material(surface) as StandardMaterial3D
				var glow := source.duplicate() as StandardMaterial3D if source != null else StandardMaterial3D.new()
				glow.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
				glow.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
				glow.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
				glow.cull_mode = BaseMaterial3D.CULL_DISABLED
				glow.no_depth_test = false
				mesh.set_surface_override_material(surface, glow)
				materials.append(glow)
		images[side] = {"node": scene, "materials": materials}
	var light := OmniLight3D.new()
	light.omni_range = float(spec["light_radius"])
	light.light_energy = 0.5
	light.visible = false
	light.position = model["position"] + Vector3.UP * float(spec["height"])
	root.add_child(light)
	return {"pivot": pivot, "images": images, "light": light, "rate": float(spec["rotate_rate"])}


# The hologram key (HoloImageGeometry's side) for a team: from its icon.
func _holo_side(team: int) -> String:
	match _team_icon(team):
		"all_icon": return "alliance"
		"imp_icon": return "empire"
		"rep_icon": return "republic"
		"cis_icon": return "cis"
	return ""


# Arrived on another server (a battle, or back at the hub): everything about
# the last one goes.
func _arrive() -> void:
	for character: Dictionary in _remotes.values():
		(character["model"] as Node).queue_free()
	_remotes.clear()
	for post: Dictionary in _posts:
		var nodes: Array = post.get("nodes", [])
		if post.has("hologram"):
			nodes = [post["hologram"]["pivot"], post["hologram"]["light"]]
		for node: Node in nodes:
			node.queue_free()
	_posts.clear()
	for node: Node in _recruiter_nodes:
		node.queue_free()
	_recruiter_nodes.clear()
	for node: Node in _remote_bolt_nodes:
		node.queue_free()
	_remote_bolt_nodes.clear()
	for bolt: Dictionary in _own_bolts:
		(bolt["node"] as Node).queue_free()
	_own_bolts.clear()
	_battlefield = _client.get_battlefield()
	_add_posts()
	_spawn_requested = false
	_notice_id = 0
	print("transferred to ", "the battle on " + str(_battlefield["name"]) if not _battlefield.is_empty() else "the hub")


# War terminals: a yellow pillar, its name, and a ring where you can queue.
func _add_recruiters() -> void:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color(1.0, 0.8, 0.25)
	for recruiter: Dictionary in _client.get_recruiters():
		var position: Vector3 = recruiter["position"]
		var pillar := MeshInstance3D.new()
		var box := BoxMesh.new()
		box.size = Vector3(0.6, 2.2, 0.6)
		pillar.mesh = box
		pillar.material_override = material
		pillar.position = position + Vector3.UP * 1.1
		var ring := MeshInstance3D.new()
		var torus := TorusMesh.new()
		torus.outer_radius = float(recruiter["radius"])
		torus.inner_radius = float(recruiter["radius"]) - 0.15
		ring.mesh = torus
		ring.material_override = material
		ring.position = position + Vector3.UP * 0.1
		var label := Label3D.new()
		label.text = recruiter["name"]
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.fixed_size = true
		label.pixel_size = 0.0015
		label.font_size = 28
		label.outline_size = 8
		label.modulate = material.albedo_color
		label.position = position + Vector3.UP * 3.2
		for node: Node3D in [pillar, ring, label]:
			root.add_child(node)
			_recruiter_nodes.append(node)


# The war terminal we're standing at, if any.
func _at_recruiter() -> Dictionary:
	var position: Vector3 = _client.get_presented()["position"]
	for recruiter: Dictionary in _client.get_recruiters():
		var at: Vector3 = recruiter["position"]
		if Vector2(position.x - at.x, position.z - at.z).length() <= float(recruiter["radius"]):
			return recruiter
	return {}


# A message from the server, e.g. how the last battle went.
func _update_notice() -> void:
	var status: Dictionary = _client.get_player_status()
	if int(status.get("notice_id", 0)) == _notice_id:
		return
	_notice_id = int(status["notice_id"])
	_notice = status["notice"]
	_notice_time = 10.0
	if _notice != "":
		print("notice: ", _notice)
		_feed_lines.append(_notice)


# In a hub: the war terminal's prompt, or the queue.
func _update_hub_hud() -> void:
	var factions: PackedStringArray = _client.get_factions()
	var status: Dictionary = _client.get_player_status()
	if status.get("queue", "none") == "queued" and factions.size() == 2:
		_match_label.text = "[center]Queued for the %s: %d in the queue, the battle starts in %d s\n[X] leave the queue[/center]" % [
				factions[int(status["team"])], status["in_queue"], ceili(status["starts_in"])]
		return
	_match_label.text = ""
	var terminal := _at_recruiter()
	if not terminal.is_empty() and factions.size() == 2 and _center.text == "":
		_center.text = "%s\n[F] fight for the %s     [G] fight for the %s" % [terminal["name"], factions[0], factions[1]]


func _post_color(owner: int, own_team: int) -> Color:
	if owner < 0:
		return NEUTRAL_COLOR
	return FRIEND_COLOR if owner == own_team else ENEMY_COLOR


func _update_posts(game: Dictionary, own_team: int) -> void:
	if game.is_empty():
		return
	for i in mini(_posts.size(), (game["posts"] as Array).size()):
		var state: Dictionary = game["posts"][i]
		if _posts[i].has("hologram"):
			_update_hologram(_posts[i]["hologram"], state, own_team)
			continue
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


# Our colors rather than the hologram class's (green for friends): blue for
# your side, red for the enemy's, white while neutral or disputed. A neutral
# post shows the insignia of the side charging it, if any (Battlefront has no
# neutral image).
func _update_hologram(holo: Dictionary, state: Dictionary, own_team: int) -> void:
	var owner := int(state["owner"])
	var shown := owner if owner >= 0 else int(state["holder"])
	var side := _holo_side(shown) if shown >= 0 else ""
	var color := _post_color(owner, own_team)
	if bool(state["contested"]):
		color = NEUTRAL_COLOR
	var flicker := 0.85 + 0.15 * sin(_elapsed * 23.0) * sin(_elapsed * 7.0)
	(holo["pivot"] as Node3D).rotation.y = _elapsed * float(holo["rate"]) * TAU
	for key: String in holo["images"]:
		var image: Dictionary = holo["images"][key]
		(image["node"] as Node3D).visible = key == side
		if key == side:
			for material: StandardMaterial3D in image["materials"]:
				material.albedo_color = Color(color, 0.55 * flicker)
	var light: OmniLight3D = holo["light"]
	light.visible = side != ""
	light.light_color = color


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
		_client.request_spawn(_spawn_loadout, _spawn_post)
		_spawn_requested = true


# The spawn screen: an overview of the chosen post. Bots other than the C++
# one spawn with the first loadout at the first post their team holds.
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
	_notice_time = maxf(0.0, _notice_time - delta)
	var game: Dictionary = _client.get_match()
	if game.is_empty() or not _client.is_spawned():
		_match_label.text = ""
		var own_state: Dictionary = _client.get_presented() if _client.is_spawned() else {}
		if _client.get_status() in ["transferring", "connecting"] and _generation > 0:
			_center.text = "Traveling..."
		elif not own_state.is_empty() and not own_state["alive"]:
			_center.text = "Eliminated. Respawning..."
		elif _notice_time > 0 and _notice != "":
			_center.text = _notice
		if _client.is_spawned() and not _client.get_recruiters().is_empty():
			_update_hub_hud()
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
	if _bf2_hud != null:
		# Battlefront's HUD shows the tickets, posts (on the map) and the
		# victory timer; only the warmup, which Battlefront hasn't, stays.
		var warmup: bool = game["phase"] == "warmup"
		_match_label.text = "[center]\n\n\n\n\nWarmup: the match starts in %d s[/center]" % ceili(game["time_left"]) \
				if warmup else ""

	var position: Vector3 = own["position"]
	var inside := position.x >= float(_battlefield["min_x"]) and position.x <= float(_battlefield["max_x"]) \
			and position.z >= float(_battlefield["min_z"]) and position.z <= float(_battlefield["max_z"])
	_outside = 0.0 if inside or not own["alive"] else _outside + delta

	if _spawn_screen != null and (game["phase"] == "ended" or own["life"] == "waiting"):
		pass  # the score and spawn screens show these
	elif game["phase"] == "ended":
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
	var loadouts: Array = _battlefield["teams"][team]["loadouts"]
	_spawn_loadout = mini(_spawn_loadout, loadouts.size() - 1)
	for i in loadouts.size():
		var loadout: Dictionary = loadouts[i]
		lines.append("%s %d  %s  (%s, %d health)" % [">" if i == _spawn_loadout else "  ", i + 1, loadout["name"],
				", ".join(loadout["weapons"] + ([loadout["secondary"]] if loadout["secondary"] != "" else [])),
				roundi(loadout["max_health"])])
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
