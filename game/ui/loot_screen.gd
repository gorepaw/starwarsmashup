# Galaxies-style Corpse Loot window in Battlefront II's UI style:
# displays items left on a defeated creature or bandit corpse, supports
# hover tooltips, taking individual slots, or "Loot All".
extends "res://ui/bf2_screen.gd"

const ItemTooltip = preload("res://ui/item_tooltip.gd")

signal loot_taken(corpse_entity: int, slot: int)
signal corpse_harvested(corpse_entity: int)
signal closed

var corpse: Dictionary = {}  # {entity_id, name, position, items: [{slot, def_id, quality}]}
var catalog: Array = []
var store: Object = null
var feedback := ""

var _def_by_id: Dictionary = {}
var _mouse_pos := Vector2.ZERO
var _hover_entry: Dictionary = {}
var _scroll := 0
var _rows_visible := 6


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		_mouse_pos = event.position
		queue_redraw()
	elif event is InputEventMouseButton and event.pressed:
		var items: Array = corpse.get("items", [])
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_scroll = maxi(0, _scroll - 1)
			queue_redraw()
			accept_event()
			return
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_scroll = mini(maxi(0, items.size() - _rows_visible), _scroll + 1)
			queue_redraw()
			accept_event()
			return
	super._gui_input(event)


func _ensure_lookup() -> void:
	if _def_by_id.size() == catalog.size():
		return
	_def_by_id.clear()
	for def: Dictionary in catalog:
		_def_by_id[str(def.get("id", ""))] = def


func _paint() -> void:
	_ensure_lookup()
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.45))
	var box := centered(520, 360, Vector2(0, 12))
	var cname := str(corpse.get("name", "Corpse"))
	text("gamefont_large", "CORPSE LOOT", Vector2(size.x * 0.5, box.position.y - 40 * u), "Center", ACCENT)
	frame(box)

	var x := box.position.x + 18 * u
	var y := box.position.y + 14 * u
	text("gamefont_medium", cname, Vector2(x, y), "Left", TEXT)
	var items: Array = corpse.get("items", [])
	button(Rect2(box.end.x - 322 * u, y - 2 * u, 100 * u, 28 * u), "Harvest", "harvest", false, false, cid_ok(corpse))
	button(Rect2(box.end.x - 214 * u, y - 2 * u, 104 * u, 28 * u), "Loot All", "loot_all", false, false, not items.is_empty())
	button(Rect2(box.end.x - 102 * u, y - 2 * u, 84 * u, 28 * u), "Close", "close", false, true)

	y += 38 * u
	_scroll = clampi(_scroll, 0, maxi(0, items.size() - _rows_visible))
	_hover_entry = {}
	var row_w := box.size.x - 36 * u
	var row_h := 38.0 * u

	if items.is_empty():
		text("gamefont_small", "Nothing left to loot.", Vector2(box.get_center().x, y + 60 * u), "Center", DIM)
	else:
		for row_i in _rows_visible:
			var idx := _scroll + row_i
			if idx >= items.size():
				break
			var entry: Dictionary = items[idx]
			var did := str(entry.get("def_id", ""))
			var def: Dictionary = _def_by_id.get(did, {})
			var ry := y + row_i * (row_h + 5 * u)
			var r := Rect2(x, ry, row_w, row_h)
			var hovered := r.has_point(_mouse_pos)
			if hovered:
				_hover_entry = entry
			frame(r, "border_3_pieces", Color(0.75, 0.88, 1.0) if hovered else Color(0.85, 0.85, 0.9))
			var kind := str(def.get("kind", "junk"))
			var title := str(def.get("name", did))
			text("gamefont_small", title, Vector2(x + 12 * u, ry + 5 * u), "Left", ACCENT if kind != "junk" else TEXT)
			var sub := ""
			if kind == "junk":
				sub = "Junk  |  Sell: %d Cr" % int(def.get("sell_value", 0))
			elif kind == "supply":
				sub = "Supply  |  +%d HP" % roundi(float(def.get("heal_amount", 0.0)))
			else:
				sub = "%s  |  Quality %d%%" % [kind.replace("_", " ").capitalize(), roundi(float(entry.get("quality", 0.5)) * 100.0)]
			text("gamefont_tiny", sub, Vector2(x + 12 * u, ry + 21 * u), "Left", DIM)
			var slot := int(entry.get("slot", 0))
			button(Rect2(box.end.x - 108 * u, ry + 5 * u, 80 * u, 28 * u), "Take", "take:%d" % slot)

	if items.size() > _rows_visible:
		text("gamefont_tiny", "Scroll (%d-%d of %d)" % [_scroll + 1, mini(_scroll + _rows_visible, items.size()), items.size()],
				Vector2(box.end.x - 18 * u, box.end.y - 22 * u), "Right", DIM)
	if feedback != "":
		text("gamefont_tiny", feedback, Vector2(x, box.end.y - 22 * u), "Left", ACCENT)

	if not _hover_entry.is_empty():
		var hdid := str(_hover_entry.get("def_id", ""))
		var hdef: Dictionary = _def_by_id.get(hdid, {})
		var hinst := {"def_id": hdid, "quality": float(_hover_entry.get("quality", 0.5))}
		var lines := ItemTooltip.build_lines(hdef, hinst, store)
		ItemTooltip.draw(self, _mouse_pos, str(hdef.get("name", hdid)), lines)


func cid_ok(c: Dictionary) -> bool:
	return int(c.get("entity_id", 0)) > 0


func _on_click(id: String, _at: Vector2) -> void:
	var cid := int(corpse.get("entity_id", 0))
	if id == "close":
		closed.emit()
	elif id == "harvest":
		if cid > 0:
			corpse_harvested.emit(cid)
	elif id == "loot_all":
		if cid > 0:
			loot_taken.emit(cid, -1)
	elif id.begins_with("take:"):
		if cid > 0:
			loot_taken.emit(cid, int(id.substr(5)))
