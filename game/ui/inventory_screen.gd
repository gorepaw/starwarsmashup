# MMO-style Inventory, 3D Equipment Paperdoll, and 3-Loadout editor in
# Battlefront II's style: drag-to-equip, sort, filter, destroy with confirm,
# species-blocked slot indicators, and full stat tooltips with comparisons.
extends "res://ui/bf2_screen.gd"

const CharacterStage = preload("res://rendering/character_stage.gd")
const ItemTooltip = preload("res://ui/item_tooltip.gd")

signal loadout_saved(slot_index: int, loadout: Dictionary)
signal loadout_activated(slot_index: int)
signal item_destroyed(item_id: int)
signal item_used(item_id: int)
signal closed

const GEAR_SLOTS := [
	["primary", "Primary"],
	["sidearm", "Sidearm"],
	["secondary", "Secondary"],
	["deflector_shield", "Deflector Shield"],
	["helmet", "Helmet"],
	["chest", "Chest"],
	["biceps", "Biceps"],
	["bracers", "Bracers"],
	["gloves", "Gloves"],
	["belt", "Belt"],
	["leggings", "Leggings"],
	["boots", "Boots"],
	["ammo_standard", "Std Ammo"],
	["ammo_heavy", "Heavy Ammo"],
	["mount", "Mount / Speeder"],
]

# The cosmetic override: the same worn slots, shown over the gear. Any armor
# or clothing goes in either; here it's only seen. And a Bodysuit, the one
# look-only item, shown in place of everything.
const WORN_SLOTS := ["helmet", "chest", "biceps", "bracers", "gloves", "belt", "leggings", "boots"]
const APPEARANCE_SLOTS := [
	["helmet", "Helmet"],
	["chest", "Chest"],
	["biceps", "Biceps"],
	["bracers", "Bracers"],
	["gloves", "Gloves"],
	["belt", "Belt"],
	["leggings", "Leggings"],
	["boots", "Boots"],
	["full_body", "Bodysuit"],
]

const CATEGORIES := [
	["all", "All"],
	["weapon", "Weapons"],
	["armor", "Armor"],
	["deflector_shield", "Shields"],
	["ammo", "Ammo"],
	["clothing", "Clothing"],
	["misc", "Junk/Med"],
]

var species := "human"
var gender := "male"
var appearance_json := "{}"
var credits := 0
var faction_points := 0
var experience := 0
var active_loadout_idx := 0
var catalog: Array = []
var inventory: Array = []
var loadouts: Array = []
var store: Object = null

var _edit_slot := 0
var _paperdoll_mode := "gear"  # "gear" or "appearance"
var _category := "all"
var _sort_by := "name"         # "name", "kind", "slot", "quality"
var _scroll := 0
var _rows_visible := 8
var _selected_item_id := 0
var _confirm_destroy_id := 0
var _dragging_inst: Dictionary = {}
var _drag_origin := Vector2.ZERO
var _drag_active := false
var _mouse_pos := Vector2.ZERO
var _hover_inst: Dictionary = {}
var _slot_rects: Array = []    # [{rect, slot, is_app}]
var _filtered: Array = []
var _def_by_id: Dictionary = {}
var _hidden_slots: PackedStringArray = []  # worn, but not shown on this species

var _viewport: SubViewport
var _stage: CharacterStage
var _stage_camera: Camera3D
var _stage_key := ""
var _stage_worn_key := ""
var _stage_rect := Rect2()
var _rotating := false
var _name_edit: LineEdit       # a loadout's new name, while renaming


func _ready() -> void:
	_name_edit = LineEdit.new()
	_name_edit.visible = false
	_name_edit.max_length = 20
	_name_edit.placeholder_text = "Loadout name, then Enter"
	_name_edit.text_submitted.connect(_on_rename_submitted)
	_name_edit.gui_input.connect(func(event: InputEvent) -> void:
		if event is InputEventKey and event.pressed and (event as InputEventKey).physical_keycode == KEY_ESCAPE:
			_name_edit.visible = false  # (Escape: not renamed after all)
			_name_edit.release_focus()
			queue_redraw())
	add_child(_name_edit)
	if not DisplayServer.get_name().to_lower().contains("headless"):
		_setup_viewport()


# Names the loadout being edited (blank: its default, "Loadout N").
func _on_rename_submitted(new_name: String) -> void:
	_name_edit.visible = false
	_name_edit.release_focus()
	var lo := _current_loadout().duplicate(true)
	var cleaned := new_name.strip_edges()
	lo["name"] = cleaned if cleaned != "" else "Loadout %d" % (_edit_slot + 1)
	for i in loadouts.size():  # shown at once; the server's copy follows
		if int((loadouts[i] as Dictionary).get("slot_index", -1)) == _edit_slot:
			loadouts[i] = lo
	loadout_saved.emit(_edit_slot, lo)
	queue_redraw()


# A loadout's name for its tab: "L1" until it's named, then as much of its
# name as fits.
func _tab_label(slot: int) -> String:
	var named := ""
	for lo: Dictionary in loadouts:
		if int(lo.get("slot_index", -1)) == slot:
			named = str(lo.get("name", ""))
	if named == "" or named == "Loadout %d" % (slot + 1):
		named = "L%d" % (slot + 1)
	var suffix := " *" if slot == active_loadout_idx else ""
	while named.length() > 1 and art.text_width("gamefont_small", named + suffix, u) > 70.0 * u:
		named = named.left(named.length() - 1)
	return named + suffix


func _setup_viewport() -> void:
	_viewport = SubViewport.new()
	_viewport.own_world_3d = true
	_viewport.transparent_bg = true
	_viewport.size = Vector2i(260, 320)
	_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	add_child(_viewport)

	var env := Environment.new()
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.7, 0.72, 0.78)
	var we := WorldEnvironment.new()
	we.environment = env
	_viewport.add_child(we)

	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-35, 30, 0)
	light.light_energy = 1.4
	_viewport.add_child(light)

	_stage_camera = Camera3D.new()
	_stage_camera.fov = 42.0
	_stage_camera.position = Vector3(0.0, 1.05, 2.75)
	_stage_camera.look_at(Vector3(0.0, 0.92, 0.0), Vector3.UP)
	_viewport.add_child(_stage_camera)

	_stage = CharacterStage.new()
	_viewport.add_child(_stage)


func open_screen() -> void:
	_edit_slot = clampi(active_loadout_idx, 0, 4)
	_confirm_destroy_id = 0
	_dragging_inst = {}
	_drag_active = false
	_rebuild_lookup()
	_sync_stage()
	if _viewport != null:
		_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	visible = true
	queue_redraw()


func close_screen() -> void:
	visible = false
	if _name_edit != null:
		_name_edit.visible = false
	_dragging_inst = {}
	_drag_active = false
	if _viewport != null:
		_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED


func refresh_data() -> void:
	_rebuild_lookup()
	if visible:
		_sync_stage()
		queue_redraw()


func _rebuild_lookup() -> void:
	_def_by_id.clear()
	for def: Dictionary in catalog:
		_def_by_id[str(def.get("id", ""))] = def
	_hidden_slots = PackedStringArray()
	if ClassDB.class_exists("SwmCharacterStore"):
		for sp: Dictionary in SwmCharacterStore.get_species_list():
			if str(sp.get("id", "")) == species:
				_hidden_slots = sp.get("hidden_slots", PackedStringArray())
				break


func _current_loadout() -> Dictionary:
	for lo: Dictionary in loadouts:
		if int(lo.get("slot_index", -1)) == _edit_slot:
			return lo
	return {
		"slot_index": _edit_slot,
		"name": "Loadout %d" % (_edit_slot + 1),
		"primary_id": 0,
		"sidearm_id": 0,
		"secondary_id": 0,
		"gear_ids": {},
		"appearance_ids": {},
	}


func _inst_by_id(id: int) -> Dictionary:
	if id <= 0:
		return {}
	for inst: Dictionary in inventory:
		if int(inst.get("id", 0)) == id:
			return inst
	return {}


func _sync_stage() -> void:
	if _stage == null:
		return
	var lo := _current_loadout()
	# What it shows, as the world shows it (core resolve_worn_models): its
	# gear and cosmetic override, a bodysuit in place of everything, a
	# Wookiee's or Ithorian's own armor.
	var worn_csv := ""
	if store != null and store.has_method("worn_models"):
		worn_csv = str(store.worn_models(species, gender, lo, inventory))
	var ov_rel := worn_csv.trim_prefix("override:") if worn_csv.begins_with("override:") else ""
	var key := "%s|%s|%s|%s" % [species, gender, appearance_json, ov_rel]
	if key != _stage_key:
		_stage_key = key
		if ov_rel != "":
			_stage.load_override(ov_rel)
		else:
			_stage.load_species(species, gender)
			var app: Dictionary = {}
			var parsed: Variant = JSON.parse_string(appearance_json)
			if parsed is Dictionary:
				app = parsed
			_stage.apply_appearance(app, 0.88, 1.12, 1.78)
			var hair_id := str(app.get("hair", ""))
			if hair_id != "":
				_stage.set_hair("hairs/" + hair_id + ".gltf", app.get("colors", {}))
		_stage_worn_key = ""

	var worn: Array = [] if ov_rel != "" else Array(worn_csv.split(",", false))
	var weapon_model := ""
	var prim := _inst_by_id(int(lo.get("primary_id", 0)))
	if not prim.is_empty():
		var def: Dictionary = _def_by_id.get(str(prim.get("def_id", "")), {})
		weapon_model = str(def.get("model", ""))
	var wkey := "|".join(worn) + "::" + weapon_model
	if wkey != _stage_worn_key:
		_stage_worn_key = wkey
		_stage.set_worn_models(worn, weapon_model)


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		_mouse_pos = event.position
		if _rotating and _stage != null:
			_stage.rotate_by(event.relative.x * 0.012)
			accept_event()
			return
		if not _dragging_inst.is_empty() and _mouse_pos.distance_to(_drag_origin) > 6.0 * u:
			_drag_active = true
		queue_redraw()
	elif event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP and event.pressed:
			_scroll = maxi(0, _scroll - 1)
			queue_redraw()
			accept_event()
			return
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN and event.pressed:
			_scroll = mini(maxi(0, _filtered.size() - _rows_visible), _scroll + 1)
			queue_redraw()
			accept_event()
			return
		elif event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
			for sr: Dictionary in _slot_rects:
				if (sr["rect"] as Rect2).has_point(event.position):
					_unequip_slot(str(sr["slot"]), bool(sr["is_app"]))
					accept_event()
					return
		elif event.button_index == MOUSE_BUTTON_LEFT:
			if event.pressed:
				if _stage_rect.has_point(event.position):
					_rotating = true
					accept_event()
					return
				if not _hover_inst.is_empty():
					_dragging_inst = _hover_inst
					_drag_origin = event.position
					_drag_active = false
					_selected_item_id = int(_hover_inst.get("id", 0))
					_confirm_destroy_id = 0
					if event.double_click:
						_quick_equip(_hover_inst)
						_dragging_inst = {}
						accept_event()
						return
			else:
				_rotating = false
				if _drag_active and not _dragging_inst.is_empty():
					for sr: Dictionary in _slot_rects:
						if (sr["rect"] as Rect2).has_point(event.position):
							_try_equip_into(_dragging_inst, str(sr["slot"]), bool(sr["is_app"]))
							break
					_dragging_inst = {}
					_drag_active = false
					queue_redraw()
					accept_event()
					return
				_dragging_inst = {}
				_drag_active = false
	super._gui_input(event)


func _paint() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.6))
	var box := centered(920, 540, Vector2(0, 8))
	text("gamefont_large", "GEAR & LOADOUTS", Vector2(size.x * 0.5, box.position.y - 40 * u), "Center", ACCENT)
	frame(box)

	var x := box.position.x + 16 * u
	var y := box.position.y + 12 * u
	for s in 5:
		button(Rect2(x + s * 86 * u, y, 80 * u, 28 * u), _tab_label(s), "tab:%d" % s, _edit_slot == s)

	button(Rect2(x + 438 * u, y, 134 * u, 28 * u),
			"Active" if _edit_slot == active_loadout_idx else "Activate Loadout",
			"activate", false, false, _edit_slot != active_loadout_idx)
	text("gamefont_small", "%d FP  |  %d Cr  |  %d XP" % [faction_points, credits, experience],
			Vector2(box.end.x - 120 * u, y + 6 * u), "Right", ACCENT)
	button(Rect2(box.end.x - 104 * u, y, 88 * u, 28 * u), "Close", "close", false, true)

	var left := Rect2(x, y + 38 * u, 468 * u, box.size.y - 62 * u)
	var right := Rect2(left.end.x + 12 * u, y + 38 * u, box.end.x - left.end.x - 28 * u, box.size.y - 62 * u)
	_paint_paperdoll(left)
	_paint_inventory(right)

	if _drag_active and not _dragging_inst.is_empty():
		var ddef: Dictionary = _def_by_id.get(str(_dragging_inst.get("def_id", "")), {})
		var ghost := Rect2(_mouse_pos + Vector2(10, 10) * u, Vector2(180, 26) * u)
		frame(ghost, "border_popup")
		text("gamefont_tiny", ItemTooltip.item_name(ddef, _dragging_inst), ghost.position + Vector2(8, 6) * u, "Left", ACCENT)
	elif not _hover_inst.is_empty():
		var hdef: Dictionary = _def_by_id.get(str(_hover_inst.get("def_id", "")), {})
		var eq := _find_equipped_for_def(hdef)
		var lines := ItemTooltip.build_lines(hdef, _hover_inst, store, eq.get("def", {}), eq.get("inst", {}))
		ItemTooltip.draw(self, _mouse_pos, ItemTooltip.item_name(hdef, _hover_inst), lines)


func _paint_paperdoll(box: Rect2) -> void:
	frame(box)
	var x := box.position.x + 10 * u
	var y := box.position.y + 10 * u
	var lo := _current_loadout()
	if _name_edit.visible:
		_name_edit.position = Vector2(x, y)
		_name_edit.size = Vector2(box.size.x - 340 * u, 24 * u)
	else:
		text("gamefont_small", str(lo.get("name", "Loadout")), Vector2(x, y + 4 * u), "Left", ACCENT)
	button(Rect2(box.end.x - 320 * u, y, 68 * u, 24 * u), "Rename", "rename", _name_edit.visible)
	button(Rect2(box.end.x - 246 * u, y, 108 * u, 24 * u), "Gear (Combat)", "pmode:gear", _paperdoll_mode == "gear")
	button(Rect2(box.end.x - 134 * u, y, 130 * u, 24 * u), "Cosmetic Override", "pmode:appearance", _paperdoll_mode == "appearance")

	var slots_y := y + 32 * u
	_slot_rects.clear()
	var slot_list := GEAR_SLOTS if _paperdoll_mode == "gear" else APPEARANCE_SLOTS
	var col_w := 148.0 * u
	var row_count := (slot_list.size() + 1) / 2
	var avail_h := (box.end.y - 110 * u) - slots_y
	var step := minf(46.0 * u, avail_h / float(maxi(1, row_count)))
	var slot_h := minf(40.0 * u, step - 5.0 * u)
	var is_app := _paperdoll_mode == "appearance"

	_stage_rect = Rect2(x + col_w + 6 * u, slots_y, box.size.x - col_w * 2 - 32 * u, 280 * u)
	# The preview renders at its box's own size and shape: drawn into a box
	# of another shape, the character came out squeezed thin (tall-looking).
	var want := Vector2i(maxi(16, roundi(_stage_rect.size.x)), maxi(16, roundi(_stage_rect.size.y)))
	if _viewport != null and _viewport.size != want:
		_viewport.size = want
	if _viewport != null and _viewport.get_texture() != null:
		draw_texture_rect(_viewport.get_texture(), _stage_rect, false)
	else:
		draw_rect(_stage_rect, Color(0.08, 0.1, 0.14, 0.7))
		text("gamefont_tiny", "3D Preview", _stage_rect.get_center(), "Center", DIM)

	var drag_def: Dictionary = _def_by_id.get(str(_dragging_inst.get("def_id", "")), {}) if _drag_active else {}
	for i in slot_list.size():
		var s_id: String = slot_list[i][0]
		var s_lbl: String = slot_list[i][1]
		var left_col := i < row_count
		var row := i if left_col else i - row_count
		var sx := x if left_col else box.end.x - col_w - 10 * u
		var sy := slots_y + row * step
		var r := Rect2(sx, sy, col_w, slot_h)
		var hidden := _hidden_slots.has(s_id)
		var valid_drop := _drag_active and _is_compatible(drag_def, s_id, is_app)
		var tint := Color(0.65, 1.0, 0.7) if valid_drop else Color.WHITE
		frame(r, "border_3_pieces", tint)
		_slot_rects.append({"rect": r, "slot": s_id, "is_app": is_app})

		var eq_id := _equipped_id_in_slot(lo, s_id, is_app)
		var eq_inst := _inst_by_id(eq_id)
		var eq_name := "(Empty)"
		if not eq_inst.is_empty():
			var edef: Dictionary = _def_by_id.get(str(eq_inst.get("def_id", "")), {})
			eq_name = ItemTooltip.item_name(edef, eq_inst)
		# Its two rows centred in the slot (slots shrink when there are many).
		var pad := maxf(2.0 * u, (slot_h - 30.0 * u) * 0.5)
		text("gamefont_tiny", s_lbl + (" (not shown)" if hidden else ""), Vector2(sx + 6 * u, sy + pad), "Left", DIM)
		# Within the slot (and clear of its [x]): a long name with a Mk is cut short.
		text("gamefont_tiny", fit_text("gamefont_tiny", eq_name, col_w - (32.0 if eq_id > 0 else 12.0) * u),
				Vector2(sx + 6 * u, sy + pad + 15 * u), "Left", TEXT if eq_id > 0 else DIM)
		if eq_id > 0:
			var clear_r := Rect2(r.end.x - 20 * u, sy + pad, 16 * u, 16 * u)
			button(clear_r, "x", "unequip:%s:%s" % ["app" if is_app else "gear", s_id])

	# Bottom stats summary
	var stats_y := box.end.y - 104 * u
	var st: Dictionary = store.compute_loadout_stats(species, lo, inventory, true) if store != null else {}
	if not st.is_empty():
		text("gamefont_tiny", "Health: %.0f   |   Run: %.1f m/s   |   Energy: %.0f" % [
				float(st.get("max_health", 400)), float(st.get("run_speed", 6.5)), float(st.get("max_energy", 100))],
				Vector2(x, stats_y), "Left", TEXT)
		text("gamefont_tiny", "Armor: %d%%   |   Laser: %d%%   |   Ballistic: %d%%   |   Explosive: %d%%" % [
				roundi(float(st.get("mitigation", 0)) * 100),
				roundi(float(st.get("mitigation_laser", 0)) * 100),
				roundi(float(st.get("mitigation_ballistic", 0)) * 100),
				roundi(float(st.get("mitigation_explosive", 0)) * 100)],
				Vector2(x, stats_y + 18 * u), "Left", DIM)
	text("gamefont_tiny", "Drag items onto slots (or double-click). Right-click or [x] unequips. Cosmetic Override: worn only for its look.",
			Vector2(x, box.end.y - 24 * u), "Left", DIM)


func _paint_inventory(box: Rect2) -> void:
	frame(box)
	var x := box.position.x + 10 * u
	var y := box.position.y + 10 * u
	var cx := x
	var row_w := box.size.x - 20 * u
	var cat_bw := maxf(48.0 * u, (row_w - float(CATEGORIES.size() - 1) * 3.0 * u) / float(CATEGORIES.size()))
	for cat: Array in CATEGORIES:
		button(Rect2(cx, y, cat_bw, 24 * u), cat[1], "icat:" + cat[0], _category == cat[0])
		cx += cat_bw + 3 * u
	y += 30 * u
	cx = x
	for s_entry: Array in [["name", "Name"], ["kind", "Kind"], ["slot", "Slot"], ["quality", "Quality"]]:
		var bw := 82.0 * u
		button(Rect2(cx, y, bw, 22 * u), "Sort: " + s_entry[1], "sort:" + s_entry[0], _sort_by == s_entry[0])
		cx += bw + 4 * u
	y += 30 * u

	_filtered.clear()
	for inst: Dictionary in inventory:
		var def: Dictionary = _def_by_id.get(str(inst.get("def_id", "")), {})
		var k := str(def.get("kind", ""))
		if _category == "misc":
			if k != "junk" and k != "supply" and k != "quest":
				continue
		elif _category == "clothing":
			if k != "clothing" and k != "bodysuit":  # (bodysuits with the clothes)
				continue
		elif _category != "all" and k != _category:
			continue
		_filtered.append(inst)

	_filtered.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var da: Dictionary = _def_by_id.get(str(a.get("def_id", "")), {})
		var db: Dictionary = _def_by_id.get(str(b.get("def_id", "")), {})
		match _sort_by:
			"kind": return str(da.get("kind", "")) < str(db.get("kind", ""))
			"slot": return str(da.get("slot", "")) < str(db.get("slot", ""))
			"quality": return float(a.get("quality", 0.5)) > float(b.get("quality", 0.5))
		return str(da.get("name", "")) < str(db.get("name", ""))
	)

	_scroll = clampi(_scroll, 0, maxi(0, _filtered.size() - _rows_visible))
	_hover_inst = {}
	var row_h := 36.0 * u
	for row_i in _rows_visible:
		var idx := _scroll + row_i
		if idx >= _filtered.size():
			break
		var inst: Dictionary = _filtered[idx]
		var iid := int(inst.get("id", 0))
		var def: Dictionary = _def_by_id.get(str(inst.get("def_id", "")), {})
		var ry := y + row_i * (row_h + 4 * u)
		var r := Rect2(x, ry, row_w, row_h)
		var hovered := r.has_point(_mouse_pos) and not _drag_active
		if hovered:
			_hover_inst = inst
		var selected := iid == _selected_item_id
		frame(r, "border_3_pieces", Color(0.75, 0.88, 1.0) if (hovered or selected) else Color(0.85, 0.85, 0.9))
		var used_in := _loadout_badges(iid)
		var kind := str(def.get("kind", ""))
		text("gamefont_small", ItemTooltip.item_name(def, inst), Vector2(x + 8 * u, ry + 5 * u),
				"Left", ACCENT if selected else TEXT)
		var sub_txt := ""
		if kind == "junk":
			sub_txt = "Junk | Sell: %d Cr" % int(def.get("sell_value", 0))
		elif kind == "supply" and str(def.get("teaches", "")) != "":
			sub_txt = "Blueprint | Use to learn its schematic"
		elif kind == "supply":
			sub_txt = "Supply | +%d HP" % roundi(float(def.get("heal_amount", 0.0)))
		elif kind == "ammo":
			var cap := 99 if str(def.get("ammo_class", "")) == "heavy" else 999
			sub_txt = "Ammo | %d / %d rounds %s" % [int(inst.get("quantity", 1)), cap, used_in]
		elif kind == "quest":
			sub_txt = "Quest item"
		elif kind == "vehicle":
			sub_txt = "Vehicle | Use to call it, again to put it away"
		else:
			sub_txt = "%s | Q %d%% %s" % [str(def.get("slot", "")).capitalize(),
					roundi(float(inst.get("quality", 0.5)) * 100), used_in]
		text("gamefont_tiny", sub_txt, Vector2(x + 8 * u, ry + 20 * u), "Left", DIM)

	var foot_y := box.end.y - 36 * u
	if _selected_item_id > 0:
		var confirming := _confirm_destroy_id == _selected_item_id
		button(Rect2(x, foot_y, 134 * u, 26 * u),
				"Confirm Destroy" if confirming else "Destroy Item", "destroy", confirming, confirming)
		var sel_inst := _inst_by_id(_selected_item_id)
		var sel_def: Dictionary = _def_by_id.get(str(sel_inst.get("def_id", "")), {})
		if str(sel_def.get("kind", "")) == "supply":
			button(Rect2(x + 142 * u, foot_y, 112 * u, 26 * u), "Use Supply", "use_supply")
		elif str(sel_def.get("kind", "")) == "vehicle":
			button(Rect2(x + 142 * u, foot_y, 150 * u, 26 * u), "Call / Put Away", "use_supply")
	if _filtered.size() > _rows_visible:
		text("gamefont_tiny", "%d-%d of %d" % [_scroll + 1, mini(_scroll + _rows_visible, _filtered.size()), _filtered.size()],
				Vector2(box.end.x - 16 * u, foot_y + 6 * u), "Right", DIM)


func _loadout_badges(item_id: int) -> String:
	var tags: PackedStringArray = []
	for lo: Dictionary in loadouts:
		var s := int(lo.get("slot_index", 0)) + 1
		var used := int(lo.get("primary_id", 0)) == item_id or int(lo.get("sidearm_id", 0)) == item_id \
				or int(lo.get("secondary_id", 0)) == item_id
		if not used:
			for v: Variant in (lo.get("gear_ids", {}) as Dictionary).values():
				if int(v) == item_id:
					used = true
					break
		if not used:
			for v: Variant in (lo.get("appearance_ids", {}) as Dictionary).values():
				if int(v) == item_id:
					used = true
					break
		if used:
			tags.append("L%d" % s)
	return ("[" + ",".join(tags) + "]") if not tags.is_empty() else ""


func _equipped_id_in_slot(lo: Dictionary, slot: String, is_app: bool) -> int:
	if is_app:
		return int((lo.get("appearance_ids", {}) as Dictionary).get(slot, 0))
	if slot == "primary": return int(lo.get("primary_id", 0))
	if slot == "sidearm": return int(lo.get("sidearm_id", 0))
	if slot == "secondary": return int(lo.get("secondary_id", 0))
	return int((lo.get("gear_ids", {}) as Dictionary).get(slot, 0))


func _is_compatible(def: Dictionary, slot: String, is_app: bool) -> bool:
	if def.is_empty():
		return false
	# As core item_fits_slot: armor and clothing in either panel, the rest
	# only in the gear.
	var kind := str(def.get("kind", ""))
	var dslot := str(def.get("slot", ""))
	if kind == "armor" or kind == "clothing":
		return WORN_SLOTS.has(slot) and dslot == slot
	if kind == "bodysuit":  # the one look-only item
		return is_app and slot == "full_body"
	if is_app:
		return false
	if kind == "weapon":
		# A species weapon: only its species' hands.
		var only: PackedStringArray = def.get("species", PackedStringArray())
		return dslot == slot and (only.is_empty() or only.has(species))
	if kind == "deflector_shield" or kind == "ammo":
		return dslot == slot
	if kind == "vehicle":
		return slot == "mount"
	return false


func _try_equip_into(inst: Dictionary, slot: String, is_app: bool) -> void:
	var def: Dictionary = _def_by_id.get(str(inst.get("def_id", "")), {})
	if not _is_compatible(def, slot, is_app):
		return
	var lo := _current_loadout().duplicate(true)
	var iid := int(inst.get("id", 0))
	if is_app:
		var app: Dictionary = (lo.get("appearance_ids", {}) as Dictionary).duplicate()
		app[slot] = iid
		lo["appearance_ids"] = app
	elif slot == "primary":
		lo["primary_id"] = iid
	elif slot == "sidearm":
		lo["sidearm_id"] = iid
	elif slot == "secondary":
		lo["secondary_id"] = iid
	else:
		var gear: Dictionary = (lo.get("gear_ids", {}) as Dictionary).duplicate()
		gear[slot] = iid
		lo["gear_ids"] = gear
	loadout_saved.emit(_edit_slot, lo)


func _quick_equip(inst: Dictionary) -> void:
	var def: Dictionary = _def_by_id.get(str(inst.get("def_id", "")), {})
	if def.is_empty():
		return
	var kind := str(def.get("kind", ""))
	var slot := str(def.get("slot", ""))
	if kind == "supply":
		item_used.emit(int(inst.get("id", 0)))
		return
	if kind == "vehicle":
		_paperdoll_mode = "gear"
		_try_equip_into(inst, "mount", false)
		return
	if kind == "junk" or kind == "quest":
		return
	# Into the panel that's open, if it fits there: worn things go in either,
	# a bodysuit only in the Cosmetic Override.
	if (_paperdoll_mode == "appearance" or kind == "bodysuit") and _is_compatible(def, slot, true):
		_paperdoll_mode = "appearance"
		_try_equip_into(inst, slot, true)
	else:
		_paperdoll_mode = "gear"
		_try_equip_into(inst, slot, false)


func _unequip_slot(slot: String, is_app: bool) -> void:
	var lo := _current_loadout().duplicate(true)
	if is_app:
		var app: Dictionary = (lo.get("appearance_ids", {}) as Dictionary).duplicate()
		app.erase(slot)
		lo["appearance_ids"] = app
	elif slot == "primary":
		lo["primary_id"] = 0
	elif slot == "sidearm":
		lo["sidearm_id"] = 0
	elif slot == "secondary":
		lo["secondary_id"] = 0
	else:
		var gear: Dictionary = (lo.get("gear_ids", {}) as Dictionary).duplicate()
		gear.erase(slot)
		lo["gear_ids"] = gear
	loadout_saved.emit(_edit_slot, lo)


func _find_equipped_for_def(def: Dictionary) -> Dictionary:
	var lo := _current_loadout()
	var is_app := _paperdoll_mode == "appearance" and _is_compatible(def, str(def.get("slot", "")), true)
	var slot := str(def.get("slot", ""))
	var eq_id := _equipped_id_in_slot(lo, slot, is_app)
	if eq_id <= 0:
		return {}
	var inst := _inst_by_id(eq_id)
	if inst.is_empty():
		return {}
	return {"def": _def_by_id.get(str(inst.get("def_id", "")), {}), "inst": inst}


func _on_click(id: String, _at: Vector2) -> void:
	if id == "rename":
		_name_edit.text = str(_current_loadout().get("name", ""))
		_name_edit.visible = true
		_name_edit.grab_focus()
		_name_edit.select_all()
	elif id.begins_with("tab:"):
		_edit_slot = int(id.substr(4))
		_confirm_destroy_id = 0
		_name_edit.visible = false
		_sync_stage()
	elif id.begins_with("pmode:"):
		_paperdoll_mode = id.substr(6)
	elif id.begins_with("icat:"):
		_category = id.substr(5)
		_scroll = 0
	elif id.begins_with("sort:"):
		_sort_by = id.substr(5)
	elif id.begins_with("unequip:"):
		var parts := id.split(":")
		if parts.size() == 3:
			_unequip_slot(parts[2], parts[1] == "app")
	elif id == "activate":
		loadout_activated.emit(_edit_slot)
	elif id == "use_supply" and _selected_item_id > 0:
		item_used.emit(_selected_item_id)
	elif id == "destroy" and _selected_item_id > 0:
		if _confirm_destroy_id == _selected_item_id:
			item_destroyed.emit(_selected_item_id)
			_confirm_destroy_id = 0
			_selected_item_id = 0
		else:
			_confirm_destroy_id = _selected_item_id
	elif id == "close":
		closed.emit()
