# Headless verification for Stage 5 Milestone 3 (Loot, Credits & Vendors):
# 1. Catalog has Tatooine junk items (sell_value) & supplies (heal_amount, cost_credits)
# 2. LootScreen & VendorScreen UI smoke check
# 3. Live Mos Eisley GameSession replicates both Junk Dealer & Gear/Supplies Vendors
extends SceneTree

const Bf2Art = preload("res://ui/bf2_art.gd")
const LootScreen = preload("res://ui/loot_screen.gd")
const VendorScreen = preload("res://ui/vendor_screen.gd")
const GameSession = preload("res://scenes/game_session.gd")

var _session: GameSession
var _e2e_db := ""
var _frames := 0


func _initialize() -> void:
	# 1. Catalog check
	var store: SwmCharacterStore = ClassDB.instantiate("SwmCharacterStore")
	var cat_err: int = store.load_catalog(
			ProjectSettings.globalize_path("res://items/swg_catalog.ini"),
			ProjectSettings.globalize_path("res://imported_assets/items"))
	assert(cat_err == OK, "load_catalog failed: " + store.get_last_error())
	var catalog: Array = store.get_catalog()
	var junk_count := 0
	var supply_count := 0
	for def: Dictionary in catalog:
		var k := str(def.get("kind", ""))
		if k == "junk":
			junk_count += 1
			assert(int(def.get("sell_value", 0)) > 0, "junk missing sell_value: " + str(def.get("id", "")))
		elif k == "supply":
			supply_count += 1
			# Tatooine's healing supplies heal and sell (later supplies, like a
			# creature egg or a droid, needn't do either).
			if str(def.get("id", "")) in ["supply_field_rations", "supply_spiced_tea", "supply_bantha_steak"]:
				assert(float(def.get("heal_amount", 0.0)) > 0.0, "supply missing heal_amount: " + str(def["id"]))
				assert(int(def.get("cost_credits", 0)) > 0, "supply missing cost_credits: " + str(def["id"]))
	assert(junk_count >= 14, "expected >= 14 junk items, got %d" % junk_count)
	assert(supply_count >= 3, "expected >= 3 supply items, got %d" % supply_count)
	print("Stage 5 Milestone 3 catalog: OK (%d junk, %d supplies)" % [junk_count, supply_count])

	# 2. Loot & Vendor UI smoke check
	var art := Bf2Art.load_from("res://imported_assets/hud")
	assert(art != null, "missing hud art")
	var loot_scr := LootScreen.new(art)
	loot_scr.catalog = catalog
	loot_scr.store = store
	loot_scr.corpse = {"entity_id": 99, "name": "Lesser Womp Rat", "items": [{"slot": 0, "def_id": "junk_womp_rat_hide", "grade": 1}]}
	loot_scr.free()

	var vend_scr := VendorScreen.new(art)
	vend_scr.catalog = catalog
	vend_scr.store = store
	vend_scr.open_vendor("Mos Eisley Junk Dealer", "junk_dealer")
	vend_scr.open_vendor("Mos Eisley Gear & Supplies", "gear_vendor")
	vend_scr.free()
	print("Stage 5 Milestone 3 UI screens: OK")

	# 3. Live Mos Eisley hub session vendor replication check
	_e2e_db = ProjectSettings.globalize_path("res://stage5_loot_temp.sqlite")
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
		"name": "Stage5Looter",
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
			printerr("FAIL: Stage5Looter did not spawn")
			_cleanup(1)
			return true
		return false

	var vendors: Array = client.get_vendors()
	assert(vendors.size() >= 2, "expected at least 2 vendors in Mos Eisley, got %d" % vendors.size())
	var kinds: Array = []
	for v: Dictionary in vendors:
		kinds.append(str(v.get("kind", "")))
	assert(kinds.has("junk_dealer") and kinds.has("gear_vendor"), "missing junk_dealer or gear_vendor")
	assert(client.get_quartermasters().filter(func(q: Dictionary) -> bool: return str(q.get("town", "")) == "").size() == 4, "expected 4 faction quartermasters")
	assert(client.get_quartermasters().filter(func(q: Dictionary) -> bool: return str(q.get("town", "")) != "").size() == 7, "expected a city quartermaster in each of Tatooine's 7 towns")
	print("Stage 5 Milestone 3 Mos Eisley vendors: OK")
	_cleanup(0)
	return true


func _cleanup(code: int) -> void:
	if _session != null:
		_session.leave_session()
		_session.queue_free()
		_session = null
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(_e2e_db + suffix):
			DirAccess.remove_absolute(_e2e_db + suffix)
	quit(code)
