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
const MultiScanner3D = preload("res://rendering/multi_scanner.gd")

const TALK_REACH := 4.5     # meters from a quest NPC to talk to them (the server allows 6)
const OFFER_REACH := 8.0    # an offer closes when you walk further than this from it
const GIVER_COLOR := Color(1.0, 0.8, 0.35)
const TERMINAL_COLOR := Color(0.45, 0.85, 1.0)

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
signal craft_requested(schematic_id: String, resource_ids: PackedStringArray, enhancement_ids: PackedInt64Array, exp_quality_pct: int, batch_count: int)
signal preset_saved(name: String, schematic_id: String, resource_ids: PackedStringArray, enhancement_defs: PackedStringArray, exp_quality_pct: int)
signal preset_deleted(name: String)
signal work_order_fulfilled(order_id: String)
signal item_salvaged(item_id: int)

var _hub_hud: HubHud
var _terminal_screen: WarTerminalScreen
var _qm_screen: QuartermasterScreen
var _inv_screen: InventoryScreen
var _loot_screen: LootScreen
var _vendor_screen: VendorScreen
var _quest_screen: QuestScreen
var _profession_screen: ProfessionScreen
var _workbench_screen: WorkbenchScreen
var _scanner: MultiScanner3D
var _shown_loadouts: Array = []    # the loadouts the profession screen was last given
var _settings: Settings
var _world_root: Node3D
var _recruiter_nodes: Array = []
var _qm_nodes: Array = []
var _vendor_nodes: Array = []
var _workbench_nodes: Array = []
var _corpse_nodes: Dictionary = {}  # corpse_entity -> {ring, label}
var _target_nodes: Dictionary = {}  # target_id -> {root, body, label, hits: Array[{t, dmg}], last_dmg, flash}
var _notice_id := 0
var _notice := ""
var _notice_time := 0.0
var _notice_lines: PackedStringArray = []
var _catalog: Array = []
var _store: Object = null
var _crafting_store: Object = null
var _tracked := ""                 # the tracked quest's id
var _known_quests := {}            # active quest ids last seen (a new one is tracked)
var _offer_serial := -1            # the offer serial when we asked; -1: not waiting
var _offer_at := Vector3.ZERO      # where the offer was asked for
var _giver_nodes: Dictionary = {}  # npc entity -> Sprite3D over their head


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
	_terminal_screen.leave_requested.connect(func() -> void: queue_requested.emit(-1, "standard"))
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
	_profession_screen.loadout_saved.connect(func(slot: int, lo: Dictionary) -> void: loadout_saved.emit(slot, lo))
	_profession_screen.closed.connect(close_all_screens)
	layer.add_child(_profession_screen)

	_workbench_screen = WorkbenchScreen.new(art)
	_workbench_screen.visible = false
	_workbench_screen.craft_requested.connect(func(sid: String, rids: PackedStringArray, eids: PackedInt64Array, qpct: int, cnt: int) -> void:
		craft_requested.emit(sid, rids, eids, qpct, cnt))
	_workbench_screen.preset_saved.connect(func(pname: String, sid: String, rids: PackedStringArray, edefs: PackedStringArray, qpct: int) -> void:
		preset_saved.emit(pname, sid, rids, edefs, qpct))
	_workbench_screen.preset_deleted.connect(func(pname: String) -> void: preset_deleted.emit(pname))
	_workbench_screen.work_order_fulfilled.connect(func(oid: String) -> void: work_order_fulfilled.emit(oid))
	_workbench_screen.item_salvaged.connect(func(iid: int) -> void: item_salvaged.emit(iid))
	_workbench_screen.closed.connect(close_all_screens)
	layer.add_child(_workbench_screen)

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
	if _profession_screen != null:
		_profession_screen.professions = professions
		_profession_screen.store = store
		_profession_screen.slots = int(store.get_specialization_slots())


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
			or (_workbench_screen != null and _workbench_screen.visible)


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
	var vendor := at_vendor(client)
	if _vendor_screen != null and not vendor.is_empty():
		_sync_screen_data(client)
		_vendor_screen.open_vendor(str(vendor.get("name", "Vendor")), str(vendor.get("kind", "gear_vendor")))
		return true
	var wb := at_workbench(client)
	if _workbench_screen != null and not wb.is_empty():
		_sync_screen_data(client)
		_workbench_screen.open_screen()
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
		_loot_screen.visible = true
		_loot_screen.queue_redraw()
		return true
	return false


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
	_shown_loadouts = []
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


func toggle_workbench(client: Object) -> bool:
	if _workbench_screen == null or client == null or not client.is_spawned():
		return false
	if _workbench_screen.visible:
		close_all_screens()
		return false
	close_all_screens()
	_sync_screen_data(client)
	_workbench_screen.open_screen()
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
	_scanner.extract_cooldown = 1.5
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
	_offer_serial = -1
	if was_open:
		terminal_closed.emit()


func clear_recruiters() -> void:
	for node: Node in _recruiter_nodes:
		if is_instance_valid(node):
			node.queue_free()
	_recruiter_nodes.clear()
	for node: Node in _qm_nodes:
		if is_instance_valid(node):
			node.queue_free()
	_qm_nodes.clear()
	for node: Node in _workbench_nodes:
		if is_instance_valid(node):
			node.queue_free()
	_workbench_nodes.clear()
	for node: Node in _vendor_nodes:
		if is_instance_valid(node):
			node.queue_free()
	_vendor_nodes.clear()
	for entry: Dictionary in _corpse_nodes.values():
		for k in ["ring", "label"]:
			var n: Variant = entry.get(k)
			if n is Node and is_instance_valid(n):
				(n as Node).queue_free()
	_corpse_nodes.clear()
	for node: Node in _giver_nodes.values():
		if is_instance_valid(node):
			node.queue_free()
	_giver_nodes.clear()
	for entry: Dictionary in _target_nodes.values():
		var r: Variant = entry.get("root")
		if r is Node and is_instance_valid(r):
			(r as Node).queue_free()
	_target_nodes.clear()


func on_world_arrived() -> void:
	clear_recruiters()
	close_all_screens()
	_notice_id = 0


func add_recruiters(client: Object, world: Node3D) -> void:
	if not _recruiter_nodes.is_empty() or client == null or world == null:
		return
	var recruiters: Array = client.get_recruiters()
	if not recruiters.is_empty():
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.albedo_color = Color(1.0, 0.8, 0.25)
		var terminals: Array[Vector3] = []
		for node: Node in world.get_children():
			if node.has_meta("recruiter"):
				terminals.append((node as Node3D).position)
		for recruiter: Dictionary in recruiters:
			var position: Vector3 = recruiter["position"]
			var drawn := terminals.any(func(at: Vector3) -> bool:
				return Vector2(at.x - position.x, at.z - position.z).length() < 1.5)
			var pillar := MeshInstance3D.new()
			var box := BoxMesh.new()
			box.size = Vector3(0.6, 2.2, 0.6)
			pillar.mesh = box
			pillar.material_override = material
			pillar.position = position + Vector3.UP * 1.1
			var ring := MeshInstance3D.new()
			var torus := TorusMesh.new()
			torus.outer_radius = float(recruiter["radius"])
			torus.inner_radius = float(recruiter["radius"]) - 0.15
			ring.mesh = torus
			ring.material_override = material
			ring.position = position + Vector3.UP * 0.1
			var label := Label3D.new()
			label.text = str(recruiter["name"])
			label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
			label.fixed_size = true
			label.pixel_size = 0.0015
			label.font_size = 28
			label.outline_size = 8
			label.modulate = material.albedo_color
			label.position = position + Vector3.UP * 3.2
			for node: Node3D in [pillar, ring, label]:
				if drawn:
					node.free()
					continue
				_world_root.add_child(node)
				_recruiter_nodes.append(node)

	if _qm_nodes.is_empty():
		for qm: Dictionary in client.get_quartermasters():
			var pos: Vector3 = qm["position"]
			var col := Color(0.35, 0.75, 1.0) if str(qm.get("faction", "")) == "rebellion" else Color(1.0, 0.45, 0.3)
			var ring := MeshInstance3D.new()
			var torus := TorusMesh.new()
			torus.outer_radius = float(qm["radius"])
			torus.inner_radius = float(qm["radius"]) - 0.12
			var mat := StandardMaterial3D.new()
			mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			mat.albedo_color = col
			ring.mesh = torus
			ring.material_override = mat
			ring.position = pos + Vector3.UP * 0.08
			var label := Label3D.new()
			label.text = str(qm["name"])
			label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
			label.fixed_size = true
			label.pixel_size = 0.0014
			label.font_size = 24
			label.outline_size = 8
			label.modulate = col
			label.position = pos + Vector3.UP * 2.5
			_world_root.add_child(ring)
			_world_root.add_child(label)
			_qm_nodes.append(ring)
			_qm_nodes.append(label)

	if _vendor_nodes.is_empty():
		for v: Dictionary in client.get_vendors():
			var pos: Vector3 = v["position"]
			var col := Color(0.95, 0.78, 0.28) if str(v.get("kind", "")) == "junk_dealer" else Color(0.35, 0.92, 0.65)
			var ring := MeshInstance3D.new()
			var torus := TorusMesh.new()
			torus.outer_radius = float(v["radius"])
			torus.inner_radius = float(v["radius"]) - 0.12
			var mat := StandardMaterial3D.new()
			mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			mat.albedo_color = col
			ring.mesh = torus
			ring.material_override = mat
			ring.position = pos + Vector3.UP * 0.08
			var label := Label3D.new()
			label.text = str(v["name"])
			label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
			label.fixed_size = true
			label.pixel_size = 0.0014
			label.font_size = 24
			label.outline_size = 8
			label.modulate = col
			label.position = pos + Vector3.UP * 2.5
			_world_root.add_child(ring)
			_world_root.add_child(label)
			_vendor_nodes.append(ring)
			_vendor_nodes.append(label)

	if _workbench_nodes.is_empty():
		for wb: Dictionary in client.get_workbenches():
			var pos: Vector3 = wb["position"]
			var col := Color(0.35, 0.85, 1.0)
			var ring := MeshInstance3D.new()
			var torus := TorusMesh.new()
			torus.outer_radius = float(wb["radius"])
			torus.inner_radius = float(wb["radius"]) - 0.12
			var mat := StandardMaterial3D.new()
			mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			mat.albedo_color = col
			ring.mesh = torus
			ring.material_override = mat
			ring.position = pos + Vector3.UP * 0.08
			var label := Label3D.new()
			label.text = str(wb["name"])
			label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
			label.fixed_size = true
			label.pixel_size = 0.0014
			label.font_size = 24
			label.outline_size = 8
			label.modulate = col
			label.position = pos + Vector3.UP * 2.5
			_world_root.add_child(ring)
			_world_root.add_child(label)
			_workbench_nodes.append(ring)
			_workbench_nodes.append(label)


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
	for v: Dictionary in client.get_vendors():
		var at: Vector3 = v["position"]
		if Vector2(position.x - at.x, position.z - at.z).length() <= float(v["radius"]):
			return v
	return {}


func at_workbench(client: Object) -> Dictionary:
	if client == null or not client.is_spawned():
		return {}
	var position: Vector3 = client.get_presented()["position"]
	for wb: Dictionary in client.get_workbenches():
		var at: Vector3 = wb["position"]
		if Vector2(position.x - at.x, position.z - at.z).length() <= float(wb["radius"]):
			return wb
	return {}


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
		if _quest_screen.visible and _quest_screen.mode == "offers":
			if _quest_screen.waiting and int(quests.get("offer_serial", 0)) != _offer_serial:
				_quest_screen.waiting = false
				_offer_serial = -1
			if not _quest_screen.waiting:
				_quest_screen.offers = quests.get("offers", [])
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
	for npc: Dictionary in client.get_quest_npcs():
		if bool(npc.get("giver", false)):
			var at: Vector3 = npc["position"]
			out.append({"position": Vector2(at.x, at.z), "kind": "giver", "color": GIVER_COLOR})
	for t: Dictionary in client.get_mission_terminals():
		var at: Vector3 = t["position"]
		out.append({"position": Vector2(at.x, at.z), "kind": "terminal", "color": TERMINAL_COLOR})
	return out


func _update_quest_hud(client: Object) -> void:
	var quests := _sync_quests(client)
	if _quest_screen.visible and _quest_screen.mode == "offers":
		var here: Vector3 = client.get_presented()["position"]
		if Vector2(here.x - _offer_at.x, here.z - _offer_at.z).length() > OFFER_REACH:
			close_all_screens()
	_update_giver_markers(client)
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
			waypoint = _screen_marker(camera, at + Vector3.UP * 2.2, distance)
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


# Battlefront's objective chevrons over each quest giver's head.
func _update_giver_markers(client: Object) -> void:
	if _world_root == null or not _giver_nodes.is_empty():
		return
	var texture: Texture2D = _hub_hud.art.texture("hud_objective_icon") if _hub_hud != null else null
	if texture == null:
		return
	for npc: Dictionary in client.get_quest_npcs():
		if not bool(npc.get("giver", false)):
			continue
		var sprite := Sprite3D.new()
		sprite.texture = texture
		sprite.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		sprite.fixed_size = true
		sprite.pixel_size = 0.0012
		sprite.modulate = GIVER_COLOR
		sprite.position = (npc["position"] as Vector3) + Vector3.UP * 2.45
		_world_root.add_child(sprite)
		_giver_nodes[int(npc["entity"])] = sprite


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
		for p: Dictionary in client.get_professions():
			progress[str(p["id"])] = int(p["xp"])
		_profession_screen.progress = progress
		_profession_screen.active_loadout = active
		# What the server last said; the screen keeps its own change until the answer.
		if los != _shown_loadouts:
			_shown_loadouts = los
			_profession_screen.loadouts = los.duplicate(true)
		if _profession_screen.visible:
			_profession_screen.queue_redraw()
	if _workbench_screen != null:
		_workbench_screen.materials = client.get_materials()
		_workbench_screen.presets = client.get_presets()
		_workbench_screen.inventory = inv
		_workbench_screen.loadouts = los
		var prof_xp := {}
		for p: Dictionary in client.get_professions():
			prof_xp[str(p["id"])] = int(p["xp"])
		_workbench_screen.professions = prof_xp
		if _workbench_screen.visible:
			_workbench_screen.queue_redraw()


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
			var label := Label3D.new()
			label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
			label.fixed_size = true
			label.pixel_size = 0.0013
			label.font_size = 24
			label.outline_size = 6
			label.position = Vector3.UP * 2.25
			root.add_child(label)
			_world_root.add_child(root)
			_target_nodes[id] = {"root": root, "body": body, "mat": mat, "label": label,
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
		var lbl: Label3D = entry["label"]
		if alive:
			lbl.text = "%s\n%d / %d HP%s" % [str(t["name"]), roundi(float(t["health"])), roundi(float(t["max_health"])),
					"\nLast: %.0f  DPS: %.0f" % [float(entry["last_dmg"]), dps] if dps > 0.5 else ""]
		else:
			lbl.text = "%s\n[KNOCKED DOWN]" % str(t["name"])

	for id: int in _target_nodes.keys():
		if not seen.has(id):
			var r: Node = _target_nodes[id]["root"]
			if is_instance_valid(r):
				r.queue_free()
			_target_nodes.erase(id)


func update_hub(client: Object, world: Node3D, delta: float, pause_visible: bool,
		center_label: Label, match_label: RichTextLabel) -> void:
	_notice_time = maxf(0.0, _notice_time - delta)
	if client == null or not client.is_spawned():
		return
	add_recruiters(client, world)
	var in_hub: bool = client.is_spawned() and not client.get_recruiters().is_empty() and client.get_match().is_empty()
	if in_hub:
		_update_targets(client, delta)
		_update_corpse_markers(client)
		if _quest_screen != null and _hub_hud != null:
			_update_quest_hud(client)
	var status: Dictionary = client.get_player_status()
	if int(status.get("notice_id", 0)) != _notice_id:
		_notice_id = int(status["notice_id"])
		_notice = str(status.get("notice", ""))
		_notice_time = 10.0
		if _notice != "":
			print("notice: ", _notice)
			_notice_lines.append(_notice)
			if _qm_screen != null and _qm_screen.visible:
				_qm_screen.feedback = _notice
			if _vendor_screen != null and _vendor_screen.visible:
				_vendor_screen.feedback = _notice
			if _workbench_screen != null and _workbench_screen.visible:
				_workbench_screen.feedback = _notice

	_sync_screen_data(client)

	if _hub_hud == null or _terminal_screen == null:
		if not client.get_recruiters().is_empty() and client.get_match().is_empty():
			_update_hub_text_hud(client, center_label, match_label)
		return

	var terminal: Dictionary = at_recruiter(client) if in_hub else {}
	var qm: Dictionary = at_quartermaster(client) if in_hub else {}
	var vendor: Dictionary = at_vendor(client) if in_hub else {}
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
		_terminal_screen.sides = sides
		_terminal_screen.queued = queued
		if queued >= 0:
			_terminal_screen.queue_mode = str(status.get("queue_mode", "standard"))
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
			"mode": str(status.get("queue_mode", "standard")),
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
		"xp": int(status.get("experience", 0)),
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
	if _scanner != null and _hub_hud != null:
		if in_hub and _scanner.active:
			_hub_hud.scanner = _scanner.update_scanner(client, _crafting_store, client.get_presented()["position"], delta)
		else:
			if _scanner.active:
				_scanner.active = false
				_scanner.visible = false
			_hub_hud.scanner = {}
	_hub_hud.queue_redraw()


func _update_corpse_markers(client: Object) -> void:
	if _world_root == null:
		return
	var seen := {}
	for c: Dictionary in client.get_corpses():
		var cid := int(c.get("entity_id", 0))
		if (c.get("items", []) as Array).is_empty():
			continue
		seen[cid] = true
		if not _corpse_nodes.has(cid):
			var pos: Vector3 = c["position"]
			var ring := MeshInstance3D.new()
			var torus := TorusMesh.new()
			torus.outer_radius = 1.25
			torus.inner_radius = 1.15
			var mat := StandardMaterial3D.new()
			mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			mat.albedo_color = Color(1.0, 0.85, 0.3)
			ring.mesh = torus
			ring.material_override = mat
			ring.position = pos + Vector3.UP * 0.08
			var label := Label3D.new()
			label.text = "[LOOT]"
			label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
			label.fixed_size = true
			label.pixel_size = 0.0012
			label.font_size = 20
			label.outline_size = 6
			label.modulate = Color(1.0, 0.88, 0.35)
			label.position = pos + Vector3.UP * 1.15
			_world_root.add_child(ring)
			_world_root.add_child(label)
			_corpse_nodes[cid] = {"ring": ring, "label": label}
	for cid: int in _corpse_nodes.keys():
		if not seen.has(cid):
			var entry: Dictionary = _corpse_nodes[cid]
			for k in ["ring", "label"]:
				var n: Variant = entry.get(k)
				if n is Node and is_instance_valid(n):
					(n as Node).queue_free()
			_corpse_nodes.erase(cid)


func _update_hub_text_hud(client: Object, center_label: Label, match_label: RichTextLabel) -> void:
	var factions: PackedStringArray = client.get_factions()
	var status: Dictionary = client.get_player_status()
	if status.get("queue", "none") == "queued" and factions.size() == 2:
		var where := str(status.get("battlefield", ""))
		match_label.text = "[center]Queued (%s) for the %s: %d in the queue, the battle%s starts in %d s\n[X] leave the queue[/center]" % [
				str(status.get("queue_mode", "standard")),
				factions[int(status["team"])], status["in_queue"], " on " + where if where != "" else "",
				ceili(status["starts_in"])]
		return
	match_label.text = ""
	var terminal := at_recruiter(client)
	if not terminal.is_empty() and factions.size() == 2 and center_label.text == "":
		center_label.text = "%s\n[F] fight for the %s     [G] fight for the %s" % [
				terminal["name"], factions[0], factions[1]]


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
