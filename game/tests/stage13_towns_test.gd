# Stage 13 check: Tatooine's other towns, and shuttles between them.
# 1. A hosted Mos Eisley hub session (its Town = files) with a new character
#    spawned on Anchorhead's shuttleport: the town is drawn around them, and
#    its travel points are known to the client.
# 2. A ticket to Mos Espa (ServiceRequest travel, as the travel screen sends):
#    the character arrives at Mos Espa's west shuttleport, 100 Cr poorer, with
#    the loading screen up while the world streams in.
# Screenshots of both go to the path in SWBFO_SHOTS (if set), as <path>NN.png.
#
#   <godot> --path game --script res://tests/stage13_towns_test.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

const ANCHORHEAD_SHUTTLEPORT := Vector2(47.53, -5318.91)
const MOS_ESPA_SHUTTLEPORT := Vector2(-3131.51, 2172.01)

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
	_db = ProjectSettings.globalize_path("res://stage13_towns_temp.sqlite")
	_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/tatooine",
		"host": 0,
		"db": _db,
		"hub": "hubs/mos_eisley.ini",
		"name": "Stage13Traveller",
		"species": "human",
		"gender": "female",
		"appearance": "{}",
		"solo": true,
		"spawn": ANCHORHEAD_SHUTTLEPORT,
	}
	_session = packed.instantiate() as GameSession
	root.add_child(_session)


func _process(_delta: float) -> bool:
	if _session == null:
		return false
	_frames += 1
	var client: Object = _session.get_client()
	if client == null or not client.is_spawned():
		if _frames > 600:
			_fail("the traveller did not spawn")
		return false
	var pos: Vector3 = client.get_presented()["position"]
	var at := Vector2(-pos.x, pos.z)
	match _phase:
		0:
			# On Anchorhead's shuttleport, with the planet's travel points known.
			if at.distance_to(ANCHORHEAD_SHUTTLEPORT) > 15.0:
				_fail("expected a spawn on Anchorhead's shuttleport, got (%.1f, %.1f)" % [at.x, at.y])
			var travel := 0
			for v: Dictionary in client.get_vendors():
				travel += 1 if str(v["kind"]) == "travel" else 0
			if travel < 8:
				_fail("expected the planet's travel points (8+), got %d" % travel)
			_credits_before = int(client.get_player_status().get("credits", 0))
			print("Anchorhead: spawned at (%.1f, %.1f, %.1f), %d travel points, %d Cr; town '%s'" % [
					at.x, pos.y, at.y, travel, _credits_before, _session._map_title()])
			_next_phase()
		1:
			# Let the town draw, then a picture.
			if _frames - _phase_frame == 120:
				_screenshot()
			if _frames - _phase_frame > 140:
				client.service(7, "Mos Espa Shuttleport West", 0, 0)
				_next_phase()
		2:
			if at.distance_to(MOS_ESPA_SHUTTLEPORT) < 15.0:
				var credits := int(client.get_player_status().get("credits", 0))
				if credits != _credits_before - 100:
					_fail("expected the fare (100 Cr) taken: %d -> %d" % [_credits_before, credits])
				var loading: bool = _session._loading_screen != null and _session._loading_screen.visible
				print("Mos Espa: arrived at (%.1f, %.1f, %.1f), %d Cr, loading screen %s; town '%s'" % [
						at.x, pos.y, at.y, credits, "up" if loading else "down", _session._map_title()])
				_next_phase()
			elif _frames - _phase_frame > 300:
				_fail("no arrival at Mos Espa; at (%.1f, %.1f)" % [at.x, at.y])
		3:
			if _frames - _phase_frame == 240:
				_screenshot()
			if _frames - _phase_frame > 260:
				print("Stage 13 towns: OK")
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
