# Stage 13 check: starships between planets.
# A hosted session (the hub with game/hubs/galaxy.ini: Tatooine, and Naboo on
# a world server of its own) with a new character (500 Cr) at the Mos Eisley
# Starport's travel terminal: passage to Theed (500 Cr, as Galaxies charged).
# The client follows the transfer to Naboo, loads its world, and the
# character stands at Theed's spaceport.
# Screenshots go to the path in SWM_SHOTS (if set), as <path>NN.png.
#
#   <godot> --path game --script res://tests/stage13_planets_test.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

const MOS_EISLEY_STARPORT := Vector2(3570.54, -4839.95)
const THEED_SPACEPORT := Vector2(-4873.09, 4210.13)

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
	_db = ProjectSettings.globalize_path("res://stage13_planets_temp.sqlite")
	_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/tatooine",
		"host": 0,
		"db": _db,
		"hub": "hubs/mos_eisley.ini",
		"galaxy": "hubs/galaxy.ini",
		"name": "Stage13Starfarer",
		"species": "human",
		"gender": "male",
		"appearance": "{}",
		"solo": true,
		"spawn": MOS_EISLEY_STARPORT,
	}
	_session = packed.instantiate() as GameSession
	root.add_child(_session)


func _process(_delta: float) -> bool:
	if _session == null:
		return false
	_frames += 1
	var client: Object = _session.get_client()
	if client == null or not client.is_spawned():
		if _frames - _phase_frame > 900:
			_fail("not spawned in phase %d" % _phase)
		return false
	var pos: Vector3 = client.get_presented()["position"]
	var at := Vector2(-pos.x, pos.z)
	match _phase:
		0:
			var offworld := 0
			for v: Dictionary in client.get_vendors():
				offworld += 1 if str(v["kind"]) == "travel_offworld" else 0
			if offworld < 4:
				_fail("expected Naboo's starports on offer (4), got %d" % offworld)
			_credits = int(client.get_player_status().get("credits", 0))
			print("Mos Eisley Starport: at (%.1f, %.1f, %.1f), %d Cr, %d of Naboo's starports on offer" % [
					at.x, pos.y, at.y, _credits, offworld])
			if at.distance_to(MOS_EISLEY_STARPORT) > 3.0:
				_fail("expected a spawn at the starport's terminal")
			_screenshot()
			client.service(7, "Theed Spaceport", 0, 0)
			_next_phase()
		1:
			if str(client.get_bake()).get_file() == "naboo" and at.distance_to(THEED_SPACEPORT) < 8.0:
				var credits := int(client.get_player_status().get("credits", 0))
				if credits != _credits - 500:
					_fail("expected the 500 Cr fare: %d -> %d" % [_credits, credits])
				print("Naboo: at (%.1f, %.1f, %.1f), %d Cr, world %s, title '%s'" % [
						at.x, pos.y, at.y, credits, client.get_bake(), _session._map_title()])
				_next_phase()
			elif _frames - _phase_frame > 1800:
				_fail("no arrival on Naboo; bake %s, at (%.1f, %.1f)" % [client.get_bake(), at.x, at.y])
		2:
			if _frames - _phase_frame == 240:
				_screenshot()
			if _frames - _phase_frame > 260:
				print("Stage 13 planets: OK")
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
