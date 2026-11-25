# Headless verification for Stage 4 Milestone A & Hoth team tags fix:
# 1. Hoth (hot1) BattleUI.add_posts() initializes TeamTags instead of falling back to text labels
# 2. SwmCharacterStore: load_catalog, ensure_starter_kit, list_items, list_loadouts, buy_item, add_rewards, persistence
extends SceneTree

const BattleUI = preload("res://scenes/battle_ui.gd")
const Bf2Audio = preload("res://audio/bf2_audio.gd")
const Bf2Art = preload("res://ui/bf2_art.gd")
const InventoryScreen = preload("res://ui/inventory_screen.gd")
const QuartermasterScreen = preload("res://ui/quartermaster_screen.gd")
const WarTerminalScreen = preload("res://ui/war_terminal_screen.gd")
const HubHud = preload("res://ui/hub_hud.gd")
const GameSession = preload("res://scenes/game_session.gd")

var _session: GameSession
var _e2e_db := ""
var _frames := 0
var _step := 0


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
	bui.free()
	layer.free()
	world_root.free()
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

	# Earn FP & Credits; buy cosmetic cloth_jacket_rebel with FP and wp_rifle_dlt20a with Credits
	var rewarded: Dictionary = store.add_rewards(cid, 250, 200, 300)
	assert(int(rewarded["faction_points"]) == 300 and int(rewarded["credits"]) == 800)
	var bought_fp: Dictionary = store.buy_item(cid, "cloth_jacket_rebel", true)
	assert(not bought_fp.is_empty() and str(bought_fp["def_id"]) == "cloth_jacket_rebel")
	var bought_cr: Dictionary = store.buy_item(cid, "wp_rifle_dlt20a", false)
	assert(not bought_cr.is_empty() and str(bought_cr["def_id"]) == "wp_rifle_dlt20a")
	store.close()

	var store2: SwmCharacterStore = ClassDB.instantiate("SwmCharacterStore")
	assert(store2.open(db_path) == OK)
	var chars2: Array = store2.list_characters()
	assert(chars2.size() == 1 and int(chars2[0]["faction_points"]) == 250 and int(chars2[0]["credits"]) == 300)
	assert(store2.list_items(cid).size() == items.size() + 2)
	var computed: Dictionary = store.compute_loadout_stats("wookiee", loadouts[0], items, true)
	assert(float(computed.get("max_health", 0.0)) > 450.0, "expected Wookiee trait health bonus")
	store2.close()
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(db_path + suffix):
			DirAccess.remove_absolute(db_path + suffix)
	print("Catalog & SQLite v3: OK")

	# 3. UI screen instantiation & paint smoke check
	var art := Bf2Art.load_from("res://imported_assets/hud")
	assert(art != null, "missing hud art")
	var inv_scr := InventoryScreen.new(art)
	inv_scr.catalog = catalog
	inv_scr.store = store
	inv_scr.inventory = items
	inv_scr.loadouts = loadouts
	inv_scr.open_screen()
	inv_scr.close_screen()
	inv_scr.free()
	var qm_scr := QuartermasterScreen.new(art)
	qm_scr.catalog = catalog
	qm_scr.store = store
	qm_scr.free()
	var wt_scr := WarTerminalScreen.new(art)
	wt_scr.free()
	var hhud := HubHud.new(art)
	hhud.free()
	print("Stage 4 UI screens: OK")

	# 4. Live GameSession Mos Eisley Stage 4 e2e
	_e2e_db = ProjectSettings.globalize_path("res://stage4_e2e_temp.sqlite")
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(_e2e_db + suffix):
			DirAccess.remove_absolute(_e2e_db + suffix)
	var gs_packed := load("res://scenes/game_session.tscn") as PackedScene
	assert(gs_packed != null)
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/tatooine" if FileAccess.file_exists("res://imported_assets/terrain/tatooine.json") else "imported_assets/terrain/tatooine_mos_eisley",
		"host": 0,
		"db": _e2e_db,
		"hub": "hubs/mos_eisley.ini",
		"name": "Stage4Hero",
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
			printerr("FAIL: Stage4Hero did not spawn (status=%s refusal=%s)" % [
					client.get_status() if client != null else "null",
					client.get_refusal() if client != null else ""])
			_cleanup_fail()
			return true
		return false

	if _step == 0:
		if client.get_inventory().size() < 7:
			if _frames > 240:
				printerr("FAIL: inventory did not sync")
				_cleanup_fail()
				return true
			return false
		assert(client.get_quartermasters().size() == 4, "expected 4 quartermasters in Mos Eisley")
		assert(client.get_factions().size() == 4, "expected 4 factions in Mos Eisley")
		assert(client.get_targets().size() == 4, "expected 4 range targets in Mos Eisley")
		assert(client.get_character_loadouts().size() == 1, "expected starter loadout")
		# Save Loadout 4 (5th slot)
		var inv: Array = client.get_inventory()
		var lo1 := {
			"slot_index": 4,
			"name": "Recon",
			"primary_id": int(inv[0]["id"]),
			"sidearm_id": int(inv[1]["id"]),
			"secondary_id": int(inv[2]["id"]),
			"gear_ids": {},
			"appearance_ids": {},
		}
		client.save_loadout(4, lo1)
		_step = 1
		return false

	if _step == 1:
		if client.get_character_loadouts().size() < 2:
			if _frames > 360:
				printerr("FAIL: loadout 1 save did not sync")
				_cleanup_fail()
				return true
			return false
		print("Stage 4 GameSession e2e: OK")
		_session.leave_session()
		_session.queue_free()
		_session = null
		for suffix in ["", "-wal", "-shm"]:
			if FileAccess.file_exists(_e2e_db + suffix):
				DirAccess.remove_absolute(_e2e_db + suffix)
		print("stage4_test: all passed")
		quit(0)
		return true
	return false


func _cleanup_fail() -> void:
	if _session != null:
		_session.leave_session()
		_session.queue_free()
		_session = null
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(_e2e_db + suffix):
			DirAccess.remove_absolute(_e2e_db + suffix)
	quit(1)
