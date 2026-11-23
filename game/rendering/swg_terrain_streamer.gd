# Streams baked SWG terrain in 512 m chunks around the player with 5 distance LODs
# and skirts to prevent cracks between neighboring LOD levels.
class_name SwgTerrainStreamer
extends Node3D

const TerrainTool = preload("res://tools/render_terrain.gd")

const CHUNK_CELLS := 128
const HYSTERESIS := 64.0
const UNLOAD_DISTANCE := 14500.0

# Each LOD: [max_distance, step, skirt_depth]
const LOD_LEVELS := [
	[750.0, 1, 4.0],
	[1800.0, 2, 10.0],
	[4000.0, 4, 22.0],
	[8500.0, 8, 44.0],
	[14500.0, 16, 80.0],
]

var _terrain: Object
var _material: Material
var _samples := 0
var _spacing := 4.0
var _origin := Vector2.ZERO
var _grid_chunks := 0
var _chunks := {}  # Vector2i -> {"node": MeshInstance3D, "lod": int, "center": Vector2}
var _last_focus := Vector2(INF, INF)


func setup(bake_prefix: String, meta: Dictionary) -> bool:
	if not ClassDB.class_exists("SwmTerrain"):
		printerr("SwmTerrain GDExtension class not registered")
		return false
	_terrain = ClassDB.instantiate("SwmTerrain")
	if _terrain.load_bake(ProjectSettings.globalize_path("res://" + bake_prefix) if not bake_prefix.is_absolute_path() else bake_prefix) != OK:
		return false
	_samples = int(_terrain.get_samples())
	_spacing = float(_terrain.get_spacing())
	_origin = _terrain.get_origin()
	_grid_chunks = (_samples - 1 + CHUNK_CELLS - 1) / CHUNK_CELLS
	_material = TerrainTool.build_material(bake_prefix, meta)
	return true


func get_terrain() -> Object:
	return _terrain


func get_material() -> Material:
	return _material


func active_chunk_count() -> int:
	return _chunks.size()


func lod_counts() -> Dictionary:
	var counts := {}
	for entry: Dictionary in _chunks.values():
		var lod := int(entry["lod"])
		counts[lod] = int(counts.get(lod, 0)) + 1
	return counts


func warm_around(focus: Vector3) -> void:
	if _terrain == null:
		return
	var focus_xz := Vector2(focus.x, focus.z)
	_last_focus = focus_xz
	for cz in _grid_chunks:
		for cx in _grid_chunks:
			var key := Vector2i(cx, cz)
			var center := _chunk_center(cx, cz)
			var dist := focus_xz.distance_to(center)
			var lod := _target_lod(dist, -1)
			if lod == 0 or lod == 1:
				_build_chunk(key, lod, center)


func update_focus(focus: Vector3, max_builds := 8) -> void:
	if _terrain == null:
		return
	var focus_xz := Vector2(focus.x, focus.z)
	if _last_focus.x != INF and focus_xz.distance_to(_last_focus) < 8.0 and _chunks.size() > 0:
		# Still allow queued chunks to stream in when standing still
		pass
	_last_focus = focus_xz

	var to_unload: Array[Vector2i] = []
	for key: Vector2i in _chunks:
		var entry: Dictionary = _chunks[key]
		var dist := focus_xz.distance_to(entry["center"])
		if dist > UNLOAD_DISTANCE + HYSTERESIS:
			to_unload.append(key)
	for key in to_unload:
		var node: MeshInstance3D = _chunks[key]["node"]
		if is_instance_valid(node):
			node.queue_free()
		_chunks.erase(key)

	var pending: Array = []
	for cz in _grid_chunks:
		for cx in _grid_chunks:
			var key := Vector2i(cx, cz)
			var center := _chunk_center(cx, cz)
			var dist := focus_xz.distance_to(center)
			var cur_lod := int(_chunks[key]["lod"]) if _chunks.has(key) else -1
			var want_lod := _target_lod(dist, cur_lod)
			if want_lod >= 0 and want_lod != cur_lod:
				pending.append({"key": key, "lod": want_lod, "center": center, "dist": dist})

	if pending.is_empty():
		return
	pending.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return float(a["dist"]) < float(b["dist"]))
	var limit := mini(max_builds, pending.size())
	for i in limit:
		var item: Dictionary = pending[i]
		_build_chunk(item["key"], int(item["lod"]), item["center"])


func _chunk_center(cx: int, cz: int) -> Vector2:
	var c0 := cx * CHUNK_CELLS
	var r0 := cz * CHUNK_CELLS
	var c1 := mini(_samples - 1, c0 + CHUNK_CELLS)
	var r1 := mini(_samples - 1, r0 + CHUNK_CELLS)
	var mid_c := (c0 + c1) * 0.5
	var mid_r := (r0 + r1) * 0.5
	return Vector2(-(_origin.x + mid_c * _spacing), _origin.y + mid_r * _spacing)


func _target_lod(dist: float, current_lod: int) -> int:
	if dist > UNLOAD_DISTANCE + (HYSTERESIS if current_lod >= 0 else 0.0):
		return -1
	for lod in LOD_LEVELS.size():
		var threshold := float(LOD_LEVELS[lod][0])
		if current_lod >= 0:
			if current_lod == lod:
				return lod if dist <= threshold + HYSTERESIS else lod + 1
			if current_lod > lod and dist <= threshold - HYSTERESIS:
				return lod
			if current_lod < lod and dist <= threshold:
				return lod
		else:
			if dist <= threshold:
				return lod
	return LOD_LEVELS.size() - 1


func _build_chunk(key: Vector2i, lod: int, center: Vector2) -> void:
	var spec: Array = LOD_LEVELS[lod]
	var step := int(spec[1])
	var skirt := float(spec[2])
	var c0 := key.x * CHUNK_CELLS
	var r0 := key.y * CHUNK_CELLS
	var mesh: ArrayMesh = _terrain.build_chunk(c0, r0, CHUNK_CELLS, step, skirt)
	if mesh == null:
		return
	if _chunks.has(key):
		var entry: Dictionary = _chunks[key]
		var node: MeshInstance3D = entry["node"]
		node.mesh = mesh
		entry["lod"] = lod
	else:
		var node := MeshInstance3D.new()
		node.name = "Chunk_%d_%d" % [key.x, key.y]
		node.mesh = mesh
		node.material_override = _material
		add_child(node)
		_chunks[key] = {"node": node, "lod": lod, "center": center}
