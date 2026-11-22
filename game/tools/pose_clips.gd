# Dev tool: screenshots a character's animation clips at a few moments, side
# by side from the side and the front, to check how they move (Galaxies'
# own clips, swgtool export-character --swg-anim, or retargeted ones).
#
#   godot --path game --script res://tools/pose_clips.gd -- <character.gltf> <output_prefix> [--varied] [clip...]
#
# Writes <output_prefix><clip>.png: four moments through the clip in a row
# (the side view), and the same under them from the front. All clips when
# none are named. In the character's own colors (its .colors.json, if any),
# or with --varied, each in colors varied as a hub's crowd's are. Needs a
# real window.
extends SceneTree

const MOMENTS := 4

var _out := ""
var _clips: PackedStringArray = []
var _index := -1
var _frames := 0
var _models: Array[Node3D] = []
var _players: Array[AnimationPlayer] = []


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2:
		printerr("usage: -- <character.gltf> <output_prefix> [clip...]")
		quit(2)
		return
	_out = args[1]
	var varied := args.has("--varied")
	args.erase("--varied")
	var colors := SwgColors.load_colors(args[0])
	DisplayServer.window_set_size(Vector2i(1280, 720))
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.35, 0.38, 0.42)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.6, 0.6, 0.6)
	var world := WorldEnvironment.new()
	world.environment = environment
	root.add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, 30, 0)
	root.add_child(sun)
	# Two rows of four: the side (turned 90 degrees) above, the front below.
	for row in 2:
		for i in MOMENTS:
			var document := GLTFDocument.new()
			var state := GLTFState.new()
			if document.append_from_file(args[0], state) != OK:
				printerr("cannot load ", args[0])
				quit(1)
				return
			var model: Node3D = document.generate_scene(state)
			model.position = Vector3((i - (MOMENTS - 1) / 2.0) * 1.6, 2.4 - row * 2.4, 0)
			model.rotation.y = PI / 2 if row == 0 else 0.0
			SwgColors.apply(model, colors, 1 + row * MOMENTS + i if varied else 0)
			root.add_child(model)
			_models.append(model)
			_players.append(model.find_child("AnimationPlayer", true, false) as AnimationPlayer)
	var camera := Camera3D.new()
	camera.position = Vector3(0, 1.4, 9.5)
	camera.fov = 50
	root.add_child(camera)
	_clips = args.slice(2) if args.size() > 2 else _players[0].get_animation_list()
	_next()


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames < 4:
		return false
	var path := "%s%s.png" % [_out, _clips[_index]]
	root.get_texture().get_image().save_png(path)
	print("wrote ", path)
	_next()
	return false


func _next() -> void:
	_index += 1
	_frames = 0
	if _index >= _clips.size():
		quit(0)
		return
	for i in _players.size():
		var player := _players[i]
		var clip := _clips[_index]
		if player == null or not player.has_animation(clip):
			continue
		player.play(clip)
		var length := player.get_animation(clip).length
		player.seek(length * float(i % MOMENTS) / MOMENTS, true)
		player.pause()
