# Draws a city exported by `swgtool export-city`: the objects a world snapshot
# places around it (<bake>.objects.json, next to the terrain bake), each
# model loaded once. Buildings are drawn with their cells' own lights
# (rendering/swg_building.gd), and so is the furniture in their cells. Their
# doors slide open when someone comes near (update_doors). What
# they stop is in <bake>.collision, which
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
	var cells := {}   # a building's file -> its cells (from its sidecar), for furniture's lights
	var placed: Array[Node3D] = []  # by the objects' index; null where one wasn't
	var count := 0
	for object: Dictionary in parsed["objects"]:
		placed.append(null)
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
		if object.has("door"):
			var door: Dictionary = object["door"]
			var slide: Array = door["slide"]
			node.set_meta("door", {"closed": node.position, "slide": Vector3(slide[0], slide[1], slide[2]),
					"open_time": maxf(0.05, float(door["open"])), "close_time": maxf(0.05, float(door["close"])),
					"radius": float(door["radius"]), "open": 0.0})
		parent.add_child(node)
		placed[placed.size() - 1] = node
		count += 1
		var inside := int(object.get("inside", -1))
		if inside >= 0 and inside < placed.size() and placed[inside] != null:
			_light_furniture(node, placed[inside], dir.path_join(str(parsed["objects"][inside]["model"])),
					int(object.get("cell", 0)), cells)
	for template: Variant in models.values():
		if template != null:
			(template as Node).free()
	print("placed ", count, " objects (", models.size(), " models) from ", path.get_file())
	return count


# The doors among a parent's children (add_objects' doors).
static func doors_in(parent: Node) -> Array[Node3D]:
	var doors: Array[Node3D] = []
	for node: Node in parent.get_children():
		if node.has_meta("door"):
			doors.append(node as Node3D)
	return doors


# Opens the doors someone is near (within a door's radius, a storey up or
# down at most) and closes the rest, at their styles' speeds.
static func update_doors(doors: Array[Node3D], people: Array[Vector3], delta: float) -> void:
	for node in doors:
		var door: Dictionary = node.get_meta("door")
		var closed: Vector3 = door["closed"]
		var near := false
		for at in people:
			if absf(at.y - closed.y) < 3.0 and Vector2(at.x - closed.x, at.z - closed.z).length() < float(door["radius"]):
				near = true
				break
		var open: float = door["open"]
		var target := 1.0 if near else 0.0
		if open == target:
			continue
		var rate := 1.0 / float(door["open_time"] if near else door["close_time"])
		open = move_toward(open, target, rate * delta)
		door["open"] = open
		node.position = closed + (door["slide"] as Vector3) * smoothstep(0.0, 1.0, open)


# Furniture in a building's cell is lit by that cell's lights, as the cell
# is (the outside, cell 0, by the sun).
static func _light_furniture(node: Node3D, building: Node3D, building_file: String, cell: int, cells: Dictionary) -> void:
	if cell <= 0:
		return
	if not cells.has(building_file):
		var layout: Variant = JSON.parse_string(FileAccess.get_file_as_string(building_file.get_basename() + ".json"))
		cells[building_file] = layout["cells"] if layout is Dictionary else []
	var list: Array = cells[building_file]
	if cell >= list.size():
		return
	# From local transforms: the world may be built before the scene runs.
	# Both stand in the same parent.
	var to_building := building.transform.affine_inverse() * node.transform
	for mesh: MeshInstance3D in node.find_children("*", "MeshInstance3D", true, false):
		var in_node := Transform3D.IDENTITY
		var at: Node = mesh
		while at != node:
			in_node = (at as Node3D).transform * in_node
			at = at.get_parent()
		SwgBuilding.apply_cell_lighting(mesh, list[cell], to_building * in_node)


static func _load_model(path: String) -> Node3D:
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		printerr("cannot load ", path)
		return null
	return document.generate_scene(state) as Node3D
