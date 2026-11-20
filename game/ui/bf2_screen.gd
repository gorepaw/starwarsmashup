# Base for screens in Battlefront II's style (pause, options, scores, spawn):
# a full-window Control that paints itself with Battlefront's art (Bf2Art)
# and turns clicks on the regions it painted into `clicked(id)`. Sizes are
# in units of a 600-pixel-high screen (`u`), as Battlefront's menus were.
extends Control

signal clicked(id: String)

const Bf2Art = preload("res://ui/bf2_art.gd")
const TEXT := Color(0.85, 0.9, 1.0)
const DIM := Color(0.6, 0.66, 0.78)
const ACCENT := Color(1.0, 0.8, 0.35)

var art: RefCounted
var u := 1.0              # pixels per unit (screen height / 600)
var _regions: Array = []  # [{rect, id}] painted this frame
var _hover := ""
var _pressed := ""        # held since a press (sliders drag)


func _init(shared_art: RefCounted = null) -> void:
	art = shared_art
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	focus_mode = Control.FOCUS_ALL


func _draw() -> void:
	_regions.clear()
	u = size.y / Bf2Art.FONT_REFERENCE
	if art != null:
		_paint()


# Subclasses paint here, registering clickable regions.
func _paint() -> void:
	pass


# Subclasses react to clicks (and drags on `_pressed`) here.
func _on_click(_id: String, _at: Vector2) -> void:
	pass


func _on_drag(_id: String, _at: Vector2) -> void:
	pass


func region(rect: Rect2, id: String) -> void:
	_regions.append({"rect": rect, "id": id})


func button(rect: Rect2, label: String, id: String, selected := false, header := false, enabled := true) -> void:
	art.draw_button(self, rect, label, selected or (enabled and _hover == id), header, "gamefont_small", enabled)
	if enabled:
		region(rect, id)


func text(font: String, value: String, at: Vector2, align := "Left", color := TEXT, scale := 1.0) -> void:
	art.draw_text(self, font, value, at, u * scale, align, color, true)


func frame(rect: Rect2, style := "border_3_pieces", tint := Color.WHITE) -> void:
	art.draw_frame(self, rect, style, tint)


# A centered rectangle `w` x `h` units, offset by `offset` units.
func centered(w: float, h: float, offset := Vector2.ZERO) -> Rect2:
	var s := Vector2(w, h) * u
	return Rect2((size - s) * 0.5 + offset * u, s)


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		var id := _region_at(event.position)
		if id != _hover:
			_hover = id
			queue_redraw()
		if _pressed != "":
			_on_drag(_pressed, event.position)
			queue_redraw()
	elif event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			_pressed = _region_at(event.position)
			if _pressed != "":
				_on_click(_pressed, event.position)
				clicked.emit(_pressed)
				queue_redraw()
		else:
			_pressed = ""
		accept_event()


func _region_at(at: Vector2) -> String:
	for i in range(_regions.size() - 1, -1, -1):
		if (_regions[i]["rect"] as Rect2).has_point(at):
			return _regions[i]["id"]
	return ""
