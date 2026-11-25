# Choosing how and where to spawn in a match, in Battlefront II's style (its
# PC spawn screen): the side's units on the left, the battlefield's map on
# the right with the command posts (click one your side holds), and a Spawn
# button. Keys still work: 1-9 pick a unit, Q / E cycle posts, Space spawns.
extends "res://ui/bf2_screen.gd"

signal spawn_requested
signal loadout_chosen(index: int)
signal post_chosen(index: int)

# Set by the game before each redraw:
var title := ""            # e.g. "Dagobah"
var side := {}             # {name, icon, color}
var loadouts: Array = []   # [{name, weapons: [String], max_health}]
var loadout := 0
var posts: Array = []      # [{name, position: Vector2 (game x, z), color, ours}]
var post := -1             # chosen, or -1 if the side holds none
var minimap := {}          # {texture, left_x, top_z, right_x, bottom_z}; empty: posts on a plain panel
var waiting := false       # asked to spawn, not spawned yet
var notice := ""           # e.g. "No reinforcements left"


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.5))
	var panel_h := 470.0
	var top := (size.y / u - panel_h) * 0.5 + 10
	var units := Rect2(Vector2(size.x * 0.5 - 400 * u, top * u), Vector2(300, panel_h) * u)
	var map_box := Rect2(Vector2(size.x * 0.5 - 85 * u, top * u), Vector2(485, panel_h) * u)
	text("gamefont_large", title, Vector2(size.x * 0.5, (top - 44) * u), "Center", ACCENT)
	_paint_units(units)
	_paint_map(map_box)


func _paint_units(box: Rect2) -> void:
	frame(box)
	var x := box.position.x + 16 * u
	var y := box.position.y + 12 * u
	art.draw_icon(self, str(side.get("icon", "")), Rect2(Vector2(x, y), Vector2(30, 30) * u), side.get("color", Color.WHITE))
	text("gamefont_medium", str(side.get("name", "")), Vector2(x + 40 * u, y + 5 * u), "Left", side.get("color", TEXT))
	y += 42 * u
	for i in loadouts.size():
		var entry: Dictionary = loadouts[i]
		button(Rect2(Vector2(x, y), Vector2(box.size.x - 32 * u, 28 * u)), "%d  %s" % [i + 1, entry["name"]],
				"unit:%d" % i, i == loadout)
		y += 32 * u
	if loadout < loadouts.size():
		var chosen: Dictionary = loadouts[loadout]
		y += 6 * u
		for weapon: String in chosen["weapons"]:
			text("gamefont_small", weapon, Vector2(x + 6 * u, y), "Left", DIM)
			y += 18 * u
		text("gamefont_small", "Health %d" % roundi(float(chosen["max_health"])), Vector2(x + 6 * u, y), "Left", DIM)
	var spawn_rect := Rect2(Vector2(x, box.end.y - 52 * u), Vector2(box.size.x - 32 * u, 38 * u))
	if notice != "":
		text("gamefont_small", notice, Vector2(box.get_center().x, spawn_rect.position.y - 26 * u), "Center", Color(1, 0.4, 0.35))
	button(spawn_rect, "Spawning..." if waiting else "Spawn", "spawn", false, true, post >= 0 and not waiting)


func _paint_map(box: Rect2) -> void:
	frame(box)
	var area := Rect2(box.position + Vector2(14, 14) * u, box.size - Vector2(28, 28) * u)
	var square := minf(area.size.x, area.size.y)
	var view := Rect2(area.get_center() - Vector2(square, square) * 0.5, Vector2(square, square))
	var to_view := func(p: Vector2) -> Vector2: return view.get_center()
	if not minimap.is_empty():
		draw_texture_rect(minimap["texture"], view, false, Color(1, 1, 1, 0.92))
		var left := float(minimap["left_x"])
		var top := float(minimap["top_z"])
		var span := Vector2(float(minimap["right_x"]) - left, float(minimap["bottom_z"]) - top)
		to_view = func(p: Vector2) -> Vector2:
			return view.position + Vector2((p.x - left) / span.x, (p.y - top) / span.y) * view.size
	elif not posts.is_empty():
		# No map image: fit the posts into the panel.
		var bounds := Rect2(posts[0]["position"], Vector2.ZERO)
		for p: Dictionary in posts:
			bounds = bounds.expand(p["position"])
		bounds = bounds.grow(maxf(bounds.size.x, bounds.size.y) * 0.15 + 10)
		var extent := maxf(bounds.size.x, bounds.size.y)
		to_view = func(p: Vector2) -> Vector2:
			return view.position + (p - bounds.get_center() + Vector2(extent, extent) * 0.5) / extent * view.size
	var flash: Texture2D = art.texture("hud_icon_flash")
	for i in posts.size():
		var p: Dictionary = posts[i]
		var at: Vector2 = to_view.call(p["position"])
		var r := 11.0 * u
		if i == post and flash != null:
			draw_texture_rect(flash, Rect2(at - Vector2(r, r) * 1.9, Vector2(r, r) * 3.8), false, Color(1, 1, 1, 0.9))
		var icon: Texture2D = art.texture("hud_flag_icon")  # the posts' map icon
		if p.get("mobile", false):
			draw_circle(at, r * 0.95, Color(0, 0, 0, 0.6))
			draw_rect(Rect2(at - Vector2(r, r) * 0.65, Vector2(r, r) * 1.3), p["color"])
		elif icon != null:
			draw_circle(at, r * 0.9, Color(0, 0, 0, 0.5))
			draw_texture_rect(icon, Rect2(at - Vector2(r, r), Vector2(r, r) * 2), false, p["color"])
		else:
			draw_circle(at, r, p["color"])
		text("gamefont_tiny", str(p["name"]), at + Vector2(0, r + 2 * u), "Center", TEXT)
		if p.get("ours", false):
			region(Rect2(at - Vector2(r, r) * 1.6, Vector2(r, r) * 3.2), "post:%d" % i)


func _on_click(id: String, _at: Vector2) -> void:
	if id.begins_with("unit:"):
		loadout_chosen.emit(int(id.substr(5)))
	elif id.begins_with("post:"):
		post_chosen.emit(int(id.substr(5)))
	elif id == "spawn":
		spawn_requested.emit()
