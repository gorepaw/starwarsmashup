# A hub's battle terminal, in Battlefront II's style. A Ground Battle
# Terminal queues for Battlefront's ground battles, a Space Terminal (in the
# Starport) for its space battles and, once more planets open, interplanetary
# travel. Either way the player picks the era first (Galactic Civil War or
# Clone Wars: its two sides, each under its icon, to fight for), and on the
# ground the game mode (Conquest, Capture the Flag, 1-Flag CTF, Hunt, XL or
# any). While queued: where and when the battle is, and leaving the queue.
# Keys work too: 1 and 2 pick a side of the era shown, the leave-queue key
# leaves, Esc or the use key closes it.
extends "res://ui/bf2_screen.gd"

signal side_chosen(side: int, mode: String)
signal leave_requested
signal closed

# Set by the game before each redraw:
var title := ""          # the terminal's name
var kind := "ground"     # "ground", "space" or "both"
var sides: Array = []    # [{name, icon}]: the hub's, two per era (GCW first, then Clone Wars)
var queued := -1         # the side queued for, or -1
var battlefield := ""    # where the next battle is fought, once chosen
var starts_in := 0.0     # seconds
var in_queue := 0
var keys := {}           # {leave, close}: key names for the hints

const TRAVEL_DESTS := ["Naboo (Theed)", "Corellia (Coronet)", "Dantooine", "Lok", "Endor", "Yavin 4"]
const ERAS := ["Galactic Civil War", "Clone Wars"]
# Battlefront II's ground modes, by id (core::game_mode_id), and "any".
const GROUND_MODES := [
	["conquest", "Conquest", "Hold command posts; holding more bleeds their reinforcements."],
	["ctf", "Capture the Flag", "Take their flag from their base to yours. 5 captures win."],
	["1flag", "1-Flag CTF", "One flag in the middle: carry it into their base. 5 captures win."],
	["hunt", "Hunt", "No posts to take: the first side to the kill limit wins."],
	["xl", "XL", "Team deathmatch with 32 a side: reinforcements decide it."],
	["any", "Any Mode", "Whatever the next battlefield offers."],
]

var _space_tab := "battles"
var _era := 0
var _mode := "conquest"


func _is_space() -> bool:
	return kind == "space"


# The era queued in follows the side; otherwise the one picked.
func _shown_era() -> int:
	if queued >= 0:
		return 0 if queued < 2 else 1
	return _era if sides.size() > 2 else 0


func _era_sides() -> Array:
	var first := _shown_era() * 2
	var out: Array = []
	for i in range(first, mini(first + 2, sides.size())):
		out.append(i)
	return out


# Picks the n-th side of the era shown (keys 1 and 2).
func pick(n: int) -> void:
	var shown := _era_sides()
	if queued < 0 and n >= 0 and n < shown.size():
		side_chosen.emit(int(shown[n]), "assault" if _is_space() else _mode)


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.5))
	var is_space := _is_space()
	var box := centered(660, 540 if not is_space else 470, Vector2(0, 14))
	text("gamefont_large", title, Vector2(size.x * 0.5, box.position.y - 44 * u), "Center", ACCENT)
	frame(box)
	var y := box.position.y + 12 * u
	if is_space:
		button(Rect2(box.position.x + 24 * u, y, 160 * u, 26 * u), "Space Battles", "stab:battles", _space_tab == "battles")
		button(Rect2(box.position.x + 192 * u, y, 196 * u, 26 * u), "Interplanetary Travel", "stab:travel",
				_space_tab == "travel")
		if _space_tab == "travel":
			_paint_travel(box)
			return
		y += 36 * u
	# The era: Galactic Civil War or Clone Wars.
	if sides.size() > 2:
		var era_w := 240.0 * u
		var x0 := box.get_center().x - era_w - 6 * u
		for e in ERAS.size():
			var enabled := queued < 0
			button(Rect2(x0 + e * (era_w + 12 * u), y, era_w, 28 * u), ERAS[e], "era:%d" % e, _shown_era() == e, false,
					enabled)
		y += 38 * u
	var heading := "Choose a side" if queued < 0 else "Queued for the %s" % _side_name(queued)
	text("gamefont_medium", heading, Vector2(box.get_center().x, y + 6 * u), "Center", TEXT)
	text("gamefont_tiny", "Gear-Matters Rules: equipped loadouts, +15% rewards, Hard bots",
			Vector2(box.get_center().x, y + 30 * u), "Center", DIM)
	y += 48 * u
	# The mode (ground): Battlefront II's.
	if not is_space:
		var mode_w := 100.0 * u
		var gap := 6.0 * u
		var mx := box.get_center().x - (GROUND_MODES.size() * mode_w + (GROUND_MODES.size() - 1) * gap) * 0.5
		for i in GROUND_MODES.size():
			var m: Array = GROUND_MODES[i]
			button(Rect2(mx + i * (mode_w + gap), y, mode_w, 26 * u), str(m[1]), "mode:%s" % m[0], _mode == m[0], false,
					queued < 0)
		y += 32 * u
		text("gamefont_tiny", _mode_blurb(), Vector2(box.get_center().x, y + 2 * u), "Center", DIM)
		y += 22 * u
	else:
		text("gamefont_tiny", "Space Assault: points for kills and for the enemy capital ship's systems.",
				Vector2(box.get_center().x, y), "Center", DIM)
		y += 22 * u
	# The era's two sides.
	var shown := _era_sides()
	var card_w := 240.0
	var side_x0 := (box.size.x / u - (shown.size() * card_w + maxi(0, shown.size() - 1) * 32.0)) * 0.5
	for n in shown.size():
		var card := Rect2(Vector2(box.position.x + (side_x0 + n * (card_w + 32)) * u, y), Vector2(card_w, 196) * u)
		_paint_side(card, int(shown[n]), n)
	var bottom := Rect2(Vector2(box.position.x + 24 * u, box.end.y - 92 * u), Vector2(box.size.x - 48 * u, 82 * u))
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
	var hint := "1-2  choose a side     %s  close" % keys.get("close", "Esc")
	if queued >= 0:
		hint = "%s  leave the queue     %s  close" % [keys.get("leave", "X"), keys.get("close", "Esc")]
	text("gamefont_tiny", hint, Vector2(size.x * 0.5, box.end.y + 14 * u), "Center", DIM)


func _paint_travel(box: Rect2) -> void:
	text("gamefont_medium", "Interplanetary Shuttle & Starfighter Travel",
			Vector2(box.get_center().x, box.position.y + 54 * u), "Center", TEXT)
	text("gamefont_tiny", "Select a sector destination (cross-planet travel opens with more planets):",
			Vector2(box.get_center().x, box.position.y + 82 * u), "Center", DIM)
	for di in TRAVEL_DESTS.size():
		var col := di % 2
		var row := di / 2
		var br := Rect2(box.position.x + (56 + col * 284) * u, box.position.y + (116 + row * 48) * u, 266 * u, 38 * u)
		button(br, "%s  [Offline]" % TRAVEL_DESTS[di], "travel:%d" % di, false, false, false)
	text("gamefont_tiny", "Ticket counters & hyperdrive routes will activate once additional planets are linked.",
			Vector2(box.get_center().x, box.end.y - 84 * u), "Center", ACCENT)
	button(Rect2(Vector2(box.get_center().x - 80 * u, box.end.y - 50 * u), Vector2(160, 32) * u), "Back", "close")
	text("gamefont_tiny", "%s  close" % keys.get("close", "Esc"), Vector2(size.x * 0.5, box.end.y + 14 * u), "Center", DIM)


func _mode_blurb() -> String:
	for m: Array in GROUND_MODES:
		if m[0] == _mode:
			return str(m[2])
	return ""


func _paint_side(card: Rect2, i: int, n: int) -> void:
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
			"%d  %s" % [n + 1, label], "side:%d" % i, chosen or hovered, false, queued < 0)
	if queued < 0:
		region(card, "side:%d" % i)


func _side_name(i: int) -> String:
	return str(sides[i].get("name", "")) if i >= 0 and i < sides.size() else ""


func _on_click(id: String, _at: Vector2) -> void:
	if id.begins_with("stab:"):
		_space_tab = id.substr(5)
	elif id.begins_with("era:") and queued < 0:
		_era = int(id.substr(4))
	elif id.begins_with("mode:") and queued < 0:
		_mode = id.substr(5)
	elif id.begins_with("side:"):
		side_chosen.emit(int(id.substr(5)), "assault" if _is_space() else _mode)
	elif id == "leave":
		leave_requested.emit()
	elif id == "close":
		closed.emit()
	queue_redraw()
