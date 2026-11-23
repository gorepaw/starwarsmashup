# Quests in Battlefront II's frames, fonts and colors. Two modes:
# - offers: what a quest giver or a mission terminal offers (a list, the
#   chosen one's story, steps and reward, and Accept);
# - log: the quest journal (active quests with their progress, Track and
#   Abandon, and the completed ones).
# Quests are the dictionaries SwmClient.get_quests() gives.
extends "res://ui/bf2_screen.gd"

signal quest_accepted(id: String)
signal quest_abandoned(id: String)
signal quest_tracked(id: String)
signal closed

const DONE_COLOR := Color(0.45, 0.85, 0.5)

var mode := "log"            # "offers" or "log"
var source := ""             # offers: who offers them
var offers: Array = []       # quests on offer
var waiting := false         # offers: asked, not answered yet
var active: Array = []       # log: active quests
var completed: Array = []    # log: completed quest titles
var tracked := ""            # the tracked quest's id
var catalog: Array = []      # item defs, for reward names
var player_position := Vector3.ZERO

var _tab := "active"         # log: "active" or "completed"
var _selected := 0
var _confirm_abandon := ""
var _scroll := 0
var _names: Dictionary = {}


func open_offers(from: String) -> void:
	mode = "offers"
	source = from
	offers = []
	waiting = true
	_selected = 0
	_scroll = 0
	visible = true
	queue_redraw()


func open_log() -> void:
	mode = "log"
	_tab = "active"
	_selected = 0
	_scroll = 0
	_confirm_abandon = ""
	visible = true
	queue_redraw()


func _list() -> Array:
	if mode == "offers":
		return offers
	return active if _tab == "active" else completed


func _item_name(def_id: String) -> String:
	if _names.size() != catalog.size():
		_names.clear()
		for def: Dictionary in catalog:
			_names[str(def.get("id", ""))] = str(def.get("name", def.get("id", "")))
	return str(_names.get(def_id, def_id))


# Words laid into lines no wider than `width` pixels.
func _wrap(font: String, value: String, width: float, scale := 1.0) -> PackedStringArray:
	var lines := PackedStringArray()
	for paragraph in value.split("\n"):
		var line := ""
		for word in paragraph.split(" ", false):
			var candidate := word if line == "" else line + " " + word
			if line != "" and art.text_width(font, candidate, u * scale) > width:
				lines.append(line)
				line = word
			else:
				line = candidate
		lines.append(line)
	return lines


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed:
		var rows := _list().size()
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_scroll = maxi(0, _scroll - 1)
			queue_redraw()
			accept_event()
			return
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_scroll = mini(maxi(0, rows - 9), _scroll + 1)
			queue_redraw()
			accept_event()
			return
	super._gui_input(event)


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.55))
	var box := centered(800, 500, Vector2(0, 10))
	var heading := source if mode == "offers" else "Quest Log"
	text("gamefont_large", heading, Vector2(size.x * 0.5, box.position.y - 42 * u), "Center", ACCENT)
	frame(box)
	var x := box.position.x + 18 * u
	var y := box.position.y + 14 * u
	var list := _list()
	_selected = clampi(_selected, 0, maxi(0, list.size() - 1))

	if mode == "log":
		button(Rect2(x, y, 140 * u, 28 * u), "Active (%d)" % active.size(), "tab:active", _tab == "active")
		button(Rect2(x + 148 * u, y, 160 * u, 28 * u), "Completed (%d)" % completed.size(), "tab:completed",
				_tab == "completed")
	else:
		text("gamefont_medium", "Missions" if offers.any(func(q: Dictionary) -> bool: return bool(q.get("mission", false)))
				else "Work on offer", Vector2(x, y + 4 * u), "Left", TEXT)
	button(Rect2(box.end.x - 110 * u, y, 92 * u, 28 * u), "Close", "close", false, true)
	y += 40 * u

	# The list, left.
	var list_rect := Rect2(x, y, 250 * u, box.end.y - y - 18 * u)
	draw_rect(list_rect, Color(0.04, 0.06, 0.09, 0.6))
	var row_h := 38.0 * u
	if list.is_empty():
		var empty := "Asking..." if waiting else ("No work for you right now." if mode == "offers"
				else ("No active quests. Look for people with a marker over their heads, or a mission terminal."
				if _tab == "active" else "Nothing finished yet."))
		var ly := list_rect.position.y + 10 * u
		for line in _wrap("gamefont_tiny", empty, list_rect.size.x - 20 * u):
			text("gamefont_tiny", line, Vector2(list_rect.position.x + 10 * u, ly), "Left", DIM)
			ly += art.line_height("gamefont_tiny", u) + 2 * u
	for i in range(_scroll, mini(list.size(), _scroll + 9)):
		var row := Rect2(list_rect.position.x + 4 * u, list_rect.position.y + 4 * u + (i - _scroll) * row_h,
				list_rect.size.x - 8 * u, row_h - 4 * u)
		var chosen := i == _selected
		draw_rect(row, Color(0.25, 0.32, 0.45, 0.55) if chosen else (Color(0.18, 0.22, 0.3, 0.4)
				if _hover == "row:%d" % i else Color(0.1, 0.13, 0.18, 0.4)))
		region(row, "row:%d" % i)
		if mode == "log" and _tab == "completed":
			text("gamefont_tiny", str(list[i]), row.position + Vector2(8, 10) * u, "Left", DONE_COLOR)
			continue
		var quest: Dictionary = list[i]
		var title := str(quest.get("title", ""))
		var mark := "> " if mode == "log" and str(quest.get("id", "")) == tracked else ""
		text("gamefont_tiny", mark + title, row.position + Vector2(8, 4) * u, "Left", ACCENT if chosen else TEXT)
		var sub := ""
		if mode == "log":
			var steps: Array = quest.get("steps", [])
			sub = str(steps.back()) if not steps.is_empty() else ""
			if int(quest.get("needed", 0)) > 1:
				sub += "  %d/%d" % [int(quest.get("count", 0)), int(quest.get("needed", 0))]
		else:
			sub = "%d Cr  %d XP" % [int(quest.get("credits", 0)), int(quest.get("experience", 0))]
		text("gamefont_tiny", _fit(sub, row.size.x - 16 * u), row.position + Vector2(8, 19) * u, "Left", DIM)

	# The chosen quest, right.
	var detail := Rect2(list_rect.end.x + 16 * u, y, box.end.x - list_rect.end.x - 34 * u, list_rect.size.y)
	if list.is_empty() or (mode == "log" and _tab == "completed"):
		return
	var quest: Dictionary = list[_selected]
	_paint_detail(quest, detail)


func _fit(value: String, width: float) -> String:
	if art.text_width("gamefont_tiny", value, u) <= width:
		return value
	while value.length() > 1 and art.text_width("gamefont_tiny", value + "...", u) > width:
		value = value.substr(0, value.length() - 1)
	return value + "..."


func _paint_detail(quest: Dictionary, rect: Rect2) -> void:
	var x := rect.position.x
	var y := rect.position.y
	text("gamefont_medium", str(quest.get("title", "")), Vector2(x, y), "Left", ACCENT)
	y += 26 * u
	var giver := str(quest.get("giver", ""))
	var where := ""
	if quest.has("waypoint"):
		var at: Vector3 = quest["waypoint"]
		where = "   |   %s" % _distance(Vector2(at.x - player_position.x, at.z - player_position.z).length())
	if giver != "":
		text("gamefont_tiny", "From: " + giver + where, Vector2(x, y), "Left", DIM)
		y += 20 * u
	var line_h: float = art.line_height("gamefont_tiny", u) + 2 * u
	for line in _wrap("gamefont_tiny", str(quest.get("summary", "")), rect.size.x):
		text("gamefont_tiny", line, Vector2(x, y), "Left", TEXT)
		y += line_h
	y += 10 * u
	text("gamefont_small", "Steps", Vector2(x, y), "Left", ACCENT)
	y += 22 * u
	var steps: Array = quest.get("steps", [])
	var current := int(quest.get("step", 0)) if mode == "log" else -1
	for i in steps.size():
		var done := mode == "log" and i < current
		var label := str(steps[i])
		if mode == "log" and i == current and int(quest.get("needed", 0)) > 1:
			label += "  (%d/%d)" % [int(quest.get("count", 0)), int(quest.get("needed", 0))]
		var color := DONE_COLOR if done else (TEXT if mode == "offers" or i == current else DIM)
		var bullet := "[x] " if done else ("[ ] " if mode == "log" else "%d. " % (i + 1))
		for line in _wrap("gamefont_tiny", bullet + label, rect.size.x - 10 * u):
			text("gamefont_tiny", line, Vector2(x + 6 * u, y), "Left", color)
			y += line_h
	y += 10 * u
	text("gamefont_small", "Reward", Vector2(x, y), "Left", ACCENT)
	y += 22 * u
	var reward := PackedStringArray()
	if int(quest.get("credits", 0)) > 0:
		reward.append("%d Credits" % int(quest["credits"]))
	if int(quest.get("experience", 0)) > 0:
		reward.append("%d XP" % int(quest["experience"]))
	if int(quest.get("faction_points", 0)) > 0:
		reward.append("%d Faction Points" % int(quest["faction_points"]))
	for item in quest.get("items", []):
		reward.append(_item_name(str(item)))
	for line in _wrap("gamefont_tiny", ", ".join(reward) if not reward.is_empty() else "None", rect.size.x):
		text("gamefont_tiny", line, Vector2(x, y), "Left", TEXT)
		y += line_h

	var buttons_y := rect.end.y - 30 * u
	var id := str(quest.get("id", ""))
	if mode == "offers":
		button(Rect2(rect.end.x - 140 * u, buttons_y, 140 * u, 30 * u), "Accept", "accept:" + id, false, true)
	else:
		var is_tracked := id == tracked
		button(Rect2(x, buttons_y, 130 * u, 30 * u), "Tracked" if is_tracked else "Track", "track:" + id, is_tracked)
		var confirming := _confirm_abandon == id
		button(Rect2(rect.end.x - 170 * u, buttons_y, 170 * u, 30 * u), "Confirm Abandon" if confirming else "Abandon",
				"abandon:" + id, confirming)


func _distance(meters: float) -> String:
	return "%d m" % roundi(meters) if meters < 1000 else "%.1f km" % (meters / 1000.0)


func _on_click(id: String, _at: Vector2) -> void:
	if id == "close":
		closed.emit()
	elif id.begins_with("tab:"):
		_tab = id.substr(4)
		_selected = 0
		_scroll = 0
		_confirm_abandon = ""
	elif id.begins_with("row:"):
		_selected = int(id.substr(4))
		_confirm_abandon = ""
	elif id.begins_with("accept:"):
		quest_accepted.emit(id.substr(7))
	elif id.begins_with("track:"):
		quest_tracked.emit(id.substr(6))
	elif id.begins_with("abandon:"):
		var quest_id := id.substr(8)
		if _confirm_abandon == quest_id:
			_confirm_abandon = ""
			quest_abandoned.emit(quest_id)
		else:
			_confirm_abandon = quest_id
	queue_redraw()
