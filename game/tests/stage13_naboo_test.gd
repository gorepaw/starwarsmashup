# Stage 13 check: Naboo's people and services. A hosted session on Naboo
# (Theed's hub and its towns, Lake Retreat among them) with a new character
# outside Theed's combat guild hall:
#   - every town's trainers, banks, clinics, garages and vendors are on offer
#     (naboo_quests.ini's givers too), and Lake Retreat's shuttleport;
#   - the guild's trainers stand in its meeting rooms, on their floors 1.1 m
#     up (Height), not on the terrain under the hall.
# Screenshots go to the path in SWM_SHOTS (if set), as <path>NN.png.
#
#   <godot> --path game --script res://tests/stage13_naboo_test.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

const OUTSIDE := Vector2(-5456.75, 4132.58)  # Galaxies x z: before the hall's door
const TALWYN := Vector2(-5444.75, 4104.68)   # in meeting room a
const TALWYN_FLOOR := 7.1                    # the hall's 6 m, and the room's 1.1

var _session: GameSession
var _db := ""
var _frames := 0
var _phase := 0
var _phase_frame := 0
var _shots := ""
var _shot := 0


func _initialize() -> void:
	_shots = OS.get_environment("SWM_SHOTS")
	_db = ProjectSettings.globalize_path("res://stage13_naboo_temp.sqlite")
	_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/naboo",
		"host": 0,
		"db": _db,
		"hub": "hubs/theed.ini",
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
			print("Naboo's services: ", kinds)
			if int(kinds.get("trainer", 0)) < 51:
				_fail("expected Naboo's 51 trainers, got %d" % int(kinds.get("trainer", 0)))
			for wanted: String in ["Theed Bank", "Theed Medical Center", "Theed Garage", "Theed Junk Dealer",
					"Theed Gear & Supplies", "Lake Retreat Shuttleport", "Keren Bank", "Kaadara Garage"]:
				if not names.has(wanted):
					_fail("no " + wanted)
			var givers := {}
			for n: Dictionary in client.get_quest_npcs():
				if bool(n["giver"]):
					givers[str(n["name"])] = true
			print("Quest givers: ", givers.keys())
			for wanted: String in ["Steward Aren Velis", "Officer Brenna Sol", "Captain Halden Rook", "Tilla Ferrow",
					"Old Vessa", "Harbormaster Jel Corrin", "Constable Ree Hadrel"]:
				if not givers.has(wanted):
					_fail("no quest giver " + wanted)
			_next_phase()
		1:
			# Sergeant Talwyn in his room, on its floor.
			var found := false
			for remote: Dictionary in client.get_remotes():
				var p: Vector3 = remote["position"]
				if Vector2(-p.x, p.z).distance_to(TALWYN) < 1.0:
					found = true
					print("Sergeant Talwyn: at (%.2f, %.2f, %.2f)" % [-p.x, p.y, p.z])
					if absf(p.y - TALWYN_FLOOR) > 0.3:
						_fail("expected him on the meeting room's floor (%.1f m), at %.2f" % [TALWYN_FLOOR, p.y])
			if found:
				_next_phase()
			elif _frames - _phase_frame > 600:
				_fail("Sergeant Talwyn isn't there")
		2:
			if _frames - _phase_frame == 30:
				_screenshot()
			if _frames - _phase_frame > 40:
				print("Stage 13 Naboo: OK")
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
