# Dev tool: contact sheets of every catalog weapon in a soldier's hands, one
# image per category (docs/WEAPONS.md), to catch a model held backwards or
# sideways, or a muzzle in the wrong place, before it reaches the game. A red
# ball marks each gun's muzzle (where its bolts leave); the item id is
# written under each soldier.
#
#   godot --path game --script res://tools/weapon_sheet.gd -- <output_prefix> [category | item id]...
#
# Writes <output_prefix><category>[_<page>].png, six weapons a page. The
# categories: sniper, rifle, carbine, heavy, sidearm, melee, tactical (all
# by default). Item ids instead (wp_...) put just those on one sheet,
# <output_prefix>items.png, in the order given. Needs a real window.
extends SceneTree

const PlayerAppearance = preload("res://rendering/player_appearance.gd")
const PER_PAGE := 6
const CATEGORY_OF := {"sniper": "sniper", "rifle_auto": "rifle", "carbine_burst": "carbine", "scatter": "carbine",
		"heavy_launcher": "heavy", "pistol": "sidearm", "melee": "melee", "grenade": "tactical", "beam": "rifle",
		"beam_cannon": "heavy", "flamer": "heavy", "scatter_pistol": "sidearm", "melee_2h": "melee",
		"placed": "tactical", "utility": "tactical"}
const PLAIN := "clothing/shirt_tunic_m.gltf,clothing/pants_cargo_m.gltf,clothing/boots_leather_m.gltf"

var _pages: Array = []  # [name, [item dicts]]
var _page := -1
var _frames := 0
var _cast: Array = []  # [character, position, marker, label]
var _camera: Camera3D
var _prefix := ""


func _initialize() -> void:
	DisplayServer.window_set_size(Vector2i(1800, 720))
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		printerr("usage: weapon_sheet.gd -- <output_prefix> [category]...")
		quit(2)
		return
	_prefix = args[0]
	var wanted: Array = Array(args.slice(1))
	var store: Object = ClassDB.instantiate("SwbfoCharacterStore")
	if store.load_catalog(ProjectSettings.globalize_path("res://items/swg_catalog.ini"),
			ProjectSettings.globalize_path("res://imported_assets/items")) != OK:
		printerr("catalog: ", store.get_last_error())
		quit(1)
		return
	var by_category := {}
	var picked: Array = wanted.filter(func(a: String) -> bool: return a.begins_with("wp_"))
	if not picked.is_empty():
		var by_id := {}
		for def: Dictionary in store.get_catalog():
			by_id[str(def.get("id", ""))] = def
		_pages.append(["items", picked.filter(func(i: String) -> bool: return by_id.has(i)).map(
				func(i: String) -> Dictionary: return by_id[i])])
		wanted = []
	for def: Dictionary in store.get_catalog() if picked.is_empty() else []:
		if str(def.get("kind", "")) != "weapon":
			continue
		var category: String = CATEGORY_OF.get(str(def.get("archetype", "")), "other")
		if not wanted.is_empty() and not wanted.has(category):
			continue
		if not by_category.has(category):
			by_category[category] = []
		by_category[category].append(def)
	for category: String in ["sniper", "rifle", "carbine", "heavy", "sidearm", "melee", "tactical", "other"]:
		var defs: Array = by_category.get(category, [])
		var pages := ceili(defs.size() / float(PER_PAGE))
		for p in pages:
			var name := category if pages == 1 else "%s_%d" % [category, p + 1]
			_pages.append([name, defs.slice(p * PER_PAGE, (p + 1) * PER_PAGE)])

	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.32, 0.34, 0.38)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.65, 0.65, 0.7)
	var world := WorldEnvironment.new()
	world.environment = environment
	root.add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-35, 25, 0)
	root.add_child(sun)
	_camera = Camera3D.new()
	_camera.fov = 34
	root.add_child(_camera)
	_camera.look_at_from_position(Vector3(0, 1.4, -6.2), Vector3(0, 1.0, 0))
	_next_page()


func _next_page() -> void:
	for entry: Array in _cast:
		(entry[0]["model"] as Node3D).queue_free()
		(entry[2] as Node3D).queue_free()
		(entry[3] as Node3D).queue_free()
	_cast.clear()
	_page += 1
	_frames = 0
	if _page >= _pages.size():
		quit(0)
		return
	var defs: Array = _pages[_page][1]
	for i in defs.size():
		var def: Dictionary = defs[i]
		var character := PlayerAppearance.load_character("human", "male", '{"height":0.5}')
		root.add_child(character["model"])
		PlayerAppearance.set_worn_models(character, PLAIN)
		var soldier = character["soldier"]
		soldier.set_weapon(str(def.get("model", "")))
		soldier.set_anim_set(str(def.get("anim_set", "rifle")))
		if soldier._weapon == null:
			printerr("weapon not held: ", def.get("id", ""), " (", def.get("model", ""), ")")
		var position := Vector3(1.5 * ((defs.size() - 1) * 0.5 - i), 0, 0)
		var marker := MeshInstance3D.new()
		var ball := SphereMesh.new()
		ball.radius = 0.035
		ball.height = 0.07
		var red := StandardMaterial3D.new()
		red.albedo_color = Color(1, 0.1, 0.1)
		red.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		ball.material = red
		marker.mesh = ball
		marker.visible = not str(def.get("archetype", "")) in ["melee", "melee_2h", "grenade", "placed"]
		root.add_child(marker)
		var label := Label3D.new()
		label.text = str(def.get("id", "")).trim_prefix("wp_")
		label.font_size = 40
		label.pixel_size = 0.004
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.position = position + Vector3(0, -0.12, 0)
		root.add_child(label)
		_cast.append([character, position, marker, label])


func _process(delta: float) -> bool:
	if _page >= _pages.size():
		return true
	_frames += 1
	for entry: Array in _cast:
		var soldier = entry[0]["soldier"]
		soldier.pose({"position": entry[1], "velocity": Vector3.ZERO, "yaw": 0.9, "pitch": 0.0, "alive": true,
				"reloading": false}, delta)
		(entry[2] as Node3D).global_position = soldier.muzzle()
	if _frames == 60:
		var path: String = _prefix + str(_pages[_page][0]) + ".png"
		root.get_texture().get_image().save_png(path)
		print("wrote ", path)
		_next_page()
	return false
