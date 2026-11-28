# Headless checks for the professions (docs/PROFESSIONS.md, docs/PERKS.md):
# 1. The roster loads through SwmCharacterStore (29 professions; weapon and
#    armor ones with their perks, the rest with milestones, in words; the
#    level curve and the perk cap), and the planner draws what's known
#    against the cap, and a trainer's screen its perks and their buttons.
# 2. A live Mos Eisley session: the planner opens with the client's
#    professions.
# Pass a path as the first user argument to save a screenshot of the planner.
extends SceneTree

const Bf2Art = preload("res://ui/bf2_art.gd")
const ProfessionScreen = preload("res://ui/profession_screen.gd")
const TrainerScreen = preload("res://ui/trainer_screen.gd")
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
	assert(roster.size() == 29, "expected 29 professions, got %d" % roster.size())
	var rifles: Dictionary = roster.filter(func(p: Dictionary) -> bool: return p["id"] == "rifles")[0]
	assert((rifles["milestones"] as Array).is_empty() and (rifles["perks"] as Array).size() == 10)
	var first: Dictionary = rifles["perks"][0]
	assert(first["id"] == "rifles_long_barrel" and first["choice"] == "A" and int(first["level"]) == 5)
	assert(first["perks"] == ["20% longer full-damage range"], "perk words: %s" % [first["perks"]])
	assert(int(first["cost"]) == 500 and str(first["needs"]) == "")
	# An advanced profession of perks (from 5: no licence), and one still of milestones.
	var doctor: Dictionary = roster.filter(func(p: Dictionary) -> bool: return p["id"] == "doctor")[0]
	assert((doctor["milestones"] as Array).is_empty() and (doctor["perks"] as Array).size() == 10)
	assert(str(doctor["pool"]) == "combat" and int(doctor["first_perk"]) == 5)
	var ranger: Dictionary = roster.filter(func(p: Dictionary) -> bool: return p["id"] == "ranger")[0]
	assert((ranger["milestones"] as Array).size() == 5 and (ranger["perks"] as Array).is_empty())
	var entertainer: Dictionary = roster.filter(func(p: Dictionary) -> bool: return p["id"] == "entertainer")[0]
	assert(str(entertainer["pool"]) == "noncombat" and int(entertainer["first_perk"]) == 1)
	var lv: Dictionary = store.profession_level(215 + 50)
	assert(int(lv["level"]) == 3 and int(lv["into"]) == 50 and int(lv["needed"]) == 130, "curve: %s" % lv)
	assert(int(store.get_perk_cap()) == 10)

	_screen = ProfessionScreen.new(art)
	_screen.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_child(_screen)
	_screen.professions = roster
	_screen.store = store
	_screen.cap = int(store.get_perk_cap())
	_screen.progress = {"rifles": 21000, "medium_armor": 4000, "pistols": 300, "ranger": 9000}
	_screen.known = {"rifles": ["rifles_long_barrel", "rifles_braced"], "medium_armor": ["medium_conditioning"]}
	_screen.trained = {"ranger": 25}
	_screen.open_screen()
	assert(_screen._learned().size() == 5, "known: %s" % [_screen._learned()])
	_screen._on_click("row:6", Vector2.ZERO)  # Rifles, in Combat
	_screen.queue_redraw()

	# A trainer's screen: perks side by side, learning and giving up.
	var trainer := TrainerScreen.new(art)
	trainer.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_child(trainer)
	trainer.professions = [rifles]
	trainer.progress = {"rifles": {"level": 30, "perks": ["rifles_long_barrel"], "paid_perks": ["rifles_long_barrel"]}}
	trainer.known_counts = {"combat": 1, "noncombat": 0}
	trainer.credits = 1000
	var asked := []
	trainer.service_requested.connect(func(action: int, id: String, amount: int) -> void: asked.append([action, id, amount]))
	trainer.open_trainer()
	trainer._on_click("perk:rifles_braced", Vector2.ZERO)
	trainer._on_click("forget:rifles_long_barrel", Vector2.ZERO)
	assert(asked == [[8, "rifles_braced", 0], [9, "rifles_long_barrel", 0]], "trainer asked: %s" % [asked])
	trainer.queue_redraw()
	assert(store.load_catalog(ProjectSettings.globalize_path("res://items/swg_catalog.ini"),
			ProjectSettings.globalize_path("res://imported_assets/items")) == OK)
	var psg: Dictionary = store.resolve_item_stats("psg_mark1")
	assert((psg.get("modifiers", []) as Array).any(func(m: Dictionary) -> bool:
		return m.get("stat_name") == "shield_capacity" and float(m.get("add", 0.0)) > 0.0), "PSG shield capacity: %s" % psg)
	var ion: Dictionary = store.resolve_item_stats("wp_pistol_ion")
	assert(ion.get("damage_type") == "electro", "Ion pistol type: %s" % ion)
	var blade: Dictionary = store.resolve_item_stats("wp_melee_vibroblade")
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
			for child in root.get_children():
				if child is TrainerScreen:
					child.queue_free()
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
			_next("open")
		"open":
			if _phase_frames < 10:
				return false
			var client: Object = _session.get_client()
			var hub_ui: Node = _session.get("_hub_ui")
			var screen: Object = hub_ui.get("_profession_screen")
			assert(screen.visible and int(screen.cap) == 10)
			assert(screen._learned().is_empty(), "a new character knows nothing: %s" % [screen._learned()])
			print("Stage 7 Mos Eisley professions: OK (%d professions with experience)" % client.get_professions().size())
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
