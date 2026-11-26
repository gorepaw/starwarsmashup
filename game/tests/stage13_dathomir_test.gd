# Stage 13 check: Dathomir's people and services. A hosted session on Dathomir
# (the Science Outpost's hub file with the Trade Outpost and the Imperial
# prison) with a new character on the plaza by the Science Outpost's starport:
#   - the outposts' trainers (in their sheds' and houses' rooms: Dathomir has
#     no guild halls), banks, clinics, starports, shuttleports and vendors are
#     on offer, and dathomir_quests.ini's givers (the prison's warden too);
#   - a trainer in the west house stands in his room, on its floor 0.7 m up
#     (Height), not on the terrain under the house.
# Screenshots go to the path in SWM_SHOTS (if set), as <path>NN.png.
#
#   <godot> --path game --script res://tests/stage13_dathomir_test.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

const OUTSIDE := Vector2(-90.0, -1599.0)     # Galaxies x z: the plaza west of the starport
const MAREND := Vector2(-108.14, -1570.95)   # in the west house's office
const MAREND_FLOOR := 18.7                   # the plaza's 18 m, and the room's 0.7

var _session: GameSession
var _db := ""
var _frames := 0
var _phase := 0
var _phase_frame := 0
var _shots := ""
var _shot := 0


func _initialize() -> void:
	_shots = OS.get_environment("SWM_SHOTS")
	_db = ProjectSettings.globalize_path("res://stage13_dathomir_temp.sqlite")
	_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/dathomir",
		"host": 0,
		"db": _db,
		"hub": "hubs/dathomir_science_outpost.ini",
		"name": "Stage13Trainee",
		"species": "human",
		"gender": "male",
		"appearance": "{}",
		"solo": true,
		"spawn": OUTSIDE,
		"third_person": true,
	}
	_session = packed.instantiate() as GameSession
	root.add_child(_session)


func _process(_delta: float) -> bool:
	if _session == null:
		return false
	_frames += 1
	var client: Object = _session.get_client()
	if client == null or not client.is_spawned():
		if _frames > 1800:
			_fail("not spawned")
		return false
	var pos: Vector3 = client.get_presented()["position"]
	var at := Vector2(-pos.x, pos.z)
	match _phase:
		0:
			var kinds := {}
			var names := {}
			for v: Dictionary in client.get_vendors():
				kinds[str(v["kind"])] = int(kinds.get(str(v["kind"]), 0)) + 1
				names[str(v["name"])] = true
			print("Dathomir's services: ", kinds)
			if int(kinds.get("trainer", 0)) < 12:
				_fail("expected Dathomir's 12 trainers, got %d" % int(kinds.get("trainer", 0)))
			for wanted: String in ["Science Outpost Bank", "Science Outpost Medical Center", "Science Outpost Junk Dealer",
					"Science Outpost Gear & Supplies", "Science Outpost Starport", "Science Outpost Shuttleport East",
					"Science Outpost Shuttleport North", "Trade Outpost Bank", "Trade Outpost Medical Center",
					"Trade Outpost Junk Dealer", "Trade Outpost Gear & Supplies", "Trade Outpost Starport",
					"Trade Outpost Shuttleport"]:
				if not names.has(wanted):
					_fail("no " + wanted)
			var givers := {}
			for n: Dictionary in client.get_quest_npcs():
				if bool(n["giver"]):
					givers[str(n["name"])] = true
			print("Quest givers: ", givers.keys())
			for wanted: String in ["Administrator Tevar Quill", "Ranger Tavi Orn", "Sergeant Halvik Roe",
					"Prospector Mella Voss", "Trader Orrin Bask", "Scout Ria Calder", "Warden Daro Kesk"]:
				if not givers.has(wanted):
					_fail("no quest giver " + wanted)
			_next_phase()
		1:
			# Trader Marend in the house's office, on its floor.
			var found := false
			for remote: Dictionary in client.get_remotes():
				var p: Vector3 = remote["position"]
				if Vector2(-p.x, p.z).distance_to(MAREND) < 1.0:
					found = true
					print("Trader Marend: at (%.2f, %.2f, %.2f)" % [-p.x, p.y, p.z])
					if absf(p.y - MAREND_FLOOR) > 0.3:
						_fail("expected him on the office's floor (%.1f m), at %.2f" % [MAREND_FLOOR, p.y])
			if found:
				_next_phase()
			elif _frames - _phase_frame > 600:
				_fail("Trader Marend isn't there")
		2:
			if _frames - _phase_frame == 30:
				_screenshot()
			if _frames - _phase_frame > 40:
				print("Stage 13 Dathomir: OK")
				_quit(0)
	return false


func _next_phase() -> void:
	_phase += 1
	_phase_frame = _frames


func _screenshot() -> void:
	if _shots == "" or DisplayServer.get_name().to_lower().contains("headless"):
		return
	_shot += 1
	var path := "%s%02d.png" % [_shots, _shot]
	root.get_viewport().get_texture().get_image().save_png(path)
	print("wrote ", path)


func _fail(why: String) -> void:
	printerr("FAIL: ", why)
	_quit(1)


func _quit(code: int) -> void:
	if _session != null:
		_session.leave_session()
		_session.queue_free()
		_session = null
	_remove_db()
	quit(code)


func _remove_db() -> void:
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(_db + suffix):
			DirAccess.remove_absolute(_db + suffix)
