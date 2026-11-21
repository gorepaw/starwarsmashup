# Draws a city exported by `swgtool export-city`: the objects a world snapshot
# places around it (<bake>.objects.json, next to the terrain bake), each
# model loaded once. Buildings are drawn with their cells' own lights
# (rendering/swg_building.gd). What they stop is in <bake>.collision, which
# the world on that bake loads (core/), so nothing here collides.
extends RefCounted

const SwgBuilding = preload("res://rendering/swg_building.gd")


static func has_city(prefix: String) -> bool:
	return FileAccess.file_exists(prefix + ".objects.json")


# Places every object; returns how many.
static func add_objects(parent: Node, prefix: String) -> int:
	var path := prefix + ".objects.json"
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not parsed is Dictionary:
		printerr("cannot read ", path)
		return 0
	var dir := prefix.replace("\\", "/").get_base_dir()
	var models := {}  # file -> loaded scene (null if it failed)
	var count := 0
	for object: Dictionary in parsed["objects"]:
		var model: String = object.get("model", "")
		if not models.has(model):
			var file := dir.path_join(model)
			models[model] = SwgBuilding.load_lit(file) if object.get("building", false) else _load_model(file)
		if models[model] == null:
			continue
		var node := (models[model] as Node3D).duplicate() as Node3D
		var b: Array = object["basis"]
		var p: Array = object["position"]
		node.basis = Basis(Vector3(b[0], b[3], b[6]), Vector3(b[1], b[4], b[7]), Vector3(b[2], b[5], b[8]))
		node.position = Vector3(p[0], p[1], p[2])
		if object.get("recruiter", false):
			node.set_meta("recruiter", true)  # a hub's war terminal (export-city --hub)
		parent.add_child(node)
		count += 1
	for template: Variant in models.values():
		if template != null:
			(template as Node).free()
	print("placed ", count, " objects (", models.size(), " models) from ", path.get_file())
	return count


static func _load_model(path: String) -> Node3D:
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		printerr("cannot load ", path)
		return null
	return document.generate_scene(state) as Node3D
