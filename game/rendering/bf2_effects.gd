# Draws Battlefront II's bolts, projectile models, muzzle flashes, sparks,
# scorch marks, smoke trails and explosions from bf2tool export-effects
# (res://imported_assets/effects/).
extends RefCounted

const MAX_PARTICLES := 120
const MAX_SCORCHES := 96
const SCORCH_LIFE := 18.0
const SCORCH_FADE := 3.0
const FLASH_MIN_TIME := 0.055
const BOLT_EMISSION := 2.2
const GLOW_EMISSION := 1.4
# Legacy texture names referenced in Battlefront II's fx__ emitters that aren't
# packed as tex_ chunks in ingame.lvl / side/*.lvl.
const TEXTURE_FALLBACKS := {
	"thicksmoke3": "com_sfx_smoke2",
	"boom4": "com_sfx_explosion1",
	"redlaser": "com_sfx_laser_red",
}

var _parent: Node
var _dir := ""
var _effects := {}     # effect name -> parsed Dictionary (or null)
var _textures := {}    # texture name -> Texture2D (or null)
var _models := {}      # model name -> template Node3D (or null)
var _bolt_mesh: ArrayMesh
var _bolt_materials := {}  # key -> StandardMaterial3D
var _scorch_texture: Texture2D
var _puff_texture: Texture2D
var _bursts: Array = []    # [{node, age, duration}]
var _flashes: Array = []   # [{node, light, age, mesh_life, light_life, light_energy}]
var _scorches: Array = []  # [{node, material, age}]
var _trails: Array = []    # detached trails fading out: [{node, age, duration}]


func _init(parent: Node, effects_dir: String) -> void:
	_parent = parent
	_dir = effects_dir
	_bolt_mesh = _build_crossed_beam_mesh()
	_scorch_texture = _build_scorch_texture()
	_puff_texture = _build_puff_texture()


# Duplicates an exported model from effects/models/ (e.g. destroyed droids).
func make_model(model_name: String) -> Node3D:
	var tmpl := _model(model_name)
	return tmpl.duplicate() as Node3D if tmpl != null else null


func update(delta: float) -> void:
	for i in range(_bursts.size() - 1, -1, -1):
		var b: Dictionary = _bursts[i]
		b["age"] = float(b["age"]) + delta
		if float(b["age"]) >= float(b["duration"]):
			var node: Node = b["node"]
			if is_instance_valid(node):
				node.queue_free()
			_bursts.remove_at(i)
	for i in range(_flashes.size() - 1, -1, -1):
		var f: Dictionary = _flashes[i]
		f["age"] = float(f["age"]) + delta
		var age := float(f["age"])
		var node: Node3D = f["node"]
		var light: OmniLight3D = f["light"]
		if is_instance_valid(node):
			node.visible = age < float(f["mesh_life"])
		if is_instance_valid(light):
			var lt := clampf(1.0 - age / maxf(0.01, float(f["light_life"])), 0.0, 1.0)
			light.light_energy = float(f["light_energy"]) * lt
			light.visible = lt > 0.01
		if age >= maxf(float(f["mesh_life"]), float(f["light_life"])):
			if is_instance_valid(node):
				node.queue_free()
			if is_instance_valid(light):
				light.queue_free()
			_flashes.remove_at(i)
	for i in range(_scorches.size() - 1, -1, -1):
		var s: Dictionary = _scorches[i]
		s["age"] = float(s["age"]) + delta
		var age := float(s["age"])
		if age >= SCORCH_LIFE:
			var node: Node = s["node"]
			if is_instance_valid(node):
				node.queue_free()
			_scorches.remove_at(i)
		elif age > SCORCH_LIFE - SCORCH_FADE:
			var mat: StandardMaterial3D = s["material"]
			mat.albedo_color.a = clampf((SCORCH_LIFE - age) / SCORCH_FADE, 0.0, 1.0) * 0.85
	for i in range(_trails.size() - 1, -1, -1):
		var t: Dictionary = _trails[i]
		t["age"] = float(t["age"]) + delta
		if float(t["age"]) >= float(t["duration"]):
			var node: Node = t["node"]
			if is_instance_valid(node):
				node.queue_free()
			_trails.remove_at(i)


func clear() -> void:
	for list: Array in [_bursts, _flashes, _scorches, _trails]:
		for item: Dictionary in list:
			for key: String in ["node", "light"]:
				if item.has(key) and is_instance_valid(item[key]):
					(item[key] as Node).queue_free()
		list.clear()


func dispose() -> void:
	clear()
	for template: Variant in _models.values():
		if template != null and is_instance_valid(template):
			(template as Node).free()
	_models.clear()


# --- bolts & projectile models ----------------------------------------------

# Creates a projectile node: crossed textured laser beam (+ outer glow) for
# bolts, or Battlefront's 3D ordnance model (thermal detonator, missile,
# mortar shell) for explosives.
func make_projectile(look: Dictionary) -> Node3D:
	var kind := str(look.get("kind", "bolt"))
	var model_name := str(look.get("model", ""))
	if kind != "bolt" and model_name != "":
		var tmpl := _model(model_name)
		if tmpl != null:
			var pivot := Node3D.new()
			var mesh_root := tmpl.duplicate() as Node3D
			# Battlefront's missile points along +Z; Godot's looking_at faces -Z.
			if kind == "missile":
				mesh_root.rotation.y = PI
			elif kind == "grenade":
				mesh_root.scale = Vector3.ONE * 1.4
			pivot.add_child(mesh_root)
			pivot.set_meta("is_model", true)
			pivot.set_meta("kind", kind)
			pivot.set_meta("model_name", model_name)
			var light_radius := float(look.get("light_radius", 0.0))
			if light_radius > 0.0:
				var light := OmniLight3D.new()
				light.light_color = look.get("light_color", look.get("color", Color.WHITE))
				light.omni_range = light_radius
				light.light_energy = 1.2
				pivot.add_child(light)
			_parent.add_child(pivot)
			return pivot
	var pivot := Node3D.new()
	var core := MeshInstance3D.new()
	core.name = "Core"
	core.mesh = _bolt_mesh
	core.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	pivot.add_child(core)
	var glow := MeshInstance3D.new()
	glow.name = "Glow"
	glow.mesh = _bolt_mesh
	glow.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	pivot.add_child(glow)
	pivot.set_meta("is_model", false)
	pivot.set_meta("kind", kind)
	style_bolt(pivot, look)
	_parent.add_child(pivot)
	return pivot


func style_bolt(pivot: Node3D, look: Dictionary) -> void:
	if bool(pivot.get_meta("is_model", false)):
		return
	var core := pivot.get_node_or_null("Core") as MeshInstance3D
	var glow := pivot.get_node_or_null("Glow") as MeshInstance3D
	if core == null or glow == null:
		return
	var tex_name := str(look.get("texture", "")).to_lower()
	var color: Color = look.get("color", Color(1.0, 0.25, 0.2))
	var length := float(look.get("length", 1.0))
	var width := float(look.get("width", 0.1))
	var glow_length := float(look.get("glow_length", length * 3.0))
	if length <= 0.0 or width <= 0.0:
		length = 1.0
		width = 0.12
		glow_length = 2.0
	if str(look.get("kind", "bolt")) == "grenade":
		width = 0.22
		length = 0.22
		glow_length = 0.35
	core.material_override = _bolt_material(tex_name, Color.WHITE if tex_name != "" else color, BOLT_EMISSION)
	core.scale = Vector3(width * 2.2, width * 2.2, maxf(length * 1.5, width * 2.5))
	glow.material_override = _bolt_material(tex_name, Color(color.r, color.g, color.b, 0.7), GLOW_EMISSION)
	glow.scale = Vector3(width * 4.4, width * 4.4, maxf(glow_length, length * 2.0))


func place_projectile(pivot: Node3D, position: Vector3, velocity: Vector3, delta: float) -> void:
	pivot.position = position
	var speed2 := velocity.length_squared()
	if bool(pivot.get_meta("is_model", false)):
		if str(pivot.get_meta("kind", "")) == "grenade":
			# Thermal detonators and mortar shells tumble in flight, then rest.
			if speed2 > 0.25:
				pivot.rotate_x(7.5 * delta)
				pivot.rotate_z(5.0 * delta)
		elif speed2 > 0.001:
			var forward := velocity.normalized()
			var up := Vector3.UP if absf(forward.y) < 0.99 else Vector3.RIGHT
			pivot.basis = Basis.looking_at(forward, up)
		return
	if speed2 > 0.0001:
		var forward := velocity.normalized()
		var up := Vector3.UP if absf(forward.y) < 0.99 else Vector3.RIGHT
		pivot.basis = Basis.looking_at(forward, up)


# --- trails -----------------------------------------------------------------

# Attaches a continuous Battlefront trail effect (rocket smoke, grenade glow)
# to a projectile node. Call detach_trail() before freeing the projectile so
# existing smoke puffs finish fading cleanly.
func attach_trail(pivot: Node3D, effect_name: String) -> Node3D:
	var data := _effect(effect_name)
	if data.is_empty():
		return null
	var holder := Node3D.new()
	holder.name = "Trail"
	var max_life := 0.5
	for e: Dictionary in data.get("emitters", []):
		var p := _make_emitter(e, true)
		holder.add_child(p)
		max_life = maxf(max_life, float(e.get("lifetime", 1.0)))
	holder.set_meta("fade_time", max_life + 0.15)
	pivot.add_child(holder)
	return holder


func detach_trail(trail: Node3D) -> void:
	if trail == null or not is_instance_valid(trail):
		return
	var parent_node := trail.get_parent() as Node3D
	var xf := trail.global_transform if trail.is_inside_tree() else (
			parent_node.transform * trail.transform if parent_node != null else trail.transform)
	if parent_node != null:
		parent_node.remove_child(trail)
	_parent.add_child(trail)
	trail.transform = xf
	for child: Node in trail.get_children():
		if child is CPUParticles3D:
			(child as CPUParticles3D).emitting = false
	_trails.append({"node": trail, "age": 0.0, "duration": float(trail.get_meta("fade_time", 1.5))})


# --- muzzle flashes, impacts, explosions, scorches --------------------------

func spawn_muzzle_flash(position: Vector3, direction: Vector3, look: Dictionary) -> void:
	var flash_name := str(look.get("muzzle_flash", ""))
	if flash_name == "":
		return
	var forward := direction.normalized() if direction.length_squared() > 1e-6 else Vector3.FORWARD
	var up := Vector3.UP if absf(forward.y) < 0.99 else Vector3.RIGHT
	var flash_color: Color = look.get("flash_color", Color(1.0, 0.5, 0.35))
	var scale := 0.8 if flash_name.begins_with("med") else 0.55
	var node: Node3D = null
	var tmpl := _model("com_sfx_muzzleflash")
	if tmpl != null:
		node = tmpl.duplicate() as Node3D
		var mat := _bolt_material("com_sfx_meshmuzzleflash", flash_color, 3.0)
		for mesh: MeshInstance3D in node.find_children("*", "MeshInstance3D", true, false):
			mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			for s in mesh.mesh.get_surface_count():
				mesh.set_surface_override_material(s, mat)
		node.position = position
		# com_sfx_muzzleflash extends along +Z (0..1.05 m), so flip +Z -> -Z (forward).
		node.basis = Basis.looking_at(forward, up) * Basis(Vector3.UP, PI) * Basis(Vector3.FORWARD, randf() * TAU) \
				* Basis.from_scale(Vector3.ONE * scale)
		_parent.add_child(node)
	var light: OmniLight3D = null
	var light_radius := float(look.get("flash_light_radius", 2.0))
	var light_life := float(look.get("flash_light_duration", 0.15))
	if light_radius > 0.0 and light_life > 0.0:
		light = OmniLight3D.new()
		light.light_color = look.get("flash_light_color", flash_color)
		light.omni_range = light_radius * 1.5
		light.light_energy = 2.0
		light.position = position + forward * 0.15
		_parent.add_child(light)
	var mesh_life := maxf(FLASH_MIN_TIME, float(look.get("flash_length", 0.025)) * 2.0)
	_flashes.append({"node": node, "light": light, "age": 0.0, "mesh_life": mesh_life,
			"light_life": minf(light_life, 0.18), "light_energy": 2.0})


func spawn_burst(effect_name: String, position: Vector3, normal := Vector3.UP) -> bool:
	var data := _effect(effect_name)
	if data.is_empty():
		return false
	var holder := Node3D.new()
	holder.position = position
	var n := normal.normalized() if normal.length_squared() > 1e-6 else Vector3.UP
	var tangent := Vector3.RIGHT if absf(n.y) > 0.95 else Vector3.UP
	var right := tangent.cross(n).normalized()
	var forward := n.cross(right).normalized()
	holder.basis = Basis(right, n, forward)
	var longest := 0.25
	for e: Dictionary in data.get("emitters", []):
		var p := _make_emitter(e, false)
		holder.add_child(p)
		longest = maxf(longest, float(e.get("start_delay", 0.0)) + float(e.get("lifetime", 1.0)) + 0.15)
	_parent.add_child(holder)
	_bursts.append({"node": holder, "age": 0.0, "duration": longest})
	return true


func spawn_explosion(position: Vector3, normal: Vector3, radius: float, effect_name: String,
		light_color: Color, light_radius: float) -> void:
	var chosen := effect_name if effect_name != "" else "com_sfx_explosion_lg"
	var spawned := spawn_burst(chosen, position, normal)
	if not spawned and chosen != "com_sfx_explosion_lg":
		spawned = spawn_burst("com_sfx_explosion_lg", position, normal)
	var light := OmniLight3D.new()
	light.light_color = light_color
	light.omni_range = maxf(light_radius, radius * 3.0)
	light.light_energy = 6.0
	light.position = position + normal.normalized() * 0.6
	_parent.add_child(light)
	_flashes.append({"node": null, "light": light, "age": 0.0, "mesh_life": 0.0,
			"light_life": 0.55, "light_energy": 6.0})
	spawn_scorch(position, normal, clampf(radius * 0.45, 0.7, 2.8))


func spawn_impact(position: Vector3, normal: Vector3, hit_character: bool, effect_name: String) -> void:
	var chosen := effect_name if effect_name != "" else "com_sfx_ord_exp"
	spawn_burst(chosen, position, normal)
	if not hit_character:
		spawn_scorch(position, normal, 0.28)


func spawn_scorch(position: Vector3, normal: Vector3, radius: float) -> void:
	if _scorches.size() >= MAX_SCORCHES:
		var oldest: Dictionary = _scorches.pop_front()
		if is_instance_valid(oldest["node"]):
			(oldest["node"] as Node).queue_free()
	var n := normal.normalized() if normal.length_squared() > 1e-6 else Vector3.UP
	var tangent := Vector3.RIGHT if absf(n.y) > 0.95 else Vector3.UP
	var right := tangent.cross(n).normalized()
	var forward := n.cross(right).normalized()
	var quad := QuadMesh.new()
	quad.size = Vector2(radius * 2.0, radius * 2.0)
	var mat := StandardMaterial3D.new()
	mat.albedo_texture = _scorch_texture
	mat.albedo_color = Color(1.0, 1.0, 1.0, 0.85)
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
	mat.roughness = 1.0
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	var node := MeshInstance3D.new()
	node.mesh = quad
	node.material_override = mat
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	node.position = position + n * 0.018
	# QuadMesh lies in XY with +Z normal; align +Z to surface normal `n`.
	node.basis = Basis(right, forward, n) * Basis(Vector3.BACK, randf() * TAU)
	_parent.add_child(node)
	_scorches.append({"node": node, "material": mat, "age": 0.0})


# --- internals --------------------------------------------------------------

func _make_emitter(e: Dictionary, continuous: bool) -> CPUParticles3D:
	var particles := CPUParticles3D.new()
	particles.name = String(e.get("name", "emitter")).validate_node_name()
	var life := maxf(0.06, float(e.get("lifetime", 1.0)))
	var max_p := int(e.get("max_particles", 1))
	var is_trail := continuous or max_p < 0
	var burst_delay: Array = e.get("burst_delay", [0.05, 0.05])
	var burst_count: Array = e.get("burst_count", [1, 1])
	var delay_avg := maxf(0.005, (float(burst_delay[0]) + float(burst_delay[1])) * 0.5)
	var count_avg := maxf(1.0, (float(burst_count[0]) + float(burst_count[1])) * 0.5)
	particles.lifetime = life
	particles.local_coords = false
	particles.one_shot = not is_trail
	if is_trail:
		particles.amount = clampi(ceili(life / delay_avg * count_avg), 2, MAX_PARTICLES)
		particles.explosiveness = 0.0
	else:
		particles.amount = clampi(max_p, 1, MAX_PARTICLES)
		particles.explosiveness = 0.92 if delay_avg <= 0.015 else 0.35

	var pos_min: Array = e.get("pos_min", [0, 0, 0])
	var pos_max: Array = e.get("pos_max", [0, 0, 0])
	var pos_scale: Array = e.get("pos_scale", [0, 0])
	var radius := maxf(maxf(absf(float(pos_max[0])), absf(float(pos_max[2]))),
			maxf(absf(float(pos_min[0])), absf(float(pos_min[2])))) * maxf(float(pos_scale[0]), float(pos_scale[1]))
	if radius > 0.02:
		particles.emission_shape = CPUParticles3D.EMISSION_SHAPE_SPHERE
		particles.emission_sphere_radius = radius
	var vel_scale: Array = e.get("vel_scale", [0, 0])
	var vmin := maxf(0.0, float(vel_scale[0]))
	var vmax := maxf(vmin, float(vel_scale[1]))
	particles.direction = Vector3.UP
	var all_around := float(pos_min[1]) < -0.05 and float(pos_max[1]) > 0.05
	particles.spread = 180.0 if all_around else 65.0
	particles.initial_velocity_min = vmin
	particles.initial_velocity_max = vmax

	var accel: Array = e.get("acceleration", [0, 0, 0])
	particles.gravity = Vector3(float(accel[0]), float(accel[1]), float(accel[2]))
	if bool(e.get("damped", false)) and vmax > 0.0:
		particles.damping_min = vmax / life * 0.6
		particles.damping_max = particles.damping_min

	var raw_tex := str(e.get("texture", "")).to_lower()
	var is_legacy_smoke := raw_tex == "thicksmoke3"
	var start_rot: Array = e.get("start_rotation", [0, 360])
	var rot_vel: Array = e.get("rotation_velocity", [0, 0])
	if is_legacy_smoke:
		particles.angle_min = 0.0
		particles.angle_max = 360.0
		particles.angular_velocity_min = -45.0
		particles.angular_velocity_max = 45.0
	else:
		particles.angle_min = float(start_rot[0])
		particles.angle_max = float(start_rot[1])
		particles.angular_velocity_min = float(rot_vel[0])
		particles.angular_velocity_max = float(rot_vel[1])

	var size_range: Array = e.get("size", [0.5, 0.5])
	var smin := maxf(0.02, float(size_range[0]))
	var smax := maxf(smin, float(size_range[1]))
	var size_curve: Array = e.get("size_curve", [[0.0, 1.0]])
	var peak := 1.0
	for pt: Array in size_curve:
		peak = maxf(peak, float(pt[1]))
	var scaled_min := smin * peak
	var scaled_max := smax * peak
	# Battlefront's legacy BlackSmoke layers (thicksmoke3) specify up to 20 m
	# quads; keep billowing explosion smoke puffs natural and well-proportioned.
	if is_legacy_smoke and scaled_max > 6.5:
		var shrink := 6.5 / scaled_max
		scaled_min *= shrink
		scaled_max *= shrink
	particles.scale_amount_min = scaled_min
	particles.scale_amount_max = scaled_max
	if size_curve.size() >= 2:
		var curve := Curve.new()
		for pt: Array in size_curve:
			curve.add_point(Vector2(clampf(float(pt[0]), 0.0, 1.0), clampf(float(pt[1]) / peak, 0.0, 1.0)))
		particles.scale_amount_curve = curve

	var color_curve: Array = e.get("color_curve", [])
	if not color_curve.is_empty():
		var gradient := Gradient.new()
		var offsets := PackedFloat32Array()
		var colors := PackedColorArray()
		for pt: Array in color_curve:
			offsets.append(clampf(float(pt[0]), 0.0, 1.0))
			var alpha := float(pt[4]) * (0.65 if is_legacy_smoke else 1.0)
			colors.append(Color(float(pt[1]), float(pt[2]), float(pt[3]), alpha))
		gradient.offsets = offsets
		gradient.colors = colors
		particles.color_ramp = gradient

	var is_spark := str(e.get("type", "particle")) == "spark"
	var quad := QuadMesh.new()
	if is_spark:
		var stretch := maxf(4.0, float(e.get("spark_length", 0.025)) * 260.0)
		# One not thrown, only rising (a burning vehicle's flames), is no
		# longer than it goes in its spark length, not a long streak.
		if vmax <= 0.5:
			var speed := vmax + particles.gravity.length() * life * 0.5
			var width := maxf(0.02, (scaled_min + scaled_max) * 0.5)
			stretch = clampf(speed * float(e.get("spark_length", 0.025)) / width, 1.0, stretch)
		quad.size = Vector2(1.0, stretch)
		particles.particle_flag_align_y = true
	else:
		quad.size = Vector2(1.0, 1.0)
	quad.material = _particle_material(raw_tex, str(e.get("blend", "alpha")) == "add")
	particles.mesh = quad
	particles.emitting = true
	return particles


func _particle_material(tex_name: String, additive: bool) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	mat.billboard_keep_scale = true
	mat.vertex_color_use_as_albedo = true
	mat.vertex_color_is_srgb = true
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	if additive:
		mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	var resolved: String = TEXTURE_FALLBACKS.get(tex_name.to_lower(), tex_name)
	var tex := _texture(resolved)
	mat.albedo_texture = tex if tex != null else _puff_texture
	return mat


func _bolt_material(tex_name: String, tint: Color, energy: float) -> StandardMaterial3D:
	var key := "%s|%s|%.2f" % [tex_name, tint.to_html(true), energy]
	if _bolt_materials.has(key):
		return _bolt_materials[key]
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	mat.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mat.albedo_color = Color(tint.r * energy, tint.g * energy, tint.b * energy, tint.a)
	var tex := _texture(tex_name)
	if tex != null:
		mat.albedo_texture = tex
	_bolt_materials[key] = mat
	return mat


func _effect(name: String) -> Dictionary:
	var key := name.to_lower()
	if key == "":
		return {}
	if _effects.has(key):
		return _effects[key]
	_effects[key] = {}
	var path := _dir.path_join(key + ".json")
	if not FileAccess.file_exists(path):
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if parsed is Dictionary:
		_effects[key] = parsed
	return _effects[key]


func _texture(name: String) -> Texture2D:
	var key := name.to_lower()
	if key == "":
		return null
	if _textures.has(key):
		return _textures[key]
	_textures[key] = null
	var path := ProjectSettings.globalize_path(_dir.path_join("textures").path_join(key + ".png"))
	if not FileAccess.file_exists(path):
		return null
	var image := Image.load_from_file(path)
	if image == null:
		return null
	image.generate_mipmaps()
	var tex := ImageTexture.create_from_image(image)
	_textures[key] = tex
	return tex


func _model(name: String) -> Node3D:
	var key := name.to_lower()
	if key == "":
		return null
	if _models.has(key):
		return _models[key]
	_models[key] = null
	var path := ProjectSettings.globalize_path(_dir.path_join("models").path_join(key + ".gltf"))
	if not FileAccess.file_exists(path):
		return null
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		return null
	var scene := document.generate_scene(state) as Node3D
	if scene != null:
		_models[key] = scene
	return scene


# Two perpendicular unit quads along Z (-0.5..+0.5): U runs along the bolt's
# length (tail -> nose), matching Battlefront's com_sfx_laser_* textures.
static func _build_crossed_beam_mesh() -> ArrayMesh:
	var tool := SurfaceTool.new()
	tool.begin(Mesh.PRIMITIVE_TRIANGLES)
	var quads := [
		[Vector3(-0.5, 0, -0.5), Vector3(-0.5, 0, 0.5), Vector3(0.5, 0, 0.5), Vector3(0.5, 0, -0.5)],
		[Vector3(0, -0.5, -0.5), Vector3(0, -0.5, 0.5), Vector3(0, 0.5, 0.5), Vector3(0, 0.5, -0.5)],
	]
	var uvs := [Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1)]
	for q: Array in quads:
		for idx: int in [0, 1, 2, 0, 2, 3]:
			tool.set_uv(uvs[idx])
			tool.add_vertex(q[idx])
	return tool.commit()


# Soft charred scorch mark texture (64x64 RGBA) for bolt hits and blasts.
static func _build_scorch_texture() -> ImageTexture:
	var size := 64
	var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
	var half := size * 0.5
	for y in size:
		for x in size:
			var dx := (x + 0.5 - half) / half
			var dy := (y + 0.5 - half) / half
			var r := sqrt(dx * dx + dy * dy)
			var angle := atan2(dy, dx)
			var jag := 0.08 * sin(angle * 7.0) + 0.05 * cos(angle * 13.0 + 1.2)
			var d := clampf(r + jag, 0.0, 1.0)
			var alpha := pow(maxf(0.0, 1.0 - d), 1.8)
			var shade := lerpf(0.04, 0.14, d)
			image.set_pixel(x, y, Color(shade, shade * 0.92, shade * 0.85, alpha))
	image.generate_mipmaps()
	return ImageTexture.create_from_image(image)


# Soft round smoke/puff fallback texture (64x64 RGBA) so a missing particle
# texture never falls back to a hard-edged square quad.
static func _build_puff_texture() -> ImageTexture:
	var size := 64
	var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
	var half := size * 0.5
	for y in size:
		for x in size:
			var dx := (x + 0.5 - half) / half
			var dy := (y + 0.5 - half) / half
			var r := sqrt(dx * dx + dy * dy)
			var angle := atan2(dy, dx)
			var wisp := 0.08 * sin(angle * 5.0 + 0.7) + 0.06 * cos(angle * 3.0 - 0.5)
			var d := clampf(r + wisp * r, 0.0, 1.0)
			var alpha := pow(maxf(0.0, 1.0 - d), 2.2)
			var shade := lerpf(0.95, 0.65, d)
			image.set_pixel(x, y, Color(shade, shade, shade, alpha))
	image.generate_mipmaps()
	return ImageTexture.create_from_image(image)
