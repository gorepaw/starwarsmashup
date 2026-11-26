# Stage 13 check: Endor's people and services. A hosted session on Endor
# (the Research Outpost's hub and its towns: the Smuggler Outpost and four Ewok
# villages) with a new character outside the east storage shed:
#   - the trainers, vendors, banks, clinics and travel points of both outposts
#     are on offer, and every quest giver of endor_quests.ini is in the world;
#   - the shed's combat trainer stands in his room, on its floor 0.1 m up
#     (Height), not on the terrain under the shed.
# Screenshots go to the path in SWM_SHOTS (if set), as <path>NN.png.
#
#   <godot> --path game --script res://tests/stage13_endor_test.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

const OUTSIDE := Vector2(3256.0, -3490.0)   # Galaxies x z: before the east shed's door
const HARROW := Vector2(3259.90, -3501.90)  # in room 1 of that shed
const HARROW_FLOOR := 24.1                  # the pad's 24 m, and the room's 0.1

var _session: GameSession
var _db := ""
var _frames := 0
var _phase := 0
var _phase_frame := 0
var _shots := ""
var _shot := 0


func _initialize() -> void:
	_shots = OS.get_environment("SWM_SHOTS")
	_db = ProjectSettings.globalize_path("res://stage13_endor_temp.sqlite")
	_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/endor",
		"host": 0,
		"db": _db,
		"hub": "hubs/endor_research_outpost.ini",
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
	match _phase:
		0:
			var kinds := {}
			var names := {}
			for v: Dictionary in client.get_vendors():
				kinds[str(v["kind"])] = int(kinds.get(str(v["kind"]), 0)) + 1
				names[str(v["name"])] = true
			print("Endor's services: ", kinds)
			if int(kinds.get("trainer", 0)) < 16:
				_fail("expected Endor's 16 trainers, got %d" % int(kinds.get("trainer", 0)))
			for wanted: String in ["Research Outpost Bank", "Research Outpost Junk Dealer", "Research Outpost Gear & Supplies",
					"Research Outpost Shuttle Pad", "Research Outpost Starport", "Smuggler Outpost Bank",
					"Smuggler Outpost Junk Dealer", "Smuggler Outpost Shuttle Landing", "Smuggler Outpost Starport"]:
				if not names.has(wanted):
					_fail("no " + wanted)
			var givers := {}
			for n: Dictionary in client.get_quest_npcs():
				if bool(n["giver"]):
					givers[str(n["name"])] = true
			print("Quest givers: ", givers.keys())
			for wanted: String in ["Quartermaster Dessa Rook", "Scout Ilen Varga", "Fixer Jorrun Vask", "Elder Mitta",
					"Hunter Rokka", "Fisher Teebu", "Net-Mender Wippa"]:
				if not givers.has(wanted):
					_fail("no quest giver " + wanted)
			_next_phase()
		1:
			# Sergeant Harrow in his room, on its floor.
			var found := false
			for remote: Dictionary in client.get_remotes():
				var p: Vector3 = remote["position"]
				if Vector2(-p.x, p.z).distance_to(HARROW) < 1.0:
					found = true
					print("Sergeant Harrow: at (%.2f, %.2f, %.2f)" % [-p.x, p.y, p.z])
					if absf(p.y - HARROW_FLOOR) > 0.3:
						_fail("expected him on the room's floor (%.1f m), at %.2f" % [HARROW_FLOOR, p.y])
			if found:
				_next_phase()
			elif _frames - _phase_frame > 600:
				_fail("Sergeant Harrow isn't there")
		2:
			if _frames - _phase_frame == 30:
				_screenshot()
			if _frames - _phase_frame > 40:
				print("Stage 13 Endor: OK")
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
