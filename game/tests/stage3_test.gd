# Headless verification for Stage 3:
# 1. SwmCharacterStore: 10 species + slot rules, create/list/reopen/delete
# 2. Exported Wookiee & Ithorian glTFs: blend shapes, 19 retargeted BF2 clips, SwgColors.apply_custom
# 3. Main Menu scene + SettingsScreen + LoadCharacterScreen + CharacterCreatorScreen instantiation
extends SceneTree

const PlayerAppearance = preload("res://rendering/player_appearance.gd")
const GameSession = preload("res://scenes/game_session.gd")

var _session: GameSession
var _e2e_db := ""
var _saved_app := '{"height":0.95,"colors":{"index_color_1":4}}'
var _frames := 0


func _initialize() -> void:
	if not ClassDB.class_exists("SwmCharacterStore"):
		printerr("FAIL: SwmCharacterStore not registered")
		quit(1)
		return

	# 1. Store & Species
	var species_list: Array = SwmCharacterStore.get_species_list()
	assert(species_list.size() == 10, "expected 10 species, got %d" % species_list.size())
	var by_id := {}
	for sp: Dictionary in species_list:
		by_id[str(sp["id"])] = sp
	assert((by_id["wookiee"]["blocked_slots"] as PackedStringArray).has("chest"))
	assert((by_id["ithorian"]["blocked_slots"] as PackedStringArray).has("boots"))
	assert((by_id["trandoshan"]["blocked_slots"] as PackedStringArray).has("gloves"))
	assert((by_id["human"]["blocked_slots"] as PackedStringArray).is_empty())

	var db_path := ProjectSettings.globalize_path("res://stage3_test_temp.sqlite")
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(db_path + suffix):
			DirAccess.remove_absolute(db_path + suffix)

	var store: SwmCharacterStore = ClassDB.instantiate("SwmCharacterStore")
	assert(store.open(db_path) == OK)
	var rec: Dictionary = store.create_character("Chewtest", "wookiee", "male",
			'{"height":0.85,"colors":{"index_color_1":2}}', "Mos Eisley", Vector3(1, 2, 3), 0.5)
	assert(not rec.is_empty() and str(rec["species"]) == "wookiee")
	var listed: Array = store.list_characters()
	assert(listed.size() == 1 and str(listed[0]["name"]) == "Chewtest")
	store.close()

	var store2: SwmCharacterStore = ClassDB.instantiate("SwmCharacterStore")
	assert(store2.open(db_path) == OK)
	var listed2: Array = store2.list_characters()
	assert(listed2.size() == 1 and str(listed2[0]["species"]) == "wookiee")
	assert(store2.delete_character(int(rec["id"])))
	assert(store2.list_characters().is_empty())
	store2.close()
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(db_path + suffix):
			DirAccess.remove_absolute(db_path + suffix)
	print("store & species: OK")

	# 2. Exported species glTFs (Wookiee & Ithorian)
	for sp_pair in [["wookiee", "male"], ["ithorian", "male"]]:
		var sg := "%s_%s" % [sp_pair[0], sp_pair[1]]
		var gltf_path := ProjectSettings.globalize_path("res://imported_assets/species/%s/%s.gltf" % [sg, sg])
		assert(FileAccess.file_exists(gltf_path), "missing " + gltf_path)
		var doc := GLTFDocument.new()
		var state := GLTFState.new()
		assert(doc.append_from_file(gltf_path, state) == OK, "failed to load " + gltf_path)
		var scene := doc.generate_scene(state) as Node3D
		assert(scene != null)
		var player := scene.find_child("AnimationPlayer", true, false) as AnimationPlayer
		assert(player != null and player.get_animation_list().size() == 19,
				"%s expected 19 clips, got %d" % [sg, player.get_animation_list().size() if player else 0])
		var total_blends := 0
		for m in scene.find_children("*", "MeshInstance3D", true, false):
			var mi := m as MeshInstance3D
			if mi.mesh != null:
				total_blends += mi.mesh.get_blend_shape_count()
		assert(total_blends > 0, "%s has no blend shapes" % sg)
		var colors := SwgColors.load_colors(gltf_path)
		SwgColors.apply_custom(scene, colors, {"index_color_1": 1})
		scene.free()
		print("species %s: OK (19 clips, %d blend shapes)" % [sg, total_blends])

	# 3. Main Menu scene instantiation
	var packed := load("res://scenes/main_menu.tscn") as PackedScene
	assert(packed != null)
	var menu: Node = packed.instantiate()
	assert(menu != null)
	root.add_child(menu)
	menu.queue_free()
	print("main_menu.tscn: OK")

	# 4. PlayerAppearance loader
	var pa_wke: Dictionary = PlayerAppearance.load_character("wookiee", "male", '{"height":1.0,"colors":{"index_color_1":2}}')
	assert(not pa_wke.is_empty() and pa_wke["species"] == "wookiee" and float(pa_wke["eye_height"]) > 2.0)
	(pa_wke["model"] as Node).free()
	var pa_ith: Dictionary = PlayerAppearance.load_character("ithorian", "female", '{"height":0.5}')
	assert(not pa_ith.is_empty() and pa_ith["species"] == "ithorian")
	(pa_ith["model"] as Node).free()
	print("PlayerAppearance: OK")

	# 5. End-to-end GameSession with Wookiee persistence & replication
	_e2e_db = ProjectSettings.globalize_path("res://stage3_e2e_temp.sqlite")
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(_e2e_db + suffix):
			DirAccess.remove_absolute(_e2e_db + suffix)
	var e2e_store: SwmCharacterStore = ClassDB.instantiate("SwmCharacterStore")
	assert(e2e_store.open(_e2e_db) == OK)
	var created: Dictionary = e2e_store.create_character("Chewtest", "wookiee", "male",
			_saved_app, "Mos Eisley", Vector3(-3480.0, 5.0, -4794.0), 0.25)
	assert(not created.is_empty())
	e2e_store.close()

	var gs_packed := load("res://scenes/game_session.tscn") as PackedScene
	assert(gs_packed != null)
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/tatooine_mos_eisley",
		"host": 0,
		"db": _e2e_db,
		"hub": "hubs/mos_eisley.ini",
		"name": "Chewtest",
		"species": "wookiee",
		"gender": "male",
		"appearance": _saved_app,
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
		if _frames >= 120:
			printerr("FAIL: Chewtest did not spawn within 120 frames")
			_session.leave_session()
			_session.queue_free()
			quit(1)
			return true
		return false

	var presented: Dictionary = client.get_presented()
	assert(str(presented.get("species", "")) == "wookiee", "expected wookiee, got %s" % presented.get("species", ""))
	assert(str(presented.get("appearance", "")) == _saved_app)
	assert(client.get_max_health() > 100.0, "expected Wookiee health bonus")
	_session.leave_session()
	_session.queue_free()
	_session = null

	var verify_store: SwmCharacterStore = ClassDB.instantiate("SwmCharacterStore")
	assert(verify_store.open(_e2e_db) == OK)
	var all_chars: Array = verify_store.list_characters()
	assert(all_chars.size() == 1)
	assert(str(all_chars[0]["species"]) == "wookiee" and str(all_chars[0]["gender"]) == "male")
	assert(str(all_chars[0]["appearance"]) == _saved_app)
	verify_store.close()
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(_e2e_db + suffix):
			DirAccess.remove_absolute(_e2e_db + suffix)
	print("GameSession e2e: OK")
	print("stage3_test: all passed")
	quit(0)
	return true
