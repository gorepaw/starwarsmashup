# The world's map (M), as Galaxies' planetary map was: the town around you
# seen from overhead (rendered once from the loaded world when it opens, a
# picture about a meter a pixel), north up, with its places marked: where to
# shop, who gives quests, where to craft, the terminals, your quest's
# waypoint and you. A list beside it names them (nearest first in each kind),
# and hovering one rings it on the map; hovering a marker on the map names it
# in a tooltip. The mouse wheel zooms: 300 m, 700 m,
# 1.6 km across, then the whole planet (its ground preview, as the radar's).
# In Battlefront II's frames and fonts.
#
#   map.places = [{position, name, kind: "battle" | "quest" | "shop" | "craft" | "service" | "terminal", color}]
#   map.player_position = ...; map.player_yaw = ...; map.waypoint = {position, name}
#   map.planet = {texture, left_x, top_z, right_x, bottom_z}   # the radar's minimap
#   map.towns = [{name, position}]   # the planet's towns, named on the map zoomed out
#   map.open_map()
extends "res://ui/bf2_screen.gd"

signal closed

const ZOOMS := [300.0, 700.0, 1600.0]  # meters across, rendered from overhead; then the planet
const RENDER_SIZE := 1024
# The render layer (its bit) of what's drawn over the world for the player
# (names, holograms, markers): the overhead picture leaves it out.
const WORLD_UI_LAYER := 2
const SHOP := Color(0.35, 0.92, 0.55)
const QUEST := Color(1.0, 0.8, 0.35)
const CRAFT := Color(0.75, 0.5, 1.0)
const TERMINAL := Color(0.55, 0.85, 1.0)
const BATTLE := Color(1.0, 0.45, 0.4)
const KINDS := [["battle", "Battle Terminals"], ["quest", "Quests"], ["shop", "Shops"], ["craft", "Crafting"],
		["service", "Services"], ["terminal", "Terminals"]]

var title := "Map"
var places: Array = []
var player_position := Vector3.ZERO
var player_yaw := 0.0
var waypoint := {}
var planet := {}
var towns: Array = []

var _zoom := 0
var _list_scroll := 0      # the list's first line shown (it scrolls with the wheel)
var _list_rect := Rect2()  # where the list is, this frame
var _mouse := Vector2.ZERO
var _picture: Texture2D
var _picture_center := Vector3.ZERO
var _picture_extent := 0.0
var _rendering: SubViewport
var _render_center := Vector3.ZERO
var _render_extent := 0.0
var _render_frames := 0


func open_map() -> void:
	visible = true
	_list_scroll = 0
	_request_picture()
	queue_redraw()


func close_map() -> void:
	visible = false
	_free_render()


func _process(_delta: float) -> void:
	if _rendering == null:
		return
	_render_frames += 1
	if _render_frames < 3:
		return
	var image := _rendering.get_texture().get_image()
	if image != null:
		_picture = ImageTexture.create_from_image(image)
		_picture_center = _render_center
		_picture_extent = _render_extent
	_free_render()
	queue_redraw()


# The town from overhead, `ZOOMS[_zoom]` across around you: an orthographic
# camera high over the loaded world, north (+z) up, in light without fog.
func _request_picture() -> void:
	if _zoom >= ZOOMS.size() or not is_inside_tree():
		return
	var extent: float = ZOOMS[_zoom]
	if _picture != null and is_equal_approx(_picture_extent, extent) \
			and Vector2(player_position.x - _picture_center.x, player_position.z - _picture_center.z).length() < extent * 0.15:
		return
	_free_render()
	var viewport := SubViewport.new()
	viewport.size = Vector2i(RENDER_SIZE, RENDER_SIZE)
	viewport.world_3d = get_viewport().world_3d
	viewport.render_target_update_mode = SubViewport.UPDATE_ONCE
	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = extent
	camera.near = 1.0
	camera.far = 4000.0
	camera.cull_mask = 0xFFFFF & ~WORLD_UI_LAYER
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.08, 0.08, 0.09)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.5, 0.5, 0.5)
	camera.environment = environment
	viewport.add_child(camera)
	add_child(viewport)
	camera.transform = Transform3D(Basis.looking_at(Vector3.DOWN, Vector3.BACK), player_position + Vector3.UP * 1500.0)
	camera.current = true
	_rendering = viewport
	_render_center = player_position
	_render_extent = extent
	_render_frames = 0


func _free_render() -> void:
	if _rendering != null and is_instance_valid(_rendering):
		_rendering.queue_free()
	_rendering = null


# What's shown: its middle (game x, z) and how many meters across.
func _view() -> Dictionary:
	if _zoom >= ZOOMS.size() and not planet.is_empty():
		return {"center": Vector2((float(planet["left_x"]) + float(planet["right_x"])) * 0.5,
				(float(planet["top_z"]) + float(planet["bottom_z"])) * 0.5),
				"extent": absf(float(planet["right_x"]) - float(planet["left_x"]))}
	if _picture != null:
		return {"center": Vector2(_picture_center.x, _picture_center.z), "extent": _picture_extent}
	return {"center": Vector2(player_position.x, player_position.z), "extent": float(ZOOMS[mini(_zoom, ZOOMS.size() - 1)])}


# A game position on the map: north (+z) up, east (Galaxies' +x, the game's
# -x) right.
func _to_map(at: Vector3, view: Dictionary, rect: Rect2) -> Vector2:
	var center: Vector2 = view["center"]
	var per_meter := rect.size.x / float(view["extent"])
	return rect.get_center() + Vector2(-(at.x - center.x), -(at.z - center.y)) * per_meter


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.55))
	var box := centered(820, 560)
	frame(box)
	text("gamefont_medium", title, box.position + Vector2(18, 12) * u)
	button(Rect2(box.end.x - 110 * u, box.position.y + 10 * u, 92 * u, 28 * u), "Close", "close", false, true)

	var map_rect := Rect2(box.position + Vector2(18, 48) * u, Vector2(490, 490) * u)
	draw_rect(map_rect, Color(0.05, 0.06, 0.08))
	var view := _view()
	var planet_view := _zoom >= ZOOMS.size() and not planet.is_empty()
	if planet_view:
		# The planet's ground preview: its columns run west to east the
		# game's way (+x), so it's flipped to put east on the right.
		draw_set_transform(Vector2(map_rect.end.x, map_rect.position.y), 0.0, Vector2(-1, 1))
		draw_texture_rect(planet["texture"], Rect2(Vector2.ZERO, map_rect.size), false)
		draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
	elif _picture != null and _rendering == null:
		draw_texture_rect(_picture, map_rect, false)
	else:
		text("gamefont_small", "Surveying...", map_rect.get_center() - Vector2(0, 8) * u, "Center", DIM)
	draw_rect(map_rect, Color(0.55, 0.62, 0.75, 0.6), false, maxf(1.0, u))

	# The towns' names, zoomed out (the planet, or 1.6 km across), under the markers.
	if planet_view or _zoom >= ZOOMS.size() - 1:
		for town: Dictionary in towns:
			var at := _to_map(town["position"], view, map_rect)
			if map_rect.grow(-12 * u).has_point(at):
				_town_label(at, str(town.get("name", "")), planet_view)

	# Its places, then your waypoint and you.
	var hovered := int(_hover.trim_prefix("place:")) if _hover.begins_with("place:") else -1
	for i in places.size():
		var place: Dictionary = places[i]
		var at := _to_map(place["position"], view, map_rect)
		if not map_rect.grow(-4 * u).has_point(at):
			continue
		_marker(at, place, i == hovered)
		region(Rect2(at - Vector2(8, 8) * u, Vector2(16, 16) * u), "place:%d" % i)
	if not waypoint.is_empty():
		var at := _to_map(waypoint["position"], view, map_rect)
		if map_rect.has_point(at):
			var icon: Texture2D = art.texture("hud_objective_icon")
			var s := 16.0 * u
			if icon != null:
				draw_texture_rect(icon, Rect2(at - Vector2(s, s) * 0.5, Vector2(s, s)), false, QUEST)
	var you := _to_map(player_position, view, map_rect)
	if map_rect.has_point(you):
		var icon: Texture2D = art.texture("player_icon")
		var s := 14.0 * u
		draw_set_transform(you, -player_yaw + PI, Vector2.ONE)  # up the map is north: you face -z at yaw 0
		if icon != null:
			draw_texture_rect(icon, Rect2(-Vector2(s, s) * 0.72, Vector2(s, s) * 1.44), false, Color(0, 0, 0, 0.8))
			draw_texture_rect(icon, Rect2(-Vector2(s, s) * 0.5, Vector2(s, s)), false, Color(1.0, 0.92, 0.45))
		draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)

	# The list beside it, nearest first in each kind: every kind, scrolled
	# with the wheel when it's longer than the map.
	var x := map_rect.end.x + 16 * u
	var width := box.end.x - 18 * u - x
	var row := 14.0 * u
	var lines: Array = []  # [kind header label] or place index
	for kind: Array in KINDS:
		var mine: Array = []
		for i in places.size():
			if str(places[i].get("kind", "")) == kind[0]:
				mine.append(i)
		if mine.is_empty():
			continue
		mine.sort_custom(func(a: int, b: int) -> bool:
			return _distance(places[a]) < _distance(places[b]))
		if not lines.is_empty():
			lines.append("")  # a gap between kinds
		lines.append([str(kind[1]), mine.size()])
		lines.append_array(mine)
	_list_rect = Rect2(x - 4 * u, map_rect.position.y, width + 8 * u, map_rect.size.y)
	# Each line's height; the furthest it scrolls shows the last line at the bottom.
	var heights: Array[float] = []
	for entry: Variant in lines:
		heights.append(row * 0.5 if entry is String else (row + 3 * u if entry is Array else row))
	var last_top := lines.size() - 1
	var below := 0.0
	while last_top >= 0 and below + heights[last_top] <= map_rect.size.y:
		below += heights[last_top]
		last_top -= 1
	_list_scroll = clampi(_list_scroll, 0, maxi(0, last_top + 1))
	var y := map_rect.position.y
	var shown_to := _list_scroll
	for n in range(_list_scroll, lines.size()):
		if y + heights[n] > map_rect.end.y + 0.5:
			break
		shown_to = n + 1
		var entry: Variant = lines[n]
		if entry is String:
			y += row * 0.5
			continue
		if entry is Array:
			text("gamefont_small", "%s (%d)" % [entry[0], entry[1]], Vector2(x, y), "Left", ACCENT)
			y += row + 3 * u
			continue
		var i: int = entry
		var place: Dictionary = places[i]
		var line := Rect2(x - 4 * u, y - 1 * u, width + 4 * u, row)
		if i == hovered:
			draw_rect(line, Color(1, 1, 1, 0.08))
		draw_circle(Vector2(x + 4 * u, y + row * 0.45), 3.5 * u, place.get("color", TEXT))
		var name := str(place.get("name", ""))
		var meters := _distance(place)
		var distance := "%d m" % roundi(meters) if meters < 1000 else "%.1f km" % (meters / 1000.0)
		text("gamefont_tiny", _fit(name, width - 60 * u), Vector2(x + 12 * u, y), "Left",
				TEXT if i == hovered else DIM)
		text("gamefont_tiny", distance, Vector2(x + width, y), "Right", DIM)
		region(line, "place:%d" % i)
		y += row
	if _list_scroll > 0 or shown_to < lines.size():
		text("gamefont_tiny", "%d-%d of %d  (wheel)" % [_list_scroll + 1, shown_to, lines.size()],
				Vector2(x + width, map_rect.end.y + 4 * u), "Right", DIM)

	var span := float(view["extent"])
	var scale := "%d m across" % roundi(span) if span < 1000 else "%.1f km across" % (span / 1000.0)
	text("gamefont_tiny", ("The planet" if planet_view else scale) + "    Mouse wheel: zoom    M / Esc: close",
			Vector2(map_rect.position.x, map_rect.end.y + 4 * u), "Left", DIM)

	# What the cursor is over, named beside it.
	if hovered >= 0 and hovered < places.size() and map_rect.has_point(_mouse):
		var place: Dictionary = places[hovered]
		var meters := _distance(place)
		var kind_label := ""
		for kind: Array in KINDS:
			if kind[0] == str(place.get("kind", "")):
				kind_label = str(kind[1])
		_tooltip(str(place.get("name", "")), "%s  -  %s" % [kind_label,
				"%d m" % roundi(meters) if meters < 1000 else "%.1f km" % (meters / 1000.0)], place.get("color", TEXT))


# A town's name, in white over a dark shadow; on the planet a dot marks it.
func _town_label(at: Vector2, name: String, with_dot: bool) -> void:
	var font := "gamefont_small"
	if with_dot:
		draw_circle(at, 3.5 * u, Color(0, 0, 0, 0.85))
		draw_circle(at, 2.5 * u, Color(1, 1, 1))
	var y := at.y - (20.0 if with_dot else 9.0) * u
	for offset: Vector2 in [Vector2(-1, 0), Vector2(1, 0), Vector2(0, -1), Vector2(0, 1)]:
		text(font, name, Vector2(at.x, y) + offset * maxf(1.0, u), "Center", Color(0, 0, 0, 0.85))
	text(font, name, Vector2(at.x, y), "Center", Color(1, 1, 1))


# A place's marker: a dot in its kind's color, outlined; a quest giver's is
# Battlefront's objective chevron. Hovered (in the list), ringed.
func _marker(at: Vector2, place: Dictionary, hovered: bool) -> void:
	var color: Color = place.get("color", TEXT)
	if hovered:
		draw_arc(at, 11 * u, 0, TAU, 24, Color(1, 1, 1, 0.9), maxf(1.5, 1.5 * u))
	var icon: Texture2D = art.texture("hud_objective_icon") if str(place.get("kind", "")) == "quest" else null
	if icon != null:
		var s := 14.0 * u
		draw_texture_rect(icon, Rect2(at - Vector2(s, s) * 0.5 + Vector2(1, 1), Vector2(s, s)), false, Color(0, 0, 0, 0.7))
		draw_texture_rect(icon, Rect2(at - Vector2(s, s) * 0.5, Vector2(s, s)), false, color)
		return
	draw_circle(at, 5.0 * u, Color(0, 0, 0, 0.75))
	draw_circle(at, 3.8 * u, color)


# A tooltip by the cursor, in Battlefront's frame: a name, and under it a line of detail.
func _tooltip(name: String, detail: String, color: Color) -> void:
	var width := maxf(art.text_width("gamefont_small", name, u), art.text_width("gamefont_tiny", detail, u)) + 24 * u
	var box := Rect2(_mouse + Vector2(16, 14) * u, Vector2(width, 40 * u))
	if box.end.x > size.x:
		box.position.x = _mouse.x - 16 * u - box.size.x
	if box.end.y > size.y:
		box.position.y = _mouse.y - 14 * u - box.size.y
	draw_rect(box, Color(0.04, 0.05, 0.07, 0.92))
	frame(box)
	text("gamefont_small", name, box.position + Vector2(12, 5) * u, "Left", color)
	text("gamefont_tiny", detail, box.position + Vector2(12, 23) * u, "Left", DIM)


func _distance(place: Dictionary) -> float:
	var at: Vector3 = place["position"]
	return Vector2(at.x - player_position.x, at.z - player_position.z).length()


func _fit(value: String, width: float) -> String:
	if art.text_width("gamefont_tiny", value, u) <= width:
		return value
	while value.length() > 3 and art.text_width("gamefont_tiny", value + "...", u) > width:
		value = value.substr(0, value.length() - 1)
	return value + "..."


func _on_click(id: String, _at: Vector2) -> void:
	if id == "close":
		closed.emit()


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		_mouse = (event as InputEventMouseMotion).position
	if event is InputEventMouseButton and event.pressed \
			and (event as InputEventMouseButton).button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN] \
			and _list_rect.has_point((event as InputEventMouseButton).position):
		# Over the list: it scrolls (three lines a notch).
		var notch := -3 if (event as InputEventMouseButton).button_index == MOUSE_BUTTON_WHEEL_UP else 3
		_list_scroll = maxi(0, _list_scroll + notch)
		queue_redraw()
		accept_event()
		return
	if event is InputEventMouseButton and event.pressed \
			and (event as InputEventMouseButton).button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		var last := ZOOMS.size() if not planet.is_empty() else ZOOMS.size() - 1
		var step := -1 if (event as InputEventMouseButton).button_index == MOUSE_BUTTON_WHEEL_UP else 1
		_zoom = clampi(_zoom + step, 0, last)
		_request_picture()
		queue_redraw()
		accept_event()
		return
	super._gui_input(event)
	if event is InputEventMouseMotion and _hover.begins_with("place:"):
		queue_redraw()  # (the tooltip follows the cursor)
