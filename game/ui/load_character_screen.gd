# Load Character screen: lists saved characters from SwmCharacterStore
# (name, species, gender, location), lets the player enter the world, delete
# a character (2-click confirm), or jump to New Character.
extends "res://ui/bf2_screen.gd"

signal character_selected(record: Dictionary)
signal new_requested
signal closed

var _store: Object
var _characters: Array = []
var _selected := 0
var _confirm_delete := false


func open(store: Object) -> void:
	_store = store
	_confirm_delete = false
	refresh()
	visible = true
	grab_focus()
	queue_redraw()


func refresh() -> void:
	_characters = _store.list_characters() if _store != null else []
	if _characters.is_empty():
		_selected = -1
	else:
		_selected = clampi(_selected, 0, _characters.size() - 1)
	queue_redraw()


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0.02, 0.03, 0.06, 0.72))
	var box := centered(660, 440)
	frame(box)
	text("gamefont_large", "Load Character", Vector2(box.get_center().x, box.position.y + 18 * u), "Center", ACCENT)

	var list_rect := Rect2(box.position + Vector2(28, 66) * u, Vector2(box.size.x - 56 * u, 280 * u))
	draw_rect(list_rect, Color(0.03, 0.04, 0.08, 0.85))
	draw_rect(list_rect, Color(0.3, 0.36, 0.5, 0.8), false, maxf(1.0, u))

	if _characters.is_empty():
		text("gamefont_medium", "No saved characters yet.", list_rect.get_center() - Vector2(0, 12 * u), "Center", DIM)
	else:
		var row_h := 38.0 * u
		var max_rows := mini(_characters.size(), 7)
		for i in max_rows:
			var rec: Dictionary = _characters[i]
			var row := Rect2(list_rect.position + Vector2(8 * u, (6 + i * 38) * u),
					Vector2(list_rect.size.x - 16 * u, row_h - 4 * u))
			var sel := i == _selected
			if sel or _hover == "char:%d" % i:
				draw_rect(row, Color(0.2, 0.45, 0.85, 0.45 if sel else 0.22))
			region(row, "char:%d" % i)
			text("gamefont_medium", str(rec["name"]), row.position + Vector2(12, 7) * u, "Left",
					ACCENT if sel else TEXT)
			var info := "%s %s  -  %s" % [str(rec["species"]).capitalize(), str(rec["gender"]).capitalize(),
					str(rec["location"])]
			text("gamefont_small", info, Vector2(row.end.x - 12 * u, row.position.y + 10 * u), "Right", DIM)

	var by := box.end.y - 56 * u
	var has_sel := _selected >= 0 and _selected < _characters.size()
	button(Rect2(Vector2(box.position.x + 28 * u, by), Vector2(160, 34) * u), "Enter World", "enter", false, false, has_sel)
	button(Rect2(Vector2(box.position.x + 200 * u, by), Vector2(140, 34) * u),
			"Confirm Delete" if _confirm_delete else "Delete", "delete", _confirm_delete, true, has_sel)
	button(Rect2(Vector2(box.position.x + 352 * u, by), Vector2(140, 34) * u), "New Character", "new")
	button(Rect2(Vector2(box.end.x - 148 * u, by), Vector2(120, 34) * u), art.localize("common.back"), "back")


func _on_click(id: String, _at: Vector2) -> void:
	if id.begins_with("char:"):
		_selected = int(id.substr(5))
		_confirm_delete = false
		return
	match id:
		"enter":
			if _selected >= 0 and _selected < _characters.size():
				visible = false
				character_selected.emit(_characters[_selected])
		"delete":
			if _selected >= 0 and _selected < _characters.size():
				if not _confirm_delete:
					_confirm_delete = true
				else:
					var rec: Dictionary = _characters[_selected]
					if _store != null and _store.delete_character(int(rec["id"])):
						_confirm_delete = false
						refresh()
		"new":
			visible = false
			new_requested.emit()
		"back":
			visible = false
			closed.emit()


func _gui_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and (event as InputEventKey).physical_keycode == KEY_ESCAPE:
		visible = false
		closed.emit()
		accept_event()
		return
	super._gui_input(event)
