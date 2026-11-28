# Stage 13 check: Talus's people and services. A hosted session on Talus
# (Dearic's hub and its towns, Nashal and the Imperial Outpost) with a new
# character on Dearic's shuttleport:
#   - every town's trainers, banks, clinics, garages and vendors are on offer
#     (talus_quests.ini's givers too), and the travel points;
#   - the guild's rifleman trainer stands in his meeting room, on its floor
#     1.1 m up (Height), not on the terrain under the hall;
#   - tickets take the traveller to Nashal's shuttleport and on to the Imperial
#     Outpost's landing pad, a fare each.
# Screenshots go to the path in SWBFO_SHOTS (if set), as <path>NN.png.
#
#   <godot> --path game --script res://tests/stage13_talus_test.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

const DEARIC_SHUTTLEPORT := Vector2(699.30, -3061.42)  # Galaxies x z
# The first player in stands on the spawn point (GameServer::join).
const SPAWN := DEARIC_SHUTTLEPORT
const VERRIN := Vector2(528.00, -2902.10)       # in meeting room a
const VERRIN_FLOOR := 7.1                       # the hall's 6 m, and the room's 1.1
const NASHAL_SHUTTLEPORT := Vector2(4314.58, 5431.24)
const OUTPOST_PAD := Vector2(-2211.76, 2301.51)

var _session: GameSession
var _db := ""
var _frames := 0
var _phase := 0
var _phase_frame := 0
var _credits := 0
var _shots := ""
var _shot := 0


func _initialize() -> void:
	_shots = OS.get_environment("SWBFO_SHOTS")
	_db = ProjectSettings.globalize_path("res://stage13_talus_temp.sqlite")
	_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/talus",
		"host": 0,
		"db": _db,
		"hub": "hubs/dearic.ini",
		"name": "Stage13Trainee",
		"species": "human",
		"gender": "male",
		"appearance": "{}",
		"solo": true,
		"spawn": SPAWN,
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
			if at.distance_to(DEARIC_SHUTTLEPORT) > 6.0:
				_fail("expected a spawn on Dearic's shuttleport, got (%.1f, %.1f): see SPAWN" % [at.x, at.y])
			var kinds := {}
			var names := {}
			for v: Dictionary in client.get_vendors():
				kinds[str(v["kind"])] = int(kinds.get(str(v["kind"]), 0)) + 1
				names[str(v["name"])] = true
			print("Talus's services: ", kinds)
			if int(kinds.get("trainer", 0)) < 23:
				_fail("expected Talus's 23 trainers, got %d" % int(kinds.get("trainer", 0)))
			if int(kinds.get("travel", 0)) < 3:
				_fail("expected 3 travel points, got %d" % int(kinds.get("travel", 0)))
			for wanted: String in ["Dearic Bank", "Dearic Medical Center", "Dearic Garage", "Dearic Junk Dealer",
					"Dearic Gear & Supplies", "Dearic Shuttleport", "Dearic Starport", "Nashal Bank",
					"Nashal Gear & Supplies", "Nashal Junk Dealer", "Nashal Shuttleport", "Nashal Starport",
					"Imperial Outpost Bank", "Imperial Outpost Medical Center", "Imperial Outpost Landing Pad",
					"Imperial Outpost Starport"]:
				if not names.has(wanted):
					_fail("no " + wanted)
			var givers := {}
			for n: Dictionary in client.get_quest_npcs():
				if bool(n["giver"]):
					givers[str(n["name"])] = true
			print("Quest givers: ", givers.keys())
			for wanted: String in ["Marshal Teran Vale", "Deputy Ilene Marsk", "Mira Tolland", "Rennick Oss",
					"Reeve Calder Voss", "Trader Hennet Brisk", "Lieutenant Sorrel Daan"]:
				if not givers.has(wanted):
					_fail("no quest giver " + wanted)
			_credits = int(client.get_player_status().get("credits", 0))
			_next_phase()
		1:
			# Sergeant Verrin in his room, on its floor.
			var found := false
			for remote: Dictionary in client.get_remotes():
				var p: Vector3 = remote["position"]
				if Vector2(-p.x, p.z).distance_to(VERRIN) < 1.0:
					found = true
					print("Sergeant Verrin: at (%.2f, %.2f, %.2f)" % [-p.x, p.y, p.z])
					if absf(p.y - VERRIN_FLOOR) > 0.3:
						_fail("expected him on the meeting room's floor (%.1f m), at %.2f" % [VERRIN_FLOOR, p.y])
			if found:
				_next_phase()
			elif _frames - _phase_frame > 600:
				_fail("Sergeant Verrin isn't there")
		2:
			if _frames - _phase_frame == 60:
				_screenshot()
			if _frames - _phase_frame > 70:
				client.service(7, "Nashal Shuttleport", 0, 0)
				_next_phase()
		3:
			if at.distance_to(NASHAL_SHUTTLEPORT) < 15.0:
				var credits := int(client.get_player_status().get("credits", 0))
				if credits >= _credits:
					_fail("expected a fare taken: %d -> %d" % [_credits, credits])
				print("Nashal: arrived at (%.1f, %.1f, %.1f), %d Cr; town '%s'" % [at.x, pos.y, at.y, credits, _session._map_title()])
				_credits = credits
				_next_phase()
			elif _frames - _phase_frame > 400:
				_fail("no arrival at Nashal; at (%.1f, %.1f)" % [at.x, at.y])
		4:
			if _frames - _phase_frame == 200:
				_screenshot()
			if _frames - _phase_frame > 220:
				client.service(7, "Imperial Outpost Landing Pad", 0, 0)
				_next_phase()
		5:
			if at.distance_to(OUTPOST_PAD) < 15.0:
				var credits := int(client.get_player_status().get("credits", 0))
				if credits >= _credits:
					_fail("expected a fare taken: %d -> %d" % [_credits, credits])
				print("Imperial Outpost: arrived at (%.1f, %.1f, %.1f), %d Cr; town '%s'" % [at.x, pos.y, at.y, credits, _session._map_title()])
				_next_phase()
			elif _frames - _phase_frame > 400:
				_fail("no arrival at the Imperial Outpost; at (%.1f, %.1f)" % [at.x, at.y])
		6:
			if _frames - _phase_frame == 200:
				_screenshot()
			if _frames - _phase_frame > 220:
				print("Stage 13 Talus: OK")
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
