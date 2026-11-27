# Battlefront II-style Open-World Vendor screen for Mos Eisley:
# - junk_dealer: Sell tab with one-click "Sell All Junk" and per-item selling.
# - gear_vendor: Buy tab (neutral weapons, armor, shields, clothing, supplies
#   for Credits, with equipped stat tooltips) and Sell tab.
extends "res://ui/bf2_screen.gd"

const ItemTooltip = preload("res://ui/item_tooltip.gd")

signal item_bought(def_id: String, use_fp: bool)
signal item_sold(item_id: int)
signal all_junk_sold
signal closed

const BUY_CATEGORIES := [
	["all", "All"],
	["weapon", "Weapons"],
	["armor", "Armor"],
	["deflector_shield", "Shields"],
	["ammo", "Ammo"],
	["clothing", "Clothing"],
	["supply", "Supplies"],
	["vehicle", "Vehicles"],
]

var title := "Vendor"
var vendor_kind := "gear_vendor"  # "junk_dealer" or "gear_vendor"
var credits := 0
var catalog: Array = []
var inventory: Array = []
var active_loadout: Dictionary = {}
var store: Object = null
var feedback := ""

var _mode := "buy"  # "buy" or "sell"
var _category := "all"
var _scroll := 0
var _rows_visible := 8
var _hover_def: Dictionary = {}
var _hover_inst: Dictionary = {}
var _mouse_pos := Vector2.ZERO
var _filtered: Array = []
var _def_by_id: Dictionary = {}


# A gear vendor sells most things; a garage, vehicles.
func _sells_gear() -> bool:
	return vendor_kind == "gear_vendor" or vendor_kind == "garage"


func open_vendor(vname: String, vkind: String) -> void:
	title = vname
	vendor_kind = vkind
	_mode = "sell" if vkind == "junk_dealer" else "buy"
	_category = "vehicle" if vkind == "garage" else "all"
	_scroll = 0
	feedback = ""
	visible = true
	queue_redraw()


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		_mouse_pos = event.position
		queue_redraw()
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_scroll = maxi(0, _scroll - 1)
			queue_redraw()
			accept_event()
			return
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_scroll = mini(maxi(0, _filtered.size() - _rows_visible), _scroll + 1)
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


func _junk_summary() -> Dictionary:
	var count := 0
	var total := 0
	for inst: Dictionary in inventory:
		var def: Dictionary = _def_by_id.get(str(inst.get("def_id", "")), {})
		if str(def.get("kind", "")) == "junk":
			count += 1
			total += maxi(1, int(def.get("sell_value", 0)))
	return {"count": count, "total": total}


func _paint() -> void:
	_ensure_lookup()
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.55))
	var box := centered(760, 500, Vector2(0, 10))
	text("gamefont_large", title, Vector2(size.x * 0.5, box.position.y - 42 * u), "Center", ACCENT)
	frame(box)

	var x := box.position.x + 18 * u
	var y := box.position.y + 14 * u
	var subtitle := "Junk Dealer — Buys Salvage & Hides"
	if vendor_kind == "garage":
		subtitle = "Garage — Vehicles for Credits"
	elif vendor_kind != "junk_dealer":
		subtitle = "Mos Eisley Outfitter — Credits"
	text("gamefont_medium", subtitle, Vector2(x, y), "Left", TEXT)
	text("gamefont_medium", "%d Credits" % credits, Vector2(box.end.x - 18 * u, y), "Right", ACCENT)

	y += 32 * u
	if _sells_gear():
		button(Rect2(x, y, 108 * u, 28 * u), "Buy", "mode:buy", _mode == "buy")
		button(Rect2(x + 116 * u, y, 108 * u, 28 * u), "Sell", "mode:sell", _mode == "sell")
	else:
		var junk := _junk_summary()
		button(Rect2(x, y, 220 * u, 28 * u),
				"Sell All Junk (%d | %d Cr)" % [int(junk["count"]), int(junk["total"])],
				"sell_all_junk", false, false, int(junk["count"]) > 0)
	button(Rect2(box.end.x - 110 * u, y, 92 * u, 28 * u), "Close", "close", false, true)

	y += 36 * u
	if _mode == "buy" and vendor_kind == "garage":
		_category = "vehicle"
	elif _mode == "buy" and _sells_gear():
		var cx := x
		for cat: Array in BUY_CATEGORIES:
			var bw := 102.0 * u
			button(Rect2(cx, y, bw, 26 * u), cat[1], "cat:" + cat[0], _category == cat[0])
			cx += bw + 6 * u
		y += 34 * u
	elif _sells_gear():
		var junk := _junk_summary()
		button(Rect2(x, y, 220 * u, 26 * u),
				"Sell All Junk (%d | %d Cr)" % [int(junk["count"]), int(junk["total"])],
				"sell_all_junk", false, false, int(junk["count"]) > 0)
		y += 34 * u

	_filtered.clear()
	_hover_def = {}
	_hover_inst = {}
	var list_w := box.size.x - 36 * u
	var row_h := 36.0 * u

	if _mode == "buy":
		var owned_counts := {}
		for inst: Dictionary in inventory:
			var did := str(inst.get("def_id", ""))
			owned_counts[did] = int(owned_counts.get(did, 0)) + 1
		for def: Dictionary in catalog:
			var kind := str(def.get("kind", ""))
			if kind == "junk" or kind == "quest":
				continue
			if int(def.get("cost_credits", 0)) <= 0:
				continue
			if _category != "all" and kind != _category:
				continue
			_filtered.append(def)
		_scroll = clampi(_scroll, 0, maxi(0, _filtered.size() - _rows_visible))
		for row_i in _rows_visible:
			var idx := _scroll + row_i
			if idx >= _filtered.size():
				break
			var def: Dictionary = _filtered[idx]
			var ry := y + row_i * (row_h + 4 * u)
			var r := Rect2(x, ry, list_w, row_h)
			var hovered := r.has_point(_mouse_pos)
			if hovered:
				_hover_def = def
			frame(r, "border_3_pieces", Color(0.75, 0.88, 1.0) if hovered else Color(0.85, 0.85, 0.9))
			var did := str(def.get("id", ""))
			var kind_s := str(def.get("kind", ""))
			var owned_n := int(owned_counts.get(did, 0))
			var name_str := str(def.get("name", did))
			if kind_s == "ammo":
				name_str += " (x%d)" % int(def.get("ammo_batch", 100))
			if owned_n > 0:
				name_str += "  [Owned x%d]" % owned_n
			text("gamefont_small", name_str, Vector2(x + 12 * u, ry + 8 * u), "Left", ACCENT if owned_n > 0 else TEXT)
			text("gamefont_tiny", "%s" % kind_s.replace("_", " ").capitalize(),
					Vector2(x + 390 * u, ry + 11 * u), "Left", DIM)
			var cost := int(def.get("cost_credits", 0))
			button(Rect2(box.end.x - 164 * u, ry + 4 * u, 136 * u, 28 * u),
					"Buy %d Cr" % cost, "buy_cr:" + did, false, false, credits >= cost)
	else:
		for inst: Dictionary in inventory:
			var def: Dictionary = _def_by_id.get(str(inst.get("def_id", "")), {})
			if int(def.get("sell_value", 0)) <= 0:
				continue
			_filtered.append(inst)
		_filtered.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
			var da: Dictionary = _def_by_id.get(str(a.get("def_id", "")), {})
			var db: Dictionary = _def_by_id.get(str(b.get("def_id", "")), {})
			var ja := 0 if str(da.get("kind", "")) == "junk" else 1
			var jb := 0 if str(db.get("kind", "")) == "junk" else 1
			if ja != jb:
				return ja < jb
			return str(da.get("name", "")) < str(db.get("name", ""))
		)
		_scroll = clampi(_scroll, 0, maxi(0, _filtered.size() - _rows_visible))
		if _filtered.is_empty():
			text("gamefont_small", "No sellable items in inventory.", Vector2(box.get_center().x, y + 80 * u), "Center", DIM)
		for row_i in _rows_visible:
			var idx := _scroll + row_i
			if idx >= _filtered.size():
				break
			var inst: Dictionary = _filtered[idx]
			var did := str(inst.get("def_id", ""))
			var def: Dictionary = _def_by_id.get(did, {})
			var ry := y + row_i * (row_h + 4 * u)
			var r := Rect2(x, ry, list_w, row_h)
			var hovered := r.has_point(_mouse_pos)
			if hovered:
				_hover_def = def
				_hover_inst = inst
			frame(r, "border_3_pieces", Color(0.75, 0.88, 1.0) if hovered else Color(0.85, 0.85, 0.9))
			var kind := str(def.get("kind", ""))
			text("gamefont_small", str(def.get("name", did)), Vector2(x + 12 * u, ry + 8 * u),
					"Left", ACCENT if kind == "junk" else TEXT)
			var sub := kind.replace("_", " ").capitalize()
			if kind != "junk" and kind != "supply":
				sub += " (Q %d%%)" % roundi(float(inst.get("quality", 0.5)) * 100.0)
			text("gamefont_tiny", sub, Vector2(x + 390 * u, ry + 11 * u), "Left", DIM)
			var sell_v := int(def.get("sell_value", 0))
			var iid := int(inst.get("id", 0))
			button(Rect2(box.end.x - 164 * u, ry + 4 * u, 136 * u, 28 * u),
					"Sell %d Cr" % sell_v, "sell:%d" % iid)

	if _filtered.size() > _rows_visible:
		text("gamefont_tiny", "Scroll (%d-%d of %d)" % [_scroll + 1, mini(_scroll + _rows_visible, _filtered.size()), _filtered.size()],
				Vector2(box.get_center().x, box.end.y - 24 * u), "Center", DIM)
	if feedback != "":
		text("gamefont_small", feedback, Vector2(box.get_center().x, box.end.y - 44 * u), "Center", ACCENT)

	if not _hover_def.is_empty():
		var eq := _find_equipped(_hover_def) if _mode == "buy" else {}
		var lines := ItemTooltip.build_lines(_hover_def, _hover_inst, store, eq.get("def", {}), eq.get("inst", {}))
		ItemTooltip.draw(self, _mouse_pos, str(_hover_def.get("name", "")), lines)


func _find_equipped(def: Dictionary) -> Dictionary:
	var slot := str(def.get("slot", ""))
	var kind := str(def.get("kind", ""))
	var target_id := 0
	if kind == "weapon":
		if slot == "primary": target_id = int(active_loadout.get("primary_id", 0))
		elif slot == "sidearm": target_id = int(active_loadout.get("sidearm_id", 0))
		elif slot == "secondary": target_id = int(active_loadout.get("secondary_id", 0))
	elif kind == "armor" or kind == "clothing" or kind == "deflector_shield":
		target_id = int((active_loadout.get("gear_ids", {}) as Dictionary).get(slot, 0))
	elif kind == "bodysuit":  # (worn only in the Cosmetic Override)
		target_id = int((active_loadout.get("appearance_ids", {}) as Dictionary).get(slot, 0))
	if target_id <= 0:
		return {}
	for inst: Dictionary in inventory:
		if int(inst.get("id", 0)) == target_id:
			var did := str(inst.get("def_id", ""))
			var cdef: Dictionary = _def_by_id.get(did, {})
			if not cdef.is_empty():
				return {"def": cdef, "inst": inst}
	return {}


func _on_click(id: String, _at: Vector2) -> void:
	if id.begins_with("mode:"):
		_mode = id.substr(5)
		_scroll = 0
	elif id.begins_with("cat:"):
		_category = id.substr(4)
		_scroll = 0
	elif id.begins_with("buy_cr:"):
		item_bought.emit(id.substr(7), false)
	elif id == "sell_all_junk":
		all_junk_sold.emit()
	elif id.begins_with("sell:"):
		item_sold.emit(int(id.substr(5)))
	elif id == "close":
		closed.emit()
