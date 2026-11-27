# Playable game session scene: runs a hub or conquest match client (and
# optional local server), using PlayerAppearance for all 10 species, HubUI,
# BattleUI, and PauseMenu. Launched either from MainMenu or net_demo.gd.
class_name GameSession
extends Node3D

const TerrainTool = preload("res://tools/render_terrain.gd")
const Terrain3DBake = preload("res://tools/terrain3d_bake.gd")
const SwgTerrainStreamer = preload("res://rendering/swg_terrain_streamer.gd")
const TATOOINE_SKY = preload("res://rendering/tatooine_sky.gdshader")
const SwgBuilding = preload("res://rendering/swg_building.gd")
const Bf2Map = preload("res://rendering/bf2_map.gd")
const Weather = preload("res://rendering/weather.gd")  # Stage 11: rain, sand, snow, ash, mist
const SwgCity = preload("res://rendering/swg_city.gd")
const SwgWater = preload("res://rendering/swg_water.gd")
const NATIVES_TEAM := 2  # a battlefield's natives (core::natives_team)
const Soldier = preload("res://rendering/soldier.gd")
const PlayerAppearance = preload("res://rendering/player_appearance.gd")
const Pickups = preload("res://rendering/pickups.gd")
const Vehicles = preload("res://rendering/vehicles.gd")
# Riding a hover vehicle (a speeder bike), where the rider's feet go in its
# model's space: crouched on it (ASSUMPTION: by eye; Battlefront poses its
# riders with their own clips, not read yet).
const RIDER_SEAT := Vector3(0, -0.5, -0.25)
const Bf2Effects = preload("res://rendering/bf2_effects.gd")
const Bf2Audio = preload("res://audio/bf2_audio.gd")
const Settings = preload("res://ui/settings.gd")
const PauseMenu = preload("res://ui/pause_menu.gd")
const HubUI = preload("res://scenes/hub_ui.gd")
const BattleUI = preload("res://scenes/battle_ui.gd")
const SpecialistUIScript = preload("res://scenes/specialist_ui.gd")  # Stage 12
const LoadingScreen = preload("res://ui/loading_screen.gd")
const Bf2Art = preload("res://ui/bf2_art.gd")

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
const MENU_FIRE_LOCKOUT := 0.4  # s no shot is fired after a menu or conversation closes

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
var _weather := Weather.new()
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
var _vehicles := Vehicles.new()
var _own_bolts: Array = []
var _remote_bolts := {}
var _kills := 0
var _deaths := 0
var _hits := 0
var _pending_shot := ""
var _settings: Settings
var _pause_menu: PauseMenu
var _hub_ui: HubUI
var _specialist_ui: SpecialistUIScript  # Stage 12: ability bar, datapad, bazaar, field effects
var _battle_ui: BattleUI
var _catalog_store: Object
var _catalog: Array = []
var _view_setting := false
var _show_stats := false
var _map_meta := {}
var _world: Node3D
var _terrain_streamer: SwgTerrainStreamer
var _world_bake := ""
var _start_bake := ""
var _buildings: Array = []
var _terrain3d := false
var _doors: Array[Node3D] = []
var _buildings_with_rooms: Array[Node3D] = []  # (their outsides hidden while the camera's in a room)
var _people := {}
var _people_colors := {}
var _shot_delay := 0
var _loading_screen: LoadingScreen
var _first_load_wait := 0.0          # s waited for the first spawn under the loading screen
const FIRST_LOAD_HOLD := 2.0         # s the loading screen stays up once spawned (the world streaming in)
const FIRST_LOAD_LIMIT := 45.0       # s it waits for a spawn at most
var _loading_phase := 0  # 0 idle, 1 waiting frame to load transfer world, 2 hiding after first rendered frame, 3 the first load (until spawned)
var _travel_hold := 0.0  # Stage 13: seconds the loading screen stays up after a long jump (a shuttle)
var _last_own_position := Vector3.INF


func _ready() -> void:
	if not pending_config.is_empty():
		var cfg := pending_config.duplicate(true)
		pending_config.clear()
		call_deferred("_start_interactive", cfg)


func _start_interactive(cfg: Dictionary) -> void:
	if not DisplayServer.get_name().to_lower().contains("headless"):
		var art: RefCounted = Bf2Art.load_from(HUD_DIR)
		Bf2Art.apply_cursor(art)
		var load_layer := CanvasLayer.new()
		load_layer.layer = 50
		add_child(load_layer)
		_loading_screen = LoadingScreen.new(art)
		var info := LoadingScreen.pending
		LoadingScreen.pending = {}
		_loading_screen.show_loading(
				str(info.get("title", "MOS EISLEY, TATOOINE")),
				str(info.get("subtitle", "Loading world...")),
				str(info.get("icon", "hud_flag_icon")))
		load_layer.add_child(_loading_screen)
		await get_tree().process_frame
		await get_tree().process_frame
	start_session(cfg)
	if _loading_screen != null:
		# Up until we're in the world and it's streamed in round us (as a
		# shuttle's arrival is held): hidden at once, the first frames showed
		# its terrain and textures still loading.
		_loading_phase = 3
		_first_load_wait = 0.0


func start_session(config: Dictionary) -> bool:
	_config = config.duplicate(true)
	if not ClassDB.class_exists("SwmClient"):
		printerr("SwmClient GDExtension not loaded")
		_emit_or_menu(2)
		return false
	var default_bake := "imported_assets/terrain/tatooine" if FileAccess.file_exists("res://imported_assets/terrain/tatooine.json") else "imported_assets/terrain/tatooine_mos_eisley"
	var bake := str(_config.get("bake", default_bake))
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
	if bool(_config.get("third_person", false)):  # (tests: whatever the settings say)
		_first_person = false
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
		var hub_path := str(_config.get("hub", ""))
		if hub_path != "" and _server.set_hub(_os_path(hub_path)) != OK:
			_emit_or_menu(1)
			return false
		# Stage 13: the other planets, each its own hub, with starships between them.
		var galaxy_path := str(_config.get("galaxy", "hubs/galaxy.ini" if hub_path != "" else ""))
		if galaxy_path != "" and FileAccess.file_exists(_os_path(galaxy_path)):
			_server.set_galaxy(_os_path(galaxy_path))
		var catalog_path := _os_path("items/swg_catalog.ini")
		if FileAccess.file_exists(catalog_path):
			_server.set_catalog(catalog_path, _os_path("imported_assets/items"))
		if DirAccess.dir_exists_absolute(_os_path("vehicles")):
			_server.set_world_vehicles(_os_path("vehicles"))
		if FileAccess.file_exists(_os_path("professions/professions.ini")):
			_server.set_professions(_os_path("professions/professions.ini"))
		if FileAccess.file_exists(_os_path("crafting/resources.ini")) \
				and FileAccess.file_exists(_os_path("crafting/schematics.ini")):
			_server.set_crafting(_os_path("crafting/resources.ini"), _os_path("crafting/schematics.ini"))
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
	# Outside a match, the world's vehicles (Galaxies' personal ones), as the server has them.
	if DirAccess.dir_exists_absolute(_os_path("vehicles")):
		_client.set_world_vehicles(_os_path("vehicles"))
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
	_vehicles.setup(_world, _vehicle_classes(), _effects, _audio)
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
	if _loading_phase == 1:
		_loading_phase = 2
		_arrive()
		return
	elif _loading_phase == 2:
		_loading_phase = 0
		if _loading_screen != null:
			_loading_screen.visible = false
	elif _loading_phase == 3:
		# The first load: once spawned, a moment more for the world round us.
		_first_load_wait += delta
		if (_client != null and _client.is_spawned()) or _first_load_wait > FIRST_LOAD_LIMIT:
			_loading_phase = 0
			_travel_hold = FIRST_LOAD_HOLD
	if _travel_hold > 0.0:
		_travel_hold -= delta
		if _travel_hold <= 0.0 and _loading_screen != null and _loading_phase == 0:
			_loading_screen.visible = false
	if _server != null:
		_server.update()
	if _bot != "conquest":
		_client.set_input(_controls())
	_client.update()
	if _client.get_generation() != _generation:
		_generation = _client.get_generation()
		_last_own_position = Vector3.INF  # another world: not a jump in this one
		if _loading_screen != null and _bot == "" and not DisplayServer.get_name().to_lower().contains("headless"):
			var bf: Dictionary = _client.get_battlefield()
			if not bf.is_empty():
				_loading_screen.show_loading(str(bf.get("name", "BATTLEFIELD")), "Deploying to battle...", "hud_flag_icon")
			else:
				# Back from a battle, or off a starship (Stage 13): the planet's name.
				var planet := _game_path(_client.get_bake()).get_file().get_slice("_", 0).capitalize()
				_loading_screen.show_loading(planet.to_upper(), "Arriving on %s..." % planet, "hud_flag_icon")
			_loading_phase = 1
			return
		_arrive()
	if _client.get_session_error() != "":
		printerr(_client.get_session_error())
		_finish(1)
		return

	if _client.is_spawned():
		var own: Dictionary = _client.get_presented()
		# A shuttle (or a clone in another town) moves us kilometers at once:
		# the loading screen covers the world streaming in there.
		var own_position: Vector3 = own["position"]
		if _last_own_position != Vector3.INF and own_position.distance_to(_last_own_position) > 300.0 \
				and _loading_screen != null and _bot == "" and _client.get_match().is_empty() \
				and not DisplayServer.get_name().to_lower().contains("headless"):
			_loading_screen.show_loading(_map_title().to_upper(), "Arriving...", "hud_flag_icon")
			_travel_hold = 1.5
		_last_own_position = own_position
		_ensure_character_appearance(_own, own, true)
		PlayerAppearance.set_worn_models(_own, str(own.get("worn", "")))
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
		_pose(_own, own, _client.get_entity_id())
		_update_remotes(int(own["team"]))
		var shown := {_client.get_entity_id(): {"position": own["position"], "yaw": float(own["yaw"]),
				"pitch": float(own.get("pitch", 0.0)), "roll": float(own.get("roll", 0.0)),
				"aim_yaw": float(own.get("roll_yaw", 0.0))}}
		for remote: Dictionary in _client.get_remotes():
			shown[int(remote["id"])] = {"position": remote["position"], "yaw": float(remote["yaw"]),
					"pitch": float(remote.get("pitch", 0.0)), "roll": float(remote.get("roll", 0.0)),
					"aim_yaw": float(remote.get("roll_yaw", 0.0))}
		_vehicles.update(_client.get_vehicles(), shown, delta)
		var game: Dictionary = _client.get_match()
		_battle_ui.update_posts(game, int(own["team"]), _elapsed)
		_update_powerups(game, delta)
		for door_snd: Dictionary in Bf2Map.animate_doors(get_tree(), game.get("doors", PackedFloat32Array())):
			if _audio != null:
				_audio.play_3d(str(door_snd["sound"]), door_snd["position"], _camera.global_position)
		Bf2Map.animate_world(get_tree(), delta, game.get("destructibles", PackedFloat32Array()), float(game.get("clock", -1.0)))
		Bf2Map.update_damage_effects(get_tree(), game.get("destructibles", PackedFloat32Array()), _effects)
		Bf2Map.update_chunks(delta, _client.raycast)
		for blast: Dictionary in Bf2Map.update_destructibles(get_tree(), game.get("destructibles", PackedFloat32Array())):
			Bf2Map.spawn_chunks(self, blast)
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
		if not _buildings_with_rooms.is_empty():
			SwgBuilding.update_exteriors(_buildings_with_rooms, _camera.global_position)

	_battle_ui.hub_markers = _hub_ui.radar_markers(_client) if _hub_ui != null else []
	_battle_ui.hub_ui_ref = _hub_ui
	_battle_ui.driving = _driven_vehicle()
	_battle_ui.update_hud(_client, _weapons, _remotes.size(), _client.get_max_health(),
			_show_stats, delta, _pause_menu != null and _pause_menu.visible,
			_hub_ui != null and _hub_ui.is_terminal_open(), _hub_ui, _generation, _bot,
			_camera, _yaw if _bot == "" else float(_client.get_presented().get("yaw", _yaw)),
			_capture_mouse, _who)
	_show_vehicle_prompt()
	if _hub_ui != null:
		_hub_ui.update_hub(_client, _world, delta, _pause_menu != null and _pause_menu.visible,
				_battle_ui.center_label(), _battle_ui.match_label())
		for line: String in _hub_ui.take_notice_lines():
			_battle_ui.add_feed_line(line)
	if _specialist_ui != null:
		_specialist_ui.covered = (_hub_ui != null and _hub_ui.is_any_screen_open()) \
				or (_pause_menu != null and _pause_menu.visible)
		_specialist_ui.update(_client, _camera, _catalog, delta)

	if _audio != null:
		_audio.update(delta, _camera.global_position)
	if _sky_dome != null:
		_sky_dome.global_position = _camera.global_position
	_weather.update(_camera.global_position, delta)
	if _terrain_streamer != null:
		var focus := _camera.global_position
		if _client != null and _client.is_spawned():
			focus = _client.get_presented()["position"]
		_terrain_streamer.update_focus(focus)

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


# The map's title: the planet (its terrain bake's name) and the town (the
# hub file's [hub] Name).
func _map_title() -> String:
	var planet := str(_config.get("bake", "")).get_file().get_slice("_", 0).capitalize()
	if _world_bake != "" and _battlefield.is_empty():
		planet = _world_bake.get_file().get_slice("_", 0).capitalize()  # the planet we're on (Stage 13)
	var town := ""
	var hub_path := str(_config.get("hub", ""))
	if hub_path != "":
		var file := FileAccess.open("res://" + hub_path if not hub_path.begins_with("res://") else hub_path, FileAccess.READ)
		var in_hub := false
		while file != null and not file.eof_reached():
			var line := file.get_line().strip_edges()
			if line.begins_with("["):
				if in_hub:
					break
				in_hub = line == "[hub]"
			elif in_hub and line.begins_with("Name"):
				town = line.get_slice("=", 1).strip_edges()
				break
	# Stage 13: the planet has several towns; the one whose travel point
	# (shuttleport, starport) is nearest, within a couple of kilometers.
	if _client != null and _client.is_spawned():
		var at: Vector3 = _client.get_presented()["position"]
		var best := 2500.0
		for v: Dictionary in _client.get_vendors():
			if str(v.get("kind", "")) != "travel" or str(v.get("detail", "")) == "":
				continue
			var p: Vector3 = v["position"]
			var d := Vector2(p.x - at.x, p.z - at.z).length()
			if d < best:
				best = d
				town = str(v["detail"])
	if planet == "":
		return town if town != "" else "Map"
	return "%s, %s" % [town, planet] if town != "" else planet

func _input(event: InputEvent) -> void:
	if not _started or _exiting or _bot != "":
		return
	# Typing into a text box (a loadout's name, a search): its keys are its own.
	var typing_in := get_viewport().gui_get_focus_owner()
	if event is InputEventKey and typing_in is LineEdit and typing_in.is_visible_in_tree():
		return
	var escape: bool = event is InputEventKey and event.pressed and not event.echo \
			and (event as InputEventKey).physical_keycode == KEY_ESCAPE
	if _pause_menu != null and _pause_menu.visible:
		if escape and not _pause_menu.is_rebinding():
			_pause_menu.visible = false
			_resume()
		return
	if _hub_ui != null and _hub_ui.is_terminal_open():
		if _hub_ui.is_loot_open() and event.is_action_pressed("interact") and not event.is_echo():
			_hub_ui.loot_all_and_harvest(_client)
		elif escape or event.is_action_pressed("interact") or event.is_action_pressed("quest_log") \
				or event.is_action_pressed("professions") or event.is_action_pressed("workbench") \
				or event.is_action_pressed("world_map"):
			_hub_ui.close_terminal()
		elif event.is_action_pressed("leave_queue"):
			_client.queue(-1)
		elif event is InputEventKey and event.pressed and not event.echo \
				and (event as InputEventKey).physical_keycode in [KEY_1, KEY_2, KEY_3, KEY_4]:
			var kc := (event as InputEventKey).physical_keycode
			_hub_ui.terminal_pick(kc - KEY_1)  # a side of the era the terminal shows
		return
	if _specialist_ui != null and _specialist_ui.handle_input(event, _client, _camera, _battle_ui.spawn_loadout):
		return  # Stage 12: its keys, and everything while its screens are open
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
		var driving_cls := _driven_vehicle()
		var flying := str(driving_cls.get("kind", "")) == "flyer"
		var lowest := -1.38 if (_first_person or flying) else -1.2
		var highest := 1.38 if (_first_person or flying) else 0.6
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
	elif event.is_action_pressed("inventory") and not event.is_echo() and _hub_ui != null \
			and _client.get_match().is_empty() and not _client.get_recruiters().is_empty():
		_capture_mouse(not _hub_ui.toggle_inventory(_client))
	elif event.is_action_pressed("quest_log") and not event.is_echo() and _hub_ui != null \
			and _client.get_match().is_empty() and not _client.get_recruiters().is_empty():
		_capture_mouse(not _hub_ui.toggle_quest_log(_client))
	elif event.is_action_pressed("world_map") and not event.is_echo() and _hub_ui != null \
			and _client.get_match().is_empty() and not _client.get_recruiters().is_empty():
		_hub_ui.map_title = _map_title()
		_hub_ui.map_planet = _battle_ui.minimap()
		_capture_mouse(not _hub_ui.toggle_map(_client))
	elif event.is_action_pressed("professions") and not event.is_echo() and _hub_ui != null \
			and _client.get_match().is_empty() and not _client.get_recruiters().is_empty():
		_capture_mouse(not _hub_ui.toggle_professions(_client))
	elif event.is_action_pressed("workbench") and not event.is_echo() and _hub_ui != null \
			and _client.get_match().is_empty() and not _client.get_recruiters().is_empty():
		_capture_mouse(not _hub_ui.toggle_workbench(_client))
	elif event.is_action_pressed("scanner") and not event.is_echo() and _hub_ui != null \
			and _client.get_match().is_empty() and not _client.get_recruiters().is_empty():
		_hub_ui.toggle_scanner()
	elif _hub_ui != null and _hub_ui.is_scanner_active() and event is InputEventMouseButton and event.pressed \
			and (event as InputEventMouseButton).button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		_hub_ui.cycle_scanner(-1 if (event as InputEventMouseButton).button_index == MOUSE_BUTTON_WHEEL_UP else 1)
	elif event.is_action_pressed("loadout_1") and not event.is_echo() and _client.get_match().is_empty():
		_client.switch_loadout(0)
	elif event.is_action_pressed("loadout_2") and not event.is_echo() and _client.get_match().is_empty():
		_client.switch_loadout(1)
	elif event.is_action_pressed("loadout_3") and not event.is_echo() and _client.get_match().is_empty():
		_client.switch_loadout(2)
	elif event.is_action_pressed("loadout_4") and not event.is_echo() and _client.get_match().is_empty():
		_client.switch_loadout(3)
	elif event.is_action_pressed("loadout_5") and not event.is_echo() and _client.get_match().is_empty():
		_client.switch_loadout(4)
	elif event.is_action_pressed("summon_mount") and not event.is_echo() and _client.get_match().is_empty():
		_toggle_equipped_mount()
	elif event.is_action_pressed("interact") and not event.is_echo() and _hub_ui != null:
		if _hub_ui.try_open_terminal(_client):
			_capture_mouse(false)
		elif _hub_ui.is_scanner_active():
			_hub_ui.extract_scanner(_client)
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


func _toggle_equipped_mount() -> void:
	if _client == null or not _client.is_spawned() or _hub_ui == null:
		return
	var loadouts: Array = _client.get_character_loadouts()
	var active_idx := int(_client.get_player_status().get("active_loadout", 0))
	var mount_id := 0
	for lo: Dictionary in loadouts:
		if int(lo.get("slot_index", -1)) == active_idx:
			mount_id = int((lo.get("gear_ids", {}) as Dictionary).get("mount", 0))
			break
	if mount_id <= 0:
		_hub_ui.show_local_notice("No speeder or mount equipped in the Mount slot.")
		return
	_client.use_item(mount_id)


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
	var can_fire := _fire_lockout <= 0.0 and not _wait_fire_release and not _battle_ui.aiming_at_peaceful()
	var is_melee := _weapon < _weapons.size() and str((_weapons[_weapon] as Dictionary).get("projectile", "")) == "melee"
	var blocking := is_melee and (Input.is_action_pressed("block") or Input.is_action_pressed("grenade"))
	return {"forward": forward, "right": right, "yaw": aim.x, "pitch": aim.y,
			"sprint": Input.is_action_pressed("sprint"), "jump": Input.is_action_pressed("jump"),
			"fire": Input.is_action_pressed("fire") and can_fire, "reload": Input.is_action_pressed("reload"),
			"secondary": false if blocking else Input.is_action_pressed("grenade"),
			"crouch": Input.is_action_pressed("crouch"),
			"use": Input.is_action_pressed("interact"),  # in or out of a vehicle (in the world, one's own)
			"block": blocking,
			"aim": _zoom_step > 0,  # aimed down the sights (a perk's condition: Spotter)
			"weapon": _weapon}


# The vehicle classes in play: the battlefield's in a match, else the world's.
func _vehicle_classes() -> Array:
	var classes: Array = _battlefield.get("vehicles", [])
	return classes if not classes.is_empty() or _client == null else _client.get_vehicle_classes()


# The vehicle we drive: {label, health, max_health, ...} or {}.
func _driven_vehicle() -> Dictionary:
	if _client == null or not _client.is_spawned():
		return {}
	var own: Dictionary = _client.get_presented()
	var index := int(own.get("vehicle", -1))
	if index < 0:
		return {}
	var out := _vehicles.class_of(index).duplicate()
	out["health"] = out.get("max_health", 1.0)
	var me: int = _client.get_entity_id()
	for v: Dictionary in _client.get_vehicles():
		if int(v["driver"]) == me or (v.get("gunners", []) as Array).has(me):
			out["health"] = v["health"]
			out["max_health"] = v["max_health"]
	return out


# On foot by an empty vehicle: how to get in.
func _show_vehicle_prompt() -> void:
	if _client == null or not _client.is_spawned():
		return
	var own: Dictionary = _client.get_presented()
	var label := _battle_ui.center_label()
	if label == null or label.text != "" or not bool(own["alive"]):
		return
	if int(own.get("vehicle", -1)) >= 0:
		# Piloting a starfighter: how to take off (landed) and land (low).
		var cls := _vehicles.class_of(int(own["vehicle"]))
		if str(cls.get("kind", "")) == "flyer" and int(own.get("seat", 0)) == 0:
			var jump := Settings.binding_name(_settings.bindings["jump"]) if _settings.bindings.has("jump") else "Space"
			var crouch := Settings.binding_name(_settings.bindings["crouch"]) if _settings.bindings.has("crouch") else "C"
			if bool(own.get("grounded", false)):
				label.text = "Press %s to take off" % jump
			elif _client.get_ground_height(own["position"].x, own["position"].z, own["position"].y) \
					> float(own["position"].y) - 14.0:
				label.text = "Hold %s to land" % crouch
		return
	var found := _vehicles.enterable(_client.get_vehicles(), own["position"], int(own["team"]),
			_client.get_entity_id(), bool(_battlefield.get("is_space", false)))
	if not found.is_empty():
		var key := Settings.binding_name(_settings.bindings["interact"]) if _settings.bindings.has("interact") else "E"
		var label_text := str(_vehicles.class_of(int(found["vehicle"]["vehicle"])).get("label", "vehicle"))
		if int(found["vehicle"].get("owner", 0)) != 0:
			label.text = "Press %s to ride your %s" % [key, label_text]
		else:
			label.text = ("Press %s to get in the %s" if int(found["seat"]) == 0 else "Press %s to man the %s's gun") \
					% [key, label_text]
		return
	var chairs: Array = _battlefield.get("turret_chairs", [])
	var own_pos: Vector3 = own["position"]
	var own_team := int(own["team"])
	for chair: Dictionary in chairs:
		var cteam := int(chair.get("team", -1))
		if cteam >= 0 and not _client.is_allied(own_team, cteam):
			continue
		if own_pos.distance_squared_to(chair["position"] as Vector3) <= 2.8 * 2.8:
			var key := Settings.binding_name(_settings.bindings["interact"]) if _settings.bindings.has("interact") else "E"
			label.text = "Press %s to control defense turret" % key
			return


func _aim() -> Vector2:
	if not _client.is_spawned():
		return Vector2(_yaw, 0)
	var own: Dictionary = _client.get_presented()
	var eye_h := float(_own.get("crouched_eye_height", CROUCHED_EYE_HEIGHT)) if own.get("crouched", false) \
			else float(_own.get("eye_height", EYE_HEIGHT))
	var eye: Vector3 = own["position"] + Vector3.UP * eye_h
	var driving := int(own.get("vehicle", -1))
	var seat := int(own.get("seat", 0))
	if driving >= 0 and seat > 0:
		# A gunner's guns fire from its seat's eye, on the vehicle as it's drawn.
		var at := _vehicles.riding(_client.get_entity_id())
		var seats: Array = _vehicles.class_of(driving).get("seats", [])
		if not at.is_empty() and seat <= seats.size():
			eye = Vehicles.to_world(at["position"], float(at["yaw"]), (seats[seat - 1] as Dictionary)["eye"])
	elif driving >= 0:
		# Its guns fire from its eye point.
		eye = Vehicles.to_world(own["position"], float(own["yaw"]),
				_vehicles.class_of(driving).get("eye", Vector3.UP))
	var target := _camera.global_position - _camera.global_basis.z * AIM_DISTANCE
	var to := target - eye
	return Vector2(atan2(-to.x, -to.z), atan2(to.y, Vector2(to.x, to.z).length()))


func _resume() -> void:
	if _settings.first_person != _view_setting:
		_first_person = _settings.first_person
	if not (_battle_ui.has_bf2_hud() and _battle_ui.is_choosing_spawn(_client)):
		_capture_mouse(true)


func _capture_mouse(captured: bool) -> void:
	if captured and not _mouse_captured:
		# Back from a menu, a conversation or the pause: the click that closed it
		# isn't a shot.
		_fire_lockout = MENU_FIRE_LOCKOUT
		_wait_fire_release = true
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


func _spawn_character_model(species_id: String, gender: String, appearance_json: String, worn_csv := "") -> Dictionary:
	var ov := PlayerAppearance.override_path_from_worn(worn_csv)
	var character := PlayerAppearance.load_character(species_id, gender, appearance_json, _character_path, ov)
	if not character.is_empty():
		character["worn_override"] = ov
		add_child(character["model"])
	return character


func _ensure_character_appearance(character: Dictionary, state: Dictionary, is_own: bool) -> void:
	var sp := str(state.get("species", "human"))
	var gen := str(state.get("gender", "male"))
	var app := str(state.get("appearance", ""))
	var ov := PlayerAppearance.override_path_from_worn(str(state.get("worn", "")))
	if sp == str(character.get("species", "")) and gen == str(character.get("gender", "")) \
			and str(character.get("raw_appearance", "")) == app \
			and str(character.get("worn_override", "")) == ov:
		return
	var replacement := PlayerAppearance.load_character(sp, gen, app, _character_path, ov)
	if replacement.is_empty():
		return
	var old_model: Variant = character.get("model")
	if old_model is Node and is_instance_valid(old_model):
		(old_model as Node).queue_free()
	for k: String in replacement:
		character[k] = replacement[k]
	character["raw_appearance"] = app
	character["worn_override"] = ov
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
					str(remote.get("appearance", "")),
					str(remote.get("worn", "")))
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
		if int(remote["team"]) != NATIVES_TEAM:
			PlayerAppearance.set_worn_models(_remotes[id], str(remote.get("worn", "")))
		_pose(_remotes[id], remote, id)
		_remotes[id]["quiet"] = bool(remote.get("quiet", false))  # (Low Profile: firing doesn't show on radar)
		var tag: Label3D = _remotes[id]["tag"]
		var friend: bool = own_team >= 0 and _client.is_allied(own_team, int(remote["team"]))
		# Natives' holograms over their own heads (soldiers': the usual height).
		var holo_height := float(_remotes[id]["eye_height"]) + 0.65 if int(remote["team"]) == NATIVES_TEAM else 2.25
		var shown_holo := _battle_ui.show_team_tag(id, _remotes[id]["model"], int(remote["team"]), own_team,
				bool(remote["alive"]), friend, holo_height)
		if not shown_holo and _hub_ui != null and _client.get_match().is_empty():
			var factions: PackedStringArray = _client.get_factions()
			var team := int(remote["team"])
			var faction := factions[team].to_lower() if team >= 0 and team < factions.size() else ""
			var side := "alliance" if faction.contains("rebel") else ("empire" if faction.contains("imp") or faction.contains("empire") else "neutral")
			shown_holo = _hub_ui.show_combat_hologram(id, _remotes[id]["model"], side, friend or own_team < 0, bool(remote["alive"]))
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
			if _hub_ui != null:
				_hub_ui.forget_hologram(id)


func _update_person(id: int, state: Dictionary) -> void:
	var actor_kind := int(state.get("actor_kind", 1))
	if not _remotes.has(id):
		var look := int(state["look"])
		if not _people.has(look):
			_people[look] = _load_person(look)
		if _people[look] == null:
			return
		var model := (_people[look] as Node3D).duplicate() as Node3D
		SwgColors.apply(model, _people_colors.get(look, {}), int(state.get("colors", 0)))
		add_child(model)
		var player := model.find_child("AnimationPlayer", true, false) as AnimationPlayer
		# Their names show when aimed at (the HUD's target name); a bandit
		# has a red hologram over its head, as an enemy soldier in a match.
		var tag: Label3D = null
		var top_y := 1.8
		if actor_kind == 3:
			for mesh: Node in model.find_children("*", "MeshInstance3D", true, false):
				var box := (mesh as MeshInstance3D).get_aabb()
				top_y = maxf(top_y, box.position.y + box.size.y)
		if actor_kind == 3 and player != null and player.has_animation("human_rifle_stand_idle_emote_full"):
			var soldier := Soldier.new(model)
			if soldier._tree != null:
				soldier._tree.advance(fposmod(float(id) * 0.618034, 1.0) * 2.5)
			_remotes[id] = {"model": model, "person": true, "actor_kind": actor_kind,
					"soldier": soldier, "tag": tag, "holo_height": top_y + 0.45}
		else:
			_remotes[id] = {"model": model, "person": true, "actor_kind": actor_kind,
					"player": player, "tag": tag, "holo_height": top_y + 0.45}
	var person: Dictionary = _remotes[id]
	if actor_kind == 3 and _hub_ui != null:
		var alive_now := bool(state.get("alive", true)) and str(state.get("life", "alive")) == "alive"
		_hub_ui.show_combat_hologram(id, person["model"], "neutral", false, alive_now, float(person.get("holo_height", 2.25)))
	var tag: Label3D = person.get("tag")
	if tag != null:
		var alive := bool(state.get("alive", true)) and str(state.get("life", "alive")) == "alive"
		var mob_name := str(state.get("name", ""))
		if alive:
			tag.text = "%s  %d/%d" % [mob_name, int(ceilf(float(state.get("health", 0.0)))),
					int(ceilf(float(state.get("max_health", 100.0))))]
			tag.modulate = Color(1.0, 0.35, 0.3) if actor_kind == 3 else Color(1.0, 0.72, 0.3)
		else:
			tag.text = "%s [Dead]" % mob_name
			tag.modulate = Color(0.55, 0.55, 0.55)
	if person.has("soldier"):
		var soldier := person["soldier"] as Soldier
		soldier.set_weapon(str(state.get("weapon_model", "")))
		soldier.set_anim_set(str(state.get("anim_set", "rifle")))
		soldier.pose(state, _delta)
		return
	var model: Node3D = person["model"]
	model.position = state["position"]
	model.rotation.y = float(state["yaw"]) + PI
	var player := person["player"] as AnimationPlayer
	if player == null:
		return
	var velocity: Vector3 = state["velocity"]
	var speed := Vector2(velocity.x, velocity.z).length()
	if actor_kind == 2:
		var alive := bool(state.get("alive", true)) and str(state.get("life", "alive")) == "alive"
		var clip := "idle"
		var loop := Animation.LOOP_LINEAR
		var spd := 1.0
		if not alive and player.has_animation("die"):
			clip = "die"
			loop = Animation.LOOP_NONE
		elif int(state.get("pose", 0)) == 2 and player.has_animation("attack"):
			clip = "attack"
			loop = Animation.LOOP_NONE
		elif speed > 3.2 and player.has_animation("run"):
			clip = "run"
			spd = clampf(speed / 4.0, 0.8, 1.8)
		elif speed > 0.3 and player.has_animation("walk"):
			clip = "walk"
			spd = clampf(speed / 1.8, 0.6, 1.6)
		elif int(state.get("pose", 0)) == 1 and player.has_animation("sit"):
			clip = "sit"
		if player.current_animation != clip and player.has_animation(clip):
			var anim := player.get_animation(clip)
			anim.loop_mode = loop
			player.play(clip, 0.2)
			if clip in ["idle", "sit"]:
				player.seek(anim.length * fposmod(float(id) * 0.618034, 1.0), true)
		player.speed_scale = spd
		return
	var clip := "sit" if int(state.get("pose", 0)) == 1 else ("walk" if speed > 0.3 else "idle")
	if player.current_animation != clip and player.has_animation(clip):
		var anim := player.get_animation(clip)
		anim.loop_mode = Animation.LOOP_LINEAR
		player.play(clip, 0.25)
		if clip in ["idle", "sit"]:
			player.seek(anim.length * fposmod(float(id) * 0.618034, 1.0), true)
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


func _pose(character: Dictionary, state: Dictionary, entity_id: int = 0) -> void:
	var soldier := character["soldier"] as Soldier
	var vehicle := int(state.get("vehicle", -1))
	var vclass: Dictionary = _vehicles.class_of(vehicle) if vehicle >= 0 else {}
	var vkind := str(vclass.get("kind", ""))
	var is_open_hover := vkind == "hover" and int(state.get("seat", 0)) == 0 and (vclass.get("seats", []) as Array).is_empty()
	var is_open_turret := vkind == "turret" and int(state.get("seat", 0)) == 0 and str(vclass.get("rider_pose", "")) != ""
	# A gunner inside a closed turret/walker cab isn't seen; open hover riders and manned ground turret operators stay visible.
	soldier.model.visible = vehicle < 0 or is_open_hover or is_open_turret
	if vehicle >= 0:
		if soldier.model.visible:
			var seated := state.duplicate()
			seated["sprinting"] = false
			seated["velocity"] = Vector3.ZERO
			if is_open_turret:
				seated["crouched"] = false
				seated["ride_clip"] = str(vclass.get("rider_pose", ""))
				seated["position"] = _vehicles.turret_gunner_pos(entity_id, state["position"], float(state["yaw"]))
			else:
				seated["crouched"] = true  # (without a pose of its own)
				seated["ride_clip"] = str(vclass.get("rider_pose", ""))
				seated["position"] = Vehicles.to_world(state["position"], float(state["yaw"]),
						vclass.get("rider_seat", RIDER_SEAT))
			soldier.set_weapon("")
			soldier.pose(seated, _delta)
		return
	var posed := _with_swimming(state)
	# Its own weapon's model (slung while it swims).
	soldier.set_weapon("" if posed.get("swimming", false) else str(state.get("weapon_model", "")))
	soldier.set_anim_set(str(state.get("anim_set", "rifle")))  # held as its kind is
	soldier.pose(posed, _delta)


# Whether a character swims (feet at deep water's swim floor), for its pose.
func _with_swimming(state: Dictionary) -> Dictionary:
	if _client == null or not _client.has_method("get_swim_floor"):
		return state
	var at: Vector3 = state.get("position", Vector3.ZERO)
	var floor_y: float = _client.get_swim_floor(at.x, at.z)
	if is_nan(floor_y) or at.y > floor_y + 0.15:
		return state
	var swimming := state.duplicate()
	swimming["swimming"] = true
	return swimming


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
	var driving := int(_client.get_presented().get("vehicle", -1))
	if driving >= 0:
		# Behind and over it, from its TrackCenter, turning with the look.
		if soldier != null:
			PlayerAppearance.set_first_person(_own, false)
		var cls := _vehicles.class_of(driving)
		var track: Vector3 = cls.get("track_center", Vector3(0, 2.5, -6))
		var offset: Vector3 = cls.get("track_offset", Vector3.ZERO)
		_camera_yaw = _yaw
		var look := _camera_basis()
		var seat := int(_client.get_presented().get("seat", 0))
		var riding := _vehicles.riding(_client.get_entity_id())
		# A gunner's view follows the vehicle as it's drawn (its own state
		# lags: the server carries it).
		if seat > 0 and not riding.is_empty():
			target = riding["position"]
		# First person (the view key): from its eye point (the seat's, a
		# gunner's), its pilot looking where it flies, a driver or gunner
		# where they look.
		if _first_person and riding.get("node") is Node3D:
			var node: Node3D = riding["node"]
			var seats: Array = cls.get("seats", [])
			var eye: Vector3 = cls.get("eye", Vector3(0, 1.5, 0))
			if seat > 0 and seat - 1 < seats.size():
				eye = (seats[seat - 1] as Dictionary).get("eye", eye)
			_camera.global_position = node.global_transform * eye
			if str(cls.get("kind", "")) == "flyer" and seat == 0:
				_camera.global_basis = node.global_basis * Basis(Vector3.UP, PI)  # (its model's +z is ahead)
			else:
				_camera.global_basis = look
			return
		# Its distance by its size (its model's bounds): TrackCenter and
		# TrackOffset put the camera inside most starfighters (a Y-wing's
		# 3.5 m back, a gunship's), as only an X-wing's framed it. At least
		# as far as they say (ASSUMPTION: a little nearer than both, as for an
		# X-wing), and over it by its height.
		var bmin: Vector3 = cls.get("bounds_min", Vector3(-1, 0, -2))
		var bmax: Vector3 = cls.get("bounds_max", Vector3(1, 2, 2))
		var radius := maxf(1.5, (bmax - bmin).length() * 0.5)
		var distance := maxf(maxf(3.5, (offset.z - track.z) * 0.75), radius * 1.25 + 1.5)
		var height := maxf(track.y, (bmin.y + bmax.y) * 0.5 + (bmax.y - bmin.y) * 0.6)
		var pivot := target + Vector3.UP * height
		var at := pivot + look * Vector3(0, 0.3, distance)
		if not bool(_battlefield.get("is_space", false)):
			at.y = maxf(at.y, _client.get_ground_height(at.x, at.z, pivot.y) + 0.5)
		_camera.global_position = at
		_camera.look_at(pivot + look * Vector3(0, 0, -10), Vector3.UP)
		return
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
	if not bool(_battlefield.get("is_space", false)):
		position.y = maxf(position.y, _client.get_ground_height(position.x, position.z, pivot.y) + 0.4)
	_camera.global_position = position
	_camera.look_at(pivot + basis * Vector3(CAMERA_SHOULDER, 0, -10), Vector3.UP)


# A shot's recoil: the weapon's degrees kick the player's aim up (a little
# to a side), steadier crouched. Bots aim for themselves.
func _kick(slot: int) -> void:
	if _bot != "" or slot >= _weapons.size():
		return
	var recoil := float((_weapons[slot] as Dictionary).get("recoil", 0.0))
	if recoil <= 0.0:
		return
	if bool(_client.get_presented().get("crouched", false)):
		recoil /= 1.4  # (combat.h's crouch_aim_factor)
	_pitch = clampf(_pitch + deg_to_rad(recoil), -1.2, 0.6)
	_yaw += deg_to_rad(recoil) * randf_range(-0.3, 0.3)


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
	var weapon_now: Dictionary = _weapons[_weapon] if _weapon < _weapons.size() else {}
	var rate := ZOOM_RATE * float(weapon_now.get("aim_speed", 1.0))  # (its aim-down-sights speed)
	_zoom = lerpf(_zoom, target, minf(1.0, rate * delta))
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
				and not _battle_ui.aiming_at_peaceful() \
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
			if _hub_ui != null:
				_hub_ui.record_target_event(event)
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
		if event["kind"] == "shielded" and mine:
			_battle_ui.note_shielded()
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
		elif str(event["kind"]).begins_with("flag_"):
			_battle_ui.handle_flag_event(event, own_team, _who)
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
		var origin: Vector3 = shot["origin"]
		var offset := Vector3.ZERO
		# Riding, the vehicle's guns fire (from where the shot says): not the
		# soldier's, hidden in its seat.
		var riding := bool(shot.get("riding", false))
		if own_soldier != null and not riding:
			if int(shot["weapon"]) >= 2:
				own_soldier.throw(str(shot.get("archetype", "")), str(shot.get("held_model", "")))
			else:
				own_soldier.fire()
				_kick(int(shot["weapon"]))
			offset = own_soldier.muzzle() - origin
		if str(shot.get("projectile", "")) == "melee":
			if _audio != null:
				_audio.play_2d(str(shot.get("fire_sound", "")))
			continue
		var dirs: Array = shot["directions"]
		if not dirs.is_empty():
			_effects.spawn_muzzle_flash(origin + offset, dirs[0], shot)
			if _audio != null:
				_audio.play_2d(str(shot.get("fire_sound", "")))
		for direction: Vector3 in dirs:
			var node := _effects.make_projectile(shot)
			var velocity: Vector3 = direction * float(shot.get("bolt_speed", 300.0))
			var bolt := {"node": node, "position": origin, "velocity": velocity, "life": float(shot.get("bolt_life", 0.8)),
					"gravity": float(shot.get("bolt_gravity", 1.0)),
					"origin": origin, "offset": offset}
			_place_drawn_bolt(node, bolt, delta)
			_own_bolts.append(bolt)
	for i in range(_own_bolts.size() - 1, -1, -1):
		var bolt: Dictionary = _own_bolts[i]
		bolt["life"] = float(bolt["life"]) - delta
		bolt["velocity"] = bolt["velocity"] + Vector3.DOWN * 9.8 * float(bolt.get("gravity", 1.0)) * delta
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
					"prev_vel": bolt["velocity"], "bounce_cd": 0.0, "owner": int(bolt["owner"])}
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
	if bool(bolt.get("trap", false)):
		return _trap_shot(bolt)
	var owner_id := int(bolt["owner"])
	var soldier := _soldier_of(owner_id)
	if soldier == null or not soldier.model.visible:
		return {"origin": bolt["position"], "offset": Vector3.ZERO}
	if not (_remotes.has(owner_id) and bool(_remotes[owner_id].get("quiet", false))):
		_battle_ui.note_fired(owner_id)  # (on enemies' radar; not a quiet shooter: Low Profile)
	if owner_id != _client.get_entity_id():
		if bolt["kind"] == "grenade" or int(bolt["weapon"]) >= 2:
			soldier.throw(str(bolt.get("archetype", "")), str(bolt.get("held_model", "")))
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


# A placed trap's shot: from the trap (just above it), which turns to face
# whom it fires at; its owner doesn't move.
func _trap_shot(bolt: Dictionary) -> Dictionary:
	var at: Vector3 = bolt["position"]
	var velocity: Vector3 = bolt["velocity"]
	var nearest: Node3D = null
	var best := 1.2
	for entry: Dictionary in _remote_bolts.values():
		var node: Node3D = entry["node"]
		if int(entry.get("owner", -1)) != int(bolt["owner"]) or not is_instance_valid(node) \
				or not node.has_meta("placed"):
			continue
		var d := node.position.distance_to(at - Vector3.UP * 0.4)
		if d < best:
			best = d
			nearest = node
	if nearest != null and Vector2(velocity.x, velocity.z).length() > 0.01:
		nearest.set_meta("yaw", atan2(velocity.x, velocity.z))  # its model faces +Z
	_effects.spawn_muzzle_flash(at, velocity, bolt)
	if _audio != null and str(bolt.get("fire_sound", "")) != "":
		_audio.play_3d(str(bolt["fire_sound"]), at, _camera.global_position)
	return {"origin": at, "offset": Vector3.ZERO}


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
	if not Bf2Map.is_bf2(meta) and ClassDB.class_exists("SwmTerrain"):
		var streamer := SwgTerrainStreamer.new()
		streamer.name = "SwgTerrainStreamer"
		if streamer.setup(bake, meta):
			var sp := _resolve_spawn(meta)
			var focus := Vector3(-sp.x, 0.0, sp.y)
			streamer.warm_around(focus)
			_world.add_child(streamer)
			_terrain_streamer = streamer
			return
	var heights := FileAccess.get_file_as_bytes(bake + ".r32").to_float32_array()
	var mesh := MeshInstance3D.new()
	mesh.mesh = TerrainTool.build_mesh(heights, meta["samples"], meta["spacing"],
			Vector2(meta["origin_x"], meta["origin_z"]), 1)
	if Bf2Map.is_bf2(meta):
		mesh.material_override = Bf2Map.build_terrain_material(bake, meta)
	else:
		mesh.material_override = TerrainTool.build_material(bake, meta)
	_world.add_child(mesh)


# A planet's sky and haze where it isn't Tatooine's: hubs/<bake name>_look.ini
# ([look]: the sky shader's colors, zenith_color, mid_sky_color,
# horizon_color, ground_color, sun_a_color and sun_b_color, black for a
# planet with one sun; fog; sun: its light), each "r g b" (0..1).
func _planet_look(planet: String) -> Dictionary:
	var look := {}
	var file := ConfigFile.new()
	if file.load("res://hubs/%s_look.ini" % planet) != OK:
		return look
	for key: String in file.get_section_keys("look") if file.has_section("look") else PackedStringArray():
		var rgb := str(file.get_value("look", key)).split(" ", false)
		if rgb.size() >= 3:
			look[key] = Color(float(rgb[0]), float(rgb[1]), float(rgb[2]))
	return look


func _build_world(bake: String) -> bool:
	var meta: Variant = JSON.parse_string(FileAccess.get_file_as_string(bake + ".json"))
	if meta == null:
		printerr("cannot load terrain ", bake)
		return false
	if _world != null:
		remove_child(_world)
		_world.queue_free()
	_sky_dome = null
	_terrain_streamer = null
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
	_buildings_with_rooms = SwgBuilding.with_rooms(_world)
	if _battle_ui != null:  # (the first world's are passed on when it's made)
		_battle_ui.set_structures(_world.get_meta(SwgCity.FOOTPRINTS, []))

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
	else:
		var sky_mat := ShaderMaterial.new()
		sky_mat.shader = TATOOINE_SKY
		var sky := Sky.new()
		sky.sky_material = sky_mat
		environment.sky = sky
		environment.background_mode = Environment.BG_SKY
		environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
		environment.ambient_light_sky_contribution = 0.75
		environment.fog_enabled = true
		environment.fog_mode = Environment.FOG_MODE_DEPTH
		environment.fog_depth_begin = 220.0
		environment.fog_depth_end = 10500.0
		environment.fog_depth_curve = 1.4
		environment.fog_light_color = Color(0.86, 0.77, 0.63)
		environment.fog_aerial_perspective = 0.65
		environment.fog_sky_affect = 0.35
		sun.rotation_degrees = Vector3(-34, -42, 0)
		sun.light_color = Color(1.0, 0.95, 0.86)
		sun.light_energy = 1.45
		var look := _planet_look(bake.get_file())
		for key: String in look:
			if key.ends_with("_color"):
				sky_mat.set_shader_parameter(key, look[key])
		if look.has("fog"):
			environment.fog_light_color = look["fog"]
		if look.has("sun"):
			sun.light_color = look["sun"]
		# Its lakes, rivers and sea (Naboo's), if its terrain has any.
		SwgWater.add_water(_world, bake, sun, environment.fog_light_color)
	# The planet's weather (Kamino's storm, Hoth's snow...): looks and sound only.
	_weather.setup(_world, Weather.planet_of(bake, Bf2Map.is_bf2(meta as Dictionary)), environment, sun, self,
			DisplayServer.get_name().to_lower().contains("headless"))
	return true


func _setup_scene() -> void:
	_camera = Camera3D.new()
	_camera.fov = 65
	_camera.near = 0.05
	_camera_sphere.radius = 0.15
	_camera.far = 16000
	add_child(_camera)

	_pickups = Pickups.new(self, HUD_DIR)
	_effects = Bf2Effects.new(self, EFFECTS_DIR)
	_audio = Bf2Audio.new(self, AUDIO_DIR)
	_audio.apply_volumes(_settings)

	var layer := CanvasLayer.new()
	add_child(layer)

	_battle_ui = BattleUI.new()
	if _world != null:
		_battle_ui.set_structures(_world.get_meta(SwgCity.FOOTPRINTS, []))
	add_child(_battle_ui)
	_battle_ui.setup(layer, self, _audio, HUD_DIR)
	Bf2Art.apply_cursor(_battle_ui.art())
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
	_specialist_ui = SpecialistUIScript.new()
	add_child(_specialist_ui)
	_specialist_ui.setup(layer, self, _battle_ui.art(), _settings)
	_specialist_ui.screen_opened.connect(func() -> void: _capture_mouse(false))
	_specialist_ui.screen_closed.connect(func() -> void:
		if _bot == "" and (_pause_menu == null or not _pause_menu.visible):
			_capture_mouse(true))
	_hub_ui.queue_requested.connect(func(side: int, mode: String) -> void:
		_client.queue(side, mode if mode != "" else "conquest"))
	_hub_ui.item_bought.connect(func(def_id: String, use_fp: bool) -> void: _client.buy_item(def_id, use_fp))
	_hub_ui.loadout_saved.connect(func(slot: int, lo: Dictionary) -> void: _client.save_loadout(slot, lo))
	_hub_ui.loadout_activated.connect(func(slot: int) -> void: _client.switch_loadout(slot))
	_hub_ui.item_destroyed.connect(func(iid: int) -> void: _client.destroy_item(iid))
	_hub_ui.item_used.connect(func(iid: int) -> void: _client.use_item(iid))
	_hub_ui.loot_taken.connect(func(cid: int, slot: int) -> void: _client.take_loot(cid, slot))
	_hub_ui.item_sold.connect(func(iid: int) -> void: _client.sell_item(iid))
	_hub_ui.all_junk_sold.connect(func() -> void: _client.sell_all_junk())
	_hub_ui.corpse_harvested.connect(func(cid: int) -> void: _client.harvest_corpse(cid))
	_hub_ui.craft_requested.connect(func(sid: String, rids: PackedStringArray, eids: PackedInt64Array, qpct: int, cnt: int, mark: int) -> void:
		_client.craft_item(sid, rids, eids, qpct, cnt, mark))
	_hub_ui.dye_requested.connect(func(iid: int, dye: String) -> void: _client.dye_item(iid, dye))
	_hub_ui.preset_saved.connect(func(pname: String, sid: String, rids: PackedStringArray, edefs: PackedStringArray, qpct: int) -> void:
		_client.save_recipe_preset(pname, sid, rids, edefs, qpct))
	_hub_ui.preset_deleted.connect(func(pname: String) -> void: _client.delete_recipe_preset(pname))
	_hub_ui.work_order_fulfilled.connect(func(oid: String) -> void: _client.fulfill_work_order(oid))
	_hub_ui.item_salvaged.connect(func(iid: int) -> void: _client.salvage_item(iid))
	_hub_ui.service_requested.connect(func(action: int, id: String, amount: int, item_id: int) -> void:
		_client.service(action, id, amount, item_id))
	_hub_ui.quest_accepted.connect(func(id: String) -> void: _client.accept_quest(id))
	_hub_ui.quest_abandoned.connect(func(id: String) -> void: _client.abandon_quest(id))
	_hub_ui.terminal_closed.connect(func() -> void:
		if _bot == "" and (_pause_menu == null or not _pause_menu.visible):
			_capture_mouse(true))
	if ClassDB.class_exists("SwmCharacterStore") and FileAccess.file_exists(_os_path("items/swg_catalog.ini")):
		_catalog_store = ClassDB.instantiate("SwmCharacterStore")
		if FileAccess.file_exists(_os_path("professions/professions.ini")) \
				and _catalog_store.load_professions(_os_path("professions/professions.ini")) == OK:
			_hub_ui.set_professions(_catalog_store.get_professions(), _catalog_store)
		if FileAccess.file_exists(_os_path("crafting/resources.ini")) \
				and FileAccess.file_exists(_os_path("crafting/schematics.ini")) \
				and _catalog_store.load_crafting(_os_path("crafting/resources.ini"), _os_path("crafting/schematics.ini")) == OK:
			_hub_ui.set_crafting(_catalog_store)
		if _catalog_store.load_catalog(_os_path("items/swg_catalog.ini"), _os_path("imported_assets/items")) == OK:
			_catalog = _catalog_store.get_catalog()
			_battle_ui.set_catalog_names(_catalog)
			_hub_ui.set_catalog(_catalog, _catalog_store,
					str(_config.get("species", "human")),
					str(_config.get("gender", "male")),
					str(_config.get("appearance", "{}")))


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
	_vehicles.setup(_world, _vehicle_classes(), _effects, _audio)
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
	var owned := _battle_ui.owned_posts(game, int(own["team"]), _client)
	if not owned.has(_battle_ui.spawn_post):
		_battle_ui.spawn_post = owned[0] if not owned.is_empty() else -1
	if _bot != "" and _bot != "conquest" and not _battle_ui.spawn_requested_flag:
		_request_spawn()
	if _battle_ui.spawn_post >= 0:
		var target := _battle_ui.spawn_post_position(_client, _battle_ui.spawn_post)
		var view := target + Basis(Vector3.UP, _yaw) * Vector3(0, 28, 38)
		_camera.global_position = _camera.global_position.lerp(view, 0.1)
		_camera.look_at(target, Vector3.UP)


func _resolve_spawn(meta: Dictionary) -> Vector2:
	var sp: Variant = _config.get("spawn", null)
	if sp is Vector2:
		return sp
	var hub_path := str(_config.get("hub", ""))
	if hub_path != "":
		var full := _os_path(hub_path)
		if FileAccess.file_exists(full):
			for line: String in FileAccess.get_file_as_string(full).split("\n"):
				var trimmed := line.strip_edges()
				if trimmed.begins_with("Spawn") and trimmed.contains("="):
					var parts := trimmed.split("=", false, 1)[1].strip_edges().split(" ", false)
					if parts.size() >= 2:
						return Vector2(float(parts[0]), float(parts[1]))
		if hub_path.contains("mos_eisley"):
			return Vector2(3500.0, -4800.0)
	var half: float = (int(meta["samples"]) - 1) * float(meta["spacing"]) / 2.0
	return Vector2(float(meta["origin_x"]) + half, float(meta["origin_z"]) + half)


func _os_path(path: String) -> String:
	return path if path.is_absolute_path() else ProjectSettings.globalize_path("res://" + path)


func _game_path(path: String) -> String:
	return ProjectSettings.localize_path(path).trim_prefix("res://")
