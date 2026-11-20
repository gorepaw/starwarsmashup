# Battlefront II's UI art, fonts and strings (bf2tool export-hud), shared by
# the HUD and the screens built in its style (spawn, scores, pause, options;
# Galaxies' screens later). Draws onto any CanvasItem:
#
#   var art := Bf2Art.load_from("res://imported_assets/hud")   # null if not exported
#   art.draw_text(self, "gamefont_medium", "Rebels", Vector2(40, 30), 1.0, "Left", Color.WHITE)
#   art.draw_frame(self, Rect2(...))                  # a metal-bordered panel
#   art.draw_button(self, Rect2(...), "Spawn", hovered)
#
# Scale: Battlefront's menus were made for a 600-pixel-high screen
# (ASSUMPTION); `ui_scale(screen)` gives the factor for the current one.
extends RefCounted

const FONT_REFERENCE := 600.0

var dir := ""
var _textures := {}
var _fonts := {}
var _strings := {}
var _styles := {}


static func load_from(directory: String) -> RefCounted:
	var path := directory.path_join("hud.json")
	if not FileAccess.file_exists(path):
		return null
	var data: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path))
	var art: RefCounted = load("res://ui/bf2_art.gd").new()
	art.dir = directory
	art._strings = data.get("strings", {})
	for font_name: String in data.get("fonts", {}):
		art._fonts[font_name] = art._load_font(data["fonts"][font_name])
	art.set_meta("data", data)
	return art


static func ui_scale(screen: Vector2) -> float:
	return screen.y / FONT_REFERENCE


func texture(name: String) -> Texture2D:
	if name == "":
		return null
	var key := name.to_lower()
	if not _textures.has(key):
		var path := ProjectSettings.globalize_path(dir.path_join("textures").path_join(key + ".png"))
		var image := Image.load_from_file(path) if FileAccess.file_exists(path) else null
		_textures[key] = ImageTexture.create_from_image(image) if image != null else null
	return _textures[key]


# Battlefront's text for a key (e.g. "common.victoryin" -> "Victory In"); the
# key itself if there's none.
func localize(key: String) -> String:
	var hash := 2166136261
	for byte in key.to_lower().to_utf8_buffer():
		hash = ((hash ^ byte) * 16777619) & 0xffffffff
	return str(_strings.get("%08x" % hash, key))


func has_font(font_name: String) -> bool:
	return _fonts.has(font_name)


func line_height(font_name: String, scale := 1.0) -> float:
	return float(_fonts.get(font_name, {}).get("line", 16)) * scale


func text_width(font_name: String, text: String, scale := 1.0) -> float:
	var glyphs: Dictionary = _fonts.get(font_name, {}).get("glyphs", {})
	var width := 0.0
	for c in text:
		var glyph: Array = glyphs.get(c.unicode_at(0), [])
		width += (float(glyph[2]) if not glyph.is_empty() else 0.0) * scale
	return width


# Text in a Battlefront font, its line's top at `at.y`; aligned on `at.x`
# ("Left", "Center", "Right"), with a drop shadow if asked.
func draw_text(canvas: CanvasItem, font_name: String, text: String, at: Vector2, scale: float, align := "Left",
		color := Color.WHITE, shadow := true) -> void:
	var font: Dictionary = _fonts.get(font_name, {})
	if font.is_empty():
		return
	var width := text_width(font_name, text, scale)
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
			if page == null:
				continue
			var page_size := page.get_size()
			var region := Rect2(float(glyph[7]) * page_size.x, float(glyph[9]) * page_size.y,
					(float(glyph[8]) - float(glyph[7])) * page_size.x, (float(glyph[10]) - float(glyph[9])) * page_size.y)
			var rect := Rect2(Vector2(pen + float(glyph[3]) * scale, at.y + float(glyph[5]) * scale) + offset,
					region.size * scale)
			canvas.draw_texture_rect_region(page, rect, region, tone)
			pen += float(glyph[2]) * scale


# A panel: Battlefront's metal border around a dark fill (border_3_pieces,
# 9-sliced), or another bordered texture ("border_3a_pieces" is maroon,
# "border_popup" black).
func draw_frame(canvas: CanvasItem, rect: Rect2, style := "border_3_pieces", tint := Color.WHITE) -> void:
	var box := _style(style, 18)
	if box != null:
		box.modulate_color = tint
		canvas.draw_style_box(box, rect)
	else:
		canvas.draw_rect(rect, Color(0.08, 0.09, 0.14, 0.85))


# A pill button: Battlefront's chrome caps around a label; blue when
# highlighted (hovered or selected), red for headers.
func draw_button(canvas: CanvasItem, rect: Rect2, label: String, highlighted := false, header := false,
		font_name := "gamefont_small", enabled := true) -> void:
	var names: Array
	if header:
		names = ["headerbuttonleft", "headerbuttonmid", "headerbuttonright"]
	elif highlighted:
		names = ["bf2_buttons_topleft", "bf2_buttons_title_center", "bf2_buttons_topright"]
	else:
		names = ["bf2_buttons_midleft", "bf2_buttons_items_center", "bf2_buttons_midright"]
	draw_pill(canvas, rect, names, Color(1, 1, 1, 1.0 if enabled else 0.5))
	var scale := rect.size.y / 40.0
	var color := Color(0.05, 0.08, 0.15) if not header else Color(1, 0.92, 0.85)
	if not enabled:
		color.a = 0.5
	draw_text(canvas, font_name, label, Vector2(rect.get_center().x, rect.get_center().y - line_height(font_name, scale) * 0.5),
			scale, "Center", color, header)


# Three textures as a horizontal pill: left cap, stretched middle, right cap
# (caps as wide as the pill is tall, cut to half their width).
func draw_pill(canvas: CanvasItem, rect: Rect2, names: Array, tint := Color.WHITE) -> void:
	var left := texture(names[0])
	var middle := texture(names[1])
	var right := texture(names[2])
	if left == null or middle == null or right == null:
		canvas.draw_rect(rect, Color(0.6, 0.65, 0.7, tint.a))
		return
	var cap := minf(rect.size.y * 0.5, rect.size.x * 0.5)
	var half := Vector2(left.get_width() * 0.5, left.get_height())
	canvas.draw_texture_rect_region(left, Rect2(rect.position, Vector2(cap, rect.size.y)), Rect2(Vector2.ZERO, half), tint)
	canvas.draw_texture_rect_region(middle, Rect2(rect.position + Vector2(cap, 0), Vector2(rect.size.x - 2 * cap, rect.size.y)),
			Rect2(Vector2(middle.get_width() * 0.25, 0), Vector2(middle.get_width() * 0.5, middle.get_height())), tint)
	canvas.draw_texture_rect_region(right, Rect2(rect.end - Vector2(cap, rect.size.y), Vector2(cap, rect.size.y)),
			Rect2(Vector2(right.get_width() * 0.5, 0), half), tint)


func draw_icon(canvas: CanvasItem, name: String, rect: Rect2, tint := Color.WHITE) -> void:
	var t := texture(name)
	if t != null:
		canvas.draw_texture_rect(t, rect, false, tint)


func _style(name: String, margin: float) -> StyleBoxTexture:
	if not _styles.has(name):
		var t := texture(name)
		if t == null:
			_styles[name] = null
		else:
			var box := StyleBoxTexture.new()
			box.texture = t
			box.texture_margin_left = margin
			box.texture_margin_right = margin
			box.texture_margin_top = margin
			box.texture_margin_bottom = margin
			_styles[name] = box
	return _styles[name]


func _load_font(font: Dictionary) -> Dictionary:
	var pages: Array[Texture2D] = []
	for page: String in font["pages"]:
		pages.append(texture(page))
	var glyphs := {}
	for glyph: Array in font["glyphs"]:
		glyphs[int(glyph[0])] = glyph
	return {"line": font["line_height"], "pages": pages, "glyphs": glyphs}
