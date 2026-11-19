# Draws a Battlefront II map exported with bf2tool export-map: its terrain's
# texture layers, its objects' models, its sky dome, fog and lights. `prefix`
# is the export's bake prefix (e.g. imported_assets/maps/dag1/dag1); models,
# textures and maps are relative to its folder.
extends RefCounted

const TERRAIN_SHADER = preload("res://rendering/bf2_terrain.gdshader")
const MAX_LAYERS := 8
const LAYER_SIZE := 512
const SKY_DOME_SCALE := 150.0  # Battlefront draws its few-meter dome at infinity


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
	material.shader = TERRAIN_SHADER
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
	return material


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
		parent.add_child(node)
		count += 1
	for template: Variant in models.values():
		if template != null:
			(template as Node).free()
	print("placed ", count, " objects (", models.size(), " models) from ", path.get_file())
	return count


# Fog, ambient light, the sun, local lights and the sky dome. Returns the
# dome, which should follow the camera, or null.
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
	# Dagobah) are where the view fades out.
	var ranges: Array = env.get("near_scene_range", [])
	environment.fog_enabled = true
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
	for light: Dictionary in env.get("lights", []):
		var omni := OmniLight3D.new()
		omni.position = _vector(light["position"])
		omni.light_color = _color(light["color"])
		omni.omni_range = float(light["range"])
		parent.add_child(omni)

	var dome_path: String = env.get("sky_dome", "")
	if dome_path == "":
		return null
	var dome := _load_model(_dir(prefix).path_join(dome_path))
	if dome == null:
		return null
	for mesh: MeshInstance3D in dome.find_children("*", "MeshInstance3D", true, false):
		mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		for surface in mesh.mesh.get_surface_count():
			var material := mesh.get_active_material(surface) as BaseMaterial3D
			if material != null:
				material = material.duplicate() as BaseMaterial3D
				material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
				material.disable_fog = true
				material.cull_mode = BaseMaterial3D.CULL_DISABLED
				mesh.set_surface_override_material(surface, material)
	dome.scale = Vector3.ONE * SKY_DOME_SCALE
	parent.add_child(dome)
	return dome


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


static func _color(values: Array) -> Color:
	return Color(values[0], values[1], values[2])


static func _vector(values: Array) -> Vector3:
	return Vector3(values[0], values[1], values[2])
