# Dev tool: renders preview images of a glTF file from several angles.
#
#   godot --path game --script res://tools/render_preview.gd -- <model.gltf> <output_prefix> [animation] [samples]
#
# Without an animation: writes <output_prefix>front.png, side.png, back.png,
# and hand.png (a close-up of the "hold_r" joint, if the model has one).
# With an animation: poses the model at <samples> evenly spaced times (default
# 4) and writes <output_prefix><animation>_<front|side>_<i>.png.
# Needs a real window: rendering doesn't run in --headless mode.
extends SceneTree

const SETTLE_FRAMES := 4

var _camera: Camera3D
var _player: AnimationPlayer
var _animation := ""
var _views: Array = []  # [name, camera position, look target, animation time or -1]
var _view_index := -1
var _frames_in_view := 0
var _prefix := ""


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2:
		printerr("usage: -- <model.gltf> <output_prefix> [animation] [samples]")
		quit(2)
		return
	_prefix = args[1]

	var document := GLTFDocument.new()
	var state := GLTFState.new()
	var error := document.append_from_file(args[0], state)
	if error != OK:
		printerr("cannot load ", args[0], ": ", error_string(error))
		quit(1)
		return
	var model := document.generate_scene(state)
	root.add_child(model)
	_setup_lighting()

	var center := Vector3(0, 0.95, 0)
	var front := center + Vector3(0, 0.1, 3.6)
	var side := center + Vector3(3.6, 0.1, 0)
	if args.size() >= 3:
		_animation = args[2]
		_player = model.find_child("AnimationPlayer", true, false) as AnimationPlayer
		if _player == null or not _player.has_animation(_animation):
			printerr("no animation '", _animation, "'")
			quit(1)
			return
		var samples := int(args[3]) if args.size() >= 4 else 4
		var length := _player.get_animation(_animation).length
		for i in samples:
			var time := length * i / samples
			_views.append(["%s_front_%d" % [_animation, i], front, center, time])
			_views.append(["%s_side_%d" % [_animation, i], side, center, time])
	else:
		_views = [["front", front, center, -1.0], ["side", side, center, -1.0],
				["back", center + Vector3(0, 0.1, -3.6), center, -1.0]]
		var hand: Variant = _find_bone_position(model, "hold_r")
		if hand != null:
			_views.append(["hand", hand + Vector3(0.6, 0.15, 0.9), hand, -1.0])
	DisplayServer.window_set_size(Vector2i(720, 960))


func _process(_delta: float) -> bool:
	if _view_index >= 0:
		_frames_in_view += 1
		if _frames_in_view < SETTLE_FRAMES:
			return false
		var path: String = _prefix + _views[_view_index][0] + ".png"
		root.get_texture().get_image().save_png(path)
		print("wrote ", path)
	_view_index += 1
	if _view_index >= _views.size():
		quit(0)
		return true
	var next: Array = _views[_view_index]
	_camera.look_at_from_position(next[1], next[2])
	if next[3] >= 0.0:
		_player.play(_animation)
		_player.seek(next[3], true)
		_player.pause()
	_frames_in_view = 0
	return false


func _setup_lighting() -> void:
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.32, 0.34, 0.38)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.55, 0.55, 0.6)
	var world := WorldEnvironment.new()
	world.environment = environment
	root.add_child(world)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, 30, 0)
	sun.light_energy = 1.2
	root.add_child(sun)

	_camera = Camera3D.new()
	_camera.fov = 35
	root.add_child(_camera)


func _find_bone_position(node: Node, bone_name: String) -> Variant:
	if node is Skeleton3D:
		var bone: int = node.find_bone(bone_name)
		if bone >= 0:
			return node.get_bone_global_pose(bone).origin
	for child in node.get_children():
		var found: Variant = _find_bone_position(child, bone_name)
		if found != null:
			return found
	return null
