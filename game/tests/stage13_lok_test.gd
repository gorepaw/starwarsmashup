# Stage 13 check: Lok's people and services. A hosted session on Lok
# (Nym's Stronghold's hub and its towns) with a new character at the hub's
# spawn (the starport's landing pad):
#   - the 12 trainer groups, the bank, the clinic, the gear vendor and the
#     junk dealer, and each town's shuttleport are on offer (lok_quests.ini's
#     givers too);
#   - a trainer in Nym's stronghold's meeting room stands on his room's floor
#     (Height), not on the terrain under the building (the hotel's Artisan
#     Dredge too, if he is within the client's range).
# Screenshots go to the path in SWM_SHOTS (if set), as <path>NN.png.
#
#   <godot> --path game --script res://tests/stage13_lok_test.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

const OUTSIDE := Vector2(470.0, 4846.0)   # Galaxies x z: before the stronghold's door
const VORREK := Vector2(454.16, 4758.82)   # Galaxies x z: in the stronghold's first meeting room
const VORREK_FLOOR := 16.2                 # the stronghold's 12 m and the room's 4.2 m
const DREDGE := Vector2(419.97, 5128.81)   # in the hotel
const DREDGE_FLOOR := 13.0                 # the hotel's 12 m and the room's 1.0 m

var _session: GameSession
var _db := ""
var _frames := 0
var _phase := 0
var _phase_frame := 0
var _shots := ""
var _shot := 0


func _initialize() -> void:
	_shots = OS.get_environment("SWM_SHOTS")
	_db = ProjectSettings.globalize_path("res://stage13_lok_temp.sqlite")
	_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/lok",
		"host": 0,
		"db": _db,
		"hub": "hubs/lok_nyms_stronghold.ini",
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
			print("Spawned at (%.2f, %.2f, %.2f)" % [at.x, pos.y, at.y])
			if at.distance_to(OUTSIDE) > 25.0:
				_fail("expected the spawn before the stronghold, at (%.1f, %.1f)" % [at.x, at.y])
			var kinds := {}
			var names := {}
			for v: Dictionary in client.get_vendors():
				kinds[str(v["kind"])] = int(kinds.get(str(v["kind"]), 0)) + 1
				names[str(v["name"])] = true
			print("Lok's services: ", kinds)
			if int(kinds.get("trainer", 0)) < 12:
				_fail("expected Lok's 12 trainers, got %d" % int(kinds.get("trainer", 0)))
			for wanted: String in ["Nym's Stronghold Bank", "Nym's Stronghold Junk Dealer", "Nym's Stronghold Gear & Supplies",
					"Nym's Stronghold Shuttleport", "Imperial Outpost Shuttleport", "Scrapyard Landing Shuttleport",
					"Nym's Stronghold Starport"]:
				if not names.has(wanted):
					_fail("no " + wanted)
			var givers := {}
			for n: Dictionary in client.get_quest_npcs():
				if bool(n["giver"]):
					givers[str(n["name"])] = true
			print("Quest givers: ", givers.keys())
			for wanted: String in ["Tobin Rask", "Orrin Dace", "Brakka Voss", "Mirra Vahn", "Lieutenant Rhea Calder", "Old Mawl"]:
				if not givers.has(wanted):
					_fail("no quest giver " + wanted)
			_next_phase()
		1:
			var found := 0
			for remote: Dictionary in client.get_remotes():
				var p: Vector3 = remote["position"]
				var rp := Vector2(-p.x, p.z)
				if rp.distance_to(VORREK) < 1.0:
					found += 1
					print("Sergeant Vorrek: at (%.2f, %.2f, %.2f)" % [rp.x, p.y, rp.y])
					if absf(p.y - VORREK_FLOOR) > 0.3:
						_fail("expected him on the meeting room's floor (%.1f m), at %.2f" % [VORREK_FLOOR, p.y])
				elif rp.distance_to(DREDGE) < 1.0:
					found += 1
					print("Artisan Dredge: at (%.2f, %.2f, %.2f)" % [rp.x, p.y, rp.y])
					if absf(p.y - DREDGE_FLOOR) > 0.3:
						_fail("expected him on the hotel room's floor (%.1f m), at %.2f" % [DREDGE_FLOOR, p.y])
			if found >= 1:
				_next_phase()
			elif _frames - _phase_frame > 600:
				_fail("the trainers aren't there (%d found)" % found)
		2:
			if _frames - _phase_frame == 30:
				_screenshot()
			if _frames - _phase_frame > 40:
				print("Stage 13 Lok: OK")
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
