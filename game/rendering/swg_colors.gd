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
const VARIED_ENTRIES := 64   # a varied color is one of a palette's first this many
const SMALLEST_VARIED := 8   # smaller palettes keep their defaults


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
	return {"palettes": palettes, "materials": (data as Dictionary).get("materials", {})}


# Colors a model (an instance of the character): its look's own colors for
# seed 0, else varied by the seed.
static func apply(model: Node, colors: Dictionary, seed: int) -> void:
	if colors.is_empty():
		return
	var materials: Dictionary = colors["materials"]
	var palettes: Dictionary = colors["palettes"]
	for node in model.find_children("*", "MeshInstance3D", true, false):
		var mesh_instance := node as MeshInstance3D
		if mesh_instance.mesh == null:
			continue
		var item := String(mesh_instance.name)
		for surface in mesh_instance.mesh.get_surface_count():
			var original := mesh_instance.mesh.surface_get_material(surface) as BaseMaterial3D
			if original == null or not materials.has(original.resource_name):
				continue
			var entry: Dictionary = materials[original.resource_name]
			var material := ShaderMaterial.new()
			material.shader = SHADER
			material.set_shader_parameter("albedo_texture", original.albedo_texture)
			material.set_shader_parameter("roughness", original.roughness)
			material.set_shader_parameter("metallic", original.metallic)
			material.set_shader_parameter("main_color", _color(entry.get("MAIN", "ffffff"), item, seed, palettes))
			if entry.has("HUEB"):
				material.set_shader_parameter("two_colors", true)
				material.set_shader_parameter("hueb_color", _color(entry["HUEB"], item, seed, palettes))
			mesh_instance.set_surface_override_material(surface, material)


# A factor's color: fixed ("rrggbb"), or a palette's entry by its variable.
static func _color(factor: Variant, item: String, seed: int, palettes: Dictionary) -> Color:
	if factor is String:
		return Color.html(factor)
	var palette: PackedByteArray = palettes.get(factor["palette"], PackedByteArray())
	var count := palette.size() / 3
	if count == 0:
		return Color.WHITE
	var index := int(factor["index"])
	if seed != 0 and count >= SMALLEST_VARIED:
		var variable := String(factor["variable"])
		var key := variable if bool(factor["shared"]) else item + "/" + variable
		index = hash("%d/%s" % [seed, key]) % mini(count, VARIED_ENTRIES)
	index = clampi(index, 0, count - 1)
	return Color8(palette[3 * index], palette[3 * index + 1], palette[3 * index + 2])
