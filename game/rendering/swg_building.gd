# Helpers for drawing buildings exported by `swgtool export-building`: a
# glTF with a node per cell (<cell>_mesh, plus collision-only <cell>_floor and
# <cell>_walls) and a JSON sidecar with each cell's lights.
extends RefCounted

const CELL_SHADER = preload("res://rendering/swg_cell.gdshader")
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
	for i in cells.size():
		var cell: Dictionary = cells[i]
		var mesh := building.find_child(cell["name"] + "_mesh", true, false) as MeshInstance3D
		if mesh != null and i > 0:  # cell 0 is the outside, lit by the sun
			apply_cell_lighting(mesh, cell)
		for suffix in ["_floor", "_walls"]:
			var collision := building.find_child(cell["name"] + suffix, true, false) as Node3D
			if collision != null:
				collision.visible = false
	return building


# Replaces each surface's material with the cell shader and the cell's lights.
static func apply_cell_lighting(mesh: MeshInstance3D, cell: Dictionary) -> void:
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
		var material := ShaderMaterial.new()
		material.shader = CELL_SHADER
		var original := mesh.get_active_material(surface) as BaseMaterial3D
		if original != null and original.albedo_texture != null:
			material.set_shader_parameter("albedo_texture", original.albedo_texture)
		material.set_shader_parameter("ambient", ambient)
		material.set_shader_parameter("parallel_count", parallel_count)
		material.set_shader_parameter("parallel_direction", parallel_direction)
		material.set_shader_parameter("parallel_color", parallel_color)
		material.set_shader_parameter("point_count", point_count)
		material.set_shader_parameter("point_position", point_position)
		material.set_shader_parameter("point_color", point_color)
		material.set_shader_parameter("point_attenuation", point_attenuation)
		mesh.set_surface_override_material(surface, material)



static func vec3(values: Array) -> Vector3:
	return Vector3(values[0], values[1], values[2])
