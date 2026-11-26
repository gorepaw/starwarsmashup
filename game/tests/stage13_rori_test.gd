# Stage 13 check: Rori's people, services and shuttles. A hosted session on
# Rori (Narmle's hub and its towns Restuss and the Rebel Outpost) with a new
# character on Narmle's shuttleport:
#   - every town's trainers, banks, clinics, vendors and travel points are on
#     offer, and the quest givers of rori_quests.ini are there;
#   - Narmle's junk dealer stands in his commerce guild room, on its floor
#     1.1 m up, not on the terrain under the hall;
#   - tickets take the traveller to Restuss's shuttleport and on to the Rebel
#     Outpost's landing pad (the fare taken each time).
# Screenshots go to the path in SWM_SHOTS (if set), as <path>NN.png.
#
#   <godot> --path game --script res://tests/stage13_rori_test.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

const NARMLE_SHUTTLEPORT := Vector2(-5241.27, -2147.49)
const RESTUSS_SHUTTLEPORT := Vector2(5213.03, 5810.53)
const OUTPOST_PAD := Vector2(3671.56, -6421.20)
const JUNK_DEALER := Vector2(-5189.00, -2326.85)  # in the commerce guild's meeting room e
const JUNK_DEALER_FLOOR := 81.1                    # the hall's 80 m, and the room's 1.1

var _session: GameSession
var _db := ""
var _frames := 0
var _phase := 0
var _phase_frame := 0
var _credits := 0
var _shots := ""
var _shot := 0


func _initialize() -> void:
	_shots = OS.get_environment("SWM_SHOTS")
	_db = ProjectSettings.globalize_path("res://stage13_rori_temp.sqlite")
	_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/rori",
		"host": 0,
		"db": _db,
		"hub": "hubs/narmle.ini",
		"name": "Stage13Marsher",
		"species": "human",
		"gender": "male",
		"appearance": "{}",
		"solo": true,
		"spawn": NARMLE_SHUTTLEPORT,
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
			if at.distance_to(NARMLE_SHUTTLEPORT) > 20.0:
				_fail("expected a spawn on Narmle's shuttleport, got (%.1f, %.1f)" % [at.x, at.y])
			var kinds := {}
			var names := {}
			for v: Dictionary in client.get_vendors():
				kinds[str(v["kind"])] = int(kinds.get(str(v["kind"]), 0)) + 1
				names[str(v["name"])] = true
			print("Rori's services: ", kinds)
			if int(kinds.get("trainer", 0)) < 24:
				_fail("expected Rori's 24 trainers, got %d" % int(kinds.get("trainer", 0)))
			if int(kinds.get("travel", 0)) < 3:
				_fail("expected 3 travel points, got %d" % int(kinds.get("travel", 0)))
			for wanted: String in ["Narmle Bank", "Narmle Medical Center", "Narmle Garage", "Narmle Junk Dealer",
					"Narmle Gear & Supplies", "Narmle Shuttleport", "Narmle Starport", "Restuss Bank",
					"Restuss Medical Center", "Restuss Junk Dealer", "Restuss Shuttleport", "Restuss Starport",
					"Rebel Outpost Landing Pad", "Rebel Outpost Starport"]:
				if not names.has(wanted):
					_fail("no " + wanted)
			var givers := {}
			for n: Dictionary in client.get_quest_npcs():
				if bool(n["giver"]):
					givers[str(n["name"])] = true
			print("Quest givers: ", givers.keys())
			for wanted: String in ["Dispatcher Orra Vell", "Warden Teb Marlo", "Hunter Kesk Dorran", "Marshal Ila Brandt",
					"Trader Nim Palo", "Lieutenant Cael Doran"]:
				if not givers.has(wanted):
					_fail("no quest giver " + wanted)
			_credits = int(client.get_player_status().get("credits", 0))
			_next_phase()
		1:
			# The junk dealer in his room, on its floor.
			var found := false
			for remote: Dictionary in client.get_remotes():
				var p: Vector3 = remote["position"]
				if Vector2(-p.x, p.z).distance_to(JUNK_DEALER) < 1.0:
					found = true
					print("Narmle Junk Dealer: at (%.2f, %.2f, %.2f)" % [-p.x, p.y, p.z])
					if absf(p.y - JUNK_DEALER_FLOOR) > 0.3:
						_fail("expected him on the meeting room's floor (%.1f m), at %.2f" % [JUNK_DEALER_FLOOR, p.y])
			if found:
				_next_phase()
			elif _frames - _phase_frame > 600:
				_fail("the junk dealer isn't there")
		2:
			if _frames - _phase_frame == 90:
				_screenshot()
			if _frames - _phase_frame > 100:
				client.service(7, "Restuss Shuttleport", 0, 0)
				_next_phase()
		3:
			if at.distance_to(RESTUSS_SHUTTLEPORT) < 15.0:
				var credits := int(client.get_player_status().get("credits", 0))
				if credits >= _credits:
					_fail("expected a fare taken: %d -> %d" % [_credits, credits])
				print("Restuss: arrived at (%.1f, %.1f, %.1f), %d Cr; town '%s'" % [at.x, pos.y, at.y, credits, _session._map_title()])
				_credits = credits
				_next_phase()
			elif _frames - _phase_frame > 400:
				_fail("no arrival at Restuss; at (%.1f, %.1f)" % [at.x, at.y])
		4:
			if _frames - _phase_frame == 200:
				_screenshot()
			if _frames - _phase_frame > 220:
				client.service(7, "Rebel Outpost Landing Pad", 0, 0)
				_next_phase()
		5:
			if at.distance_to(OUTPOST_PAD) < 15.0:
				var credits := int(client.get_player_status().get("credits", 0))
				if credits >= _credits:
					_fail("expected a fare taken: %d -> %d" % [_credits, credits])
				print("Rebel Outpost: arrived at (%.1f, %.1f, %.1f), %d Cr; town '%s'" % [at.x, pos.y, at.y, credits, _session._map_title()])
				_next_phase()
			elif _frames - _phase_frame > 400:
				_fail("no arrival at the Rebel Outpost; at (%.1f, %.1f)" % [at.x, at.y])
		6:
			if _frames - _phase_frame == 200:
				_screenshot()
			if _frames - _phase_frame > 220:
				print("Stage 13 Rori: OK")
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
