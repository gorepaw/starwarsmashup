# Headless verification for Stage 4 Milestone A & Hoth team tags fix:
# 1. Hoth (hot1) BattleUI.add_posts() initializes TeamTags instead of falling back to text labels
# 2. SwmCharacterStore: load_catalog, ensure_starter_kit, list_items, list_loadouts, buy_item, add_rewards, persistence
extends SceneTree

const BattleUI = preload("res://scenes/battle_ui.gd")
const Bf2Audio = preload("res://audio/bf2_audio.gd")


func _initialize() -> void:
	# 1. Hoth TeamTags check
	var world_root := Node3D.new()
	root.add_child(world_root)
	var layer := CanvasLayer.new()
	root.add_child(layer)
	var bui := BattleUI.new()
	root.add_child(bui)
	bui.setup(layer, world_root, null, "res://imported_assets/hud")
	var meta: Variant = JSON.parse_string(FileAccess.get_file_as_string("res://imported_assets/maps/hot1/hot1.json"))
	assert(meta is Dictionary, "missing hot1.json")
	var map_meta: Dictionary = (meta as Dictionary).duplicate()
	map_meta["dir"] = "imported_assets/maps/hot1"
	map_meta["bake"] = "imported_assets/maps/hot1/hot1"
	var bf := {
		"name": "Hoth",
		"posts": [
			{"name": "CP3", "position": Vector3(213.0, -4.7, 192.0), "radius": 10.0},
			{"name": "CP4", "position": Vector3(326.0, -0.8, -52.2), "radius": 10.0},
		],
	}
	bui.set_world_context("imported_assets/maps/hot1", map_meta, bf)
	bui.add_posts()
	assert(bui._team_tags != null and bui._team_tags.available(), "Hoth TeamTags failed to initialize")
	bui.clear_posts()
	print("Hoth TeamTags: OK")

	# 2. Catalog, starter kit, economy, and SQLite v3 persistence
	var db_path := ProjectSettings.globalize_path("res://stage4_test_temp.sqlite")
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(db_path + suffix):
			DirAccess.remove_absolute(db_path + suffix)

	var store: SwmCharacterStore = ClassDB.instantiate("SwmCharacterStore")
	assert(store.open(db_path) == OK)
	var cat_err: int = store.load_catalog(
			ProjectSettings.globalize_path("res://items/swg_catalog.ini"),
			ProjectSettings.globalize_path("res://imported_assets/items"))
	assert(cat_err == OK, "load_catalog failed: " + store.get_last_error())
	var catalog: Array = store.get_catalog()
	assert(catalog.size() >= 40, "expected >= 40 catalog items, got %d" % catalog.size())

	var ch: Dictionary = store.create_character("LukeTest", "human", "male", "{}", "Mos Eisley", Vector3.ZERO, 0.0)
	assert(not ch.is_empty())
	var cid := int(ch["id"])
	assert(store.ensure_starter_kit(cid) == OK)
	var items: Array = store.list_items(cid)
	assert(items.size() >= 7, "expected starter kit items, got %d" % items.size())
	var loadouts: Array = store.list_loadouts(cid)
	assert(loadouts.size() == 1 and int(loadouts[0]["primary_id"]) > 0)

	# Earn FP & buy DLT-20A
	var rewarded: Dictionary = store.add_rewards(cid, 250, 200, 300)
	assert(int(rewarded["faction_points"]) == 300 and int(rewarded["credits"]) == 800)
	var bought: Dictionary = store.buy_item(cid, "wp_rifle_dlt20a", true)
	assert(not bought.is_empty() and str(bought["def_id"]) == "wp_rifle_dlt20a")
	store.close()

	var store2: SwmCharacterStore = ClassDB.instantiate("SwmCharacterStore")
	assert(store2.open(db_path) == OK)
	var chars2: Array = store2.list_characters()
	assert(chars2.size() == 1 and int(chars2[0]["faction_points"]) == 150)
	assert(store2.list_items(cid).size() == items.size() + 1)
	store2.close()
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(db_path + suffix):
			DirAccess.remove_absolute(db_path + suffix)
	print("Catalog & SQLite v3: OK")
	print("stage4_test: all passed")
	quit(0)
