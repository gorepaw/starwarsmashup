# Headless verification for Stage 5 Milestone 1:
# 1. SwbfoTerrain loads full 16x16 km Tatooine (4097x4097 @ 4m, origin -8192)
# 2. SwgTerrainStreamer streams LOD chunks around focus, supports .holes, and updates LODs when moving 5 km
# 3. Live GameSession spawns new character in Mos Eisley (~3500, -4800) with _terrain_streamer active
extends SceneTree

const SwgTerrainStreamer = preload("res://rendering/swg_terrain_streamer.gd")
const GameSession = preload("res://scenes/game_session.gd")

var _session: GameSession
var _e2e_db := ""
var _frames := 0


func _initialize() -> void:
	# 1. SwbfoTerrain full planet load
	assert(ClassDB.class_exists("SwbfoTerrain"), "SwbfoTerrain class missing")
	var bake := "imported_assets/terrain/tatooine"
	assert(FileAccess.file_exists("res://" + bake + ".json"), "missing tatooine.json")
	var swbfo_t: Object = ClassDB.instantiate("SwbfoTerrain")
	assert(swbfo_t.load_bake(ProjectSettings.globalize_path("res://" + bake)) == OK)
	assert(int(swbfo_t.get_samples()) == 4097, "expected 4097 samples")
	assert(is_equal_approx(float(swbfo_t.get_spacing()), 4.0), "expected 4.0 m spacing")
	assert(swbfo_t.get_origin() == Vector2(-8192.0, -8192.0), "unexpected origin")
	var mos_h := float(swbfo_t.height_at(-3500.0, -4800.0))
	var center_h := float(swbfo_t.height_at(0.0, 0.0))
	assert(not is_nan(mos_h) and not is_nan(center_h) and absf(mos_h) < 50.0, "unexpected heights")
	print("SwbfoTerrain planet load: OK (mos_h=%.2f, center_h=%.2f)" % [mos_h, center_h])

	# 2. SwgTerrainStreamer LOD + holes + focus shift
	var meta: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://" + bake + ".json"))
	var streamer := SwgTerrainStreamer.new()
	root.add_child(streamer)
	assert(streamer.setup(bake, meta), "SwgTerrainStreamer.setup failed")
	var mat := streamer.get_material() as ShaderMaterial
	assert(mat != null and bool(mat.get_shader_parameter("has_holes")), "expected terrain holes on tatooine")

	var mos_focus := Vector3(-3500.0, 5.0, -4800.0)
	streamer.warm_around(mos_focus)
	for _i in 40:
		streamer.update_focus(mos_focus, 16)
	var counts := streamer.lod_counts()
	assert(int(counts.get(0, 0)) > 0 and int(counts.get(1, 0)) > 0 and int(counts.get(2, 0)) > 0,
			"expected multiple LOD rings around Mos Eisley, got %s" % str(counts))
	print("SwgTerrainStreamer Mos Eisley LODs: ", counts, " total=", streamer.active_chunk_count())

	# Shift focus 5 km away
	var dune_focus := Vector3(1500.0, 20.0, 2000.0)
	for _i in 80:
		streamer.update_focus(dune_focus, 16)
	var counts2 := streamer.lod_counts()
	assert(int(counts2.get(0, 0)) > 0, "expected LOD 0 chunks at new focus")
	var near_lod0 := false
	for entry: Dictionary in streamer._chunks.values():
		if int(entry["lod"]) == 0 and Vector2(dune_focus.x, dune_focus.z).distance_to(entry["center"]) < 600.0:
			near_lod0 = true
			break
	assert(near_lod0, "expected an LOD 0 chunk within 600 m of (1500, 2000)")
	print("SwgTerrainStreamer shifted LODs: ", counts2)
	streamer.queue_free()

	# 3. Live GameSession spawn check
	_e2e_db = ProjectSettings.globalize_path("res://stage5_terrain_temp.sqlite")
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(_e2e_db + suffix):
			DirAccess.remove_absolute(_e2e_db + suffix)
	var gs_packed := load("res://scenes/game_session.tscn") as PackedScene
	assert(gs_packed != null)
	GameSession.pending_config = {
		"bake": bake,
		"host": 0,
		"db": _e2e_db,
		"hub": "hubs/mos_eisley.ini",
		"name": "Stage5Tatooine",
		"species": "human",
		"gender": "male",
		"appearance": "{}",
		"solo": true,
	}
	_session = gs_packed.instantiate() as GameSession
	root.add_child(_session)


func _process(_delta: float) -> bool:
	if _session == null:
		return false
	_frames += 1
	var client: Object = _session.get_client()
	if client == null or not client.is_spawned():
		if _frames > 240:
			printerr("FAIL: Stage5Tatooine did not spawn")
			_cleanup_and_quit(1)
			return true
		return false

	var own: Dictionary = client.get_presented()
	var pos: Vector3 = own["position"]
	var swg_x := -pos.x
	var swg_z := pos.z
	assert(absf(swg_x - 3500.0) < 30.0 and absf(swg_z - (-4800.0)) < 30.0,
			"expected spawn in Mos Eisley (~3500, -4800), got (%.2f, %.2f)" % [swg_x, swg_z])
	assert(_session._terrain_streamer != null and _session._terrain_streamer.active_chunk_count() > 0,
			"expected active SwgTerrainStreamer in GameSession")
	print("Live GameSession Tatooine spawn: OK at SWG (%.2f, %.2f, %.2f), chunks=%d" % [
			swg_x, pos.y, swg_z, _session._terrain_streamer.active_chunk_count()])
	_cleanup_and_quit(0)
	return true


func _cleanup_and_quit(code: int) -> void:
	if _session != null:
		_session.leave_session()
		_session.queue_free()
	for suffix in ["", "-wal", "-shm"]:
		if _e2e_db != "" and FileAccess.file_exists(_e2e_db + suffix):
			DirAccess.remove_absolute(_e2e_db + suffix)
	quit(code)
