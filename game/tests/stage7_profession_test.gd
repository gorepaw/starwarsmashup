# Headless checks for Stage 7's professions:
# 1. The roster loads through SwmCharacterStore (31 professions, their
#    milestones and perks in words, the level curve), and the planner draws
#    and slots and unslots professions in a loadout (only slottable ones,
#    three at most).
# 2. A live Mos Eisley session: the planner opens, a loadout's slots are
#    saved through the server (which keeps only slottable professions) and
#    come back to the client, and the client's profession list arrives.
# Pass a path as the first user argument to save a screenshot of the planner.
extends SceneTree

const Bf2Art = preload("res://ui/bf2_art.gd")
const ProfessionScreen = preload("res://ui/profession_screen.gd")
const GameSession = preload("res://scenes/game_session.gd")

var _session: GameSession
var _screen: ProfessionScreen
var _db := ""
var _frames := 0
var _shot := ""
var _phase := "screen"
var _phase_frames := 0


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	_shot = args[0] if args.size() > 0 else ""
	DisplayServer.window_set_size(Vector2i(1280, 720))
	var art := Bf2Art.load_from("res://imported_assets/hud")
	assert(art != null, "missing hud art")
	var store: Object = ClassDB.instantiate("SwmCharacterStore")
	assert(store.load_professions(ProjectSettings.globalize_path("res://professions/professions.ini")) == OK,
			"the roster didn't load")
	var roster: Array = store.get_professions()
	assert(roster.size() == 31, "expected 31 professions, got %d" % roster.size())
	var rifles: Dictionary = roster.filter(func(p: Dictionary) -> bool: return p["id"] == "rifles")[0]
	assert((rifles["milestones"] as Array).size() == 5)
	var first: Dictionary = rifles["milestones"][0]
	assert(first["perks"] == ["3% tighter spread"], "perk words: %s" % [first["perks"]])
	var lv: Dictionary = store.profession_level(215 + 50)
	assert(int(lv["level"]) == 3 and int(lv["into"]) == 50 and int(lv["needed"]) == 130, "curve: %s" % lv)
	assert(int(store.get_specialization_slots()) == 3)

	_screen = ProfessionScreen.new(art)
	_screen.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_child(_screen)
	_screen.professions = roster
	_screen.store = store
	_screen.progress = {"rifles": 21000, "medium_armor": 4000, "pistols": 300}
	_screen.loadouts = [{"slot_index": 0, "name": "Standard Issue", "specializations": []},
			{"slot_index": 1, "name": "Scout", "specializations": ["pistols"]}]
	var saved := []
	_screen.loadout_saved.connect(func(slot: int, lo: Dictionary) -> void: saved.append([slot, lo["specializations"]]))
	_screen.open_screen()
	_screen._on_click("slot:rifles", Vector2.ZERO)
	_screen._on_click("slot:bounty_hunter", Vector2.ZERO)  # Wave 7B: not in the game yet
	_screen._on_click("slot:weaponsmith", Vector2.ZERO)    # not a combat profession
	_screen._on_click("slot:medium_armor", Vector2.ZERO)
	_screen._on_click("slot:pistols", Vector2.ZERO)
	_screen._on_click("slot:heavy_armor", Vector2.ZERO)  # the slots are full
	assert(saved.size() == 3, "saves: %s" % [saved])
	assert(saved.back() == [0, ["rifles", "medium_armor", "pistols"]], "slots: %s" % [saved.back()])
	_screen._on_click("unslot:pistols", Vector2.ZERO)
	assert(saved.back() == [0, ["rifles", "medium_armor"]])
	_screen._on_click("row:4", Vector2.ZERO)  # Pistols, in Combat
	_screen.queue_redraw()
	assert(store.load_catalog(ProjectSettings.globalize_path("res://items/swg_catalog.ini"),
			ProjectSettings.globalize_path("res://imported_assets/items")) == OK)
	var psg: Dictionary = store.resolve_item_stats("psg_mark1", 0.5, true)
	assert((psg.get("modifiers", []) as Array).any(func(m: Dictionary) -> bool:
		return m.get("stat_name") == "shield_capacity" and float(m.get("add", 0.0)) > 0.0), "PSG shield capacity: %s" % psg)
	var ion: Dictionary = store.resolve_item_stats("wp_pistol_ion", 0.5, true)
	assert(ion.get("damage_type") == "electro", "Ion pistol type: %s" % ion)
	var blade: Dictionary = store.resolve_item_stats("wp_melee_vibroblade", 0.5, true)
	assert(blade.get("projectile") == "melee", "Melee blade projectile: %s" % blade)
	print("Stage 7 profession planner: OK")


func _process(_delta: float) -> bool:
	_frames += 1
	_phase_frames += 1
	match _phase:
		"screen":
			if _phase_frames < 4:
				return false
			if _shot != "":
				root.get_texture().get_image().save_png(_shot)
				print("wrote ", _shot)
			_screen.queue_free()
			_start_session()
			_next("join")
		"join":
			var client: Object = _session.get_client()
			if client == null or not client.is_spawned() or client.get_character_loadouts().is_empty():
				if _phase_frames > 600:
					printerr("FAIL: no character loadouts arrived")
					_cleanup(1)
				return false
			var hub_ui: Node = _session.get("_hub_ui")
			assert(hub_ui.toggle_professions(client), "the planner didn't open")
			var lo: Dictionary = (client.get_character_loadouts()[0] as Dictionary).duplicate(true)
			lo["specializations"] = ["carbines", "bounty_hunter", "light_armor", "carbines"]
			client.save_loadout(int(lo["slot_index"]), lo)
			_next("saved")
		"saved":
			var client: Object = _session.get_client()
			var lo: Dictionary = client.get_character_loadouts()[0]
			if lo.get("specializations", []) != ["carbines", "light_armor"]:
				if _phase_frames > 300:
					printerr("FAIL: slots came back as %s" % [lo.get("specializations", [])])
					_cleanup(1)
				return false
			var hub_ui: Node = _session.get("_hub_ui")
			var screen: Object = hub_ui.get("_profession_screen")
			assert(screen.visible and (screen.loadouts as Array).size() >= 1)
			assert(screen.loadouts[0]["specializations"] == ["carbines", "light_armor"])
			print("Stage 7 Mos Eisley professions: OK (slots %s, %d professions trained)" % [
					lo["specializations"], client.get_professions().size()])
			_cleanup(0)
			return true
	return false


func _next(phase: String) -> void:
	_phase = phase
	_phase_frames = 0


func _start_session() -> void:
	_db = ProjectSettings.globalize_path("res://stage7_profession_temp.sqlite")
	_remove_db()
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/tatooine" if FileAccess.file_exists("res://imported_assets/terrain/tatooine.json")
				else "imported_assets/terrain/tatooine_mos_eisley",
		"host": 0,
		"db": _db,
		"hub": "hubs/mos_eisley.ini",
		"name": "Stage7Trainee",
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
	quit(code)
