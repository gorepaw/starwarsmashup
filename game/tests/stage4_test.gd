# Headless verification for Stage 4 Milestone A & Hoth team tags fix:
# 1. Hoth (hot1) BattleUI.add_posts() initializes TeamTags instead of falling back to text labels
# 2. SwmCharacterStore: load_catalog, ensure_starter_kit, list_items, list_loadouts, buy_item, add_rewards, persistence
extends SceneTree

const BattleUI = preload("res://scenes/battle_ui.gd")
const Bf2Audio = preload("res://audio/bf2_audio.gd")
const Bf2Art = preload("res://ui/bf2_art.gd")
const InventoryScreen = preload("res://ui/inventory_screen.gd")
const ItemTooltip = preload("res://ui/item_tooltip.gd")
const ItemGrade = preload("res://ui/item_grade.gd")
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
	var computed: Dictionary = store.compute_loadout_stats("wookiee", loadouts[0], items)
	assert(float(computed.get("max_health", 0.0)) > 400.0, "expected Wookiee trait health bonus")
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
	# Ammo equips like gear (double-click), into its slot, and says what its hits do.
	var burning := {"id": 990001, "def_id": "ammo_tibanna_burning", "grade": 1, "quantity": 100}
	inv_scr.inventory = items + [burning]
	var saved: Array = []
	inv_scr.loadout_saved.connect(func(_slot: int, lo: Dictionary) -> void: saved.append(lo))
	inv_scr._quick_equip(burning)
	assert(saved.size() == 1 and int((saved[0]["gear_ids"] as Dictionary).get("ammo_standard", 0)) == 990001,
			"burning ammo didn't equip into the ammo slot")
	var burning_def: Dictionary = {}
	for d: Dictionary in catalog:
		if str(d.get("id", "")) == "ammo_tibanna_burning":
			burning_def = d
	var tip := ItemTooltip.build_lines(burning_def, burning, store)
	assert(tip.any(func(l: Dictionary) -> bool: return str(l["text"]).begins_with("On hit: Burning")),
			"burning ammo's tooltip doesn't name its effect")
	# A marked copy: its name and tooltip say its Mk and the level it asks for,
	# and it hits harder (Mk IV: x1.4).
	var e11_def: Dictionary = {}
	for d: Dictionary in catalog:
		if str(d.get("id", "")) == "wp_rifle_e11":
			e11_def = d
	var mk4 := {"id": 990002, "def_id": "wp_rifle_e11", "grade": 2, "mark": 4}
	assert(ItemTooltip.item_name(e11_def, mk4) == str(e11_def["name"]) + " Mk IV", "no Mk in the name")
	assert(store.item_display_name("wp_rifle_e11", 4) == ItemTooltip.item_name(e11_def, mk4), "names disagree")
	var mk_tip := ItemTooltip.build_lines(e11_def, mk4, store)
	var says_level := func(l: Dictionary) -> bool: return str(l["text"]).begins_with("Mk IV: requires") and str(l["text"]).ends_with("level 40")
	assert(mk_tip.any(says_level), "a marked item's tooltip doesn't say its level")
	var dmg0 := float(store.resolve_item_stats("wp_rifle_e11")["damage"])
	var dmg4 := float(store.resolve_item_stats("wp_rifle_e11", 1, 4)["damage"])
	assert(absf(dmg4 - dmg0 * 1.4) < 0.01, "Mk IV doesn't hit 1.4x as hard")
	# Grades: Exceptional hits 16% harder; its tooltip says its grade, in its color; ammo has none.
	var dmg_ex := float(store.resolve_item_stats("wp_rifle_e11", 3)["damage"])
	assert(absf(dmg_ex - dmg0 * 1.16) < 0.01, "Exceptional doesn't hit 1.16x as hard")
	assert(mk_tip.any(func(l: Dictionary) -> bool: return str(l["text"]) == "Fine" and l["color"] == ItemGrade.COLORS[2]),
			"a Fine item's tooltip doesn't say Fine in blue")
	assert(ItemTooltip.name_color(e11_def, mk4) == ItemGrade.COLORS[2], "a Fine name isn't blue")
	assert(ItemTooltip.name_color(burning_def, burning) == ItemGrade.WHITE and ItemGrade.label(burning_def, burning) == "",
			"ammo isn't ungraded white")
	# The bowcaster is a Wookiee's: its tooltip says so.
	var bow_def: Dictionary = {}
	for d: Dictionary in catalog:
		if str(d.get("id", "")) == "wp_rifle_bowcaster":
			bow_def = d
	assert((bow_def["species"] as PackedStringArray).has("wookiee"), "the bowcaster isn't a Wookiee's")
	var bow_tip := ItemTooltip.build_lines(bow_def, {}, store)
	assert(bow_tip.any(func(l: Dictionary) -> bool: return str(l["text"]) == "Wookiee only"), "no species line")
	var chest_req: Dictionary = store.mark_requirement("armor_stormtrooper_chest", 10)
	assert(absf(float(chest_req["health"]) - 0.2) < 1e-6, "a Mk X chest isn't a fifth of the health curve")
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
