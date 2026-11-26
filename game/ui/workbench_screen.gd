# Single-Screen Unified Workbench + Dedicated Material Vault + Work Orders +
# Salvage (docs/PROFESSIONS.md) in Battlefront II's frames, fonts and colors.
extends "res://ui/bf2_screen.gd"

signal craft_requested(schematic_id: String, resource_ids: PackedStringArray, enhancement_ids: PackedInt64Array, exp_quality_pct: int, batch_count: int)
signal preset_saved(name: String, schematic_id: String, resource_ids: PackedStringArray, enhancement_defs: PackedStringArray, exp_quality_pct: int)
signal preset_deleted(name: String)
signal work_order_fulfilled(order_id: String)
signal item_salvaged(item_id: int)
signal closed

const TABS := [
	["workbench", "Workbench"],
	["vault", "Material Vault"],
	["orders", "Work Orders"],
	["salvage", "Salvage"],
]
const CRAFTERS := [
	["weaponsmith", "Weaponsmith"],
	["droid_engineer", "Droid Engineer"],
	["armorsmith", "Armorsmith"],
	["tailor", "Tailor"],
	["chef", "Chef"],
	["doctor", "Doctor"],
	["bio_engineer", "Bio-Engineer"],
	["architect", "Architect"],
]
# What each kind of station makes (as core's station_makes: Galaxies'
# stations each made their own professions' schematics); a "general" one
# everything. The server holds to it.
const STATION_MAKES := {
	"weapon": ["weaponsmith", "droid_engineer"],
	"clothing": ["armorsmith", "tailor"],
	"food": ["chef", "doctor", "bio_engineer"],
	"structure": ["architect"],
}
const VAULT_CATS := [
	["all", "All"],
	["mineral", "Mineral"],
	["chemical", "Chemical"],
	["gas", "Gas"],
	["flora", "Flora"],
	["water", "Water"],
	["organic", "Organic"],
	["salvage", "Salvage"],
]
const STAT_SORTS := [
	["quality", "Quality"],
	["conductivity", "Conductivity"],
	["toughness", "Toughness"],
	["malleability", "Malleability"],
	["purity", "Purity"],
	["amount", "Amount"],
]
const GOOD := Color(0.45, 0.9, 0.5)
const WARN := Color(0.95, 0.72, 0.25)
const BAD := Color(0.95, 0.4, 0.35)

var store: Object
var catalog: Array = []
var resources: Array = []
var schematics: Array = []
var work_orders: Array = []
var materials: Array = []
var presets: Array = []
var inventory: Array = []
var loadouts: Array = []
var professions: Dictionary = {}
var feedback := ""
var station_kind := "general"  # a station's kind; "none" away from one (nothing is made)
var station_title := ""

var _tab := "workbench"
var _prof_filter := "all"
var _selected_schematic := ""
var _pinned: Dictionary = {}
var _slot_resources: Array = []       # resource_id per schematic slot
var _slot_enhancements: Array = []    # item_id (int) per enhancement slot
var _exp_quality_pct := 65
var _sch_scroll := 0
var _vault_cat := "all"
var _vault_sort := "quality"
var _vault_scroll := 0
var _salvage_scroll := 0
var _res_by_id: Dictionary = {}
var _cat_by_id: Dictionary = {}


func open_screen(kind := "general", title := "") -> void:
	station_kind = kind
	station_title = title
	_prof_filter = "all"
	_rebuild_lookups()
	var list := _filtered_schematics()
	_select_schematic(str(list[0].get("id", "")) if not list.is_empty() else "")
	visible = true
	queue_redraw()


func _rebuild_lookups() -> void:
	_res_by_id.clear()
	for r: Dictionary in resources:
		_res_by_id[str(r.get("id", ""))] = r
	_cat_by_id.clear()
	for d: Dictionary in catalog:
		_cat_by_id[str(d.get("id", ""))] = d


func _prof_level(id: String) -> int:
	if store == null:
		return 1
	return int(store.profession_level(int(professions.get(id, 0))).get("level", 1))


func _vault_amount(rid: String) -> int:
	for m: Dictionary in materials:
		if str(m.get("resource_id", "")) == rid:
			return int(m.get("amount", 0))
	return 0


func _equipped_ids() -> Dictionary:
	var out := {}
	for lo: Dictionary in loadouts:
		for k in ["primary_id", "sidearm_id", "secondary_id"]:
			var id := int(lo.get(k, 0))
			if id > 0:
				out[id] = true
		for map_k in ["gear_ids", "appearance_ids"]:
			var d: Dictionary = lo.get(map_k, {})
			for v in d.values():
				if int(v) > 0:
					out[int(v)] = true
	return out


# Whether this station makes a profession's schematics.
func _makes(profession: String) -> bool:
	var makes: Array = STATION_MAKES.get(station_kind, [])
	return makes.is_empty() or makes.has(profession)


# Where the rest are made: "Others at the Armor & Clothing, Food & Chemical...
# stations".
func _elsewhere_note() -> String:
	var names := {"weapon": "Weapon & Droid", "clothing": "Armor & Clothing", "food": "Food & Chemical",
			"structure": "Structure & Machinery"}
	var others := PackedStringArray()
	for kind: String in STATION_MAKES:
		if kind != station_kind:
			others.append(names[kind])
	return "Others at the %s stations" % ", ".join(others)


# The station a profession's schematics are made at, by name.
static func _station_name_for(profession: String) -> String:
	for kind: String in STATION_MAKES:
		if (STATION_MAKES[kind] as Array).has(profession):
			return {"weapon": "Weapon & Droid", "clothing": "Armor & Clothing", "food": "Food & Chemical",
					"structure": "Structure & Machinery"}[kind] + " Crafting Station"
	return "crafting station"


# The professions' filter chips this station shows: "All", then each it makes.
func _crafters() -> Array:
	var out: Array = [["all", "All"]]
	for chip: Array in CRAFTERS:
		if _makes(str(chip[0])):
			out.append(chip)
	return out


func _filtered_schematics() -> Array:
	var out := schematics.filter(func(s: Dictionary) -> bool:
		var prof := str(s.get("profession", ""))
		return _makes(prof) and (_prof_filter == "all" or prof == _prof_filter))
	out.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var pa := bool(_pinned.get(str(a.get("id", "")), false))
		var pb := bool(_pinned.get(str(b.get("id", "")), false))
		if pa != pb:
			return pa
		return int(a.get("required_level", 1)) < int(b.get("required_level", 1)))
	return out


func _current_schematic() -> Dictionary:
	for s: Dictionary in schematics:
		if str(s.get("id", "")) == _selected_schematic:
			return s
	return {}


func _select_schematic(id: String) -> void:
	_selected_schematic = id
	var sch := _current_schematic()
	var slots: Array = sch.get("slots", [])
	_slot_resources.resize(slots.size())
	for i in slots.size():
		_slot_resources[i] = ""
	var enh_n := int(sch.get("enhancement_slots", 0))
	_slot_enhancements.resize(enh_n)
	for i in enh_n:
		_slot_enhancements[i] = 0
	if store != null and id != "":
		var filled: PackedStringArray = store.auto_fill_schematic(id, materials, "best_stat")
		for i in mini(filled.size(), _slot_resources.size()):
			_slot_resources[i] = filled[i]


func _enhancement_items() -> Array:
	var eq := _equipped_ids()
	var out: Array = []
	for inst: Dictionary in inventory:
		var iid := int(inst.get("id", 0))
		if eq.has(iid):
			continue
		var def: Dictionary = _cat_by_id.get(str(inst.get("def_id", "")), {})
		if str(def.get("kind", "")) == "enhancement":
			out.append({"inst": inst, "def": def})
	return out


func _paint() -> void:
	_rebuild_lookups()
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.55))
	var box := centered(940, 536, Vector2(0, -6))
	var hdr := station_title if station_title != "" else "Unified Workbench"
	text("gamefont_large", hdr, Vector2(size.x * 0.5, box.position.y - 38 * u), "Center", ACCENT)
	frame(box)
	var x := box.position.x + 16 * u
	var y := box.position.y + 14 * u
	var tx := x
	for tab: Array in TABS:
		var w: float = art.text_width("gamefont_small", tab[1], u) + 28 * u
		button(Rect2(tx, y, w, 28 * u), tab[1], "tab:" + str(tab[0]), _tab == tab[0])
		tx += w + 6 * u
	button(Rect2(box.end.x - 108 * u, y, 92 * u, 28 * u), "Close", "close", false, true)
	y += 38 * u

	var body := Rect2(x, y, box.size.x - 32 * u, box.end.y - y - 14 * u)
	match _tab:
		"workbench":
			_paint_workbench(body)
		"vault":
			_paint_vault(body)
		"orders":
			_paint_orders(body)
		"salvage":
			_paint_salvage(body)
	if feedback != "":
		var fb_y := box.end.y + 8 * u
		var max_w := box.size.x - 24 * u
		var line := ""
		for w in feedback.split(" ", false):
			var cand := w if line == "" else line + " " + w
			if line != "" and art.text_width("gamefont_tiny", cand, u) > max_w:
				text("gamefont_tiny", line, Vector2(size.x * 0.5, fb_y), "Center", ACCENT)
				fb_y += art.line_height("gamefont_tiny", u) + 2 * u
				line = w
			else:
				line = cand
		if line != "":
			text("gamefont_tiny", line, Vector2(size.x * 0.5, fb_y), "Center", ACCENT)


func _paint_workbench(rect: Rect2) -> void:
	var px := rect.position.x
	for chip: Array in _crafters():
		var cw: float = art.text_width("gamefont_tiny", chip[1], u) + 20 * u
		button(Rect2(px, rect.position.y, cw, 22 * u), chip[1], "pfilter:" + str(chip[0]), _prof_filter == chip[0])
		px += cw + 5 * u
	if STATION_MAKES.has(station_kind):
		text("gamefont_tiny", _elsewhere_note(), Vector2(rect.end.x, rect.position.y + 4 * u), "Right", DIM)
	elif station_kind == "none":
		text("gamefont_tiny", "Craft at a crafting station", Vector2(rect.end.x, rect.position.y + 4 * u), "Right", WARN)
	var y := rect.position.y + 30 * u
	var col_h := rect.end.y - y

	# Left column: Schematics
	var left := Rect2(rect.position.x, y, 270 * u, col_h)
	draw_rect(left, Color(0.04, 0.06, 0.09, 0.65))
	var list := _filtered_schematics()
	var rows := 11
	var row_h := (left.size.y - 8 * u) / rows
	_sch_scroll = clampi(_sch_scroll, 0, maxi(0, list.size() - rows))
	for i in range(_sch_scroll, mini(list.size(), _sch_scroll + rows)):
		var s: Dictionary = list[i]
		var sid := str(s.get("id", ""))
		var row := Rect2(left.position.x + 4 * u, left.position.y + 4 * u + (i - _sch_scroll) * row_h,
				left.size.x - 8 * u, row_h - 3 * u)
		var sel := sid == _selected_schematic
		draw_rect(row, Color(0.25, 0.34, 0.48, 0.6) if sel else Color(0.1, 0.13, 0.18, 0.45))
		var pin_r := Rect2(row.position.x + 4 * u, row.position.y + 4 * u, 22 * u, row.size.y - 8 * u)
		var pinned := bool(_pinned.get(sid, false))
		button(pin_r, "*" if pinned else "-", "pin:" + sid, pinned)
		var main_r := Rect2(pin_r.end.x + 4 * u, row.position.y, row.size.x - 32 * u, row.size.y)
		region(main_r, "sch:" + sid)
		var req := int(s.get("required_level", 1))
		var lv := _prof_level(str(s.get("profession", "")))
		text("gamefont_tiny", str(s.get("name", sid)), main_r.position + Vector2(4, 3) * u, "Left",
				ACCENT if sel else (TEXT if lv >= req else DIM))
		text("gamefont_tiny", "%s Lv %d" % [str(s.get("profession", "")).capitalize(), req],
				main_r.position + Vector2(4, 18) * u, "Left", GOOD if lv >= req else BAD)

	# Center column: 1-step slots + enhancements + auto-fill
	var center := Rect2(left.end.x + 12 * u, y, 300 * u, col_h)
	draw_rect(center, Color(0.04, 0.06, 0.09, 0.65))
	var sch := _current_schematic()
	if sch.is_empty():
		return
	var cx := center.position.x + 12 * u
	var cy := center.position.y + 10 * u
	text("gamefont_medium", str(sch.get("name", "")), Vector2(cx, cy), "Left", ACCENT)
	cy += 24 * u
	var prof := str(sch.get("profession", ""))
	var req := int(sch.get("required_level", 1))
	var lv := _prof_level(prof)
	text("gamefont_tiny", "%s (Lv %d / Req %d)   +%d XP" % [prof.capitalize(), lv, req, int(sch.get("base_xp", 60))],
			Vector2(cx, cy), "Left", GOOD if lv >= req else BAD)
	cy += 18 * u
	if station_kind == "none":
		text("gamefont_tiny", "Made at a " + _station_name_for(prof), Vector2(cx, cy), "Left", WARN)
		cy += 18 * u
	cy += 4 * u
	text("gamefont_tiny", "Auto-Fill:", Vector2(cx, cy + 4 * u), "Left", DIM)
	button(Rect2(cx + 68 * u, cy, 68 * u, 22 * u), "Quality", "fill:best_quality")
	button(Rect2(cx + 140 * u, cy, 68 * u, 22 * u), "Best Stat", "fill:best_stat")
	button(Rect2(cx + 212 * u, cy, 64 * u, 22 * u), "Cheapest", "fill:cheapest")
	cy += 30 * u

	var slots: Array = sch.get("slots", [])
	for i in slots.size():
		var sl: Dictionary = slots[i]
		var rid := str(_slot_resources[i]) if i < _slot_resources.size() else ""
		var r: Dictionary = _res_by_id.get(rid, {})
		var have := _vault_amount(rid) if rid != "" else 0
		var need := int(sl.get("amount", 1))
		var sr := Rect2(cx, cy, center.size.x - 24 * u, 42 * u)
		frame(sr, "border_3_pieces", Color(1, 1, 1, 0.85))
		region(sr, "cslot:%d" % i)
		text("gamefont_tiny", "%s (%d needed)" % [str(sl.get("category_label", "")), need],
				sr.position + Vector2(8, 4) * u, "Left", DIM)
		var rlabel := "%s (%d in vault)" % [str(r.get("name", rid)), have] if not r.is_empty() else "Click to choose material"
		text("gamefont_tiny", rlabel, sr.position + Vector2(8, 21) * u, "Left",
				GOOD if have >= need else (BAD if rid != "" else WARN))
		cy += 48 * u

	var enh_n := int(sch.get("enhancement_slots", 0))
	if enh_n > 0:
		cy += 4 * u
		text("gamefont_tiny", "Optional Enhancement Slots (%d):" % enh_n, Vector2(cx, cy), "Left", ACCENT)
		cy += 18 * u
		for ei in enh_n:
			var eid := int(_slot_enhancements[ei]) if ei < _slot_enhancements.size() else 0
			var er := Rect2(cx, cy, center.size.x - 24 * u, 34 * u)
			frame(er, "border_3_pieces", Color(0.7, 0.85, 1.0, 0.7))
			region(er, "eslot:%d" % ei)
			var elabel := "Empty enhancement slot (click to attach)"
			if eid > 0:
				for entry: Dictionary in _enhancement_items():
					if int((entry["inst"] as Dictionary).get("id", 0)) == eid:
						elabel = str((entry["def"] as Dictionary).get("name", "Module"))
						break
			text("gamefont_tiny", elabel, er.position + Vector2(8, 10) * u, "Left", TEXT if eid > 0 else DIM)
			cy += 40 * u

	# Right column: Stat weights, Experimentation odds, Presets & Craft
	var right := Rect2(center.end.x + 12 * u, y, rect.end.x - center.end.x - 12 * u, col_h)
	draw_rect(right, Color(0.04, 0.06, 0.09, 0.65))
	var rx := right.position.x + 12 * u
	var ry := right.position.y + 10 * u
	text("gamefont_small", "Preview & Experimentation", Vector2(rx, ry), "Left", ACCENT)
	ry += 24 * u
	var wparts := PackedStringArray()
	for w: Dictionary in sch.get("weights", []):
		wparts.append("%s %d%%" % [str(w.get("label", "")), roundi(float(w.get("weight", 0.0)) * 100.0)])
	text("gamefont_tiny", "Weights: " + ", ".join(wparts), Vector2(rx, ry), "Left", DIM)
	ry += 20 * u

	var enh_count := 0
	for eid in _slot_enhancements:
		if int(eid) > 0:
			enh_count += 1
	var prev: Dictionary = store.preview_craft(str(sch.get("id", "")), PackedStringArray(_slot_resources),
			enh_count, lv, _exp_quality_pct) if store != null else {}
	if not prev.is_empty():
		text("gamefont_small", "Quality: %d%%  (%d%% - %d%%)" % [
				roundi(float(prev["base_quality"]) * 100.0),
				roundi(float(prev["min_quality"]) * 100.0),
				roundi(float(prev["max_quality"]) * 100.0)], Vector2(rx, ry), "Left", TEXT)
		ry += 22 * u
		text("gamefont_tiny", "Great %d%%   Success %d%%   Neutral %d%%   Flaw %d%%" % [
				roundi(float(prev["great_success"]) * 100.0),
				roundi(float(prev["success"]) * 100.0),
				roundi(float(prev["neutral"]) * 100.0),
				roundi(float(prev["minor_flaw"]) * 100.0)], Vector2(rx, ry), "Left", GOOD)
		ry += 24 * u

	text("gamefont_tiny", "Experimentation Focus: %d%%" % _exp_quality_pct, Vector2(rx, ry), "Left", TEXT)
	button(Rect2(rx + 210 * u, ry - 2 * u, 28 * u, 20 * u), "-", "exp:-10")
	button(Rect2(rx + 242 * u, ry - 2 * u, 28 * u, 20 * u), "+", "exp:+10")
	ry += 28 * u

	text("gamefont_tiny", "Recipe Presets:", Vector2(rx, ry + 3 * u), "Left", ACCENT)
	button(Rect2(rx + 158 * u, ry, 112 * u, 22 * u), "Save Preset", "save_preset")
	ry += 26 * u
	for pi in mini(presets.size(), 4):
		var pr: Dictionary = presets[pi]
		var pname := str(pr.get("name", ""))
		button(Rect2(rx, ry, 236 * u, 22 * u), "%s (%s)" % [pname, str(pr.get("schematic_id", ""))], "load_preset:%d" % pi)
		button(Rect2(rx + 240 * u, ry, 30 * u, 22 * u), "X", "del_preset:" + pname)
		ry += 25 * u

	var ready := lv >= req and station_kind != "none"
	for i in slots.size():
		var rid := str(_slot_resources[i]) if i < _slot_resources.size() else ""
		if rid == "" or _vault_amount(rid) < int((slots[i] as Dictionary).get("amount", 1)):
			ready = false
	var by := right.end.y - 42 * u
	button(Rect2(rx, by, 86 * u, 30 * u), "Craft x1", "craft:1", false, true, ready)
	button(Rect2(rx + 92 * u, by, 86 * u, 30 * u), "Craft x3", "craft:3", false, false, ready)
	button(Rect2(rx + 184 * u, by, 86 * u, 30 * u), "Craft x5", "craft:5", false, false, ready)


func _paint_vault(rect: Rect2) -> void:
	var px := rect.position.x
	for cat: Array in VAULT_CATS:
		var w: float = art.text_width("gamefont_tiny", cat[1], u) + 18 * u
		button(Rect2(px, rect.position.y, w, 22 * u), cat[1], "vcat:" + str(cat[0]), _vault_cat == cat[0])
		px += w + 4 * u
	var sy := rect.position.y + 28 * u
	text("gamefont_tiny", "Sort by:", Vector2(rect.position.x, sy + 4 * u), "Left", DIM)
	var sx := rect.position.x + 62 * u
	for st: Array in STAT_SORTS:
		var w: float = art.text_width("gamefont_tiny", st[1], u) + 18 * u
		button(Rect2(sx, sy, w, 22 * u), st[1], "vsort:" + str(st[0]), _vault_sort == st[0])
		sx += w + 4 * u

	var rows_data: Array = []
	for m: Dictionary in materials:
		var rid := str(m.get("resource_id", ""))
		var amt := int(m.get("amount", 0))
		if amt <= 0:
			continue
		var r: Dictionary = _res_by_id.get(rid, {})
		if _vault_cat != "all" and str(r.get("category", "")) != _vault_cat:
			continue
		rows_data.append({"id": rid, "amount": amt, "def": r})
	rows_data.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if _vault_sort == "amount":
			return int(a["amount"]) > int(b["amount"])
		var sa: Dictionary = (a["def"] as Dictionary).get("stats", {})
		var sb: Dictionary = (b["def"] as Dictionary).get("stats", {})
		return int(sa.get(_vault_sort, 0)) > int(sb.get(_vault_sort, 0)))

	var list_r := Rect2(rect.position.x, sy + 30 * u, rect.size.x, rect.end.y - sy - 30 * u)
	draw_rect(list_r, Color(0.04, 0.06, 0.09, 0.65))
	var rows := 10
	var row_h := (list_r.size.y - 8 * u) / rows
	_vault_scroll = clampi(_vault_scroll, 0, maxi(0, rows_data.size() - rows))
	if rows_data.is_empty():
		text("gamefont_small", "Material Vault is empty. Use the Multi-Scanner (G) outside Mos Eisley!",
				list_r.get_center(), "Center", DIM)
		return
	for i in range(_vault_scroll, mini(rows_data.size(), _vault_scroll + rows)):
		var entry: Dictionary = rows_data[i]
		var r: Dictionary = entry["def"]
		var stats: Dictionary = r.get("stats", {})
		var ry := list_r.position.y + 4 * u + (i - _vault_scroll) * row_h
		var row := Rect2(list_r.position.x + 6 * u, ry, list_r.size.x - 12 * u, row_h - 3 * u)
		draw_rect(row, Color(0.1, 0.14, 0.2, 0.5))
		text("gamefont_small", "%s  (%s)" % [str(r.get("name", entry["id"])), str(r.get("category_label", ""))],
				row.position + Vector2(10, 6) * u, "Left", ACCENT)
		text("gamefont_small", "x%d" % int(entry["amount"]), Vector2(row.position.x + 310 * u, row.position.y + 6 * u),
				"Left", GOOD)
		text("gamefont_tiny", "Qual %d | Cond %d | Tough %d | Mall %d | Purity %d" % [
				int(stats.get("quality", 0)), int(stats.get("conductivity", 0)), int(stats.get("toughness", 0)),
				int(stats.get("malleability", 0)), int(stats.get("purity", 0))],
				Vector2(row.position.x + 380 * u, row.position.y + 9 * u), "Left", TEXT)


func _paint_orders(rect: Rect2) -> void:
	draw_rect(rect, Color(0.04, 0.06, 0.09, 0.65))
	var eq := _equipped_ids()
	var ry := rect.position.y + 10 * u
	for wo: Dictionary in work_orders:
		var out_id := str(wo.get("output_item", ""))
		var need := int(wo.get("count", 1))
		var have := 0
		for inst: Dictionary in inventory:
			if str(inst.get("def_id", "")) == out_id and not eq.has(int(inst.get("id", 0))):
				have += 1
		var row := Rect2(rect.position.x + 10 * u, ry, rect.size.x - 20 * u, 56 * u)
		frame(row, "border_3_pieces", Color(1, 1, 1, 0.8))
		var item_def: Dictionary = _cat_by_id.get(out_id, {})
		text("gamefont_small", str(wo.get("title", "")), row.position + Vector2(12, 8) * u, "Left", ACCENT)
		text("gamefont_tiny", "Deliver %dx %s (have %d)   |   +%d Cr, +%d FP, +%d %s XP" % [
				need, str(item_def.get("name", out_id)), have, int(wo.get("reward_credits", 0)),
				int(wo.get("reward_fp", 0)), int(wo.get("reward_xp", 0)), str(wo.get("profession", "")).capitalize()],
				row.position + Vector2(12, 30) * u, "Left", TEXT)
		button(Rect2(row.end.x - 116 * u, row.position.y + 13 * u, 102 * u, 30 * u),
				"Fulfill", "order:" + str(wo.get("id", "")), false, false, have >= need)
		ry += 64 * u


func _paint_salvage(rect: Rect2) -> void:
	draw_rect(rect, Color(0.04, 0.06, 0.09, 0.65))
	var eq := _equipped_ids()
	var list: Array = []
	for inst: Dictionary in inventory:
		var iid := int(inst.get("id", 0))
		if eq.has(iid):
			continue
		var def: Dictionary = _cat_by_id.get(str(inst.get("def_id", "")), {})
		if str(def.get("kind", "")) != "quest":
			list.append({"inst": inst, "def": def})
	var rows := 9
	var row_h := (rect.size.y - 12 * u) / rows
	_salvage_scroll = clampi(_salvage_scroll, 0, maxi(0, list.size() - rows))
	if list.is_empty():
		text("gamefont_small", "No unequipped items available to salvage.", rect.get_center(), "Center", DIM)
		return
	for i in range(_salvage_scroll, mini(list.size(), _salvage_scroll + rows)):
		var entry: Dictionary = list[i]
		var inst: Dictionary = entry["inst"]
		var def: Dictionary = entry["def"]
		var row := Rect2(rect.position.x + 10 * u, rect.position.y + 6 * u + (i - _salvage_scroll) * row_h,
				rect.size.x - 20 * u, row_h - 4 * u)
		frame(row, "border_3_pieces", Color(0.85, 0.85, 0.9))
		text("gamefont_small", "%s  (%d%%)" % [str(def.get("name", inst.get("def_id", ""))),
				roundi(float(inst.get("quality", 0.5)) * 100.0)], row.position + Vector2(12, 10) * u, "Left", TEXT)
		button(Rect2(row.end.x - 116 * u, row.position.y + 6 * u, 102 * u, row.size.y - 12 * u),
				"Salvage", "salvage:%d" % int(inst.get("id", 0)))


func _on_click(id: String, _at: Vector2) -> void:
	if id == "close":
		closed.emit()
	elif id.begins_with("tab:"):
		_tab = id.substr(4)
	elif id.begins_with("pfilter:"):
		_prof_filter = id.substr(8)
		_sch_scroll = 0
	elif id.begins_with("pin:"):
		var sid := id.substr(4)
		_pinned[sid] = not bool(_pinned.get(sid, false))
	elif id.begins_with("sch:"):
		_select_schematic(id.substr(4))
	elif id.begins_with("fill:"):
		var sch := _current_schematic()
		if store != null and not sch.is_empty():
			var filled: PackedStringArray = store.auto_fill_schematic(str(sch["id"]), materials, id.substr(5))
			for i in mini(filled.size(), _slot_resources.size()):
				_slot_resources[i] = filled[i]
	elif id.begins_with("cslot:"):
		var idx := int(id.substr(6))
		var sch := _current_schematic()
		var slots: Array = sch.get("slots", [])
		if idx >= 0 and idx < slots.size():
			var cat := str((slots[idx] as Dictionary).get("category", ""))
			var choices: Array = []
			for r: Dictionary in resources:
				if str(r.get("category", "")) == cat and _vault_amount(str(r.get("id", ""))) > 0:
					choices.append(str(r.get("id", "")))
			if not choices.is_empty():
				var cur := choices.find(str(_slot_resources[idx]))
				_slot_resources[idx] = choices[(cur + 1) % choices.size()]
	elif id.begins_with("eslot:"):
		var ei := int(id.substr(6))
		var choices: Array = [0]
		for entry: Dictionary in _enhancement_items():
			choices.append(int((entry["inst"] as Dictionary).get("id", 0)))
		var cur := choices.find(int(_slot_enhancements[ei]))
		_slot_enhancements[ei] = choices[(cur + 1) % choices.size()]
	elif id == "exp:-10":
		_exp_quality_pct = clampi(_exp_quality_pct - 10, 0, 100)
	elif id == "exp:+10":
		_exp_quality_pct = clampi(_exp_quality_pct + 10, 0, 100)
	elif id == "save_preset":
		var sch := _current_schematic()
		if not sch.is_empty():
			var pname := "%s Preset" % str(sch.get("name", "Craft"))
			preset_saved.emit(pname, str(sch["id"]), PackedStringArray(_slot_resources),
					PackedStringArray(), _exp_quality_pct)
	elif id.begins_with("load_preset:"):
		var pi := int(id.substr(12))
		if pi >= 0 and pi < presets.size():
			var pr: Dictionary = presets[pi]
			_select_schematic(str(pr.get("schematic_id", "")))
			var rids: PackedStringArray = pr.get("resource_ids", PackedStringArray())
			for i in mini(rids.size(), _slot_resources.size()):
				_slot_resources[i] = rids[i]
			_exp_quality_pct = int(pr.get("exp_quality_pct", 50))
	elif id.begins_with("del_preset:"):
		preset_deleted.emit(id.substr(11))
	elif id.begins_with("craft:"):
		var sch := _current_schematic()
		if not sch.is_empty():
			var eids := PackedInt64Array()
			for eid in _slot_enhancements:
				if int(eid) > 0:
					eids.append(int(eid))
			craft_requested.emit(str(sch["id"]), PackedStringArray(_slot_resources), eids,
					_exp_quality_pct, int(id.substr(6)))
	elif id.begins_with("vcat:"):
		_vault_cat = id.substr(5)
		_vault_scroll = 0
	elif id.begins_with("vsort:"):
		_vault_sort = id.substr(6)
	elif id.begins_with("order:"):
		work_order_fulfilled.emit(id.substr(6))
	elif id.begins_with("salvage:"):
		item_salvaged.emit(int(id.substr(8)))
	queue_redraw()
