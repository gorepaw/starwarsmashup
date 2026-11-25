# Professions in Battlefront II's frames, fonts and colors (docs/PROFESSIONS.md):
# the planner. Each profession's level and experience, what earns it more,
# its milestones (reached ones marked) and what their perks do; and each
# loadout's Active Specialization Slots, which say whose combat perks it
# fights with, and its Companion Slot.
# `professions` are SwmCharacterStore.get_professions()'s; `progress` is
# id -> experience (SwmClient.get_professions()); `loadouts` are
# SwmClient.get_character_loadouts()'s.
extends "res://ui/bf2_screen.gd"

signal loadout_saved(slot: int, loadout: Dictionary)
signal closed

const DONE_COLOR := Color(0.45, 0.85, 0.5)
const LOCKED_COLOR := Color(0.42, 0.46, 0.55)
const BAR_BACK := Color(0.08, 0.1, 0.14, 0.9)
const BAR_FILL := Color(0.95, 0.72, 0.25, 0.9)
const TABS := [
	["combat", "Combat", ["armor", "weapon"]],
	["gathering", "Gathering", ["gathering"]],
	["crafting", "Crafting", ["crafting"]],
	["specialist", "Specialists", ["specialist"]],
	["companion", "Companions", ["companion"]],
]
const ROWS := 9

var professions: Array = []
var progress: Dictionary = {}
var trained: Dictionary = {}     # profession id -> the highest milestone level learned from a trainer
var loadouts: Array = []
var active_loadout := 0
var store: Object            # SwmCharacterStore: the level curve
var slots := 3               # Active Specialization Slots per loadout

var _tab := "combat"
var _selected := 0
var _scroll := 0
var _detail_scroll := 0
var _loadout := 0            # whose slots are shown


func open_screen() -> void:
	_loadout = clampi(active_loadout, 0, 2)
	_selected = 0
	_scroll = 0
	_detail_scroll = 0
	visible = true
	queue_redraw()


func _list() -> Array:
	var categories: Array = []
	for tab: Array in TABS:
		if tab[0] == _tab:
			categories = tab[2]
	return professions.filter(func(p: Dictionary) -> bool: return str(p.get("category", "")) in categories)


func _xp(id: String) -> int:
	return int(progress.get(id, 0))


func _level(id: String) -> Dictionary:
	if store == null:
		return {"level": 1, "into": 0, "needed": 100}
	return store.profession_level(_xp(id))


func _profession(id: String) -> Dictionary:
	for p: Dictionary in professions:
		if str(p.get("id", "")) == id:
			return p
	return {}


func _loadout_dict(slot: int) -> Dictionary:
	for lo: Dictionary in loadouts:
		if int(lo.get("slot_index", -1)) == slot:
			return lo
	return {}


func _slotted(slot: int) -> Array:
	return Array(_loadout_dict(slot).get("specializations", []))


# Words laid into lines no wider than `width` pixels.
func _wrap(font: String, value: String, width: float) -> PackedStringArray:
	var lines := PackedStringArray()
	for paragraph in value.split("\n"):
		var line := ""
		for word in paragraph.split(" ", false):
			var candidate := word if line == "" else line + " " + word
			if line != "" and art.text_width(font, candidate, u) > width:
				lines.append(line)
				line = word
			else:
				line = candidate
		lines.append(line)
	return lines


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index in [MOUSE_BUTTON_WHEEL_UP,
			MOUSE_BUTTON_WHEEL_DOWN]:
		var step := -1 if event.button_index == MOUSE_BUTTON_WHEEL_UP else 1
		if _region_at(event.position).begins_with("row:") or event.position.x < size.x * 0.4:
			_scroll = clampi(_scroll + step, 0, maxi(0, _list().size() - ROWS))
		else:
			_detail_scroll = maxi(0, _detail_scroll + step)
		queue_redraw()
		accept_event()
		return
	super._gui_input(event)


func _bar(rect: Rect2, fraction: float) -> void:
	draw_rect(rect, BAR_BACK)
	draw_rect(Rect2(rect.position, Vector2(rect.size.x * clampf(fraction, 0, 1), rect.size.y)), BAR_FILL)


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.55))
	var box := centered(880, 520, Vector2(0, 10))
	text("gamefont_large", "Professions", Vector2(size.x * 0.5, box.position.y - 42 * u), "Center", ACCENT)
	frame(box)
	var x := box.position.x + 18 * u
	var y := box.position.y + 14 * u
	var tab_x := x
	for tab: Array in TABS:
		var w: float = art.text_width("gamefont_small", tab[1], u) + 28 * u
		button(Rect2(tab_x, y, w, 28 * u), tab[1], "tab:" + str(tab[0]), _tab == tab[0])
		tab_x += w + 6 * u
	button(Rect2(box.end.x - 110 * u, y, 92 * u, 28 * u), "Close", "close", false, true)
	y += 40 * u

	var list := _list()
	_selected = clampi(_selected, 0, maxi(0, list.size() - 1))
	var slots_h := 92.0 * u
	var list_rect := Rect2(x, y, 260 * u, box.end.y - y - slots_h - 26 * u)
	draw_rect(list_rect, Color(0.04, 0.06, 0.09, 0.6))
	var row_h := (list_rect.size.y - 8 * u) / ROWS
	for i in range(_scroll, mini(list.size(), _scroll + ROWS)):
		var p: Dictionary = list[i]
		var id := str(p.get("id", ""))
		var row := Rect2(list_rect.position.x + 4 * u, list_rect.position.y + 4 * u + (i - _scroll) * row_h,
				list_rect.size.x - 8 * u, row_h - 3 * u)
		var chosen := i == _selected
		draw_rect(row, Color(0.25, 0.32, 0.45, 0.55) if chosen else (Color(0.18, 0.22, 0.3, 0.4)
				if _hover == "row:%d" % i else Color(0.1, 0.13, 0.18, 0.4)))
		region(row, "row:%d" % i)
		var available := bool(p.get("available", true))
		var name_color := (ACCENT if chosen else TEXT) if available else LOCKED_COLOR
		text("gamefont_tiny", str(p.get("name", id)), row.position + Vector2(8, 3) * u, "Left", name_color)
		if available:
			var lv := _level(id)
			text("gamefont_tiny", "Lv %d" % int(lv["level"]), Vector2(row.end.x - 8 * u, row.position.y + 3 * u),
					"Right", name_color)
			var needed := int(lv["needed"])
			_bar(Rect2(row.position.x + 8 * u, row.end.y - 9 * u, row.size.x - 16 * u, 4 * u),
					1.0 if needed <= 0 else float(lv["into"]) / needed)
		else:
			text("gamefont_tiny", "Wave %s" % str(p.get("wave", "")), Vector2(row.end.x - 8 * u, row.position.y + 3 * u),
					"Right", LOCKED_COLOR)
			text("gamefont_tiny", "Not in the game yet", row.position + Vector2(8, 17) * u, "Left", LOCKED_COLOR)

	var detail := Rect2(list_rect.end.x + 16 * u, y, box.end.x - list_rect.end.x - 34 * u, list_rect.size.y)
	if not list.is_empty():
		_paint_detail(list[_selected], detail)
	_paint_slots(Rect2(x, list_rect.end.y + 12 * u, box.size.x - 36 * u, slots_h))


func _paint_detail(p: Dictionary, rect: Rect2) -> void:
	var id := str(p.get("id", ""))
	var available := bool(p.get("available", true))
	var x := rect.position.x
	var y := rect.position.y
	text("gamefont_medium", str(p.get("name", id)), Vector2(x, y), "Left", ACCENT)
	var lv := _level(id)
	var level := int(lv["level"]) if available else 0
	if available:
		var needed := int(lv["needed"])
		var label := "Level %d" % level + ("   %d / %d XP" % [int(lv["into"]), needed] if needed > 0 else "   Mastered")
		text("gamefont_tiny", label, Vector2(rect.end.x, y + 4 * u), "Right", TEXT)
		y += 24 * u
		_bar(Rect2(x, y, rect.size.x, 6 * u), 1.0 if needed <= 0 else float(lv["into"]) / needed)
		y += 12 * u
	else:
		y += 24 * u
		text("gamefont_tiny", "Planned for Wave %s: not in the game yet." % str(p.get("wave", "")), Vector2(x, y),
				"Left", LOCKED_COLOR)
		y += 18 * u
	var line_h: float = art.line_height("gamefont_tiny", u) + 2 * u
	var lines := PackedStringArray()
	var colors: Array = []
	for line in _wrap("gamefont_tiny", str(p.get("description", "")), rect.size.x):
		lines.append(line)
		colors.append(TEXT)
	var earns := str(p.get("earns", ""))
	if earns != "":
		for line in _wrap("gamefont_tiny", "Experience: " + earns, rect.size.x):
			lines.append(line)
			colors.append(DIM)
	if bool(p.get("slotted", false)):
		lines.append("Its perks apply from a loadout's specialization slot" +
				(" while wearing its armor." if str(p.get("category", "")) == "armor" else "."))
		colors.append(DIM)
	var rank: Array = p.get("rank", [])
	if not rank.is_empty() and int(p.get("rank_every", 0)) > 0:
		var every := int(p.get("rank_every", 0))
		lines.append("Every %d levels: %s  (x%d now)" % [every, ", ".join(PackedStringArray(rank)), level / every])
		colors.append(DIM)
	lines.append("")
	colors.append(TEXT)
	for m: Dictionary in p.get("milestones", []):
		var reached := available and int(m.get("level", 0)) <= level
		var learned := reached and int(m.get("level", 0)) <= int(trained.get(id, 0))
		var head := "%s Lv %d  %s%s" % ["[x]" if learned else "[ ]", int(m.get("level", 0)), str(m.get("name", "")),
				"   (reached: a trainer teaches it)" if reached and not learned else ""]
		lines.append(head)
		colors.append(DONE_COLOR if reached else (ACCENT if available else LOCKED_COLOR))
		var body := str(m.get("description", ""))
		var perks: Array = m.get("perks", [])
		if not perks.is_empty():
			body += "  (" + ", ".join(PackedStringArray(perks)) + ")"
		for line in _wrap("gamefont_tiny", body, rect.size.x - 18 * u):
			lines.append("      " + line)
			colors.append(TEXT if reached or available else LOCKED_COLOR)
	var room := int((rect.end.y - 40 * u - y) / line_h)
	_detail_scroll = clampi(_detail_scroll, 0, maxi(0, lines.size() - room))
	for i in range(_detail_scroll, mini(lines.size(), _detail_scroll + room)):
		text("gamefont_tiny", lines[i], Vector2(x, y), "Left", colors[i])
		y += line_h

	var slotted := _slotted(_loadout)
	var buttons_y := rect.end.y - 30 * u
	if bool(p.get("slotted", false)) and available:
		var lo_name := str(_loadout_dict(_loadout).get("name", "Loadout %d" % (_loadout + 1)))
		if id in slotted:
			button(Rect2(rect.end.x - 240 * u, buttons_y, 240 * u, 30 * u), "Unslot from " + lo_name, "unslot:" + id)
		else:
			var free := slotted.size() < slots and not _loadout_dict(_loadout).is_empty()
			button(Rect2(rect.end.x - 240 * u, buttons_y, 240 * u, 30 * u), "Slot in " + lo_name, "slot:" + id,
					false, true, free)


func _paint_slots(rect: Rect2) -> void:
	draw_rect(rect, Color(0.04, 0.06, 0.09, 0.6))
	var x := rect.position.x + 10 * u
	var y := rect.position.y + 8 * u
	text("gamefont_small", "Specializations", Vector2(x, y), "Left", ACCENT)
	var tab_x := x + 170 * u
	for slot in 3:
		var lo := _loadout_dict(slot)
		var label := str(lo.get("name", "Loadout %d" % (slot + 1)))
		if slot == active_loadout:
			label += " *"
		button(Rect2(tab_x, y - 2 * u, 170 * u, 24 * u), label, "loadout:%d" % slot, _loadout == slot, false,
				not lo.is_empty())
		tab_x += 176 * u
	y += 30 * u
	var slotted := _slotted(_loadout)
	var w := (rect.size.x - 20 * u - (slots) * 8 * u) / (slots + 1)
	for i in slots:
		var cell := Rect2(x + i * (w + 8 * u), y, w, 44 * u)
		frame(cell, "border_3_pieces", Color(1, 1, 1, 0.8))
		if i < slotted.size():
			var id := str(slotted[i])
			var p := _profession(id)
			text("gamefont_tiny", str(p.get("name", id)), cell.position + Vector2(10, 6) * u, "Left", TEXT)
			text("gamefont_tiny", "Lv %d   (click to unslot)" % int(_level(id)["level"]),
					cell.position + Vector2(10, 22) * u, "Left", DIM)
			region(cell, "unslot:" + id)
		else:
			text("gamefont_tiny", "Empty slot", cell.position + Vector2(10, 14) * u, "Left", DIM)
	var companion := Rect2(x + slots * (w + 8 * u), y, w, 44 * u)
	frame(companion, "border_3_pieces", Color(1, 1, 1, 0.5))
	text("gamefont_tiny", "Companion", companion.position + Vector2(10, 6) * u, "Left", LOCKED_COLOR)
	text("gamefont_tiny", "None yet (Wave 7C)", companion.position + Vector2(10, 22) * u, "Left", LOCKED_COLOR)


func _save_slots(slot: int, ids: Array) -> void:
	var lo := _loadout_dict(slot).duplicate(true)
	if lo.is_empty():
		return
	lo["specializations"] = ids
	for i in loadouts.size():
		if int((loadouts[i] as Dictionary).get("slot_index", -1)) == slot:
			loadouts[i] = lo  # shown at once; the server's answer replaces it
	loadout_saved.emit(slot, lo)


func _on_click(id: String, _at: Vector2) -> void:
	if id == "close":
		closed.emit()
	elif id.begins_with("tab:"):
		_tab = id.substr(4)
		_selected = 0
		_scroll = 0
		_detail_scroll = 0
	elif id.begins_with("row:"):
		_selected = int(id.substr(4))
		_detail_scroll = 0
	elif id.begins_with("loadout:"):
		_loadout = int(id.substr(8))
	elif id.begins_with("slot:"):
		var ids := _slotted(_loadout)
		var p := _profession(id.substr(5))
		if ids.size() < slots and not id.substr(5) in ids and bool(p.get("slotted", false)) \
				and bool(p.get("available", true)):
			ids.append(id.substr(5))
			_save_slots(_loadout, ids)
	elif id.begins_with("unslot:"):
		var ids := _slotted(_loadout)
		ids.erase(id.substr(7))
		_save_slots(_loadout, ids)
	queue_redraw()
