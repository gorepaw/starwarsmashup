# Unified Multi-Scanner (docs/PROFESSIONS.md):
# Projects 3D holographic terrain contour lines and directional peak pings
# in-world without opening a modal survey window.
class_name MultiScanner3D
extends Node3D

const MODES := ["mineral", "chemical", "gas", "flora", "water"]
const MODE_LABELS := {
	"mineral": "Mineral",
	"chemical": "Chemical",
	"gas": "Gas",
	"flora": "Flora",
	"water": "Water",
}
const MODE_COLORS := {
	"mineral": Color(0.3, 0.85, 1.0, 0.85),
	"chemical": Color(0.75, 0.4, 1.0, 0.85),
	"gas": Color(0.95, 0.78, 0.25, 0.85),
	"flora": Color(0.35, 0.95, 0.5, 0.85),
	"water": Color(0.25, 0.6, 1.0, 0.85),
}
const SCAN_RADIUS := 48.0
const GRID_N := 17

var active := false
var mode_index := 0
var extract_cooldown := 0.0
var last_scan: Dictionary = {}

var _mesh_instance: MeshInstance3D
var _mesh: ImmediateMesh
var _mat: StandardMaterial3D
var _rescan_timer := 0.0
var _last_pos := Vector3(1e9, 0, 1e9)
var _last_mode := ""


func _init() -> void:
	_mesh = ImmediateMesh.new()
	_mat = StandardMaterial3D.new()
	_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	_mat.vertex_color_use_as_albedo = true
	_mat.no_depth_test = false
	_mesh_instance = MeshInstance3D.new()
	_mesh_instance.mesh = _mesh
	_mesh_instance.material_override = _mat
	_mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_mesh_instance)


func current_mode() -> String:
	return MODES[posmod(mode_index, MODES.size())]


func current_mode_label() -> String:
	return str(MODE_LABELS.get(current_mode(), "Mineral"))


func toggle() -> bool:
	active = not active
	_mesh_instance.visible = active
	if active:
		_rescan_timer = 0.0
	else:
		last_scan = {}
		_mesh.clear_surfaces()
	return active


func cycle(step: int) -> void:
	mode_index = posmod(mode_index + step, MODES.size())
	_rescan_timer = 0.0


func update_scanner(client: Object, store: Object, player_pos: Vector3, delta: float) -> Dictionary:
	extract_cooldown = maxf(0.0, extract_cooldown - delta)
	if not active or client == null or store == null:
		return {}
	_rescan_timer -= delta
	var mode := current_mode()
	if _rescan_timer <= 0.0 or mode != _last_mode or player_pos.distance_to(_last_pos) > 3.0:
		_rescan_timer = 0.35
		_last_pos = player_pos
		_last_mode = mode
		last_scan = store.scan_terrain(mode, player_pos.x, player_pos.z, SCAN_RADIUS, GRID_N)
		_rebuild_mesh(client, player_pos, last_scan, mode)
	if last_scan.is_empty():
		return {}
	var peak := Vector2(float(last_scan.get("peak_x", player_pos.x)), float(last_scan.get("peak_z", player_pos.z)))
	var dist := Vector2(player_pos.x, player_pos.z).distance_to(peak)
	var conc := float(last_scan.get("concentration", 0.0))
	return {
		"mode": mode,
		"mode_label": current_mode_label(),
		"resource_name": str(last_scan.get("resource_name", "")),
		"concentration": conc,
		"concentration_pct": roundi(conc * 100.0),
		"peak_distance": roundi(dist),
		"cooldown": extract_cooldown,
	}


func _rebuild_mesh(client: Object, origin: Vector3, scan: Dictionary, mode: String) -> void:
	_mesh.clear_surfaces()
	var grid: PackedFloat32Array = scan.get("grid", PackedFloat32Array())
	var n := int(scan.get("grid_n", 0))
	var step := float(scan.get("step", 0.0))
	if n < 2 or grid.size() < n * n or step <= 0.0:
		return
	var base_col: Color = MODE_COLORS.get(mode, Color(0.3, 0.85, 1.0, 0.85))
	_mesh.surface_begin(Mesh.PRIMITIVE_LINES)
	var min_x := origin.x - SCAN_RADIUS
	var min_z := origin.z - SCAN_RADIUS
	# Holographic contour grid draped over terrain heights
	for gz in n:
		for gx in n:
			var idx := gz * n + gx
			var c := float(grid[idx])
			var wx := min_x + gx * step
			var wz := min_z + gz * step
			var wy: float = client.get_ground_height(wx, wz, origin.y + 8.0) + 0.28
			var col := Color(base_col.r, base_col.g, base_col.b, clampf(0.18 + c * 0.72, 0.12, 0.9))
			if gx + 1 < n:
				var nx := min_x + (gx + 1) * step
				var ny: float = client.get_ground_height(nx, wz, origin.y + 8.0) + 0.28
				_mesh.surface_set_color(col)
				_mesh.surface_add_vertex(Vector3(wx, wy, wz))
				_mesh.surface_add_vertex(Vector3(nx, ny, wz))
			if gz + 1 < n:
				var nz := min_z + (gz + 1) * step
				var ny2: float = client.get_ground_height(wx, nz, origin.y + 8.0) + 0.28
				_mesh.surface_set_color(col)
				_mesh.surface_add_vertex(Vector3(wx, wy, wz))
				_mesh.surface_add_vertex(Vector3(wx, ny2, nz))
			# Vertical concentration ping on rich cells
			if c >= 0.55:
				var h := (c - 0.50) * 4.2
				_mesh.surface_set_color(Color(1.0, 0.95, 0.5, 0.85))
				_mesh.surface_add_vertex(Vector3(wx, wy, wz))
				_mesh.surface_add_vertex(Vector3(wx, wy + h, wz))

	# Directional ping ray from player toward the resource peak
	var px := float(scan.get("peak_x", origin.x))
	var pz := float(scan.get("peak_z", origin.z))
	var dir := Vector2(px - origin.x, pz - origin.z)
	if dir.length() > 2.0:
		var dn := dir.normalized()
		var start_p := Vector3(origin.x + dn.x * 1.5, origin.y + 0.55, origin.z + dn.y * 1.5)
		var end_p := Vector3(origin.x + dn.x * minf(12.0, dir.length()), origin.y + 0.55,
				origin.z + dn.y * minf(12.0, dir.length()))
		_mesh.surface_set_color(Color(1.0, 0.9, 0.3, 0.95))
		_mesh.surface_add_vertex(start_p)
		_mesh.surface_add_vertex(end_p)
	_mesh.surface_end()
