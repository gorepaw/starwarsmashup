# A skill trainer's screen, in Battlefront II's frame: each profession they
# teach, with its milestones (certifications, gadgets, abilities) in order. A
# level earns a milestone; a trainer teaches it for credits (the first is
# free), one at a time, in order, and it counts from then.
#
#   screen.trainer_name = ...; screen.professions = [{id, name, milestones: [{level, name, description, cost}]}]
#   screen.progress = {profession id: {xp, level, trained}}; screen.credits = ...
#   screen.open_trainer()          # signal train_requested(profession id, level)
extends "res://ui/bf2_screen.gd"

signal train_requested(profession: String, level: int)
signal closed

var trainer_name := "Trainer"
var professions: Array = []      # the ones this trainer teaches (their defs)
var progress := {}               # profession id -> {level, trained}
var credits := 0
var feedback := ""

var _scroll := 0
var _rows: Array = []            # [{kind: "head" | "milestone", ...}]
const ROWS_VISIBLE := 7


func open_trainer() -> void:
	_scroll = 0
	feedback = ""
	visible = true
	queue_redraw()


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed \
			and (event as InputEventMouseButton).button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		var step := -1 if (event as InputEventMouseButton).button_index == MOUSE_BUTTON_WHEEL_UP else 1
		_scroll = clampi(_scroll + step, 0, maxi(0, _rows.size() - ROWS_VISIBLE))
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
		var trained := int(mine.get("trained", 0))
		_rows.append({"kind": "head", "name": str(p.get("name", id)), "level": level})
		var next_level := 0  # the next one not yet learned
		for m: Dictionary in p.get("milestones", []):
			if int(m["level"]) > trained and (next_level == 0 or int(m["level"]) < next_level):
				next_level = int(m["level"])
		for m: Dictionary in p.get("milestones", []):
			var at := int(m["level"])
			var state := "learned" if at <= trained else ("ready" if at == next_level and at <= level else
					("later" if at == next_level else "after"))
			_rows.append({"kind": "milestone", "profession": id, "milestone": m, "state": state, "level": level})


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.55))
	var box := centered(760, 470, Vector2(0, 10))
	text("gamefont_large", trainer_name, Vector2(size.x * 0.5, box.position.y - 42 * u), "Center", ACCENT)
	draw_rect(box, Color(0.04, 0.05, 0.07, 0.9))
	frame(box)
	var x := box.position.x + 18 * u
	var y := box.position.y + 14 * u
	text("gamefont_medium", "Learn what you've earned", Vector2(x, y), "Left", TEXT)
	text("gamefont_medium", "%d Credits" % credits, Vector2(box.end.x - 130 * u, y), "Right", ACCENT)
	button(Rect2(box.end.x - 110 * u, y - 2 * u, 92 * u, 28 * u), "Close", "close", false, true)
	y += 40 * u
	_build_rows()
	_scroll = clampi(_scroll, 0, maxi(0, _rows.size() - ROWS_VISIBLE))
	var row_h := 44.0 * u
	var width := box.size.x - 36 * u
	for i in ROWS_VISIBLE:
		var index := _scroll + i
		if index >= _rows.size():
			break
		var row: Dictionary = _rows[index]
		var ry := y + i * (row_h + 3 * u)
		if str(row["kind"]) == "head":
			text("gamefont_medium", "%s   Level %d" % [row["name"], int(row["level"])], Vector2(x, ry + 10 * u), "Left", ACCENT)
			continue
		var m: Dictionary = row["milestone"]
		var rect := Rect2(x, ry, width, row_h)
		frame(rect, "border_3_pieces", Color(0.85, 0.85, 0.9))
		var state := str(row["state"])
		var color := DIM if state == "after" or state == "later" else (Color(0.5, 0.9, 0.55) if state == "learned" else TEXT)
		text("gamefont_small", "Lv %d   %s" % [int(m["level"]), str(m["name"])], Vector2(x + 12 * u, ry + 5 * u), "Left", color)
		text("gamefont_tiny", str(m.get("description", "")), Vector2(x + 12 * u, ry + 25 * u), "Left", DIM)
		var cost := int(m.get("cost", 0))
		var button_rect := Rect2(rect.end.x - 170 * u, ry + 8 * u, 150 * u, 28 * u)
		match state:
			"learned":
				text("gamefont_small", "Learned", Vector2(button_rect.get_center().x, ry + 12 * u), "Center", color)
			"ready":
				button(button_rect, "Learn" if cost == 0 else "Learn  %d Cr" % cost,
						"learn:%s:%d" % [row["profession"], int(m["level"])], false, false, credits >= cost)
			"later":
				text("gamefont_tiny", "Needs level %d" % int(m["level"]), Vector2(button_rect.get_center().x, ry + 14 * u), "Center", DIM)
			_:
				text("gamefont_tiny", "Learn the one before", Vector2(button_rect.get_center().x, ry + 14 * u), "Center", DIM)
	if _rows.size() > ROWS_VISIBLE:
		text("gamefont_tiny", "Scroll (%d-%d of %d)" % [_scroll + 1, mini(_scroll + ROWS_VISIBLE, _rows.size()), _rows.size()],
				Vector2(box.get_center().x, box.end.y - 22 * u), "Center", DIM)
	if feedback != "":
		text("gamefont_small", feedback, Vector2(box.get_center().x, box.end.y - 44 * u), "Center", ACCENT)


func _on_click(id: String, _at: Vector2) -> void:
	if id == "close":
		closed.emit()
	elif id.begins_with("learn:"):
		var parts := id.split(":")
		train_requested.emit(parts[1], int(parts[2]))
