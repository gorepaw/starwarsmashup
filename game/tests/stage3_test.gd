# Headless verification for Stage 3:
# 1. SwmCharacterStore: 10 species + slot rules, create/list/reopen/delete
# 2. Exported Wookiee & Ithorian glTFs: blend shapes, 19 retargeted BF2 clips, SwgColors.apply_custom
# 3. Main Menu scene + SettingsScreen + LoadCharacterScreen + CharacterCreatorScreen instantiation
extends SceneTree


func _init() -> void:
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

	print("stage3_test: all passed")
	quit(0)
