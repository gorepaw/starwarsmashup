# A skill trainer's screen, in Battlefront II's frame (docs/PERKS.md): each
# profession they teach and what it offers: two perks at each of its five
# tiers (5 or 1, then 25/45/65/85), of which you know one. Learning costs
# credits the first time and is free after; giving up is always free. A
# character knows at most `cap` perks in each pool.
#
#   screen.trainer_name = ...; screen.professions = [{id, name, perks: [...]}]
#   screen.progress = {profession id: {level, trained, paid, perks, paid_perks}}
#   screen.credits = ...; screen.known_counts = ...; screen.cap = ...
#   screen.open_trainer()   # signal service_requested(action, id, amount):
#                           # 8 learn_perk (perk), 9 forget (perk)
extends "res://ui/bf2_screen.gd"

signal service_requested(action: int, id: String, amount: int)
signal closed

const LEARN_PERK := 8
const FORGET := 9
const DONE := Color(0.5, 0.9, 0.55)
const WAITS := Color(0.75, 0.62, 0.35)

var trainer_name := "Trainer"
var professions: Array = []      # the ones this trainer teaches (their defs)
var progress := {}               # profession id -> {level, trained, paid, perks, paid_perks}
var credits := 0
var known_counts := {"combat": 0, "noncombat": 0}  # perks known, by pool (cap in each)
var cap := 10
var feedback := ""

var _scroll := 0
var _rows: Array = []            # [{kind: "head" | "pair" | "milestone", ...}]
var _visible_rows := 6


func open_trainer() -> void:
	_scroll = 0
	feedback = ""
	visible = true
	queue_redraw()


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed \
			and (event as InputEventMouseButton).button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		var step := -1 if (event as InputEventMouseButton).button_index == MOUSE_BUTTON_WHEEL_UP else 1
		_scroll = clampi(_scroll + step, 0, maxi(0, _rows.size() - _visible_rows))
		queue_redraw()
		accept_event()
		return
	super._gui_input(event)


func _build_rows() -> void:
	_rows.clear()
	for p: Dictionary in professions:
		var id := str(p.get("id", ""))
		var mine: Dictionary = progress.get(id, {})
		var level := int(mine.get("level", 1))
		_rows.append({"kind": "head", "name": str(p.get("name", id)), "level": level})
		var perks: Array = p.get("perks", [])
		var at := 0
		while at + 1 < perks.size():
			_rows.append({"kind": "pair", "profession": id, "a": perks[at], "b": perks[at + 1], "level": level})
			at += 2


# Words laid into at most `most` lines no wider than `width` pixels.
func _wrap(font: String, value: String, width: float, most: int) -> PackedStringArray:
	var lines := PackedStringArray()
	var line := ""
	for word in value.split(" ", false):
		var candidate := word if line == "" else line + " " + word
		if line != "" and art.text_width(font, candidate, u) > width:
			lines.append(line)
			line = word
		else:
			line = candidate
	lines.append(line)
	if lines.size() > most:
		lines.resize(most)
		lines[most - 1] += "..."
	return lines


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.55))
	var box := centered(900, 500, Vector2(0, 10))
	text("gamefont_large", trainer_name, Vector2(size.x * 0.5, box.position.y - 42 * u), "Center", ACCENT)
	draw_rect(box, Color(0.04, 0.05, 0.07, 0.9))
	frame(box)
	var x := box.position.x + 18 * u
	var y := box.position.y + 14 * u
	text("gamefont_medium", "Learn what you've earned", Vector2(x, y), "Left", TEXT)
	# The pools this trainer's professions count against.
	var pools: Array = []
	for p: Dictionary in professions:
		var pool := str(p.get("pool", "combat"))
		if not pool in pools:
			pools.append(pool)
	var kx := x + 330 * u
	for pool: String in pools:
		var n := int(known_counts.get(pool, 0))
		var label := "%s %d / %d" % ["Combat" if pool == "combat" else "Non-combat", n, cap]
		text("gamefont_small", label, Vector2(kx, y + 3 * u), "Left", ACCENT if n < cap else Color(0.95, 0.55, 0.35))
		kx += 190 * u
	text("gamefont_medium", "%d Credits" % credits, Vector2(box.end.x - 130 * u, y), "Right", ACCENT)
	button(Rect2(box.end.x - 110 * u, y - 2 * u, 92 * u, 28 * u), "Close", "close", false, true)
	y += 40 * u
	_build_rows()
	var row_h := 66.0 * u
	var bottom := box.end.y - 50 * u
	_visible_rows = maxi(1, int((bottom - y) / (row_h + 3 * u)))
	_scroll = clampi(_scroll, 0, maxi(0, _rows.size() - _visible_rows))
	var width := box.size.x - 36 * u
	var ry := y
	var more := false  # rows below the box
	for index in range(_scroll, _rows.size()):
		var row: Dictionary = _rows[index]
		var h := 30.0 * u if str(row["kind"]) == "head" else (row_h if str(row["kind"]) == "pair" else 46.0 * u)
		if ry + h > bottom:
			more = true
			break
		match str(row["kind"]):
			"head":
				text("gamefont_medium", "%s   Level %d" % [row["name"], int(row["level"])], Vector2(x, ry + 6 * u),
						"Left", ACCENT)
			"pair":
				# The two side by side with an OR between: one or the other.
				var gap := OR_GAP * u
				var half := (width - gap) * 0.5
				var a: Dictionary = row["a"]
				var b: Dictionary = row["b"]
				_paint_perk(Rect2(x, ry, half, h), str(row["profession"]), a, b, int(row["level"]))
				_paint_perk(Rect2(x + half + gap, ry, half, h), str(row["profession"]), b, a, int(row["level"]))
				_paint_or(Vector2(x + half + gap * 0.5, ry + h * 0.5))
		ry += h + 3 * u
	if _scroll > 0 or more:
		text("gamefont_tiny", "Scroll for more (row %d of %d)" % [_scroll + 1, _rows.size()],
				Vector2(box.get_center().x, box.end.y - 22 * u), "Center", DIM)
	if feedback != "":
		text("gamefont_small", feedback, Vector2(box.get_center().x, box.end.y - 44 * u), "Center", ACCENT)


const OR_GAP := 44.0  # units between a level's two perks, for the OR


# "OR" in a small dark disc, joined to both perks by a line: they exclude each other.
func _paint_or(center: Vector2) -> void:
	var reach := OR_GAP * 0.5 * u
	draw_line(center - Vector2(reach, 0), center + Vector2(reach, 0), ACCENT, maxf(1.0, 1.5 * u))
	draw_circle(center, 15 * u, Color(0.04, 0.05, 0.07))
	draw_arc(center, 15 * u, 0, TAU, 24, ACCENT, maxf(1.0, 1.5 * u))
	text("gamefont_tiny", "OR", center - Vector2(0, 7 * u), "Center", ACCENT)


# Whether the pool a profession's perks count against is full.
func _full(profession: String) -> bool:
	for p: Dictionary in professions:
		if str(p.get("id", "")) == profession:
			return int(known_counts.get(str(p.get("pool", "combat")), 0)) >= cap
	return false


func _paint_perk(rect: Rect2, profession: String, perk: Dictionary, other: Dictionary, level: int) -> void:
	var mine: Dictionary = progress.get(profession, {})
	var known: Array = mine.get("perks", [])
	var paid: Array = mine.get("paid_perks", [])
	var id := str(perk.get("id", ""))
	var at := int(perk.get("level", 0))
	var learned := id in known
	var other_learned := str(other.get("id", "")) in known
	var waits := str(perk.get("needs", "")) != ""
	frame(rect, "border_3_pieces", DONE if learned else Color(0.85, 0.85, 0.9))
	var color := DONE if learned else (TEXT if at <= level else DIM)
	text("gamefont_small", "Lv %d  %s" % [at, str(perk.get("name", ""))],
			Vector2(rect.position.x + 10 * u, rect.position.y + 5 * u), "Left", color)
	var desc := str(perk.get("description", ""))
	if waits:
		desc += " (Not working yet.)"
	var lines := _wrap("gamefont_tiny", desc, rect.size.x - 150 * u, 2)
	for i in lines.size():
		text("gamefont_tiny", lines[i], Vector2(rect.position.x + 10 * u, rect.position.y + (26 + 15 * i) * u), "Left",
				WAITS if waits else DIM)
	var button_rect := Rect2(rect.end.x - 132 * u, rect.position.y + 18 * u, 122 * u, 28 * u)
	var label_at := Vector2(button_rect.get_center().x, rect.position.y + 24 * u)
	if learned:
		button(button_rect, "Give up", "forget:" + id)
	elif at > level:
		text("gamefont_tiny", "Needs level %d" % at, label_at, "Center", DIM)
	elif other_learned:
		text("gamefont_tiny", "Give up %s" % str(other.get("name", "")), label_at, "Center", DIM)
	elif _full(profession):
		text("gamefont_tiny", "Give one up first", label_at, "Center", DIM)
	else:
		var cost := 0 if id in paid else int(perk.get("cost", 0))
		button(button_rect, "Learn" if cost == 0 else "Learn %d Cr" % cost, "perk:" + id, false, false, credits >= cost)


func _on_click(id: String, _at: Vector2) -> void:
	if id == "close":
		closed.emit()
	elif id.begins_with("perk:"):
		service_requested.emit(LEARN_PERK, id.substr(5), 0)
	elif id.begins_with("forget:"):
		service_requested.emit(FORGET, id.substr(7), 0)
