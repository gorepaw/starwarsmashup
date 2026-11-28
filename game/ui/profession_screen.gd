# Professions in Battlefront II's frames, fonts and colors (docs/PROFESSIONS.md,
# docs/PERKS.md): the planner. Each profession's level and experience, what
# earns it more, and what it teaches: its perks (an A or a B at each tier: 5
# or 1, then 25/45/65/85). What a character knows works in every loadout;
# they may know at most `cap` perks in each pool, learned and given up at
# trainers. `professions` are SwbfoCharacterStore.get_professions()'s;
# `progress` is id -> experience and `known` id -> the perks learned
# (SwbfoClient.get_professions()).
extends "res://ui/bf2_screen.gd"

signal closed

const DONE_COLOR := Color(0.45, 0.85, 0.5)
const LOCKED_COLOR := Color(0.42, 0.46, 0.55)
const WAIT_COLOR := Color(0.75, 0.62, 0.35)
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
var known: Dictionary = {}       # profession id -> [perk ids learned]
var store: Object            # SwbfoCharacterStore: the level curve
var cap := 10                # perks a character may know in each pool

var _tab := "combat"
var _selected := 0
var _scroll := 0
var _detail_scroll := 0


func open_screen() -> void:
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


# What a character knows, in order: [{name, profession, pool}].
func _learned() -> Array:
	var out: Array = []
	for p: Dictionary in professions:
		var id := str(p.get("id", ""))
		var mine: Array = known.get(id, [])
		for perk: Dictionary in p.get("perks", []):
			if str(perk.get("id", "")) in mine:
				out.append({"name": str(perk.get("name", "")), "profession": str(p.get("name", id)),
						"pool": str(p.get("pool", "combat"))})
	return out


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
	var known_h := 92.0 * u
	var list_rect := Rect2(x, y, 260 * u, box.end.y - y - known_h - 26 * u)
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
	_paint_known(Rect2(x, list_rect.end.y + 12 * u, box.size.x - 36 * u, known_h))


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
	var add := func(line: String, color: Color) -> void:
		lines.append(line)
		colors.append(color)
	for line in _wrap("gamefont_tiny", str(p.get("description", "")), rect.size.x):
		add.call(line, TEXT)
	var earns := str(p.get("earns", ""))
	if earns != "":
		for line in _wrap("gamefont_tiny", "Experience: " + earns, rect.size.x):
			add.call(line, DIM)
	var perks: Array = p.get("perks", [])
	if not perks.is_empty():
		var category := str(p.get("category", ""))
		add.call("Choose one perk at each level; " + ("they work while you wear its armor." if category == "armor"
				else ("they work with its weapons." if category == "weapon"
				else ("the first is its licence." if int(p.get("first_perk", 5)) == 1 else "none is needed to begin."))), DIM)
	add.call("", TEXT)
	var mine: Array = known.get(id, [])
	var last_level := -1
	for perk: Dictionary in perks:
		var at := int(perk.get("level", 0))
		if at == last_level:
			add.call("            - or -", DIM)  # a level's perks exclude each other
		last_level = at
		var reached := available and at <= level
		var learned := str(perk.get("id", "")) in mine
		var waits := str(perk.get("needs", "")) != ""
		var head := "%s Lv %d  %s%s" % ["[x]" if learned else "[ ]", at,
				str(perk.get("name", "")), "   (not working yet)" if waits else ""]
		add.call(head, DONE_COLOR if learned else (TEXT if reached else (ACCENT if available else LOCKED_COLOR)))
		for line in _wrap("gamefont_tiny", str(perk.get("description", "")), rect.size.x - 18 * u):
			add.call("      " + line, WAIT_COLOR if waits else (TEXT if available else LOCKED_COLOR))
	var room := int((rect.end.y - 10 * u - y) / line_h)
	_detail_scroll = clampi(_detail_scroll, 0, maxi(0, lines.size() - room))
	for i in range(_detail_scroll, mini(lines.size(), _detail_scroll + room)):
		text("gamefont_tiny", lines[i], Vector2(x, y), "Left", colors[i])
		y += line_h


# What the character knows, against the cap.
func _paint_known(rect: Rect2) -> void:
	draw_rect(rect, Color(0.04, 0.06, 0.09, 0.6))
	var x := rect.position.x + 10 * u
	var y := rect.position.y + 8 * u
	var learned := _learned()
	var kx := x
	for pool: String in ["combat", "noncombat"]:
		var n := 0
		for l: Dictionary in learned:
			if str(l.get("pool", "combat")) == pool:
				n += 1
		text("gamefont_small", "%s  %d / %d" % ["Combat" if pool == "combat" else "Non-combat", n, cap],
				Vector2(kx, y), "Left", ACCENT if n < cap else Color(0.95, 0.55, 0.35))
		kx += 200 * u
	text("gamefont_tiny", "Learned and given up at trainers; giving up is free, and so is learning again.",
			Vector2(rect.end.x - 10 * u, y + 3 * u), "Right", DIM)
	y += 28 * u
	if learned.is_empty():
		text("gamefont_tiny", "Nothing yet: a trainer teaches what your levels have earned.", Vector2(x, y + 6 * u),
				"Left", DIM)
		return
	var columns := 5
	var w := (rect.size.x - 20 * u) / columns
	for i in mini(learned.size(), columns * 2):
		var cell := Vector2(x + (i % columns) * w, y + (i / columns) * 22 * u)
		var entry: Dictionary = learned[i]
		text("gamefont_tiny", "%s (%s)" % [entry["name"], entry["profession"]], cell, "Left", TEXT)
	if learned.size() > columns * 2:
		text("gamefont_tiny", "+%d more" % (learned.size() - columns * 2), Vector2(rect.end.x - 10 * u, y + 22 * u),
				"Right", DIM)


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
	queue_redraw()
