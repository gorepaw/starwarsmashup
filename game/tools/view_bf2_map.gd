# Screenshots of a Battlefront II map export (bf2tool export-map) from given
# viewpoints, without a match: terrain, objects, sky, fog, mist and water as
# the client draws them. For checking how a map looks.
#
#   godot --path game --script res://tools/view_bf2_map.gd -- <bake prefix> <out prefix> \
#       "x,y,z>x,y,z" ["x,y,z>x,y,z" ...] [--no-fog]
#
# Each view is a camera position and the point it looks at, in game
# coordinates (x = -Battlefront x). Writes <out prefix>01.png, 02.png, ...
# --no-fog turns the distance fog off (e.g. to see the map from above).
# A few seconds pass before each shot, so the water and mist move. Needs a
# real window.
extends SceneTree

const TerrainTool = preload("res://tools/render_terrain.gd")
const Bf2Map = preload("res://rendering/bf2_map.gd")

var _camera: Camera3D
var _views: Array = []  # [position, target]
var _out := ""
var _view := -1
var _wait := 0.0
var _sky_dome: Node3D


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 3:
		printerr("usage: view_bf2_map.gd <bake prefix> <out prefix> \"x,y,z>x,y,z\"...")
		quit(2)
		return
	var bake := "res://" + args[0]
	_out = args[1]
	for view: String in args.slice(2):
		if view.begins_with("--"):
			continue
		var ends := view.split(">")
		_views.append([_vector(ends[0]), _vector(ends[1])])
	var meta: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(bake + ".json"))
	if not Bf2Map.is_bf2(meta):
		printerr(bake, " isn't a Battlefront II map export")
		quit(1)
		return

	var heights := FileAccess.get_file_as_bytes(bake + ".r32").to_float32_array()
	var terrain := MeshInstance3D.new()
	terrain.mesh = TerrainTool.build_mesh(heights, meta["samples"], meta["spacing"],
			Vector2(meta["origin_x"], meta["origin_z"]), 1)
	terrain.material_override = Bf2Map.build_terrain_material(bake, meta)
	root.add_child(terrain)
	Bf2Map.add_objects(root, bake)

	var environment := Environment.new()
	var world := WorldEnvironment.new()
	world.environment = environment
	root.add_child(world)
	var sun := DirectionalLight3D.new()
	sun.shadow_enabled = true
	root.add_child(sun)
	_sky_dome = Bf2Map.setup_environment(root, bake, meta, environment, sun)
	if args.has("--no-fog"):
		environment.fog_enabled = false
	Bf2Map.add_water(root, bake, meta, sun)
	Bf2Map.add_fog_cloud(root, bake, meta)

	_camera = Camera3D.new()
	_camera.fov = 65
	_camera.near = 0.05
	_camera.far = 8000
	root.add_child(_camera)
	_next_view()


func _process(delta: float) -> bool:
	_wait -= delta
	if _wait > 0:
		return false
	root.get_texture().get_image().save_png("%s%02d.png" % [_out, _view + 1])
	print("wrote %s%02d.png" % [_out, _view + 1])
	_next_view()
	return false


func _next_view() -> void:
	_view += 1
	if _view >= _views.size():
		quit(0)
		return
	var from: Vector3 = _views[_view][0]
	var to: Vector3 = _views[_view][1]
	_camera.look_at_from_position(from, to)
	if _sky_dome != null:
		_sky_dome.position = from
	_wait = 3.0


static func _vector(text: String) -> Vector3:
	var parts := text.split(",")
	return Vector3(float(parts[0]), float(parts[1]), float(parts[2]))
