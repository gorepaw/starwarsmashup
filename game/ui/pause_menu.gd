# The pause menu (Esc) and its options, in Battlefront II's style: resume,
# options (OptionsView's pages: settings, keybinds), leave. Settings apply as
# they change and are saved on leaving the options.
extends "res://ui/bf2_screen.gd"

signal resumed
signal leave_requested

const Settings = preload("res://ui/settings.gd")
const OptionsView = preload("res://ui/options_view.gd")

var settings: Settings
var in_match := false        # "Leave Battle" rather than "Quit"
var _page := "main"         # "main", "settings" or "keybinds"
var _rebinding := ""         # action waiting for a key
var _scroll := 0             # the keybinds' first row shown


func is_rebinding() -> bool:
	return _rebinding != ""


func open() -> void:
	_page = "main"
	_rebinding = ""
	visible = true
	grab_focus()
	queue_redraw()


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.45))
	if _page == "main":
		_paint_main()
	else:
		_paint_options()


func _paint_main() -> void:
	var box := centered(300, 250)
	frame(box)
	text("gamefont_large", "Paused", Vector2(box.get_center().x, box.position.y + 18 * u), "Center", ACCENT)
	var row := Rect2(box.position + Vector2(40, 70) * u, Vector2(220, 36) * u)
	button(row, art.localize("common.resume"), "resume")
	button(Rect2(row.position + Vector2(0, 50) * u, row.size), "Options", "options")
	button(Rect2(row.position + Vector2(0, 100) * u, row.size), "Leave Battle" if in_match else art.localize("common.quit"),
			"leave", false, true)


func _paint_options() -> void:
	var box := centered(780, 580)
	frame(box)
	text("gamefont_large", "Options", Vector2(box.get_center().x, box.position.y + 14 * u), "Center", ACCENT)
	OptionsView.paint_tabs(self, box, _page)
	if _page == "keybinds":
		_scroll = OptionsView.paint_keybinds(self, box, settings, _rebinding, _scroll)
	else:
		OptionsView.paint_settings(self, box, settings)
	button(Rect2(Vector2(box.get_center().x - 80 * u, box.end.y - 50 * u), Vector2(160, 34) * u), art.localize("common.back"),
			"back")


func _on_click(id: String, at: Vector2) -> void:
	match id:
		"resume":
			visible = false
			resumed.emit()
		"options":
			_page = "settings"
			_scroll = 0
		"leave":
			leave_requested.emit()
		"back":
			_rebinding = ""
			settings.save_file()
			_page = "main"
		_:
			if id.begins_with("page:"):
				_page = id.substr(5)
				_rebinding = ""
			elif OptionsView.click(self, id, at, settings):
				pass
			elif id.begins_with("bind:"):
				_rebinding = id.substr(5)
				grab_focus()


func _on_drag(id: String, at: Vector2) -> void:
	OptionsView.drag(self, id, at, settings)


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
	if _page == "keybinds" and event is InputEventMouseButton and event.pressed:
		var wheel := (event as InputEventMouseButton).button_index
		if wheel == MOUSE_BUTTON_WHEEL_UP or wheel == MOUSE_BUTTON_WHEEL_DOWN:
			_scroll += -1 if wheel == MOUSE_BUTTON_WHEEL_UP else 1
			queue_redraw()
			accept_event()
			return
	super._gui_input(event)


func _unhandled_key_input(event: InputEvent) -> void:
	if visible and _rebinding != "":
		_gui_input(event)
