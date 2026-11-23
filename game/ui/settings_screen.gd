# Standalone Options screen for the Main Menu (Battlefront II style).
# Shares Settings (user://settings.cfg) with the in-match pause menu.
extends "res://ui/bf2_screen.gd"

signal closed

const Settings = preload("res://ui/settings.gd")

var settings: Settings
var _rebinding := ""


func open(shared_settings: Settings) -> void:
	settings = shared_settings
	_rebinding = ""
	visible = true
	grab_focus()
	queue_redraw()


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0.02, 0.03, 0.06, 0.72))
	if settings == null:
		return
	var box := centered(780, 580)
	frame(box)
	text("gamefont_large", "Settings", Vector2(box.get_center().x, box.position.y + 14 * u), "Center", ACCENT)

	var x := box.position.x + 30 * u
	var y := box.position.y + 56 * u
	text("gamefont_medium", "Mouse", Vector2(x, y), "Left", ACCENT)
	y += 26 * u
	_slider(Vector2(x, y), "Sensitivity", "sensitivity", (settings.mouse_sensitivity - 0.2) / 2.8,
			"%.1f" % settings.mouse_sensitivity)
	y += 30 * u
	_toggle(Vector2(x, y), "Invert Mouse", "invert", settings.invert_mouse)
	y += 36 * u
	text("gamefont_medium", "View", Vector2(x, y), "Left", ACCENT)
	y += 26 * u
	_slider(Vector2(x, y), "Field of View", "fov", (settings.field_of_view - 50.0) / 50.0, "%d" % roundi(settings.field_of_view))
	y += 30 * u
	_toggle(Vector2(x, y), "First Person", "first_person", settings.first_person)
	y += 36 * u
	text("gamefont_medium", "Display", Vector2(x, y), "Left", ACCENT)
	y += 26 * u
	_toggle(Vector2(x, y), "Fullscreen", "fullscreen", settings.fullscreen)
	y += 30 * u
	_toggle(Vector2(x, y), "Vertical Sync", "vsync", settings.vsync)
	y += 36 * u
	text("gamefont_medium", "Audio", Vector2(x, y), "Left", ACCENT)
	y += 26 * u
	_slider(Vector2(x, y), "Master", "master_volume", settings.master_volume, "%d%%" % roundi(settings.master_volume * 100.0))
	y += 28 * u
	_slider(Vector2(x, y), "Effects", "sfx_volume", settings.sfx_volume, "%d%%" % roundi(settings.sfx_volume * 100.0))
	y += 28 * u
	_slider(Vector2(x, y), "Music", "music_volume", settings.music_volume, "%d%%" % roundi(settings.music_volume * 100.0))
	y += 28 * u
	_slider(Vector2(x, y), "Voice", "voice_volume", settings.voice_volume, "%d%%" % roundi(settings.voice_volume * 100.0))
	y += 34 * u
	text("gamefont_medium", "Bots", Vector2(x, y), "Left", ACCENT)
	y += 26 * u
	text("gamefont_small", "Difficulty", Vector2(x, y + 3 * u), "Left", TEXT)
	button(Rect2(Vector2(x + 140 * u, y - 2 * u), Vector2(160, 26) * u), settings.bot_difficulty.capitalize(),
			"difficulty", false)

	var cx := box.position.x + 400 * u
	var cy := box.position.y + 56 * u
	text("gamefont_medium", "Controls", Vector2(cx, cy), "Left", ACCENT)
	cy += 28 * u
	var row := minf(27.0, 425.0 / Settings.ACTIONS.size())
	for action: Array in Settings.ACTIONS:
		var name: String = action[0]
		text("gamefont_small", action[1], Vector2(cx, cy + (row - 22) * u), "Left", TEXT)
		var key_label := "Press a key..." if _rebinding == name else Settings.binding_name(settings.bindings[name])
		button(Rect2(Vector2(cx + 190 * u, cy), Vector2(150, row - 3) * u), key_label, "bind:" + name, _rebinding == name)
		cy += row * u

	button(Rect2(Vector2(box.get_center().x - 80 * u, box.end.y - 50 * u), Vector2(160, 34) * u),
			art.localize("common.back"), "back")


func _slider(at: Vector2, label: String, id: String, fraction: float, value: String) -> void:
	text("gamefont_small", label, at + Vector2(0, 3) * u, "Left", TEXT)
	var track := Rect2(at + Vector2(140, 6) * u, Vector2(160, 10) * u)
	var fill := clampf(fraction, 0, 1)
	draw_rect(track, Color(0.02, 0.03, 0.06, 0.9))
	var bar: Texture2D = art.texture("slider_bg")
	if bar != null:
		draw_texture_rect_region(bar, Rect2(track.position, Vector2(track.size.x * fill, track.size.y)),
				Rect2(0, 0, bar.get_width(), bar.get_height()), Color(0.75, 0.85, 1.0))
	draw_rect(track, Color(0.55, 0.6, 0.7), false, maxf(1.0, u))
	var knob := Vector2(track.position.x + fill * track.size.x, track.get_center().y)
	draw_rect(Rect2(knob - Vector2(7, 11) * u, Vector2(14, 22) * u), Color(0.05, 0.05, 0.08))
	art.draw_icon(self, "slider_fg", Rect2(knob - Vector2(6, 10) * u, Vector2(12, 20) * u))
	text("gamefont_small", value, at + Vector2(312, 3) * u, "Left", DIM)
	region(Rect2(track.position - Vector2(6, 8) * u, track.size + Vector2(12, 16) * u), "slider:" + id)


func _toggle(at: Vector2, label: String, id: String, on: bool) -> void:
	text("gamefont_small", label, at + Vector2(0, 3) * u, "Left", TEXT)
	var mark := Rect2(at + Vector2(140, -2) * u, Vector2(22, 22) * u)
	art.draw_icon(self, "bf2_radiobutton_on" if on else "bf2_radiobutton_off", mark)
	region(Rect2(at - Vector2(0, 4) * u, Vector2(170, 28) * u), "toggle:" + id)


func _on_click(id: String, at: Vector2) -> void:
	if settings == null:
		return
	match id:
		"difficulty":
			var i: int = Settings.DIFFICULTIES.find(settings.bot_difficulty)
			settings.bot_difficulty = Settings.DIFFICULTIES[(i + 1) % Settings.DIFFICULTIES.size()]
		"back":
			_rebinding = ""
			settings.save_file()
			visible = false
			closed.emit()
		_:
			if id.begins_with("slider:"):
				_on_drag(id, at)
			elif id.begins_with("toggle:"):
				match id.substr(7):
					"invert": settings.invert_mouse = not settings.invert_mouse
					"first_person": settings.first_person = not settings.first_person
					"fullscreen": settings.fullscreen = not settings.fullscreen
					"vsync": settings.vsync = not settings.vsync
				settings.apply()
			elif id.begins_with("bind:"):
				_rebinding = id.substr(5)
				grab_focus()


func _on_drag(id: String, at: Vector2) -> void:
	if settings == null or not id.begins_with("slider:"):
		return
	var area := Rect2()
	for r: Dictionary in _regions:
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


func _gui_input(event: InputEvent) -> void:
	if _rebinding != "":
		var binding: Variant = Settings.binding_from(event)
		if binding != null:
			if not (event is InputEventKey and (event as InputEventKey).physical_keycode == KEY_ESCAPE):
				settings.bindings[_rebinding] = binding
				settings.apply()
			_rebinding = ""
			accept_event()
			queue_redraw()
			return
	if event is InputEventKey and event.pressed and (event as InputEventKey).physical_keycode == KEY_ESCAPE:
		settings.save_file()
		visible = false
		closed.emit()
		accept_event()
		return
	super._gui_input(event)
