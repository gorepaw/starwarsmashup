# Helpers for drawing buildings exported by `swgtool export-building`: a
# glTF with a node per cell (<cell>_mesh, plus collision-only <cell>_floor and
# <cell>_walls) and a JSON sidecar with each cell's lights.
extends RefCounted

const CELL_SHADER = preload("res://rendering/swg_cell.gdshader")
const CELL_ALPHA_SHADER = preload("res://rendering/swg_cell_alpha.gdshader")
const CELL_CUTOUT_SHADER = preload("res://rendering/swg_cell_cutout.gdshader")
const ADD_SHADER = preload("res://rendering/swg_add.gdshader")
const MAX_CELL_LIGHTS := 8  # per kind; matches the shader


# Loads a building and its sidecar, lights its interior cells with their own
# Galaxies lights, and hides the collision meshes. Returns null on failure.
static func load_lit(path: String) -> Node3D:
	var layout: Variant = JSON.parse_string(FileAccess.get_file_as_string(path.get_basename() + ".json"))
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if layout == null or document.append_from_file(path, state) != OK:
		return null
	var building: Node3D = document.generate_scene(state)
	var cells: Array = layout["cells"]
	var rooms: Array[AABB] = []  # its interior cells' bounds, in its own space
	for i in cells.size():
		var cell: Dictionary = cells[i]
		var mesh := building.find_child(cell["name"] + "_mesh", true, false) as MeshInstance3D
		if mesh != null and i > 0:  # cell 0 is the outside, lit by the sun
			apply_cell_lighting(mesh, cell)
			rooms.append(_in_building(building, mesh) * mesh.get_aabb())
		elif mesh != null:
			building.set_meta(EXTERIOR, building.get_path_to(mesh))
		for suffix in ["_floor", "_walls"]:
			var collision := building.find_child(cell["name"] + suffix, true, false) as Node3D
			if collision != null:
				collision.visible = false
	draw_additive(building)
	if not rooms.is_empty() and building.has_meta(EXTERIOR):
		building.set_meta(ROOMS, rooms)
		var all := rooms[0]
		for room in rooms:
			all = all.merge(room)
		building.set_meta(ROOMS_REACH, all.get_center().length() + all.size.length() * 0.5 + 1.0)
	return building


const EXTERIOR := "swg_exterior"  # a building's outside cell's mesh (r0's), a NodePath from it
const ROOMS := "swg_rooms"        # its interior cells' bounds
const ROOMS_REACH := "swg_rooms_reach"  # m from its origin that all of them lie within

# The buildings with rooms (load_lit's) among `parent`'s children.
static func with_rooms(parent: Node) -> Array[Node3D]:
	var out: Array[Node3D] = []
	for node: Node in parent.get_children():
		if node.has_meta(ROOMS):
			out.append(node as Node3D)
	return out


# Galaxies draws a building's outside (its r0 cell) from within only through
# the portals that lead out; drawn whole, its walls cut through the rooms
# they wrap (Mos Eisley's starport's sandstone shell through its lobby's
# doorways). So a building's outside isn't drawn while the camera is in one of
# its rooms.
static func update_exteriors(buildings: Array[Node3D], camera: Vector3) -> void:
	for building in buildings:
		if not is_instance_valid(building):
			continue
		var inside := false
		var reach := float(building.get_meta(ROOMS_REACH, 1e9))
		if building.global_position.distance_squared_to(camera) <= reach * reach:
			var local := building.global_transform.affine_inverse() * camera
			for room: AABB in building.get_meta(ROOMS):
				if room.grow(0.25).has_point(local):
					inside = true
					break
		var exterior := building.get_node_or_null(building.get_meta(EXTERIOR)) as Node3D
		if exterior != null and exterior.visible == inside:
			exterior.visible = not inside


static func _in_building(building: Node3D, node: Node3D) -> Transform3D:
	var xf := Transform3D.IDENTITY
	var at: Node = node
	while at != null and at != building:
		if at is Node3D:
			xf = (at as Node3D).transform * xf
		at = at.get_parent()
	return xf


# Physics collision from a building's drawn cells (each <cell>_mesh: walls,
# vaulted ceilings, the outside's domes), for the camera only: movement is
# core's, which has floors and the walls along their edges, not ceilings.
static func add_camera_collision(building: Node) -> void:
	for mesh in building.find_children("*_mesh", "MeshInstance3D", true, false):
		(mesh as MeshInstance3D).create_trimesh_collision()


# The additive material for a glTF material swgtool marked as one (extras
# {"blend": "add"}: Galaxies' glowing panels), else null. Unlit, so the same
# inside and out.
static func additive_material(original: Material) -> ShaderMaterial:
	if original == null or not original.has_meta("extras"):
		return null
	var extras: Variant = original.get_meta("extras")
	if not extras is Dictionary or (extras as Dictionary).get("blend", "") != "add":
		return null
	var material := ShaderMaterial.new()
	material.shader = ADD_SHADER
	var base := original as BaseMaterial3D
	if base != null:
		if base.albedo_texture != null:
			material.set_shader_parameter("albedo_texture", base.albedo_texture)
		material.set_shader_parameter("albedo_color", base.albedo_color)
	return material


# Draws a model's additive materials (additive_material) as they are drawn.
static func draw_additive(model: Node) -> void:
	for mesh: MeshInstance3D in model.find_children("*", "MeshInstance3D", true, false):
		if mesh.mesh == null:
			continue
		for surface in mesh.mesh.get_surface_count():
			var material := additive_material(mesh.get_active_material(surface))
			if material != null:
				mesh.set_surface_override_material(surface, material)


# Replaces each surface's material with the cell shader and the cell's lights.
# `to_building` takes the mesh's space to the building's (furniture in it;
# a cell's own mesh is in the building's already).
static func apply_cell_lighting(mesh: MeshInstance3D, cell: Dictionary,
		to_building := Transform3D.IDENTITY) -> void:
	var ambient := Vector3.ZERO
	var parallel_direction := PackedVector3Array()
	var parallel_color := PackedVector3Array()
	var point_position := PackedVector3Array()
	var point_color := PackedVector3Array()
	var point_attenuation := PackedVector3Array()
	for light: Dictionary in cell["lights"]:
		var color := vec3(light["color"])
		match light["kind"]:
			"ambient":
				ambient += color
			"parallel":
				if parallel_direction.size() < MAX_CELL_LIGHTS:
					parallel_direction.append(vec3(light["direction"]))
					parallel_color.append(color)
			"point":
				if point_position.size() < MAX_CELL_LIGHTS:
					point_position.append(vec3(light["position"]))
					point_color.append(color)
					point_attenuation.append(vec3(light["attenuation"]))
	var parallel_count := parallel_direction.size()
	var point_count := point_position.size()
	parallel_direction.resize(MAX_CELL_LIGHTS)
	parallel_color.resize(MAX_CELL_LIGHTS)
	point_position.resize(MAX_CELL_LIGHTS)
	point_color.resize(MAX_CELL_LIGHTS)
	point_attenuation.resize(MAX_CELL_LIGHTS)

	for surface in mesh.mesh.get_surface_count():
		var active := mesh.get_active_material(surface)
		if active is ShaderMaterial and (active as ShaderMaterial).shader == ADD_SHADER:
			continue  # lit by nothing but itself (draw_additive)
		var additive := additive_material(active)
		if additive != null:
			mesh.set_surface_override_material(surface, additive)
			continue
		var material := ShaderMaterial.new()
		var original := active as BaseMaterial3D
		# Drawn as the material's effect draws (swgtool: glTF's alpha modes).
		material.shader = CELL_SHADER
		if original != null and original.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA:
			material.shader = CELL_ALPHA_SHADER
		elif original != null and original.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR:
			material.shader = CELL_CUTOUT_SHADER
		if original != null and original.albedo_texture != null:
			material.set_shader_parameter("albedo_texture", original.albedo_texture)
		if original != null:
			material.set_shader_parameter("albedo_color", original.albedo_color)
		material.set_shader_parameter("ambient", ambient)
		material.set_shader_parameter("parallel_count", parallel_count)
		material.set_shader_parameter("parallel_direction", parallel_direction)
		material.set_shader_parameter("parallel_color", parallel_color)
		material.set_shader_parameter("point_count", point_count)
		material.set_shader_parameter("point_position", point_position)
		material.set_shader_parameter("point_color", point_color)
		material.set_shader_parameter("point_attenuation", point_attenuation)
		material.set_shader_parameter("to_building", Projection(to_building))
		mesh.set_surface_override_material(surface, material)



static func vec3(values: Array) -> Vector3:
	return Vector3(values[0], values[1], values[2])
