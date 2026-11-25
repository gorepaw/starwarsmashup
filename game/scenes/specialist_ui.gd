# Stage 12: the specialist and companion professions on the client. Owns the
# ability bar (HUD), the datapad (P), the bazaar (E at a bazaar terminal) and
# the field effects in the world, and turns keys into requests:
#
#   ability_1..6   use the active loadout's abilities, aimed with the crosshair
#                  (an ally, an enemy, a creature, or the ground it points at)
#   companion_command  one-key context ping: on an enemy, attack; on the
#                  ground, guard there; twice quickly, follow / recall
#   companion_summon   field the Companion Slot's (or first) companion, or store it
#   beacon_spawn   in a match, waiting to spawn: at your side's squad beacon
#
#   var ui := SpecialistUI.new(); add_child(ui); ui.setup(layer, world, art, settings)
#   ui.update(client, camera, catalog, delta)          # every frame
#   if ui.handle_input(event, client, camera, spawn_loadout): return
class_name SpecialistUI
extends Node

const AbilityBar = preload("res://ui/ability_bar.gd")
const DatapadScreen = preload("res://ui/datapad_screen.gd")
const BazaarScreen = preload("res://ui/bazaar_screen.gd")
const FieldEffectsScript = preload("res://rendering/field_effects.gd")

const USE_ABILITY := 0
const SUMMON := 1
const DISMISS := 2
const COMMAND := 3
const MODE_NAMES := ["Following", "Attacking", "Guarding"]
const ABILITY_KEYS := 6
const DOUBLE_TAP := 0.35
const BOOK_PATH := "professions/specialists.ini"
# Profession levels from experience: professions.ini's [curve] (Base 100, PerLevel 15).
const CURVE_BASE := 100
const CURVE_PER_LEVEL := 15

signal screen_opened
signal screen_closed

var _bar: AbilityBar
var _datapad: DatapadScreen
var _bazaar: BazaarScreen
var _field: FieldEffectsScript
var _settings: RefCounted
var _client: Object
var _book := {}
var _look_heights := {}   # a companion's look (object template) -> its height (m)
var _spec := {}
var _revision := -1
var _synced_at := 0.0     # when the last sync arrived (cooldowns count down from it)
var _clock := 0.0
var _last_command := -10.0
var _hint := ""
var _hint_time := 0.0
var _catalog: Array = []


func setup(layer: CanvasLayer, world_root: Node3D, art: RefCounted, settings: RefCounted) -> void:
	_settings = settings
	_field = FieldEffectsScript.new(world_root)
	if art == null:
		return
	_bar = AbilityBar.new(art)
	layer.add_child(_bar)
	_datapad = DatapadScreen.new(art)
	_datapad.visible = false
	_datapad.request.connect(_on_request)
	_datapad.bar_edit.connect(_on_bar_edit)
	_datapad.closed.connect(close_screens)
	layer.add_child(_datapad)
	_bazaar = BazaarScreen.new(art)
	_bazaar.visible = false
	_bazaar.request.connect(_on_request)
	_bazaar.closed.connect(close_screens)
	layer.add_child(_bazaar)


# Which ability is on each of the bar's six keys. `layout` is the player's
# choice per key: an ability's id, "" (the key fills itself) or "-" (left
# empty). A key whose ability isn't in the kit (another loadout's) fills
# itself too. The keys that fill themselves take the kit's abilities that are
# neither placed nor in `off` (taken off the bar), in the kit's order. Returns
# six ids, "" for an empty key.
static func resolve_bar(layout: Array, kit_ids: Array, off: Array) -> Array:
	var bar := []
	var placed := {}
	for i in ABILITY_KEYS:
		var id := str(layout[i]) if i < layout.size() else ""
		if id != "" and id != "-" and kit_ids.has(id) and not placed.has(id):
			placed[id] = true
			bar.append(id)
		else:
			bar.append("-" if id == "-" else "")
	var next := 0
	for i in ABILITY_KEYS:
		if bar[i] != "":
			if bar[i] == "-":
				bar[i] = ""
			continue
		while next < kit_ids.size() and (placed.has(kit_ids[next]) or off.has(kit_ids[next])):
			next += 1
		if next < kit_ids.size():
			bar[i] = kit_ids[next]
			placed[kit_ids[next]] = true
	return bar


# The layout after the player puts `ability` on key `slot` (0..5), or takes it
# off the bar (slot -1), or (ability "") sets the whole bar back to filling
# itself. Returns {"layout": [...], "off": [...]}.
static func edit_bar(layout: Array, off: Array, kit_ids: Array, ability: String, slot: int) -> Dictionary:
	if ability == "":
		return {"layout": [], "off": []}
	var now := resolve_bar(layout, kit_ids, off)
	# Start from what the bar shows, so what the player didn't touch stays put
	# (a key that filled itself becomes theirs once they rearrange the bar).
	var out := []
	for i in ABILITY_KEYS:
		out.append(now[i] if now[i] != "" else ("-" if layout.size() > i and str(layout[i]) == "-" else ""))
	var kept_off := off.duplicate()
	for i in ABILITY_KEYS:
		if out[i] == ability:
			out[i] = "-"
	if slot >= 0 and slot < ABILITY_KEYS:
		out[slot] = ability
		kept_off.erase(ability)
	elif not kept_off.has(ability):
		kept_off.append(ability)
	return {"layout": out, "off": kept_off}


func _kit_ids() -> Array:
	var ids := []
	for e: Dictionary in _spec.get("abilities", []):
		ids.append(str(e["id"]))
	return ids


# The six keys' abilities now (resolve_bar with the player's layout).
func bar_ids() -> Array:
	var layout: Array = _settings.bar_layout if _settings != null and "bar_layout" in _settings else []
	var off: Array = _settings.bar_off if _settings != null and "bar_off" in _settings else []
	return resolve_bar(layout, _kit_ids(), off)


func _on_bar_edit(ability: String, slot: int) -> void:
	if _settings == null or not "bar_layout" in _settings:
		return
	var edited := edit_bar(_settings.bar_layout, _settings.bar_off, _kit_ids(), ability, slot)
	_settings.bar_layout = edited["layout"]
	_settings.bar_off = edited["off"]
	_settings.save_file()
	if _datapad != null:
		_datapad.bar_ids = bar_ids()


func has_ui() -> bool:
	return (_datapad != null and _datapad.visible) or (_bazaar != null and _bazaar.visible)


func close_screens() -> void:
	var was := has_ui()
	if _datapad != null:
		_datapad.visible = false
	if _bazaar != null:
		_bazaar.visible = false
	if was:
		screen_closed.emit()


func _on_request(action: int, id: String, target: int, position: Vector3, amount: int, item_id: int, text: String) -> void:
	if _client != null:
		_client.specialist(action, id, target, position, amount, item_id, text)


func _say(text: String) -> void:
	_hint = text
	_hint_time = 3.0


func _level_for(xp: int) -> int:
	var level := 1
	var need := CURVE_BASE
	var left := xp
	while level < 100 and left >= need:
		left -= need
		level += 1
		need = CURVE_BASE + CURVE_PER_LEVEL * (level - 1)
	return level


func _levels(client: Object) -> Dictionary:
	var out := {}
	for p: Dictionary in client.get_professions():
		out[str(p["id"])] = _level_for(int(p.get("xp", 0)))
	return out


func _own(client: Object) -> Dictionary:
	return client.get_presented() if client.is_spawned() else {}


func _positions(client: Object) -> Dictionary:
	var out := {}
	var own := _own(client)
	if not own.is_empty():
		out[client.get_entity_id()] = own["position"]
	var looks: PackedStringArray = client.get_looks()
	var heights := {}
	for r: Dictionary in client.get_remotes():
		out[int(r["id"])] = r["position"]
		# Companions' tags sit over their heads: a bantha's 2.8 m, an R2's 1 m.
		var look := int(r.get("look", 0))
		if look >= 1 and look <= looks.size() and _look_heights.has(looks[look - 1]):
			heights[int(r["id"])] = _look_heights[looks[look - 1]]
	if _field != null:
		_field.tag_heights = heights
	return out


func _cooldown_left(entry: Dictionary) -> float:
	return maxf(0.0, float(entry.get("cooldown", 0.0)) - (_clock - _synced_at))


func update(client: Object, camera: Camera3D, catalog: Array, delta: float) -> void:
	_client = client
	_catalog = catalog
	_clock += delta
	_hint_time = maxf(0.0, _hint_time - delta)
	if client == null or not client.has_method("get_specialist"):
		return
	if _book.is_empty():
		var path := ProjectSettings.globalize_path("res://" + BOOK_PATH)
		if FileAccess.file_exists("res://" + BOOK_PATH):
			_book = client.get_specialist_book(path)
			for c: Dictionary in (_book.get("companions", {}) as Dictionary).values():
				_look_heights[str(c.get("look", ""))] = float(c.get("height", 1.0))
	_spec = client.get_specialist()
	if int(_spec.get("revision", -1)) != _revision:
		_revision = int(_spec.get("revision", -1))
		_synced_at = _clock
	var abilities: Dictionary = _book.get("abilities", {})
	var spawned: bool = client.is_spawned()
	if _field != null:
		_field.update(client.get_field() if spawned else {}, _positions(client), client.get_entity_id(), delta)
	if _bar == null:
		return
	# The bar: the player's six keys (empty ones drawn as empty slots, up to
	# the last one in use), with their keys and cooldowns.
	var slots: Array = []
	var chosen := bar_ids()
	var last := -1
	for i in ABILITY_KEYS:
		if chosen[i] != "":
			last = i
	for i in last + 1:
		var key := _key_name("ability_%d" % (i + 1))
		var entry := _kit_entry(str(chosen[i]))
		if entry.is_empty():
			slots.append({"key": key, "name": "", "cooldown": 0.0, "total": 1.0})
			continue
		var a: Dictionary = abilities.get(str(entry["id"]), {})
		slots.append({"key": key, "name": str(a.get("name", entry["id"])),
				"cooldown": _cooldown_left(entry), "total": float(a.get("cooldown", 1.0))})
	_bar.slots = slots
	var effects: Array = []
	for e: Dictionary in _spec.get("effects", []):
		var left := float(e.get("remaining", 0.0)) - (_clock - _synced_at)
		if left > 0:
			effects.append({"name": str(e.get("name", "")), "remaining": left, "debuff": int(e.get("flags", 0)) & 1})
	_bar.effects = effects
	_bar.performing = bool(_spec.get("performing", false))
	# The fielded companion: its health from the snapshot.
	var companion := {}
	var pet := int(_spec.get("companion_entity", 0))
	if pet != 0:
		for r: Dictionary in client.get_remotes():
			if int(r["id"]) == pet:
				var name := ""
				for c: Dictionary in _spec.get("companions", []):
					if int(c["id"]) == int(_spec.get("active_companion", 0)):
						name = str(c.get("name", ""))
				companion = {"name": name if name != "" else str(r.get("name", "Companion")),
						"health": float(r.get("health", 0.0)), "max_health": float(r.get("max_health", 1.0)),
						"mode": "%s  (%s: command, %s: store)" % [MODE_NAMES[clampi(int(_spec.get("companion_mode", 0)), 0, 2)],
								_key_name("companion_command"), _key_name("companion_summon")]}
	elif float(_spec.get("companion_cooldown", 0.0)) > 0:
		companion = {"name": "", "cooldown": maxf(0.0, float(_spec["companion_cooldown"]) - (_clock - _synced_at))}
	_bar.companion = companion
	var channel := str(_spec.get("channel", ""))
	if channel != "":
		var total := maxf(0.1, float((abilities.get(channel, {}) as Dictionary).get("channel", 1.0)))
		var left := maxf(0.0, float(_spec.get("channel_remaining", 0.0)) - (_clock - _synced_at))
		_bar.channel = {"name": str((abilities.get(channel, {}) as Dictionary).get("name", channel)) + "... hold still",
				"progress": 1.0 - left / total}
	else:
		_bar.channel = {}
	var hint := _hint if _hint_time > 0 else ""
	if hint == "" and _beacon_ready(client):
		hint = "Press %s to spawn at your squad's beacon" % _key_name("beacon_spawn")
	_bar.hint = hint
	_bar.visible = spawned and (not slots.is_empty() or not companion.is_empty() or not effects.is_empty() or hint != "")
	_bar.queue_redraw()
	if _datapad.visible:
		_fill_datapad(client)
		_datapad.queue_redraw()
	if _bazaar.visible:
		_fill_bazaar(client)
		_bazaar.queue_redraw()


func _key_name(action: String) -> String:
	if _settings == null:
		return ""
	var bound: Variant = (_settings.get("bindings") as Dictionary).get(action, null)
	if bound is int:
		return OS.get_keycode_string(int(bound))
	return str(bound) if bound != null else ""


func _beacon_ready(client: Object) -> bool:
	if client.get_match().is_empty() or not client.is_spawned():
		return false
	if str(client.get_presented().get("life", "")) != "waiting":
		return false
	for d: Dictionary in client.get_field().get("deployables", []):
		if str(d["kind"]) == "spawn_beacon" and bool(d.get("friendly", false)):
			return true
	return false


func _equipped(client: Object) -> Dictionary:
	var out := {}
	for lo: Dictionary in client.get_character_loadouts():
		for key in ["primary_id", "sidearm_id", "secondary_id"]:
			out[int(lo.get(key, 0))] = true
		for id: Variant in (lo.get("gear_ids", {}) as Dictionary).values():
			out[int(id)] = true
		for id: Variant in (lo.get("appearance_ids", {}) as Dictionary).values():
			out[int(id)] = true
	out.erase(0)
	return out


func _near(own: Vector3, list: Array, extra := 1.5) -> Dictionary:
	for t: Dictionary in list:
		var p: Vector3 = t["position"]
		if Vector2(p.x - own.x, p.z - own.z).length() <= float(t.get("radius", 3.0)) + extra:
			return t
	return {}


func _fill_datapad(client: Object) -> void:
	var own := _own(client)
	_datapad.spec = _spec
	_datapad.book = _book
	_datapad.items = client.get_inventory()
	_datapad.catalog = _catalog
	_datapad.levels = _levels(client)
	_datapad.bar_ids = bar_ids()
	_datapad.key_names = {}
	for i in ABILITY_KEYS:
		_datapad.key_names["ability_%d" % (i + 1)] = _key_name("ability_%d" % (i + 1))
	if own.is_empty():
		return
	var at: Vector3 = own["position"]
	var terminal := _near(at, client.get_mission_terminals())
	_datapad.terminal = terminal
	_datapad.at_quartermaster = not _near(at, client.get_quartermasters()).is_empty()
	_datapad.own = {"entity": client.get_entity_id(), "name": str(own.get("name", "")), "appearance": str(own.get("appearance", ""))}
	var best := 8.0
	_datapad.designee = {}
	for r: Dictionary in client.get_remotes():
		if int(r.get("actor_kind", 0)) != 0 or str(r.get("name", "")) == "":
			continue
		var d := (r["position"] as Vector3).distance_to(at)
		if d <= best:
			best = d
			_datapad.designee = {"entity": int(r["id"]), "name": str(r["name"]), "appearance": str(r.get("appearance", ""))}


func _fill_bazaar(client: Object) -> void:
	_bazaar.spec = _spec
	_bazaar.items = client.get_inventory()
	_bazaar.catalog = _catalog
	_bazaar.equipped = _equipped(client)
	_bazaar.credits = int(client.get_player_status().get("credits", 0))
	_bazaar.merchant_level = int(_levels(client).get("merchant", 0))


# What the crosshair points at: the nearest fitting entity along it (within
# `reach`), and the ground it meets.
func _aim(client: Object, camera: Camera3D, target: String, reach: float) -> Dictionary:
	var from := camera.global_position
	var dir := -camera.global_basis.z
	var far := from + dir * maxf(reach + 30.0, 60.0)
	var t := float(client.raycast(from, far))
	var ground := from.lerp(far, t) if t >= 0 else far
	var own := _own(client)
	var own_team := int(own.get("team", -1))
	var in_match: bool = not client.get_match().is_empty()
	var best_id := 0
	var best_along := INF
	for r: Dictionary in client.get_remotes():
		if not bool(r.get("alive", true)) and target != "ally":
			continue
		var kind := int(r.get("actor_kind", 0))
		var team := int(r.get("team", -1))
		var fits := false
		match target:
			"ally":
				fits = kind == 0 and (not in_match or team == own_team)
			"enemy":
				fits = (in_match and kind == 0 and team != own_team) or (not in_match and (kind == 2 or kind == 3)) \
						or (not in_match and kind == 0)
			"creature":
				fits = kind == 2
		if not fits:
			continue
		var p: Vector3 = (r["position"] as Vector3) + Vector3(0, 1.0, 0)
		var along := (p - from).dot(dir)
		if along <= 0 or along > reach + 25.0:
			continue
		var off := (from + dir * along).distance_to(p)
		if off <= maxf(1.0, along * 0.06) and along < best_along:
			best_along = along
			best_id = int(r["id"])
	return {"entity": best_id, "ground": ground}


func _kit_entry(id: String) -> Dictionary:
	if id == "":
		return {}
	for e: Dictionary in _spec.get("abilities", []):
		if str(e["id"]) == id:
			return e
	return {}


func _use(client: Object, camera: Camera3D, index: int) -> void:
	var entry := _kit_entry(str(bar_ids()[index]))
	if entry.is_empty():
		_say("Nothing on that key: choose abilities in the datapad (%s)" % _key_name("datapad"))
		return
	var a: Dictionary = (_book.get("abilities", {}) as Dictionary).get(str(entry["id"]), {})
	if _cooldown_left(entry) > 0.05:
		_say("%s: %ds" % [str(a.get("name", entry["id"])), ceili(_cooldown_left(entry))])
		return
	var target := str(a.get("target", "self"))
	var reach := maxf(3.0, float(a.get("range", 0.0)))
	var aim := _aim(client, camera, target, reach) if camera != null else {"entity": 0, "ground": Vector3.ZERO}
	if (target == "enemy" or target == "creature") and int(aim["entity"]) == 0:
		_say("%s: aim at %s" % [str(a.get("name", "")), "an enemy" if target == "enemy" else "a creature"])
		return
	client.specialist(USE_ABILITY, str(entry["id"]), int(aim["entity"]), aim["ground"], 0, 0, "")


func _command(client: Object, camera: Camera3D) -> void:
	if int(_spec.get("companion_entity", 0)) == 0:
		_say("No companion fielded (%s)" % _key_name("companion_summon"))
		return
	if _clock - _last_command < DOUBLE_TAP:
		client.specialist(COMMAND, "", 0, Vector3.ZERO, 0, 0, "")  # follow / recall
		_say("Companion: follow")
		_last_command = -10.0
		return
	_last_command = _clock
	var aim := _aim(client, camera, "enemy", 80.0)
	if int(aim["entity"]) != 0:
		client.specialist(COMMAND, "", int(aim["entity"]), Vector3.ZERO, 1, 0, "")
		_say("Companion: attack")
	else:
		client.specialist(COMMAND, "", 0, aim["ground"], 2, 0, "")
		_say("Companion: guard there (twice quickly: follow)")


func handle_input(event: InputEvent, client: Object, camera: Camera3D, spawn_loadout := 0) -> bool:
	if client == null or not client.has_method("get_specialist"):
		return false
	var pressed := func(action: String) -> bool:
		return InputMap.has_action(action) and event.is_action_pressed(action) and not event.is_echo()
	var escape: bool = event is InputEventKey and event.pressed and not event.echo \
			and (event as InputEventKey).physical_keycode == KEY_ESCAPE
	if has_ui():
		if escape or pressed.call("datapad") or (_bazaar.visible and pressed.call("interact")):
			close_screens()
			return true
		# The screens have the mouse and keyboard (their LineEdits too); the
		# game mustn't take a click on them as "capture the mouse".
		return true
	if pressed.call("datapad"):
		if _datapad != null:
			_fill_datapad(client)
			_datapad.open_datapad()
			screen_opened.emit()
		return true
	if pressed.call("beacon_spawn") and _beacon_ready(client):
		client.request_spawn(spawn_loadout, 255)
		return true
	if not client.is_spawned() or str(client.get_presented().get("life", "")) != "alive":
		return false
	for i in ABILITY_KEYS:
		if pressed.call("ability_%d" % (i + 1)):
			_use(client, camera, i)
			return true
	if pressed.call("companion_command"):
		_command(client, camera)
		return true
	if pressed.call("companion_summon"):
		if int(_spec.get("active_companion", 0)) != 0:
			client.specialist(DISMISS, "", 0, Vector3.ZERO, 0, 0, "")
		else:
			var id := int(_spec.get("companion_slot", 0))
			var list: Array = _spec.get("companions", [])
			if id == 0 and not list.is_empty() and client.get_match().is_empty():
				id = int((list[0] as Dictionary)["id"])
			if id == 0 and list.is_empty():
				_say("No companions: tame one, hatch an egg or register a droid (%s: datapad)" % _key_name("datapad"))
				return true
			client.specialist(SUMMON, "", 0, Vector3.ZERO, id, 0, "")
		return true
	if pressed.call("interact") and client.get_match().is_empty() and _bazaar != null:
		var own := _own(client)
		var bazaars: Array = []
		for v: Dictionary in client.get_vendors():
			if str(v.get("kind", "")) == "bazaar":
				bazaars.append(v)
		var at := _near(own["position"], bazaars) if not own.is_empty() else {}
		if not at.is_empty():
			_fill_bazaar(client)
			_bazaar.open_bazaar(str(at.get("detail", at.get("name", "Galactic Bazaar"))))
			screen_opened.emit()
			return true
	return false


func clear() -> void:
	if _field != null:
		_field.clear()
	close_screens()
