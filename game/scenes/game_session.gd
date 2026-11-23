# Playable game session scene: runs a hub or conquest match client (and
# optional local server), using PlayerAppearance for all 10 species, HubUI,
# BattleUI, and PauseMenu. Launched either from MainMenu or net_demo.gd.
class_name GameSession
extends Node3D

const TerrainTool = preload("res://tools/render_terrain.gd")
const Terrain3DBake = preload("res://tools/terrain3d_bake.gd")
const SwgBuilding = preload("res://rendering/swg_building.gd")
const Bf2Map = preload("res://rendering/bf2_map.gd")
const SwgCity = preload("res://rendering/swg_city.gd")
const NATIVES_TEAM := 2  # a battlefield's natives (core::natives_team)
const Soldier = preload("res://rendering/soldier.gd")
const PlayerAppearance = preload("res://rendering/player_appearance.gd")
const Pickups = preload("res://rendering/pickups.gd")
const Bf2Effects = preload("res://rendering/bf2_effects.gd")
const Bf2Audio = preload("res://audio/bf2_audio.gd")
const Settings = preload("res://ui/settings.gd")
const PauseMenu = preload("res://ui/pause_menu.gd")
const HubUI = preload("res://scenes/hub_ui.gd")
const BattleUI = preload("res://scenes/battle_ui.gd")

const HUD_DIR := "res://imported_assets/hud"
const EFFECTS_DIR := "res://imported_assets/effects"
const AUDIO_DIR := "res://imported_assets/audio"
const SHOT_COUNT := 4
const MOUSE_SENSITIVITY := 0.003
const CAMERA_DISTANCE := 4.5
const CAMERA_HEIGHT := 2.0
const CAMERA_SHOULDER := 0.7
const CAMERA_MIN_DISTANCE := 0.6
const FIRST_PERSON_FORWARD := 0.12
const EYE_HEIGHT := 1.6
const CROUCHED_EYE_HEIGHT := 1.1
const CROUCH_CAMERA_DROP := 0.5
const CROUCH_CAMERA_RATE := 6.0
const ZOOM_RATE := 12.0
const AIM_DISTANCE := 40.0
const MUZZLE_BLEND := 20.0
const BOT_FIRE_RANGE := 40.0
const SPAWN_FIRE_LOCKOUT := 0.35

static var pending_config: Dictionary = {}

signal finished(exit_code: int)

var _config: Dictionary = {}
var _started := false
var _exiting := false
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
var _own: Dictionary = {}
var _remotes: Dictionary = {}
var _delta := 0.0
var _saw_remote := false
var _solo := false
var _announced := false
var _camera: Camera3D
var _sky_dome: Node3D
var _camera_yaw := 0.0
var _camera_distance := CAMERA_DISTANCE
var _camera_sphere := SphereShape3D.new()
var _first_person := false
var _route: Array[Vector2] = []
var _look := Vector2.INF
var _route_index := -1
var _hit_flash_unused := 0.0
var _weapons: Array = []
var _weapon := 0
var _battlefield: Dictionary = {}
var _crouch_view := 0.0
var _bot_difficulty := ""
var _zoom_step := 0
var _zoom := 1.0
var _zoom_weapon := -1
var _pickups: Pickups
var _effects: Bf2Effects
var _audio: Bf2Audio
var _prev_reloading := false
var _prev_overheated := false
var _prev_weapon := -1
var _prev_health := -1.0
var _prev_reserve := -1
var _prev_grenades := -1
var _empty_click_cd := 0.0
var _hurt_vo_cd := {}
var _prev_pickups := {}
var _fire_lockout := 0.0
var _wait_fire_release := false
var _was_alive := false
var _powerup_nodes: Array = []
var _generation := 0
var _explosions_seen := 0
var _own_bolts: Array = []
var _remote_bolts := {}
var _kills := 0
var _deaths := 0
var _hits := 0
var _pending_shot := ""
var _settings: Settings
var _pause_menu: PauseMenu
var _hub_ui: HubUI
var _battle_ui: BattleUI
var _view_setting := false
var _show_stats := false
var _map_meta := {}
var _world: Node3D
var _world_bake := ""
var _start_bake := ""
var _buildings: Array = []
var _terrain3d := false
var _doors: Array[Node3D] = []
var _people := {}
var _people_colors := {}
var _shot_delay := 0


func _ready() -> void:
	if not pending_config.is_empty():
		var cfg := pending_config.duplicate(true)
		pending_config.clear()
		start_session(cfg)


func start_session(config: Dictionary) -> bool:
	_config = config.duplicate(true)
	if not ClassDB.class_exists("SwmClient"):
		printerr("SwmClient GDExtension not loaded")
		_emit_or_menu(2)
		return false
	var bake := str(_config.get("bake", "imported_assets/terrain/tatooine_mos_eisley"))
	_character_path = str(_config.get("character_path", "imported_assets/characters/stormtrooper/stormtrooper.gltf"))
	var meta: Variant = JSON.parse_string(FileAccess.get_file_as_string(bake + ".json"))
	if meta == null:
		printerr("cannot load terrain ", bake)
		_emit_or_menu(1)
		return false

	_bot = str(_config.get("bot", ""))
	_seconds = float(_config.get("seconds", 0.0))
	_shots = str(_config.get("shots", ""))
	_solo = bool(_config.get("solo", false))
	_settings = Settings.new()
	_settings.load_file()
	_first_person = bool(_config.get("first_person", false)) or _settings.first_person
	var bake_path := _os_path(bake)

	for point: Variant in _config.get("route", []):
		if point is Vector2:
			_route.append(point)
	var look_v: Variant = _config.get("look", Vector2.INF)
	if look_v is Vector2:
		_look = look_v

	var building: Array = _config.get("building", [])
	var collision := ""
	var building_position := Vector3.ZERO
	var building_yaw := 0.0
	if building.size() >= 5:
		collision = _os_path(str(building[0])).get_basename() + ".collision"
		building_position = Vector3(float(building[1]), float(building[2]), float(building[3]))
		building_yaw = float(building[4])

	var server_addr := str(_config.get("server", "127.0.0.1:7777"))
	var battlefield := str(_config.get("battlefield", ""))
	var host_val: Variant = _config.get("host", null)
	if host_val != null and str(host_val) != "":
		_server = ClassDB.instantiate("SwmServer")
		var database := str(_config.get("db", ""))
		if database != "":
			_server.set_database(_os_path(database))
		if collision != "" and _server.add_building(collision, building_position, building_yaw) != OK:
			_emit_or_menu(1)
			return false
		var loadout_path := str(_config.get("loadout", ""))
		if loadout_path != "" and _server.set_loadout(_os_path(loadout_path)) != OK:
			_emit_or_menu(1)
			return false
		if battlefield != "" and _server.set_battlefield(_os_path(battlefield)) != OK:
			_emit_or_menu(1)
			return false
		_server.set_bots(int(_config.get("bots", 0)))
		_server.set_bot_difficulty(_settings.bot_difficulty)
		_bot_difficulty = _settings.bot_difficulty
		var hub_path := str(_config.get("hub", ""))
		if hub_path != "" and _server.set_hub(_os_path(hub_path)) != OK:
			_emit_or_menu(1)
			return false
		var spawn := _resolve_spawn(meta as Dictionary)
		if _server.start(int(host_val), bake_path, spawn) != OK:
			_emit_or_menu(1)
			return false
		server_addr = "127.0.0.1:%d" % _server.get_port()
		print("hosting on port ", _server.get_port())

	_client = ClassDB.instantiate("SwmClient")
	_client.set_simulated_network(float(_config.get("lag", 0.0)), float(_config.get("jitter", 0.0)),
			float(_config.get("loss", 0.0)))
	if collision != "" and _client.add_building(collision, building_position, building_yaw) != OK:
		_emit_or_menu(1)
		return false
	var cl_loadout := str(_config.get("loadout", ""))
	if cl_loadout != "" and _client.set_loadout(_os_path(cl_loadout)) != OK:
		_emit_or_menu(1)
		return false
	if battlefield != "" and _client.set_battlefield(_os_path(battlefield)) != OK:
		_emit_or_menu(1)
		return false
	_client.set_bot(_bot == "conquest")
	_client.set_battlefield_dir(_os_path("battlefields"))
	_weapons = _client.get_weapons()
	var player_name := str(_config.get("name", "player"))
	if _client.start(server_addr, bake_path, player_name) != OK:
		_emit_or_menu(1)
		return false
	_battlefield = _client.get_battlefield()

	_terrain3d = bool(_config.get("terrain3d", false))
	if building.size() >= 5:
		_buildings.append([str(building[0]), building_position, building_yaw])
	_start_bake = _game_path(_client.get_bake())
	if not _build_world(_start_bake):
		_emit_or_menu(1)
		return false

	_own = _spawn_character_model(
			str(_config.get("species", "human")),
			str(_config.get("gender", "male")),
			str(_config.get("appearance", "")))
	if _own.is_empty():
		_emit_or_menu(1)
		return false

	_setup_scene()
	_battle_ui.set_world_context(_world_bake, _map_meta, _battlefield)
	_battle_ui.add_posts()
	_bind_powerups()
	if _bot == "" and not DisplayServer.get_name().to_lower().contains("headless"):
		DisplayServer.window_set_size(Vector2i(1280, 720))
		_capture_mouse(true)
	_started = true
	return true


func get_client() -> Object:
	return _client


func get_server() -> Object:
	return _server


func leave_session() -> void:
	_leave()


func _process(delta: float) -> void:
	if not _started or _exiting:
		return
	step_frame(delta)


func step_frame(delta: float) -> void:
	_elapsed += delta
	_delta = delta
	_empty_click_cd = maxf(0.0, _empty_click_cd - delta)
	_fire_lockout = maxf(0.0, _fire_lockout - delta)
	if _wait_fire_release and not Input.is_action_pressed("fire"):
		_wait_fire_release = false
	if _server != null:
		if _settings.bot_difficulty != _bot_difficulty:
			_bot_difficulty = _settings.bot_difficulty
			_server.set_bot_difficulty(_bot_difficulty)
		_server.update()
	if _bot != "conquest":
		_client.set_input(_controls())
	_client.update()
	if _client.get_generation() != _generation:
		_generation = _client.get_generation()
		_arrive()
	if _client.get_session_error() != "":
		printerr(_client.get_session_error())
		_finish(1)
		return

	if _client.is_spawned():
		var own: Dictionary = _client.get_presented()
		_ensure_character_appearance(_own, own, true)
		var alive_now: bool = bool(own["alive"]) and str(own.get("life", "alive")) == "alive"
		if alive_now and not _was_alive:
			_lock_spawn_fire()
		_was_alive = alive_now
		if not _announced:
			_announced = true
			var at: Vector3 = own["position"]
			print("spawned as character %d at Galaxies (%.2f, %.2f, %.2f)" % [
					_client.get_character_id(), -at.x, at.y, at.z])
		_weapons = _client.get_weapons()
		_pose(_own, own)
		_update_remotes(int(own["team"]))
		var game: Dictionary = _client.get_match()
		_battle_ui.update_posts(game, int(own["team"]), _elapsed)
		_update_powerups(game, delta)
		for door_snd: Dictionary in Bf2Map.animate_doors(get_tree(), game.get("doors", PackedFloat32Array())):
			if _audio != null:
				_audio.play_3d(str(door_snd["sound"]), door_snd["position"], _camera.global_position)
		Bf2Map.animate_world(get_tree(), delta, game.get("destructibles", PackedFloat32Array()), float(game.get("clock", -1.0)))
		for blast: Dictionary in Bf2Map.update_destructibles(get_tree(), game.get("destructibles", PackedFloat32Array())):
			if _effects != null:
				var radius := float(blast["radius"])
				_effects.spawn_explosion(blast["position"], Vector3.UP, radius, str(blast["effect"]),
						Color(1.0, 0.7, 0.3), radius * 2.0)
			if _audio != null:
				var exp_snd := str(blast.get("sound", ""))
				_audio.play_3d(exp_snd if exp_snd != "" else "com_weap_obj_med_exp",
						blast["position"], _camera.global_position)
		if own["life"] == "waiting" and not game.is_empty():
			_choose_spawn(game, own)
		else:
			_battle_ui.spawn_requested_flag = false
			_update_camera(own["position"], delta)
		_battle_ui.update_team_tags(_camera, delta, _client.raycast)
		Bf2Map.animate_powerups(get_tree(), _elapsed)
		if _pickups != null:
			_pickups.update(_client.get_pickups(), delta)
		_update_bolts(delta)
		_update_events()
		_update_own_audio(own, game)
		_battle_ui.update_match_audio(game, own)
		if not _doors.is_empty():
			var people: Array[Vector3] = [own["position"]]
			for remote: Dictionary in _client.get_remotes():
				if bool(remote["alive"]):
					people.append(remote["position"])
			SwgCity.update_doors(_doors, people, delta)

	_battle_ui.update_hud(_client, _weapons, _remotes.size(), _client.get_max_health(),
			_show_stats, delta, _pause_menu != null and _pause_menu.visible,
			_hub_ui != null and _hub_ui.is_terminal_open(), _hub_ui, _generation, _bot,
			_camera, _yaw if _bot == "" else float(_client.get_presented().get("yaw", _yaw)),
			_capture_mouse, _who)
	if _hub_ui != null:
		_hub_ui.update_hub(_client, _world, delta, _pause_menu != null and _pause_menu.visible,
				_battle_ui.center_label(), _battle_ui.match_label())
		for line: String in _hub_ui.take_notice_lines():
			_battle_ui.add_feed_line(line)

	if _audio != null:
		_audio.update(delta, _camera.global_position)
	if _sky_dome != null:
		_sky_dome.global_position = _camera.global_position

	if _pending_shot != "":
		_shot_delay -= 1
		if _shot_delay <= 0:
			get_viewport().get_texture().get_image().save_png(_pending_shot)
			print("wrote ", _pending_shot)
			_pending_shot = ""
	elif _shots != "" and _seconds > 0 and _shots_taken < SHOT_COUNT \
			and _elapsed >= _seconds * (_shots_taken + 1) / (SHOT_COUNT + 0.5):
		_shots_taken += 1
		_pending_shot = "%s%02d.png" % [_shots, _shots_taken]
		_shot_delay = 2

	if _seconds > 0 and _elapsed >= _seconds and _pending_shot == "":
		_finish(-1)
		return
	if _client.get_status() == "timed_out":
		printerr("connection timed out")
		_finish(-1)
		return
	if _client.get_status() == "refused":
		printerr("refused: ", _client.get_refusal())
		_finish(-1)


func _physics_process(_delta: float) -> void:
	if not _started or _exiting or _client == null or not _client.is_spawned():
		return
	var pivot := _camera_pivot(_client.get_presented()["position"])
	var wanted := pivot + _camera_basis() * Vector3(CAMERA_SHOULDER, 0.3, CAMERA_DISTANCE)
	var query := PhysicsShapeQueryParameters3D.new()
	query.shape = _camera_sphere
	query.transform = Transform3D(Basis.IDENTITY, pivot)
	query.motion = wanted - pivot
	var fractions := get_world_3d().direct_space_state.cast_motion(query)
	var reach := CAMERA_DISTANCE + 1.0
	if fractions.size() == 2 and fractions[0] < 1.0:
		reach = fractions[0] * pivot.distance_to(wanted) + 0.25
	var along: float = _client.raycast(pivot, wanted)
	if along >= 0:
		reach = minf(reach, along * pivot.distance_to(wanted))
	_camera_distance = CAMERA_DISTANCE if reach > CAMERA_DISTANCE else maxf(CAMERA_MIN_DISTANCE, reach - 0.25)


func _input(event: InputEvent) -> void:
	if not _started or _exiting or _bot != "":
		return
	var escape: bool = event is InputEventKey and event.pressed and not event.echo \
			and (event as InputEventKey).physical_keycode == KEY_ESCAPE
	if _pause_menu != null and _pause_menu.visible:
		if escape and not _pause_menu.is_rebinding():
			_pause_menu.visible = false
			_resume()
		return
	if _hub_ui != null and _hub_ui.is_terminal_open():
		if escape or event.is_action_pressed("interact"):
			_hub_ui.close_terminal()
		elif event.is_action_pressed("leave_queue"):
			_client.queue(-1)
		elif event is InputEventKey and event.pressed and not event.echo \
				and (event as InputEventKey).physical_keycode in [KEY_1, KEY_2]:
			_client.queue(0 if (event as InputEventKey).physical_keycode == KEY_1 else 1)
		return
	if escape and _pause_menu != null:
		_capture_mouse(false)
		_pause_menu.in_match = not _battlefield.is_empty()
		_view_setting = _settings.first_person
		_pause_menu.open()
		return
	var choosing := _battle_ui.is_choosing_spawn(_client)
	var screen_spawn := _battle_ui.has_bf2_hud() and choosing
	if event is InputEventMouseMotion and _mouse_captured:
		var motion := event as InputEventMouseMotion
		var sensitivity := MOUSE_SENSITIVITY * _settings.mouse_sensitivity / _zoom
		_yaw -= motion.relative.x * sensitivity
		var lowest := -1.35 if _first_person else -1.2
		var highest := 1.35 if _first_person else 0.6
		var up := -1.0 if _settings.invert_mouse else 1.0
		_pitch = clampf(_pitch - motion.relative.y * sensitivity * up, lowest, highest)
	elif event is InputEventMouseButton and event.pressed and not _mouse_captured and not screen_spawn:
		_capture_mouse(true)
		_lock_spawn_fire()
	elif event is InputEventMouseButton and event.pressed and choosing and not screen_spawn:
		_request_spawn()
	elif event is InputEventKey and event.pressed and choosing and (event as InputEventKey).physical_keycode != KEY_ESCAPE:
		var key: int = (event as InputEventKey).physical_keycode
		var loadouts: Array = _battlefield["teams"][int(_client.get_presented()["team"])]["loadouts"]
		if key >= KEY_1 and key <= KEY_9:
			_battle_ui.spawn_loadout = mini(key - KEY_1, loadouts.size() - 1)
		elif key in [KEY_Q, KEY_LEFT]:
			_battle_ui.cycle_post(_client, -1)
		elif key in [KEY_E, KEY_RIGHT]:
			_battle_ui.cycle_post(_client, 1)
		elif key in [KEY_SPACE, KEY_ENTER]:
			_request_spawn()
	elif event.is_action_pressed("leave_queue") and not _client.get_recruiters().is_empty():
		_client.queue(-1)
	elif event.is_action_pressed("interact") and not event.is_echo() and _hub_ui != null \
			and _hub_ui.try_open_terminal(_client):
		_capture_mouse(false)
	elif (_hub_ui == null or not _hub_ui.has_ui()) and event is InputEventKey and event.pressed \
			and (event as InputEventKey).physical_keycode in [KEY_F, KEY_G] \
			and _hub_ui != null and not _hub_ui.at_recruiter(_client).is_empty():
		_client.queue(0 if (event as InputEventKey).physical_keycode == KEY_F else 1)
	elif event is InputEventKey and event.pressed and (event as InputEventKey).physical_keycode == KEY_F3 and not event.echo:
		_show_stats = not _show_stats
	elif event.is_action_pressed("toggle_view") and not event.is_echo():
		_first_person = not _first_person
		_pitch = clampf(_pitch, -1.2, 0.6)
	elif event.is_action_pressed("zoom") and not event.is_echo():
		var steps := _zoom_steps()
		_zoom_step = 0 if steps.is_empty() else (_zoom_step + 1) % (steps.size() + 1)
		_zoom_weapon = _weapon
	elif event.is_action_pressed("primary"):
		_weapon = 0
	elif event.is_action_pressed("sidearm"):
		_weapon = mini(1, _weapons.size() - 1)
	elif escape:
		if _mouse_captured:
			_capture_mouse(false)
		else:
			_finish(0)


func _exit_tree() -> void:
	_dispose_resources()


func _dispose_resources() -> void:
	for person: Variant in _people.values():
		if person != null and is_instance_valid(person):
			(person as Node).free()
	_people.clear()
	_people_colors.clear()
	if _pickups != null:
		_pickups.dispose()
		_pickups = null
	if _effects != null:
		_effects.dispose()
		_effects = null
	if _audio != null:
		_audio.dispose()
		_audio = null


func _finish(forced_exit: int) -> void:
	if _exiting:
		return
	_exiting = true
	var stats: Dictionary = _client.get_stats() if _client != null else {}
	var at: Vector3 = _client.get_presented().get("position", Vector3.ZERO) if _client != null else Vector3.ZERO
	print("left at Galaxies (%.2f, %.2f, %.2f)" % [-at.x, at.y, at.z])
	print("combat: %d hits, %d kills, %d deaths, %d explosions seen" % [_hits, _kills, _deaths, _explosions_seen])
	var game: Dictionary = _client.get_match() if _client != null else {}
	if not game.is_empty():
		var own: Dictionary = _client.get_presented()
		print("match: %s, tickets %d to %d; team %s, %d captures" % [
				game["phase"], ceili(game["tickets"][0]), ceili(game["tickets"][1]),
				_battle_ui._team_name(int(own["team"])), own["captures"]])
	if _client != null:
		print("status %s, entity %d, %d ticks, %d snapshots, %d corrections (max %.3f m), rtt %.0f ms, saw other players: %s" % [
				_client.get_status(), _client.get_entity_id(), stats.get("ticks", 0), stats.get("snapshots", 0),
				stats.get("corrections", 0), stats.get("max_correction", 0.0), stats.get("rtt_ms", 0.0), _saw_remote])
	var spawned: bool = _client != null and _client.is_spawned()
	_leave()
	_dispose_resources()
	var code := forced_exit if forced_exit >= 0 else (0 if spawned and (_saw_remote or _solo) else 1)
	_emit_or_menu(code)


func _emit_or_menu(code: int) -> void:
	_capture_mouse(false)
	if bool(_config.get("return_to_menu", false)):
		get_tree().change_scene_to_file("res://scenes/main_menu.tscn")
	else:
		finished.emit(code)


func _leave() -> void:
	if _client != null:
		_client.leave()
		for _i in 20:
			_client.update()
			if _server != null:
				_server.update()
			OS.delay_msec(10)
	if _server != null:
		_server.stop()
		_server = null


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
			var eye: Vector3 = _client.get_presented()["position"] + Vector3.UP * float(_own.get("eye_height", EYE_HEIGHT))
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
	var can_fire := _fire_lockout <= 0.0 and not _wait_fire_release
	return {"forward": forward, "right": right, "yaw": aim.x, "pitch": aim.y,
			"sprint": Input.is_action_pressed("sprint"), "jump": Input.is_action_pressed("jump"),
			"fire": Input.is_action_pressed("fire") and can_fire, "reload": Input.is_action_pressed("reload"),
			"secondary": Input.is_action_pressed("grenade"), "crouch": Input.is_action_pressed("crouch"),
			"weapon": _weapon}


func _aim() -> Vector2:
	if not _client.is_spawned():
		return Vector2(_yaw, 0)
	var own: Dictionary = _client.get_presented()
	var eye_h := float(_own.get("crouched_eye_height", CROUCHED_EYE_HEIGHT)) if own.get("crouched", false) \
			else float(_own.get("eye_height", EYE_HEIGHT))
	var eye: Vector3 = own["position"] + Vector3.UP * eye_h
	var target := _camera.global_position - _camera.global_basis.z * AIM_DISTANCE
	var to := target - eye
	return Vector2(atan2(-to.x, -to.z), atan2(to.y, Vector2(to.x, to.z).length()))


func _resume() -> void:
	if _settings.first_person != _view_setting:
		_first_person = _settings.first_person
	if not (_battle_ui.has_bf2_hud() and _battle_ui.is_choosing_spawn(_client)):
		_capture_mouse(true)


func _capture_mouse(captured: bool) -> void:
	_mouse_captured = captured
	if DisplayServer.get_name().to_lower().contains("headless"):
		return
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if captured else Input.MOUSE_MODE_VISIBLE


func _nearest_remote() -> Dictionary:
	var own_position: Vector3 = _client.get_presented()["position"]
	var best: Dictionary = {}
	for remote: Dictionary in _client.get_remotes():
		if int(remote.get("look", 0)) > 0:
			continue
		if best.is_empty() or own_position.distance_to(remote["position"]) < own_position.distance_to(best["position"]):
			best = remote
	return best


func _nearest_enemy() -> Dictionary:
	var own: Dictionary = _client.get_presented()
	var best: Dictionary = {}
	for remote: Dictionary in _client.get_remotes():
		var enemy: bool = int(own["team"]) < 0 or not _client.is_allied(int(own["team"]), int(remote["team"]))
		if not enemy or not remote["alive"] or int(remote.get("look", 0)) > 0:
			continue
		var position: Vector3 = remote["position"]
		if best.is_empty() or position.distance_to(own["position"]) < (best["position"] as Vector3).distance_to(own["position"]):
			best = remote
	return best


# A battlefield's native (team 2), by the battlefield's natives' look: a
# character exported to imported_assets/characters/<look>/, or
# "species:<id>", one of the playable species' bodies.
func _spawn_native_model() -> Dictionary:
	var natives: Dictionary = _client.get_battlefield().get("natives", {})
	var look := str(natives.get("look", ""))
	if look.begins_with("species:"):
		return _spawn_character_model(look.trim_prefix("species:"), "male", "")
	var path := "res://imported_assets/characters/%s/%s.gltf" % [look, look]
	var character := PlayerAppearance.load_character("native_" + look, "male", "", path)
	if not character.is_empty():
		# Its eyes (and the name over it) by its own height: an Ewok's or a
		# Jawa's is half a human's.
		var height := 0.0
		for mesh: Node in (character["model"] as Node).find_children("*", "MeshInstance3D", true, false):
			var box := (mesh as MeshInstance3D).get_aabb()
			height = maxf(height, box.position.y + box.size.y)
		if height > 0.3:
			character["eye_height"] = height * 0.9
			character["crouched_eye_height"] = height * 0.65
		add_child(character["model"])
	return character


func _spawn_character_model(species_id: String, gender: String, appearance_json: String) -> Dictionary:
	var character := PlayerAppearance.load_character(species_id, gender, appearance_json, _character_path)
	if not character.is_empty():
		add_child(character["model"])
	return character


func _ensure_character_appearance(character: Dictionary, state: Dictionary, is_own: bool) -> void:
	var sp := str(state.get("species", "human"))
	var gen := str(state.get("gender", "male"))
	var app := str(state.get("appearance", ""))
	if sp == str(character.get("species", "")) and gen == str(character.get("gender", "")) \
			and str(character.get("raw_appearance", "")) == app:
		return
	var replacement := PlayerAppearance.load_character(sp, gen, app, _character_path)
	if replacement.is_empty():
		return
	var old_model: Variant = character.get("model")
	if old_model is Node and is_instance_valid(old_model):
		(old_model as Node).queue_free()
	for k: String in replacement:
		character[k] = replacement[k]
	character["raw_appearance"] = app
	add_child(character["model"])
	if is_own:
		PlayerAppearance.set_first_person(character, _first_person)


func _update_remotes(own_team: int) -> void:
	var seen := {}
	for remote: Dictionary in _client.get_remotes():
		var id: int = remote["id"]
		seen[id] = true
		if int(remote.get("look", 0)) > 0:
			_update_person(id, remote)
			continue
		_saw_remote = true
		if not _remotes.has(id):
			var character := _spawn_native_model() if int(remote["team"]) == NATIVES_TEAM else \
					_spawn_character_model(
					str(remote.get("species", "human")),
					str(remote.get("gender", "male")),
					str(remote.get("appearance", "")))
			if character.is_empty():
				continue
			character["raw_appearance"] = str(remote.get("appearance", ""))
			var tag := Label3D.new()
			tag.billboard = BaseMaterial3D.BILLBOARD_ENABLED
			tag.fixed_size = true
			tag.pixel_size = 0.0012
			tag.font_size = 24
			tag.outline_size = 6
			tag.no_depth_test = true
			tag.position = Vector3.UP * (float(character.get("eye_height", 1.6)) + 0.65)
			(character["model"] as Node3D).add_child(tag)
			character["tag"] = tag
			_remotes[id] = character
		elif int(remote["team"]) != NATIVES_TEAM:
			_ensure_character_appearance(_remotes[id], remote, false)
		_pose(_remotes[id], remote)
		var tag: Label3D = _remotes[id]["tag"]
		var friend: bool = own_team >= 0 and _client.is_allied(own_team, int(remote["team"]))
		# Natives' holograms over their own heads (soldiers': the usual height).
		var holo_height := float(_remotes[id]["eye_height"]) + 0.65 if int(remote["team"]) == NATIVES_TEAM else 2.25
		var shown_holo := _battle_ui.show_team_tag(id, _remotes[id]["model"], int(remote["team"]), own_team,
				bool(remote["alive"]), friend, holo_height)
		tag.visible = not shown_holo
		if tag.visible:
			var rname := str(remote.get("name", ""))
			tag.text = rname if rname != "" else "player %d" % id
			tag.modulate = BattleUI.FRIEND_COLOR if friend else BattleUI.ENEMY_COLOR
	for id: int in _remotes.keys():
		if not seen.has(id):
			(_remotes[id]["model"] as Node).queue_free()
			_remotes.erase(id)
			_battle_ui.forget_team_tag(id)


func _update_person(id: int, state: Dictionary) -> void:
	if not _remotes.has(id):
		var look := int(state["look"])
		if not _people.has(look):
			_people[look] = _load_person(look)
		if _people[look] == null:
			return
		var model := (_people[look] as Node3D).duplicate() as Node3D
		SwgColors.apply(model, _people_colors.get(look, {}), int(state.get("colors", 0)))
		add_child(model)
		_remotes[id] = {"model": model, "person": true,
				"player": model.find_child("AnimationPlayer", true, false)}
	var person: Dictionary = _remotes[id]
	var model: Node3D = person["model"]
	model.position = state["position"]
	model.rotation.y = float(state["yaw"]) + PI
	var player := person["player"] as AnimationPlayer
	if player == null:
		return
	var velocity: Vector3 = state["velocity"]
	var speed := Vector2(velocity.x, velocity.z).length()
	var clip := "sit" if int(state.get("pose", 0)) == 1 else ("walk" if speed > 0.3 else "idle")
	if player.current_animation != clip and player.has_animation(clip):
		player.get_animation(clip).loop_mode = Animation.LOOP_LINEAR
		player.play(clip, 0.25)
	player.speed_scale = clampf(speed / 1.4, 0.6, 1.6) if clip == "walk" else 1.0


func _load_person(look: int) -> Node3D:
	var looks: PackedStringArray = _client.get_looks()
	if look < 1 or look > looks.size():
		return null
	var stem := looks[look - 1].get_file().get_basename().trim_prefix("shared_")
	var path := "res://" + _start_bake + "_city/people/" + stem + ".gltf"
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		printerr("no person exported for ", looks[look - 1], " (", path, ")")
		return null
	_people_colors[look] = SwgColors.load_colors(path)
	return document.generate_scene(state) as Node3D


func _pose(character: Dictionary, state: Dictionary) -> void:
	(character["soldier"] as Soldier).pose(state, _delta)


func _follow_route() -> Dictionary:
	if not _client.is_spawned():
		return {"yaw": _yaw}
	var position: Vector3 = _client.get_presented()["position"]
	if _route.is_empty():
		var remote := _nearest_remote()
		if not remote.is_empty():
			var to: Vector3 = remote["position"] - position
			_yaw = atan2(-to.x, -to.z)
		return {"yaw": _yaw}
	if _route_index < 0:
		_route_index = 0
		for i in _route.size():
			if Vector2(position.x, position.z).distance_to(_route[i]) \
					< Vector2(position.x, position.z).distance_to(_route[_route_index]):
				_route_index = i
	while _route_index < _route.size() and Vector2(position.x, position.z).distance_to(_route[_route_index]) < 0.8:
		_route_index += 1
	if _route_index >= _route.size():
		var target := _look
		if target == Vector2.INF:
			var remote := _nearest_remote()
			if not remote.is_empty():
				target = Vector2(remote["position"].x, remote["position"].z)
		if target != Vector2.INF:
			_yaw = atan2(-(target.x - position.x), -(target.y - position.z))
		return {"yaw": _yaw}
	var goal := _route[_route_index]
	_yaw = atan2(-(goal.x - position.x), -(goal.y - position.z))
	return {"forward": 1.0, "yaw": _yaw}


func _camera_basis() -> Basis:
	return Basis(Vector3.UP, _camera_yaw) * Basis(Vector3.RIGHT, _pitch)


func _camera_pivot(target: Vector3) -> Vector3:
	return target + Vector3.UP * (CAMERA_HEIGHT * 0.8 - CROUCH_CAMERA_DROP * _crouch_view)


func _update_camera(target: Vector3, delta: float) -> void:
	var soldier := _soldier_of(_client.get_entity_id())
	var scoped := _update_zoom(delta)
	var from_eyes := (_first_person or scoped) and soldier != null and bool(_client.get_presented()["alive"])
	if soldier != null:
		PlayerAppearance.set_first_person(_own, from_eyes)
	if from_eyes:
		if _bot != "":
			var own: Dictionary = _client.get_presented()
			_yaw = float(own["yaw"])
			_pitch = float(own["pitch"])
		_camera_yaw = _yaw
		_crouch_view = 0.0
		var look := _camera_basis()
		_camera.global_position = soldier.eye() - look.z * FIRST_PERSON_FORWARD
		_camera.global_basis = look
		if scoped:
			soldier.model.visible = false
		return
	var crouched := bool(_client.get_presented().get("crouched", false))
	_crouch_view = move_toward(_crouch_view, 1.0 if crouched else 0.0, CROUCH_CAMERA_RATE * delta)
	_camera_yaw = _yaw if _bot == "" else lerp_angle(_camera_yaw, _yaw, minf(1.0, 4.0 * delta))
	var basis := _camera_basis()
	var pivot := _camera_pivot(target)
	var offset := Vector3(CAMERA_SHOULDER, 0.3, CAMERA_DISTANCE) * (_camera_distance / CAMERA_DISTANCE)
	var position := pivot + basis * offset
	position.y = maxf(position.y, _client.get_ground_height(position.x, position.z, pivot.y) + 0.4)
	_camera.global_position = position
	_camera.look_at(pivot + basis * Vector3(CAMERA_SHOULDER, 0, -10), Vector3.UP)


func _zoom_steps() -> Array[float]:
	var steps: Array[float] = []
	if _weapon >= _weapons.size() or (_weapons[_weapon] as Dictionary).is_empty():
		return steps
	var weapon: Dictionary = _weapons[_weapon]
	var low := float(weapon.get("zoom_min", 1.0))
	var high := float(weapon.get("zoom_max", low))
	if high <= 1.0:
		return steps
	steps.append(low)
	while steps[-1] * 2.0 <= high + 0.01:
		steps.append(steps[-1] * 2.0)
	if steps[-1] < high - 0.01:
		steps.append(high)
	return steps


func _update_zoom(delta: float) -> bool:
	var own: Dictionary = _client.get_presented()
	var steps := _zoom_steps()
	if _zoom_weapon != _weapon or not bool(own.get("alive", false)) or bool(own.get("sprinting", false)) \
			or float(own.get("roll", 0.0)) > 0.0 or _zoom_step > steps.size():
		_zoom_step = 0
	var target := steps[_zoom_step - 1] if _zoom_step > 0 else 1.0
	_zoom = lerpf(_zoom, target, minf(1.0, ZOOM_RATE * delta))
	if absf(_zoom - target) < 0.01:
		_zoom = target
	var base := deg_to_rad(_settings.field_of_view)
	_camera.fov = rad_to_deg(2.0 * atan(tan(base / 2.0) / _zoom))
	var weapon: Dictionary = _weapons[_weapon] if _weapon < _weapons.size() else {}
	var scoped := _zoom_step > 0 and bool(weapon.get("zoom_first_person", false))
	_battle_ui.set_scope_visible(scoped, str(weapon.get("name", "imp_")))
	return scoped


func _entity_team(id: int) -> int:
	if id == _client.get_entity_id():
		return int(_client.get_presented().get("team", -1))
	for remote: Dictionary in _client.get_remotes():
		if int(remote["id"]) == id:
			return int(remote.get("team", -1))
	return -1


func _update_own_audio(own: Dictionary, _game: Dictionary) -> void:
	if _audio == null:
		return
	var alive := bool(own.get("alive", false)) and str(own.get("life", "alive")) == "alive"
	var slot := int(own.get("weapon", 0))
	var weapon: Dictionary = _weapons[slot] if slot < _weapons.size() else {}
	var reloading := bool(own.get("reloading", false))
	var overheated := bool(own.get("overheated", false))
	var health := float(own.get("health", 0.0))
	var rounds := int(own.get("rounds", 0))
	var reserve := int(own.get("reserve", 0))
	var grenades := int(own.get("grenades", 0))
	if alive:
		if _prev_weapon >= 0 and slot != _prev_weapon:
			_audio.play_2d(str(weapon.get("weapon_change_sound", "")))
		if reloading and not _prev_reloading:
			_audio.play_2d(str(weapon.get("reload_sound", "")))
		if overheated and not _prev_overheated:
			_audio.play_2d(str(weapon.get("overheat_sound", "")))
		elif not overheated and _prev_overheated:
			_audio.play_2d(str(weapon.get("overheat_stop_sound", "")))
		if _mouse_captured and Input.is_action_pressed("fire") and _fire_lockout <= 0.0 and not _wait_fire_release \
				and int(weapon.get("rounds_per_clip", 0)) > 0 and rounds <= 0 and reserve == 0 and not reloading \
				and _empty_click_cd <= 0.0:
			_audio.play_2d(str(weapon.get("fire_empty_sound", "com_weap_inf_ammo_empty")))
			_empty_click_cd = 0.28
		var own_pos: Vector3 = own["position"]
		if _prev_health >= 0.0 and health > _prev_health + 1.0:
			for node: Node3D in get_tree().get_nodes_in_group(Bf2Map.POWERUPS):
				if node.visible and str((node.get_meta("powerup") as Dictionary).get("kind", "")) == "health" \
						and node.global_position.distance_squared_to(own_pos) <= 25.0:
					_audio.play_2d("health_recharge")
					break
		if (_prev_reserve >= 0 and reserve > _prev_reserve) or (_prev_grenades >= 0 and grenades > _prev_grenades):
			for node: Node3D in get_tree().get_nodes_in_group(Bf2Map.POWERUPS):
				if node.visible and str((node.get_meta("powerup") as Dictionary).get("kind", "")) == "ammo" \
						and node.global_position.distance_squared_to(own_pos) <= 25.0:
					_audio.play_2d("com_item_gonkrecharge")
					break
	_prev_reloading = reloading and alive
	_prev_overheated = overheated and alive
	_prev_weapon = slot if alive else -1
	_prev_health = health if alive else -1.0
	_prev_reserve = reserve if alive else -1
	_prev_grenades = grenades if alive else -1

	var cur_pickups := {}
	for p: Dictionary in _client.get_pickups():
		cur_pickups[int(p["id"])] = {"kind": str(p["kind"]), "position": p["position"]}
	if alive:
		var own_pos: Vector3 = own["position"]
		for pid: int in _prev_pickups:
			if not cur_pickups.has(pid):
				var old: Dictionary = _prev_pickups[pid]
				if (old["position"] as Vector3).distance_squared_to(own_pos) <= 6.25:
					_audio.play_2d("com_weap_ammo_pickup" if str(old["kind"]) == "ammo" else "com_weap_powerup_pickup")
	_prev_pickups = cur_pickups


func _update_events() -> void:
	var me: int = _client.get_entity_id()
	var own_team := int(_client.get_presented().get("team", -1)) if _client.is_spawned() else -1
	for event: Dictionary in _client.take_events():
		var mine := int(event["attacker"]) == me
		if event["kind"] == "hit":
			var vid := int(event["victim"])
			var victim := _soldier_of(vid)
			if victim != null:
				victim.flinch()
			if _audio != null and _elapsed - float(_hurt_vo_cd.get(vid, -10.0)) >= 1.0:
				var vside := _battle_ui.side_code(_entity_team(vid))
				if vside != "" and vside != "cis" and victim != null:
					_hurt_vo_cd[vid] = _elapsed
					_audio.play_3d(vside + "_inf_com_chatter_wound", victim.model.global_position,
							_camera.global_position)
		if event["kind"] == "hit" and mine:
			_hits += 1
			_battle_ui.note_hit()
		elif event["kind"] == "kill":
			var vid := int(event["victim"])
			_kills += 1 if mine and vid != me else 0
			_deaths += 1 if vid == me else 0
			if _audio != null:
				var victim := _soldier_of(vid)
				var vside := _battle_ui.side_code(_entity_team(vid))
				if victim != null and vside != "":
					var dsnd := "droid_death" if vside == "cis" else (vside + "_inf_com_chatter_death")
					_audio.play_3d(dsnd, victim.model.global_position, _camera.global_position)
			if int(event["attacker"]) == 0:
				_battle_ui.add_feed_line("%s died" % _who(event["victim"]))
			else:
				_battle_ui.add_feed_line("%s eliminated %s" % [_who(event["attacker"]), _who(event["victim"])])
		elif event["kind"] in ["captured", "neutralized"]:
			_battle_ui.handle_capture_event(event, own_team)
		_battle_ui.push_hud_message(event, _client)


func _soldier_of(id: int) -> Soldier:
	if id == _client.get_entity_id():
		return _own.get("soldier")
	if _remotes.has(id):
		return _remotes[id].get("soldier")
	return null


func _who(id: int) -> String:
	if id == _client.get_entity_id():
		var own_name := str(_client.get_presented().get("name", ""))
		return own_name if own_name != "" else "you"
	if _remotes.has(id):
		var rname := str(_remotes[id].get("name", ""))
		if rname != "":
			return rname
	for remote: Dictionary in _client.get_remotes():
		if int(remote["id"]) == id:
			var rname := str(remote.get("name", ""))
			if rname != "":
				return rname
	return "player %d" % id


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
		var dirs: Array = shot["directions"]
		if not dirs.is_empty():
			_effects.spawn_muzzle_flash(origin + offset, dirs[0], shot)
			if _audio != null:
				_audio.play_2d(str(shot.get("fire_sound", "")))
		for direction: Vector3 in dirs:
			var node := _effects.make_projectile(shot)
			var velocity: Vector3 = direction * float(weapon["bolt_speed"])
			var bolt := {"node": node, "position": origin, "velocity": velocity, "life": float(weapon["bolt_life"]),
					"origin": origin, "offset": offset}
			_place_drawn_bolt(node, bolt, delta)
			_own_bolts.append(bolt)
	for i in range(_own_bolts.size() - 1, -1, -1):
		var bolt: Dictionary = _own_bolts[i]
		bolt["life"] = float(bolt["life"]) - delta
		bolt["velocity"] = bolt["velocity"] + Vector3.DOWN * 9.8 * delta
		var node: Node3D = bolt["node"]
		var prev_pos: Vector3 = bolt["position"]
		var next_pos: Vector3 = prev_pos + (bolt["velocity"] as Vector3) * delta
		if float(bolt["life"]) <= 0 or _client.raycast(prev_pos, next_pos) >= 0.0:
			node.queue_free()
			_own_bolts.remove_at(i)
			continue
		bolt["position"] = next_pos
		_place_drawn_bolt(node, bolt, delta)

	for impact: Dictionary in _client.take_impacts():
		_effects.spawn_impact(impact["position"], impact["normal"], bool(impact["hit_character"]),
				str(impact.get("effect", "")))
		if _audio != null and not bool(impact["hit_character"]):
			_audio.play_3d(str(impact.get("sound", "imp_weap_ord_exp")), impact["position"], _camera.global_position)
	for explosion: Dictionary in _client.take_explosions():
		_effects.spawn_explosion(explosion["position"], explosion.get("normal", Vector3.UP),
				float(explosion["radius"]), str(explosion.get("effect", "")),
				explosion.get("light_color", Color(1.0, 0.7, 0.3)), float(explosion.get("light_radius", 7.0)))
		if _audio != null:
			_audio.play_3d(str(explosion.get("sound", "com_weap_thermaldetonator_exp")),
					explosion["position"], _camera.global_position)
		_explosions_seen += 1
	_effects.update(delta)

	var seen := {}
	for bolt: Dictionary in _client.get_bolts():
		var id := int(bolt["id"])
		seen[id] = true
		if not _remote_bolts.has(id):
			var start := _bolt_start(bolt)
			var node := _effects.make_projectile(bolt)
			var trail_name := str(bolt.get("trail_effect", ""))
			var trail := _effects.attach_trail(node, trail_name) if trail_name != "" else null
			_remote_bolts[id] = {"node": node, "trail": trail, "origin": start["origin"], "offset": start["offset"],
					"prev_vel": bolt["velocity"], "bounce_cd": 0.0}
		var entry: Dictionary = _remote_bolts[id]
		entry["bounce_cd"] = maxf(0.0, float(entry["bounce_cd"]) - delta)
		if _audio != null and str(bolt.get("kind", "")) == "grenade":
			var prev_v: Vector3 = entry["prev_vel"]
			var cur_v: Vector3 = bolt["velocity"]
			var bounced := (prev_v.y < -1.5 and cur_v.y > 0.5) \
					or (prev_v.length() > 3.0 and cur_v.length() > 0.5 and prev_v.normalized().dot(cur_v.normalized()) < 0.3)
			if bounced and float(entry["bounce_cd"]) <= 0.0:
				var bsnd := str(bolt.get("bounce_sound", ""))
				_audio.play_3d(bsnd if bsnd != "" else "com_weap_inf_grenade_bounce",
						bolt["position"], _camera.global_position)
				entry["bounce_cd"] = 0.12
		entry["prev_vel"] = bolt["velocity"]
		_place_drawn_bolt(entry["node"], {"position": bolt["position"], "velocity": bolt["velocity"],
				"origin": entry["origin"], "offset": entry["offset"]}, delta)
	for id: int in _remote_bolts.keys():
		if not seen.has(id):
			var entry: Dictionary = _remote_bolts[id]
			if entry["trail"] != null:
				_effects.detach_trail(entry["trail"])
			var node: Node3D = entry["node"]
			if is_instance_valid(node):
				node.queue_free()
			_remote_bolts.erase(id)


func _bolt_start(bolt: Dictionary) -> Dictionary:
	var owner_id := int(bolt["owner"])
	var soldier := _soldier_of(owner_id)
	if soldier == null or not soldier.model.visible:
		return {"origin": bolt["position"], "offset": Vector3.ZERO}
	_battle_ui.note_fired(owner_id)
	if owner_id != _client.get_entity_id():
		if bolt["kind"] == "grenade":
			soldier.throw()
		else:
			soldier.fire()
	if _audio != null:
		var fsnd := str(bolt.get("fire_sound", ""))
		if fsnd != "":
			if owner_id == _client.get_entity_id():
				_audio.play_2d(fsnd)
			else:
				_audio.play_3d(fsnd, soldier.muzzle(), _camera.global_position)
	var eye_h := float(_own.get("eye_height", EYE_HEIGHT)) if owner_id == _client.get_entity_id() else EYE_HEIGHT
	var origin := soldier.model.global_position + Vector3.UP * eye_h
	var muzzle := soldier.muzzle()
	if bolt["kind"] != "grenade":
		_effects.spawn_muzzle_flash(muzzle, bolt["velocity"], bolt)
	return {"origin": origin, "offset": muzzle - origin}


func _place_drawn_bolt(node: Node3D, bolt: Dictionary, delta: float) -> void:
	var position: Vector3 = bolt["position"]
	var travelled := position.distance_to(bolt["origin"])
	var shift: Vector3 = bolt["offset"] * maxf(0.0, 1.0 - travelled / MUZZLE_BLEND)
	_effects.place_projectile(node, position + shift, bolt["velocity"], delta)


func _add_building(path: String, galaxies_position: Vector3, yaw_degrees: float) -> bool:
	var building := SwgBuilding.load_lit(path)
	if building == null:
		printerr("cannot load ", path, " and its .json sidecar")
		return false
	building.position = Vector3(-galaxies_position.x, galaxies_position.y, galaxies_position.z)
	building.rotation.y = deg_to_rad(-yaw_degrees)
	_world.add_child(building)
	SwgBuilding.add_camera_collision(building)
	return true


func _add_terrain(bake: String, meta: Dictionary, use_terrain3d: bool) -> void:
	if meta.get("hidden", false):
		return
	if use_terrain3d and Terrain3DBake.available():
		var terrain: Node3D = Terrain3DBake.create(_world, bake, meta)
		terrain.collision_mode = 0
		return
	if use_terrain3d:
		printerr("Terrain3D isn't installed (tools/install_terrain3d.ps1); using a mesh")
	var heights := FileAccess.get_file_as_bytes(bake + ".r32").to_float32_array()
	var mesh := MeshInstance3D.new()
	mesh.mesh = TerrainTool.build_mesh(heights, meta["samples"], meta["spacing"],
			Vector2(meta["origin_x"], meta["origin_z"]), 1)
	if Bf2Map.is_bf2(meta):
		mesh.material_override = Bf2Map.build_terrain_material(bake, meta)
	else:
		mesh.material_override = TerrainTool.build_material(bake, meta)
	_world.add_child(mesh)


func _build_world(bake: String) -> bool:
	var meta: Variant = JSON.parse_string(FileAccess.get_file_as_string(bake + ".json"))
	if meta == null:
		printerr("cannot load terrain ", bake)
		return false
	if _world != null:
		remove_child(_world)
		_world.queue_free()
	_sky_dome = null
	_world = Node3D.new()
	_world.name = "World"
	add_child(_world)
	_world_bake = bake
	_map_meta = (meta as Dictionary).duplicate()
	_map_meta["dir"] = bake.get_base_dir()
	_map_meta["bake"] = bake
	_add_terrain(bake, meta as Dictionary, _terrain3d)
	if Bf2Map.is_bf2(meta as Dictionary):
		Bf2Map.add_objects(_world, bake)
	elif SwgCity.has_city(bake):
		SwgCity.add_objects(_world, bake, true)
	_doors = SwgCity.doors_in(_world)
	if bake == _start_bake:
		for building: Array in _buildings:
			if not _add_building(building[0], building[1], building[2]):
				return false

	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.62, 0.72, 0.85)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.5, 0.5, 0.55)
	environment.fog_enabled = true
	environment.fog_light_color = Color(0.75, 0.72, 0.68)
	environment.fog_density = 0.0004
	environment.glow_enabled = true
	var we := WorldEnvironment.new()
	we.environment = environment
	_world.add_child(we)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, -40, 0)
	sun.light_energy = 1.3
	sun.shadow_enabled = true
	_world.add_child(sun)
	if Bf2Map.is_bf2(meta as Dictionary):
		_sky_dome = Bf2Map.setup_environment(_world, bake, meta as Dictionary, environment, sun)
		Bf2Map.add_water(_world, bake, meta as Dictionary, sun)
		Bf2Map.add_fog_cloud(_world, bake, meta as Dictionary)
	return true


func _setup_scene() -> void:
	_camera = Camera3D.new()
	_camera.fov = 65
	_camera.near = 0.05
	_camera_sphere.radius = 0.15
	_camera.far = 8000
	add_child(_camera)

	_pickups = Pickups.new(self, HUD_DIR)
	_effects = Bf2Effects.new(self, EFFECTS_DIR)
	_audio = Bf2Audio.new(self, AUDIO_DIR)
	_audio.apply_volumes(_settings)

	var layer := CanvasLayer.new()
	add_child(layer)

	_battle_ui = BattleUI.new()
	add_child(_battle_ui)
	_battle_ui.setup(layer, self, _audio, HUD_DIR)
	_battle_ui.spawn_requested.connect(func(_l: int, _p: int) -> void: _request_spawn())

	if _battle_ui.has_bf2_hud():
		_pause_menu = PauseMenu.new(_battle_ui.art())
		_pause_menu.settings = _settings
		_pause_menu.visible = false
		_pause_menu.resumed.connect(_resume)
		_pause_menu.leave_requested.connect(func() -> void: _finish(0))
		layer.add_child(_pause_menu)

	_hub_ui = HubUI.new()
	add_child(_hub_ui)
	_hub_ui.setup(layer, self, _battle_ui.art(), _settings)
	_hub_ui.queue_requested.connect(func(side: int) -> void: _client.queue(side))
	_hub_ui.terminal_closed.connect(func() -> void:
		if _bot == "" and (_pause_menu == null or not _pause_menu.visible):
			_capture_mouse(true))


func _arrive() -> void:
	for character: Dictionary in _remotes.values():
		(character["model"] as Node).queue_free()
	_remotes.clear()
	for entry: Dictionary in _remote_bolts.values():
		var node: Node3D = entry["node"]
		if is_instance_valid(node):
			node.queue_free()
	_remote_bolts.clear()
	for bolt: Dictionary in _own_bolts:
		(bolt["node"] as Node).queue_free()
	_own_bolts.clear()
	if _pickups != null:
		_pickups.clear()
	if _effects != null:
		_effects.clear()
	if _audio != null:
		_audio.clear()
	_hurt_vo_cd.clear()
	_prev_pickups.clear()
	_prev_health = -1.0
	_prev_weapon = -1
	_battlefield = _client.get_battlefield()
	var bake := _game_path(_client.get_bake())
	if bake != _world_bake and not _build_world(bake):
		printerr("cannot draw ", bake)
	_hub_ui.on_world_arrived()
	_battle_ui.on_world_arrived(_battlefield)
	_battle_ui.set_world_context(_world_bake, _map_meta, _battlefield)
	_battle_ui.add_posts()
	_bind_powerups()
	_was_alive = false
	print("transferred to ", "the battle on " + str(_battlefield["name"]) if not _battlefield.is_empty() else "the hub")


func _lock_spawn_fire() -> void:
	_fire_lockout = SPAWN_FIRE_LOCKOUT
	_wait_fire_release = true


func _bind_powerups() -> void:
	for entry: Dictionary in _powerup_nodes:
		var wreck: Variant = entry.get("wreck")
		if wreck != null and is_instance_valid(wreck):
			(wreck as Node).queue_free()
	_powerup_nodes.clear()
	if _battlefield.is_empty():
		return
	var defs: Array = _battlefield.get("powerups", [])
	if defs.is_empty():
		return
	for node: Node3D in get_tree().get_nodes_in_group(Bf2Map.POWERUPS):
		var best := -1
		var best_d2 := 1.0
		for i in defs.size():
			var d2 := node.position.distance_squared_to(defs[i]["position"])
			if d2 < best_d2:
				best_d2 = d2
				best = i
		if best >= 0:
			var spec: Dictionary = node.get_meta("powerup")
			_powerup_nodes.append({"node": node, "index": best, "kind": str(spec.get("kind", "health")),
					"wreck": null, "spark_cd": 0.0})


func _update_powerups(game: Dictionary, delta: float) -> void:
	if _powerup_nodes.is_empty() or game.is_empty():
		return
	var states: Array = game.get("powerups", [])
	if states.is_empty():
		return
	for entry: Dictionary in _powerup_nodes:
		var idx: int = entry["index"]
		if idx < 0 or idx >= states.size():
			continue
		var st: Dictionary = states[idx]
		var alive: bool = bool(st.get("alive", true))
		var node: Node3D = entry["node"]
		if not is_instance_valid(node):
			continue
		node.visible = alive
		var wreck: Node3D = entry["wreck"]
		if alive:
			if wreck != null and is_instance_valid(wreck):
				wreck.visible = false
			var hp := float(st.get("health", 1.0))
			var max_hp := maxf(1.0, float(st.get("max_health", 1.0)))
			if hp < max_hp * 0.5 and _effects != null:
				entry["spark_cd"] = float(entry["spark_cd"]) - delta
				if float(entry["spark_cd"]) <= 0.0:
					entry["spark_cd"] = 1.1 + randf() * 0.8
					_effects.spawn_burst("com_sfx_ord_exp", node.global_position + Vector3.UP * 0.9, Vector3.UP)
		else:
			if (wreck == null or not is_instance_valid(wreck)) and _effects != null:
				var model_name := "com_item_healthrecharge_dst" if str(entry["kind"]) == "health" \
						else "com_item_weaponrecharge_dst"
				wreck = _effects.make_model(model_name)
				if wreck != null:
					wreck.position = node.position
					wreck.basis = node.get_meta("placed", node.basis)
					_world.add_child(wreck)
					entry["wreck"] = wreck
			if wreck != null and is_instance_valid(wreck):
				wreck.visible = true


func _request_spawn() -> void:
	if _battle_ui.spawn_post >= 0:
		_lock_spawn_fire()
		_client.request_spawn(_battle_ui.spawn_loadout, _battle_ui.spawn_post)
		_battle_ui.spawn_requested_flag = true


func _choose_spawn(game: Dictionary, own: Dictionary) -> void:
	var owned := _battle_ui.owned_posts(game, int(own["team"]))
	if not owned.has(_battle_ui.spawn_post):
		_battle_ui.spawn_post = owned[0] if not owned.is_empty() else -1
	if _bot != "" and _bot != "conquest" and not _battle_ui.spawn_requested_flag:
		_request_spawn()
	if _battle_ui.spawn_post >= 0:
		var target: Vector3 = _battlefield["posts"][_battle_ui.spawn_post]["position"]
		var view := target + Basis(Vector3.UP, _yaw) * Vector3(0, 28, 38)
		_camera.global_position = _camera.global_position.lerp(view, 0.1)
		_camera.look_at(target, Vector3.UP)


func _resolve_spawn(meta: Dictionary) -> Vector2:
	var sp: Variant = _config.get("spawn", null)
	if sp is Vector2:
		return sp
	var half: float = (int(meta["samples"]) - 1) * float(meta["spacing"]) / 2.0
	return Vector2(float(meta["origin_x"]) + half, float(meta["origin_z"]) + half)


func _os_path(path: String) -> String:
	return path if path.is_absolute_path() else ProjectSettings.globalize_path("res://" + path)


func _game_path(path: String) -> String:
	return ProjectSettings.localize_path(path).trim_prefix("res://")
