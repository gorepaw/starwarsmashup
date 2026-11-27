# Dev tool: contact sheets of the catalog's armor and clothing, worn, one
# image per set (docs/ARMOR.md), to catch a piece that doesn't fit the body,
# floats, or is missing for a gender before it reaches the game. Each set is
# worn whole six times: a human man and woman from the front and the back, a
# Wookiee and a Trandoshan (who don't show some slots: check they're hidden).
# The bodysuits are their own sheets, each on a human; the clothing another;
# and "species", a Wookiee and an Ithorian in a light, a medium and a heavy
# set, to check their own sets show in its place; and "paint", the Katarn in
# each of its paint schemes, front and back. Figures are dressed the way
# the world dresses them (core resolve_worn_models).
#
#   godot --path game --script res://tools/armor_sheet.gd -- <output_prefix> [set | item id]...
#
# Writes <output_prefix><set>.png. A set is its models' folder (ubese,
# stormtrooper ...), or "bodysuits", "clothing", "species" or "paint"; all of them by default. Item
# ids instead put just those, worn together, on one sheet,
# <output_prefix>items.png. Needs a real window.
extends SceneTree

const PlayerAppearance = preload("res://rendering/player_appearance.gd")
const PER_PAGE := 6
# Who wears a set: species, gender, facing away.
const WEARERS := [["human", "male", false], ["human", "female", false], ["human", "male", true],
		["human", "female", true], ["wookiee", "male", false], ["trandoshan", "male", false]]

var _pages: Array = []  # [name, [figure: {species, gender, back, worn: [def], label}]]
var _page := -1
var _frames := 0
var _cast: Array = []  # [character, position, yaw, label]
var _camera: Camera3D
var _prefix := ""
var _store: Object
# The species page: who wears which set (a light, a medium and a heavy one).
const SPECIES_PAGE := [["wookiee", "ubese", "light"], ["wookiee", "stormtrooper", "medium"],
		["wookiee", "composite", "heavy"], ["ithorian", "ubese", "light"], ["ithorian", "stormtrooper", "medium"],
		["ithorian", "composite", "heavy"]]


func _initialize() -> void:
	DisplayServer.window_set_size(Vector2i(1800, 720))
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		printerr("usage: armor_sheet.gd -- <output_prefix> [set | item id]...")
		quit(2)
		return
	_prefix = args[0]
	var wanted: Array = Array(args.slice(1))
	var store: Object = ClassDB.instantiate("SwmCharacterStore")
	_store = store
	if store.load_catalog(ProjectSettings.globalize_path("res://items/swg_catalog.ini"),
			ProjectSettings.globalize_path("res://imported_assets/items")) != OK:
		printerr("catalog: ", store.get_last_error())
		quit(1)
		return

	var sets := {}  # name -> [def]
	var order: Array = []
	var by_id := {}
	for def: Dictionary in store.get_catalog():
		by_id[str(def.get("id", ""))] = def
		var kind := str(def.get("kind", ""))
		if kind != "armor" and kind != "clothing" and kind != "bodysuit":
			continue
		var set_name := "clothing" if kind == "clothing" else (
				"bodysuits" if kind == "bodysuit" else
				str(def.get("model", "")).split(",")[0].get_base_dir().get_file())
		if not sets.has(set_name):
			sets[set_name] = []
			order.append(set_name)
		sets[set_name].append(def)

	var picked: Array = wanted.filter(func(a: String) -> bool: return by_id.has(a))
	if not picked.is_empty():
		_pages.append(["items", _wearers(picked.map(func(i: String) -> Dictionary: return by_id[i]))])
		order = []
	for set_name: String in order:
		if not wanted.is_empty() and not wanted.has(set_name):
			continue
		var defs: Array = sets[set_name]
		if set_name == "bodysuits":
			for p in ceili(defs.size() / float(PER_PAGE)):
				var figures: Array = []
				for def: Dictionary in defs.slice(p * PER_PAGE, (p + 1) * PER_PAGE):
					figures.append({"species": "human", "gender": "male", "back": false, "worn": [def],
							"label": str(def.get("id", "")).trim_prefix("suit_")})
				_pages.append(["bodysuits_%d" % (p + 1), figures])
		else:
			_pages.append([set_name, _wearers(defs)])
	if (wanted.is_empty() or wanted.has("species")) and picked.is_empty():
		var figures: Array = []
		for w: Array in SPECIES_PAGE:
			figures.append({"species": w[0], "gender": "male", "back": false, "worn": sets.get(w[1], []),
					"label": "%s %s" % [w[0], w[2]]})
		_pages.append(["species", figures])
	if (wanted.is_empty() or wanted.has("paint")) and picked.is_empty() and sets.has("katarn"):
		var figures: Array = []
		for scheme: String in ["neutral", "empire", "rebellion"]:
			for back: bool in [false, true]:
				figures.append({"species": "human", "gender": "male", "back": back, "worn": sets["katarn"],
						"paint": scheme, "label": scheme + (" back" if back else "")})
		_pages.append(["paint", figures])

	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.32, 0.34, 0.38)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.65, 0.65, 0.7)
	var world := WorldEnvironment.new()
	world.environment = environment
	root.add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-35, 160, 0)
	root.add_child(sun)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-20, -20, 0)
	fill.light_energy = 0.5
	root.add_child(fill)
	_camera = Camera3D.new()
	_camera.fov = 34
	root.add_child(_camera)
	_camera.look_at_from_position(Vector3(0, 1.2, -6.6), Vector3(0, 0.95, 0))
	_next_page()


# A set worn by each of WEARERS. Clothing pieces share slots (a shirt and a
# jacket are both chest), so they're worn one each.
func _wearers(defs: Array) -> Array:
	var figures: Array = []
	for w: Array in WEARERS:
		figures.append({"species": w[0], "gender": w[1], "back": w[2], "worn": defs,
				"label": (w[1] if w[0] == "human" else w[0]) + (" back" if w[2] else "")})
	return figures


func _next_page() -> void:
	for entry: Array in _cast:
		(entry[0]["model"] as Node3D).queue_free()
		(entry[3] as Node3D).queue_free()
	_cast.clear()
	_page += 1
	_frames = 0
	if _page >= _pages.size():
		quit(0)
		return
	var figures: Array = _pages[_page][1]
	for i in figures.size():
		var fig: Dictionary = figures[i]
		# A loadout of the set: worn in the gear, a bodysuit in the override
		# (the last of a slot's: clothing shares them).
		var items: Array = []
		var gear := {}
		var looks := {}
		for def: Dictionary in fig["worn"]:
			items.append({"id": items.size() + 1, "def_id": str(def.get("id", "")),
					"color_json": '{"paint":"%s"}' % fig["paint"] if fig.has("paint") else "{}"})
			var slot := str(def.get("slot", ""))
			if str(def.get("kind", "")) == "bodysuit":
				looks[slot] = items.size()
			else:
				gear[slot] = items.size()
		var worn := str(_store.worn_models(fig["species"], fig["gender"],
				{"gear_ids": gear, "appearance_ids": looks}, items))
		var override_rel := worn.trim_prefix("override:") if worn.begins_with("override:") else ""
		var character := PlayerAppearance.load_character(fig["species"], fig["gender"], '{"height":0.5}', "",
				override_rel)
		root.add_child(character["model"])
		if override_rel == "":
			PlayerAppearance.set_worn_models(character, worn)
		var position := Vector3(1.4 * ((figures.size() - 1) * 0.5 - i), 0, 0)
		var label := Label3D.new()
		label.text = fig["label"]
		label.font_size = 36
		label.pixel_size = 0.004
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.position = position + Vector3(0, -0.12, 0)
		root.add_child(label)
		_cast.append([character, position, PI if fig["back"] else 0.0, label])
	print("%s: %s" % [_pages[_page][0], ", ".join((figures[0]["worn"] as Array).map(
			func(d: Dictionary) -> String: return str(d.get("id", ""))))])


func _process(delta: float) -> bool:
	if _page >= _pages.size():
		return true
	_frames += 1
	for entry: Array in _cast:
		var soldier = entry[0]["soldier"]
		soldier.pose({"position": entry[1], "velocity": Vector3.ZERO, "yaw": entry[2], "pitch": 0.0, "alive": true,
				"reloading": false}, delta)
	if _frames == 60:
		var path: String = _prefix + str(_pages[_page][0]) + ".png"
		root.get_texture().get_image().save_png(path)
		print("wrote ", path)
		_next_page()
	return false
