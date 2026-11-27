# Headless checks for Stage 7's gathering, Material Vault, Multi-Scanner, and Unified Workbench:
# 1. Crafting data loads through SwmCharacterStore (16 resources, 21 schematics, 5 work orders),
#    scan_terrain / auto_fill_schematic / preview_craft work, and WorkbenchScreen renders.
# 2. Live solo Mos Eisley session: workbenches arrive, a trainer teaches Surveying's licence,
#    the Multi-Scanner extracts into the Material Vault,
#    Recipe Presets save & sync, and Unified Workbench opens with synced materials & presets.
extends SceneTree

const Bf2Art = preload("res://ui/bf2_art.gd")
const WorkbenchScreen = preload("res://ui/workbench_screen.gd")
const GameSession = preload("res://scenes/game_session.gd")

var _session: GameSession
var _screen: WorkbenchScreen
var _db := ""
var _desert_hub := ""
var _frames := 0
var _phase := "screen"
var _phase_frames := 0
var _phase_sec := 0.0


func _initialize() -> void:
	DisplayServer.window_set_size(Vector2i(1280, 720))
	var art := Bf2Art.load_from("res://imported_assets/hud")
	assert(art != null, "missing hud art")
	var store: Object = ClassDB.instantiate("SwmCharacterStore")
	assert(store.load_professions(ProjectSettings.globalize_path("res://professions/professions.ini")) == OK)
	assert(store.load_catalog(ProjectSettings.globalize_path("res://items/swg_catalog.ini"),
			ProjectSettings.globalize_path("res://imported_assets/items")) == OK)
	assert(store.load_crafting(ProjectSettings.globalize_path("res://crafting/resources.ini"),
			ProjectSettings.globalize_path("res://crafting/schematics.ini")) == OK, "crafting failed to load")

	var res: Array = store.get_resources()
	var sch: Array = store.get_schematics()
	var wo: Array = store.get_work_orders()
	assert(res.size() == 16, "expected 16 resources, got %d" % res.size())
	assert(sch.size() >= 26, "expected at least 26 schematics, got %d" % sch.size())
	assert(wo.size() == 5, "expected 5 work orders, got %d" % wo.size())

	var scan: Dictionary = store.scan_terrain("mineral", -3520.0, -4790.0, 60.0, 17)
	assert(not scan.is_empty() and str(scan.get("resource_id", "")) != "", "scan_terrain failed: %s" % scan)
	assert((scan.get("grid", PackedFloat32Array()) as PackedFloat32Array).size() == 17 * 17)

	var vault := [{"resource_id": "min_tatooine_durasteel", "amount": 25}, {"resource_id": "gas_inert_argon", "amount": 25}]
	var filled: PackedStringArray = store.auto_fill_schematic("sch_wp_pistol_d18", vault, "best_stat")
	assert(filled.size() == 2 and filled[0] == "min_tatooine_durasteel" and filled[1] == "gas_inert_argon", "auto_fill: %s" % [filled])
	var prev: Dictionary = store.preview_craft("sch_wp_pistol_d18", filled, 1, 20, 70)
	assert(float(prev.get("base_quality", 0.0)) > 0.0, "preview: %s" % prev)

	_screen = WorkbenchScreen.new(art)
	_screen.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_child(_screen)
	_screen.store = store
	_screen.catalog = store.get_catalog()
	_screen.resources = res
	_screen.schematics = sch
	_screen.work_orders = wo
	_screen.materials = vault
	_screen.open_screen()
	_screen.queue_redraw()
	# docs/CRAFTING.md: the families' schematics, with their groups and Mk
	# tiers; amounts at a mark; the group picker; the Dye tab (drawn below).
	var padded: Dictionary = {}
	for s: Dictionary in sch:
		if str(s["id"]) == "sch_armor_padded_chest":
			padded = s
	assert(str(padded.get("group", "")) == "Medium Armor" and bool(padded.get("marks", false)), "padded: %s" % padded)
	assert(store.auto_fill_schematic("sch_wp_pistol_d18", vault, "best_stat", 10)[0] != "", "Mk X fits 25 durasteel")
	assert(store.auto_fill_schematic("sch_wp_pistol_d18", [{"resource_id": "min_tatooine_durasteel", "amount": 5},
			{"resource_id": "gas_inert_argon", "amount": 5}], "best_stat", 2)[0] == "", "Mk II takes 6 durasteel")
	_screen._on_click("group:+1", Vector2.ZERO)
	assert(_screen._group != "" and not _screen._filtered_schematics().is_empty(), "group picker")
	_screen.inventory = [{"id": 7, "def_id": "cloth_shirt_s04", "quality": 0.5, "color_json": "{\"dye\":\"index_color_1=5\"}"}]
	_screen._on_click("tab:dye", Vector2.ZERO)
	_screen._on_click("dyeitem:7", Vector2.ZERO)
	assert(int(_screen._dye_choice.get("index_color_1", -1)) == 5, "dye read back: %s" % _screen._dye_choice)
	assert(not _screen._dye_variables(_screen._dye_entry()["def"]).is_empty(), "the shirt's colors")
	_screen._on_click("tab:workbench", Vector2.ZERO)
	print("Stage 7 crafting store & workbench screen: OK")


func _process(delta: float) -> bool:
	_frames += 1
	_phase_frames += 1
	_phase_sec += delta
	match _phase:
		"screen":
			if _screen == null:
				quit(1)
				return true
			if _phase_frames == 2:  # (each tab drawn: the workbench, then the dyes)
				_screen._on_click("tab:dye", Vector2.ZERO)
			if _phase_frames < 4:
				return false
			_screen.queue_free()
			_start_session()
			_next("join")
		"join":
			var client: Object = _session.get_client()
			if client == null or not client.is_spawned() or client.get_workbenches().is_empty():
				if _phase_frames > 600:
					printerr("FAIL: client did not spawn with workbenches")
					_cleanup(1)
				return false
			var hub_ui: Node = _session.get("_hub_ui")
			assert(hub_ui.toggle_scanner(), "scanner did not turn on")
			assert(hub_ui.extract_scanner(client), "scanner extract failed")
			_next("city_blocked")
		"city_blocked":
			var client: Object = _session.get_client()
			var notice := str(client.get_player_status().get("notice", ""))
			if not notice.contains("Cannot extract"):
				if _phase_frames > 240:
					printerr("FAIL: expected city extraction block notice, got '%s'" % notice)
					_cleanup(1)
				return false
			_session.leave_session()
			_session.queue_free()
			_session = null
			_write_desert_hub()
			_start_session("hubs/stage7_desert_temp.ini")
			_next("desert_join")
		"desert_join":
			var client: Object = _session.get_client()
			if client == null or not client.is_spawned():
				if _phase_frames > 600:
					printerr("FAIL: client did not spawn in desert hub")
					_cleanup(1)
				return false
			# Extracting takes Surveying's licence (docs/PERKS.md): its first milestone, free.
			client.service(0, "surveying", 1, 0)
			_next("licensed")
		"licensed":
			var client: Object = _session.get_client()
			var trained := 0
			for p: Dictionary in client.get_professions():
				if str(p["id"]) == "surveying":
					trained = int(p.get("trained", 0))
			if trained < 1:
				if _phase_sec > 5.0:
					printerr("FAIL: the Survey Trainer didn't teach the licence")
					_cleanup(1)
				return false
			var hub_ui: Node = _session.get("_hub_ui")
			assert(hub_ui.toggle_scanner(), "desert scanner did not turn on")
			assert(hub_ui.extract_scanner(client), "desert scanner extract failed")
			_next("extracted")
		"extracted":
			var client: Object = _session.get_client()
			var mats: Array = client.get_materials()
			if mats.is_empty():
				if _phase_sec > 10.0:
					printerr("FAIL: no materials arrived after extraction")
					_cleanup(1)
				return false
			var rid := str(mats[0]["resource_id"])
			client.save_recipe_preset("Solo Preset", "sch_wp_pistol_d18", PackedStringArray([rid]), PackedStringArray(), 65)
			_next("preset")
		"preset":
			var client: Object = _session.get_client()
			var presets: Array = client.get_presets()
			if presets.is_empty():
				if _phase_sec > 5.0:
					printerr("FAIL: recipe preset did not sync")
					_cleanup(1)
				return false
			var hub_ui: Node = _session.get("_hub_ui")
			assert(hub_ui.toggle_workbench(client), "workbench did not open")
			var wb: Object = hub_ui.get("_workbench_screen")
			assert(wb.visible and (wb.materials as Array).size() >= 1 and (wb.presets as Array).size() >= 1)
			print("Stage 7 Mos Eisley crafting: OK (%d materials, %d presets)" % [
					client.get_materials().size(), presets.size()])
			_cleanup(0)
			return true
	return false


func _next(phase: String) -> void:
	_phase = phase
	_phase_frames = 0
	_phase_sec = 0.0


func _write_desert_hub() -> void:
	_desert_hub = ProjectSettings.globalize_path("res://hubs/stage7_desert_temp.ini")
	var f := FileAccess.open(_desert_hub, FileAccess.WRITE)
	f.store_string("[hub]\nName = Dune Camp\nSpawn = 3500 -5200\nSides = Rebellion, Empire\nBattlefield = ../battlefields/coruscant.ini\nResources = ../crafting/resources.ini\nSchematics = ../crafting/schematics.ini\n\n[recruiter]\nName = Desert Terminal\nPosition = 3500 -5300\nRadius = 4\n\n[workbench]\nName = Desert Workbench\nPosition = 3500 -5200\nRadius = 4\n\n[service]\nName = Survey Trainer\nType = trainer\nProfession = surveying\nPosition = 3500 -5200\nRadius = 6\n")
	f.close()


func _start_session(hub_path := "hubs/mos_eisley.ini") -> void:
	_db = ProjectSettings.globalize_path("res://stage7_crafting_temp.sqlite")
	_remove_db()
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/tatooine" if FileAccess.file_exists("res://imported_assets/terrain/tatooine.json")
				else "imported_assets/terrain/tatooine_mos_eisley",
		"host": 0,
		"db": _db,
		"hub": hub_path,
		"name": "Stage7Crafter",
		"species": "human",
		"gender": "male",
		"appearance": "{}",
		"solo": true,
	}
	_session = (load("res://scenes/game_session.tscn") as PackedScene).instantiate() as GameSession
	root.add_child(_session)


func _remove_db() -> void:
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(_db + suffix):
			DirAccess.remove_absolute(_db + suffix)


func _cleanup(code: int) -> void:
	if _session != null:
		_session.leave_session()
		_session.queue_free()
		_session = null
	_remove_db()
	if _desert_hub != "" and FileAccess.file_exists(_desert_hub):
		DirAccess.remove_absolute(_desert_hub)
	quit(code)
