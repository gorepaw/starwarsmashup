# Stage 13 check: Dantooine's people and services. A hosted session on
# Dantooine (the Imperial Outpost's hub, with the Mining and Agro Outposts as
# its Town = files), a new character spawned at the Imperial Outpost's landing
# pad:
#   - every outpost's trainers, banks, clinics, vendors and landing pads are on
#     offer, and the quest givers (dantooine_quests.ini's) stand in the outposts;
#   - the cantina's entertainer trainer stands in its main hall, on its floor
#     (Height), not on the terrain under the building;
#   - a ticket to the Mining Outpost's landing pad takes the traveller there,
#     100 Cr poorer.
# Screenshots go to the path in SWM_SHOTS (if set), as <path>NN.png.
#
#   <godot> --path game --script res://tests/stage13_dantooine_test.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

const LANDING_PAD := Vector2(-4210.7, -2350.5)   # Galaxies x z: the Imperial Outpost's starport door
const MAESTRO := Vector2(-4207.0, -2432.0)       # in the cantina's main hall
const MAESTRO_FLOOR := 3.1                       # the outpost's 3 m, and the room's 0.1
const MINING_LANDING_PAD := Vector2(-637.2, 2505.3)

var _session: GameSession
var _db := ""
var _frames := 0
var _phase := 0
var _phase_frame := 0
var _credits_before := 0
var _shots := ""
var _shot := 0


func _initialize() -> void:
	_shots = OS.get_environment("SWM_SHOTS")
	_db = ProjectSettings.globalize_path("res://stage13_dantooine_temp.sqlite")
	_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/dantooine",
		"host": 0,
		"db": _db,
		"hub": "hubs/dantooine_imperial_outpost.ini",
		"name": "Stage13Settler",
		"species": "human",
		"gender": "male",
		"appearance": "{}",
		"solo": true,
		"spawn": LANDING_PAD,
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
			if at.distance_to(LANDING_PAD) > 15.0:
				_fail("expected a spawn at the landing pad, got (%.1f, %.1f)" % [at.x, at.y])
			var kinds := {}
			var names := {}
			for v: Dictionary in client.get_vendors():
				kinds[str(v["kind"])] = int(kinds.get(str(v["kind"]), 0)) + 1
				names[str(v["name"])] = true
			print("Dantooine's services: ", kinds)
			if int(kinds.get("trainer", 0)) < 17:
				_fail("expected Dantooine's 17 trainers, got %d" % int(kinds.get("trainer", 0)))
			if int(kinds.get("travel", 0)) < 3:
				_fail("expected 3 landing pads, got %d" % int(kinds.get("travel", 0)))
			for wanted: String in ["Imperial Outpost Bank", "Mining Outpost Bank", "Agro Outpost Bank",
					"Imperial Outpost Medical Center", "Mining Outpost Medical Center", "Agro Outpost Medical Center",
					"Imperial Outpost Junk Dealer", "Imperial Outpost Gear & Supplies", "Mining Outpost Junk Dealer",
					"Agro Outpost Gear & Supplies", "Imperial Outpost Landing Pad", "Mining Outpost Landing Pad",
					"Agro Outpost Landing Pad", "Imperial Outpost Starport", "Mining Outpost Starport", "Agro Outpost Starport"]:
				if not names.has(wanted):
					_fail("no " + wanted)
			var givers := {}
			for n: Dictionary in client.get_quest_npcs():
				if bool(n["giver"]):
					givers[str(n["name"])] = true
			print("Quest givers: ", givers.keys())
			for wanted: String in ["Lieutenant Odan Vessk", "Sergeant Kell Marrow", "Foreman Dross Halvane",
					"Prospector Idris Moll", "Farmer Ysolde Brandt", "Rancher Hob Tessaly"]:
				if not givers.has(wanted):
					_fail("no quest giver " + wanted)
			_credits_before = int(client.get_player_status().get("credits", 0))
			_next_phase()
		1:
			# The entertainer trainer in the cantina's main hall, on its floor.
			var found := false
			for remote: Dictionary in client.get_remotes():
				var p: Vector3 = remote["position"]
				if Vector2(-p.x, p.z).distance_to(MAESTRO) < 1.0:
					found = true
					print("Maestro: at (%.2f, %.2f, %.2f)" % [-p.x, p.y, p.z])
					if absf(p.y - MAESTRO_FLOOR) > 0.3:
						_fail("expected him on the main hall's floor (%.1f m), at %.2f" % [MAESTRO_FLOOR, p.y])
			if found:
				_next_phase()
			elif _frames - _phase_frame > 600:
				_fail("the cantina's trainer isn't there")
		2:
			if _frames - _phase_frame == 60:
				_screenshot()
			if _frames - _phase_frame > 80:
				client.service(7, "Mining Outpost Landing Pad", 0, 0)
				_next_phase()
		3:
			if at.distance_to(MINING_LANDING_PAD) < 15.0:
				var credits := int(client.get_player_status().get("credits", 0))
				if credits != _credits_before - 100:
					_fail("expected the fare (100 Cr) taken: %d -> %d" % [_credits_before, credits])
				print("Mining Outpost: arrived at (%.1f, %.1f, %.1f), %d Cr; town '%s'" % [
						at.x, pos.y, at.y, credits, _session._map_title()])
				_next_phase()
			elif _frames - _phase_frame > 300:
				_fail("no arrival at the Mining Outpost; at (%.1f, %.1f)" % [at.x, at.y])
		4:
			if _frames - _phase_frame == 180:
				_screenshot()
			if _frames - _phase_frame > 200:
				print("Stage 13 Dantooine: OK")
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
