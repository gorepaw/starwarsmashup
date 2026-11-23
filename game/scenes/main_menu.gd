# Main menu scene: Galaxies backdrop, Battlefront II-styled main menu
# (New Character, Load Character, Settings, Quit), and transitions to the
# 3D Character Creator, Load Character, and Settings screens.
extends Node3D

const Bf2Art = preload("res://ui/bf2_art.gd")
const Bf2Screen = preload("res://ui/bf2_screen.gd")
const Settings = preload("res://ui/settings.gd")
const SettingsScreen = preload("res://ui/settings_screen.gd")
const LoadCharacterScreen = preload("res://ui/load_character_screen.gd")
const CharacterCreatorScreen = preload("res://ui/character_creator_screen.gd")
const CharacterStage = preload("res://rendering/character_stage.gd")
const TerrainTool = preload("res://tools/render_terrain.gd")
const Bf2Audio = preload("res://audio/bf2_audio.gd")
const GameSession = preload("res://scenes/game_session.gd")

const HUD_DIR := "res://imported_assets/hud"
const AUDIO_DIR := "res://imported_assets/audio"
const TERRAIN_BAKE := "res://imported_assets/terrain/tatooine_mos_eisley"
const WORLD_DB := "res://world.sqlite"

var _art: RefCounted
var _settings: Settings
var _store: Object
var _audio: Bf2Audio
var _camera: Camera3D
var _backdrop: Node3D
var _stage: CharacterStage
var _menu_overlay: Control
var _settings_screen: Control
var _load_screen: Control
var _creator_screen: Control
var _mode := "main"
var _orbit_time := 0.0
var _status_msg := ""


func _ready() -> void:
	_settings = Settings.new()
	_settings.load_file()
	_art = Bf2Art.load_from(HUD_DIR)
	_audio = Bf2Audio.new(self, AUDIO_DIR)
	_audio.apply_volumes(_settings)

	if ClassDB.class_exists("SwmCharacterStore"):
		_store = ClassDB.instantiate("SwmCharacterStore")
		var db_os := ProjectSettings.globalize_path(WORLD_DB)
		if _store.open(db_os) != OK:
			_status_msg = "Database error: " + str(_store.get_last_error())

	_setup_3d()
	_setup_ui()
	_show_mode("main")


func _process(delta: float) -> void:
	_orbit_time += delta
	if _audio != null:
		_audio.update(delta, _camera.global_position if _camera != null else Vector3.ZERO)
	if _camera == null:
		return
	if _mode == "create" and _stage != null:
		var eye_y := _stage.eye_y()
		var target := Vector3(0.0, eye_y - 0.05, 0.0) if _creator_screen.is_face_camera() \
				else Vector3(0.0, eye_y * 0.55, 0.0)
		var wanted := Vector3(0.0, eye_y, 0.85) if _creator_screen.is_face_camera() \
				else Vector3(0.0, eye_y * 0.65, 2.75)
		_camera.global_position = _camera.global_position.lerp(wanted, minf(1.0, 8.0 * delta))
		_camera.look_at(target, Vector3.UP)
	else:
		var angle := _orbit_time * 0.04
		_camera.global_position = Vector3(sin(angle) * 45.0, 22.0, cos(angle) * 45.0)
		_camera.look_at(Vector3(0.0, 6.0, 0.0), Vector3.UP)


func _exit_tree() -> void:
	if _audio != null:
		_audio.dispose()
	if _store != null:
		_store.close()


func _setup_3d() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.76, 0.68, 0.56)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.65, 0.58, 0.50)
	env.fog_enabled = true
	env.fog_light_color = Color(0.76, 0.68, 0.56)
	env.fog_density = 0.0005
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-42.0, -35.0, 0.0)
	sun.light_energy = 1.4
	add_child(sun)

	_camera = Camera3D.new()
	_camera.fov = 55.0
	_camera.far = 4000.0
	add_child(_camera)

	_backdrop = Node3D.new()
	add_child(_backdrop)
	if FileAccess.file_exists(TERRAIN_BAKE + ".json") and FileAccess.file_exists(TERRAIN_BAKE + ".r32"):
		var meta: Variant = JSON.parse_string(FileAccess.get_file_as_string(TERRAIN_BAKE + ".json"))
		if meta is Dictionary:
			var heights := FileAccess.get_file_as_bytes(TERRAIN_BAKE + ".r32").to_float32_array()
			var mesh := MeshInstance3D.new()
			mesh.mesh = TerrainTool.build_mesh(heights, int(meta["samples"]), float(meta["spacing"]),
					Vector2(float(meta["origin_x"]), float(meta["origin_z"])), 4)
			mesh.material_override = TerrainTool.build_material(TERRAIN_BAKE, meta)
			mesh.position = Vector3(3550.0, -5.0, 4780.0)
			_backdrop.add_child(mesh)

	_stage = CharacterStage.new()
	_stage.visible = false
	add_child(_stage)


func _setup_ui() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)

	_menu_overlay = MainMenuOverlay.new(_art)
	_menu_overlay.action_chosen.connect(_on_menu_action)
	layer.add_child(_menu_overlay)

	_load_screen = LoadCharacterScreen.new(_art)
	_load_screen.visible = false
	_load_screen.character_selected.connect(_launch_character)
	_load_screen.new_requested.connect(func() -> void: _show_mode("create"))
	_load_screen.closed.connect(func() -> void: _show_mode("main"))
	layer.add_child(_load_screen)

	_creator_screen = CharacterCreatorScreen.new(_art)
	_creator_screen.visible = false
	_creator_screen.character_created.connect(_launch_character)
	_creator_screen.closed.connect(func() -> void: _show_mode("main"))
	layer.add_child(_creator_screen)

	_settings_screen = SettingsScreen.new(_art)
	_settings_screen.visible = false
	_settings_screen.closed.connect(func() -> void: _show_mode("main"))
	layer.add_child(_settings_screen)

	for scr: Control in [_menu_overlay, _load_screen, _creator_screen, _settings_screen]:
		scr.clicked.connect(func(_id: String) -> void:
			if _audio != null:
				_audio.play_2d("shell_select_unit"))


func _show_mode(mode: String) -> void:
	_mode = mode
	_menu_overlay.visible = mode == "main"
	_load_screen.visible = mode == "load"
	_creator_screen.visible = mode == "create"
	_settings_screen.visible = mode == "settings"
	_stage.visible = mode == "create"
	_backdrop.visible = mode != "create"
	match mode:
		"main":
			_menu_overlay.set("status_text", _status_msg)
			_menu_overlay.queue_redraw()
		"load":
			_load_screen.open(_store)
		"create":
			_camera.global_position = Vector3(0.0, 1.1, 2.75)
			_creator_screen.open(_store, _stage)
		"settings":
			_settings_screen.open(_settings)


func _on_menu_action(action: String) -> void:
	match action:
		"new":
			_show_mode("create")
		"load":
			_show_mode("load")
		"settings":
			_show_mode("settings")
		"quit":
			get_tree().quit(0)


func _launch_character(record: Dictionary) -> void:
	var sp := str(record.get("species", "human"))
	var gen := str(record.get("gender", "male"))
	var char_name := str(record.get("name", "player"))
	var gltf_rel := "imported_assets/species/%s_%s/%s_%s.gltf" % [sp, gen, sp, gen]
	if not FileAccess.file_exists("res://" + gltf_rel):
		gltf_rel = "imported_assets/characters/stormtrooper/stormtrooper.gltf"
	if _store != null:
		_store.close()
		_store = null
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/tatooine_mos_eisley",
		"character_path": gltf_rel,
		"host": 0,
		"db": "world.sqlite",
		"hub": "hubs/mos_eisley.ini",
		"name": char_name,
		"species": sp,
		"gender": gen,
		"appearance": str(record.get("appearance", "{}")),
		"return_to_menu": true,
	}
	get_tree().change_scene_to_file("res://scenes/game_session.tscn")


class MainMenuOverlay extends Bf2Screen:
	signal action_chosen(action: String)
	var status_text := ""

	func _paint() -> void:
		var box := centered(360, 340)
		frame(box)
		text("gamefont_large", "STAR WARS MASHUP", Vector2(box.get_center().x, box.position.y + 22 * u),
				"Center", ACCENT)
		text("gamefont_small", "Galaxies & Battlefront II", Vector2(box.get_center().x, box.position.y + 56 * u),
				"Center", DIM)
		var w := 240.0 * u
		var x := box.get_center().x - w * 0.5
		var y := box.position.y + 96 * u
		button(Rect2(x, y, w, 38 * u), "New Character", "new")
		button(Rect2(x, y + 50 * u, w, 38 * u), "Load Character", "load")
		button(Rect2(x, y + 100 * u, w, 38 * u), "Settings", "settings")
		button(Rect2(x, y + 150 * u, w, 38 * u), art.localize("common.quit"), "quit", false, true)
		if status_text != "":
			text("gamefont_small", status_text, Vector2(box.get_center().x, box.end.y - 28 * u),
					"Center", Color(1.0, 0.35, 0.3))

	func _on_click(id: String, _at: Vector2) -> void:
		action_chosen.emit(id)
