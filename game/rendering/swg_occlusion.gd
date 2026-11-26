extends RefCounted
# What's worn hides what it covers, as Galaxies draws it (its occlusion
# zones): swgtool writes <name>.occlusion.json next to a character's, a hair
# style's or a worn thing's .gltf (see tools/swg/character_export.h). Each
# mesh names the zones it covers ("chest", "l_hand"...) and its layer; a
# triangle of another mesh whose zones are all covered by meshes of higher
# layers isn't drawn (nor the whole mesh, once its "fully" zones all are).
# This replaces standing worn things off the skin, which split hard-edged
# armor at its seams.
#
# tag() marks a model's meshes with their data as they're loaded; apply()
# hides what's covered among all the tagged meshes under a model, again
# whenever what's worn changes. (Loaded by preload, as SwgOcclusion.)

const META := "swg_occlusion"
const ORIGINAL := "swg_occlusion_mesh"   # the mesh as exported (all its triangles)
const SHOWN := "swg_occlusion_shown"     # the covered set the shown mesh was built for

static var _data_cache: Dictionary = {}  # .gltf path -> {node name: entry}


# Marks `model`'s meshes (an instance of `gltf_path`) with their occlusion
# zones, if it has any. Call before its meshes are moved or renamed.
static func tag(model: Node, gltf_path: String) -> void:
	var by_node := _load(gltf_path)
	if by_node.is_empty():
		return
	for node in model.find_children("*", "MeshInstance3D", true, false):
		var entry: Variant = by_node.get(String(node.name))
		if entry is Dictionary:
			node.set_meta(META, entry)


# Hides what's covered among the tagged meshes under `root` (a character and
# what it wears, moved under it). Meshes under `excluded` nodes (a hair style
# under a helmet) cover nothing.
static func apply(root: Node, excluded: Array = []) -> void:
	var meshes: Array[MeshInstance3D] = []
	for node in root.find_children("*", "MeshInstance3D", true, false):
		if node.has_meta(META) and not node.is_queued_for_deletion() and not _under_freed(node, root):
			meshes.append(node as MeshInstance3D)
	var covering: Array[MeshInstance3D] = []
	for mi in meshes:
		if not _under_any(mi, excluded):
			covering.append(mi)
	for mi in meshes:
		var entry: Dictionary = mi.get_meta(META)
		var layer := int(entry.get("layer", 0))
		var covered := {}
		for other in covering:
			if other == mi:
				continue
			var other_entry: Dictionary = other.get_meta(META)
			if int(other_entry.get("layer", 0)) > layer:
				for zone: String in other_entry.get("occludes", []):
					covered[zone] = true
		_show(mi, entry, covered)


static func _show(mi: MeshInstance3D, entry: Dictionary, covered: Dictionary) -> void:
	var original: Variant = mi.get_meta(ORIGINAL, mi.mesh)
	if not original is ArrayMesh:
		return
	var zones := covered.keys()
	zones.sort()
	var key := ",".join(zones)
	if mi.has_meta(SHOWN) and String(mi.get_meta(SHOWN)) == key:
		return
	mi.set_meta(ORIGINAL, original)
	mi.set_meta(SHOWN, key)

	var mesh := original as ArrayMesh
	var whole := _all_covered(entry.get("fully", []), covered)
	var combinations: Array = entry.get("combinations", [])
	var hidden_combination := PackedByteArray()
	hidden_combination.resize(combinations.size())
	var any := whole
	for c in combinations.size():
		if _all_covered(combinations[c], covered):
			hidden_combination[c] = 1
			any = true
	if not any:
		_set_mesh(mi, mesh)
		return
	var surfaces: Array = entry.get("surfaces", [])
	var hidden: Array = []  # per surface: each triangle's 1 (hidden) or 0, or null (all shown)
	for s in mesh.get_surface_count():
		var triangles := mesh.surface_get_array_index_len(s) / 3
		var flags := PackedByteArray()
		flags.resize(triangles)
		if whole:
			flags.fill(1)
		elif s < surfaces.size() and (surfaces[s] as Array).size() == triangles:
			var per_triangle: Array = surfaces[s]
			for t in triangles:
				var c := int(per_triangle[t])
				if c >= 0 and c < hidden_combination.size() and hidden_combination[c] == 1:
					flags[t] = 1
		else:
			if s < surfaces.size():
				push_warning("occlusion: %s surface %d has %d triangles, not %d" % [
						mi.name, s, triangles, (surfaces[s] as Array).size()])
			flags = PackedByteArray()
		hidden.append(flags if flags.has(1) else null)
	_set_mesh(mi, _filtered(mesh, hidden))


# `mesh` without its hidden triangles. A surface left with none keeps one
# degenerate triangle, so surfaces (and their override materials) stay put.
static func _filtered(mesh: ArrayMesh, hidden: Array) -> ArrayMesh:
	var out := ArrayMesh.new()
	out.resource_name = mesh.resource_name
	out.blend_shape_mode = mesh.blend_shape_mode
	for b in mesh.get_blend_shape_count():
		out.add_blend_shape(mesh.get_blend_shape_name(b))
	for s in mesh.get_surface_count():
		var arrays := mesh.surface_get_arrays(s)
		var flags: Variant = hidden[s]
		if flags != null:
			var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
			var kept := PackedInt32Array()
			for t in (flags as PackedByteArray).size():
				if (flags as PackedByteArray)[t] == 0:
					kept.append(indices[3 * t])
					kept.append(indices[3 * t + 1])
					kept.append(indices[3 * t + 2])
			if kept.is_empty():
				kept = PackedInt32Array([indices[0], indices[0], indices[0]])
			arrays[Mesh.ARRAY_INDEX] = kept
		out.add_surface_from_arrays(mesh.surface_get_primitive_type(s), arrays,
				mesh.surface_get_blend_shape_arrays(s))
		out.surface_set_material(s, mesh.surface_get_material(s))
		out.surface_set_name(s, mesh.surface_get_name(s))
	return out


# Swaps a mesh instance's mesh, keeping its override materials (its colors)
# and blend shape values.
static func _set_mesh(mi: MeshInstance3D, mesh: ArrayMesh) -> void:
	if mi.mesh == mesh:
		return
	var overrides: Array[Material] = []
	for s in mi.get_surface_override_material_count():
		overrides.append(mi.get_surface_override_material(s))
	var blends := PackedFloat32Array()
	if mi.mesh != null:
		for b in mi.mesh.get_blend_shape_count():
			blends.append(mi.get_blend_shape_value(b))
	mi.mesh = mesh
	for s in mini(overrides.size(), mi.get_surface_override_material_count()):
		mi.set_surface_override_material(s, overrides[s])
	for b in mini(blends.size(), mesh.get_blend_shape_count()):
		mi.set_blend_shape_value(b, blends[b])


static func _all_covered(zones: Array, covered: Dictionary) -> bool:
	if zones.is_empty():
		return false
	for zone: String in zones:
		if not covered.has(zone):
			return false
	return true


static func _under_any(node: Node, ancestors: Array) -> bool:
	for a: Variant in ancestors:
		if a is Node and is_instance_valid(a) and (a == node or (a as Node).is_ancestor_of(node)):
			return true
	return false


# Whether something between `node` and `root` is on its way out (a worn
# holder being replaced).
static func _under_freed(node: Node, root: Node) -> bool:
	var at := node.get_parent()
	while at != null and at != root:
		if at.is_queued_for_deletion():
			return true
		at = at.get_parent()
	return false


static func _load(gltf_path: String) -> Dictionary:
	if _data_cache.has(gltf_path):
		return _data_cache[gltf_path]
	var by_node := {}
	var path := gltf_path.get_basename() + ".occlusion.json"
	if FileAccess.file_exists(path):
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
		if parsed is Dictionary:
			for entry: Variant in (parsed as Dictionary).get("meshes", []):
				if entry is Dictionary:
					by_node[String(entry.get("node", ""))] = entry
		else:
			push_warning("unreadable occlusion: " + path)
	_data_cache[gltf_path] = by_node
	return by_node
