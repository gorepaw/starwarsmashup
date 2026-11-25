# Stage 13 check: Naboo's water. A hosted session on Naboo (Theed's hub)
# with a new character on Lake Paonga's shore south-east of Moenia, walking
# west into it: the lake (its terrain's water table at 3 m) is drawn, and
# the character swims in it, its feet core::World::swim_depth under the
# surface, slower than it walks, rather than walking the riverbed.
# Screenshots go to the path in SWM_SHOTS (if set), as <path>NN.png.
#
#   <godot> --path game --script res://tests/stage13_water_test.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

const SHORE := Vector2(5362, -5520)  # Galaxies x z: dry, a gentle slope down to the west
const LAKE := Vector2(5300, -5520)   # 3 m and more of water over the bed
const SURFACE := 3.0
const SWIM_DEPTH := 1.3  # core::World::swim_depth

var _session: GameSession
var _db := ""
var _frames := 0
var _phase := 0
var _phase_frame := 0
var _shots := ""
var _shot := 0
var _swum := 0  # frames swimming in a row


func _initialize() -> void:
	_shots = OS.get_environment("SWM_SHOTS")
	_db = ProjectSettings.globalize_path("res://stage13_water_temp.sqlite")
	_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/naboo",
		"host": 0,
		"db": _db,
		"hub": "hubs/theed.ini",
		"name": "Stage13Swimmer",
		"species": "human",
		"gender": "female",
		"appearance": "{}",
		"solo": true,
		"spawn": SHORE,
		"bot": "route",
		"third_person": true,
		# Game space (x = -Galaxies x).
		"route": [Vector2(-LAKE.x, LAKE.y), Vector2(-LAKE.x + 20, LAKE.y)],
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
	var state: Dictionary = client.get_presented()
	var pos: Vector3 = state["position"]
	var at := Vector2(-pos.x, pos.z)
	match _phase:
		0:
			if _session._world == null or _session._world.find_child("Water_*", false, false) == null:
				if _frames - _phase_frame > 600:
					_fail("no water drawn")
				return false
			var tables := _session._world.find_children("Water_*", "MeshInstance3D", false, false).size()
			print("Lake Paonga's shore: at (%.1f, %.2f, %.1f); %d water tables drawn" % [at.x, pos.y, at.y, tables])
			if pos.y < SURFACE or not is_nan(client.get_swim_floor(pos.x, pos.z)):
				_fail("expected to start on dry land")
			_next_phase()
		1:
			var floor_y: float = client.get_swim_floor(pos.x, pos.z)
			var swimming := not is_nan(floor_y) and absf(pos.y - floor_y) < 0.05
			_swum = _swum + 1 if swimming else 0
			if _swum > 20:  # a moment in: up to swimming speed (down from running)
				var speed := Vector2(state["velocity"].x, state["velocity"].z).length()
				print("Swimming: at (%.1f, %.2f, %.1f), feet %.2f m under the surface, %.2f m/s" % [
						at.x, pos.y, at.y, SURFACE - pos.y, speed])
				if absf(pos.y - (SURFACE - SWIM_DEPTH)) > 0.05:
					_fail("expected the feet %.1f m under the surface" % SWIM_DEPTH)
				if speed > 4.0:
					_fail("expected swimming slower than running (%.2f m/s)" % speed)
				_next_phase()
			elif _frames - _phase_frame > 1200:
				_fail("never swam; at (%.1f, %.2f, %.1f), swim floor %s" % [at.x, pos.y, at.y, str(floor_y)])
		2:
			if _frames - _phase_frame == 30 or _frames - _phase_frame == 90:
				_screenshot()
			if _frames - _phase_frame > 100:
				print("Stage 13 water: OK")
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
