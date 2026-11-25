# A hub's war terminal, in Battlefront II's style: the two sides its battles
# are fought between, each under its icon, to fight for; while queued, where
# and when the battle is, and leaving the queue. Keys work too: 1 and 2 pick
# a side, the leave-queue key leaves, Esc or the use key closes it.
extends "res://ui/bf2_screen.gd"

signal side_chosen(side: int, mode: String)
signal leave_requested
signal closed

# Set by the game before each redraw:
var title := ""          # the terminal's name
var sides: Array = []    # [{name, icon}]
var queued := -1         # the side queued for, or -1
var queue_mode := "standard"  # "standard" or "gear"
var battlefield := ""    # where the next battle is fought, once chosen
var starts_in := 0.0     # seconds
var in_queue := 0
var keys := {}           # {leave, close}: key names for the hints
const TRAVEL_DESTS := ["Naboo (Theed)", "Corellia (Coronet)", "Dantooine", "Lok", "Endor", "Yavin 4"]
var _space_tab := "battles"


func _is_space() -> bool:
	return title.to_lower().contains("space")


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.5))
	var is_space := _is_space()
	var box := centered(600, 440, Vector2(0, 14)) if is_space else centered(560, 410, Vector2(0, 14))
	text("gamefont_large", title, Vector2(size.x * 0.5, box.position.y - 44 * u), "Center", ACCENT)
	frame(box)
	if is_space:
		button(Rect2(box.position.x + 24 * u, box.position.y + 12 * u, 160 * u, 26 * u),
				"Space Battles", "stab:battles", _space_tab == "battles")
		button(Rect2(box.position.x + 192 * u, box.position.y + 12 * u, 196 * u, 26 * u),
				"Interplanetary Travel", "stab:travel", _space_tab == "travel")
		if _space_tab == "travel":
			text("gamefont_medium", "Interplanetary Shuttle & Starfighter Travel",
					Vector2(box.get_center().x, box.position.y + 54 * u), "Center", TEXT)
			text("gamefont_tiny", "Select a sector destination (Stub — Cross-planet travel coming soon):",
					Vector2(box.get_center().x, box.position.y + 82 * u), "Center", DIM)
			for di in TRAVEL_DESTS.size():
				var col := di % 2
				var row := di / 2
				var br := Rect2(box.position.x + (36 + col * 268) * u, box.position.y + (116 + row * 48) * u, 252 * u, 38 * u)
				button(br, "%s  [Offline]" % TRAVEL_DESTS[di], "travel:%d" % di, false, false, false)
			text("gamefont_tiny", "Ticket counters & hyperdrive routes will activate once additional planets are linked.",
					Vector2(box.get_center().x, box.end.y - 84 * u), "Center", ACCENT)
			button(Rect2(Vector2(box.get_center().x - 80 * u, box.end.y - 50 * u), Vector2(160, 32) * u), "Back", "close")
			text("gamefont_tiny", "%s  close" % keys.get("close", "Esc"), Vector2(size.x * 0.5, box.end.y + 14 * u), "Center", DIM)
			return
	var top_off := 36.0 * u if is_space else 0.0
	var heading := "Choose a side" if queued < 0 else "Queued (%s) for the %s" % [queue_mode.capitalize(), _side_name(queued)]
	text("gamefont_medium", heading, Vector2(box.get_center().x, box.position.y + 16 * u + top_off), "Center", TEXT)
	var mode_y := box.position.y + 44 * u + top_off
	button(Rect2(Vector2(box.get_center().x - 166 * u, mode_y), Vector2(160, 28) * u),
			"Standard Queue", "mode:standard", queue_mode == "standard", false, queued < 0)
	button(Rect2(Vector2(box.get_center().x + 6 * u, mode_y), Vector2(160, 28) * u),
			"Gear Queue", "mode:gear", queue_mode == "gear", false, queued < 0)
	var mode_desc := "Quality flattened: sidegrade choice only" if queue_mode == "standard" \
			else "Quality counts (+8% crafted band, +15% rewards)"
	text("gamefont_tiny", mode_desc, Vector2(box.get_center().x, mode_y + 32 * u), "Center", DIM)
	var card_w := 240.0
	var side_x0 := 44.0 if is_space else 24.0
	for i in sides.size():
		var card := Rect2(box.position + Vector2(side_x0 + i * (card_w + 32), 96) * u + Vector2(0, top_off), Vector2(card_w, 196) * u)
		_paint_side(card, i)
	var bottom := Rect2(box.position + Vector2(24, 308) * u + Vector2(0, top_off), Vector2(box.size.x - 48 * u, 82 * u))
	if queued >= 0:
		var where := "The battle on %s" % battlefield if battlefield != "" else "The battle"
		var when := "starts in %d" % ceili(starts_in) if starts_in > 0.5 else "is starting"
		text("gamefont_small", "%s %s" % [where, when], Vector2(bottom.get_center().x, bottom.position.y), "Center", TEXT)
		text("gamefont_tiny", "%d in the queue" % in_queue, Vector2(bottom.get_center().x, bottom.position.y + 22 * u),
				"Center", DIM)
		button(Rect2(Vector2(bottom.get_center().x - 110 * u, bottom.position.y + 42 * u), Vector2(220, 32) * u),
				"Leave the Queue", "leave")
	else:
		text("gamefont_tiny", "Bots fill the places players don't. The battlefield is chosen when the first soldier queues.",
				Vector2(bottom.get_center().x, bottom.position.y + 4 * u), "Center", DIM)
		button(Rect2(Vector2(bottom.get_center().x - 80 * u, bottom.position.y + 42 * u), Vector2(160, 32) * u),
				"Back", "close")
	var hint := "1 / 2  choose a side     %s  close" % keys.get("close", "Esc")
	if queued >= 0:
		hint = "%s  leave the queue     %s  close" % [keys.get("leave", "X"), keys.get("close", "Esc")]
	text("gamefont_tiny", hint, Vector2(size.x * 0.5, box.end.y + 14 * u), "Center", DIM)


func _paint_side(card: Rect2, i: int) -> void:
	var side: Dictionary = sides[i]
	var chosen := queued == i
	var hovered := _hover == "side:%d" % i and queued < 0
	frame(card, "border_3_pieces", Color(1, 1, 1) if not (chosen or hovered) else Color(0.75, 0.88, 1.0))
	var icon := Vector2(96, 96) * u
	var tint := ACCENT if chosen else (TEXT if queued < 0 else DIM)
	art.draw_icon(self, str(side.get("icon", "")),
			Rect2(Vector2(card.get_center().x - icon.x * 0.5, card.position.y + 18 * u), icon), tint)
	text("gamefont_medium", str(side.get("name", "")), Vector2(card.get_center().x, card.position.y + 122 * u),
			"Center", tint)
	var label := "Queued" if chosen else "Fight for the %s" % side.get("name", "")
	button(Rect2(Vector2(card.position.x + 16 * u, card.end.y - 46 * u), Vector2(card.size.x - 32 * u, 32 * u)),
			"%d  %s" % [i + 1, label], "side:%d" % i, chosen or hovered, false, queued < 0)
	if queued < 0:
		region(card, "side:%d" % i)


func _side_name(i: int) -> String:
	return str(sides[i].get("name", "")) if i >= 0 and i < sides.size() else ""


func _on_click(id: String, _at: Vector2) -> void:
	if id.begins_with("stab:"):
		_space_tab = id.substr(5)
	elif id == "mode:standard":
		queue_mode = "standard"
	elif id == "mode:gear":
		queue_mode = "gear"
	elif id.begins_with("side:"):
		side_chosen.emit(int(id.substr(5)), queue_mode)
	elif id == "leave":
		leave_requested.emit()
	elif id == "close":
		closed.emit()
