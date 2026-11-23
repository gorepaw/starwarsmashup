# Faction Quartermaster screen in Battlefront II's style: browse faction +
# neutral standard-issue weapons, armor, shields, and clothing, compare stats
# with currently equipped gear via ItemTooltip, and buy with Faction Points.
extends "res://ui/bf2_screen.gd"

const ItemTooltip = preload("res://ui/item_tooltip.gd")

signal item_bought(def_id: String, use_fp: bool)
signal closed

const CATEGORIES := [
	["all", "All"],
	["weapon", "Weapons"],
	["armor", "Armor"],
	["deflector_shield", "Shields"],
	["clothing", "Clothing"],
]

var title := "Quartermaster"
var faction := "neutral"
var faction_points := 0
var credits := 0
var catalog: Array = []          # from SwmCharacterStore.get_catalog()
var inventory: Array = []        # owned items
var active_loadout: Dictionary = {}
var store: Object = null
var feedback := ""

var _category := "all"
var _scroll := 0
var _rows_visible := 9
var _hover_def: Dictionary = {}
var _mouse_pos := Vector2.ZERO
var _filtered: Array = []


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		_mouse_pos = event.position
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


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.55))
	var box := centered(740, 490, Vector2(0, 10))
	text("gamefont_large", title, Vector2(size.x * 0.5, box.position.y - 42 * u), "Center", ACCENT)
	frame(box)

	var x := box.position.x + 18 * u
	var y := box.position.y + 14 * u
	text("gamefont_medium", "Faction: %s" % faction.capitalize(), Vector2(x, y), "Left", TEXT)
	text("gamefont_medium", "%d FP   |   %d Credits" % [faction_points, credits], Vector2(box.end.x - 18 * u, y), "Right", ACCENT)

	y += 32 * u
	var cx := x
	for cat: Array in CATEGORIES:
		var bw := 112.0 * u
		button(Rect2(cx, y, bw, 28 * u), cat[1], "cat:" + cat[0], _category == cat[0])
		cx += bw + 8 * u
	button(Rect2(box.end.x - 110 * u, y, 92 * u, 28 * u), "Close", "close", false, true)

	y += 40 * u
	_filtered.clear()
	var owned_counts := {}
	for inst: Dictionary in inventory:
		var did := str(inst.get("def_id", ""))
		owned_counts[did] = int(owned_counts.get(did, 0)) + 1

	for def: Dictionary in catalog:
		var df := str(def.get("faction", "neutral"))
		if df != "neutral" and faction != "neutral" and df != faction:
			continue
		if _category != "all" and str(def.get("kind", "")) != _category:
			continue
		_filtered.append(def)

	_scroll = clampi(_scroll, 0, maxi(0, _filtered.size() - _rows_visible))
	_hover_def = {}

	var list_w := box.size.x - 36 * u
	var row_h := 36.0 * u
	for row_i in _rows_visible:
		var idx := _scroll + row_i
		if idx >= _filtered.size():
			break
		var def: Dictionary = _filtered[idx]
		var ry := y + row_i * (row_h + 4 * u)
		var row_rect := Rect2(x, ry, list_w, row_h)
		var hovered := row_rect.has_point(_mouse_pos)
		if hovered:
			_hover_def = def
		frame(row_rect, "border_3_pieces", Color(0.75, 0.88, 1.0) if hovered else Color(0.85, 0.85, 0.9))
		var did := str(def.get("id", ""))
		var owned_n := int(owned_counts.get(did, 0))
		var name_str := str(def.get("name", did))
		if owned_n > 0:
			name_str += "  [Owned x%d]" % owned_n
		text("gamefont_small", name_str, Vector2(x + 12 * u, ry + 8 * u), "Left", ACCENT if owned_n > 0 else TEXT)
		text("gamefont_tiny", "%s (%s)" % [str(def.get("kind", "")).capitalize(), str(def.get("slot", ""))],
				Vector2(x + 370 * u, ry + 11 * u), "Left", DIM)

		var cost_fp := int(def.get("cost_fp", 0))
		var can_fp := faction_points >= cost_fp
		button(Rect2(box.end.x - 164 * u, ry + 4 * u, 136 * u, 28 * u),
				"Buy %d FP" % cost_fp, "buy_fp:" + did, false, false, can_fp)

	if _filtered.size() > _rows_visible:
		text("gamefont_tiny", "Scroll mouse wheel (%d-%d of %d)" % [_scroll + 1, mini(_scroll + _rows_visible, _filtered.size()), _filtered.size()],
				Vector2(box.get_center().x, box.end.y - 26 * u), "Center", DIM)
	if feedback != "":
		text("gamefont_small", feedback, Vector2(box.get_center().x, box.end.y - 46 * u), "Center", ACCENT)

	if not _hover_def.is_empty():
		var eq := _find_equipped(_hover_def)
		var lines := ItemTooltip.build_lines(_hover_def, {}, store, eq.get("def", {}), eq.get("inst", {}))
		ItemTooltip.draw(self, _mouse_pos, str(_hover_def.get("name", "")), lines)


func _find_equipped(def: Dictionary) -> Dictionary:
	var slot := str(def.get("slot", ""))
	var kind := str(def.get("kind", ""))
	var target_id := 0
	if kind == "weapon":
		if slot == "primary": target_id = int(active_loadout.get("primary_id", 0))
		elif slot == "sidearm": target_id = int(active_loadout.get("sidearm_id", 0))
		elif slot == "secondary": target_id = int(active_loadout.get("secondary_id", 0))
	elif kind == "clothing":
		target_id = int((active_loadout.get("appearance_ids", {}) as Dictionary).get(slot, 0))
	else:
		target_id = int((active_loadout.get("gear_ids", {}) as Dictionary).get(slot, 0))
	if target_id <= 0:
		return {}
	for inst: Dictionary in inventory:
		if int(inst.get("id", 0)) == target_id:
			var did := str(inst.get("def_id", ""))
			for cd: Dictionary in catalog:
				if str(cd.get("id", "")) == did:
					return {"def": cd, "inst": inst}
	return {}


func _on_click(id: String, _at: Vector2) -> void:
	if id.begins_with("cat:"):
		_category = id.substr(4)
		_scroll = 0
	elif id.begins_with("buy_fp:"):
		item_bought.emit(id.substr(7), true)
	elif id == "close":
		closed.emit()
