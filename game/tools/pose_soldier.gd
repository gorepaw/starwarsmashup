# Dev tool: screenshots a soldier (rendering/soldier.gd) in a series of
# states, to check its animation layers: aim pitch, firing while running or
# strafing, reloading, throwing, dying. A bolt marker shows where its muzzle is.
#
#   godot --path game --script res://tools/pose_soldier.gd -- <character.gltf> <output_prefix>
#
# Writes <output_prefix><state>.png for each state below. Needs a real window.
extends SceneTree

const Soldier = preload("res://rendering/soldier.gd")
const SETTLE := 0.9  # seconds each state runs before its shot

# name, velocity (x, z; the soldier faces -Z), pitch, action after 0.3 s
const STATES := [
	["idle", Vector2.ZERO, 0.0, ""],
	["fire_level", Vector2.ZERO, 0.0, "fire"],
	["fire_up", Vector2.ZERO, 0.6, "fire"],
	["fire_down", Vector2.ZERO, -0.5, "fire"],
	["run_fire", Vector2(0, -6), 0.0, "fire"],
	["strafe_right_fire", Vector2(5, 0), 0.0, "fire"],
	["strafe_left", Vector2(-5, 0), 0.0, ""],
	["run_back", Vector2(0, 5), 0.0, ""],
	["reload", Vector2.ZERO, 0.0, "reload"],
	["throw", Vector2.ZERO, 0.0, "throw"],
	["death", Vector2.ZERO, 0.0, "die"],
]

var _soldier: Soldier
var _marker: MeshInstance3D
var _camera: Camera3D
var _prefix := ""
var _index := -1
var _time := 0.0
var _acted := false
var _reloading := false
var _alive := true


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2:
		printerr("usage: -- <character.gltf> <output_prefix>")
		quit(2)
		return
	_prefix = args[1]
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(args[0], state) != OK:
		printerr("cannot load ", args[0])
		quit(1)
		return
	var model: Node3D = document.generate_scene(state)
	root.add_child(model)
	_soldier = Soldier.new(model)

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
	root.add_child(sun)
	_camera = Camera3D.new()
	_camera.fov = 40
	root.add_child(_camera)
	# The soldier faces -Z; seen from its right side, a little in front.
	_camera.look_at_from_position(Vector3(3.2, 1.4, -1.6), Vector3(0, 1.0, 0))

	_marker = MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = 0.04
	sphere.height = 0.08
	_marker.mesh = sphere
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color(1, 0.2, 0.1)
	material.no_depth_test = true
	_marker.material_override = material
	root.add_child(_marker)
	DisplayServer.window_set_size(Vector2i(640, 640))
	_next()


func _process(delta: float) -> bool:
	if _index >= STATES.size():
		return true
	var current: Array = STATES[_index]
	_time += delta
	if not _acted and _time > 0.3:
		_acted = true
		match current[3]:
			"fire": _soldier.fire()
			"throw": _soldier.throw()
			"reload": _reloading = true
			"die": _alive = false
	# Keep firing so the rifle stays up (as a held trigger does).
	if current[3] == "fire" and fmod(_time, 0.25) < delta:
		_soldier.fire()
	var velocity: Vector2 = current[1]
	_soldier.pose({"position": Vector3.ZERO, "velocity": Vector3(velocity.x, 0, velocity.y), "yaw": 0.0,
			"pitch": current[2], "alive": _alive, "reloading": _reloading}, delta)
	_marker.global_position = _soldier.muzzle()
	if _time >= SETTLE + (1.2 if current[3] in ["reload", "die"] else 0.0):
		var path: String = _prefix + str(current[0]) + ".png"
		root.get_texture().get_image().save_png(path)
		print("wrote ", path)
		_next()
	return false


func _next() -> void:
	_index += 1
	_time = 0.0
	_acted = false
	_reloading = false
	_alive = true
	if _index >= STATES.size():
		quit(0)
