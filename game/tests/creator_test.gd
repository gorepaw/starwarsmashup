# Headless checks for the character creator: a Twi'lek's every colour is
# offered (her lekku pattern's among them, named so), and the clothes toggle
# dresses the stage in a new character's plain clothes and takes them off
# (none for a species that can't wear them). Pass a path as the first user
# argument (or SWM_SHOTS) to save screenshots of it (<path>_1.png dressed,
# _2.png not).
extends SceneTree

const Bf2Art = preload("res://ui/bf2_art.gd")
const CharacterCreatorScreen = preload("res://ui/character_creator_screen.gd")
const CharacterStage = preload("res://rendering/character_stage.gd")

var _screen: CharacterCreatorScreen
var _stage: CharacterStage
var _shot := ""
var _frames := 0


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	_shot = args[0] if args.size() > 0 else OS.get_environment("SWM_SHOTS")
	DisplayServer.window_set_size(Vector2i(1280, 720))
	var art := Bf2Art.load_from("res://imported_assets/hud")
	assert(art != null, "missing hud art")
	var store: Object = ClassDB.instantiate("SwmCharacterStore")

	var world := Node3D.new()
	root.add_child(world)
	_stage = CharacterStage.new()
	world.add_child(_stage)
	var camera := Camera3D.new()
	camera.position = Vector3(0.0, 1.1, 3.4)
	world.add_child(camera)
	camera.look_at(Vector3(0.0, 0.95, 0.0))
	var sun := DirectionalLight3D.new()
	sun.rotation = Vector3(-0.7, 0.5, 0.0)
	world.add_child(sun)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.12, 0.14, 0.18)
	env.environment.ambient_light_color = Color(0.6, 0.6, 0.65)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	world.add_child(env)

	var layer := CanvasLayer.new()
	root.add_child(layer)
	_screen = CharacterCreatorScreen.new(art)
	_screen.set_anchors_preset(Control.PRESET_FULL_RECT)
	layer.add_child(_screen)
	_screen.open(store, _stage)

	# A Twi'lek, female.
	var species: Array = store.get_species_list()
	var twilek := -1
	for i in species.size():
		if str(species[i]["id"]) == "twilek":
			twilek = i
	assert(twilek >= 0, "no Twi'lek")
	_screen._on_click("sp:%d" % twilek, Vector2.ZERO)
	_screen._on_click("gender:female", Vector2.ZERO)
	var labels: Array = _screen._color_vars.map(func(cv: Dictionary) -> String: return str(cv["label"]))
	assert(labels.has("Lekku Pattern Color"), "colours offered: %s" % [labels])
	assert(labels.size() >= 5, "colours offered: %s" % [labels])
	assert(labels[0] == "Skin", "skin first: %s" % [labels])
	var pattern_names: Array = _screen._discrete_vars.map(func(d: Dictionary) -> String: return str(d["name"]))
	assert(pattern_names.has("Lekku Pattern"), "styles: %s" % [pattern_names])
	# Every colour is on a page of the Colors tab.
	_screen._on_click("tab:2", Vector2.ZERO)
	_screen.queue_redraw()

	# Dressed by default; the toggle undresses.
	assert(_screen._clothed and _worn_meshes() >= 2, "not dressed: %d worn meshes" % _worn_meshes())
	print("creator colours: OK (%s)" % ", ".join(labels))


func _worn_meshes() -> int:
	if _stage.worn_node == null or not is_instance_valid(_stage.worn_node) or _stage.worn_node.is_queued_for_deletion():
		return 0
	return _stage.worn_node.get_child_count()


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames == 6:
		_save(1)
		_screen._on_click("clothes", Vector2.ZERO)
	elif _frames == 8:
		assert(not _screen._clothed and _worn_meshes() == 0, "still dressed")
		_save(2)
		# A Wookiee can't wear them: the toggle says so.
		var species: Array = (ClassDB.instantiate("SwmCharacterStore") as Object).get_species_list()
		for i in species.size():
			if str(species[i]["id"]) == "wookiee":
				_screen._on_click("sp:%d" % i, Vector2.ZERO)
		assert(_screen._wearable_clothes().is_empty(), "a Wookiee in a tunic")
		print("creator clothes: OK")
		quit(0)
		return true
	return false


func _save(n: int) -> void:
	if _shot != "":
		var path := "%s_%d.png" % [_shot, n]
		root.get_texture().get_image().save_png(path)
		print("wrote ", path)
