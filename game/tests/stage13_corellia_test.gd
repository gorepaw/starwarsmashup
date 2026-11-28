# Stage 13 check: Corellia's people and services. A hosted session on
# Corellia (Coronet's hub and its five other towns) with a new character
# at Coronet's north shuttleport:
#   - every town's trainers, banks, clinics, garages and vendors are on offer
#     (corellia_quests.ini's givers too), and the planet's shuttleports;
#   - the commerce guild's first trainer stands in his meeting room, on its
#     floor 1.1 m up (Height), not on the terrain under the hall;
#   - a ticket to Tyrena (ServiceRequest travel, as the travel screen sends):
#     the character arrives at Tyrena's east shuttleport, poorer by the fare.
# Screenshots go to the path in SWBFO_SHOTS (if set), as <path>NN.png.
#
#   <godot> --path game --script res://tests/stage13_corellia_test.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

const SHUTTLEPORT := Vector2(-44.7, -4393.8)     # Galaxies x z: Coronet's north shuttleport (its travel point: the plaza at its foot)
# The first player in stands on the spawn point (GameServer::join).
const SLOT_OFFSET := 0.0
const OKELLE := Vector2(-162.10, -4378.00)      # in the commerce guild's meeting room a
const OKELLE_FLOOR := 29.1                      # the hall's 28 m, and the room's 1.1
const TYRENA_SHUTTLEPORT := Vector2(-4999.0, -2349.0)

var _session: GameSession
var _db := ""
var _frames := 0
var _phase := 0
var _phase_frame := 0
var _credits_before := 0
var _shots := ""
var _shot := 0


func _initialize() -> void:
	_shots = OS.get_environment("SWBFO_SHOTS")
	_db = ProjectSettings.globalize_path("res://stage13_corellia_temp.sqlite")
	_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/corellia",
		"host": 0,
		"db": _db,
		"hub": "hubs/coronet.ini",
		"name": "Stage13Corellian",
		"species": "human",
		"gender": "male",
		"appearance": "{}",
		"solo": true,
		"spawn": SHUTTLEPORT + Vector2(SLOT_OFFSET, 0.0),
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
			print("Corellia's services: ", kinds)
			if int(kinds.get("trainer", 0)) < 58:
				_fail("expected Corellia's 58 trainers, got %d" % int(kinds.get("trainer", 0)))
			if int(kinds.get("travel", 0)) < 9:
				_fail("expected the planet's 9 shuttleports, got %d" % int(kinds.get("travel", 0)))
			for wanted: String in ["Coronet Bank", "Coronet Medical Center", "Coronet Garage", "Coronet Junk Dealer",
					"Coronet Gear & Supplies", "Coronet Starport", "Tyrena Bank", "Tyrena Garage", "Tyrena Starport",
					"Kor Vella Medical Center", "Kor Vella Starport", "Doaba Guerfel Bank", "Doaba Guerfel Starport",
					"Bela Vistal Junk Dealer", "Vreni Island Shuttleport", "Tyrena Shuttleport East"]:
				if not names.has(wanted):
					_fail("no " + wanted)
			var givers := {}
			for n: Dictionary in client.get_quest_npcs():
				if bool(n["giver"]):
					givers[str(n["name"])] = true
			print("Quest givers: ", givers.keys())
			for wanted: String in ["Sergeant Teva Ardane", "Captain Joren Halvick", "Orla Wen", "Detective Rivan Sole",
					"Lieutenant Aris Kell", "Hallan Ostrow", "Warden Sethsis"]:
				if not givers.has(wanted):
					_fail("no quest giver " + wanted)
			if at.distance_to(SHUTTLEPORT) > 6.0:
				_fail("spawned %.1f m from the travel point at (%.1f, %.1f): the spawn slot's offset is not SLOT_OFFSET (%.0f m)" % [at.distance_to(SHUTTLEPORT), at.x, at.y, SLOT_OFFSET])
			_credits_before = int(client.get_player_status().get("credits", 0))
			_next_phase()
		1:
			# Artisan Okelle in his room, on its floor.
			var found := false
			for remote: Dictionary in client.get_remotes():
				var p: Vector3 = remote["position"]
				if Vector2(-p.x, p.z).distance_to(OKELLE) < 1.0:
					found = true
					print("Artisan Okelle: at (%.2f, %.2f, %.2f)" % [-p.x, p.y, p.z])
					if absf(p.y - OKELLE_FLOOR) > 0.3:
						_fail("expected him on the meeting room's floor (%.1f m), at %.2f" % [OKELLE_FLOOR, p.y])
			if found:
				_next_phase()
			elif _frames - _phase_frame > 600:
				_fail("Artisan Okelle isn't there")
		2:
			if _frames - _phase_frame == 60:
				_screenshot()
			if _frames - _phase_frame > 80:
				client.service(7, "Tyrena Shuttleport East", 0, 0)
				_next_phase()
		3:
			if at.distance_to(TYRENA_SHUTTLEPORT) < 30.0:
				var credits := int(client.get_player_status().get("credits", 0))
				if credits >= _credits_before:
					_fail("expected a fare taken: %d -> %d" % [_credits_before, credits])
				print("Tyrena: arrived at (%.1f, %.1f, %.1f), %d Cr (was %d); town '%s'" % [
						at.x, pos.y, at.y, credits, _credits_before, _session._map_title()])
				_next_phase()
			elif _frames - _phase_frame > 400:
				_fail("no arrival at Tyrena; at (%.1f, %.1f)" % [at.x, at.y])
		4:
			if _frames - _phase_frame == 240:
				_screenshot()
			if _frames - _phase_frame > 260:
				print("Stage 13 Corellia: OK")
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
