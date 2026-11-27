# The bank, in Battlefront II's frame: your credits and what you carry on the
# left, what the bank keeps on the right. Credits go in and out by the
# hundred, the thousand or all; an item moves with a click. What's banked is
# safe from death and waits for you at any bank.
#
#   screen.credits = ...; screen.bank_credits = ...; screen.inventory = [...]; screen.bank_items = [...]
#   screen.catalog = [...]  # item defs, for names
extends "res://ui/bf2_screen.gd"

signal service_requested(action: int, id: String, amount: int, item_id: int)
signal closed

const ItemTooltip = preload("res://ui/item_tooltip.gd")

const DEPOSIT_CREDITS := 1
const WITHDRAW_CREDITS := 2
const DEPOSIT_ITEM := 3
const WITHDRAW_ITEM := 4
const ROWS := 9

var title := "Bank"
var credits := 0
var bank_credits := 0
var inventory: Array = []
var bank_items: Array = []
var catalog: Array = []
var feedback := ""

var _scroll_left := 0
var _scroll_right := 0
var _defs := {}


func open_bank(bank_name: String) -> void:
	title = bank_name
	_scroll_left = 0
	_scroll_right = 0
	feedback = ""
	visible = true
	queue_redraw()


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed \
			and (event as InputEventMouseButton).button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		var step := -1 if (event as InputEventMouseButton).button_index == MOUSE_BUTTON_WHEEL_UP else 1
		var middle := size.x * 0.5
		if (event as InputEventMouseButton).position.x < middle:
			_scroll_left = clampi(_scroll_left + step, 0, maxi(0, _carried().size() - ROWS))
		else:
			_scroll_right = clampi(_scroll_right + step, 0, maxi(0, bank_items.size() - ROWS))
		queue_redraw()
		accept_event()
		return
	super._gui_input(event)


func _name_of(item: Dictionary) -> String:
	if _defs.size() != catalog.size():
		_defs.clear()
		for d: Dictionary in catalog:
			_defs[str(d.get("id", ""))] = d
	var custom := str(item.get("custom_name", ""))
	if custom != "":
		return custom
	return ItemTooltip.item_name(_defs.get(str(item.get("def_id", "")), {}), item)


# What you carry that can be banked: not a quest item.
func _carried() -> Array:
	var out: Array = []
	for item: Dictionary in inventory:
		var def: Dictionary = _defs.get(str(item.get("def_id", "")), {})
		if str(def.get("kind", "")) != "quest":
			out.append(item)
	return out


func _paint() -> void:
	_name_of({})  # (builds the lookup)
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.55))
	var box := centered(780, 500, Vector2(0, 10))
	text("gamefont_large", title, Vector2(size.x * 0.5, box.position.y - 42 * u), "Center", ACCENT)
	draw_rect(box, Color(0.04, 0.05, 0.07, 0.9))
	frame(box)
	button(Rect2(box.end.x - 110 * u, box.position.y + 10 * u, 92 * u, 28 * u), "Close", "close", false, true)
	var half := (box.size.x - 54 * u) * 0.5
	var left_x := box.position.x + 18 * u
	var right_x := left_x + half + 18 * u
	var y := box.position.y + 14 * u
	text("gamefont_medium", "You carry   %d Cr" % credits, Vector2(left_x, y), "Left", TEXT)
	text("gamefont_medium", "Bank   %d Cr" % bank_credits, Vector2(right_x, y), "Left", ACCENT)
	y += 34 * u
	# Credits: in on the left, out on the right.
	var step := 70.0 * u
	for i in 3:
		var amount: int = [100, 1000, 1000000000][i]
		var label: String = ["100", "1000", "All"][i]
		button(Rect2(left_x + i * (step + 6 * u), y, step, 26 * u), ">> " + label,
				"credits_in:%d" % amount, false, false, credits > 0)
		button(Rect2(right_x + i * (step + 6 * u), y, step, 26 * u), "<< " + label,
				"credits_out:%d" % amount, false, false, bank_credits > 0)
	y += 36 * u
	var row_h := 28.0 * u
	var carried := _carried()
	_scroll_left = clampi(_scroll_left, 0, maxi(0, carried.size() - ROWS))
	_scroll_right = clampi(_scroll_right, 0, maxi(0, bank_items.size() - ROWS))
	if carried.is_empty():
		text("gamefont_small", "Nothing to bank.", Vector2(left_x + half * 0.5, y + 40 * u), "Center", DIM)
	if bank_items.is_empty():
		text("gamefont_small", "The bank holds no items.", Vector2(right_x + half * 0.5, y + 40 * u), "Center", DIM)
	for i in ROWS:
		var ry := y + i * (row_h + 2 * u)
		if _scroll_left + i < carried.size():
			var item: Dictionary = carried[_scroll_left + i]
			var rect := Rect2(left_x, ry, half, row_h)
			draw_rect(rect, Color(0.18, 0.22, 0.3, 0.5) if _hover == "in:%d" % int(item["id"]) else Color(0.1, 0.13, 0.18, 0.4))
			text("gamefont_tiny", _name_of(item), Vector2(rect.position.x + 8 * u, ry + 7 * u), "Left",
					ItemTooltip.name_color(_defs.get(str(item.get("def_id", "")), {}), item))
			text("gamefont_tiny", "bank >>", Vector2(rect.end.x - 8 * u, ry + 7 * u), "Right", DIM)
			region(rect, "in:%d" % int(item["id"]))
		if _scroll_right + i < bank_items.size():
			var item: Dictionary = bank_items[_scroll_right + i]
			var rect := Rect2(right_x, ry, half, row_h)
			draw_rect(rect, Color(0.18, 0.22, 0.3, 0.5) if _hover == "out:%d" % int(item["id"]) else Color(0.1, 0.13, 0.18, 0.4))
			text("gamefont_tiny", _name_of(item), Vector2(rect.position.x + 8 * u, ry + 7 * u), "Left",
					ItemTooltip.name_color(_defs.get(str(item.get("def_id", "")), {}), item))
			text("gamefont_tiny", "<< take", Vector2(rect.end.x - 8 * u, ry + 7 * u), "Right", DIM)
			region(rect, "out:%d" % int(item["id"]))
	if feedback != "":
		text("gamefont_small", feedback, Vector2(box.get_center().x, box.end.y - 30 * u), "Center", ACCENT)


func _on_click(id: String, _at: Vector2) -> void:
	if id == "close":
		closed.emit()
	elif id.begins_with("credits_in:"):
		service_requested.emit(DEPOSIT_CREDITS, "", int(id.substr(11)), 0)
	elif id.begins_with("credits_out:"):
		service_requested.emit(WITHDRAW_CREDITS, "", int(id.substr(12)), 0)
	elif id.begins_with("in:"):
		service_requested.emit(DEPOSIT_ITEM, "", 0, int(id.substr(3)))
	elif id.begins_with("out:"):
		service_requested.emit(WITHDRAW_ITEM, "", 0, int(id.substr(4)))
