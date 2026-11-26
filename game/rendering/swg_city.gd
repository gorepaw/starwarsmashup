# Draws a city exported by `swgtool export-city`: the objects a world snapshot
# places around it (<bake>.objects.json, next to the terrain bake), each
# model loaded once. Its creatures (a ronto, a saddled dewback: "creature")
# stand in their own colors, breathing (their idle clip, looped, each from
# a moment of its own). Its particle effects (steam, smoke, a campfire:
# "particles") are drawn by rendering/swg_particles.gd. Buildings are drawn with their cells' own lights
# (rendering/swg_building.gd), and so is the furniture in their cells. Their
# doors slide open when someone comes near (update_doors). What
# they stop is in <bake>.collision, which
# the world on that bake loads (core/), so nothing here collides.
extends RefCounted

const SwgBuilding = preload("res://rendering/swg_building.gd")
const SwgParticles = preload("res://rendering/swg_particles.gd")


static func has_city(prefix: String) -> bool:
	return FileAccess.file_exists(prefix + ".objects.json")


# Places every object; returns how many. With `camera_collision`, buildings
# get physics collision for a camera to keep inside their rooms
# (SwgBuilding.add_camera_collision), shared by their copies. With a
# `radius`, only those within it of `center` (game x z), and with
# `outdoors_only` none of what's inside buildings (a backdrop's).
static func add_objects(parent: Node, prefix: String, camera_collision := false, center := Vector2.ZERO,
		radius := -1.0, outdoors_only := false) -> int:
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
	var bounds := {}  # model -> its meshes' bounds (for footprints)
	var footprints: Array = parent.get_meta(FOOTPRINTS, [])
	for object: Dictionary in parsed["objects"]:
		placed.append(null)
		if radius > 0.0:
			var at: Array = object["position"]
			if Vector2(float(at[0]), float(at[2])).distance_to(center) > radius:
				continue
		if outdoors_only and int(object.get("inside", -1)) >= 0:
			continue
		var model: String = object.get("model", "")
		if not models.has(model):
			var file := dir.path_join(model)
			if object.get("particles", false):
				models[model] = SwgParticles.load_effect(file)
			elif object.get("building", false):
				models[model] = SwgBuilding.load_lit(file)
			else:
				models[model] = _load_model(file)
			if models[model] != null and object.get("creature", false):
				SwgColors.apply(models[model], SwgColors.load_colors(file), 0)
			if camera_collision and models[model] != null and object.get("building", false):
				SwgBuilding.add_camera_collision(models[model])
			if models[model] != null:
				_set_draw_distance(models[model], _draw_distance(object))
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
			node.add_to_group(DOORS_GROUP)
		parent.add_child(node)
		placed[placed.size() - 1] = node
		count += 1
		if int(object.get("inside", -1)) < 0 and not object.get("particles", false) and not object.get("creature", false):
			if not bounds.has(model):
				bounds[model] = _bounds(models[model] as Node3D)
			var footprint := _footprint(node, bounds[model])
			if not footprint.is_empty():
				footprints.append(footprint)
		if object.get("creature", false):
			_breathe(node, placed.size())
		var inside := int(object.get("inside", -1))
		if inside >= 0 and inside < placed.size() and placed[inside] != null:
			_light_furniture(node, placed[inside], dir.path_join(str(parsed["objects"][inside]["model"])),
					int(object.get("cell", 0)), cells)
	parent.set_meta(FOOTPRINTS, footprints)
	for template: Variant in models.values():
		if template != null:
			(template as Node).free()
	print("placed ", count, " objects (", models.size(), " models) from ", path.get_file())
	return count


const DOORS_GROUP := &"swg_doors"  # the sliding doors' nodes (a mark on one goes with it)
# The structures' footprints (on the parent: their outlines, game x z, for
# the radar): a building's, a wall's, a tent's; not a prop's (smaller than
# FOOTPRINT_AREA m^2 or lower than FOOTPRINT_HEIGHT m).
const FOOTPRINTS := "swg_footprints"
const FOOTPRINT_AREA := 10.0
const FOOTPRINT_HEIGHT := 2.2


# A model's meshes' bounds, in its own space (an empty AABB if it has none).
static func _bounds(model: Node3D) -> AABB:
	var out := AABB()
	var first := true
	for node in model.find_children("*", "MeshInstance3D", true, false):
		var mi := node as MeshInstance3D
		if mi.mesh == null or not mi.visible:
			continue
		var xf := Transform3D.IDENTITY
		var at: Node = mi
		while at != null and at != model:
			if at is Node3D:
				xf = (at as Node3D).transform * xf
			at = at.get_parent()
		var box := xf * mi.get_aabb()
		out = box if first else out.merge(box)
		first = false
	return out


# Its outline on the ground (game x z, as placed), or empty for a prop.
static func _footprint(node: Node3D, box: AABB) -> PackedVector2Array:
	if box.size.x * box.size.z < FOOTPRINT_AREA or box.size.y < FOOTPRINT_HEIGHT:
		return PackedVector2Array()
	var out := PackedVector2Array()
	for corner: Vector2 in [Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1)]:
		var p := node.transform * (box.position + Vector3(box.size.x * corner.x, 0.0, box.size.z * corner.y))
		out.append(Vector2(p.x, p.z))
	return out


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


# Loops a creature's idle clip, from a moment of its own (by `seed`), so
# a herd doesn't breathe as one.
static func _breathe(node: Node3D, seed: int) -> void:
	var player := node.find_child("AnimationPlayer", true, false) as AnimationPlayer
	if player == null or not player.has_animation("idle"):
		return
	var idle := player.get_animation("idle")
	idle.loop_mode = Animation.LOOP_LINEAR
	player.play("idle")
	player.seek(idle.length * fposmod(seed * 0.618034, 1.0), true)


# How far an object is drawn (Stage 13: a planet's towns are kilometers
# apart, and all of them are placed at once): buildings from across a town,
# props less far, what's in rooms only from close by. Set on a model once;
# its copies keep it.
static func _draw_distance(object: Dictionary) -> float:
	if object.get("building", false):
		return 3000.0
	if int(object.get("inside", -1)) >= 0:
		return 150.0
	if object.get("particles", false):
		return 350.0
	if object.get("creature", false):
		return 500.0
	return 1200.0


static func _set_draw_distance(model: Node, distance: float) -> void:
	if model is GeometryInstance3D:
		(model as GeometryInstance3D).visibility_range_end = distance
		(model as GeometryInstance3D).visibility_range_end_margin = distance * 0.05
	for child: Node in model.find_children("*", "GeometryInstance3D", true, false):
		(child as GeometryInstance3D).visibility_range_end = distance
		(child as GeometryInstance3D).visibility_range_end_margin = distance * 0.05
	# Rooms' lights only matter from close by.
	for child: Node in model.find_children("*", "Light3D", true, false):
		var light := child as Light3D
		light.distance_fade_enabled = true
		light.distance_fade_begin = minf(distance, 250.0)
		light.distance_fade_length = 50.0


static func _load_model(path: String) -> Node3D:
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		printerr("cannot load ", path)
		return null
	var model := document.generate_scene(state) as Node3D
	SwgBuilding.draw_additive(model)
	return model
