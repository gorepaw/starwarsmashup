# Draws a Galaxies particle effect (steam, smoke, a campfire) as swgtool
# describes it (tools/swg/particle_export.h): each emitter a CPUParticles3D,
# its particles rising (straight up, spread by its angle) from a disc of its
# radius, at its rate, living their lifetime, under its share of gravity
# (below 0, they rise), in its colors and alpha over their life, growing
# by its size curve, its texture a sprite sheet if it's one, alpha blended
# or added. A particle's speed over its life is drawn as its average (no
# curve for it here). Already running when it's made (preprocessed).
extends RefCounted

const MAX_PARTICLES := 400  # per emitter


# A node holding the effect's emitters, or null if it can't be read.
static func load_effect(path: String) -> Node3D:
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not parsed is Dictionary:
		printerr("cannot read ", path)
		return null
	var effect := Node3D.new()
	effect.name = path.get_file().get_basename()
	var dir := path.get_base_dir()
	for emitter: Dictionary in parsed.get("emitters", []):
		effect.add_child(_emitter(emitter, dir))
	return effect


static func _emitter(e: Dictionary, dir: String) -> CPUParticles3D:
	var particles := CPUParticles3D.new()
	particles.name = String(e.get("name", "emitter")).validate_node_name()
	var life: Array = e.get("lifetime", [1, 1])
	var low := maxf(0.05, float(life[0]))
	var high := maxf(low, float(life[1]))
	particles.lifetime = high
	particles.lifetime_randomness = 1.0 - low / high
	particles.amount = clampi(ceili(float(e.get("rate", 1)) * high), 1, MAX_PARTICLES)
	particles.preprocess = high
	particles.local_coords = false
	var radius := float(e.get("radius", 0))
	if radius > 0:
		particles.emission_shape = CPUParticles3D.EMISSION_SHAPE_RING
		particles.emission_ring_axis = Vector3.UP
		particles.emission_ring_radius = radius
		particles.emission_ring_inner_radius = 0.0
		particles.emission_ring_height = 0.0
	particles.direction = Vector3.UP
	particles.spread = clampf(float(e.get("spread", 0)), 0, 180)
	var speed := float(e.get("speed", 0)) * _mean(e.get("speed_scale", []), 1.0)
	particles.initial_velocity_min = speed
	particles.initial_velocity_max = speed
	particles.gravity = Vector3(0, -9.8 * float(e.get("gravity", 0)), 0)
	particles.color_ramp = _ramp(e.get("color", []), e.get("alpha", []))

	# Size over life: the curve's largest is the particle's scale.
	var size: Array = e.get("size", [])
	var largest := 0.0
	for point: Array in size:
		largest = maxf(largest, float(point[1]))
	if largest > 0:
		particles.scale_amount_min = largest
		particles.scale_amount_max = largest
		var curve := Curve.new()
		for point: Array in size:
			curve.add_point(Vector2(clampf(float(point[0]), 0, 1), float(point[1]) / largest))
		particles.scale_amount_curve = curve

	var quad := QuadMesh.new()
	quad.material = _material(e, dir)
	particles.mesh = quad
	var frames := int(e.get("frames", 1))
	var fps := float(e.get("fps", -1))
	if frames > 1 and fps > 0:
		# Godot's speed 1 plays the sheet once over a particle's life.
		particles.anim_speed_min = fps * high / frames
		particles.anim_speed_max = particles.anim_speed_min
	return particles


static func _material(e: Dictionary, dir: String) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	material.vertex_color_use_as_albedo = true
	material.vertex_color_is_srgb = true
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	if String(e.get("blend", "alpha")) == "add":
		material.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	var grid := int(e.get("grid", 1))
	material.particles_anim_h_frames = grid
	material.particles_anim_v_frames = grid
	material.particles_anim_loop = false
	var texture := String(e.get("texture", ""))
	if texture != "":
		var image := Image.new()
		var bytes := FileAccess.get_file_as_bytes(dir.path_join(texture))
		if not bytes.is_empty() and image.load_png_from_buffer(bytes) == OK:
			image.generate_mipmaps()
			material.albedo_texture = ImageTexture.create_from_image(image)
	return material


# Colors (t, r, g, b) and alphas (t, a) over a particle's life, as one
# gradient: a stop wherever either has a point.
static func _ramp(colors: Array, alphas: Array) -> Gradient:
	var times: Array[float] = []
	for point: Array in colors + alphas:
		var t := clampf(float(point[0]), 0, 1)
		if not times.has(t):
			times.append(t)
	times.sort()
	var gradient := Gradient.new()
	if times.is_empty():
		return gradient
	var offsets := PackedFloat32Array()
	var stops := PackedColorArray()
	for t in times:
		var rgb := _sample(colors, t, [1.0, 1.0, 1.0])
		var a: float = _sample(alphas, t, [1.0])[0]
		offsets.append(t)
		stops.append(Color(rgb[0], rgb[1], rgb[2], a))
	gradient.offsets = offsets
	gradient.colors = stops
	return gradient


# A curve's values at t (linear between its points; [t, values...] each).
static func _sample(points: Array, t: float, fallback: Array) -> Array:
	if points.is_empty():
		return fallback
	var before: Array = points[0]
	if t <= float(before[0]):
		return before.slice(1)
	for point: Array in points:
		if float(point[0]) >= t:
			var span := float(point[0]) - float(before[0])
			var f := 0.0 if span <= 0 else (t - float(before[0])) / span
			var out := []
			for i in range(1, point.size()):
				out.append(lerpf(float(before[i]), float(point[i]), f))
			return out
		before = point
	return (points[points.size() - 1] as Array).slice(1)


static func _mean(points: Array, fallback: float) -> float:
	if points.is_empty():
		return fallback
	var sum := 0.0
	for point: Array in points:
		sum += float(point[1])
	return sum / points.size()
