class_name SwgColors
extends RefCounted
# A Galaxies character's colors: what swgtool export-character writes next to
# its model (<name>.colors.json): each customizable material's MAIN color and,
# for a two-color one, its HUEB color, each a palette's entry chosen by a
# customization variable (or a fixed color). Galaxies' pixel programs multiply
# the texture by MAIN, or by MAIN blended to HUEB by the texture's alpha;
# swg_person.gdshader does the same.
#
# The colors are the look's own (its shaders' defaults), or varied by a seed:
# each variable then takes an entry of its palette by the seed. A variable the
# whole body shares (its skin) takes one for the body; a worn item's own, one
# for that item. How they vary is ours (Galaxies' server chose): only from a
# palette's first entries, its plain colors (later ones are dyes, up to
# garish), and a small palette (a stormtrooper's white, an eye's) keeps its
# default.

const SHADER := preload("res://rendering/swg_person.gdshader")
const ALPHA_SHADER := preload("res://rendering/swg_person_alpha.gdshader")
const CUTOUT_SHADER := preload("res://rendering/swg_person_cutout.gdshader")
const FACE_SHADER := preload("res://rendering/swg_face.gdshader")
const FACE_LAYERS := 8       # swg_face.gdshader's MAX_LAYERS
const VARIED_ENTRIES := 64   # a varied color is one of a palette's first this many
const SMALLEST_VARIED := 8   # smaller palettes keep their defaults
const CLOTHING_GROW := 0.015
const ARMOR_GROW := 0.018


static func worn_grow_for_material(mat_name: String) -> float:
	var n := mat_name.to_lower()
	for prefix: String in ["armor_", "belt_", "bandolier_", "utility_belt_", "boots_", "gloves_", "helmet_", "goggles_"]:
		if n.begins_with(prefix):
			return ARMOR_GROW
	for prefix: String in ["shirt_", "dress_", "skirt_", "pants_", "vest_", "jacket_", "shoes_", "robe_", "hat_", "apron_", "cloak_"]:
		if n.begins_with(prefix):
			return CLOTHING_GROW
	return 0.0


# The colors exported with a character's model (its .gltf's path); empty if
# there are none.
static func load_colors(model_path: String) -> Dictionary:
	var path := model_path.get_basename() + ".colors.json"
	if not FileAccess.file_exists(path):
		return {}
	var data: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not data is Dictionary:
		push_warning("unreadable colors: " + path)
		return {}
	var palettes := {}
	var listed: Dictionary = (data as Dictionary).get("palettes", {})
	for name: String in listed:
		palettes[name] = String(listed[name]).hex_decode()
	return {"palettes": palettes, "materials": (data as Dictionary).get("materials", {}),
			"dir": model_path.get_base_dir(), "textures": {}}


# Colors a model (an instance of the character): its look's own colors for
# seed 0, else varied by the seed.
static func apply(model: Node, colors: Dictionary, seed: int) -> void:
	_apply_impl(model, colors, seed, {})


# Colors a model using explicit customization variable overrides
# ({variable_name: int_index}), falling back to each factor's default index.
static func apply_custom(model: Node, colors: Dictionary, overrides: Dictionary) -> void:
	_apply_impl(model, colors, 0, overrides)


static func _apply_impl(model: Node, colors: Dictionary, seed: int, overrides: Dictionary) -> void:
	if colors.is_empty():
		return
	var materials: Dictionary = colors["materials"]
	var palettes: Dictionary = colors["palettes"]
	for node in model.find_children("*", "MeshInstance3D", true, false):
		var mesh_instance := node as MeshInstance3D
		if mesh_instance.mesh == null:
			continue
		var item := String(mesh_instance.name)
		var hidden: Dictionary = mesh_instance.get_meta("hidden_surfaces", {})  # (a hair style's replaced head parts)
		for surface in mesh_instance.mesh.get_surface_count():
			if hidden.has(surface):
				continue
			var original := mesh_instance.mesh.surface_get_material(surface) as BaseMaterial3D
			if original == null or not materials.has(original.resource_name):
				continue
			var entry: Dictionary = materials[original.resource_name]
			if entry.has("layers"):
				mesh_instance.set_surface_override_material(surface,
						_face(entry["layers"], original, seed, colors, overrides))
				continue
			var material := ShaderMaterial.new()
			material.shader = SHADER  # as the material's effect draws (glTF's alpha modes)
			if original.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA:
				material.shader = ALPHA_SHADER
			elif original.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR:
				material.shader = CUTOUT_SHADER
			material.set_shader_parameter("albedo_texture", original.albedo_texture)
			material.set_shader_parameter("roughness", original.roughness)
			material.set_shader_parameter("metallic", original.metallic)
			material.set_shader_parameter("main_color", _color(entry.get("MAIN", "ffffff"), item, seed, palettes, overrides))
			if entry.has("HUEB"):
				material.set_shader_parameter("two_colors", true)
				material.set_shader_parameter("hueb_color", _color(entry["HUEB"], item, seed, palettes, overrides))
			var grow := worn_grow_for_material(original.resource_name)
			if grow > 0.0:
				material.set_shader_parameter("grow", grow)
			mesh_instance.set_surface_override_material(surface, material)


# A texture renderer's texture (a face), composited as it's drawn
# (swg_face.gdshader): each layer's texture (a style variable's choice, or
# its one) and color. A style's default is its first (ASSUMPTION: Galaxies'
# renderers give none; its first is a plain one, e.g. a Zabrak's no tattoo).
static func _face(layers: Array, original: BaseMaterial3D, seed: int, colors: Dictionary,
		overrides: Dictionary = {}) -> ShaderMaterial:
	var textures: Array[Texture2D] = []
	var rects := PackedVector4Array()
	var uvs := PackedVector4Array()
	var layer_colors := PackedVector3Array()
	var blends := PackedInt32Array()
	for layer: Dictionary in layers.slice(0, FACE_LAYERS):
		var choices: Array = layer["textures"]
		var index := 0
		var style := String(layer.get("style", ""))
		if not style.is_empty():
			if overrides.has(style):
				index = clampi(int(overrides[style]), 0, maxi(0, choices.size() - 1))
			elif seed != 0:
				index = hash("%d/%s" % [seed, style]) % choices.size()
		textures.append(_texture(colors, String(choices[index])))
		var r: Array = layer["rect"]
		var u: Array = layer["uv"]
		rects.append(Vector4(r[0], r[1], r[2], r[3]))
		uvs.append(Vector4(u[0], u[1], u[2], u[3]))
		var color := _color(layer["color"], "face", seed, colors["palettes"], overrides).srgb_to_linear()
		layer_colors.append(Vector3(color.r, color.g, color.b))
		blends.append(1 if bool(layer["blend"]) else 0)
	var count := textures.size()
	while textures.size() < FACE_LAYERS:  # unused layers still need a texture
		textures.append(textures[0] if count > 0 else null)
		rects.append(Vector4())
		uvs.append(Vector4())
		layer_colors.append(Vector3.ONE)
		blends.append(0)
	var material := ShaderMaterial.new()
	material.shader = FACE_SHADER
	material.set_shader_parameter("layer_count", count)
	material.set_shader_parameter("layer_textures", textures)
	material.set_shader_parameter("layer_rects", rects)
	material.set_shader_parameter("layer_uvs", uvs)
	material.set_shader_parameter("layer_colors", layer_colors)
	material.set_shader_parameter("layer_blends", blends)
	material.set_shader_parameter("roughness", original.roughness)
	material.set_shader_parameter("metallic", original.metallic)
	return material


# A texture exported next to the model (cached with its colors).
static func _texture(colors: Dictionary, file: String) -> Texture2D:
	var cache: Dictionary = colors["textures"]
	if not cache.has(file):
		# Read as bytes, as GLTFDocument reads a model's, not as an import.
		var image := Image.new()
		var bytes := FileAccess.get_file_as_bytes(String(colors["dir"]).path_join(file))
		if not bytes.is_empty() and image.load_png_from_buffer(bytes) == OK:
			image.generate_mipmaps()
			cache[file] = ImageTexture.create_from_image(image)
		else:
			push_warning("missing texture: " + file)
			cache[file] = null
	return cache[file]


# A factor's color: fixed ("rrggbb"), or a palette's entry by its variable.
static func _color(factor: Variant, item: String, seed: int, palettes: Dictionary,
		overrides: Dictionary = {}) -> Color:
	if factor is String:
		return Color.html(factor)
	var palette: PackedByteArray = palettes.get(factor["palette"], PackedByteArray())
	var count := palette.size() / 3
	if count == 0:
		return Color.WHITE
	var variable := String(factor["variable"])
	var index := int(factor["index"])
	if overrides.has(variable):
		index = int(overrides[variable])
	elif seed != 0 and count >= SMALLEST_VARIED:
		var key := variable if bool(factor["shared"]) else item + "/" + variable
		index = hash("%d/%s" % [seed, key]) % mini(count, VARIED_ENTRIES)
	index = clampi(index, 0, count - 1)
	return Color8(palette[3 * index], palette[3 * index + 1], palette[3 * index + 2])
