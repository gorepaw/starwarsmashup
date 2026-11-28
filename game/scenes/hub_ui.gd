# Hub UI & world markers: War Terminal screen, Quartermaster shop,
# Inventory & Loadouts screen, queue HUD, shooting range target visuals/DPS,
# battle outcome notices, and quests (givers' offers, mission terminals, the
# quest log, the tracker, objective markers on screen and on the radar).
class_name HubUI
extends Node

const Settings = preload("res://ui/settings.gd")
const WarTerminalScreen = preload("res://ui/war_terminal_screen.gd")
const QuartermasterScreen = preload("res://ui/quartermaster_screen.gd")
const InventoryScreen = preload("res://ui/inventory_screen.gd")
const LootScreen = preload("res://ui/loot_screen.gd")
const VendorScreen = preload("res://ui/vendor_screen.gd")
const HubHud = preload("res://ui/hub_hud.gd")
const QuestScreen = preload("res://ui/quest_screen.gd")
const ProfessionScreen = preload("res://ui/profession_screen.gd")
const WorkbenchScreen = preload("res://ui/workbench_screen.gd")
const WorldMapScreen = preload("res://ui/world_map_screen.gd")
const TrainerScreen = preload("res://ui/trainer_screen.gd")
const BankScreen = preload("res://ui/bank_screen.gd")
const InfoTerminalScreen = preload("res://ui/info_terminal_screen.gd")
const TravelScreen = preload("res://ui/travel_screen.gd")
const MultiScanner3D = preload("res://rendering/multi_scanner.gd")
const TeamTags = preload("res://rendering/team_tags.gd")
const SwgCity = preload("res://rendering/swg_city.gd")
const ItemGrade = preload("res://ui/item_grade.gd")
const LOOT_BEAM_HEIGHT := 6.0  # m: a corpse's loot beam

const TALK_REACH := 4.5     # meters from a quest NPC to talk to them (the server allows 6)
const OFFER_REACH := 8.0    # an offer closes when you walk further than this from it
const GIVER_COLOR := Color(1.0, 0.8, 0.35)
const GIVER_TURNIN_COLOR := Color(1.0, 0.52, 0.12)
const GIVER_WAITING_COLOR := Color(0.6, 0.65, 0.75, 1.0)  # a giver whose work isn't open to you yet
const GIVER_DONE := 0
const GIVER_WAITING := 1
const GIVER_READY := 2
const GIVER_TURNIN := 3
const MARKER_RANGE := 160.0  # m a creature's or bandit's chevron shows from
const WHITE_CHEVRON := Color(0.95, 0.95, 0.95)
const RED_CHEVRON := Color(1.0, 0.3, 0.25)
const TERMINAL_COLOR := Color(0.45, 0.85, 1.0)
const SHOP_COLOR := Color(0.35, 0.92, 0.55)
const CRAFT_COLOR := Color(0.75, 0.5, 1.0)
const HOLO_HEIGHT := 2.6   # m over a place's ground, its hologram
const STATION_HOLO_HEIGHT := 3.7  # over a crafting station's (they stand up to 2.9 m)
const TERMINAL_CHEVRON_HEIGHT := 2.9  # over a mission terminal's (2.1 m)
# Where the city has the terminal's or station's model, its symbol is this
# far over its top (measured: SwgCity.prop_top); the heights above where not.
const HOLO_OVER_PROP := 0.9
const CHEVRON_OVER_PROP := 0.55
const BARE_TERMINAL_HOLO_HEIGHT := 1.9  # a battle terminal with nothing drawn: about a terminal's height
const PLACE_HOLO_SCALE := 2.5  # a place's hologram, as big as a soldier's this many times (ASSUMPTION, by eye)
const CLONING_HOLO_SCALE := 3.6  # a cloning facility's, larger: it has no terminal of its own to see
const CLONING_INDOOR_REACH := 22.0  # m: under a roof this near a cloning facility, you're in it
const TARGET_HOLO_ID := 1 << 40  # practice targets' hologram ids, past any entity's

# Stage 11: `mode` is the game mode id wanted ("conquest", "ctf", "1flag",
# "hunt", "xl", "any"; "assault" at a Space Terminal); "" when leaving.
signal queue_requested(side: int, mode: String)
signal item_bought(def_id: String, use_fp: bool)
signal loadout_saved(slot_index: int, loadout: Dictionary)
signal loadout_activated(slot_index: int)
signal item_destroyed(item_id: int)
signal item_used(item_id: int)
signal loot_taken(corpse_entity: int, slot: int)
signal item_sold(item_id: int)
signal all_junk_sold
signal terminal_closed
signal quest_accepted(id: String)
signal quest_abandoned(id: String)
signal corpse_harvested(corpse_entity: int)
signal craft_requested(schematic_id: String, resource_ids: PackedStringArray, enhancement_ids: PackedInt64Array, exp_quality_pct: int, batch_count: int, mark: int)
signal dye_requested(item_id: int, dye: String)
signal preset_saved(name: String, schematic_id: String, resource_ids: PackedStringArray, enhancement_defs: PackedStringArray, exp_quality_pct: int)
signal preset_deleted(name: String)
signal work_order_fulfilled(order_id: String)
signal item_salvaged(item_id: int)
signal service_requested(action: int, id: String, amount: int, item_id: int)

var _hub_hud: HubHud
var _terminal_screen: WarTerminalScreen
var _qm_screen: QuartermasterScreen
var _inv_screen: InventoryScreen
var _loot_screen: LootScreen
var _vendor_screen: VendorScreen
var _quest_screen: QuestScreen
var _profession_screen: ProfessionScreen
var _workbench_screen: WorkbenchScreen
var _map_screen: WorldMapScreen
var _trainer_screen: TrainerScreen
var _bank_screen: BankScreen
var _info_screen: InfoTerminalScreen
var _travel_screen: TravelScreen
var _professions: Array = []       # the professions' defs (set_professions), for the trainers
var _trained_by := ""              # the trainer (their name) whose screen is open
var map_title := "Map"         # the map's title (the session sets it: the planet and the town)
var map_planet := {}           # the radar's picture of the planet, for the map's widest view
var _scanner: MultiScanner3D
var _settings: Settings
var _world_root: Node3D
var _recruiter_nodes: Array = []
var _bare_recruiters: Array = []  # battle terminals the city draws none for: a hologram stands for each
var _recruiters_added := false
var _corpse_nodes: Dictionary = {}  # corpse_entity -> {icon (its loot beam), label, color}
var _catalog_by_id: Dictionary = {}  # item id -> catalog entry (for loot beams' colors)
var _holograms: TeamTags      # holograms over the world's people and places (null: no insignia exported)
var _holograms_looked := false
var _holo_anchors := {}       # place key -> Node3D standing where it is
var _over_roofs := {}         # place key -> its hologram's height over a building's roof (cloning)
var _over_props := {}         # place key -> its hologram's height over its terminal's or station's top
var _waypoint_ground := {}    # quest waypoint (x, z) -> where it is on the floor
var _target_nodes: Dictionary = {}  # target_id -> {root, body, label, hits: Array[{t, dmg}], last_dmg, flash}
var _notice_id := 0
var _notice := ""
var _notice_time := 0.0
var _notice_lines: PackedStringArray = []
var _prof_levels := {}              # profession id -> last seen level (for level-up banner)
var _level_up_queue: Array = []     # [{title, detail}]
var _level_up_active: Dictionary = {}
var _level_up_time := 0.0
var _catalog: Array = []
var _store: Object = null
var _crafting_store: Object = null
var _tracked := ""                 # the tracked quest's id
var _known_quests := {}            # active quest ids last seen (a new one is tracked)
var _offer_serial := -1            # the offer serial when we asked; -1: not waiting
var _offer_at := Vector3.ZERO      # where the offer was asked for
var _marker_nodes: Dictionary = {}  # key -> Sprite3D: the objective chevrons over people and mobs
var _single_chevron: AtlasTexture   # one of the objective icon's chevrons


func setup(layer: CanvasLayer, world_root: Node3D, art: RefCounted, settings: Settings) -> void:
	_world_root = world_root
	_settings = settings
	if art == null:
		return
	_hub_hud = HubHud.new(art)
	layer.add_child(_hub_hud)
	_terminal_screen = WarTerminalScreen.new(art)
	_terminal_screen.visible = false
	_terminal_screen.side_chosen.connect(func(side: int, mode: String) -> void: queue_requested.emit(side, mode))
	_terminal_screen.leave_requested.connect(func() -> void: queue_requested.emit(-1, ""))
	_terminal_screen.closed.connect(close_terminal)
	layer.add_child(_terminal_screen)

	_qm_screen = QuartermasterScreen.new(art)
	_qm_screen.visible = false
	_qm_screen.item_bought.connect(func(def_id: String, use_fp: bool) -> void: item_bought.emit(def_id, use_fp))
	_qm_screen.closed.connect(close_all_screens)
	layer.add_child(_qm_screen)

	_inv_screen = InventoryScreen.new(art)
	_inv_screen.visible = false
	_inv_screen.loadout_saved.connect(func(slot: int, lo: Dictionary) -> void: loadout_saved.emit(slot, lo))
	_inv_screen.loadout_activated.connect(func(slot: int) -> void:
		loadout_activated.emit(slot)
		close_all_screens())
	_inv_screen.item_destroyed.connect(func(iid: int) -> void: item_destroyed.emit(iid))
	_inv_screen.item_used.connect(func(iid: int) -> void: item_used.emit(iid))
	_inv_screen.closed.connect(close_all_screens)
	layer.add_child(_inv_screen)

	_loot_screen = LootScreen.new(art)
	_loot_screen.visible = false
	_loot_screen.loot_taken.connect(func(cid: int, slot: int) -> void: loot_taken.emit(cid, slot))
	_loot_screen.corpse_harvested.connect(func(cid: int) -> void: corpse_harvested.emit(cid))
	_loot_screen.closed.connect(close_all_screens)
	layer.add_child(_loot_screen)

	_vendor_screen = VendorScreen.new(art)
	_vendor_screen.visible = false
	_vendor_screen.item_bought.connect(func(did: String, use_fp: bool) -> void: item_bought.emit(did, use_fp))
	_vendor_screen.item_sold.connect(func(iid: int) -> void: item_sold.emit(iid))
	_vendor_screen.all_junk_sold.connect(func() -> void: all_junk_sold.emit())
	_vendor_screen.closed.connect(close_all_screens)
	layer.add_child(_vendor_screen)

	_quest_screen = QuestScreen.new(art)
	_quest_screen.visible = false
	_quest_screen.quest_accepted.connect(func(id: String) -> void:
		_tracked = id
		quest_accepted.emit(id))
	_quest_screen.quest_abandoned.connect(func(id: String) -> void: quest_abandoned.emit(id))
	_quest_screen.quest_tracked.connect(func(id: String) -> void:
		_tracked = id
		_quest_screen.tracked = id
		_quest_screen.queue_redraw())
	_quest_screen.closed.connect(close_all_screens)
	layer.add_child(_quest_screen)

	_profession_screen = ProfessionScreen.new(art)
	_profession_screen.visible = false
	_profession_screen.closed.connect(close_all_screens)
	layer.add_child(_profession_screen)

	_workbench_screen = WorkbenchScreen.new(art)
	_workbench_screen.visible = false
	_workbench_screen.craft_requested.connect(func(sid: String, rids: PackedStringArray, eids: PackedInt64Array, qpct: int, cnt: int, mark: int) -> void:
		craft_requested.emit(sid, rids, eids, qpct, cnt, mark))
	_workbench_screen.dye_requested.connect(func(iid: int, dye: String) -> void: dye_requested.emit(iid, dye))
	_workbench_screen.preset_saved.connect(func(pname: String, sid: String, rids: PackedStringArray, edefs: PackedStringArray, qpct: int) -> void:
		preset_saved.emit(pname, sid, rids, edefs, qpct))
	_workbench_screen.preset_deleted.connect(func(pname: String) -> void: preset_deleted.emit(pname))
	_workbench_screen.work_order_fulfilled.connect(func(oid: String) -> void: work_order_fulfilled.emit(oid))
	_workbench_screen.item_salvaged.connect(func(iid: int) -> void: item_salvaged.emit(iid))
	_workbench_screen.closed.connect(close_all_screens)
	layer.add_child(_workbench_screen)

	_map_screen = WorldMapScreen.new(art)
	_map_screen.visible = false
	_map_screen.closed.connect(close_all_screens)
	layer.add_child(_map_screen)

	_trainer_screen = TrainerScreen.new(art)
	_trainer_screen.visible = false
	_trainer_screen.service_requested.connect(func(action: int, id: String, amount: int) -> void:
		service_requested.emit(action, id, amount, 0))
	_trainer_screen.closed.connect(close_all_screens)
	layer.add_child(_trainer_screen)

	_bank_screen = BankScreen.new(art)
	_bank_screen.visible = false
	_bank_screen.service_requested.connect(func(action: int, id: String, amount: int, item_id: int) -> void:
		service_requested.emit(action, id, amount, item_id))
	_bank_screen.closed.connect(close_all_screens)
	layer.add_child(_bank_screen)

	_info_screen = InfoTerminalScreen.new(art)
	_info_screen.visible = false
	_info_screen.closed.connect(close_all_screens)
	layer.add_child(_info_screen)

	_travel_screen = TravelScreen.new(art)
	_travel_screen.visible = false
	_travel_screen.service_requested.connect(func(action: int, id: String, amount: int, item_id: int) -> void:
		service_requested.emit(action, id, amount, item_id))
	_travel_screen.closed.connect(close_all_screens)
	layer.add_child(_travel_screen)

	if world_root != null:
		_scanner = MultiScanner3D.new()
		world_root.add_child(_scanner)


func set_crafting(store: Object) -> void:
	_crafting_store = store
	if _workbench_screen != null:
		_workbench_screen.store = store
		if store != null:
			_workbench_screen.resources = store.get_resources()
			_workbench_screen.schematics = store.get_schematics()
			_workbench_screen.work_orders = store.get_work_orders()


func set_catalog(catalog: Array, store: Object, species: String, gender: String, appearance: String) -> void:
	_catalog = catalog
	_store = store
	if _qm_screen != null:
		_qm_screen.catalog = catalog
		_qm_screen.store = store
	if _inv_screen != null:
		_inv_screen.catalog = catalog
		_inv_screen.store = store
		_inv_screen.species = species
		_inv_screen.gender = gender
	if _workbench_screen != null:
		_workbench_screen.catalog = catalog
		_inv_screen.appearance_json = appearance
	if _loot_screen != null:
		_loot_screen.catalog = catalog
		_loot_screen.store = store
	if _vendor_screen != null:
		_vendor_screen.catalog = catalog
		_vendor_screen.store = store
	if _quest_screen != null:
		_quest_screen.catalog = catalog


# The profession roster (SwmCharacterStore.get_professions()) and the store
# that knows its level curve.
func set_professions(professions: Array, store: Object) -> void:
	_professions = professions
	if _profession_screen != null:
		_profession_screen.professions = professions
		_profession_screen.store = store
		_profession_screen.cap = int(store.get_perk_cap())
	if _trainer_screen != null:
		_trainer_screen.cap = int(store.get_perk_cap())


func has_ui() -> bool:
	return _terminal_screen != null


func is_terminal_open() -> bool:
	return is_any_screen_open()


func is_any_screen_open() -> bool:
	return (_terminal_screen != null and _terminal_screen.visible) \
			or (_qm_screen != null and _qm_screen.visible) \
			or (_inv_screen != null and _inv_screen.visible) \
			or (_loot_screen != null and _loot_screen.visible) \
			or (_vendor_screen != null and _vendor_screen.visible) \
			or (_quest_screen != null and _quest_screen.visible) \
			or (_profession_screen != null and _profession_screen.visible) \
			or (_workbench_screen != null and _workbench_screen.visible) \
			or (_trainer_screen != null and _trainer_screen.visible) \
			or (_bank_screen != null and _bank_screen.visible) \
			or (_info_screen != null and _info_screen.visible) \
			or (_travel_screen != null and _travel_screen.visible) \
			or (_map_screen != null and _map_screen.visible)


func try_open_terminal(client: Object) -> bool:
	# Talking to someone a quest names counts, whatever else they do (the
	# junk dealer buys junk too).
	var person := at_quest_npc(client)
	if not person.is_empty():
		client.talk_to(int(person["entity"]))
	if _terminal_screen != null and not at_recruiter(client).is_empty():
		_terminal_screen.visible = true
		return true
	var qm := at_quartermaster(client)
	if _qm_screen != null and not qm.is_empty():
		_qm_screen.title = str(qm.get("name", "Quartermaster"))
		_qm_screen.faction = str(qm.get("faction", "neutral"))
		_qm_screen.feedback = ""
		_qm_screen.visible = true
		_qm_screen.queue_redraw()
		return true
	var service := at_service(client)
	if not service.is_empty():
		match str(service["kind"]):
			"trainer":
				if _trainer_screen != null:
					_sync_screen_data(client)
					_trained_by = str(service["name"])
					_trainer_screen.trainer_name = _trained_by
					_trainer_screen.professions = _professions_of(str(service.get("detail", "")))
					_trainer_screen.open_trainer()
					return true
			"bank":
				if _bank_screen != null:
					_sync_screen_data(client)
					_bank_screen.open_bank(str(service["name"]))
					return true
			"bazaar", "newsnet", "gambling", "data_terminal":
				if _info_screen != null:
					_info_screen.open_terminal(str(service["kind"]), str(service["name"]), str(service.get("detail", "")))
					return true
			"travel", "starport":
				if _travel_screen != null:
					# The planet's shuttleports and starports; from a starport,
					# other planets' too (Stage 13).
					var points: Array = []
					for v: Dictionary in client.get_vendors():
						var kind := str(v["kind"])
						if kind == "travel" or kind == "starport":
							points.append(v)
						elif kind == "travel_offworld" and str(service["kind"]) == "starport":
							points.append(v)
					var status: Dictionary = client.get_player_status()
					_travel_screen.open_travel(str(service["name"]), points, int(status.get("credits", 0)))
					return true
			"clinic":
				service_requested.emit(5, "", 0, 0)
			"cloning":
				service_requested.emit(6, "", 0, 0)
		return false  # (a clinic and a cloning terminal answer with a notice)
	var vendor := at_vendor(client)
	if _vendor_screen != null and not vendor.is_empty():
		_sync_screen_data(client)
		_vendor_screen.open_vendor(str(vendor.get("name", "Vendor")), str(vendor.get("kind", "gear_vendor")))
		return true
	var wb := at_workbench(client)
	if _workbench_screen != null and not wb.is_empty():
		_sync_screen_data(client)
		_workbench_screen.open_screen(str(wb.get("kind", "general")), str(wb.get("name", "Workbench")))
		return true
	if _quest_screen != null and not person.is_empty() and bool(person.get("giver", false)):
		_await_offers(client, str(person["name"]))
		return true
	var mission_terminal := at_mission_terminal(client)
	if _quest_screen != null and not mission_terminal.is_empty():
		client.use_mission_terminal(int(mission_terminal["index"]))
		_await_offers(client, str(mission_terminal["name"]))
		return true
	if not person.is_empty():
		return false  # talked: what it did comes back as a notice
	var corpse := at_lootable_corpse(client)
	if _loot_screen != null and not corpse.is_empty():
		_loot_screen.corpse = corpse
		_loot_screen.feedback = ""
		_loot_screen.visible = true
		_loot_screen.queue_redraw()
		return true
	return false


func is_loot_open() -> bool:
	return _loot_screen != null and _loot_screen.visible


func loot_all_and_harvest(_client: Object) -> void:
	if _loot_screen == null or not _loot_screen.visible:
		return
	var cid := int(_loot_screen.corpse.get("entity_id", 0))
	if cid > 0:
		if not (_loot_screen.corpse.get("items", []) as Array).is_empty():
			loot_taken.emit(cid, -1)
		corpse_harvested.emit(cid)
	close_all_screens()


func show_local_notice(msg: String) -> void:
	_notice = msg
	_notice_time = 5.0


# The professions' defs for a trainer's `detail` (their ids, comma-separated).
func _professions_of(ids: String) -> Array:
	var wanted := ids.split(",", false)
	var out: Array = []
	for id: String in wanted:
		for p: Dictionary in _professions:
			if str(p.get("id", "")) == id.strip_edges():
				out.append(p)
	return out


func _await_offers(client: Object, from: String) -> void:
	close_all_screens()
	_offer_serial = int(client.get_quests().get("offer_serial", 0))
	_offer_at = client.get_presented()["position"]
	_quest_screen.player_position = _offer_at
	_quest_screen.open_offers(from)


# Opens or closes the quest log; true if it's open now.
func toggle_quest_log(client: Object) -> bool:
	if _quest_screen == null or client == null or not client.is_spawned():
		return false
	if _quest_screen.visible and _quest_screen.mode == "log":
		close_all_screens()
		return false
	close_all_screens()
	_sync_quests(client)
	_quest_screen.open_log()
	return true


# Opens or closes the profession planner; true if it's open now.
func toggle_professions(client: Object) -> bool:
	if _profession_screen == null or client == null or not client.is_spawned():
		return false
	if _profession_screen.visible:
		close_all_screens()
		return false
	close_all_screens()
	_sync_screen_data(client)
	_profession_screen.open_screen()
	return true


func toggle_inventory(client: Object) -> bool:
	if _inv_screen == null or client == null or not client.is_spawned():
		return false
	if _inv_screen.visible:
		close_all_screens()
		return false
	close_all_screens()
	_sync_screen_data(client)
	_inv_screen.open_screen()
	return true


# Opens or closes the map; true if it's open now.
func toggle_map(client: Object) -> bool:
	if _map_screen == null or client == null or not client.is_spawned():
		return false
	if _map_screen.visible:
		close_all_screens()
		return false
	close_all_screens()
	var own: Dictionary = client.get_presented()
	_map_screen.title = map_title
	_map_screen.planet = map_planet
	_map_screen.player_position = own["position"]
	_map_screen.player_yaw = float(own.get("yaw", 0.0))
	_map_screen.places = map_places(client)
	_map_screen.towns = map_towns(client)
	var quest := tracked_quest(client)
	_map_screen.waypoint = {"position": quest["waypoint"], "name": str(quest.get("title", ""))} \
			if quest.has("waypoint") else {}
	_map_screen.open_map()
	return true


# The hub's places for the map: shops (vendors, quartermasters), quest
# givers (bright when they've work for you) and terminals (missions, the war
# terminal), and workbenches.
func map_places(client: Object) -> Array:
	var out: Array = []
	for v: Dictionary in client.get_vendors():
		if str(v["kind"]) == "travel_offworld":
			continue  # another planet's starport (Stage 13)
		if is_shop(str(v["kind"])):
			out.append({"position": v["position"], "name": str(v["name"]), "kind": "shop", "color": WorldMapScreen.SHOP})
		else:
			out.append({"position": v["position"], "name": str(v["name"]), "kind": "service", "color": WorldMapScreen.TERMINAL})
	for q: Dictionary in client.get_quartermasters():
		out.append({"position": q["position"], "name": str(q["name"]), "kind": "shop", "color": WorldMapScreen.SHOP})
	var quests: Dictionary = client.get_quests()
	for npc: Dictionary in client.get_quest_npcs():
		if bool(npc.get("giver", false)):
			var state := _giver_state(quests, str(npc["name"]))
			if state != GIVER_DONE:
				var c := GIVER_TURNIN_COLOR if state == GIVER_TURNIN else (WorldMapScreen.QUEST if state == GIVER_READY else GIVER_WAITING_COLOR)
				out.append({"position": npc["position"], "name": str(npc["name"]), "kind": "quest", "color": c})
	for wb: Dictionary in client.get_workbenches():
		out.append({"position": wb["position"], "name": str(wb["name"]), "kind": "craft", "color": WorldMapScreen.CRAFT})
	for r: Dictionary in client.get_recruiters():
		out.append({"position": r["position"], "name": str(r["name"]), "kind": "battle", "color": WorldMapScreen.BATTLE})
	for t: Dictionary in client.get_mission_terminals():
		out.append({"position": t["position"], "name": str(t["name"]), "kind": "terminal", "color": _mission_terminal_color(str(t.get("kind", "general")))})
	return out


# The planet's towns for the map: each named by its travel points (a
# shuttleport's or starport's Detail is its town), at their middle.
func map_towns(client: Object) -> Array:
	var sums := {}  # name -> [position sum, count]
	for v: Dictionary in client.get_vendors():
		var kind := str(v.get("kind", ""))
		var name := str(v.get("detail", ""))
		if (kind != "travel" and kind != "starport") or name == "":
			continue
		if not sums.has(name):
			sums[name] = [Vector3.ZERO, 0]
		sums[name][0] += v["position"]
		sums[name][1] += 1
	var out: Array = []
	for name: String in sums:
		out.append({"name": name, "position": sums[name][0] / float(sums[name][1])})
	return out


func toggle_workbench(client: Object) -> bool:
	if _workbench_screen == null or client == null or not client.is_spawned():
		return false
	if _workbench_screen.visible:
		close_all_screens()
		return false
	close_all_screens()
	_sync_screen_data(client)
	# At a station, its own; elsewhere the vault, orders and salvage, and the
	# schematics to look over (made only at their stations).
	var wb := at_workbench(client)
	if wb.is_empty():
		_workbench_screen.open_screen("none", "Workbench")
	else:
		_workbench_screen.open_screen(str(wb.get("kind", "general")), str(wb.get("name", "Workbench")))
	return true


func toggle_scanner() -> bool:
	if _scanner == null:
		return false
	var on := _scanner.toggle()
	if not on and _hub_hud != null:
		_hub_hud.scanner = {}
	return on


func is_scanner_active() -> bool:
	return _scanner != null and _scanner.active


func cycle_scanner(step: int) -> void:
	if _scanner != null and _scanner.active:
		_scanner.cycle(step)


func extract_scanner(client: Object) -> bool:
	if _scanner == null or not _scanner.active or client == null or not client.is_spawned():
		return false
	if _scanner.extract_cooldown > 0.0:
		return false
	client.gather_extract(_scanner.current_mode())
	_scanner.extract_cooldown = 0.4  # (the server takes its own time: it is a few seconds, standing still)
	return true


func close_terminal() -> void:
	close_all_screens()


func close_all_screens() -> void:
	var was_open := is_any_screen_open()
	if _terminal_screen != null:
		_terminal_screen.visible = false
	if _qm_screen != null:
		_qm_screen.visible = false
	if _inv_screen != null and _inv_screen.visible:
		_inv_screen.close_screen()
	if _loot_screen != null:
		_loot_screen.visible = false
	if _vendor_screen != null:
		_vendor_screen.visible = false
	if _quest_screen != null:
		_quest_screen.visible = false
	if _profession_screen != null:
		_profession_screen.visible = false
	if _workbench_screen != null:
		_workbench_screen.visible = false
	if _map_screen != null and _map_screen.visible:
		_map_screen.close_map()
	if _trainer_screen != null:
		_trainer_screen.visible = false
	if _bank_screen != null:
		_bank_screen.visible = false
	if _info_screen != null:
		_info_screen.visible = false
	if _travel_screen != null:
		_travel_screen.visible = false
	_offer_serial = -1
	if was_open:
		terminal_closed.emit()


func clear_recruiters() -> void:
	for key: String in _holo_anchors.keys():
		if _holograms != null:
			_holograms.forget(key.hash())
		if is_instance_valid(_holo_anchors[key]):
			(_holo_anchors[key] as Node).queue_free()
	_holo_anchors.clear()
	_over_roofs.clear()
	_over_props.clear()
	_waypoint_ground.clear()
	for node: Node in _recruiter_nodes:
		if is_instance_valid(node):
			node.queue_free()
	_recruiter_nodes.clear()
	_bare_recruiters.clear()
	_recruiters_added = false
	for entry: Dictionary in _corpse_nodes.values():
		for k in ["icon", "label"]:
			var n: Variant = entry.get(k)
			if n is Node and is_instance_valid(n):
				(n as Node).queue_free()
	_corpse_nodes.clear()
	for node: Node in _marker_nodes.values():
		if is_instance_valid(node):
			node.queue_free()
	_marker_nodes.clear()
	for entry: Dictionary in _target_nodes.values():
		var r: Variant = entry.get("root")
		if r is Node and is_instance_valid(r):
			(r as Node).queue_free()
	_target_nodes.clear()


func on_world_arrived() -> void:
	clear_recruiters()
	close_all_screens()
	_notice_id = 0


# The battle terminals the city doesn't draw (a Galaxies terminal stands for
# most): each gets a hologram, as the world's other places do (a starfighter
# for space battles, Battlefront's command post icon for ground ones).
func add_recruiters(client: Object, world: Node3D) -> void:
	if _recruiters_added or client == null or world == null:
		return
	var recruiters: Array = client.get_recruiters()
	if recruiters.is_empty():
		return
	_recruiters_added = true
	var terminals: Array[Vector3] = []
	for node: Node in world.get_children():
		if node.has_meta("recruiter"):
			terminals.append((node as Node3D).position)
	for recruiter: Dictionary in recruiters:
		var position: Vector3 = recruiter["position"]
		var drawn := terminals.any(func(at: Vector3) -> bool:
			return Vector2(at.x - position.x, at.z - position.z).length() < 1.5)
		if not drawn:
			_bare_recruiters.append(recruiter)


# Keys 1 and 2 at an open battle terminal: a side of the era it shows.
func terminal_pick(n: int) -> void:
	if _terminal_screen != null and _terminal_screen.visible:
		_terminal_screen.pick(n)


func at_recruiter(client: Object) -> Dictionary:
	if client == null or not client.is_spawned():
		return {}
	var position: Vector3 = client.get_presented()["position"]
	for recruiter: Dictionary in client.get_recruiters():
		var at: Vector3 = recruiter["position"]
		if Vector2(position.x - at.x, position.z - at.z).length() <= float(recruiter["radius"]):
			return recruiter
	return {}


func at_quartermaster(client: Object) -> Dictionary:
	if client == null or not client.is_spawned():
		return {}
	var position: Vector3 = client.get_presented()["position"]
	for qm: Dictionary in client.get_quartermasters():
		var at: Vector3 = qm["position"]
		if Vector2(position.x - at.x, position.z - at.z).length() <= float(qm["radius"]):
			return qm
	return {}


func at_vendor(client: Object) -> Dictionary:
	if client == null or not client.is_spawned():
		return {}
	var position: Vector3 = client.get_presented()["position"]
	var best: Dictionary = {}
	var best_d := INF
	for v: Dictionary in client.get_vendors():
		if not is_shop(str(v["kind"])):
			continue
		var at: Vector3 = v["position"]
		var d := Vector2(position.x - at.x, position.z - at.z).length()
		if d <= float(v["radius"]) and d < best_d:  # the nearest, where they stand together
			best = v
			best_d = d
	return best


# Shops (junk dealer, gear vendor, garage) trade; service points (trainer,
# bank, clinic, cloning terminal) do something else.
static func is_shop(kind: String) -> bool:
	return kind == "junk_dealer" or kind == "gear_vendor" or kind == "garage"


# The service point you stand at (kinds: trainer, bank, clinic, cloning).
func at_service(client: Object) -> Dictionary:
	if client == null or not client.is_spawned():
		return {}
	var position: Vector3 = client.get_presented()["position"]
	var best: Dictionary = {}
	var best_d := INF
	for v: Dictionary in client.get_vendors():
		if is_shop(str(v["kind"])) or str(v["kind"]) == "travel_offworld":
			continue  # (another planet's starport: not here)
		var at: Vector3 = v["position"]
		var d := Vector2(position.x - at.x, position.z - at.z).length()
		if d <= float(v["radius"]) and d < best_d:  # the nearest (trainers stand side by side)
			best = v
			best_d = d
	return best


func at_workbench(client: Object) -> Dictionary:
	if client == null or not client.is_spawned():
		return {}
	var position: Vector3 = client.get_presented()["position"]
	var best: Dictionary = {}
	var best_d := INF
	for wb: Dictionary in client.get_workbenches():
		var at: Vector3 = wb["position"]
		var d := Vector2(position.x - at.x, position.z - at.z).length()
		if d <= float(wb["radius"]) and d < best_d:  # the nearest, where stations stand together
			best = wb
			best_d = d
	return best


# The nearest of the people quests name, within talking reach.
func at_quest_npc(client: Object) -> Dictionary:
	if client == null or not client.is_spawned():
		return {}
	var position: Vector3 = client.get_presented()["position"]
	var best := {}
	var best_d := TALK_REACH
	for npc: Dictionary in client.get_quest_npcs():
		var at: Vector3 = npc["position"]
		var d := Vector2(position.x - at.x, position.z - at.z).length()
		if d <= best_d:
			best_d = d
			best = npc
	return best


func at_mission_terminal(client: Object) -> Dictionary:
	if client == null or not client.is_spawned():
		return {}
	var position: Vector3 = client.get_presented()["position"]
	for t: Dictionary in client.get_mission_terminals():
		var at: Vector3 = t["position"]
		if Vector2(position.x - at.x, position.z - at.z).length() <= float(t["radius"]):
			return t
	return {}


# The quest journal into the quest screen; tracks a quest that's new, or
# the first when the tracked one is gone.
func _sync_quests(client: Object) -> Dictionary:
	var quests: Dictionary = client.get_quests()
	var active: Array = quests.get("active", [])
	var ids := {}
	for q: Dictionary in active:
		var id := str(q["id"])
		ids[id] = true
		if not _known_quests.is_empty() and not _known_quests.has(id):
			_tracked = id
	if not ids.has(_tracked):
		_tracked = str(active[0]["id"]) if not active.is_empty() else ""
	_known_quests = ids if not ids.is_empty() else {"": true}
	if _quest_screen != null:
		_quest_screen.active = active
		_quest_screen.completed = quests.get("completed", [])
		_quest_screen.tracked = _tracked
		_quest_screen.player_position = client.get_presented()["position"]
		_quest_screen.combat_level = int(client.get_player_status().get("combat_level", 1))
		if _quest_screen.visible and _quest_screen.mode == "offers":
			if _quest_screen.waiting and int(quests.get("offer_serial", 0)) != _offer_serial:
				_quest_screen.waiting = false
				_offer_serial = -1
			if not _quest_screen.waiting:
				var note := str(quests.get("offer_note", ""))
				var offers: Array = quests.get("offers", [])
				if offers.is_empty() and note == "__completed__":
					close_all_screens()
					return quests
				_quest_screen.offers = offers
				_quest_screen.offer_note = note
				_quest_screen.source = str(quests.get("offer_source", _quest_screen.source))
		if _quest_screen.visible:
			_quest_screen.queue_redraw()
	return quests


func tracked_quest(client: Object) -> Dictionary:
	for q: Dictionary in client.get_quests().get("active", []):
		if str(q["id"]) == _tracked:
			return q
	return {}


# What the radar shows for quests: the tracked step's place (on the rim when
# it's further than the radar reaches), the other quests' dimmer, and the
# quest givers and mission terminals near by.
func radar_markers(client: Object) -> Array:
	var out: Array = []
	if client == null or not client.is_spawned() or client.get_recruiters().is_empty():
		return out
	for q: Dictionary in client.get_quests().get("active", []):
		if not q.has("waypoint"):
			continue
		var at: Vector3 = q["waypoint"]
		var tracked := str(q["id"]) == _tracked
		out.append({"position": Vector2(at.x, at.z), "kind": "waypoint", "icon": "hud_objective_icon",
				"color": Color(GIVER_COLOR, 1.0 if tracked else 0.45)})
	var quests: Dictionary = client.get_quests()
	for npc: Dictionary in client.get_quest_npcs():
		if bool(npc.get("giver", false)):
			var state := _giver_state(quests, str(npc["name"]))
			if state != GIVER_DONE:
				var at: Vector3 = npc["position"]
				var c := GIVER_TURNIN_COLOR if state == GIVER_TURNIN else (GIVER_COLOR if state == GIVER_READY else GIVER_WAITING_COLOR)
				out.append({"position": Vector2(at.x, at.z), "kind": "giver", "color": c})
	for t: Dictionary in client.get_mission_terminals():
		var at: Vector3 = t["position"]
		out.append({"position": Vector2(at.x, at.z), "kind": "terminal", "color": _mission_terminal_color(str(t.get("kind", "general")))})
	return out


func _update_quest_hud(client: Object) -> void:
	var quests := _sync_quests(client)
	if _quest_screen.visible and _quest_screen.mode == "offers":
		var here: Vector3 = client.get_presented()["position"]
		if Vector2(here.x - _offer_at.x, here.z - _offer_at.z).length() > OFFER_REACH:
			close_all_screens()
	_update_objective_markers(client)
	var quest := tracked_quest(client)
	if quest.is_empty():
		_hub_hud.tracker = {}
		_hub_hud.waypoint = {}
		return
	var steps: Array = quest.get("steps", [])
	var progress := ""
	if int(quest.get("needed", 0)) > 1:
		progress = "%d/%d" % [int(quest.get("count", 0)), int(quest["needed"])]
	var own: Vector3 = client.get_presented()["position"]
	var distance := ""
	var waypoint := {}
	if quest.has("waypoint"):
		var at: Vector3 = quest["waypoint"]
		var meters := Vector2(at.x - own.x, at.z - own.z).length()
		distance = "%d m" % roundi(meters) if meters < 1000 else "%.1f km" % (meters / 1000.0)
		var camera := _world_root.get_viewport().get_camera_3d() if _world_root != null else null
		if camera != null and meters > 3.0:
			waypoint = _screen_marker(camera, _waypoint_marker(client, at, own), distance)
	_hub_hud.tracker = {"title": str(quest.get("title", "")), "step": str(steps.back()) if not steps.is_empty() else "",
			"progress": progress, "distance": distance, "log_key": _key_name("quest_log")}
	_hub_hud.waypoint = waypoint


# Where a place is on screen, or the edge it lies past.
func _screen_marker(camera: Camera3D, at: Vector3, distance: String) -> Dictionary:
	var view := _hub_hud.size
	var center := view * 0.5
	var behind := camera.is_position_behind(at)
	var point := camera.unproject_position(at)
	var margin := 48.0 * view.y / 600.0
	var inside := Rect2(Vector2(margin, margin), view - Vector2(margin, margin) * 2)
	if not behind and inside.has_point(point):
		return {"at": point, "onscreen": true, "distance": distance}
	var direction := (point - center) if not behind else (center - point)
	if direction.length() < 0.001:
		direction = Vector2(0, 1)
	var angle := direction.angle()
	# Out along that way to the margin's edge.
	var half := inside.size * 0.5
	var scale := minf(half.x / maxf(absf(direction.x), 0.001), half.y / maxf(absf(direction.y), 0.001))
	return {"at": center + direction * scale, "onscreen": false, "angle": angle, "distance": distance}


# Battlefront's objective chevrons, for quests' objectives only: over the
# person a quest wants you to talk to (the chevrons as they are), and over
# what it wants killed (smaller). Every other creature and bandit wears one
# chevron: red if it will fight you, white if it leaves you be.
func _update_objective_markers(client: Object) -> void:
	if _world_root == null or _hub_hud == null:
		return
	var full: Texture2D = _hub_hud.art.texture("hud_objective_icon")
	if full == null:
		return
	if _single_chevron == null:
		_single_chevron = AtlasTexture.new()
		_single_chevron.atlas = full
		_single_chevron.region = Rect2(8, 1, 48, 22)  # the icon's top chevron
	var people := {}
	var wanted: Array = []
	for q: Dictionary in client.get_quests().get("active", []):
		if str(q.get("step_npc", "")) != "":
			people[str(q["step_npc"])] = true
		var names: PackedStringArray = q.get("targets", PackedStringArray())
		if not names.is_empty():
			wanted.append({"names": names, "at": q.get("waypoint", Vector3.ZERO), "radius": float(q.get("area_radius", 0.0))})
	var own: Vector3 = client.get_presented()["position"]
	var seen := {}
	for npc: Dictionary in client.get_quest_npcs():
		if people.has(str(npc["name"])):
			var key := "n%d" % int(npc["entity"])
			seen[key] = true
			_place_marker(key, full, 0.0012, GIVER_COLOR, (npc["position"] as Vector3) + Vector3.UP * (HOLO_HEIGHT + 0.85))
	for r: Dictionary in client.get_remotes():
		var kind := int(r.get("actor_kind", 0))
		if kind < 2 or not bool(r["alive"]) or (r["position"] as Vector3).distance_to(own) > MARKER_RANGE:
			continue
		var at: Vector3 = r["position"]
		var height := 1.0 + float(r.get("max_health", 100.0)) / 400.0 + (0.9 if kind == 3 else 0.0)
		var key := "m%d" % int(r["id"])
		seen[key] = true
		if _is_quest_prey(str(r["name"]), at, wanted):
			_place_marker(key, full, 0.0009, GIVER_COLOR, at + Vector3.UP * height)
		else:
			var fights := kind == 3 or not bool(r.get("passive", false))
			_place_marker(key, _single_chevron, 0.0012, RED_CHEVRON if fights else WHITE_CHEVRON, at + Vector3.UP * height)
	for t: Dictionary in client.get_mission_terminals():
		var at: Vector3 = t["position"]
		if at.distance_to(own) <= MARKER_RANGE:
			var key := "mt%d" % int(t["index"])
			seen[key] = true
			_place_marker(key, _single_chevron, 0.0012, _mission_terminal_color(str(t.get("kind", "general"))),
					at + Vector3.UP * _height_over_prop(key, at, CHEVRON_OVER_PROP, TERMINAL_CHEVRON_HEIGHT))
	for key: String in _marker_nodes.keys():
		if not seen.has(key):
			if is_instance_valid(_marker_nodes[key]):
				(_marker_nodes[key] as Node).queue_free()
			_marker_nodes.erase(key)


# Whether a quest wants this one killed: by name, in the step's area if it has one.
func _is_quest_prey(entity_name: String, at: Vector3, wanted: Array) -> bool:
	for w: Dictionary in wanted:
		if not (w["names"] as PackedStringArray).has(entity_name):
			continue
		var center: Vector3 = w["at"]
		if float(w["radius"]) <= 0.0 or Vector2(at.x - center.x, at.z - center.z).length() <= float(w["radius"]):
			return true
	return false


func _place_marker(key: String, texture: Texture2D, pixel_size: float, color: Color, at: Vector3) -> void:
	var sprite: Sprite3D = _marker_nodes.get(key)
	if sprite == null or not is_instance_valid(sprite):
		sprite = Sprite3D.new()
		sprite.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		sprite.fixed_size = true
		sprite.layers = TeamTags.LAYER
		_world_root.add_child(sprite)
		_marker_nodes[key] = sprite
	sprite.texture = texture
	sprite.pixel_size = pixel_size
	sprite.modulate = color
	sprite.position = at


# The world's holograms (Battlefront's insignia, as over soldiers in a match):
# the Rebels', the Empire's, and Battlefront's command post icon for
# everyone else; from whichever exported map has them. null if none does.
func _world_holograms() -> TeamTags:
	if _holograms != null or _holograms_looked:
		return _holograms
	_holograms_looked = true
	var maps := DirAccess.open("res://imported_assets/maps")
	if maps == null:
		return null
	for map_name: String in maps.get_directories():
		var models := "maps/%s/models/" % map_name
		if FileAccess.file_exists("res://imported_assets/" + models + "com_icon_alliance.gltf") \
				and FileAccess.file_exists("res://imported_assets/" + models + "com_icon_imperial.gltf"):
			var tags := TeamTags.new("res://imported_assets", {"images": {
					"alliance": models + "com_icon_alliance.gltf", "empire": models + "com_icon_imperial.gltf",
					"neutral": "hud/textures/hud_flag_icon.png", "quest": "gen:quest", "coins": "gen:coins",
					"chest": "gen:chest", "star": "gen:star", "cross": "gen:cross", "clone": "gen:clone",
					"wrench": "gen:wrench", "shuttle": "gen:shuttle", "fighter": "gen:fighter", "news": "gen:news"}})
			if tags.available():
				_holograms = tags
			break
	return _holograms


# Whose side a Galaxies character is on, by their template. ASSUMPTION:
# troopers and Imperials are the Empire's, rebels the Rebellion's, and
# everyone else neutral.
static func side_of_template(template: String) -> String:
	var t := template.to_lower()
	for imperial: String in ["stormtrooper", "imperial", "scout_trooper", "dark_trooper", "_imp_", "storm_commando"]:
		if t.contains(imperial):
			return "empire"
	return "alliance" if t.contains("rebel") else "neutral"


# A soldier, bandit or player's hologram in the world (red or blue, as in a
# match); false if there are no holograms.
func show_combat_hologram(id: int, model: Node3D, side: String, friend: bool, alive: bool,
		height := TeamTags.HEIGHT) -> bool:
	if _world_holograms() == null:
		return false
	_holograms.show(id, model, side, friend, alive, height)
	return true


func forget_hologram(id: int) -> void:
	if _holograms != null:
		_holograms.forget(id)


# Over each shop (vendors, quartermasters: green), quest giver (yellow) and
# workbench (purple), a hologram: its keeper's faction's insignia if they
# have one, else what the place is: an exclamation mark for a quest giver, a
# stack of coins for somewhere to sell, a chest for a shop, a wrench for
# crafting. A quest giver with work waiting for you later is grey; with none
# left, they wear none. Then all the world's holograms turned, sized and
# faded for the camera.
func _update_place_holograms(client: Object, delta: float) -> void:
	if _world_holograms() == null or _world_root == null:
		return
	var looks: PackedStringArray = client.get_looks()
	var by_name := {}
	var by_entity := {}
	for r: Dictionary in client.get_remotes():
		var look := int(r.get("look", 0))
		if look > 0 and look <= looks.size():
			by_name[str(r["name"])] = looks[look - 1]
			by_entity[int(r["id"])] = looks[look - 1]
	var seen := {}
	# Whether the camera is under a roof (a cloning facility's hologram comes inside).
	var eye_camera := _world_root.get_viewport().get_camera_3d()
	var eye: Vector3 = eye_camera.global_position if eye_camera != null else Vector3.ZERO
	var indoors := eye_camera != null and float(client.raycast(eye, eye + Vector3.UP * 30.0)) >= 0.0
	var vendors: Array = client.get_vendors()
	for vi in vendors.size():
		var v: Dictionary = vendors[vi]
		var kind := str(v.get("kind", ""))
		if kind == "travel_offworld":
			continue  # another planet's starport (Stage 13)
		if not is_shop(kind):
			# A service point: a star (trainers, gambling, data_terminal), a holoprojector (newsnet), coins (the bank, bazaar),
			# a shuttle (travel), a cross (clinics) or a ringed one, larger (cloning), in the terminals' blue.
			var symbol: String = {"trainer": "star", "bank": "coins", "bazaar": "coins", "newsnet": "news",
					"gambling": "star", "data_terminal": "star", "travel": "shuttle", "starport": "shuttle",
					"cloning": "clone"}.get(kind, "cross")
			var key := "service:%d" % vi
			var height := HOLO_HEIGHT
			if kind != "cloning" and kind != "trainer":
				# a terminal (only a trainer is someone): over its top
				height = _height_over_prop(key, v["position"], HOLO_OVER_PROP, HOLO_HEIGHT)
			var scale := PLACE_HOLO_SCALE
			if kind == "cloning":
				# A cloning facility is the building itself (its middle): its
				# hologram stands over its roof, to be seen from the street;
				# from inside (the camera under a roof, near), in its room,
				# where the roof doesn't hide it.
				if not _over_roofs.has(key):
					_over_roofs[key] = _height_over_roof(client, v["position"])
				height = _over_roofs[key]
				scale = CLONING_HOLO_SCALE
				if indoors and Vector2(eye.x - v["position"].x, eye.z - v["position"].z).length() < CLONING_INDOOR_REACH:
					height = HOLO_HEIGHT
					scale = PLACE_HOLO_SCALE
			_place_hologram(key, v["position"], symbol, TERMINAL_COLOR, seen, height, scale)
			continue
		var side := side_of_template(str(by_name.get(str(v["name"]), "")))
		var plain := "coins" if kind == "junk_dealer" else "chest"
		_place_hologram("vendor:%d" % vi, v["position"], side if side != "neutral" else plain, SHOP_COLOR, seen)
	var qms: Array = client.get_quartermasters()
	for qi in qms.size():
		var q: Dictionary = qms[qi]
		var faction := str(q.get("faction", ""))
		var sym := "chest"
		match faction:
			"rebellion": sym = "alliance"
			"empire": sym = "empire"
			"republic": sym = "republic"
			"separatists": sym = "cis"
		_place_hologram("qm:%d" % qi, q["position"], sym, SHOP_COLOR, seen)
	for ri in _bare_recruiters.size():
		var r: Dictionary = _bare_recruiters[ri]
		var rkey := "recruiter:%d" % ri
		_place_hologram(rkey, r["position"], "fighter" if str(r.get("kind", "")) == "space" else "neutral",
				TERMINAL_COLOR, seen, _height_over_prop(rkey, r["position"], HOLO_OVER_PROP, BARE_TERMINAL_HOLO_HEIGHT))
	var wbs: Array = client.get_workbenches()
	for wbi in wbs.size():
		var wb: Dictionary = wbs[wbi]
		var wkey := "workbench:%d" % wbi
		_place_hologram(wkey, wb["position"], "wrench", CRAFT_COLOR, seen,
				_height_over_prop(wkey, wb["position"], HOLO_OVER_PROP, STATION_HOLO_HEIGHT))
	var quests: Dictionary = client.get_quests()
	for npc: Dictionary in client.get_quest_npcs():
		if bool(npc.get("giver", false)):
			var state := _giver_state(quests, str(npc["name"]))
			if state == GIVER_DONE:
				continue
			var template := str(by_entity.get(int(npc["entity"]), by_name.get(str(npc["name"]), "")))
			var side := side_of_template(template)
			var icon := "quest" if state == GIVER_TURNIN else (side if side != "neutral" else "quest")
			var color := GIVER_TURNIN_COLOR if state == GIVER_TURNIN else (GIVER_COLOR if state == GIVER_READY else GIVER_WAITING_COLOR)
			_place_hologram("giver:" + str(npc["name"]), npc["position"], icon, color, seen)
	for key: String in _holo_anchors.keys():
		if not seen.has(key):
			_holograms.forget(key.hash())
			(_holo_anchors[key] as Node).queue_free()
			_holo_anchors.erase(key)
	var camera := _world_root.get_viewport().get_camera_3d()
	if camera != null:
		_holograms.update(camera, delta, client.raycast)


# Whether a quest giver has a turn-in ready (GIVER_TURNIN), work for you now
# (GIVER_READY), later or under way (GIVER_WAITING), or none left (GIVER_DONE).
func _giver_state(quests: Dictionary, giver: String) -> int:
	for q: Dictionary in quests.get("active", []):
		if str(q.get("step_npc", "")) == giver:
			var needed := int(q.get("needed", 0))
			if needed <= 0 or int(q.get("count", 0)) >= needed:
				return GIVER_TURNIN
	if (quests.get("ready_givers", PackedStringArray()) as PackedStringArray).has(giver):
		return GIVER_READY
	if (quests.get("waiting_givers", PackedStringArray()) as PackedStringArray).has(giver):
		return GIVER_WAITING
	return GIVER_DONE


# A hologram's height over `at` to stand `margin` m over the top of the
# terminal or station there (its model in the city, measured: they're from
# a knee-high console to a crafting station 2.9 m tall, and `at` is the
# floor's height round it, so not always its foot); `fallback` where the city
# has none there. Measured once per place.
func _height_over_prop(key: String, at: Vector3, margin: float, fallback: float) -> float:
	if _over_props.has(key):
		return _over_props[key]
	var world: Node = _world_root.get("_world") if _world_root != null else null
	if world == null or not world.has_meta(SwgCity.PROPS):
		return fallback  # (not loaded yet: not cached)
	var top := SwgCity.prop_top(world, at)
	if is_nan(top):  # (the server's spot a floor off: a house's cellar under it)
		top = SwgCity.prop_top(world, at, 1.2, 15.0, 15.0)
	var height := fallback if is_nan(top) else top - at.y + margin
	_over_props[key] = height
	return height


# Where a quest's waypoint marker stands: over its terminal or station if
# one is there, else over the place (2.2 m). A step's Position is known only
# across (y 0): it's put on the floor there first (from the player's height,
# 30 m up, down), once it can be.
func _waypoint_marker(client: Object, at: Vector3, own: Vector3) -> Vector3:
	var key := "%.1f,%.1f" % [at.x, at.z]
	if absf(at.y) >= 0.001:
		return at + Vector3.UP * 2.2  # a person (where they stand)
	if _waypoint_ground.has(key):
		at = _waypoint_ground[key]
	else:
		var from := Vector3(at.x, own.y + 30.0, at.z)
		var to := Vector3(at.x, own.y - 30.0, at.z)
		var t := float(client.raycast(from, to))
		if t < 0.0:
			return Vector3(at.x, own.y + 2.2, at.z)  # (not loaded there yet)
		at = from.lerp(to, t)
		_waypoint_ground[key] = at
	return at + Vector3.UP * _height_over_prop("waypoint:" + key, at, CHEVRON_OVER_PROP, 2.2)


# A hologram's height over `at` to stand over whatever roof is over it (none:
# HOLO_HEIGHT).
func _height_over_roof(client: Object, at: Vector3) -> float:
	var from := at + Vector3.UP * 40.0
	var to := at + Vector3.UP * 0.5
	var t := float(client.raycast(from, to))
	if t < 0.0:
		return HOLO_HEIGHT
	return maxf(HOLO_HEIGHT, from.lerp(to, t).y - at.y + 2.4)


func _place_hologram(key: String, at: Vector3, side: String, color: Color, seen: Dictionary,
		height := HOLO_HEIGHT, scale := PLACE_HOLO_SCALE) -> void:
	seen[key] = true
	var anchor: Node3D = _holo_anchors.get(key)
	if anchor == null:
		anchor = Node3D.new()
		_world_root.add_child(anchor)
		_holo_anchors[key] = anchor
	anchor.position = at
	_holograms.show(key.hash(), anchor, side, true, true, height, color, scale)


# A practice target's damage per second over the last few seconds (0 when
# it's not been hit), for the aim's readout.
func target_dps(id: int) -> float:
	return float((_target_nodes.get(id, {}) as Dictionary).get("dps", 0.0))


func at_lootable_corpse(client: Object) -> Dictionary:
	if client == null or not client.is_spawned():
		return {}
	var position: Vector3 = client.get_presented()["position"]
	var best: Dictionary = {}
	var best_d := 5.5
	for c: Dictionary in client.get_corpses():
		var items: Array = c.get("items", [])
		if items.is_empty():
			continue
		var at: Vector3 = c["position"]
		var d := Vector2(position.x - at.x, position.z - at.z).length()
		if d <= best_d:
			best_d = d
			best = c
	return best


func record_target_event(event: Dictionary) -> void:
	var vid := int(event.get("victim", 0))
	if not _target_nodes.has(vid):
		return
	var entry: Dictionary = _target_nodes[vid]
	if event.get("kind", "") == "hit":
		var dmg := float(event.get("damage", 0.0))
		entry["last_dmg"] = dmg
		entry["flash"] = 0.15
		var hits: Array = entry["hits"]
		hits.append({"t": Time.get_ticks_msec() / 1000.0, "dmg": dmg})


func take_notice_lines() -> PackedStringArray:
	var out := _notice_lines.duplicate()
	_notice_lines.clear()
	return out


func notice_text() -> String:
	return _notice


func notice_time() -> float:
	return _notice_time


func _sync_screen_data(client: Object) -> void:
	var status: Dictionary = client.get_player_status()
	var inv: Array = client.get_inventory()
	var los: Array = client.get_character_loadouts()
	var active := int(status.get("active_loadout", 0))
	if _inv_screen != null:
		_inv_screen.credits = int(status.get("credits", 0))
		_inv_screen.faction_points = int(status.get("faction_points", 0))
		_inv_screen.experience = int(status.get("experience", 0))
		_inv_screen.active_loadout_idx = active
		_inv_screen.inventory = inv
		_inv_screen.loadouts = los
		_inv_screen.refresh_data()
	if _qm_screen != null:
		_qm_screen.credits = int(status.get("credits", 0))
		_qm_screen.faction_points = int(status.get("faction_points", 0))
		_qm_screen.reputation = client.get_reputation()
		_qm_screen.inventory = inv
		var cur_lo: Dictionary = {}
		for lo: Dictionary in los:
			if int(lo.get("slot_index", -1)) == active:
				cur_lo = lo
				break
		_qm_screen.active_loadout = cur_lo
		if _qm_screen.visible:
			_qm_screen.queue_redraw()
	if _vendor_screen != null:
		_vendor_screen.credits = int(status.get("credits", 0))
		_vendor_screen.inventory = inv
		var cur_lo: Dictionary = {}
		for lo: Dictionary in los:
			if int(lo.get("slot_index", -1)) == active:
				cur_lo = lo
				break
		_vendor_screen.active_loadout = cur_lo
		if _vendor_screen.visible:
			_vendor_screen.queue_redraw()
	if _profession_screen != null:
		var progress := {}
		var taught := {}
		var known := {}
		for p: Dictionary in client.get_professions():
			progress[str(p["id"])] = int(p["xp"])
			taught[str(p["id"])] = int(p.get("trained", 0))
			known[str(p["id"])] = p.get("perks", [])
		_profession_screen.progress = progress
		_profession_screen.trained = taught
		_profession_screen.known = known
		if _profession_screen.visible:
			_profession_screen.queue_redraw()
	if _trainer_screen != null:
		var learned := {}
		for p: Dictionary in client.get_professions():
			var xp := int(p["xp"])
			var level := int(_store.profession_level(xp)["level"]) if _store != null else 1
			learned[str(p["id"])] = {"xp": xp, "level": level, "trained": int(p.get("trained", 0)),
					"paid": int(p.get("paid", 0)), "perks": p.get("perks", []), "paid_perks": p.get("paid_perks", [])}
		_trainer_screen.progress = learned
		_trainer_screen.known_counts = _known_count(client)
		_trainer_screen.credits = int(status.get("credits", 0))
		if _trainer_screen.visible:
			_trainer_screen.professions = _professions_of(_trainer_detail(client))
			_trainer_screen.queue_redraw()
	if _bank_screen != null:
		var bank: Dictionary = client.get_bank()
		_bank_screen.credits = int(status.get("credits", 0))
		_bank_screen.bank_credits = int(bank.get("credits", 0))
		_bank_screen.inventory = inv
		_bank_screen.bank_items = bank.get("items", [])
		_bank_screen.catalog = _catalog
		if _bank_screen.visible:
			_bank_screen.queue_redraw()
	if _workbench_screen != null:
		_workbench_screen.materials = client.get_materials()
		_workbench_screen.presets = client.get_presets()
		if client.has_method("get_blueprints"):
			_workbench_screen.blueprints = client.get_blueprints()
		_workbench_screen.inventory = inv
		_workbench_screen.loadouts = los
		var prof_xp := {}
		var prof_perks := {}
		for p: Dictionary in client.get_professions():
			prof_xp[str(p["id"])] = int(p["xp"])
			prof_perks[str(p["id"])] = PackedStringArray(p.get("perks", []))
		_workbench_screen.professions = prof_xp
		_workbench_screen.perks = prof_perks
		if _workbench_screen.visible:
			_workbench_screen.queue_redraw()


# How many perks and milestones the character knows in each pool, combat
# and noncombat (docs/PERKS.md: at most the store's perk cap in each).
func _known_count(client: Object) -> Dictionary:
	var counts := {"combat": 0, "noncombat": 0}
	for p: Dictionary in client.get_professions():
		var id := str(p["id"])
		for def: Dictionary in _professions:
			if str(def.get("id", "")) != id:
				continue
			var pool := str(def.get("pool", "combat"))
			var count := (p.get("perks", []) as Array).size()
			for m: Dictionary in def.get("milestones", []):
				if int(m.get("level", 0)) <= int(p.get("trained", 0)):
					count += 1
			counts[pool] = int(counts.get(pool, 0)) + count
	return counts


# What the trainer whose screen is open teaches (their `detail`).
func _trainer_detail(client: Object) -> String:
	for v: Dictionary in client.get_vendors():
		if str(v["name"]) == _trained_by and str(v["kind"]) == "trainer":
			return str(v.get("detail", ""))
	return ""


func _update_targets(client: Object, delta: float) -> void:
	var targets: Array = client.get_targets()
	var seen := {}
	var now_s := Time.get_ticks_msec() / 1000.0
	for t: Dictionary in targets:
		var id := int(t["id"])
		seen[id] = true
		if not _target_nodes.has(id):
			var root := Node3D.new()
			var body := Node3D.new()
			root.add_child(body)
			var mat := StandardMaterial3D.new()
			mat.albedo_color = Color(0.85, 0.28, 0.22)
			var torso := MeshInstance3D.new()
			var tmesh := BoxMesh.new()
			tmesh.size = Vector3(0.55, 1.15, 0.25)
			torso.mesh = tmesh
			torso.material_override = mat
			torso.position = Vector3.UP * 0.85
			body.add_child(torso)
			var head := MeshInstance3D.new()
			var hmesh := SphereMesh.new()
			hmesh.radius = 0.18
			hmesh.height = 0.36
			head.mesh = hmesh
			head.material_override = mat
			head.position = Vector3.UP * 1.62
			body.add_child(head)
			_world_root.add_child(root)
			_target_nodes[id] = {"root": root, "body": body, "mat": mat,
					"hits": [], "last_dmg": 0.0, "flash": 0.0}

		var entry: Dictionary = _target_nodes[id]
		var root: Node3D = entry["root"]
		root.position = t["position"]
		var body: Node3D = entry["body"]
		var alive := bool(t["alive"])
		body.rotation.x = lerpf(body.rotation.x, 0.0 if alive else -PI * 0.48, minf(1.0, 12.0 * delta))
		entry["flash"] = maxf(0.0, float(entry["flash"]) - delta)
		(entry["mat"] as StandardMaterial3D).albedo_color = Color(1.0, 0.85, 0.3) if float(entry["flash"]) > 0.0 else Color(0.85, 0.28, 0.22)
		var hits: Array = entry["hits"]
		while not hits.is_empty() and now_s - float(hits[0]["t"]) > 5.0:
			hits.pop_front()
		var total := 0.0
		for h: Dictionary in hits:
			total += float(h["dmg"])
		var span := maxf(1.0, now_s - float(hits[0]["t"])) if hits.size() > 1 else 1.0
		var dps := total / span if not hits.is_empty() else 0.0
		entry["dps"] = dps
		if _world_holograms() != null:
			_holograms.show(TARGET_HOLO_ID + id, root, "neutral", false, alive)

	for id: int in _target_nodes.keys():
		if not seen.has(id):
			var r: Node = _target_nodes[id]["root"]
			if is_instance_valid(r):
				r.queue_free()
			_target_nodes.erase(id)
			forget_hologram(TARGET_HOLO_ID + id)


func update_hub(client: Object, world: Node3D, delta: float, pause_visible: bool,
		center_label: Label, match_label: RichTextLabel) -> void:
	_notice_time = maxf(0.0, _notice_time - delta)
	_level_up_time = maxf(0.0, _level_up_time - delta)
	if _level_up_time <= 0.0:
		if not _level_up_queue.is_empty():
			_level_up_active = _level_up_queue.pop_front()
			_level_up_time = 5.0
		else:
			_level_up_active = {}
	if client == null or not client.is_spawned():
		return
	add_recruiters(client, world)
	var in_hub: bool = client.is_spawned() and not client.get_recruiters().is_empty() and client.get_match().is_empty()
	if in_hub:
		_update_targets(client, delta)
		_update_corpse_markers(client)
		_update_place_holograms(client, delta)
		if _quest_screen != null and _hub_hud != null:
			_update_quest_hud(client)
	_check_profession_level_ups(client)
	var status: Dictionary = client.get_player_status()
	if int(status.get("notice_id", 0)) != _notice_id:
		_notice_id = int(status["notice_id"])
		var incoming := str(status.get("notice", ""))
		if incoming.contains(" reached level "):
			_queue_level_up("PROFESSION LEVEL UP", incoming)
			_notice = ""
			_notice_time = 0.0
		else:
			_notice = incoming
			_notice_time = 10.0
		if incoming != "":
			print("notice: ", incoming)
			_notice_lines.append(incoming)
			if _qm_screen != null and _qm_screen.visible:
				_qm_screen.feedback = incoming
			if _vendor_screen != null and _vendor_screen.visible:
				_vendor_screen.feedback = incoming
			if _workbench_screen != null and _workbench_screen.visible:
				_workbench_screen.feedback = incoming
			if _trainer_screen != null and _trainer_screen.visible:
				_trainer_screen.feedback = incoming
			if _bank_screen != null and _bank_screen.visible:
				_bank_screen.feedback = incoming
			if _loot_screen != null and _loot_screen.visible:
				_loot_screen.feedback = incoming

	_sync_screen_data(client)

	if _hub_hud == null or _terminal_screen == null:
		if not client.get_recruiters().is_empty() and client.get_match().is_empty():
			_update_hub_text_hud(client, center_label, match_label)
		return

	var terminal: Dictionary = at_recruiter(client) if in_hub else {}
	var qm: Dictionary = at_quartermaster(client) if in_hub else {}
	var vendor: Dictionary = at_vendor(client) if in_hub else {}
	var service: Dictionary = at_service(client) if in_hub else {}
	var wb: Dictionary = at_workbench(client) if in_hub else {}
	var corpse: Dictionary = at_lootable_corpse(client) if in_hub else {}
	var quest_npc: Dictionary = at_quest_npc(client) if in_hub else {}
	var mission_terminal: Dictionary = at_mission_terminal(client) if in_hub else {}
	if _terminal_screen.visible and terminal.is_empty():
		close_all_screens()
	if _qm_screen != null and _qm_screen.visible and qm.is_empty():
		close_all_screens()
	if _vendor_screen != null and _vendor_screen.visible and vendor.is_empty():
		close_all_screens()
	if (_trainer_screen != null and _trainer_screen.visible or _bank_screen != null and _bank_screen.visible or _info_screen != null and _info_screen.visible) and service.is_empty():
		close_all_screens()
	if _loot_screen != null and _loot_screen.visible:
		var cid := int(_loot_screen.corpse.get("entity_id", 0))
		var found_corpse: Dictionary = {}
		for c: Dictionary in client.get_corpses():
			if int(c.get("entity_id", 0)) == cid and not (c.get("items", []) as Array).is_empty():
				found_corpse = c
				break
		if found_corpse.is_empty():
			close_all_screens()
		else:
			var own_pos: Vector3 = client.get_presented()["position"]
			var cpos: Vector3 = found_corpse["position"]
			if Vector2(own_pos.x - cpos.x, own_pos.z - cpos.z).length() > 7.0:
				close_all_screens()
			else:
				_loot_screen.corpse = found_corpse
				_loot_screen.queue_redraw()
	var factions: PackedStringArray = client.get_factions()
	var queued: int = int(status.get("team", -1)) if status.get("queue", "none") == "queued" else -1
	var sides: Array = []
	for i in factions.size():
		sides.append({"name": factions[i], "icon": _side_icon(factions[i], i)})
	if _terminal_screen.visible:
		_terminal_screen.title = str(terminal["name"])
		_terminal_screen.kind = str(terminal.get("kind", "ground"))
		_terminal_screen.sides = sides
		_terminal_screen.queued = queued
		_terminal_screen.battlefield = str(status.get("battlefield", ""))
		_terminal_screen.starts_in = float(status.get("starts_in", 0))
		_terminal_screen.in_queue = int(status.get("in_queue", 0))
		_terminal_screen.keys = {"leave": _key_name("leave_queue"), "close": "Esc"}
		_terminal_screen.queue_redraw()
	_hub_hud.visible = in_hub and not (pause_visible or is_any_screen_open())
	var tip := ""
	if not terminal.is_empty() and queued < 0:
		tip = "Press %s to use the %s" % [_key_name("interact"), terminal["name"]]
	elif not qm.is_empty():
		tip = "Press %s to talk to %s" % [_key_name("interact"), qm["name"]]
	elif not vendor.is_empty():
		tip = "Press %s to trade with %s" % [_key_name("interact"), vendor["name"]]
	elif not service.is_empty():
		var verb := {"trainer": "talk to", "bank": "use the", "clinic": "get patched up at the", "cloning": "keep your clone at the", "travel": "buy a shuttle ticket at the", "starport": "buy passage at the"}
		tip = "Press %s to %s %s" % [_key_name("interact"), verb.get(str(service["kind"]), "use"), service["name"]]
	elif not wb.is_empty():
		tip = "Press %s to use %s" % [_key_name("interact"), wb["name"]]
	elif not quest_npc.is_empty():
		tip = "Press %s to talk to %s" % [_key_name("interact"), quest_npc["name"]]
	elif not mission_terminal.is_empty():
		tip = "Press %s to use the %s" % [_key_name("interact"), mission_terminal["name"]]
	elif not corpse.is_empty():
		var n_items := (corpse.get("items", []) as Array).size()
		tip = "Press %s to loot %s (%d %s)" % [_key_name("interact"), corpse["name"], n_items, "item" if n_items == 1 else "items"]
	_hub_hud.tooltip = tip
	_hub_hud.queue = {} if queued < 0 or queued >= sides.size() else {"side": sides[queued]["name"],
			"icon": sides[queued]["icon"], "battlefield": str(status.get("battlefield", "")),
			"starts_in": float(status.get("starts_in", 0)), "in_queue": int(status.get("in_queue", 0)),
			"leave_key": _key_name("leave_queue")}
	var los: Array = client.get_character_loadouts()
	var active_idx := int(status.get("active_loadout", 0))
	var lo_name := "Standard Issue"
	if active_idx >= 0 and active_idx < los.size():
		lo_name = str((los[active_idx] as Dictionary).get("name", lo_name))
	_hub_hud.info = {
		"credits": int(status.get("credits", 0)),
		"fp": int(status.get("faction_points", 0)),
		"level": int(status.get("combat_level", 1)),
		"loadout_name": lo_name,
		"inv_key": _key_name("inventory"),
	}
	var sw_slot := int(status.get("switching_loadout", -1))
	if sw_slot >= 0:
		var sw_name := "Loadout %d" % (sw_slot + 1)
		if sw_slot < los.size():
			sw_name = str((los[sw_slot] as Dictionary).get("name", sw_name))
		_hub_hud.switching = {"name": sw_name, "progress": float(status.get("switch_progress", 0.0))}
	else:
		_hub_hud.switching = {}
	_hub_hud.notice = _notice
	_hub_hud.notice_alpha = clampf(_notice_time, 0.0, 1.0)
	_hub_hud.level_up = {} if _level_up_active.is_empty() else {
		"title": _level_up_active.get("title", "PROFESSION LEVEL UP"),
		"detail": _level_up_active.get("detail", ""),
		"alpha": clampf(_level_up_time, 0.0, 1.0),
	}
	if _scanner != null and _hub_hud != null:
		if in_hub and _scanner.active:
			var scan: Dictionary = _scanner.update_scanner(client, _crafting_store, client.get_presented()["position"], delta)
			if not scan.is_empty():
				scan["extract_progress"] = float(status.get("extract_progress", -1.0))
			_hub_hud.scanner = scan
		else:
			if _scanner.active:
				_scanner.active = false
				_scanner.visible = false
			_hub_hud.scanner = {}
	_hub_hud.queue_redraw()


# The grade color of a corpse's best item (white if it holds nothing graded).
func _corpse_loot_color(c: Dictionary) -> Color:
	if _catalog_by_id.size() != _catalog.size():
		_catalog_by_id.clear()
		for d: Dictionary in _catalog:
			_catalog_by_id[str(d.get("id", ""))] = d
	var best := -1
	for item: Dictionary in c.get("items", []):
		best = maxi(best, ItemGrade.shown(_catalog_by_id.get(str(item.get("def_id", "")), {}), item))
	return ItemGrade.color_of(best)


# A beam of light, in its loot's color, standing out of a corpse: brightest at
# the ground, fading out some 6 m up.
static func _make_loot_beam(color: Color) -> MeshInstance3D:
	var mesh := CylinderMesh.new()
	mesh.top_radius = 0.05
	mesh.bottom_radius = 0.11
	mesh.height = LOOT_BEAM_HEIGHT
	mesh.radial_segments = 12
	mesh.rings = 1
	mesh.cap_top = false
	mesh.cap_bottom = false
	var fade := Gradient.new()
	fade.set_color(0, Color(1, 1, 1, 0))  # the top (the mesh's v = 0)
	fade.set_color(1, Color(1, 1, 1, 1))
	var tex := GradientTexture2D.new()
	tex.gradient = fade
	tex.fill_from = Vector2(0, 0)
	tex.fill_to = Vector2(0, 1)
	tex.width = 4
	tex.height = 64
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mat.no_depth_test = false
	mat.albedo_texture = tex
	mat.albedo_color = Color(color.r, color.g, color.b, 0.75)
	var beam := MeshInstance3D.new()
	beam.mesh = mesh
	beam.material_override = mat
	beam.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	return beam


# A lootable corpse: a beam of light in its best loot's grade color (white for
# anything ungraded), and "Loot" under, in Battlefront's font.
func _update_corpse_markers(client: Object) -> void:
	if _world_root == null:
		return
	var seen := {}
	for c: Dictionary in client.get_corpses():
		var cid := int(c.get("entity_id", 0))
		if (c.get("items", []) as Array).is_empty():
			continue
		seen[cid] = true
		var color := _corpse_loot_color(c)
		if _corpse_nodes.has(cid):
			# Taking its best item turns the beam to the next best's color.
			var entry: Dictionary = _corpse_nodes[cid]
			if entry.get("color", color) != color:
				entry["color"] = color
				var beam_node: Variant = entry.get("icon")
				if beam_node is MeshInstance3D and is_instance_valid(beam_node):
					var mat := (beam_node as MeshInstance3D).material_override as StandardMaterial3D
					mat.albedo_color = Color(color.r, color.g, color.b, 0.75)
				var label_node: Variant = entry.get("label")
				if label_node is Label3D and is_instance_valid(label_node):
					(label_node as Label3D).modulate = color
		if not _corpse_nodes.has(cid):
			var pos: Vector3 = c["position"]
			var art: RefCounted = _hub_hud.art if _hub_hud != null else null
			var icon := _make_loot_beam(color)
			icon.position = pos + Vector3.UP * (LOOT_BEAM_HEIGHT * 0.5)
			var label := Label3D.new()
			label.text = art.localize("common.loot") if art != null and art.localize("common.loot") != "common.loot" else "Loot"
			label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
			label.fixed_size = true
			label.pixel_size = 0.0011
			label.font_size = 32
			if art != null and art.godot_font("gamefont_small") != null:
				label.font = art.godot_font("gamefont_small")
			label.outline_size = 0
			label.modulate = color
			label.position = pos + Vector3.UP * 0.95
			_world_root.add_child(icon)
			_world_root.add_child(label)
			_corpse_nodes[cid] = {"icon": icon, "label": label, "color": color}
	for cid: int in _corpse_nodes.keys():
		if not seen.has(cid):
			var entry: Dictionary = _corpse_nodes[cid]
			for k in ["icon", "label"]:
				var n: Variant = entry.get(k)
				if n is Node and is_instance_valid(n):
					(n as Node).queue_free()
			_corpse_nodes.erase(cid)


func _update_hub_text_hud(client: Object, center_label: Label, match_label: RichTextLabel) -> void:
	var factions: PackedStringArray = client.get_factions()
	var status: Dictionary = client.get_player_status()
	if status.get("queue", "none") == "queued" and factions.size() >= 2:
		var where := str(status.get("battlefield", ""))
		var tidx := int(status["team"])
		var tname := str(factions[tidx]) if tidx >= 0 and tidx < factions.size() else ""
		match_label.text = "[center]Queued for the %s: %d in the queue, the battle%s starts in %d s\n[X] leave the queue[/center]" % [
				tname, status["in_queue"], " on " + where if where != "" else "",
				ceili(status["starts_in"])]
		return
	match_label.text = ""
	var terminal := at_recruiter(client)
	if not terminal.is_empty() and factions.size() >= 2 and center_label.text == "":
		var opts: PackedStringArray = []
		for i in mini(factions.size(), 4):
			opts.append("[%d] %s" % [i + 1, str(factions[i])])
		center_label.text = "%s\n%s" % [terminal["name"], "   ".join(opts)]


func _mission_terminal_color(kind: String) -> Color:
	match kind:
		"artisan":
			return CRAFT_COLOR
		"bounty":
			return Color(1.0, 0.58, 0.2)
		"entertainer":
			return Color(0.95, 0.55, 0.9)
		"scout":
			return Color(0.45, 0.92, 0.5)
		"imperial":
			return Color(1.0, 0.35, 0.3)
		"rebel":
			return Color(0.35, 0.65, 1.0)
	return TERMINAL_COLOR


func _side_icon(side: String, index: int) -> String:
	var sname := side.to_lower()
	if sname.contains("rebel"):
		return "all_icon"
	if sname.contains("empire") or sname.contains("imperial"):
		return "imp_icon"
	if sname.contains("republic"):
		return "rep_icon"
	if sname.contains("separatist") or sname.contains("cis"):
		return "cis_icon"
	return "all_icon" if index == 0 else "imp_icon"


func _key_name(action: String) -> String:
	return Settings.binding_name(_settings.bindings[action]) if _settings != null and _settings.bindings.has(action) else action


func _queue_level_up(title: String, detail: String) -> void:
	for queued: Dictionary in _level_up_queue:
		if str(queued.get("detail", "")) == detail:
			return
	if str(_level_up_active.get("detail", "")) == detail and _level_up_time > 0.0:
		return
	if _level_up_active.is_empty():
		_level_up_active = {"title": title, "detail": detail}
		_level_up_time = 5.0
	else:
		_level_up_queue.append({"title": title, "detail": detail})


func _check_profession_level_ups(client: Object) -> void:
	if _store == null:
		return
	var profs: Array = client.get_professions()
	if profs.is_empty():
		return
	var initial := _prof_levels.is_empty()
	var names := {}
	for d: Dictionary in _professions:
		names[str(d.get("id", ""))] = str(d.get("name", d.get("id", "")))
	for p: Dictionary in profs:
		var id := str(p.get("id", ""))
		var lv := int(_store.profession_level(int(p.get("xp", 0))).get("level", 1))
		if not initial and _prof_levels.has(id) and lv > int(_prof_levels[id]):
			var pname := str(names.get(id, id.capitalize()))
			_queue_level_up("PROFESSION LEVEL UP", "%s reached level %d" % [pname, lv])
		_prof_levels[id] = lv
