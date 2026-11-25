# A planet's water (Stage 13): its terrain's water tables (swgtool
# bake-terrain or export-water: <bake>.water), flat surfaces at their heights
# over their outlines: Naboo's lakes, rivers and sea. The terrain hides them
# where it's higher, so a table over a whole plain shows only in its hollows,
# as Galaxies drew them. Drawn with Battlefront's water shader
# (bf2_water.gdshader): each table's texture (its Galaxies water shader's),
# and, when Battlefront's Naboo map is exported, its rippling normal maps.
#
#   SwgWater.add_water(world, bake, sun, sky_color)   # -> how many surfaces
#
# The server and client keep to the same tables (core::Heightfield::water_at):
# deep water is swum.
extends RefCounted

const WATER_SHADER = preload("res://rendering/bf2_water.gdshader")
# Ripples borrowed from Battlefront's Naboo (bf2tool export-map nab2).
const RIPPLES_DIR := "res://imported_assets/maps/nab2/textures"
const RIPPLE_FRAMES := 16


static func has_water(bake: String) -> bool:
	return FileAccess.file_exists(bake + ".water")


# Each table: {name, height, outline: PackedVector2Array (x z), texture, repeat}.
static func load_tables(bake: String) -> Array:
	var tables: Array = []
	var file := FileAccess.open(bake + ".water", FileAccess.READ)
	if file == null:
		return tables
	while not file.eof_reached():
		var line := file.get_line().strip_edges()
		if line == "" or line.begins_with("#"):
			continue
		var fields := line.split(" ", false)
		if fields.size() < 3:
			continue
		var count := int(fields[2])
		if fields.size() < 3 + count * 2:
			continue
		var outline := PackedVector2Array()
		for i in count:
			outline.append(Vector2(float(fields[3 + i * 2]), float(fields[4 + i * 2])))
		var rest := 3 + count * 2
		tables.append({
			"name": fields[0],
			"height": float(fields[1]),
			"outline": outline,
			"texture": fields[rest] if fields.size() > rest else "",
			"repeat": float(fields[rest + 1]) if fields.size() > rest + 1 else 2.0,
		})
	return tables


static func add_water(parent: Node, bake: String, sun: DirectionalLight3D, sky_color: Color) -> int:
	var tables := load_tables(bake)
	if tables.is_empty():
		return 0
	var ripples := _ripples()
	var materials := {}  # texture -> ShaderMaterial
	var count := 0
	for table: Dictionary in tables:
		var outline: PackedVector2Array = table["outline"]
		var indices := Geometry2D.triangulate_polygon(outline)
		if indices.is_empty():
			# Not a simple polygon: a fan from its first point.
			for i in range(1, outline.size() - 1):
				indices.append_array([0, i, i + 1])
		var y := float(table["height"])
		var tool := SurfaceTool.new()
		tool.begin(Mesh.PRIMITIVE_TRIANGLES)
		tool.set_normal(Vector3.UP)
		for t in range(0, indices.size(), 3):
			var a := outline[indices[t]]
			var b := outline[indices[t + 1]]
			var c := outline[indices[t + 2]]
			# Facing up: clockwise seen from above, a positive x z cross product
			# (as Bf2Map.add_water's rectangles).
			if (b - a).cross(c - a) < 0.0:
				var swap := b
				b = c
				c = swap
			for v: Vector2 in [a, b, c]:
				tool.add_vertex(Vector3(v.x, y, v.y))
		var mesh := MeshInstance3D.new()
		mesh.name = "Water_" + str(table["name"])
		mesh.mesh = tool.commit()
		var key := str(table["texture"])
		if not materials.has(key):
			materials[key] = _material(bake, key, float(table["repeat"]), ripples, sun, sky_color)
		mesh.material_override = materials[key]
		mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		parent.add_child(mesh)
		count += 1
	print("water: ", count, " tables")
	return count


static func _material(bake: String, texture: String, repeat: float, ripples: Texture2DArray,
		sun: DirectionalLight3D, sky_color: Color) -> ShaderMaterial:
	var material := ShaderMaterial.new()
	material.shader = WATER_SHADER
	if texture != "":
		var path := bake + "_textures/" + texture
		var image := Image.load_from_file(path) if FileAccess.file_exists(path) else null
		if image != null:
			image.generate_mipmaps()
			material.set_shader_parameter("main_texture", ImageTexture.create_from_image(image))
	# ASSUMPTION: Galaxies' shader size is meters per repeat; its water
	# textures are near-flat color, so a larger repeat hides their tiling.
	material.set_shader_parameter("repeat_meters", maxf(repeat, 2.0) * 8.0)
	if ripples != null:
		material.set_shader_parameter("normal_maps", ripples)
		material.set_shader_parameter("normal_frames", ripples.get_layers())
		material.set_shader_parameter("normal_meters", 6.0)
	material.set_shader_parameter("refraction_color", Color(0.25, 0.42, 0.5))
	material.set_shader_parameter("reflection_color", Color(0.75, 0.8, 0.85))
	material.set_shader_parameter("fresnel", Vector2(0.25, 0.8))
	material.set_shader_parameter("edge_fade", 1.2)
	material.set_shader_parameter("sun_direction", -sun.basis.z)
	material.set_shader_parameter("sun_color", sun.light_color)
	material.set_shader_parameter("sky_color", sky_color)
	return material


static func _ripples() -> Texture2DArray:
	var frames: Array[Image] = []
	for i in RIPPLE_FRAMES:
		var path := RIPPLES_DIR.path_join("water_normalmap_%d.png" % i)
		var image: Image = null
		if ResourceLoader.exists(path):
			var texture := load(path) as Texture2D
			image = texture.get_image() if texture != null else null
		elif FileAccess.file_exists(path):
			image = Image.load_from_file(path)
		if image != null and image.is_compressed():
			image.decompress()
		if image == null:
			break
		image.convert(Image.FORMAT_RGBA8)
		if not frames.is_empty() and image.get_size() != frames[0].get_size():
			image.resize(frames[0].get_width(), frames[0].get_height())
		image.generate_mipmaps()
		frames.append(image)
	if frames.is_empty():
		return null
	var array := Texture2DArray.new()
	array.create_from_images(frames)
	return array
