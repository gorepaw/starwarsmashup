# Draws a Battlefront II map exported with bf2tool export-map: its terrain's
# texture layers, its objects' models, its sky dome, fog, mist, water and
# lights. `prefix`
# is the export's bake prefix (e.g. imported_assets/maps/dag1/dag1); models,
# textures and maps are relative to its folder.
extends RefCounted

const TERRAIN_SHADER = preload("res://rendering/bf2_terrain.gdshader")
const SUNLIT_TERRAIN_SHADER = preload("res://rendering/bf2_terrain_sunlit.gdshader")
const WATER_SHADER = preload("res://rendering/bf2_water.gdshader")
const FOG_CLOUD_SHADER = preload("res://rendering/bf2_fog_cloud.gdshader")
const MAX_LAYERS := 8
const LAYER_SIZE := 512
# Battlefront draws its sky's models (a few meters across, or hundreds:
# Mygeeto's) around the camera at infinity, each over the one before. Here
# the dome is SKY_DOME_RADIUS away and each later model a little nearer,
# every one scaled to its own size: about the camera, that changes nothing
# of how it looks.
const SKY_DOME_RADIUS := 3000.0  # m, within the camera's far plane
const SKY_LAYER_STEP := 0.04     # each later model this much nearer
const SKY_FIRST_PRIORITY := -120  # drawn before the map's other blended surfaces


const POWERUPS := &"bf2_powerups"  # the health and ammo droids' nodes (meta "powerup": export-map's)
const DROID_TURN_TIME := 1.2       # s the health droid turns before it waits (ASSUMPTION)
const DROID_TURN_RANGE := 1.2      # radians either way of where it was placed it looks
const GONK_ROCK_RATE := 1.6        # rocks a second, the ammo droid shifting from foot to foot


static func is_bf2(meta: Dictionary) -> bool:
	return meta.has("terrain_layers")


static func build_terrain_material(prefix: String, meta: Dictionary) -> Material:
	var dir := _dir(prefix)
	var images: Array[Image] = []
	var scales := PackedFloat32Array()
	for layer: Dictionary in meta["terrain_layers"]:
		if images.size() == MAX_LAYERS:
			printerr("terrain: only the first ", MAX_LAYERS, " texture layers are drawn")
			break
		var image: Image = null
		if layer["texture"] != "":
			image = Image.load_from_file(dir.path_join(layer["texture"]))
		if image == null:
			image = Image.create(4, 4, false, Image.FORMAT_RGBA8)
			image.fill(Color(0.35, 0.33, 0.25))
		image.convert(Image.FORMAT_RGBA8)
		image.resize(LAYER_SIZE, LAYER_SIZE)
		image.generate_mipmaps()
		images.append(image)
		scales.append(maxf(float(layer["scale"]), 0.01))
	scales.resize(MAX_LAYERS)
	var textures := Texture2DArray.new()
	textures.create_from_images(images)

	var material := ShaderMaterial.new()
	# A sun that isn't baked into the lighting lights the terrain as it's drawn.
	var sun: Dictionary = meta.get("environment", {}).get("sun", {})
	material.shader = SUNLIT_TERRAIN_SHADER if not sun.is_empty() and not sun.get("baked", true) \
			else TERRAIN_SHADER
	material.set_shader_parameter("layers", textures)
	material.set_shader_parameter("layer_scale", scales)
	material.set_shader_parameter("layer_count", images.size())
	var splats: Array = meta["splat_maps"]
	for i in 2:
		if i < splats.size():
			material.set_shader_parameter("splat%d" % i, _texture(dir.path_join(splats[i])))
	material.set_shader_parameter("light_map", _texture(dir.path_join(meta["light_map"])))
	material.set_shader_parameter("map_origin", Vector2(meta["origin_x"], meta["origin_z"]))
	material.set_shader_parameter("map_spacing", float(meta["spacing"]))
	material.set_shader_parameter("map_samples", int(meta["samples"]))
	var cuts := _cuts_texture(prefix)
	if cuts != null:
		material.set_shader_parameter("cuts", cuts)
		material.set_shader_parameter("cut_count", cuts.get_height())
	return material


# The terrain's cuts (<prefix>.cuts, bf2tool export-map: a line per cut, its
# bounding sphere's center x y z and radius, then its planes' nx ny nz d, game
# space) as a float texture for the shader: a row per cut, its first texel
# (center x, center z, radius, plane count), then a texel per plane. Null
# without cuts.
static func _cuts_texture(prefix: String) -> ImageTexture:
	var file := FileAccess.open(prefix + ".cuts", FileAccess.READ)
	if file == null:
		return null
	var rows: Array[PackedFloat32Array] = []
	var width := 1
	while not file.eof_reached():
		var line := file.get_line().strip_edges()
		if line == "" or line.begins_with("#"):
			continue
		var numbers := PackedFloat32Array()
		for field in line.split(" ", false):
			numbers.append(float(field))
		var planes := (numbers.size() - 4) / 4
		var row := PackedFloat32Array([numbers[0], numbers[2], numbers[3] + 0.5, planes])
		row.append_array(numbers.slice(4, 4 + planes * 4))
		rows.append(row)
		width = maxi(width, 1 + planes)
	if rows.is_empty():
		return null
	var data := PackedFloat32Array()
	for row in rows:
		row.resize(width * 4)
		data.append_array(row)
	var image := Image.create_from_data(width, rows.size(), false, Image.FORMAT_RGBAF, data.to_byte_array())
	return ImageTexture.create_from_image(image)


# Places every object that has a model; returns how many.
static func add_objects(parent: Node, prefix: String) -> int:
	var path := prefix + ".objects.json"
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not parsed is Dictionary:
		printerr("cannot read ", path)
		return 0
	var dir := _dir(prefix)
	var models := {}  # file -> loaded scene (null if it failed)
	var count := 0
	for object: Dictionary in parsed["objects"]:
		var model: String = object.get("model", "")
		if model == "":
			continue
		if not models.has(model):
			models[model] = _load_model(dir.path_join(model))
		if models[model] == null:
			continue
		var node := (models[model] as Node3D).duplicate() as Node3D
		var b: Array = object["basis"]
		var p: Array = object["position"]
		node.basis = Basis(Vector3(b[0], b[3], b[6]), Vector3(b[1], b[4], b[7]), Vector3(b[2], b[5], b[8]))
		node.position = Vector3(p[0], p[1], p[2])
		if object.has("powerup"):
			node.set_meta("powerup", object["powerup"])
			node.set_meta("placed", node.basis)
			node.add_to_group(POWERUPS)
		parent.add_child(node)
		count += 1
	for template: Variant in models.values():
		if template != null:
			(template as Node).free()
	print("placed ", count, " objects (", models.size(), " models) from ", path.get_file())
	return count


# Fog, ambient light, the sun, local lights and the sky dome. Returns the
# dome, which should follow the camera, or null.
# The health and ammo droids idling, as their classes describe: the health
# droid turns (IdleRotateSpeed, radians a second) to look somewhere else, then
# waits (IdleWaitTime); the GONK rocks from foot to foot (IdleWobbleFactor).
# Their models come as one piece, so the whole droid moves.
static func animate_powerups(tree: SceneTree, time: float) -> void:
	for node: Node3D in tree.get_nodes_in_group(POWERUPS):
		var spec: Dictionary = node.get_meta("powerup")
		var placed: Basis = node.get_meta("placed")
		var seed := float(node.get_instance_id() % 997)
		var wobble := float(spec.get("wobble", 0))
		var speed := float(spec.get("idle_rotate_speed", 0))
		if wobble > 0:
			node.basis = placed * Basis(Vector3.BACK, wobble * sin((time + seed) * GONK_ROCK_RATE * TAU))
		elif speed > 0:
			# Turn after turn, each to a new heading, then a wait.
			var period := DROID_TURN_TIME + float(spec.get("idle_wait_time", 0))
			var cycle := floorf((time + seed) / period)
			var into := fmod(time + seed, period)
			var from := DROID_TURN_RANGE * sin(cycle * 1.7 + seed)
			var to := DROID_TURN_RANGE * sin((cycle + 1.0) * 1.7 + seed)
			var turned := clampf(into * speed / maxf(absf(to - from), 1e-3), 0.0, 1.0)
			node.basis = Basis(Vector3.UP, lerpf(from, to, smoothstep(0.0, 1.0, turned))) * placed


static func setup_environment(parent: Node, prefix: String, meta: Dictionary, environment: Environment,
		sun: DirectionalLight3D) -> Node3D:
	var env: Dictionary = meta.get("environment", {})
	if env.is_empty():
		return null
	var fog := _color(env["fog_color"])
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = fog
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = _color(env["ambient_top"]).lerp(_color(env["ambient_bottom"]), 0.5)
	environment.ambient_light_energy = 1.0
	# ASSUMPTION: the near scene range's last two values (60 and 80 m on
	# Dagobah) are where the view fades out. A sky with no fog color has no
	# fog (Utapau's: the far side of its pit stays in view).
	var ranges: Array = env.get("near_scene_range", [])
	environment.fog_enabled = env.get("fog", true)
	environment.fog_mode = Environment.FOG_MODE_DEPTH
	environment.fog_light_color = fog
	environment.fog_density = 1.0
	environment.fog_sun_scatter = 0.0
	environment.fog_depth_begin = float(ranges[0]) if ranges.size() >= 4 else 20.0
	environment.fog_depth_end = float(ranges[3]) if ranges.size() >= 4 else 200.0
	environment.fog_depth_curve = 1.5

	if env.has("sun"):
		var direction := _vector(env["sun"]["direction"])
		sun.basis = Basis.looking_at(direction, Vector3.UP if absf(direction.y) < 0.99 else Vector3.FORWARD)
		sun.light_color = _color(env["sun"]["color"])
		sun.light_energy = 1.5
	# The second global light, on soldiers and the like (the map's own models
	# carry their baked light): Coruscant's temple has no ambient, and out of
	# the sun its soldiers are lit by this one alone. No shadows, as in
	# Battlefront.
	if env.has("back"):
		var back := DirectionalLight3D.new()
		var along := _vector(env["back"]["direction"])
		back.basis = Basis.looking_at(along, Vector3.UP if absf(along.y) < 0.99 else Vector3.FORWARD)
		back.light_color = _color(env["back"]["color"])
		back.light_energy = 1.5
		back.shadow_enabled = false
		parent.add_child(back)
	for light: Dictionary in env.get("lights", []):
		var omni := OmniLight3D.new()
		omni.position = _vector(light["position"])
		omni.light_color = _color(light["color"])
		omni.omni_range = float(light["range"])
		parent.add_child(omni)

	# The dome and the models drawn with it (Hoth's mountains, Endor's
	# Death Star, Kashyyyk's clouds); older exports name only the dome.
	var models: Array = env.get("sky_domes", [])
	if models.is_empty() and env.get("sky_dome", "") != "":
		models = [{"model": env["sky_dome"]}]
	var sky := Node3D.new()
	sky.name = "Sky"
	var layer := 0
	for entry: Dictionary in models:
		var path: String = entry.get("model", "")
		var model := _load_model(_dir(prefix).path_join(path)) if path != "" else null
		if model == null:
			continue
		var reach := 0.0
		for mesh: MeshInstance3D in model.find_children("*", "MeshInstance3D", true, false):
			mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			var box := mesh.transform * mesh.mesh.get_aabb()
			for corner in 8:
				reach = maxf(reach, box.get_endpoint(corner).length())
			for surface in mesh.mesh.get_surface_count():
				var material := mesh.get_active_material(surface) as BaseMaterial3D
				if material != null:
					material = material.duplicate() as BaseMaterial3D
					material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
					material.disable_fog = true
					material.cull_mode = BaseMaterial3D.CULL_DISABLED
					# Each over the ones before, in order, as Battlefront
					# layers them (Yavin's clouds over its gas giant over
					# the dome), whatever their sizes; none hiding the
					# next, and the map in front of them all.
					material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
					material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
					material.render_priority = SKY_FIRST_PRIORITY + layer
					mesh.set_surface_override_material(surface, material)
		if reach <= 0.0:
			continue
		# With no fog, under the dome's lower edge (Utapau's, 8 degrees up)
		# is its horizon's color, not black.
		if layer == 0 and not environment.fog_enabled:
			var horizon := _horizon_color(model)
			if horizon.a > 0.0:
				environment.background_color = Color(horizon, 1.0)
		model.scale = Vector3.ONE * (SKY_DOME_RADIUS * (1.0 - SKY_LAYER_STEP * layer) / reach)
		sky.add_child(model)
		layer += 1
		# Turning (drifting clouds): ASSUMPTION, RotationSpeed in turns a
		# second (Kashyyyk's 0.004: a turn in some four minutes).
		var speed := float(entry.get("rotation_speed", 0.0))
		if speed > 0.0:
			var turn := model.create_tween().set_loops()
			turn.tween_property(model, "rotation:y", TAU, 1.0 / speed).from(0.0)
	if sky.get_child_count() == 0:
		sky.free()
		return null
	parent.add_child(sky)
	return sky


# The map's water: one mesh of flat rectangles per surface, drawn with the
# map's water look. Returns how many surfaces.
static func add_water(parent: Node, prefix: String, meta: Dictionary, sun: DirectionalLight3D) -> int:
	var water: Dictionary = meta.get("water", {})
	if water.is_empty():
		return 0
	var dir := _dir(prefix)
	var material := ShaderMaterial.new()
	material.shader = WATER_SHADER
	if water.get("texture", "") != "":
		material.set_shader_parameter("main_texture", _texture(dir.path_join(water["texture"])))
	var frames: Array[Image] = []
	for file: String in water.get("normal_maps", []):
		var image := Image.load_from_file(dir.path_join(file)) if file != "" else null
		if image == null:
			continue
		image.convert(Image.FORMAT_RGBA8)
		if not frames.is_empty() and image.get_size() != frames[0].get_size():
			image.resize(frames[0].get_width(), frames[0].get_height())
		image.generate_mipmaps()
		frames.append(image)
	if not frames.is_empty():
		var normals := Texture2DArray.new()
		normals.create_from_images(frames)
		material.set_shader_parameter("normal_maps", normals)
		material.set_shader_parameter("normal_frames", frames.size())
		material.set_shader_parameter("normal_rate", float(water.get("normal_map_rate", 8.0)))
	# ASSUMPTION: Tile is how many times the texture repeats across a water
	# cell (Dagobah: twice over 16 m).
	var tile: Array = water.get("tile", [])
	var cell := float(water.get("cell_size", 16.0))
	if tile.size() >= 1 and float(tile[0]) > 0:
		material.set_shader_parameter("repeat_meters", cell / float(tile[0]))
	var velocity: Array = water.get("velocity", [])
	if velocity.size() >= 2:
		material.set_shader_parameter("velocity", Vector2(velocity[0], velocity[1]))
	for key: String in ["refraction_color", "reflection_color"]:
		var c: Array = water.get(key, [])
		if c.size() >= 3:
			material.set_shader_parameter(key, Color(c[0], c[1], c[2]))
	var fresnel: Array = water.get("fresnel", [])
	if fresnel.size() >= 2:
		material.set_shader_parameter("fresnel", Vector2(fresnel[0], fresnel[1]))
	material.set_shader_parameter("sun_direction", -sun.basis.z)  # its parent is the root
	material.set_shader_parameter("sun_color", sun.light_color)
	var fog: Array = meta.get("environment", {}).get("fog_color", [])
	if fog.size() >= 3:
		material.set_shader_parameter("sky_color", Color(fog[0], fog[1], fog[2]))

	var count := 0
	for surface: Dictionary in water.get("surfaces", []):
		var rectangles: Array = surface.get("rectangles", [])
		if rectangles.is_empty():
			continue
		var y := float(surface["height"])
		var tool := SurfaceTool.new()
		tool.begin(Mesh.PRIMITIVE_TRIANGLES)
		tool.set_normal(Vector3.UP)
		for r: Array in rectangles:
			var a := Vector3(r[0], y, r[1])
			var b := Vector3(r[2], y, r[1])
			var c := Vector3(r[2], y, r[3])
			var d := Vector3(r[0], y, r[3])
			for v: Vector3 in [a, b, c, a, c, d]:  # clockwise seen from above: facing up
				tool.add_vertex(v)
		var mesh := MeshInstance3D.new()
		mesh.name = "Water"
		mesh.mesh = tool.commit()
		mesh.material_override = material
		mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		parent.add_child(mesh)
		count += 1
	print("water: ", count, " surfaces")
	return count


# The map's fog cloud: drifting sprites around the camera (the shader places
# them). Returns the node, or null if the map has none.
static func add_fog_cloud(parent: Node, prefix: String, meta: Dictionary) -> Node3D:
	var cloud: Dictionary = meta.get("environment", {}).get("fog_cloud", {})
	if cloud.is_empty() or int(cloud.get("density", 0)) <= 0:
		return null
	var material := ShaderMaterial.new()
	material.shader = FOG_CLOUD_SHADER
	if cloud.get("texture", "") != "":
		material.set_shader_parameter("cloud_texture", _texture(_dir(prefix).path_join(cloud["texture"])))
	var c: Array = cloud.get("color", [])
	if c.size() >= 4:
		material.set_shader_parameter("color", Color(c[0], c[1], c[2], c[3]))
	var range_values: Array = cloud.get("range", [])
	if range_values.size() >= 2:
		material.set_shader_parameter("range", Vector2(range_values[0], range_values[1]))
	var velocity: Array = cloud.get("velocity", [0, 0])
	material.set_shader_parameter("velocity", Vector2(velocity[0], velocity[1]))
	material.set_shader_parameter("spin", float(cloud.get("rotation", 0.0)))
	material.set_shader_parameter("height", float(cloud.get("height", 0.0)))
	var size := float(cloud.get("particle_size", 1.0))
	material.set_shader_parameter("size", size)
	material.set_shader_parameter("spread", size * 0.25)

	# ASSUMPTION: the density is the number of sprites around the camera.
	var quad := QuadMesh.new()
	quad.material = material
	var sprites := MultiMesh.new()
	sprites.transform_format = MultiMesh.TRANSFORM_3D
	sprites.use_custom_data = true
	sprites.mesh = quad
	sprites.instance_count = int(cloud["density"])
	var random := RandomNumberGenerator.new()
	random.seed = 1
	for i in sprites.instance_count:
		sprites.set_instance_transform(i, Transform3D.IDENTITY)
		sprites.set_instance_custom_data(i, Color(random.randf(), random.randf(), random.randf(), random.randf()))
	var node := MultiMeshInstance3D.new()
	node.name = "FogCloud"
	node.multimesh = sprites
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	# The shader moves the sprites to the camera: never cull them.
	node.custom_aabb = AABB(Vector3(-1e5, -1e4, -1e5), Vector3(2e5, 2e4, 2e5))
	parent.add_child(node)
	return node


static func _load_model(path: String) -> Node3D:
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		printerr("cannot load ", path)
		return null
	return document.generate_scene(state) as Node3D


static func _texture(path: String) -> Texture2D:
	var image := Image.load_from_file(path)
	if image == null:
		printerr("cannot load ", path)
		image = Image.create(1, 1, false, Image.FORMAT_RGBA8)
		image.fill(Color.WHITE)
	return ImageTexture.create_from_image(image)


static func _dir(prefix: String) -> String:
	return prefix.replace("\\", "/").get_base_dir()


# The color of a dome's texture along its lowest edge: the average over its
# lowest vertices' texture coordinates. Alpha 0 if there's none to read.
static func _horizon_color(dome: Node3D) -> Color:
	var lowest := INF
	var uvs: Array[Vector2] = []
	var image: Image = null
	for mesh: MeshInstance3D in dome.find_children("*", "MeshInstance3D", true, false):
		for surface in mesh.mesh.get_surface_count():
			var material := mesh.get_active_material(surface) as BaseMaterial3D
			if material == null or material.albedo_texture == null:
				continue
			var arrays := mesh.mesh.surface_get_arrays(surface)
			var points: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
			var coords: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV]
			for i in mini(points.size(), coords.size()):
				var y := (mesh.transform * points[i]).y
				if y < lowest - 1e-4:
					lowest = y
					uvs.clear()
					image = material.albedo_texture.get_image()
				if absf(y - lowest) <= 1e-4:
					uvs.append(coords[i])
	if image == null or uvs.is_empty():
		return Color(0, 0, 0, 0)
	if image.is_compressed():
		image.decompress()
	var sum := Color(0, 0, 0, 0)
	for uv in uvs:
		var x := clampi(int(fposmod(uv.x, 1.0) * image.get_width()), 0, image.get_width() - 1)
		var y := clampi(int(clampf(uv.y, 0.0, 1.0) * image.get_height()), 0, image.get_height() - 1)
		sum += image.get_pixel(x, y)
	return Color(sum.r / uvs.size(), sum.g / uvs.size(), sum.b / uvs.size(), 1.0)


static func _color(values: Array) -> Color:
	return Color(values[0], values[1], values[2])


static func _vector(values: Array) -> Vector3:
	return Vector3(values[0], values[1], values[2])
