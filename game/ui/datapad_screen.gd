# Stage 12: the datapad, in Battlefront II's frame. Everything the Wave 7B
# and 7C professions keep that isn't a button on the HUD:
#
#   Kit         the active loadout's abilities and being open to bounty hunters
#   Companions  tamed, hatched and registered creatures and droids: field or
#               store one, put it in the loadout's Companion Slot, train a
#               mount, rename, release
#   Lab         DNA samples: engineer eggs, grow tissues and additives; hatch
#               eggs and register droids you carry
#   Droids      assemble a chassis with modules from the Material Vault
#   Contracts   bounties, contraband runs, tracking jobs and gigs
#   Slicing     a Smuggler's permanent sidegrades on weapons and armor
#
# The controller (SpecialistUI) sets the data; requests go out as `request`.
extends "res://ui/bf2_screen.gd"

signal request(action: int, id: String, target: int, position: Vector3, amount: int, item_id: int, text: String)
signal closed
# The player's choice for the ability bar: put `ability` on key `slot` (0..5),
# take it off the bar (slot -1), or (ability "") set the bar back to filling
# itself. SpecialistUI keeps the layout.
signal bar_edit(ability: String, slot: int)

# net::SpecialistRequest::Action
const SUMMON := 1
const DISMISS := 2
const RENAME := 4
const RELEASE := 5
const MOUNT := 6
const ENGINEER_EGG := 7
const HATCH_EGG := 8
const GROW_TISSUE := 9
const ASSEMBLE := 10
const REGISTER := 11
const CONTRACTS := 12
const ACCEPT := 13
const ABANDON := 14
const SLICE := 21
const BOUNTY_FLAG := 23
const SUPPLY := 24
const COMPANION_SLOT := 25

const TABS := ["Kit", "Companions", "Lab", "Droids", "Contracts", "Slicing"]
const ROWS := 8

var spec := {}             # SwmClient.get_specialist()
var book := {}             # SwmClient.get_specialist_book()
var items: Array = []      # what you carry (SwmClient.get_inventory())
var catalog: Array = []    # item defs, for names
var levels := {}           # profession id -> level
var terminal := {}         # a mission terminal in reach: {index, kind, name}; empty: none
var at_quartermaster := false
var feedback := ""
var key_names := {}        # action -> key label
var bar_ids: Array = []    # the ability on each of the bar's six keys now ("" empty)

var _tab := 0
var _scroll := 0
var _sample_a := -1
var _sample_b := -1
var _chassis := ""
var _modules: Array = []
var _slice_item := -1
var _slice := ""
var _confirm_release := -1
var _defs := {}
var _name_edit: LineEdit
var _renaming := -1


func _init(shared_art: RefCounted = null) -> void:
	super._init(shared_art)
	_name_edit = LineEdit.new()
	_name_edit.visible = false
	_name_edit.max_length = 24
	_name_edit.placeholder_text = "New name, then Enter"
	_name_edit.text_submitted.connect(_on_rename_submitted)
	add_child(_name_edit)


func open_datapad(tab := -1) -> void:
	if tab >= 0:
		_tab = tab
	_scroll = 0
	feedback = ""
	_confirm_release = -1
	visible = true
	queue_redraw()


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed \
			and (event as InputEventMouseButton).button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		_scroll = maxi(0, _scroll + (-1 if (event as InputEventMouseButton).button_index == MOUSE_BUTTON_WHEEL_UP else 1))
		queue_redraw()
		accept_event()
		return
	super._gui_input(event)


func _send(action: int, amount := 0, item_id := 0, id := "", text_value := "", target := 0) -> void:
	request.emit(action, id, target, Vector3.ZERO, amount, item_id, text_value)


func _def(def_id: String) -> Dictionary:
	if _defs.size() != catalog.size():
		_defs.clear()
		for d: Dictionary in catalog:
			_defs[str(d.get("id", ""))] = d
	return _defs.get(def_id, {})


func _item_name(item: Dictionary) -> String:
	var custom := str(item.get("custom_name", ""))
	if custom != "":
		return custom
	return str(_def(str(item.get("def_id", ""))).get("name", item.get("def_id", "?")))


func _companion_template(id: String) -> Dictionary:
	return (book.get("companions", {}) as Dictionary).get(id, {})


func _level(profession: String) -> int:
	return int(levels.get(profession, 0))


func _row(rect: Rect2, id: String, selected := false) -> void:
	var hot := _hover == id or selected
	draw_rect(rect, Color(0.18, 0.22, 0.3, 0.55) if hot else Color(0.1, 0.13, 0.18, 0.4))
	if id != "":
		region(rect, id)


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.55))
	var box := centered(900, 520, Vector2(0, 12))
	text("gamefont_large", "Datapad", Vector2(size.x * 0.5, box.position.y - 44 * u), "Center", ACCENT)
	draw_rect(box, Color(0.04, 0.05, 0.07, 0.92))
	frame(box)
	button(Rect2(box.end.x - 104 * u, box.position.y + 10 * u, 88 * u, 26 * u), "Close", "close", false, true)
	var tx := box.position.x + 14 * u
	for i in TABS.size():
		var w := 104.0 * u
		button(Rect2(tx, box.position.y + 10 * u, w, 26 * u), TABS[i], "tab:%d" % i, i == _tab)
		tx += w + 4 * u
	var body := Rect2(box.position.x + 18 * u, box.position.y + 50 * u, box.size.x - 36 * u, box.size.y - 90 * u)
	_name_edit.visible = _renaming >= 0 and _tab == 1
	match _tab:
		0: _paint_kit(body)
		1: _paint_companions(body)
		2: _paint_lab(body)
		3: _paint_droids(body)
		4: _paint_contracts(body)
		5: _paint_slicing(body)
	if feedback != "":
		text("gamefont_small", feedback, Vector2(box.get_center().x, box.end.y - 30 * u), "Center", ACCENT)


func _paint_kit(body: Rect2) -> void:
	var y := body.position.y
	var abilities: Dictionary = book.get("abilities", {})
	var kit: Array = spec.get("abilities", [])
	text("gamefont_medium", "This loadout's kit", Vector2(body.position.x, y), "Left", TEXT)
	var flag_on := bool(spec.get("bounty_flag", false))
	button(Rect2(body.end.x - 260 * u, y - 2 * u, 260 * u, 26 * u),
			"Open to bounty hunters: %s" % ("ON" if flag_on else "OFF"), "bounty_flag", flag_on)
	button(Rect2(body.end.x - 400 * u, y - 2 * u, 130 * u, 26 * u), "Reset bar", "bar_reset")
	text("gamefont_tiny", "Choose a key for each ability to set your bar.",
			Vector2(body.end.x - 410 * u, y + 3 * u), "Right", DIM, 0.9)
	y += 30 * u
	if kit.is_empty():
		text("gamefont_small", "No abilities: a specialist's trainer teaches them.",
				Vector2(body.get_center().x, y + 30 * u), "Center", DIM)
		return
	_scroll = clampi(_scroll, 0, maxi(0, kit.size() - ROWS))
	for i in mini(ROWS, kit.size() - _scroll):
		var entry: Dictionary = kit[_scroll + i]
		var a: Dictionary = abilities.get(str(entry.get("id", "")), {})
		var rect := Rect2(body.position.x, y, body.size.x, 44 * u)
		_row(rect, "")
		var ability := str(entry.get("id", ""))
		var on_key := bar_ids.find(ability)
		var key := str(key_names.get("ability_%d" % (on_key + 1), "")) if on_key >= 0 else ""
		text("gamefont_small", "%s%s" % ["[%s] " % key if key != "" else "", str(a.get("name", entry["id"]))],
				Vector2(rect.position.x + 8 * u, y + 3 * u), "Left", ACCENT)
		# Which of the bar's keys it's on: a button each (the one it's on takes it off).
		var bx := rect.end.x - 8 * u - 6 * 30 * u
		text("gamefont_tiny", "Bar", Vector2(bx - 8 * u, y + 24 * u), "Right", DIM, 0.9)
		for n in 6:
			button(Rect2(bx + n * 30 * u, y + 20 * u, 27 * u, 21 * u),
					str(key_names.get("ability_%d" % (n + 1), str(n + 3))), "bar:%s:%d" % [ability, n], on_key == n)
		text("gamefont_tiny", "%s %d  ·  %ds cooldown%s" % [str(a.get("profession", "")).capitalize(), int(a.get("level", 1)),
				int(a.get("cooldown", 0)), "" if str(a.get("where", "both")) == "both" else "  ·  " + str(a.get("where")) + " only"],
				Vector2(rect.end.x - 8 * u, y + 5 * u), "Right", DIM, 0.9)
		text("gamefont_tiny", str(a.get("description", "")), Vector2(rect.position.x + 8 * u, y + 24 * u), "Left", TEXT, 0.9)
		y += 48 * u


func _paint_companions(body: Rect2) -> void:
	var list: Array = spec.get("companions", [])
	var y := body.position.y
	var active := int(spec.get("active_companion", 0))
	var slot := int(spec.get("companion_slot", 0))
	var cooldown := float(spec.get("companion_cooldown", 0.0))
	text("gamefont_small", "Companions %d  ·  Creature Handler %d  ·  Droid Operations %d%s" % [list.size(),
			_level("creature_handler"), _level("droid_operations"),
			"  ·  recovering %ds" % ceili(cooldown) if cooldown > 0 else ""],
			Vector2(body.position.x, y), "Left", TEXT)
	y += 28 * u
	if list.is_empty():
		text("gamefont_small", "Tame a creature (Creature Handler), hatch an egg, or register a droid.",
				Vector2(body.get_center().x, y + 40 * u), "Center", DIM)
		return
	if _renaming >= 0:
		_name_edit.position = Vector2(body.position.x, body.end.y - 30 * u)
		_name_edit.size = Vector2(300 * u, 26 * u)
	_scroll = clampi(_scroll, 0, maxi(0, list.size() - 7))
	for i in mini(7, list.size() - _scroll):
		var c: Dictionary = list[_scroll + i]
		var id := int(c["id"])
		var tpl := _companion_template(str(c.get("template", "")))
		var rect := Rect2(body.position.x, y, body.size.x, 46 * u)
		_row(rect, "", id == active)
		var title := "%s  (%s, %s)" % [str(c.get("name", "")), str(tpl.get("name", c.get("template", ""))), str(c.get("origin", ""))]
		text("gamefont_small", title, Vector2(rect.position.x + 8 * u, y + 3 * u), "Left", ACCENT if id == active else TEXT)
		var traits := "Q%d  health x%.2f  damage x%.2f  %s%s%s" % [roundi(float(c.get("quality", 0.5)) * 100),
				float(c.get("health", 1.0)), float(c.get("damage", 1.0)), str(tpl.get("role", "")),
				"  ·  " + ", ".join(c.get("modules", PackedStringArray())) if not (c.get("modules", PackedStringArray()) as PackedStringArray).is_empty() else "",
				"  ·  mount trained" if bool(c.get("mount", false)) else ""]
		text("gamefont_tiny", traits, Vector2(rect.position.x + 8 * u, y + 25 * u), "Left", DIM, 0.9)
		var bx := rect.end.x - 8 * u
		var buttons := [["Release" if _confirm_release != id else "Sure?", "release:%d" % id],
				["Rename", "rename:%d" % id],
				["Slot" if slot != id else "Slotted", "slot:%d" % id]]
		if bool(tpl.get("mountable", false)) and not bool(c.get("mount", false)):
			buttons.append(["Train mount", "mount:%d" % id])
		buttons.append(["Store" if id == active else "Field", ("store:%d" if id == active else "field:%d") % id])
		for b: Array in buttons:
			var w := 92.0 * u if str(b[0]) != "Train mount" else 120.0 * u
			bx -= w
			button(Rect2(bx, y + 10 * u, w, 26 * u), str(b[0]), str(b[1]), str(b[0]) == "Slotted")
			bx -= 4 * u
		y += 50 * u


func _paint_lab(body: Rect2) -> void:
	var samples: Array = spec.get("samples", [])
	var half := (body.size.x - 18 * u) * 0.5
	var x := body.position.x
	var y := body.position.y
	text("gamefont_small", "DNA samples (Bio-Engineer %d)" % _level("bio_engineer"), Vector2(x, y), "Left", TEXT)
	y += 26 * u
	for i in mini(9, samples.size()):
		var s: Dictionary = samples[i]
		var sid := int(s["id"])
		var tpl := _companion_template(str(s.get("template", "")))
		var rect := Rect2(x, y, half, 28 * u)
		var tag := " [A]" if sid == _sample_a else (" [B]" if sid == _sample_b else "")
		_row(rect, "sample:%d" % sid, tag != "")
		text("gamefont_tiny", "%s  Q%d%s" % [str(tpl.get("name", s.get("template", ""))), roundi(float(s.get("quality", 0.0)) * 100), tag],
				Vector2(rect.position.x + 8 * u, y + 7 * u), "Left", ACCENT if tag != "" else TEXT)
		y += 30 * u
	if samples.is_empty():
		text("gamefont_tiny", "Sample creatures with your kit's Sample DNA.", Vector2(x + 8 * u, y + 6 * u), "Left", DIM)
	var by := body.end.y - 34 * u
	button(Rect2(x, by, 150 * u, 28 * u), "Engineer egg", "egg", false, false, _sample_a >= 0)
	button(Rect2(x + 156 * u, by, 120 * u, 28 * u), "Grow tissue", "tissue", false, false, _sample_a >= 0)
	button(Rect2(x + 282 * u, by, 130 * u, 28 * u), "Grow additive", "additive", false, false, _sample_a >= 0)
	text("gamefont_tiny", "Click a sample for A, another for B (optional: splices in its toughness).",
			Vector2(x, by - 20 * u), "Left", DIM, 0.85)
	# Eggs and droids carried.
	x += half + 18 * u
	y = body.position.y
	text("gamefont_small", "Eggs and droids you carry", Vector2(x, y), "Left", TEXT)
	y += 26 * u
	var any := false
	for item: Dictionary in items:
		var def_id := str(item.get("def_id", ""))
		if def_id != "bio_creature_egg" and def_id != "companion_droid":
			continue
		any = true
		var rect := Rect2(x, y, half, 30 * u)
		_row(rect, "")
		text("gamefont_tiny", _item_name(item), Vector2(rect.position.x + 8 * u, y + 8 * u), "Left", TEXT)
		var egg := def_id == "bio_creature_egg"
		button(Rect2(rect.end.x - 96 * u, y + 2 * u, 90 * u, 26 * u), "Hatch" if egg else "Register",
				("hatch:%d" if egg else "register:%d") % int(item["id"]))
		y += 34 * u
	if not any:
		text("gamefont_tiny", "None. Engineer an egg, or buy a droid from a Droid Engineer.", Vector2(x + 8 * u, y + 6 * u), "Left", DIM)


func _paint_droids(body: Rect2) -> void:
	var x := body.position.x
	var y := body.position.y
	var level := _level("droid_engineer")
	text("gamefont_small", "Chassis (Droid Engineer %d)" % level, Vector2(x, y), "Left", TEXT)
	y += 26 * u
	var col := 300.0 * u
	for c: Dictionary in (book.get("companions", {}) as Dictionary).values():
		if str(c.get("kind", "")) != "droid":
			continue
		var cid := str(c["id"])
		var rect := Rect2(x, y, col, 40 * u)
		var ok := int(c.get("build_level", 1)) <= level
		_row(rect, "chassis:" + cid if ok else "", cid == _chassis)
		text("gamefont_tiny", "%s  (%s, %d bays)" % [str(c.get("name", cid)), str(c.get("role", "")), int(c.get("bays", 0))],
				Vector2(rect.position.x + 8 * u, y + 3 * u), "Left", TEXT if ok else DIM)
		text("gamefont_tiny", "DE %d  ·  %s" % [int(c.get("build_level", 1)), ", ".join(c.get("materials", []))],
				Vector2(rect.position.x + 8 * u, y + 21 * u), "Left", DIM, 0.85)
		y += 44 * u
	# Modules for the chosen chassis.
	x += col + 18 * u
	y = body.position.y
	var chassis := _companion_template(_chassis)
	text("gamefont_small", "Modules%s" % ("" if chassis.is_empty() else " (%d of %d bays)" % [_modules.size(), int(chassis.get("bays", 0))]),
			Vector2(x, y), "Left", TEXT)
	y += 26 * u
	for m: Dictionary in book.get("modules", []):
		var mid := str(m["id"])
		var ok := int(m.get("level", 1)) <= level and not chassis.is_empty()
		var rect := Rect2(x, y, body.end.x - x, 34 * u)
		_row(rect, "module:" + mid if ok else "", _modules.has(mid))
		text("gamefont_tiny", "%s [%s, DE %d]  %s" % [str(m.get("name", mid)), str(m.get("bay", "")), int(m.get("level", 1)),
				str(m.get("description", ""))], Vector2(rect.position.x + 8 * u, y + 9 * u), "Left",
				ACCENT if _modules.has(mid) else (TEXT if ok else DIM), 0.85)
		y += 36 * u
	button(Rect2(body.end.x - 200 * u, body.end.y - 30 * u, 200 * u, 28 * u), "Assemble", "assemble", false, false, _chassis != "")


func _paint_contracts(body: Rect2) -> void:
	var x := body.position.x
	var y := body.position.y
	var held: Array = spec.get("contracts", [])
	text("gamefont_small", "Your contracts (%d of 3)" % held.size(), Vector2(x, y), "Left", TEXT)
	y += 26 * u
	for c: Dictionary in held:
		var rect := Rect2(x, y, body.size.x, 40 * u)
		_row(rect, "")
		text("gamefont_tiny", str(c.get("title", "")), Vector2(rect.position.x + 8 * u, y + 3 * u), "Left", ACCENT)
		var detail := "%d cr  ·  %s XP  ·  %d min left" % [int(c.get("reward", 0)), str(c.get("profession", "")).capitalize(),
				ceili(float(c.get("expires_in", 0.0)) / 60.0)]
		if float(c.get("needed", 0.0)) > 0:
			detail += "  ·  %d / %d s performed" % [int(c.get("progress", 0.0)), int(c.get("needed", 0.0))]
		text("gamefont_tiny", detail, Vector2(rect.position.x + 8 * u, y + 21 * u), "Left", DIM, 0.85)
		button(Rect2(rect.end.x - 100 * u, y + 7 * u, 92 * u, 26 * u), "Abandon", "abandon:%d" % int(c["id"]))
		y += 44 * u
	y += 10 * u
	var offers: Array = spec.get("offers", [])
	if not terminal.is_empty():
		button(Rect2(x, y, 300 * u, 28 * u), "Ask %s for work" % str(terminal.get("name", "the terminal")), "ask")
		y += 36 * u
	else:
		text("gamefont_tiny", "Stand at a bounty, scout, entertainer or general mission terminal for contracts.",
				Vector2(x, y), "Left", DIM)
		y += 22 * u
	if not offers.is_empty():
		text("gamefont_small", "Offered by %s" % str(spec.get("offer_source", "")), Vector2(x, y), "Left", TEXT)
		y += 24 * u
		for c: Dictionary in offers:
			if y > body.end.y - 40 * u:
				break
			var rect := Rect2(x, y, body.size.x, 34 * u)
			_row(rect, "")
			text("gamefont_tiny", "%s  ·  %d cr" % [str(c.get("title", "")), int(c.get("reward", 0))],
					Vector2(rect.position.x + 8 * u, y + 9 * u), "Left", TEXT)
			button(Rect2(rect.end.x - 100 * u, y + 4 * u, 92 * u, 26 * u), "Accept", "accept:%d" % int(c["id"]))
			y += 38 * u


func _paint_slicing(body: Rect2) -> void:
	var half := (body.size.x - 18 * u) * 0.5
	var x := body.position.x
	var y := body.position.y
	text("gamefont_small", "Weapons and armor (Smuggler %d; 250 cr a slice)" % _level("smuggler"), Vector2(x, y), "Left", TEXT)
	y += 26 * u
	var shown := 0
	for item: Dictionary in items:
		var kind := str(_def(str(item.get("def_id", ""))).get("kind", ""))
		if kind != "weapon" and kind != "armor":
			continue
		if shown >= 11:
			break
		shown += 1
		var rect := Rect2(x, y, half, 28 * u)
		var iid := int(item["id"])
		_row(rect, "sliceitem:%d" % iid, iid == _slice_item)
		var current := ""
		var cj := str(item.get("color_json", ""))
		var at := cj.find("\"slice\"")
		if at >= 0:
			current = cj.substr(at).get_slice("\"", 3)
		text("gamefont_tiny", _item_name(item) + ("  [%s]" % current if current != "" else ""), Vector2(rect.position.x + 8 * u, y + 7 * u),
				"Left", ACCENT if iid == _slice_item else TEXT)
		y += 30 * u
	x += half + 18 * u
	y = body.position.y
	text("gamefont_small", "Slices", Vector2(x, y), "Left", TEXT)
	y += 26 * u
	for s: Dictionary in book.get("slices", []):
		var sid := str(s["id"])
		var rect := Rect2(x, y, half, 40 * u)
		var ok := maxi(50, int(s.get("level", 1))) <= _level("smuggler")
		_row(rect, "slice:" + sid if ok else "", sid == _slice)
		text("gamefont_tiny", "%s (%s, Smuggler %d)" % [str(s.get("name", sid)), str(s.get("applies", "")), maxi(50, int(s.get("level", 1)))],
				Vector2(rect.position.x + 8 * u, y + 3 * u), "Left", ACCENT if sid == _slice else (TEXT if ok else DIM))
		text("gamefont_tiny", str(s.get("description", "")), Vector2(rect.position.x + 8 * u, y + 21 * u), "Left", DIM, 0.85)
		y += 44 * u
	button(Rect2(body.end.x - 200 * u, body.end.y - 30 * u, 200 * u, 28 * u), "Slice it", "do_slice", false, false,
			_slice_item >= 0 and _slice != "")
	if at_quartermaster and _level("merchant") >= 100:
		button(Rect2(body.position.x, body.end.y - 30 * u, 300 * u, 28 * u), "Supply the selected item to the quartermaster",
				"supply", false, false, _slice_item >= 0)


func _on_click(id: String, _at: Vector2) -> void:
	feedback = ""
	if id == "close":
		_renaming = -1
		closed.emit()
	elif id.begins_with("tab:"):
		_tab = int(id.substr(4))
		_scroll = 0
		_renaming = -1
	elif id == "bounty_flag":
		_send(BOUNTY_FLAG, 0 if bool(spec.get("bounty_flag", false)) else 1)
	elif id == "bar_reset":
		bar_edit.emit("", -1)
	elif id.begins_with("bar:"):
		var parts := id.split(":")
		var slot := int(parts[2])
		bar_edit.emit(parts[1], -1 if bar_ids.size() > slot and bar_ids[slot] == parts[1] else slot)
	elif id.begins_with("field:"):
		_send(SUMMON, int(id.substr(6)))
	elif id.begins_with("store:"):
		_send(DISMISS)
	elif id.begins_with("slot:"):
		var cid := int(id.substr(5))
		_send(COMPANION_SLOT, 0 if int(spec.get("companion_slot", 0)) == cid else cid)
	elif id.begins_with("mount:"):
		_send(MOUNT, int(id.substr(6)))
	elif id.begins_with("rename:"):
		_renaming = int(id.substr(7))
		_name_edit.text = ""
		_name_edit.visible = true
		_name_edit.grab_focus()
	elif id.begins_with("release:"):
		var cid := int(id.substr(8))
		if _confirm_release == cid:
			_send(RELEASE, cid)
			_confirm_release = -1
		else:
			_confirm_release = cid
	elif id.begins_with("sample:"):
		var sid := int(id.substr(7))
		if _sample_a == sid:
			_sample_a = -1
		elif _sample_b == sid:
			_sample_b = -1
		elif _sample_a < 0:
			_sample_a = sid
		else:
			_sample_b = sid
	elif id == "egg":
		_send(ENGINEER_EGG, _sample_a, maxi(0, _sample_b))
		_sample_a = -1
		_sample_b = -1
	elif id == "tissue" or id == "additive":
		_send(GROW_TISSUE, _sample_a, 0, id)
		_sample_a = -1
	elif id.begins_with("hatch:"):
		_send(HATCH_EGG, 0, int(id.substr(6)))
	elif id.begins_with("register:"):
		_send(REGISTER, 0, int(id.substr(9)))
	elif id.begins_with("chassis:"):
		_chassis = id.substr(8)
		_modules.clear()
	elif id.begins_with("module:"):
		var mid := id.substr(7)
		if _modules.has(mid):
			_modules.erase(mid)
		elif _modules.size() < int(_companion_template(_chassis).get("bays", 0)):
			_modules.append(mid)
	elif id == "assemble":
		_send(ASSEMBLE, 0, 0, _chassis, ",".join(_modules))
	elif id == "ask":
		_send(CONTRACTS, 0, 0, "", "", int(terminal.get("index", 0)))
	elif id.begins_with("accept:"):
		_send(ACCEPT, int(id.substr(7)))
	elif id.begins_with("abandon:"):
		_send(ABANDON, int(id.substr(8)))
	elif id.begins_with("sliceitem:"):
		_slice_item = int(id.substr(10))
	elif id.begins_with("slice:"):
		_slice = id.substr(6)
	elif id == "do_slice":
		_send(SLICE, 0, _slice_item, _slice)
	elif id == "supply":
		_send(SUPPLY, 0, _slice_item)
		_slice_item = -1
	queue_redraw()


func _on_rename_submitted(new_name: String) -> void:
	if _renaming >= 0 and new_name.strip_edges() != "":
		_send(RENAME, _renaming, 0, "", new_name.strip_edges())
	_renaming = -1
	_name_edit.visible = false
	queue_redraw()
