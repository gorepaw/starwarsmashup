# Dev tool: renders a baked terrain region (swgtool bake-terrain) as a mesh at
# its real world coordinates, optionally with a character standing on it.
#
#   godot --path game --script res://tools/render_terrain.gd -- <bake_prefix> <output.png> [character.gltf] [step]
#
# <bake_prefix> names the .r32/.json pair. [step] samples every Nth height
# (default 2). SWG is left-handed, so world X is mirrored (Godot x = -SWG x).
# Needs a real window: rendering doesn't run in --headless mode.
extends SceneTree

const SETTLE_FRAMES := 6

var _frames := 0
var _output := ""


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2:
		printerr("usage: -- <bake_prefix> <output.png> [character.gltf] [step]")
		quit(2)
		return
	_output = args[1]
	var step := int(args[3]) if args.size() >= 4 else 2

	var meta: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(args[0] + ".json"))
	var heights := FileAccess.get_file_as_bytes(args[0] + ".r32").to_float32_array()
	var samples: int = meta["samples"]
	var spacing: float = meta["spacing"]
	var origin := Vector2(meta["origin_x"], meta["origin_z"])
	if heights.size() != samples * samples:
		printerr("height data doesn't match the metadata")
		quit(1)
		return

	var terrain := MeshInstance3D.new()
	terrain.mesh = _build_mesh(heights, samples, spacing, origin, step)
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.78, 0.66, 0.47)
	material.roughness = 0.95
	terrain.material_override = material
	root.add_child(terrain)

	# Stand at the center of the region.
	var center_column := samples / 2
	var center := Vector3(-(origin.x + center_column * spacing), heights[center_column * samples + center_column],
			origin.y + center_column * spacing)
	if args.size() >= 3 and args[2] != "-":
		var document := GLTFDocument.new()
		var state := GLTFState.new()
		if document.append_from_file(args[2], state) == OK:
			var character := document.generate_scene(state)
			character.position = center
			character.scale = Vector3.ONE * 12.0  # exaggerated so it reads at this distance
			root.add_child(character)

	_setup_scene(center)
	DisplayServer.window_set_size(Vector2i(1280, 720))


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames == SETTLE_FRAMES:
		root.get_texture().get_image().save_png(_output)
		print("wrote ", _output)
		quit(0)
		return true
	return false


func _build_mesh(heights: PackedFloat32Array, samples: int, spacing: float, origin: Vector2, step: int) -> ArrayMesh:
	var count := (samples - 1) / step + 1
	var vertices := PackedVector3Array()
	vertices.resize(count * count)
	for row in count:
		for column in count:
			var h := heights[row * step * samples + column * step]
			vertices[row * count + column] = Vector3(-(origin.x + column * step * spacing), h,
					origin.y + row * step * spacing)
	var indices := PackedInt32Array()
	for row in count - 1:
		for column in count - 1:
			var a := row * count + column
			var b := a + 1
			var c := a + count
			var d := c + 1
			# Mirroring X flips handedness, so wind triangles to face up
			# (verified by rendering: the other order shows the underside).
			indices.append_array([a, c, b, b, c, d])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_INDEX] = indices
	var tool := SurfaceTool.new()
	tool.create_from_arrays(arrays)
	tool.generate_normals()
	return tool.commit()


func _setup_scene(center: Vector3) -> void:
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.62, 0.72, 0.85)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.5, 0.5, 0.55)
	environment.fog_enabled = true
	environment.fog_light_color = Color(0.75, 0.72, 0.68)
	environment.fog_density = 0.0002
	var world := WorldEnvironment.new()
	world.environment = environment
	root.add_child(world)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-35, -40, 0)
	sun.light_energy = 1.3
	sun.shadow_enabled = true
	root.add_child(sun)

	var camera := Camera3D.new()
	camera.fov = 55
	camera.far = 20000
	root.add_child(camera)
	camera.look_at_from_position(center + Vector3(260, 160, 420), center + Vector3(0, 10, 0))
