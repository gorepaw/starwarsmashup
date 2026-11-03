# Dev tool: walks a character into a Galaxies building (swgtool export-building)
# with physics, and saves screenshots along the way.
#
#   godot --path game --script res://tools/walk_building.gd -- <building.gltf> <output_prefix> <character.gltf>
#         [--route <cell>,<cell>...] [--place <x> <y> <z> <yaw>] [--terrain <bake_prefix>]
#
# The character stands on the building's floors and is stopped by the walls
# exported from the floors' uncrossable edges, as in Galaxies. The floors also
# serve as its navigation mesh: Galaxies floors are already convex triangles
# joined at shared edges, with doorways open to the next cell's floor. It
# starts outside the entrance and runs to the middle of each --route cell in
# turn (default: the cell with the most portals). --place puts the building
# where the world snapshot has it (SWG position and yaw in degrees, as printed
# by `swgtool snapshot`). --terrain adds that baked terrain around it.
#
# Interior cells are lit only by their own Galaxies lights
# (rendering/swg_cell.gdshader). Indoors, the character is lit by its current
# cell's lights instead of the sun. While the camera is inside, the terrain is
# hidden (Galaxies only shows the outside through portals) and the ambient
# light is the cell's.
#
# Writes numbered images: <output_prefix>01_outside.png, one per cell entered,
# and a final one. Prints the cells visited. Exits 1 if the character got stuck.
# Needs a real window: rendering doesn't run in --headless mode.
extends SceneTree

const TerrainTool = preload("res://tools/render_terrain.gd")
const CELL_SHADER = preload("res://rendering/swg_cell.gdshader")

const WORLD_LAYERS := 1      # visual layers: terrain and buildings
const CHARACTER_LAYERS := 2  # visual layers: the character
const SOLID := 1             # collision layer: floors, walls, terrain
const CELL_FLOOR := 2        # collision layer: building floors (for finding the current cell)
const WALL := 4              # collision layer: building walls (for path clearance)
const NAV_CELL_SIZE := 0.05  # navigation vertex snapping, in meters
const CLEARANCE := 0.35      # path smoothing keeps this far from walls
const MAX_RIG_LIGHTS := 7    # Godot renders at most 8 directional lights; one is the sun
const MAX_CELL_LIGHTS := 8   # per kind; matches the shader
const RUN_SPEED := 4.5
const GRAVITY := 9.8
const ARRIVE_DISTANCE := 0.35
const STUCK_SECONDS := 2.5
const RUN_ANIMATION := "human_rifle_stand_runforward"
const IDLE_ANIMATION := "human_rifle_stand_idle_emote_full"
const OUTDOOR_AMBIENT := Color(0.5, 0.5, 0.55)
const HOLD_FRAMES := 30  # physics frames to stand still for the establishing shot

var _prefix := ""
var _route_names := PackedStringArray()
var _layout: Dictionary = {}
var _building: Node3D
var _terrain: MeshInstance3D
var _body: CharacterBody3D
var _model: Node3D
var _player: AnimationPlayer
var _camera: Camera3D
var _sun: DirectionalLight3D
var _environment: Environment
var _rig: Array[DirectionalLight3D] = []
var _cell_omnis: Dictionary = {}  # cell index -> Array of OmniLight3D
var _cell_ambient: Dictionary = {}  # cell index -> Color
var _map: RID
var _regions: Array[RID] = []
var _waypoints: Array[Vector3] = []
var _waypoint := 0
var _physics_frames := 0
var _hold := 0
var _stuck_time := 0.0
var _best_distance := INF
var _cell := -2
var _visited := PackedStringArray()
var _facing := Vector3.BACK
var _pending_shot := ""
var _shot_delay := 0
var _shots := 0
var _finished := false
var _exit_code := 0


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 3:
		printerr("usage: -- <building.gltf> <output_prefix> <character.gltf> [--route a,b] ",
				"[--place x y z yaw] [--terrain prefix]")
		quit(2)
		return
	_prefix = args[1]
	var layout: Variant = JSON.parse_string(FileAccess.get_file_as_string(args[0].get_basename() + ".json"))
	_building = _load_gltf(args[0])
	if layout == null or _building == null:
		printerr("cannot load ", args[0], " and its .json sidecar")
		quit(1)
		return
	_layout = layout
	root.add_child(_building)
	_map = root.get_world_3d().navigation_map
	NavigationServer3D.map_set_cell_size(_map, NAV_CELL_SIZE)
	NavigationServer3D.map_set_cell_height(_map, NAV_CELL_SIZE)

	var route := _option(args, "--route", 1)
	if route.size() == 1:
		_route_names = route[0].split(",")
	var place := _option(args, "--place", 4)
	if place.size() == 4:
		# SWG is left-handed: mirror X, which also reverses the yaw.
		_building.position = Vector3(-float(place[0]), float(place[1]), float(place[2]))
		_building.rotation.y = deg_to_rad(-float(place[3]))
	_setup_cells()
	var terrain := _option(args, "--terrain", 1)
	if terrain.size() == 1:
		_add_terrain(terrain[0])
	if not _setup_character(args[2]):
		quit(1)
		return
	_setup_scene()
	DisplayServer.window_set_size(Vector2i(1280, 720))


func _physics_process(delta: float) -> bool:
	_physics_frames += 1
	if _finished:
		return false
	if _waypoints.is_empty():
		# Region updates reach the navigation map asynchronously; wait for them.
		if _physics_frames < 3 or not _navigation_ready():
			if _physics_frames > 300:
				printerr("navigation map never became ready")
				_finish(false)
			return false
		if not _build_route():
			_finish(false)
		_hold = HOLD_FRAMES
		return false
	if _hold > 0:
		_hold -= 1
		return false

	var to_target := _waypoints[_waypoint] - _body.global_position
	to_target.y = 0
	var distance := to_target.length()
	if distance < ARRIVE_DISTANCE:
		_waypoint += 1
		_stuck_time = 0
		_best_distance = INF
		if _waypoint >= _waypoints.size():
			_finish(true)
		return false
	if distance < _best_distance - 0.02:
		_best_distance = distance
		_stuck_time = 0
	else:
		_stuck_time += delta
		if _stuck_time > STUCK_SECONDS:
			print("stuck before waypoint ", _waypoint, " at ", _body.global_position)
			_finish(false)
			return false

	var direction := to_target / distance
	_facing = _facing.lerp(direction, minf(1.0, delta * 10.0)).normalized()
	_model.rotation.y = atan2(_facing.x, _facing.z)  # the model faces +Z
	var velocity := direction * RUN_SPEED
	velocity.y = _body.velocity.y - GRAVITY * delta
	_body.velocity = velocity
	_body.move_and_slide()

	var cell := _cell_at(_body.global_position)
	if cell != _cell:
		_enter_cell(cell)
	_update_camera(delta)
	return false


func _finalize() -> void:
	for region in _regions:
		NavigationServer3D.free_rid(region)


func _process(_delta: float) -> bool:
	if _pending_shot.is_empty():
		return false
	_shot_delay -= 1
	if _shot_delay > 0:
		return false
	var path := _prefix + _pending_shot + ".png"
	root.get_texture().get_image().save_png(path)
	print("wrote ", path)
	_pending_shot = ""
	if _finished:
		quit(_exit_code)
		return true
	return false


# --- setup -------------------------------------------------------------------

func _load_gltf(path: String) -> Node3D:
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		return null
	return document.generate_scene(state) as Node3D


func _option(args: PackedStringArray, flag: String, count: int) -> PackedStringArray:
	var i := args.find(flag)
	if i < 0 or i + count >= args.size():
		return PackedStringArray()
	return args.slice(i + 1, i + 1 + count)


func _cells() -> Array:
	return _layout["cells"]


func _setup_cells() -> void:
	var cells := _cells()
	for i in cells.size():
		var cell: Dictionary = cells[i]
		var mesh := _building.find_child(cell["name"] + "_mesh", true, false) as MeshInstance3D
		if mesh != null and i > 0:
			_apply_cell_lighting(mesh, cell)
		var floor_mesh := _building.find_child(cell["name"] + "_floor", true, false) as MeshInstance3D
		if floor_mesh != null:
			_add_collision(floor_mesh, SOLID | CELL_FLOOR, i)
			_add_navigation(floor_mesh)
		var walls := _building.find_child(cell["name"] + "_walls", true, false) as MeshInstance3D
		if walls != null:
			_add_collision(walls, SOLID | WALL, i)
		var ambient := Vector3.ZERO
		for light: Dictionary in cell["lights"]:
			if light["kind"] == "ambient":
				ambient += _vec3(light["color"])
		_cell_ambient[i] = Color(ambient.x, ambient.y, ambient.z)


# Replaces each surface's material with the cell shader and the cell's lights.
func _apply_cell_lighting(mesh: MeshInstance3D, cell: Dictionary) -> void:
	var ambient := Vector3.ZERO
	var parallel_direction := PackedVector3Array()
	var parallel_color := PackedVector3Array()
	var point_position := PackedVector3Array()
	var point_color := PackedVector3Array()
	var point_attenuation := PackedVector3Array()
	for light: Dictionary in cell["lights"]:
		var color := _vec3(light["color"])
		match light["kind"]:
			"ambient":
				ambient += color
			"parallel":
				if parallel_direction.size() < MAX_CELL_LIGHTS:
					parallel_direction.append(_vec3(light["direction"]))
					parallel_color.append(color)
			"point":
				if point_position.size() < MAX_CELL_LIGHTS:
					point_position.append(_vec3(light["position"]))
					point_color.append(color)
					point_attenuation.append(_vec3(light["attenuation"]))
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


# One navigation region per floor. Each triangle becomes a polygon, wound
# clockwise seen from above as Godot expects.
func _add_navigation(floor_mesh: MeshInstance3D) -> void:
	var faces := floor_mesh.mesh.get_faces()
	var navigation := NavigationMesh.new()
	navigation.cell_size = NAV_CELL_SIZE
	navigation.cell_height = NAV_CELL_SIZE
	var vertices := PackedVector3Array()
	var index_of := {}
	var polygons: Array[PackedInt32Array] = []
	for i in range(0, faces.size(), 3):
		var corners := [faces[i], faces[i + 1], faces[i + 2]]
		if (corners[1] - corners[0]).cross(corners[2] - corners[0]).y < 0:
			corners = [corners[0], corners[2], corners[1]]
		var polygon := PackedInt32Array()
		for corner: Vector3 in corners:
			var key := (corner / NAV_CELL_SIZE).round()
			if not index_of.has(key):
				index_of[key] = vertices.size()
				vertices.append(corner)
			polygon.append(index_of[key])
		polygons.append(polygon)
	navigation.vertices = vertices
	for polygon in polygons:
		navigation.add_polygon(polygon)
	var region := NavigationServer3D.region_create()
	NavigationServer3D.region_set_map(region, _map)
	NavigationServer3D.region_set_transform(region, _building_space(floor_mesh))
	NavigationServer3D.region_set_navigation_mesh(region, navigation)
	_regions.append(region)


# A node's transform in world space while the tree is still being built.
func _building_space(node: Node3D) -> Transform3D:
	var transform := Transform3D.IDENTITY
	var current: Node = node
	while current is Node3D:
		transform = (current as Node3D).transform * transform
		current = current.get_parent()
	return transform


func _add_collision(mesh: MeshInstance3D, layers: int, cell: int) -> void:
	var shape := ConcavePolygonShape3D.new()
	shape.set_faces(mesh.mesh.get_faces())
	shape.backface_collision = true
	var collider := CollisionShape3D.new()
	collider.shape = shape
	var body := StaticBody3D.new()
	body.collision_layer = layers
	body.collision_mask = 0
	body.set_meta("cell", cell)
	body.add_child(collider)
	mesh.add_child(body)
	mesh.visible = false


func _add_terrain(prefix: String) -> void:
	var meta: Variant = JSON.parse_string(FileAccess.get_file_as_string(prefix + ".json"))
	var heights := FileAccess.get_file_as_bytes(prefix + ".r32").to_float32_array()
	if meta == null:
		printerr("cannot load terrain ", prefix)
		return
	var samples: int = meta["samples"]
	var spacing: float = meta["spacing"]
	var origin := Vector2(meta["origin_x"], meta["origin_z"])
	_terrain = MeshInstance3D.new()
	_terrain.mesh = TerrainTool.build_mesh(heights, samples, spacing, origin, 2)
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.78, 0.66, 0.47)
	material.roughness = 0.95
	_terrain.material_override = material
	root.add_child(_terrain)

	# Collision only near the building: 160 m square at the bake's spacing.
	var center_column := int(round((-_building.position.x - origin.x) / spacing))
	var center_row := int(round((_building.position.z - origin.y) / spacing))
	var point := func(column: int, row: int) -> Vector3:
		return Vector3(-(origin.x + column * spacing), heights[row * samples + column], origin.y + row * spacing)
	var faces := PackedVector3Array()
	for row in range(maxi(center_row - 20, 0), mini(center_row + 20, samples - 1)):
		for column in range(maxi(center_column - 20, 0), mini(center_column + 20, samples - 1)):
			var a: Vector3 = point.call(column, row)
			var b: Vector3 = point.call(column + 1, row)
			var c: Vector3 = point.call(column, row + 1)
			var d: Vector3 = point.call(column + 1, row + 1)
			faces.append_array(PackedVector3Array([a, c, b, b, c, d]))
	var shape := ConcavePolygonShape3D.new()
	shape.set_faces(faces)
	shape.backface_collision = true
	var collider := CollisionShape3D.new()
	collider.shape = shape
	var body := StaticBody3D.new()
	body.collision_layer = SOLID
	body.collision_mask = 0
	body.add_child(collider)
	root.add_child(body)


func _setup_character(path: String) -> bool:
	_model = _load_gltf(path)
	if _model == null:
		printerr("cannot load ", path)
		return false
	_body = CharacterBody3D.new()
	_body.collision_layer = 0
	_body.collision_mask = SOLID
	_body.floor_snap_length = 0.6
	_body.floor_max_angle = deg_to_rad(50)
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.3
	capsule.height = 1.8
	var collider := CollisionShape3D.new()
	collider.shape = capsule
	collider.position.y = 0.9
	_body.add_child(collider)
	_body.add_child(_model)
	_set_layers(_model, CHARACTER_LAYERS)
	root.add_child(_body)

	_player = _model.find_child("AnimationPlayer", true, false) as AnimationPlayer
	if _player != null:
		for animation_name in [RUN_ANIMATION, IDLE_ANIMATION]:
			if _player.has_animation(animation_name):
				_player.get_animation(animation_name).loop_mode = Animation.LOOP_LINEAR
		if _player.has_animation(RUN_ANIMATION):
			_player.play(RUN_ANIMATION)
	return true


func _set_layers(node: Node, layers: int) -> void:
	if node is VisualInstance3D:
		(node as VisualInstance3D).layers = layers
	for child in node.get_children():
		_set_layers(child, layers)


func _setup_scene() -> void:
	_environment = Environment.new()
	_environment.background_mode = Environment.BG_COLOR
	_environment.background_color = Color(0.62, 0.72, 0.85)
	_environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	_environment.ambient_light_color = OUTDOOR_AMBIENT
	var world := WorldEnvironment.new()
	world.environment = _environment
	root.add_child(world)

	_sun = DirectionalLight3D.new()
	_sun.rotation_degrees = Vector3(-50, -40, 0)
	_sun.light_energy = 1.3
	_sun.shadow_enabled = true
	_sun.light_cull_mask = WORLD_LAYERS | CHARACTER_LAYERS
	root.add_child(_sun)

	# The character's indoor lights: its current cell's parallel lights.
	for _i in MAX_RIG_LIGHTS:
		var light := DirectionalLight3D.new()
		light.light_cull_mask = CHARACTER_LAYERS
		light.visible = false
		root.add_child(light)
		_rig.append(light)

	# Point lights light the character only while it's in their cell.
	var cells := _cells()
	for i in cells.size():
		var omnis: Array = []
		for light: Dictionary in cells[i]["lights"]:
			if light["kind"] != "point":
				continue
			var color := _vec3(light["color"])
			var omni := OmniLight3D.new()
			omni.position = _building.transform * _vec3(light["position"])
			var strength := maxf(color.x, maxf(color.y, color.z))
			omni.light_color = Color(color.x / strength, color.y / strength, color.z / strength)
			omni.light_energy = strength
			omni.omni_range = 8.0
			omni.light_cull_mask = CHARACTER_LAYERS
			omni.visible = false
			root.add_child(omni)
			omnis.append(omni)
		_cell_omnis[i] = omnis

	_camera = Camera3D.new()
	_camera.fov = 60
	_camera.near = 0.05
	_camera.far = 20000
	root.add_child(_camera)


# --- route -------------------------------------------------------------------

# Waypoints: outside the entrance, then both sides of every portal on the way
# through the route's cells, then the middle of the last cell's floor.
func _build_route() -> bool:
	var cells := _cells()
	var targets: Array[int] = []
	for route_name in _route_names:
		var index := -1
		for i in cells.size():
			if cells[i]["name"] == route_name:
				index = i
		if index < 0:
			printerr("no cell named '", route_name, "'")
			return false
		targets.append(index)
	if targets.is_empty():
		var best := 1
		for i in range(1, cells.size()):
			if cells[i]["portals"].size() > cells[best]["portals"].size():
				best = i
		targets.append(best)

	var path: Array[int] = [0]
	for target in targets:
		var hop := _cell_path(path[-1], target)
		if hop.is_empty():
			printerr("no way from cell ", path[-1], " to cell ", target)
			return false
		path.append_array(hop.slice(1))

	# Start 5 m outside the first doorway, then navigate to each route cell.
	var entrance := _portal_crossing(path[0], path[1])
	if entrance.is_empty():
		printerr("no portal from cell ", path[0], " to cell ", path[1])
		return false
	_waypoints.append(entrance[0] + (entrance[0] - entrance[1]).normalized() * 5.0)
	_waypoints.append(entrance[0])
	for target in targets:
		# The middle of the floor, or the nearest walkable point to it (a room's
		# centroid can fall inside furniture, which is cut out of the floor).
		var goal := NavigationServer3D.map_get_closest_point(_map, _floor_center(target))
		var points := _navigation_path(_waypoints[-1], goal)
		if points.is_empty() or points[points.size() - 1].distance_to(goal) > 1.0:
			printerr("no navigation path to cell ", cells[target]["name"])
			return false
		_waypoints.append_array(_smooth(points).slice(1))

	var names := PackedStringArray()
	for cell in path:
		names.append(cells[cell]["name"])
	print("route: ", " -> ".join(names), " (", _waypoints.size(), " waypoints)")

	var ground := _ray(_waypoints[0] + Vector3.UP * 20.0, _waypoints[0] + Vector3.DOWN * 40.0, SOLID)
	_body.global_position = (ground["position"] if not ground.is_empty() else _waypoints[0]) + Vector3.UP * 0.05
	_facing = (_waypoints[1] - _waypoints[0]).normalized()
	_facing.y = 0
	_facing = _facing.normalized()
	_model.rotation.y = atan2(_facing.x, _facing.z)

	# Establishing shot from farther back.
	var head := _body.global_position + Vector3.UP * 1.6
	_camera.look_at_from_position(head - _facing * 9.0 + Vector3.UP * 3.0, head + _facing * 4.0)
	_request_shot("outside", 20)
	return true


func _cell_path(from: int, to: int) -> Array[int]:
	var cells := _cells()
	var previous := {from: -1}
	var queue: Array[int] = [from]
	while not queue.is_empty():
		var cell: int = queue.pop_front()
		if cell == to:
			var path: Array[int] = []
			while cell != -1:
				path.push_front(cell)
				cell = previous[cell]
			return path
		for portal: Dictionary in cells[cell]["portals"]:
			var next: int = portal["target_cell"]
			if portal["passable"] and not previous.has(next):
				previous[next] = cell
				queue.append(next)
	return []


# [a point 1 m on the `from` side of the shared portal, one 1 m on the `to` side]
func _portal_crossing(from: int, to: int) -> Array[Vector3]:
	var portal_index := -1
	for portal: Dictionary in _cells()[from]["portals"]:
		if portal["target_cell"] == to:
			portal_index = portal["portal"]
	if portal_index < 0:
		return []
	var polygon: Array = _layout["portals"][portal_index]
	var center := Vector3.ZERO
	var bottom := INF
	var normal := Vector3.ZERO
	for i in polygon.size():
		var a := _building.global_transform * _vec3(polygon[i])
		var b := _building.global_transform * _vec3(polygon[(i + 1) % polygon.size()])
		center += a
		bottom = minf(bottom, a.y)
		normal += Vector3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y))
	center /= polygon.size()
	normal.y = 0
	normal = normal.normalized()
	var base := Vector3(center.x, bottom, center.z)
	var plus := base + normal
	var minus := base - normal
	if _same_cell(_cell_at(plus + Vector3.UP), to):
		return [minus, plus]
	if _same_cell(_cell_at(minus + Vector3.UP), to):
		return [plus, minus]
	if _same_cell(_cell_at(minus + Vector3.UP), from):
		return [minus, plus]
	return [plus, minus]


func _navigation_ready() -> bool:
	var probe := _floor_center(1)
	return NavigationServer3D.map_get_closest_point(_map, probe).distance_to(probe) < 1.0


func _navigation_path(from: Vector3, to: Vector3) -> PackedVector3Array:
	var query := NavigationPathQueryParameters3D.new()
	query.map = _map
	query.start_position = from
	query.target_position = to
	query.path_postprocessing = NavigationPathQueryParameters3D.PATH_POSTPROCESSING_EDGECENTERED
	var result := NavigationPathQueryResult3D.new()
	NavigationServer3D.query_path(query, result)
	return result.path


# Drops path points that can be skipped without passing within CLEARANCE of
# a wall.
func _smooth(points: PackedVector3Array) -> Array[Vector3]:
	var smoothed: Array[Vector3] = [points[0]]
	var i := 0
	while i < points.size() - 1:
		var j := points.size() - 1
		while j > i + 1 and not _clear(points[i], points[j]):
			j -= 1
		smoothed.append(points[j])
		i = j
	return smoothed


func _clear(from: Vector3, to: Vector3) -> bool:
	var sphere := SphereShape3D.new()
	sphere.radius = CLEARANCE
	var query := PhysicsShapeQueryParameters3D.new()
	query.shape = sphere
	query.transform = Transform3D(Basis.IDENTITY, from + Vector3.UP)
	query.motion = to - from
	query.collision_mask = WALL
	var space := root.get_world_3d().direct_space_state
	return space.intersect_shape(query, 1).is_empty() and space.cast_motion(query)[0] >= 1.0


func _floor_center(cell: int) -> Vector3:
	var floor_mesh := _building.find_child(_cells()[cell]["name"] + "_floor", true, false) as MeshInstance3D
	var faces := floor_mesh.mesh.get_faces()
	var sum := Vector3.ZERO
	for v in faces:
		sum += v
	return floor_mesh.global_transform * (sum / faces.size())


# Cell whose floor is under `point`; -1 outside any floor. Cell 0 (the
# exterior) counts as outside.
func _cell_at(point: Vector3) -> int:
	var hit := _ray(point + Vector3.UP * 0.5, point + Vector3.DOWN * 3.0, CELL_FLOOR)
	if hit.is_empty():
		return -1
	var cell: int = hit["collider"].get_meta("cell", -1)
	return -1 if cell == 0 else cell


func _same_cell(a: int, b: int) -> bool:
	return maxi(a, 0) == maxi(b, 0)


func _ray(from: Vector3, to: Vector3, mask: int) -> Dictionary:
	var query := PhysicsRayQueryParameters3D.create(from, to, mask)
	return root.get_world_3d().direct_space_state.intersect_ray(query)


# --- presentation ------------------------------------------------------------

func _enter_cell(cell: int) -> void:
	for omnis: Array in _cell_omnis.values():
		for omni: OmniLight3D in omnis:
			omni.visible = false
	for light in _rig:
		light.visible = false
	if cell < 0:
		_sun.light_cull_mask = WORLD_LAYERS | CHARACTER_LAYERS
	else:
		_sun.light_cull_mask = WORLD_LAYERS
		var used := 0
		for light: Dictionary in _cells()[cell]["lights"]:
			var color := _vec3(light["color"])
			if light["kind"] == "parallel" and used < _rig.size():
				var rig := _rig[used]
				var direction := (_building.global_basis * _vec3(light["direction"])).normalized()
				var up := Vector3.UP if absf(direction.y) < 0.99 else Vector3.RIGHT
				rig.global_basis = Basis.looking_at(direction, up)
				rig.light_color = Color(color.x, color.y, color.z)
				rig.visible = true
				used += 1
		for omni: OmniLight3D in _cell_omnis.get(cell, []):
			omni.visible = true

	var cell_name: String = "outside" if cell < 0 else _cells()[cell]["name"]
	if _cell != -2:
		print("entered ", cell_name)
	_cell = cell
	if cell >= 0:
		_visited.append(cell_name)
		_request_shot(cell_name, 25)


func _update_camera(delta: float) -> void:
	var head := _body.global_position + Vector3.UP * 1.6
	var desired := head - _facing * 3.2 + Vector3.UP * 0.5
	var hit := _ray(head, desired, SOLID)
	if not hit.is_empty():
		var hit_position: Vector3 = hit["position"]
		desired = hit_position + (head - hit_position).normalized() * 0.25
	var position := _camera.global_position.lerp(desired, minf(1.0, delta * 6.0))
	_camera.look_at_from_position(position, head)
	var camera_cell := _cell_at(position)
	_environment.ambient_light_color = OUTDOOR_AMBIENT if camera_cell < 0 else _cell_ambient[camera_cell]
	if _terrain != null:
		_terrain.visible = camera_cell < 0


func _request_shot(label: String, delay_frames: int) -> void:
	_shots += 1
	_pending_shot = "%02d_%s" % [_shots, label]
	_shot_delay = delay_frames


func _finish(success: bool) -> void:
	_finished = true
	_exit_code = 0 if success else 1
	if _player != null and _player.has_animation(IDLE_ANIMATION):
		_player.play(IDLE_ANIMATION)
	print("visited: ", ", ".join(_visited))
	print("route ", "completed" if success else "NOT completed")
	_request_shot("end" if success else "stuck", 10)


func _vec3(values: Array) -> Vector3:
	return Vector3(values[0], values[1], values[2])
