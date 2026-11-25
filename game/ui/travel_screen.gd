# A shuttleport's (or starport's) travel terminal, in Battlefront II's frame:
# the planet's other travel points by town, and a ticket to any of them
# (Stage 13). Choosing one sends a ServiceRequest (travel, its name); the
# server takes the fare and puts you off at the far end.
#
#   screen.open_travel("Mos Eisley Shuttleport", destinations, credits)
#   destinations: [{name, detail (its town), position}], this one included
extends "res://ui/bf2_screen.gd"

signal service_requested(action: int, id: String, amount: int, item_id: int)
signal closed

const TRAVEL_ACTION := 7  # net::ServiceRequest::travel
const FARE := 100         # a shuttle on this planet, as the server charges (server_services.cpp travel_fare)

var title := "Travel Terminal"
var here := ""
var credits := 0
var _destinations: Array = []  # the others, by town then name
var _selected := 0
var _first := 0  # the first row shown (the list scrolls)


func open_travel(p_here: String, destinations: Array, p_credits: int) -> void:
	here = p_here
	title = p_here
	credits = p_credits
	_destinations.clear()
	var from := Vector3.ZERO
	for d: Dictionary in destinations:
		if str(d.get("name", "")) == here:
			from = d.get("position", Vector3.ZERO)
	for d: Dictionary in destinations:
		if str(d.get("name", "")) != here:
			var entry := d.duplicate()
			var at: Vector3 = d.get("position", Vector3.ZERO)
			entry["distance"] = Vector2(at.x - from.x, at.z - from.z).length()
			entry["fare"] = FARE
			entry["planet"] = ""
			# Another planet's starport (Stage 13): Detail "<planet>|<fare>".
			if str(d.get("kind", "")) == "travel_offworld":
				var parts := str(d.get("detail", "")).split("|")
				entry["planet"] = parts[0]
				entry["fare"] = int(parts[1]) if parts.size() > 1 else 1000
				entry["distance"] = 1e9
			_destinations.append(entry)
	_destinations.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if str(a["planet"]) != str(b["planet"]):
			return str(a["planet"]) < str(b["planet"])
		return float(a["distance"]) < float(b["distance"]))
	_selected = 0
	_first = 0
	visible = true
	queue_redraw()


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.5))
	var box := centered(620, 420, Vector2(0, 10))
	text("gamefont_large", title.to_upper(), Vector2(size.x * 0.5, box.position.y - 40 * u), "Center", ACCENT)
	frame(box)
	var x := box.position.x + 18 * u
	var y := box.position.y + 14 * u
	button(Rect2(box.end.x - 108 * u, y, 90 * u, 28 * u), "Close", "close", false, true)
	text("gamefont_medium", "Shuttles & Starships", Vector2(x, y + 4 * u), "Left", TEXT)
	y += 30 * u
	text("gamefont_tiny", "You carry %d Cr" % credits, Vector2(x, y), "Left", DIM)
	y += 26 * u
	var list := Rect2(x, y, box.size.x - 36 * u, box.end.y - y - 64 * u)
	draw_rect(list, Color(0.04, 0.06, 0.09, 0.6))
	if _destinations.is_empty():
		text("gamefont_small", "No other shuttleports are open.", list.get_center(), "Center", DIM)
		return
	_selected = clampi(_selected, 0, _destinations.size() - 1)
	var row_h := 34 * u
	var fits := int(list.size.y / row_h)
	_first = clampi(_first, 0, maxi(0, _destinations.size() - fits))
	var rows := mini(_destinations.size() - _first, fits)
	if _destinations.size() > fits:
		button(Rect2(list.end.x - 70 * u, list.end.y + 6 * u, 32 * u, 26 * u), "^", "up", false, false, _first > 0)
		button(Rect2(list.end.x - 34 * u, list.end.y + 6 * u, 32 * u, 26 * u), "v", "down", false, false,
				_first + fits < _destinations.size())
	for r in rows:
		var i := _first + r
		var d: Dictionary = _destinations[i]
		var row := Rect2(list.position.x + 4 * u, list.position.y + 4 * u + r * row_h, list.size.x - 8 * u, row_h - 4 * u)
		draw_rect(row, Color(0.25, 0.32, 0.45, 0.55) if i == _selected else Color(0.1, 0.13, 0.18, 0.4))
		region(row, "dest:%d" % i)
		text("gamefont_small", str(d.get("name", "")), row.position + Vector2(10, 6) * u, "Left", ACCENT if i == _selected else TEXT)
		var where := str(d["planet"]) if str(d["planet"]) != "" else "%s   %.1f km" % [
				str(d.get("detail", "")), float(d["distance"]) / 1000.0]
		var fare := int(d["fare"])
		text("gamefont_tiny", "%s   %d Cr" % [where, fare], Vector2(row.end.x - 10 * u, row.position.y + 9 * u),
				"Right", DIM if credits >= fare else Color(1.0, 0.45, 0.35))
	var chosen: Dictionary = _destinations[_selected]
	var go := Rect2(box.position.x + 18 * u, box.end.y - 50 * u, 200 * u, 32 * u)
	button(go, "Board Starship" if str(chosen["planet"]) != "" else "Board Shuttle", "board", false, false,
			credits >= int(chosen["fare"]))


func _on_click(id: String, _at: Vector2) -> void:
	if id == "close":
		closed.emit()
	elif id.begins_with("dest:"):
		_selected = int(id.substr(5))
	elif id == "up":
		_first -= 1
	elif id == "down":
		_first += 1
	elif id == "board" and _selected < _destinations.size() and credits >= int(_destinations[_selected]["fare"]):
		service_requested.emit(TRAVEL_ACTION, str(_destinations[_selected]["name"]), 0, 0)
		closed.emit()
