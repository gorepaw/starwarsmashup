# Scores (held Tab) and the end of a match, in Battlefront II's style: the two
# sides in panels under their icons and reinforcements, a row per soldier
# (kills, deaths, captures under Battlefront's stats icons), yours
# highlighted; at the end, victory or defeat and the next match's countdown.
extends "res://ui/bf2_screen.gd"

# Set by the game before each redraw:
var sides: Array = []      # per team: {name, icon, color, tickets, players: [{name, kills, deaths, captures, you}]}
var own_team := -1
var ended := false
var winner := -1
var next_in := 0.0         # seconds to the next match, at the end


func _init(shared_art: RefCounted = null) -> void:
	super._init(shared_art)
	mouse_filter = Control.MOUSE_FILTER_IGNORE


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.35 if not ended else 0.55))
	var top := 60.0
	if ended:
		var headline := "Draw" if winner < 0 else ("Victory" if winner == own_team else "Defeat")
		var color := ACCENT if winner < 0 else (Color(0.4, 0.65, 1.0) if winner == own_team else Color(1.0, 0.35, 0.3))
		text("gamefont_large", headline, Vector2(size.x * 0.5, 34 * u), "Center", color, 1.6)
		if winner >= 0 and winner < sides.size():
			text("gamefont_medium", "%s win" % sides[winner]["name"], Vector2(size.x * 0.5, 74 * u), "Center", TEXT)
		top = 110.0
	var width := 330.0
	for i in sides.size():
		var side: Dictionary = sides[i]
		var x: float = size.x * 0.5 + (-width - 10 if i == 0 else 10) * u
		_paint_side(Rect2(Vector2(x, top * u), Vector2(width, 600 - top - 60) * u), side)
	if ended:
		text("gamefont_medium", "Next battle in %d" % ceili(next_in), Vector2(size.x * 0.5, size.y - 44 * u), "Center", DIM)


func _paint_side(box: Rect2, side: Dictionary) -> void:
	frame(box)
	var x: float = box.position.x + 16 * u
	var y := box.position.y + 12 * u
	art.draw_icon(self, side["icon"], Rect2(Vector2(x, y), Vector2(34, 34) * u), side["color"])
	text("gamefont_large", side["name"], Vector2(x + 44 * u, y + 2 * u), "Left", side["color"])
	text("gamefont_large", str(ceili(float(side["tickets"]))), Vector2(box.end.x - 16 * u, y + 2 * u), "Right", side["color"])
	y += 48 * u
	# Column heads: Battlefront's stats icons.
	var columns := [box.end.x - 120 * u, box.end.x - 75 * u, box.end.x - 30 * u]
	for c in 3:
		art.draw_icon(self, ["stats_kills", "stats_deaths", "stats_cpp"][c],
				Rect2(Vector2(columns[c] - 11 * u, y), Vector2(22, 22) * u), Color(1, 1, 1, 0.85))
	y += 28 * u
	var players: Array = side["players"].duplicate()
	players.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		return int(a["kills"]) > int(b["kills"]) if int(a["kills"]) != int(b["kills"]) else int(a["deaths"]) < int(b["deaths"]))
	var row := 21.0 * u
	for p: Dictionary in players:
		if y + row > box.end.y - 8 * u:
			break
		if p.get("you", false):
			art.draw_icon(self, "listbox_hilight", Rect2(Vector2(box.position.x + 8 * u, y - 2 * u),
					Vector2(box.size.x - 16 * u, row)), Color(1, 1, 1, 0.9))
		var color := ACCENT if p.get("you", false) else TEXT
		text("gamefont_small", str(p["name"]), Vector2(x, y), "Left", color)
		for c in 3:
			text("gamefont_small", str(p[["kills", "deaths", "captures"][c]]), Vector2(columns[c], y), "Center", color)
		y += row
