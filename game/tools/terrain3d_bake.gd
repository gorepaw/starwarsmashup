# Loads a baked terrain region (swgtool bake-terrain) into a Terrain3D node:
# heights, plus one texture asset per ground-texture family painted through
# the control map. Needs the Terrain3D addon (tools/install_terrain3d.ps1).
# Terrain3D types are untyped here, so tools that preload this script still
# parse when the addon isn't installed.
extends RefCounted

const TEXTURE_SIZE := 512  # Terrain3D needs every albedo the same size and format
const REGION_SIZE := 256   # vertices per region side; at 4 m spacing 32 regions span 32 km
const NO_FAMILY := 255     # the bake's "no texture" index


static func available() -> bool:
	return ClassDB.class_exists("Terrain3D")


# Adds a Terrain3D node under `parent` and imports the bake into it.
static func create(parent: Node, prefix: String, meta: Dictionary) -> Node3D:
	var samples: int = meta["samples"]
	var spacing: float = meta["spacing"]
	var heights := FileAccess.get_file_as_bytes(prefix + ".r32")
	var indices := FileAccess.get_file_as_bytes(prefix + ".shaders")

	var terrain: Node3D = ClassDB.instantiate("Terrain3D")
	terrain.name = "Terrain3D"
	terrain.region_size = REGION_SIZE
	terrain.vertex_spacing = spacing
	var families: Array = meta.get("shader_families", [])
	terrain.assets = _build_assets(prefix, meta, families)
	terrain.material.world_background = 0  # none: nothing outside the bake
	terrain.free_editor_textures = false   # it would need the assets saved to a file
	# Terrain3D sets up its data when it enters the tree.
	if parent.is_inside_tree():
		parent.add_child(terrain)
		_import(terrain, samples, spacing, meta, heights, indices, families.size())
	else:
		terrain.ready.connect(_import.bind(terrain, samples, spacing, meta, heights, indices, families.size()),
				CONNECT_ONE_SHOT)
		parent.add_child(terrain)
	return terrain


static func _import(terrain: Node3D, samples: int, spacing: float, meta: Dictionary, heights: PackedByteArray,
		indices: PackedByteArray, family_count: int) -> void:
	# Rows run along +Z in both. Columns run along SWG +X, which is Godot -X,
	# so the images are mirrored and placed from the region's most negative
	# Godot x.
	var height_image := Image.create_from_data(samples, samples, false, Image.FORMAT_RF, heights)
	height_image.flip_x()
	var control_image: Image = null
	if indices.size() == samples * samples:
		control_image = _control_image(indices, samples, family_count)
		control_image.flip_x()
	var corner := Vector3(-(float(meta["origin_x"]) + (samples - 1) * spacing), 0, float(meta["origin_z"]))
	terrain.data.import_images([height_image, control_image, null], corner, 0.0, 1.0)


# Control map: each vertex's base texture is its family (Terrain3D blends
# between neighboring vertices). Vertices with no family get the fallback
# asset after the families.
static func _control_image(indices: PackedByteArray, samples: int, family_count: int) -> Image:
	var control := PackedByteArray()
	control.resize(samples * samples * 4)
	for i in indices.size():
		var id := indices[i] if indices[i] != NO_FAMILY else family_count
		control.encode_u32(i * 4, (id & 0x1F) << 27)  # base texture id, bits 31-27 (controlmap_format.md)
	return Image.create_from_data(samples, samples, false, Image.FORMAT_RF, control)


static func _build_assets(prefix: String, meta: Dictionary, families: Array) -> Resource:
	var assets: Resource = ClassDB.instantiate("Terrain3DAssets")
	var texture_dir := prefix.replace("\\", "/").get_base_dir().path_join(meta.get("texture_dir", ""))
	for i in families.size():
		var family: Dictionary = families[i]
		var image: Image = null
		var children: Array = family["children"]
		if not children.is_empty() and children[0]["texture"] != "":
			image = Image.load_from_file(texture_dir.path_join(children[0]["texture"]))
		var color: Array = family["color"]
		assets.set_texture_asset(i, _texture_asset(family["name"], image, Color8(color[0], color[1], color[2]),
				family["size"]))
	assets.set_texture_asset(families.size(), _texture_asset("none", null, Color(0.78, 0.66, 0.47), 2.0))
	return assets


static func _texture_asset(asset_name: String, image: Image, color: Color, size: float) -> Resource:
	if image == null:
		image = Image.create(4, 4, false, Image.FORMAT_RGBA8)
		image.fill(color)
	# Terrain3D reads albedo alpha as height for blending. Galaxies' alpha
	# means something else (transparency), so make it uniform.
	image.convert(Image.FORMAT_RGB8)
	image.convert(Image.FORMAT_RGBA8)
	image.resize(TEXTURE_SIZE, TEXTURE_SIZE)
	image.generate_mipmaps()
	var asset: Resource = ClassDB.instantiate("Terrain3DTextureAsset")
	asset.name = asset_name
	asset.albedo_texture = ImageTexture.create_from_image(image)
	asset.uv_scale = 1.0 / maxf(size, 0.01)  # Galaxies: meters per repeat; Terrain3D: repeats per meter
	return asset

