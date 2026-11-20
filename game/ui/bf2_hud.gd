# Battlefront II's HUD, drawn from its own layout, textures, models and fonts
# (bf2tool export-hud). It interprets one layout (by default "1playerhud")
# the way Battlefront does: a tree of elements (Group, Bitmap, BarBitmap,
# Text, Model3D, segmented arcs, the message list, the minimap), each
# placed, scaled, colored and shown by its properties, and fed by events: the
# game sets `bindings` (e.g. "player1.healthFraction" = 0.8) and elements
# whose EventEnable is unset stay hidden. Only the elements and events the
# game supplies show, so unsupported parts of Battlefront's HUD (vehicles,
# heroes, flags) simply don't appear.
#
# Units, worked out from the layouts (ASSUMPTIONS where noted):
#  - A top-level element's Position is a fraction of the screen (x of its
#    width, y of its height, y down), so anchored elements keep to their edge
#    on any aspect ratio.
#  - Below that, positions and sizes are fractions of a 4:3 screen of the same
#    height (x of 4/3 of the height, y of the height), as Battlefront's
#    layouts were made for 4:3: the reticle's clip ring (0.088 x 0.111) is
#    round there.
#  - HUD models are in units of a fifth of the screen height (MESH_UNIT):
#    frames fit the bars they surround at that scale.
#  - A higher ZOrder is drawn further back.
#  - Fonts' pixel sizes are for a 600-pixel-high screen (ASSUMPTION).
#
#   var hud := Bf2Hud.new()
#   hud.load_layout("res://imported_assets/hud")   # false if not exported
#   hud.bindings["player1.health"] = true ...      # each frame, then queue_redraw()
extends Control

const MESH_UNIT := 0.2
const ASPECT := 4.0 / 3.0
const FONT_REFERENCE := 600.0
const PIXEL_REFERENCE := 480.0   # "Pixels" positions (ASSUMPTION: a 640x480 screen)
const MAP_RADIUS := 0.125        # the small map's radius, in screen heights before its Scale (ASSUMPTION)
const MAP_RANGE := 90.0          # meters from the player to the small map's edge

# Element keywords drawn (others, and properties, are skipped).
const MESSAGE_LIST := "d53e50d7"
const SEGMENT_BAR := "4b4bf31a"
const BAR_BITMAP := "BarBitmap"
# Properties without recovered names.
const ANGLE_START := "04507e24"
const ANGLE_END := "7197bcbd"
const SEGMENT_COUNT := "40fbdebd"
const TEXTURE_RECT := "1ffad2e0"   # u0, v0, u1, v1 of a bitmap's texture
const MAX_LINES := "0269babe"
const COLOR_TABLE := "0d34920d"     # hudtransforms: an event's value -> a color event
const TABLE_ENTRY := "ed01cbb7"     # value, r, g, b
const TABLE_INPUT := "cce718ed"
const TABLE_OUTPUT := "6903ed32"

var bindings := {}        # event -> bool, float, String, Color, Array
var minimap := {}         # {texture, left_x, top_z, right_x, bottom_z} (game space), or empty
var map_markers: Array = []  # [{position: Vector2 (game x, z), color, kind: "post" | "soldier", label}]
var map_player := {}      # {position: Vector2, yaw}
var messages: Array = []  # [{text, color, age}], newest last

var _dir := ""
var _layout: Array = []
var _tables: Array = []   # color tables from hudtransforms
var _textures := {}
var _meshes := {}
var _fonts := {}
var _strings := {}        # key hash (8 hex digits) -> English text
var _draws: Array = []    # collected each redraw: [{z, order, call}]


func load_layout(dir: String, layout := "1playerhud") -> bool:
	_dir = dir
	var path := dir.path_join("hud.json")
	if not FileAccess.file_exists(path):
		return false
	var data: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not data["layouts"].has(layout):
		return false
	_layout = data["layouts"][layout]
	for node: Dictionary in data["layouts"].get("hudtransforms", []):
		_collect_tables(node)
	_strings = data.get("strings", {})
	for font_name: String in data["fonts"]:
		_fonts[font_name] = _load_font(data["fonts"][font_name])
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	return true


# Events derived from others by the layouts' color tables (e.g. the health
# bar's color from the health fraction). Call after setting bindings.
func apply_tables() -> void:
	for table: Dictionary in _tables:
		if not bindings.has(table["input"]):
			continue
		var value := float(bindings[table["input"]])
		var entries: Array = table["entries"]
		var color: Color = entries[0][1]
		for i in entries.size():
			if value >= entries[i][0]:
				color = entries[i][1]
				if i + 1 < entries.size() and value < entries[i + 1][0]:
					var t: float = (value - entries[i][0]) / maxf(0.0001, entries[i + 1][0] - entries[i][0])
					color = color.lerp(entries[i + 1][1], t)
		bindings[table["output"]] = color


func _process(delta: float) -> void:
	for message: Dictionary in messages:
		message["age"] = float(message["age"]) + delta


func _draw() -> void:
	_draws.clear()
	var screen := size
	for node: Dictionary in _layout:
		_element(node, Transform2D(), Color.WHITE, 128.0, true, screen)
	_draws.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		return a["z"] > b["z"] if a["z"] != b["z"] else a["order"] < b["order"])
	for entry: Dictionary in _draws:
		(entry["call"] as Callable).call()
	draw_set_transform_matrix(Transform2D())


# --- the tree ---------------------------------------------------------------

func _element(node: Dictionary, parent: Transform2D, tint: Color, parent_z: float, top: bool, screen: Vector2) -> void:
	var kind: String = node["keyword"]
	if not kind in ["Group", "Bitmap", BAR_BITMAP, "Text", "Model3D", SEGMENT_BAR, MESSAGE_LIST, "Map"]:
		return
	if not _enabled(node):
		return
	var xf := parent * _local(node, top, screen)
	var color := tint * _color(node)
	var z := float(_value(node, "ZOrder", 0, parent_z))
	match kind:
		"Group":
			for child: Dictionary in node.get("children", []):
				_element(child, xf, color, z, false, screen)
		"Bitmap", BAR_BITMAP:
			_bitmap(node, xf, color, z, kind == BAR_BITMAP, screen)
		"Text":
			_text(node, xf, color, z, screen)
		"Model3D":
			_model(node, xf, color, z, screen)
		SEGMENT_BAR:
			_segment_bar(node, xf, color, z, screen)
		MESSAGE_LIST:
			_message_list(node, xf, color, z, screen)
		"Map":
			_map(node, xf, color, z, screen)


func _enabled(node: Dictionary) -> bool:
	var enable := _prop(node, "EventEnable")
	if not enable.is_empty() and not _truthy(str(enable["text"])):
		return false
	var disable := _prop(node, "EventDisable")
	return disable.is_empty() or not _truthy(str(disable["text"]))


func _truthy(event: String) -> bool:
	if event == "initialize":
		return true
	var value: Variant = bindings.get(event, false)
	if value is bool:
		return value
	if value is float or value is int:
		return true  # a number is shown once the game supplies it
	return value != null and str(value) != ""


func _local(node: Dictionary, top: bool, screen: Vector2) -> Transform2D:
	var position := Vector2.ZERO
	var at := _prop(node, "Position")
	var event := _prop(node, "EventPosition")
	if not at.is_empty():
		var v: Array = at["values"]
		if v.size() >= 4 and str(v[3]) == "Pixels":
			position = Vector2(v[0], v[1]) * screen.y / PIXEL_REFERENCE
		elif top:
			position = Vector2(float(v[0]) * screen.x, float(v[1]) * screen.y)
		else:
			position = Vector2(float(v[0]) * ASPECT * screen.y, float(v[1]) * screen.y)
	if not event.is_empty() and bindings.get(str(event["text"])) is Vector2:
		var p: Vector2 = bindings[str(event["text"])]
		position = Vector2(p.x * screen.x, p.y * screen.y)
	var scale := Vector2.ONE
	var s := _prop(node, "Scale")
	if not s.is_empty():
		scale = Vector2(s["values"][0], s["values"][1])
	var rotation := 0.0
	var r := _prop(node, "Rotation")
	if not r.is_empty() and r["values"].size() >= 3:
		rotation = deg_to_rad(float(r["values"][2]))
	return Transform2D(rotation, scale, 0.0, position)


func _color(node: Dictionary) -> Color:
	var color := Color.WHITE
	var c := _prop(node, "Color")
	if not c.is_empty():
		color = Color8(int(c["values"][0]), int(c["values"][1]), int(c["values"][2]))
	var event := _prop(node, "EventColor")
	if not event.is_empty() and bindings.get(str(event["text"])) is Color:
		color = bindings[str(event["text"])]
	var alpha := _prop(node, "Alpha")
	if not alpha.is_empty():
		color.a = float(alpha["values"][0])
	var event_alpha := _prop(node, "EventAlpha")
	if not event_alpha.is_empty() and bindings.has(str(event_alpha["text"])):
		color.a *= float(bindings[str(event_alpha["text"])])
	return color


# --- elements ---------------------------------------------------------------

func _bitmap(node: Dictionary, xf: Transform2D, color: Color, z: float, bar: bool, screen: Vector2) -> void:
	var name := str(_prop(node, "Bitmap").get("text", ""))
	var event := _prop(node, "EventBitmap")
	if not event.is_empty():
		name = str(bindings.get(str(event["text"]), name))
	var texture := _texture(name)
	if texture == null:
		return
	var rect := _rect(node, "BitmapRect", screen)
	var uv := Rect2(0, 0, 1, 1)
	var t := _prop(node, TEXTURE_RECT)
	if not t.is_empty():
		var v: Array = t["values"]
		uv = Rect2(v[0], v[1], float(v[2]) - float(v[0]), float(v[3]) - float(v[1]))
	if bar:
		var fraction := clampf(_number_event(node, "EventValue", _value(node, "Value", 0, 1.0)), 0.0, 1.0)
		rect.size.x *= fraction
		uv.size.x *= fraction
		if fraction <= 0.0:
			return
	var region := Rect2(uv.position * texture.get_size(), uv.size * texture.get_size())
	_add(z, func() -> void:
		draw_set_transform_matrix(xf)
		draw_texture_rect_region(texture, rect, region, color))


func _model(node: Dictionary, xf: Transform2D, color: Color, z: float, screen: Vector2) -> void:
	var name := str(_prop(node, "Mesh").get("text", ""))
	var event := _prop(node, "EventMesh")
	if not event.is_empty():
		name = str(bindings.get(str(event["text"]), ""))
	var model: Dictionary = _mesh(name)
	if model.is_empty():
		return
	var unit := MESH_UNIT * screen.y
	var placed := xf * Transform2D(0.0, Vector2(unit, unit), 0.0, Vector2.ZERO)
	_add(z, func() -> void:
		draw_set_transform_matrix(placed)
		draw_mesh(model["mesh"], model["texture"], Transform2D(), color))


func _text(node: Dictionary, xf: Transform2D, color: Color, z: float, screen: Vector2) -> void:
	var text := ""
	var event_text := _prop(node, "EventText")
	var event_number := _prop(node, "EventNumber")
	var fixed := _child(node, "Text")
	if not event_text.is_empty():
		text = str(bindings.get(str(event_text["text"]), ""))
	elif not event_number.is_empty():
		var value: Variant = bindings.get(str(event_number["text"]))
		if value == null:
			return
		var format := str(_prop(node, "NumberToTime").get("text", ""))
		if format != "" and (value is float or value is int):
			text = "%d:%02d" % [int(value) / 60, int(value) % 60]
		else:
			text = str(value)
	elif not fixed.is_empty():
		text = localize(str(fixed["text"]))
	if text == "":
		return
	var font: Dictionary = _fonts.get(str(_prop(node, "TextFont").get("text", "gamefont_small")), {})
	if font.is_empty():
		return
	var scale := screen.y / FONT_REFERENCE
	var text_scale := _prop(node, "TextScale")
	if not text_scale.is_empty():
		scale *= float(text_scale["values"][0])
	var align := str(_prop(node, "TextAlignment").get("text", "Left"))
	var shadow := str(_prop(node, "TextStyle").get("text", "")) == "Shadow"
	_add(z, func() -> void:
		draw_set_transform_matrix(xf)
		_draw_string(font, text, Vector2.ZERO, scale, align, color, shadow))


# Battlefront's arcs of segments around the reticle (clip, heat, refire):
# `count` segments evenly from AngleStart to AngleEnd (degrees clockwise from
# up) on an ellipse the size of Rect; the first value x count show.
func _segment_bar(node: Dictionary, xf: Transform2D, color: Color, z: float, screen: Vector2) -> void:
	var count := int(_value(node, SEGMENT_COUNT, 0, 10))
	var start := float(_value(node, ANGLE_START, 0, 0))
	var end := float(_value(node, ANGLE_END, 0, 360))
	var fraction := clampf(_number_event(node, "EventValue", _value(node, "Value", 0, 1.0)), 0.0, 1.0)
	var lit := ceili(fraction * count - 0.001)
	var ring := _rect(node, "Rect", screen).size * 0.5
	var segment := _child(node, "Segment")
	if segment.is_empty() or lit <= 0:
		return
	var texture := _texture(str(_prop(segment, "Bitmap").get("text", "white")))
	var piece := _rect(segment, "BitmapRect", screen).size
	var piece_color := color * _color(segment)
	if texture == null:
		return
	for i in lit:
		var angle := deg_to_rad(lerpf(start, end, (i + 0.5) / count))
		var at := Vector2(sin(angle) * ring.x, -cos(angle) * ring.y)
		var placed := xf * Transform2D(angle, at)
		_add(z, func() -> void:
			draw_set_transform_matrix(placed)
			draw_texture_rect(texture, Rect2(-piece * 0.5, piece), false, piece_color))


func _message_list(node: Dictionary, xf: Transform2D, color: Color, z: float, screen: Vector2) -> void:
	var format := _child(node, "Format")
	var font: Dictionary = _fonts.get(str(_prop(format, "TextFont").get("text", "gamefont_small")), {})
	if font.is_empty() or messages.is_empty():
		return
	var lines := int(_value(node, MAX_LINES, 0, 8))
	var shown := messages.slice(maxi(0, messages.size() - lines))
	var scale := screen.y / FONT_REFERENCE
	var step := float(font["line"]) * scale
	for i in shown.size():
		var message: Dictionary = shown[i]
		var fade := clampf(14.0 - float(message["age"]), 0.0, 1.0)  # out over the last second of 15
		var line_color: Color = message.get("color", color * _color(format))
		line_color.a *= fade
		var at := Vector2(0, i * step)
		_add(z, func() -> void:
			draw_set_transform_matrix(xf)
			_draw_string(font, str(message["text"]), at, scale, "Left", line_color, true))


# The small map: Battlefront's round radar (its map_mask backdrop), turned so
# the player looks up, with command posts and soldiers as dots and the
# player's view cone (hud_map_fov).
func _map(node: Dictionary, xf: Transform2D, color: Color, z: float, screen: Vector2) -> void:
	if minimap.is_empty() or map_player.is_empty():
		return
	var radius := MAP_RADIUS * screen.y
	var player: Vector2 = map_player["position"]
	var yaw := float(map_player["yaw"])
	var meters := radius / MAP_RANGE
	# Game (x, z) -> map pixels: relative to the player, turned so its facing
	# (-z at yaw 0) points up.
	var to_map := func(p: Vector2) -> Vector2:
		return (p - player).rotated(yaw) * meters
	var texture: Texture2D = minimap["texture"]
	var span := Vector2(float(minimap["right_x"]) - float(minimap["left_x"]),
			float(minimap["bottom_z"]) - float(minimap["top_z"]))
	var points := PackedVector2Array()
	var uvs := PackedVector2Array()
	for i in 48:
		var corner := Vector2.from_angle(TAU * i / 48.0) * radius
		points.append(corner)
		var world := player + (corner / meters).rotated(-yaw)
		uvs.append(Vector2((world.x - float(minimap["left_x"])) / span.x, (world.y - float(minimap["top_z"])) / span.y))
	var map_color := color
	_add(z, func() -> void:
		draw_set_transform_matrix(xf)
		draw_polygon(points, PackedColorArray([map_color]), uvs, texture))
	var backdrop := _child(node, "BackdropSmall")
	var mask := _texture(str(_prop(backdrop, "Mask").get("text", "map_mask")))
	if mask != null:
		# The whole mask is the disc. (BackdropSmall's texture rectangle is left
		# alone: ASSUMPTION, it's the part of the map image shown.)
		var region := Rect2(Vector2.ZERO, mask.get_size())
		var backdrop_color := color * _color(backdrop)
		var pad := radius * 1.12
		_add(float(_value(backdrop, "ZOrder", 0, 255)), func() -> void:
			draw_set_transform_matrix(xf)
			draw_texture_rect_region(mask, Rect2(-Vector2(pad, pad), Vector2(pad, pad) * 2), region, backdrop_color))
	var fov := _mesh("hud_map_fov")
	if not fov.is_empty():
		var cone := xf * Transform2D(PI, Vector2(radius * 0.35, radius * 0.35), 0.0, Vector2.ZERO)
		var cone_color := color * Color(1, 1, 1, 0.5)
		_add(z - 1, func() -> void:
			draw_set_transform_matrix(cone)
			draw_mesh(fov["mesh"], fov["texture"], Transform2D(), cone_color))
	var dot := _texture("whitecircle")
	for marker: Dictionary in map_markers:
		var at: Vector2 = to_map.call(marker["position"])
		if at.length() > radius * 0.95:
			if marker.get("kind") != "post":
				continue
			at = at.normalized() * radius * 0.95  # posts stay on the rim
		var big: bool = marker.get("kind") == "post"
		var dot_size := radius * (0.16 if big else 0.07)
		var marker_color: Color = marker["color"]
		_add(z - 2, func() -> void:
			draw_set_transform_matrix(xf)
			if dot != null:
				draw_texture_rect(dot, Rect2(at - Vector2(dot_size, dot_size) * 0.5, Vector2(dot_size, dot_size)),
						false, marker_color)
			else:
				draw_circle(at, dot_size * 0.5, marker_color))
	var north := _texture("hud_map_northpointer")
	if north != null:
		var direction := Vector2(0, -1).rotated(yaw)
		var north_at := xf * Transform2D(yaw, direction * radius * 1.05)
		var north_size := radius * 0.18
		_add(z - 3, func() -> void:
			draw_set_transform_matrix(north_at)
			draw_texture_rect(north, Rect2(-Vector2(north_size, north_size) * 0.5, Vector2(north_size, north_size)),
					false, Color.WHITE))


# --- drawing helpers ----------------------------------------------------------

func _add(z: float, call: Callable) -> void:
	_draws.append({"z": z, "order": _draws.size(), "call": call})


# A size property (BitmapRect, Rect): [w, h, horizontal align, vertical align,
# unit] as a rectangle around the element's origin.
func _rect(node: Dictionary, keyword: String, screen: Vector2) -> Rect2:
	var p := _prop(node, keyword)
	if p.is_empty():
		return Rect2(0, 0, 0, 0)
	var v: Array = p["values"]
	var s := Vector2(float(v[0]) * ASPECT * screen.y, float(v[1]) * screen.y)
	var origin := Vector2.ZERO
	var horizontal := str(v[2]) if v.size() > 2 else "Left"
	var vertical := str(v[3]) if v.size() > 3 else "Top"
	origin.x = -s.x * 0.5 if horizontal == "Center" else (-s.x if horizontal == "Right" else 0.0)
	origin.y = -s.y * 0.5 if vertical == "Center" else (-s.y if vertical == "Bottom" else 0.0)
	return Rect2(origin, s)


func _draw_string(font: Dictionary, text: String, at: Vector2, scale: float, align: String, color: Color,
		shadow: bool) -> void:
	var width := 0.0
	for c in text:
		var glyph: Array = font["glyphs"].get(c.unicode_at(0), [])
		width += (float(glyph[2]) if not glyph.is_empty() else 0.0) * scale
	var x := at.x - (width * 0.5 if align == "Center" else (width if align == "Right" else 0.0))
	for pass_index in (2 if shadow else 1):
		var offset := Vector2(1, 1) * maxf(1.0, scale) if shadow and pass_index == 0 else Vector2.ZERO
		var tone := Color(0, 0, 0, color.a * 0.8) if shadow and pass_index == 0 else color
		var pen := x
		for c in text:
			var glyph: Array = font["glyphs"].get(c.unicode_at(0), [])
			if glyph.is_empty():
				continue
			var page: Texture2D = font["pages"][int(glyph[1])]
			var page_size := page.get_size()
			var region := Rect2(float(glyph[7]) * page_size.x, float(glyph[9]) * page_size.y,
					(float(glyph[8]) - float(glyph[7])) * page_size.x, (float(glyph[10]) - float(glyph[9])) * page_size.y)
			var rect := Rect2(Vector2(pen + float(glyph[3]) * scale, at.y + float(glyph[5]) * scale) + offset,
					region.size * scale)
			draw_texture_rect_region(page, rect, region, tone)
			pen += float(glyph[2]) * scale


# Width of a string in a font, in pixels, at a scale.
func string_width(font_name: String, text: String, scale := 1.0) -> float:
	var font: Dictionary = _fonts.get(font_name, {})
	var width := 0.0
	for c in text:
		var glyph: Array = font.get("glyphs", {}).get(c.unicode_at(0), [])
		width += (float(glyph[2]) if not glyph.is_empty() else 0.0) * scale
	return width


# Draws text in one of Battlefront's fonts (for other screens built on it).
func draw_bf2_string(font_name: String, text: String, at: Vector2, scale: float, align := "Left",
		color := Color.WHITE, shadow := true) -> void:
	var font: Dictionary = _fonts.get(font_name, {})
	if not font.is_empty():
		_draw_string(font, text, at, scale, align, color, shadow)


func font_line_height(font_name: String) -> float:
	return float(_fonts.get(font_name, {}).get("line", 16))


# Battlefront's text for a key (e.g. "common.victoryin" -> "Victory In"); the
# key itself if there's none.
func localize(key: String) -> String:
	var hash := 2166136261
	for byte in key.to_lower().to_utf8_buffer():
		hash = ((hash ^ byte) * 16777619) & 0xffffffff
	return str(_strings.get("%08x" % hash, key))


# --- data -------------------------------------------------------------------

func _prop(node: Dictionary, keyword: String) -> Dictionary:
	for child: Dictionary in node.get("children", []):
		if child["keyword"] == keyword and not child.has("children"):
			return child
	return {}


func _child(node: Dictionary, keyword: String) -> Dictionary:
	for child: Dictionary in node.get("children", []):
		if child["keyword"] == keyword:
			return child
	return {}


func _value(node: Dictionary, keyword: String, index: int, fallback: Variant) -> Variant:
	var p := _prop(node, keyword)
	if p.is_empty() or p.get("values", []).size() <= index:
		return fallback
	var v: Variant = p["values"][index]
	return v if v is float or v is int else fallback


func _number_event(node: Dictionary, keyword: String, fallback: Variant) -> float:
	var p := _prop(node, keyword)
	if not p.is_empty():
		var value: Variant = bindings.get(str(p["text"]))
		if value is float or value is int:
			return float(value)
	return float(fallback)


func texture(name: String) -> Texture2D:
	return _texture(name)


func _texture(name: String) -> Texture2D:
	if name == "":
		return null
	var key := name.to_lower()
	if not _textures.has(key):
		var path := ProjectSettings.globalize_path(_dir.path_join("textures").path_join(key + ".png"))
		var image := Image.load_from_file(path) if FileAccess.file_exists(path) else null
		_textures[key] = ImageTexture.create_from_image(image) if image != null else null
	return _textures[key]


# A HUD model flattened to 2D: Battlefront's models are flat in x, y (y up);
# the export mirrored x for the game's 3D space, so it's mirrored back.
func _mesh(name: String) -> Dictionary:
	if name == "":
		return {}
	var key := name.to_lower()
	if _meshes.has(key):
		return _meshes[key]
	_meshes[key] = {}
	var path := _dir.path_join("models").path_join(key + ".gltf")
	if not FileAccess.file_exists(path):
		return {}
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		return {}
	var scene := document.generate_scene(state)
	var instance := scene.find_children("*", "MeshInstance3D", true, false)
	if instance.is_empty():
		scene.free()
		return {}
	var source: Mesh = (instance[0] as MeshInstance3D).mesh
	var mesh := ArrayMesh.new()
	var texture: Texture2D = null
	for surface in source.get_surface_count():
		var arrays := source.surface_get_arrays(surface)
		var flat := PackedVector2Array()
		for v: Vector3 in arrays[Mesh.ARRAY_VERTEX]:
			flat.append(Vector2(-v.x, -v.y))
		var out := []
		out.resize(Mesh.ARRAY_MAX)
		out[Mesh.ARRAY_VERTEX] = flat
		out[Mesh.ARRAY_TEX_UV] = arrays[Mesh.ARRAY_TEX_UV]
		out[Mesh.ARRAY_INDEX] = arrays[Mesh.ARRAY_INDEX]
		if arrays[Mesh.ARRAY_COLOR] != null:
			out[Mesh.ARRAY_COLOR] = arrays[Mesh.ARRAY_COLOR]  # the models' shading and fades
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, out)
		var material := source.surface_get_material(surface) as BaseMaterial3D
		if texture == null and material != null:
			texture = material.albedo_texture
	scene.free()
	_meshes[key] = {"mesh": mesh, "texture": texture}
	return _meshes[key]


func _load_font(font: Dictionary) -> Dictionary:
	var pages: Array[Texture2D] = []
	for page: String in font["pages"]:
		pages.append(_texture(page))
	var glyphs := {}
	for glyph: Array in font["glyphs"]:
		glyphs[int(glyph[0])] = glyph
	return {"line": font["line_height"], "pages": pages, "glyphs": glyphs}


func _collect_tables(node: Dictionary) -> void:
	if node["keyword"] == COLOR_TABLE:
		var entries: Array = []
		var input := ""
		var output := ""
		for child: Dictionary in node.get("children", []):
			var v: Array = child.get("values", [])
			if child["keyword"] == TABLE_ENTRY and v.size() >= 4:
				entries.append([float(v[0]), Color8(int(v[1]), int(v[2]), int(v[3]))])
			elif child["keyword"] == TABLE_INPUT:
				input = str(child["text"])
			elif child["keyword"] == TABLE_OUTPUT:
				output = str(child["text"])
		if not entries.is_empty() and input != "" and output != "":
			_tables.append({"input": input, "output": output, "entries": entries})
	for child: Dictionary in node.get("children", []):
		_collect_tables(child)
