# The options, as the pause menu and the main menu's Settings screen both
# show them (in Battlefront II's style): two pages, Settings (mouse, view,
# display, audio) and Keybinds (every rebindable action, two to a row,
# scrolled with the wheel), with tabs between them. Draws on and handles
# clicks for a Bf2Screen `s` (its text, buttons and regions).
extends RefCounted

const Bf2Screen = preload("res://ui/bf2_screen.gd")
const Settings = preload("res://ui/settings.gd")
const ROW := 34.0   # units, a keybind row


static func paint_tabs(s: Control, box: Rect2, page: String) -> void:
	var u: float = s.u
	var w := 160.0 * u
	var x0 := box.get_center().x - w - 5 * u
	var y := box.position.y + 50 * u
	s.button(Rect2(x0, y, w, 28 * u), "Settings", "page:settings", page == "settings")
	s.button(Rect2(x0 + w + 10 * u, y, w, 28 * u), "Keybinds", "page:keybinds", page == "keybinds")


static func paint_settings(s: Control, box: Rect2, settings: Settings) -> void:
	var u: float = s.u
	var x := box.position.x + 50 * u
	var y := box.position.y + 100 * u
	s.text("gamefont_medium", "Mouse", Vector2(x, y), "Left", Bf2Screen.ACCENT)
	y += 30 * u
	_slider(s, Vector2(x, y), "Sensitivity", "sensitivity", (settings.mouse_sensitivity - 0.2) / 2.8,
			"%.1f" % settings.mouse_sensitivity)
	y += 36 * u
	_toggle(s, Vector2(x, y), "Invert Mouse", "invert", settings.invert_mouse)
	y += 46 * u
	s.text("gamefont_medium", "View", Vector2(x, y), "Left", Bf2Screen.ACCENT)
	y += 30 * u
	_slider(s, Vector2(x, y), "Field of View", "fov", (settings.field_of_view - 50.0) / 50.0,
			"%d" % roundi(settings.field_of_view))
	y += 36 * u
	_toggle(s, Vector2(x, y), "First Person", "first_person", settings.first_person)
	# The right column: display and audio.
	x = box.position.x + 410 * u
	y = box.position.y + 100 * u
	s.text("gamefont_medium", "Display", Vector2(x, y), "Left", Bf2Screen.ACCENT)
	y += 30 * u
	_toggle(s, Vector2(x, y), "Fullscreen", "fullscreen", settings.fullscreen)
	y += 36 * u
	_toggle(s, Vector2(x, y), "Vertical Sync", "vsync", settings.vsync)
	y += 46 * u
	s.text("gamefont_medium", "Audio", Vector2(x, y), "Left", Bf2Screen.ACCENT)
	y += 30 * u
	for row: Array in [["Master", "master_volume", settings.master_volume], ["Effects", "sfx_volume", settings.sfx_volume],
			["Music", "music_volume", settings.music_volume], ["Voice", "voice_volume", settings.voice_volume]]:
		_slider(s, Vector2(x, y), row[0], row[1], row[2], "%d%%" % roundi(float(row[2]) * 100.0))
		y += 34 * u


# The keybinds from row `scroll`; returns the scroll, kept in range.
static func paint_keybinds(s: Control, box: Rect2, settings: Settings, rebinding: String, scroll: int) -> int:
	var u: float = s.u
	var top := box.position.y + 96 * u
	var bottom := box.end.y - 66 * u
	var rows := maxi(1, int((bottom - top) / (ROW * u)))
	var total := (Settings.ACTIONS.size() + 1) / 2
	scroll = clampi(scroll, 0, maxi(0, total - rows))
	var col_w := (box.size.x - 100 * u) * 0.5
	for r in rows:
		for c in 2:
			var i := (scroll + r) * 2 + c
			if i >= Settings.ACTIONS.size():
				continue
			var action: Array = Settings.ACTIONS[i]
			var name: String = action[0]
			var x := box.position.x + 40 * u + c * (col_w + 20 * u)
			var y := top + r * ROW * u
			s.text("gamefont_small", action[1], Vector2(x, y + 6 * u), "Left", Bf2Screen.TEXT)
			var key_label := "Press a key..." if rebinding == name else Settings.binding_name(settings.bindings[name])
			s.button(Rect2(Vector2(x + col_w - 150 * u, y), Vector2(150, ROW - 5) * u), key_label, "bind:" + name,
					rebinding == name)
	if total > rows:
		# Where the list is: a bar down the right side.
		var track := Rect2(box.end.x - 22 * u, top, 6 * u, rows * ROW * u)
		s.draw_rect(track, Color(0.02, 0.03, 0.06, 0.9))
		var share := float(rows) / total
		var thumb := Rect2(track.position.x, track.position.y + track.size.y * float(scroll) / total, track.size.x,
				track.size.y * share)
		s.draw_rect(thumb, Color(0.55, 0.7, 0.95, 0.9))
		s.text("gamefont_tiny", "Scroll for more", Vector2(box.get_center().x, bottom + 2 * u), "Center", Bf2Screen.DIM)
	return scroll


static func _slider(s: Control, at: Vector2, label: String, id: String, fraction: float, value: String) -> void:
	var u: float = s.u
	s.text("gamefont_small", label, at + Vector2(0, 3) * u, "Left", Bf2Screen.TEXT)
	# A groove, filled with Battlefront's slider bar up to the value, and its
	# red-lit knob.
	var track := Rect2(at + Vector2(140, 6) * u, Vector2(160, 10) * u)
	var fill := clampf(fraction, 0, 1)
	s.draw_rect(track, Color(0.02, 0.03, 0.06, 0.9))
	var bar: Texture2D = s.art.texture("slider_bg")
	if bar != null:
		s.draw_texture_rect_region(bar, Rect2(track.position, Vector2(track.size.x * fill, track.size.y)),
				Rect2(0, 0, bar.get_width(), bar.get_height()), Color(0.75, 0.85, 1.0))
	s.draw_rect(track, Color(0.55, 0.6, 0.7), false, maxf(1.0, u))
	var knob := Vector2(track.position.x + fill * track.size.x, track.get_center().y)
	s.draw_rect(Rect2(knob - Vector2(7, 11) * u, Vector2(14, 22) * u), Color(0.05, 0.05, 0.08))
	s.art.draw_icon(s, "slider_fg", Rect2(knob - Vector2(6, 10) * u, Vector2(12, 20) * u))
	s.text("gamefont_small", value, at + Vector2(312, 3) * u, "Left", Bf2Screen.DIM)
	s.region(Rect2(track.position - Vector2(6, 8) * u, track.size + Vector2(12, 16) * u), "slider:" + id)


static func _toggle(s: Control, at: Vector2, label: String, id: String, on: bool) -> void:
	var u: float = s.u
	s.text("gamefont_small", label, at + Vector2(0, 3) * u, "Left", Bf2Screen.TEXT)
	var mark := Rect2(at + Vector2(140, -2) * u, Vector2(22, 22) * u)
	s.art.draw_icon(s, "bf2_radiobutton_on" if on else "bf2_radiobutton_off", mark)
	s.region(Rect2(at - Vector2(0, 4) * u, Vector2(170, 28) * u), "toggle:" + id)


# A click on a slider or toggle: applied. False if it was neither.
static func click(s: Control, id: String, at: Vector2, settings: Settings) -> bool:
	if id.begins_with("slider:"):
		drag(s, id, at, settings)
		return true
	if id.begins_with("toggle:"):
		match id.substr(7):
			"invert": settings.invert_mouse = not settings.invert_mouse
			"first_person": settings.first_person = not settings.first_person
			"fullscreen": settings.fullscreen = not settings.fullscreen
			"vsync": settings.vsync = not settings.vsync
		settings.apply()
		return true
	return false


static func drag(s: Control, id: String, at: Vector2, settings: Settings) -> void:
	if not id.begins_with("slider:"):
		return
	var u: float = s.u
	var area := Rect2()
	for r: Dictionary in s._regions:
		if r["id"] == id:
			area = r["rect"]
	if area.size.x <= 0:
		return
	var fraction := clampf((at.x - area.position.x - 6 * u) / (area.size.x - 12 * u), 0.0, 1.0)
	match id.substr(7):
		"sensitivity": settings.mouse_sensitivity = snappedf(0.2 + fraction * 2.8, 0.1)
		"fov": settings.field_of_view = roundf(50.0 + fraction * 50.0)
		"master_volume":
			settings.master_volume = snappedf(fraction, 0.05)
			settings.apply()
		"sfx_volume":
			settings.sfx_volume = snappedf(fraction, 0.05)
			settings.apply()
		"music_volume":
			settings.music_volume = snappedf(fraction, 0.05)
			settings.apply()
		"voice_volume":
			settings.voice_volume = snappedf(fraction, 0.05)
			settings.apply()
