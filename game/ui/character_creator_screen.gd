# Character Creator screen: species & gender picker, body & face blend-shape
# sliders, palette swatches, hair styles, markings, species traits, randomizer,
# name generator, and 3D turntable drag.
extends "res://ui/bf2_screen.gd"

signal character_created(record: Dictionary)
signal closed

const CharacterStage = preload("res://rendering/character_stage.gd")

const TABS := ["Body", "Face", "Colors", "Hair", "Traits"]

const NAME_PREFIXES := {
	"human": ["Jarek", "Kael", "Mira", "Tarin", "Vex", "Sera", "Corin", "Dax"],
	"twilek": ["Aayla", "Bib", "Oola", "Nym", "Lyn", "Koa", "Rianna", "Vette"],
	"zabrak": ["Bao", "Darth", "Koth", "Maris", "Sugi", "Tiro", "Zeth", "Kaelen"],
	"rodian": ["Greedo", "Navik", "Wald", "Neela", "Doda", "Onaconda", "Thuku", "Zev"],
	"bothan": ["Borsk", "Koth", "Tereb", "Fey'lya", "Myn", "Nola", "Asyr", "Kair"],
	"sullustan": ["Nien", "Ten", "Syub", "Dllr", "Sian", "Jae", "Kerl", "Aril"],
	"moncal": ["Ackbar", "Raddus", "Gial", "Lee-Char", "Ibtisam", "Naut", "Bant", "Cilghal"],
	"trandoshan": ["Bossk", "Cradossk", "Qyz", "Ssk", "Tosk", "Vossk", "Zir", "Krassk"],
	"wookiee": ["Chewbacca", "Lowbacca", "Zaalbar", "Hanharr", "Tarfful", "Gungi", "Attichitcuk", "Lumpa"],
	"ithorian": ["Momaw", "Chodo", "Roron", "Bulduga", "Ooroo", "Tendau", "Zon", "Voro"],
}
const NAME_SUFFIXES := ["", " Vao", " Dur", " Kryze", " Nunb", " Nadon", " Fett", " Ordo", " Vos", " Solo"]

var _store: Object
var _stage: CharacterStage
var _species_list: Array = []
var _species_idx := 0
var _gender := "male"
var _tab := 0
var _face_page := 0
var _color_page := 0

var _sliders: Dictionary = {}        # customization name -> float (0..1)
var _colors: Dictionary = {}         # variable -> int palette index
var _color_vars: Array = []          # [{variable, label, palette, count}]
var _discrete_vars: Array = []       # [{name, variable, max_count}]
var _hairs: Array = []
var _hair_idx := -1                  # -1 = bald
var _allow_bald := true
var _char_name := ""
var _error_msg := ""
var _camera_face := false
var _turntable_dragging := false
var _clothed := true                 # the plain clothes a new character starts in, on the stage
var _clothes: Dictionary = {}        # covered slot -> model path ("{g}" for the gender)

# What a new character starts in (core: plain_clothes): the slot each covers.
const PLAIN_CLOTHES := {"cloth_shirt_tunic": "chest", "cloth_pants_cargo": "leggings", "cloth_boots_leather": "boots"}
const SWATCH_LIMIT := 32             # palette entries offered (and randomized over)


func open(store: Object, stage: CharacterStage) -> void:
	_store = store
	_stage = stage
	_species_list = store.get_species_list() if store != null else []
	_species_idx = 0
	_gender = "male"
	_tab = 0
	_face_page = 0
	_char_name = _random_name()
	_error_msg = ""
	_camera_face = false
	_load_clothes()
	_reload_species()
	visible = true
	grab_focus()
	queue_redraw()


func is_face_camera() -> bool:
	return _camera_face


func _load_clothes() -> void:
	_clothes.clear()
	if _store == null:
		return
	var catalog: Array = _store.get_catalog()
	if catalog.is_empty() and _store.load_catalog(ProjectSettings.globalize_path("res://items/swg_catalog.ini"),
			ProjectSettings.globalize_path("res://imported_assets/items")) == OK:
		catalog = _store.get_catalog()
	for def: Dictionary in catalog:
		var id := str(def.get("id", ""))
		if PLAIN_CLOTHES.has(id) and str(def.get("model", "")) != "":
			_clothes[PLAIN_CLOTHES[id]] = str(def["model"])


# The plain clothes this species can wear (none in a slot it can't).
func _wearable_clothes() -> Array:
	var hidden: PackedStringArray = _current_species().get("hidden_slots", PackedStringArray())
	var g := "f" if _gender == "female" else "m"
	var out: Array = []
	for slot: String in _clothes:
		if not hidden.has(slot):
			out.append(str(_clothes[slot]).replace("{g}", g))
	return out


func _apply_clothes() -> void:
	if _stage != null:
		_stage.set_worn_models(_wearable_clothes() if _clothed else [], "")


func _current_species() -> Dictionary:
	return _species_list[_species_idx] if _species_idx >= 0 and _species_idx < _species_list.size() else {}


func _reload_species() -> void:
	var sp := _current_species()
	if sp.is_empty() or _stage == null:
		return
	var meta := _stage.load_species(str(sp["id"]), _gender)
	_allow_bald = bool(meta.get("allow_bald", true))
	_hairs = meta.get("hairs", [])
	_hair_idx = -1 if _allow_bald or _hairs.is_empty() else 0
	_sliders.clear()
	_sliders["height"] = 0.5
	_colors.clear()
	_color_vars.clear()
	_discrete_vars.clear()

	# Collect sliders and discrete variables from species.json customizations.
	var seen_sliders := {}
	var seen_disc := {}
	for c: Dictionary in meta.get("customizations", []):
		var cname := str(c.get("name", ""))
		var ctype := str(c.get("type", ""))
		var vars_str := str(c.get("variables", ""))
		if ctype == "hslider":
			if bool(c.get("discrete", false)) or vars_str.begins_with("index_"):
				if not seen_disc.has(vars_str) and vars_str != "":
					seen_disc[vars_str] = true
					var shown := cname.replace("_", " ").capitalize()
					if str(c.get("group", "")) == "lekku":
						shown = "Lekku " + shown
					_discrete_vars.append({"name": shown, "variable": vars_str})
			elif not seen_sliders.has(cname):
				seen_sliders[cname] = true
				_sliders[cname] = 0.5 if "," in vars_str or bool(c.get("is_scale", false)) else 0.0

	# Collect color variables from .colors.json materials.
	var seen_cvars := {}
	var materials: Dictionary = _stage.colors_data.get("materials", {})
	var palettes: Dictionary = _stage.colors_data.get("palettes", {})
	for mat_name: String in materials:
		var mat: Dictionary = materials[mat_name]
		for key: String in ["MAIN", "HUEB"]:
			var factor: Variant = mat.get(key)
			if factor is Dictionary:
				_register_color_var(factor, palettes, seen_cvars)
		for layer: Dictionary in mat.get("layers", []):
			if layer.get("color") is Dictionary:
				_register_color_var(layer["color"], palettes, seen_cvars)

	if _hair_idx >= 0:
		_stage.set_hair(str(_hairs[_hair_idx]["gltf"]), _colors)
		_register_hair_colors(seen_cvars)
	else:
		_stage.set_hair("", _colors)
	_order_color_vars()
	_apply_clothes()
	_apply_to_stage()


# The colours in the order the species' customizations list them (skin,
# markings, eyes...), named for them, and the hair style's; where they list
# any, not what they don't (an eye's white: no one chose it in Galaxies).
func _order_color_vars() -> void:
	var rank := {}
	var names := {}
	var lekku := {}
	for c: Dictionary in _stage.species_meta.get("customizations", []):
		if str(c.get("type", "")) != "color":
			continue
		var v := str(c.get("variables", ""))
		if not rank.has(v):
			rank[v] = rank.size()
			names[v] = str(c.get("name", "")).trim_prefix("color_").replace("_", " ").capitalize()
		if str(c.get("group", "")) == "lekku" and v != "index_color_skin":
			lekku[v] = true
	for i in _color_vars.size():
		var cv: Dictionary = _color_vars[i]
		var v := str(cv["variable"])
		if names.has(v):
			cv["label"] = "Lekku Pattern Color" if lekku.has(v) else str(names[v])
		cv["order"] = int(rank.get(v, 1000 + i))
	if not rank.is_empty():
		_color_vars = _color_vars.filter(func(cv: Dictionary) -> bool: return rank.has(str(cv["variable"])) or bool(cv["hair"]))
	_color_vars.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return int(a["order"]) < int(b["order"]))


func _register_hair_colors(seen_cvars: Dictionary) -> void:
	if _stage == null or _stage.hair_colors_data.is_empty():
		return
	var palettes: Dictionary = _stage.hair_colors_data.get("palettes", {})
	var mats: Dictionary = _stage.hair_colors_data.get("materials", {})
	for m: String in mats:
		for key: String in ["MAIN", "HUEB"]:
			var f: Variant = mats[m].get(key)
			if f is Dictionary:
				_register_color_var(f, palettes, seen_cvars, "Hair" if key == "MAIN" else "Hair Accent")


# `hair_label` names a hair style's colours (their variables are only numbered).
func _register_color_var(factor: Dictionary, palettes: Dictionary, seen: Dictionary, hair_label := "") -> void:
	var v := str(factor.get("variable", ""))
	if v == "" or seen.has(v):
		return
	var pal_name := str(factor.get("palette", ""))
	var pal: PackedByteArray = palettes.get(pal_name, PackedByteArray())
	var count := pal.size() / 3
	if count <= 1:
		return
	seen[v] = true
	_colors[v] = int(factor.get("index", 0))
	var label := v.trim_prefix("index_color_").replace("_", " ").capitalize()
	if hair_label != "": label = hair_label
	elif label == "1": label = "Fur / Skin"
	elif label == "2": label = "Eyes"
	elif label == "3": label = "Markings"
	_color_vars.append({"variable": v, "label": label, "palette": pal, "count": count, "hair": hair_label != ""})


func _apply_to_stage() -> void:
	if _stage == null:
		return
	var sp := _current_species()
	var blends := {}
	for c: Dictionary in _stage.species_meta.get("customizations", []):
		if str(c.get("type", "")) != "hslider" or bool(c.get("discrete", false)) or bool(c.get("is_scale", false)):
			continue
		var cname := str(c.get("name", ""))
		var vars_str := str(c.get("variables", ""))
		if vars_str == "" or vars_str.begins_with("index_"):
			continue
		var t := float(_sliders.get(cname, 0.0))
		if bool(c.get("reverse", false)):
			t = 1.0 - t
		var parts := vars_str.split(",", false)
		if parts.size() == 2:
			blends[parts[0]] = maxf(0.0, (0.5 - t) * 2.0)
			blends[parts[1]] = maxf(0.0, (t - 0.5) * 2.0)
		elif parts.size() == 1:
			blends[parts[0]] = t
	var appearance := {
		"height": float(_sliders.get("height", 0.5)),
		"blends": blends,
		"colors": _colors,
		"hair": str(_hairs[_hair_idx]["id"]) if _hair_idx >= 0 and _hair_idx < _hairs.size() else "",
	}
	var body_h := float(sp.get("female_height" if _gender == "female" else "male_height", 1.78))
	_stage.apply_appearance(appearance, float(sp.get("scale_min", 0.88)), float(sp.get("scale_max", 1.12)), body_h)


func _paint() -> void:
	var sp := _current_species()
	# Left panel: Species & Gender
	var left := Rect2(18 * u, 18 * u, 230 * u, 500 * u)
	frame(left)
	text("gamefont_medium", "Species", left.position + Vector2(18, 16) * u, "Left", ACCENT)
	for i in _species_list.size():
		var s: Dictionary = _species_list[i]
		button(Rect2(left.position + Vector2(18, 50 + i * 36) * u, Vector2(194, 30) * u),
				str(s["name"]), "sp:%d" % i, i == _species_idx)
	var gy := left.position.y + 426 * u
	text("gamefont_small", "Gender", Vector2(left.position.x + 18 * u, gy), "Left", ACCENT)
	button(Rect2(left.position.x + 18 * u, gy + 24 * u, 92 * u, 30 * u), "Male", "gender:male", _gender == "male")
	button(Rect2(left.position.x + 120 * u, gy + 24 * u, 92 * u, 30 * u), "Female", "gender:female", _gender == "female")

	# Top, between the panels: the camera and clothes toggles.
	var mid := (left.end.x + size.x - 368 * u) * 0.5
	button(Rect2(mid - 145 * u, 18 * u, 140 * u, 30 * u),
			"View: Face" if _camera_face else "View: Full", "cam_toggle", _camera_face)
	var can_dress := not _wearable_clothes().is_empty()
	button(Rect2(mid + 5 * u, 18 * u, 140 * u, 30 * u),
			("Clothes: On" if _clothed else "Clothes: Off") if can_dress else "No Clothes",
			"clothes", _clothed and can_dress, false, can_dress)

	# Right panel: Customization tabs
	var right := Rect2(size.x - 368 * u, 18 * u, 350 * u, 500 * u)
	frame(right)
	for i in TABS.size():
		button(Rect2(right.position + Vector2(12 + i * 65, 16) * u, Vector2(61, 28) * u),
				TABS[i], "tab:%d" % i, i == _tab)

	var cx := right.position.x + 18 * u
	var cy := right.position.y + 58 * u
	match _tab:
		0: _paint_sliders(cx, cy, right.size.x - 36 * u, ["body"])
		1: _paint_sliders(cx, cy, right.size.x - 36 * u, ["face", "eyes_nose", "trunk", "ears_chin_mouth"])
		2: _paint_colors(cx, cy, right.size.x - 36 * u)
		3: _paint_hair(cx, cy, right.size.x - 36 * u)
		4: _paint_traits(cx, cy, right.size.x - 36 * u, sp)

	# Bottom bar: Name, Randomize, Create, Back
	var bar := Rect2(18 * u, size.y - 72 * u, size.x - 36 * u, 56 * u)
	frame(bar)
	text("gamefont_small", "Name:", bar.position + Vector2(16, 19) * u, "Left", ACCENT)
	var name_box := Rect2(bar.position + Vector2(76, 12) * u, Vector2(210, 32) * u)
	draw_rect(name_box, Color(0.02, 0.04, 0.08, 0.95))
	draw_rect(name_box, ACCENT, false, maxf(1.0, u))
	text("gamefont_small", _char_name + "_", name_box.position + Vector2(8, 7) * u, "Left", TEXT)
	button(Rect2(bar.position + Vector2(296, 12) * u, Vector2(124, 32) * u), "Random Name", "rand_name")
	button(Rect2(bar.position + Vector2(430, 12) * u, Vector2(116, 32) * u), "Randomize", "rand_look")
	if _error_msg != "":
		text("gamefont_small", _error_msg, Vector2(bar.position.x + 560 * u, bar.position.y + 19 * u),
				"Left", Color(1.0, 0.35, 0.3))
	button(Rect2(Vector2(bar.end.x - 280 * u, bar.position.y + 12 * u), Vector2(148, 32) * u),
			"Create", "create")
	button(Rect2(Vector2(bar.end.x - 122 * u, bar.position.y + 12 * u), Vector2(106, 32) * u),
			art.localize("common.back"), "back")


func _paint_sliders(x: float, y: float, w: float, groups: Array) -> void:
	if _stage == null:
		return
	var items: Array = []
	var seen := {}
	for c: Dictionary in _stage.species_meta.get("customizations", []):
		if str(c.get("type", "")) != "hslider" or bool(c.get("discrete", false)):
			continue
		if not groups.has(str(c.get("group", ""))):
			continue
		var cname := str(c.get("name", ""))
		if seen.has(cname):
			continue
		seen[cname] = true
		items.append(cname)
	if items.is_empty():
		text("gamefont_small", "No sliders in this category.", Vector2(x, y + 12 * u), "Left", DIM)
		return
	const PER_PAGE := 8
	var pages := maxi(1, ceili(float(items.size()) / PER_PAGE))
	_face_page = clampi(_face_page, 0, pages - 1)
	var slice := items.slice(_face_page * PER_PAGE, (_face_page + 1) * PER_PAGE)
	for i in slice.size():
		var cname: String = slice[i]
		var sy := y + i * 44 * u
		var val := float(_sliders.get(cname, 0.5))
		text("gamefont_small", cname.replace("_", " ").capitalize(), Vector2(x, sy), "Left", TEXT)
		var track := Rect2(Vector2(x, sy + 20 * u), Vector2(w - 48 * u, 10 * u))
		draw_rect(track, Color(0.02, 0.03, 0.06, 0.9))
		draw_rect(Rect2(track.position, Vector2(track.size.x * val, track.size.y)), Color(0.35, 0.65, 1.0))
		draw_rect(track, Color(0.55, 0.6, 0.7), false, maxf(1.0, u))
		text("gamefont_small", "%d%%" % roundi(val * 100.0), Vector2(track.end.x + 8 * u, sy + 16 * u), "Left", DIM)
		region(Rect2(track.position - Vector2(6, 8) * u, track.size + Vector2(12, 16) * u), "cslider:" + cname)
	if pages > 1:
		var py := y + 368 * u
		button(Rect2(x, py, 70 * u, 28 * u), "< Prev", "page:-1", false, false, _face_page > 0)
		text("gamefont_small", "Page %d / %d" % [_face_page + 1, pages], Vector2(x + 150 * u, py + 5 * u), "Center", DIM)
		button(Rect2(x + w - 70 * u, py, 70 * u, 28 * u), "Next >", "page:1", false, false, _face_page + 1 < pages)


func _paint_colors(x: float, y: float, w: float) -> void:
	# Every colour the species has, a page at a time when they don't all fit.
	const COLS := 16
	const ROOM := 360.0  # (u) above the page buttons
	var pages: Array = [[]]
	var used := 0.0
	for cv: Dictionary in _color_vars:
		var h := 18.0 + ceilf(float(mini(int(cv["count"]), SWATCH_LIMIT)) / COLS) * 20.0 + 10.0
		if used + h > ROOM and not (pages.back() as Array).is_empty():
			pages.append([])
			used = 0.0
		(pages.back() as Array).append(cv)
		used += h
	_color_page = clampi(_color_page, 0, pages.size() - 1)
	var cy := y
	var cell := (w - 4 * u) / COLS
	for cv: Dictionary in pages[_color_page]:
		var vname: String = cv["variable"]
		text("gamefont_small", str(cv["label"]), Vector2(x, cy), "Left", TEXT)
		cy += 18 * u
		var pal: PackedByteArray = cv["palette"]
		var count := mini(int(cv["count"]), SWATCH_LIMIT)
		var cur := int(_colors.get(vname, 0))
		for idx in count:
			var r := Rect2(Vector2(x + (idx % COLS) * cell, cy + (idx / COLS) * 20 * u), Vector2(cell - 3 * u, 16 * u))
			draw_rect(r, Color8(pal[idx * 3], pal[idx * 3 + 1], pal[idx * 3 + 2]))
			if idx == cur:
				draw_rect(r, ACCENT, false, maxf(2.0, u))
			region(r, "color:%s:%d" % [vname, idx])
		cy += (ceili(float(count) / COLS) * 20 + 10) * u
	if pages.size() > 1:
		var py := y + 368 * u
		button(Rect2(x, py, 70 * u, 28 * u), "< Prev", "cpage:-1", false, false, _color_page > 0)
		text("gamefont_small", "Page %d / %d" % [_color_page + 1, pages.size()], Vector2(x + w * 0.5, py + 5 * u), "Center", DIM)
		button(Rect2(x + w - 70 * u, py, 70 * u, 28 * u), "Next >", "cpage:1", false, false, _color_page + 1 < pages.size())


func _paint_hair(x: float, y: float, w: float) -> void:
	text("gamefont_small", "Hair Style", Vector2(x, y), "Left", ACCENT)
	var hair_label := "Bald" if _hair_idx < 0 else str(_hairs[_hair_idx]["name"])
	button(Rect2(x, y + 24 * u, 60 * u, 30 * u), "<", "hair:-1", false, false, not _hairs.is_empty())
	text("gamefont_medium", hair_label, Vector2(x + w * 0.5, y + 28 * u), "Center", TEXT)
	button(Rect2(x + w - 60 * u, y + 24 * u, 60 * u, 30 * u), ">", "hair:1", false, false, not _hairs.is_empty())

	var dy := y + 80 * u
	for d: Dictionary in _discrete_vars:
		var vname := str(d["variable"])
		var val := int(_colors.get(vname, 0))
		text("gamefont_small", str(d["name"]), Vector2(x, dy), "Left", TEXT)
		button(Rect2(x + w - 120 * u, dy - 4 * u, 40 * u, 26 * u), "-", "disc:%s:-1" % vname)
		text("gamefont_small", str(val), Vector2(x + w - 60 * u, dy), "Center", ACCENT)
		button(Rect2(x + w - 40 * u, dy - 4 * u, 40 * u, 26 * u), "+", "disc:%s:1" % vname)
		dy += 36 * u


func _paint_traits(x: float, y: float, w: float, sp: Dictionary) -> void:
	text("gamefont_medium", str(sp.get("name", "")), Vector2(x, y), "Left", ACCENT)
	text("gamefont_small", "Height range: %.2f - %.2f m" % [
			float(sp.get("male_height", 1.78)) * float(sp.get("scale_min", 0.88)),
			float(sp.get("male_height", 1.78)) * float(sp.get("scale_max", 1.12))],
			Vector2(x, y + 32 * u), "Left", TEXT)
	var hidden: PackedStringArray = sp.get("hidden_slots", PackedStringArray())
	var gear_text := "Wears all armor" if hidden.is_empty() \
			else "Wears all armor; not shown: " + ", ".join(hidden)
	text("gamefont_small", gear_text, Vector2(x, y + 60 * u), "Left",
			ACCENT if not hidden.is_empty() else DIM)
	var summary := str(sp.get("traits", ""))
	var parts := summary.split("; ")
	for i in parts.size():
		text("gamefont_small", "• " + parts[i], Vector2(x, y + (96 + i * 26) * u), "Left", TEXT)
	var _unused := w


func _on_click(id: String, at: Vector2) -> void:
	_error_msg = ""
	if id.begins_with("sp:"):
		_species_idx = int(id.substr(3))
		_face_page = 0
		_char_name = _random_name()
		_reload_species()
	elif id.begins_with("gender:"):
		_gender = id.substr(7)
		_face_page = 0
		_reload_species()
	elif id.begins_with("tab:"):
		_tab = int(id.substr(4))
	elif id == "cam_toggle":
		_camera_face = not _camera_face
	elif id.begins_with("page:"):
		_face_page = maxi(0, _face_page + int(id.substr(5)))
	elif id.begins_with("cpage:"):
		_color_page = maxi(0, _color_page + int(id.substr(6)))
	elif id == "clothes":
		_clothed = not _clothed
		_apply_clothes()
		_apply_to_stage()
	elif id.begins_with("cslider:"):
		_on_drag(id, at)
	elif id.begins_with("color:"):
		var parts := id.split(":")
		_colors[parts[1]] = int(parts[2])
		_apply_to_stage()
	elif id.begins_with("hair:"):
		if not _hairs.is_empty():
			var step := int(id.substr(5))
			var min_idx := -1 if _allow_bald else 0
			var span := _hairs.size() - min_idx
			_hair_idx = min_idx + posmod(_hair_idx - min_idx + step, span)
			_stage.set_hair(str(_hairs[_hair_idx]["gltf"]) if _hair_idx >= 0 else "", _colors)
			# This style's colours in place of the last one's.
			_color_vars = _color_vars.filter(func(cv: Dictionary) -> bool: return not bool(cv["hair"]))
			var seen := {}
			for cv: Dictionary in _color_vars:
				seen[str(cv["variable"])] = true
			if _hair_idx >= 0:
				_register_hair_colors(seen)
			_order_color_vars()
			_apply_to_stage()
	elif id.begins_with("disc:"):
		var parts := id.split(":")
		var vname := parts[1]
		var step := int(parts[2])
		_colors[vname] = maxi(0, int(_colors.get(vname, 0)) + step)
		_apply_to_stage()
	elif id == "rand_name":
		_char_name = _random_name()
	elif id == "rand_look":
		_randomize_look()
	elif id == "create":
		_submit()
	elif id == "back":
		visible = false
		closed.emit()


func _on_drag(id: String, at: Vector2) -> void:
	if not id.begins_with("cslider:"):
		return
	var area := Rect2()
	for r: Dictionary in _regions:
		if r["id"] == id:
			area = r["rect"]
	if area.size.x <= 0:
		return
	var cname := id.substr(8)
	_sliders[cname] = clampf((at.x - area.position.x - 6 * u) / (area.size.x - 12 * u), 0.0, 1.0)
	_apply_to_stage()


func _randomize_look() -> void:
	for k: String in _sliders:
		_sliders[k] = randf()
	for cv: Dictionary in _color_vars:
		var count := int(cv["count"])
		_colors[str(cv["variable"])] = randi() % mini(count, SWATCH_LIMIT)
	if not _hairs.is_empty():
		var min_idx := -1 if _allow_bald else 0
		_hair_idx = randi_range(min_idx, _hairs.size() - 1)
		_stage.set_hair(str(_hairs[_hair_idx]["gltf"]) if _hair_idx >= 0 else "", _colors)
	_apply_to_stage()


func _submit() -> void:
	var trimmed := _char_name.strip_edges()
	if trimmed.length() < 2:
		_error_msg = "Name must be at least 2 characters"
		return
	if _store == null:
		return
	var sp := _current_species()
	var compact_sliders := {}
	for k: String in _sliders:
		var v := float(_sliders[k])
		if absf(v - 0.5) > 0.005:
			compact_sliders[k] = snappedf(v, 0.001)
	var appearance := {
		"height": snappedf(float(_sliders.get("height", 0.5)), 0.001),
		"sliders": compact_sliders,
		"colors": _colors,
		"hair": str(_hairs[_hair_idx]["id"]) if _hair_idx >= 0 and _hair_idx < _hairs.size() else "",
	}
	var rec: Dictionary = _store.create_character(trimmed, str(sp["id"]), _gender,
			JSON.stringify(appearance), "Mos Eisley", Vector3(-3480.0, 5.0, -4794.0), 0.0)
	if rec.is_empty():
		_error_msg = _store.get_last_error()
		return
	visible = false
	character_created.emit(rec)


func _random_name() -> String:
	var sp_id := str(_current_species().get("id", "human"))
	var pool: Array = NAME_PREFIXES.get(sp_id, NAME_PREFIXES["human"])
	return str(pool[randi() % pool.size()]) + str(NAME_SUFFIXES[randi() % NAME_SUFFIXES.size()])


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		var id := _region_at(event.position)
		if event.pressed and id == "":
			_turntable_dragging = true
			accept_event()
			return
		elif not event.pressed and _turntable_dragging:
			_turntable_dragging = false
			accept_event()
			return
	elif event is InputEventMouseMotion and _turntable_dragging:
		if _stage != null:
			_stage.rotate_by(event.relative.x * 0.012)
		accept_event()
		return
	elif event is InputEventKey and event.pressed:
		var key := event as InputEventKey
		if key.physical_keycode == KEY_ESCAPE:
			visible = false
			closed.emit()
			accept_event()
			return
		elif key.physical_keycode == KEY_BACKSPACE:
			if _char_name.length() > 0:
				_char_name = _char_name.substr(0, _char_name.length() - 1)
				queue_redraw()
			accept_event()
			return
		elif key.unicode >= 32 and key.unicode < 127 and _char_name.length() < 20:
			var ch := char(key.unicode)
			if ch in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -'":
				_char_name += ch
				queue_redraw()
				accept_event()
				return
	super._gui_input(event)
