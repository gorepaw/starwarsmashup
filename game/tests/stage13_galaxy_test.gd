# Stage 13 check: the whole galaxy (game/hubs/galaxy.ini: ten planets, each
# a hub on a world server of its own). A hosted session with a new character
# (500 Cr) at Corellia's Coronet Starport, the galaxy's crossroads:
#   - its starport sells passage to the seven planets travel.iff routes it
#     to (Tatooine, Naboo, Talus, Dantooine, Dathomir, Yavin 4, Endor), and
#     not to Lok or Rori (no route from Corellia);
#   - a ticket to Talus (300 Cr) lands the character at Dearic's starport
#     with Talus's world loaded.
# Screenshots go to the path in SWM_SHOTS (if set), as <path>NN.png.
#
#   <godot> --path game --script res://tests/stage13_galaxy_test.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

const CORONET_STARPORT := Vector2(-108.07, -4726.71)
const DEARIC_STARPORT := Vector2(307.23, -2941.02)
const ROUTES := ["Tatooine", "Naboo", "Talus", "Dantooine", "Dathomir", "Yavin 4", "Endor"]

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
	_db = ProjectSettings.globalize_path("res://stage13_galaxy_temp.sqlite")
	_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/corellia",
		"host": 0,
		"db": _db,
		"hub": "hubs/coronet.ini",
		"galaxy": "hubs/galaxy.ini",
		"name": "Stage13Voyager",
		"species": "human",
		"gender": "female",
		"appearance": "{}",
		"solo": true,
		"spawn": CORONET_STARPORT,
	}
	_session = packed.instantiate() as GameSession
	root.add_child(_session)


func _process(_delta: float) -> bool:
	if _session == null:
		return false
	_frames += 1
	var client: Object = _session.get_client()
	if client == null or not client.is_spawned():
		if _frames - _phase_frame > 1800:
			_fail("not spawned in phase %d" % _phase)
		return false
	var pos: Vector3 = client.get_presented()["position"]
	var at := Vector2(-pos.x, pos.z)
	match _phase:
		0:
			var planets := {}
			var dearic := 0
			for v: Dictionary in client.get_vendors():
				if str(v["kind"]) == "travel_offworld":
					planets[str(v["detail"]).get_slice("|", 0)] = int(str(v["detail"]).get_slice("|", 1))
					dearic += 1 if str(v["name"]) == "Dearic Starport" else 0
			print("Coronet Starport: at (%.1f, %.1f), routes %s" % [at.x, at.y, str(planets)])
			for planet: String in ROUTES:
				if not planets.has(planet):
					_fail("no route to " + planet)
			for planet: String in ["Lok", "Rori"]:
				if planets.has(planet):
					_fail("a route to %s, which travel.iff doesn't give Corellia" % planet)
			if int(planets.get("Talus", 0)) != 300 or dearic != 1:
				_fail("expected Dearic Starport at 300 Cr")
			if at.distance_to(CORONET_STARPORT) > 3.0:
				_fail("expected a spawn at the starport's terminal, %.1f m off" % at.distance_to(CORONET_STARPORT))
			_credits = int(client.get_player_status().get("credits", 0))
			client.service(7, "Dearic Starport", 0, 0)
			_next_phase()
		1:
			if str(client.get_bake()).get_file() == "talus" and at.distance_to(DEARIC_STARPORT) < 8.0:
				var credits := int(client.get_player_status().get("credits", 0))
				print("Talus: at (%.1f, %.1f, %.1f), %d Cr, world %s, title '%s'" % [
						at.x, pos.y, at.y, credits, client.get_bake(), _session._map_title()])
				if credits != _credits - 300:
					_fail("expected the 300 Cr fare: %d -> %d" % [_credits, credits])
				_next_phase()
			elif _frames - _phase_frame > 1800:
				_fail("no arrival on Talus; bake %s, at (%.1f, %.1f)" % [client.get_bake(), at.x, at.y])
		2:
			if _frames - _phase_frame == 240:
				_screenshot()
			if _frames - _phase_frame > 260:
				print("Stage 13 galaxy: OK")
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
