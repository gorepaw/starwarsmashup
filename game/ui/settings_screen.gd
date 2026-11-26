# Standalone Options screen for the Main Menu (Battlefront II style).
# Shares Settings (user://settings.cfg) with the in-match pause menu.
extends "res://ui/bf2_screen.gd"

signal closed

const Settings = preload("res://ui/settings.gd")
const OptionsView = preload("res://ui/options_view.gd")

var settings: Settings
var _rebinding := ""
var _page := "settings"   # or "keybinds" (OptionsView's pages)
var _scroll := 0


func open(shared_settings: Settings) -> void:
	settings = shared_settings
	_rebinding = ""
	_page = "settings"
	_scroll = 0
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

	OptionsView.paint_tabs(self, box, _page)
	if _page == "keybinds":
		_scroll = OptionsView.paint_keybinds(self, box, settings, _rebinding, _scroll)
	else:
		OptionsView.paint_settings(self, box, settings)

	button(Rect2(Vector2(box.get_center().x - 80 * u, box.end.y - 50 * u), Vector2(160, 34) * u),
			art.localize("common.back"), "back")


func _on_click(id: String, at: Vector2) -> void:
	if settings == null:
		return
	match id:
		"back":
			_rebinding = ""
			settings.save_file()
			visible = false
			closed.emit()
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
	if settings != null:
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
	if event is InputEventKey and event.pressed and (event as InputEventKey).physical_keycode == KEY_ESCAPE:
		settings.save_file()
		visible = false
		closed.emit()
		accept_event()
		return
	super._gui_input(event)
