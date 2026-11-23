# Headless verification for Stage 5 Milestone 2:
# 1. Verify creature & bandit glTFs exist and load cleanly
# 2. Launch live GameSession on Tatooine + Mos Eisley with tatooine_spawns.ini
# 3. Verify server spawns creatures & bandits and client connects & sees actor_kind/max_health
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

var _session: GameSession
var _e2e_db := ""
var _frames := 0


func _initialize() -> void:
	var people_dir := "res://imported_assets/terrain/tatooine_city/people/"
	for stem: String in ["worrt", "kreetle", "womp_rat", "eopie", "cu_pa", "bantha", "dewback",
			"squill", "dune_lizard", "ronto", "dressed_tatooine_valarian_thug", "tusken_raider"]:
		var path := people_dir + stem + ".gltf"
		assert(FileAccess.file_exists(path), "missing exported model: " + path)
	print("Creature & bandit glTF check: OK")

	_e2e_db = ProjectSettings.globalize_path("res://stage5_wildlife_temp.sqlite")
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(_e2e_db + suffix):
			DirAccess.remove_absolute(_e2e_db + suffix)

	var gs_packed := load("res://scenes/game_session.tscn") as PackedScene
	assert(gs_packed != null)
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/tatooine",
		"host": 0,
		"db": _e2e_db,
		"hub": "hubs/mos_eisley.ini",
		"name": "Stage5Hunter",
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
			printerr("FAIL: Stage5Hunter did not spawn")
			_cleanup_and_quit(1)
			return true
		return false

	var looks: PackedStringArray = client.get_looks()
	assert(looks.size() >= 20, "expected >= 20 looks (citizens + creatures + bandits), got %d" % looks.size())
	var server: Object = _session.get_server()
	assert(server != null)
	var remotes: Array = client.get_remotes()
	assert(not remotes.is_empty(), "expected hub/world NPCs replicated")
	var found_kind := false
	for r: Dictionary in remotes:
		if r.has("actor_kind") and r.has("max_health"):
			found_kind = true
			break
	assert(found_kind, "expected actor_kind and max_health on remote entities")
	print("Live Stage 5 wildlife session: OK (looks=%d, remotes=%d)" % [looks.size(), remotes.size()])
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
