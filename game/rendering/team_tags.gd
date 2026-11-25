# Faction holograms over soldiers' heads in a match, in place of their names:
# a small copy of the command post hologram's insignia (the map's own model,
# bf2tool export-map), glowing and flickering as the post's does, turned
# toward the camera and swaying a little either way, blue for your side and
# red for the enemy's. When the map stands between the camera and one (a few
# rays a frame, round-robin), your side's turn grey and faint and the enemy's
# fade away.
#
#   var tags := TeamTags.new(map_dir, hologram_spec)   # a post's "hologram" (objects.json)
#   tags.show(id, model, "empire", friend, alive)       # each frame, per soldier
#   tags.update(camera, delta, raycast)                 # raycast(from, to) -> fraction or < 0
#   tags.forget(id)                                     # gone
#
# In the world they mark people and places by what they're for: a `tint`
# (a shop's green, a quest's yellow, crafting's purple) in place of friend
# and enemy, and a "side"'s symbol may be a flat image (a .png: a glowing
# quad, as Battlefront's command post icon; or "gen:<name>", one of
# PlaceSymbols'). They're drawn on render layer LAYER (pictures of the world
# from overhead leave them out).
extends RefCounted

const PlaceSymbols = preload("res://rendering/place_symbols.gd")

const HEIGHT := 2.25           # m over a soldier's feet
const SIZE_PER_METER := 0.018  # its width, per meter away, so it reads at range
const MIN_SIZE := 0.3          # m
const MAX_SIZE := 1.4
const RAYS_PER_FRAME := 8
const FADE := 8.0              # per second, seen <-> blocked
const SWAY := 0.5              # radians either way it turns from facing the camera
const SWAY_RATE := 0.35        # turns a second
const BRIGHTNESS := 0.75
const FRIEND := Color(0.35, 0.6, 1.0)  # as the game's FRIEND_COLOR, ENEMY_COLOR (net_demo.gd)
const ENEMY := Color(1.0, 0.3, 0.25)
const BLOCKED_FRIEND := Color(0.6, 0.6, 0.62)
const BLOCKED_FRIEND_ALPHA := 0.3
const LAYER := 2  # render layer (its bit) of what's drawn over the world for the player

var _dir := ""
var _images := {}     # side -> its model's file
var _templates := {}  # side -> Node3D (or null: no model)
var _tags := {}       # id -> {pivot, materials, side, friend, alive, seen, blocked}
var _order: Array = []
var _next := 0
var _time := 0.0


func _init(map_dir: String, spec: Dictionary) -> void:
	_dir = map_dir
	_images = spec.get("images", {})


func available() -> bool:
	return not _images.is_empty()


# A soldier's hologram, created the first time (side: "alliance", "empire",
# "republic" or "cis"; none for anything else), `height` over its feet (a
# native's own: an Ewok's is lower).
func show(id: int, model: Node3D, side: String, friend: bool, alive: bool, height := HEIGHT,
		tint := Color(0, 0, 0, 0), scale := 1.0) -> void:
	var tag: Dictionary = _tags.get(id, {})
	if tag.is_empty() or tag["side"] != side or not is_instance_valid(tag["pivot"]):
		forget(id)
		tag = _make(model, side, height)
		if tag.is_empty():
			return
		tag["seen"] = 1.0
		tag["blocked"] = false
		_tags[id] = tag
		_order.append(id)
	tag["friend"] = friend
	tag["alive"] = alive
	tag["tint"] = tint
	tag["scale"] = scale


func forget(id: int) -> void:
	if not _tags.has(id):
		return
	var pivot: Node3D = _tags[id]["pivot"]
	if is_instance_valid(pivot):
		pivot.queue_free()
	_tags.erase(id)
	_order.erase(id)


func update(camera: Camera3D, delta: float, raycast: Callable) -> void:
	_time += delta
	# Tags on what's gone with a world (another planet's, a battle's): forgotten.
	for id: int in _tags.keys():
		if not is_instance_valid(_tags[id]["pivot"]):
			_tags.erase(id)
			_order.erase(id)
	if _order.is_empty():
		_next = 0
	var eye := camera.global_position
	# A few rays a frame: is the map between the camera and the hologram?
	for i in mini(RAYS_PER_FRAME, _order.size()):
		_next = (_next + 1) % _order.size()
		var tag: Dictionary = _tags[_order[_next]]
		var pivot: Node3D = tag["pivot"]
		if pivot.is_inside_tree():
			tag["blocked"] = float(raycast.call(eye, pivot.global_position)) >= 0.0
	var flicker := 0.85 + 0.15 * sin(_time * 23.0) * sin(_time * 7.0)
	for id: int in _tags:
		var tag: Dictionary = _tags[id]
		var pivot: Node3D = tag["pivot"]
		var parent := pivot.get_parent() as Node3D
		pivot.visible = bool(tag["alive"]) and parent != null and parent.visible
		if not pivot.visible:
			continue
		tag["seen"] = move_toward(float(tag["seen"]), 0.0 if tag["blocked"] else 1.0, FADE * delta)
		var seen := float(tag["seen"])
		var color: Color
		var alpha: float
		var tint: Color = tag.get("tint", Color(0, 0, 0, 0))
		if tint.a > 0.0:
			# A place's or person's own color: greyed and faint when hidden.
			color = tint.lerp(BLOCKED_FRIEND, 1.0 - seen)
			alpha = lerpf(BLOCKED_FRIEND_ALPHA, BRIGHTNESS, seen) * tint.a
		elif tag["friend"]:
			color = FRIEND.lerp(BLOCKED_FRIEND, 1.0 - seen)
			alpha = lerpf(BLOCKED_FRIEND_ALPHA, BRIGHTNESS, seen)
		else:
			color = ENEMY
			alpha = BRIGHTNESS * seen
		pivot.visible = alpha > 0.01
		for material: StandardMaterial3D in tag["materials"]:
			material.albedo_color = Color(color, alpha * flicker)
		# Facing the camera, swaying either way; sized by its distance.
		var at := pivot.global_position
		var to_camera := Vector2(eye.x - at.x, eye.z - at.z)
		var facing := atan2(to_camera.x, to_camera.y) + SWAY * sin(_time * SWAY_RATE * TAU + id)
		var size := clampf(at.distance_to(eye) * SIZE_PER_METER, MIN_SIZE, MAX_SIZE) * float(tag.get("scale", 1.0))
		pivot.global_basis = Basis(Vector3.UP, facing).scaled(Vector3.ONE * size / _width(tag))


func _width(tag: Dictionary) -> float:
	return maxf(0.01, float(tag.get("width", 1.0)))


func _make(model: Node3D, side: String, height := HEIGHT) -> Dictionary:
	var template := _template(side)
	if template == null:
		return {}
	var pivot := Node3D.new()
	pivot.name = "team_tag"
	pivot.top_level = true  # turned and sized on its own, not with the soldier
	var image := template.duplicate() as Node3D
	image.visible = true
	pivot.add_child(image)
	var follow := RemoteTransform3D.new()
	follow.position = Vector3.UP * height
	follow.update_rotation = false
	follow.update_scale = false
	model.add_child(follow)
	model.add_child(pivot)
	follow.remote_path = follow.get_path_to(pivot)
	var materials: Array = []
	for mesh: MeshInstance3D in image.find_children("*", "MeshInstance3D", true, false):
		mesh.layers = LAYER
		for surface in mesh.mesh.get_surface_count():
			var glow := (mesh.get_surface_override_material(surface) as StandardMaterial3D).duplicate() as StandardMaterial3D
			mesh.set_surface_override_material(surface, glow)
			materials.append(glow)
	pivot.tree_exiting.connect(follow.queue_free)  # forgotten: its follower goes too
	return {"pivot": pivot, "materials": materials, "side": side, "width": template.get_meta("width", 1.0)}


# The side's insignia, loaded once: glowing, seen through anything (whether
# it's blocked is the rays' to say), centered on its pivot.
func _template(side: String) -> Node3D:
	if _templates.has(side):
		return _templates[side]
	_templates[side] = null
	if not _images.has(side):
		return null
	if str(_images[side]).begins_with("gen:"):
		_templates[side] = _flat_template(PlaceSymbols.texture(str(_images[side]).substr(4)))
		return _templates[side]
	if str(_images[side]).ends_with(".png"):
		var path := _dir.path_join(_images[side])
		_templates[side] = _flat_template(load(path) if ResourceLoader.exists(path) else null)
		return _templates[side]
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(ProjectSettings.globalize_path(_dir.path_join(_images[side])), state) != OK:
		return null
	var scene: Node3D = document.generate_scene(state)
	var bounds := AABB()
	var first := true
	for mesh: MeshInstance3D in scene.find_children("*", "MeshInstance3D", true, false):
		mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		var box := mesh.get_aabb()
		bounds = box if first else bounds.merge(box)
		first = false
		for surface in mesh.mesh.get_surface_count():
			var source := mesh.mesh.surface_get_material(surface) as StandardMaterial3D
			var glow := source.duplicate() as StandardMaterial3D if source != null else StandardMaterial3D.new()
			glow.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			glow.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			glow.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
			glow.cull_mode = BaseMaterial3D.CULL_DISABLED
			glow.no_depth_test = true
			glow.render_priority = 1
			mesh.set_surface_override_material(surface, glow)
	if first:
		return null
	scene.position = -bounds.get_center()
	scene.set_meta("width", maxf(bounds.size.x, bounds.size.y))
	_templates[side] = scene
	return scene


# A flat symbol (an image): a glowing square, a meter across, drawn as the
# models are (over anything, both sides).
func _flat_template(texture: Texture2D) -> Node3D:
	if texture == null:
		return null
	var root := Node3D.new()
	var mesh := MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = Vector2.ONE
	mesh.mesh = quad
	mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var glow := StandardMaterial3D.new()
	glow.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	glow.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	glow.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	glow.cull_mode = BaseMaterial3D.CULL_DISABLED
	glow.no_depth_test = true
	glow.render_priority = 1
	glow.albedo_texture = texture
	mesh.set_surface_override_material(0, glow)
	root.add_child(mesh)
	root.set_meta("width", 1.0)
	return root
