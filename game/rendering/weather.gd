# Planetary weather (Stage 11): what falls or blows on a battlefield (or the
# open world) by its planet: Kamino's storm (rain streaks, a darker sky,
# lightning and thunder), Geonosis's blowing sand, Hoth's and
# Mygeeto's snow, Mustafar's ash and embers, Dagobah's and Felucia's mist
# (Dagobah's with a drizzle). Particles fall in a box that follows the
# camera; the ambience (rain hiss, wind, thunder) is made here from noise,
# so it needs no sound files. Looks only: it changes nothing in play.
extends RefCounted

# Per planet code (a Battlefront map's first three letters, or "swg_tat"
# for Galaxies' Tatooine): the kind of weather and how it looks.
const PROFILES := {
	"kam": {"kind": "rain", "amount": 9000, "fall": 22.0, "wind": Vector3(2.5, 0, 1.0), "fog": 0.0035,
			"fog_color": Color(0.42, 0.47, 0.55), "dim": 0.62, "lightning": true, "hiss": 0.55},
	"dag": {"kind": "rain", "amount": 2200, "fall": 14.0, "wind": Vector3(0.4, 0, 0.2), "fog": 0.009,
			"fog_color": Color(0.5, 0.56, 0.5), "dim": 0.85, "lightning": false, "hiss": 0.2},
	"fel": {"kind": "mist", "amount": 900, "fall": -0.15, "wind": Vector3(0.3, 0, 0.1), "fog": 0.006,
			"fog_color": Color(0.55, 0.62, 0.5), "dim": 0.95, "lightning": false, "hiss": 0.0},
	# Tatooine ("tat", "swg_tat") has no weather: the blowing sand was cut.
	"geo": {"kind": "sand", "amount": 3200, "fall": 0.6, "wind": Vector3(9.0, 0, -3.0), "fog": 0.0028,
			"fog_color": Color(0.8, 0.55, 0.38), "dim": 0.92, "lightning": false, "hiss": 0.3},
	"hot": {"kind": "snow", "amount": 4500, "fall": 2.2, "wind": Vector3(6.0, 0, 2.5), "fog": 0.0026,
			"fog_color": Color(0.82, 0.86, 0.92), "dim": 0.95, "lightning": false, "hiss": 0.3},
	"myg": {"kind": "snow", "amount": 1800, "fall": 1.6, "wind": Vector3(2.0, 0, 1.0), "fog": 0.0012,
			"fog_color": Color(0.7, 0.76, 0.85), "dim": 0.95, "lightning": false, "hiss": 0.12},
	"mus": {"kind": "ash", "amount": 2400, "fall": 0.5, "wind": Vector3(1.0, 0, 0.5), "fog": 0.0018,
			"fog_color": Color(0.45, 0.25, 0.18), "dim": 0.9, "lightning": false, "hiss": 0.08},
}

const BOX := Vector3(46, 26, 46)  # m: where particles live, centered on the camera

var _root: Node3D
var _particles: Array[GPUParticles3D] = []
var _profile := {}
var _sun: DirectionalLight3D
var _sun_energy := 1.0
var _environment: Environment
var _flash := 0.0
var _next_flash := 8.0
var _thunder_in := -1.0
var _rumble := 0.0
var _player: AudioStreamPlayer
var _playback: AudioStreamGeneratorPlayback
var _noise_state := [0.0, 0.0, 0.0]
var _gust := 0.0
var _rng := RandomNumberGenerator.new()


# The planet code of a bake: a Battlefront map's ("kam1" -> "kam"), or
# Galaxies' Tatooine.
static func planet_of(bake: String, is_bf2: bool) -> String:
	var name := bake.get_file().to_lower()
	if is_bf2:
		return name.substr(0, 3)
	return "swg_tat" if name.contains("tatooine") or name.contains("tat") else ""


static func has_weather(planet: String) -> bool:
	return PROFILES.has(planet)


func setup(parent: Node3D, planet: String, environment: Environment, sun: DirectionalLight3D,
		audio_parent: Node = null, headless := false) -> void:
	clear()
	if not PROFILES.has(planet) or parent == null:
		return
	_profile = PROFILES[planet]
	_environment = environment
	_sun = sun
	_root = Node3D.new()
	_root.name = "Weather"
	parent.add_child(_root)
	if headless:
		return  # no particles or sound to see or hear
	# The sky: overcast, closer fog.
	if _environment != null and float(_profile["fog"]) > 0.0:
		_environment.fog_enabled = true
		_environment.fog_mode = Environment.FOG_MODE_EXPONENTIAL
		_environment.fog_density = maxf(_environment.fog_density, float(_profile["fog"]))
		_environment.fog_light_color = _profile["fog_color"]
	if _sun != null:
		_sun.light_energy *= float(_profile["dim"])
		_sun_energy = _sun.light_energy
	_add_particles()
	if audio_parent != null and float(_profile["hiss"]) > 0.0:
		_add_ambience(audio_parent)


func clear() -> void:
	if _root != null and is_instance_valid(_root):
		_root.queue_free()
	if _player != null and is_instance_valid(_player):
		_player.queue_free()
	_root = null
	_player = null
	_playback = null
	_particles.clear()
	_profile = {}


func _add_particles() -> void:
	var kind := str(_profile["kind"])
	var layers := 2 if kind in ["rain", "snow"] else 1
	for layer in layers:
		var particles := GPUParticles3D.new()
		particles.amount = int(_profile["amount"]) / layers
		particles.local_coords = false
		particles.lifetime = BOX.y / maxf(1.0, absf(float(_profile["fall"]))) if kind in ["rain", "snow"] else 6.0
		particles.preprocess = particles.lifetime
		particles.visibility_aabb = AABB(-BOX, BOX * 2)
		particles.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		var process := ParticleProcessMaterial.new()
		process.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
		process.emission_box_extents = Vector3(BOX.x * 0.5, BOX.y * 0.5, BOX.z * 0.5)
		var wind: Vector3 = _profile["wind"]
		var fall := float(_profile["fall"]) * (1.0 + 0.25 * layer)
		var velocity := wind + Vector3.DOWN * fall
		process.direction = velocity.normalized() if velocity.length() > 0.01 else Vector3.DOWN
		process.spread = 4.0 if kind == "rain" else 20.0
		process.initial_velocity_min = velocity.length() * 0.85
		process.initial_velocity_max = velocity.length() * 1.15
		process.gravity = Vector3.ZERO
		match kind:
			"ash":
				# Embers drift up and glow; ash flakes tumble down.
				process.gravity = Vector3(0, 0.25, 0)
				process.turbulence_enabled = true
				process.turbulence_noise_strength = 0.6
			"snow", "sand", "mist":
				process.turbulence_enabled = true
				process.turbulence_noise_strength = 0.35 if kind == "snow" else 0.8
		particles.process_material = process
		particles.draw_pass_1 = _particle_mesh(kind)
		particles.position = Vector3.ZERO
		_root.add_child(particles)
		_particles.append(particles)


func _particle_mesh(kind: String) -> Mesh:
	var quad := QuadMesh.new()
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.vertex_color_use_as_albedo = true
	material.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	match kind:
		"rain":
			quad.size = Vector2(0.012, 0.55)
			material.billboard_mode = BaseMaterial3D.BILLBOARD_FIXED_Y
			material.albedo_color = Color(0.72, 0.78, 0.88, 0.35)
		"snow":
			quad.size = Vector2(0.06, 0.06)
			material.albedo_color = Color(1, 1, 1, 0.85)
		"sand":
			quad.size = Vector2(0.04, 0.04)
			material.albedo_color = Color(0.82, 0.66, 0.45, 0.55)
		"ash":
			quad.size = Vector2(0.05, 0.05)
			material.albedo_color = Color(1.0, 0.45, 0.12, 0.9)
			material.emission_enabled = true
			material.emission = Color(1.0, 0.35, 0.08)
			material.emission_energy_multiplier = 2.0
		_:  # mist: big soft motes
			quad.size = Vector2(2.5, 2.5)
			material.albedo_color = Color(0.85, 0.9, 0.85, 0.06)
	quad.material = material
	return quad


func _add_ambience(parent: Node) -> void:
	var stream := AudioStreamGenerator.new()
	stream.mix_rate = 22050.0
	stream.buffer_length = 0.25
	_player = AudioStreamPlayer.new()
	_player.stream = stream
	_player.volume_db = linear_to_db(float(_profile["hiss"]))
	_player.bus = "Master"
	parent.add_child(_player)
	_player.play()
	_playback = _player.get_stream_playback() as AudioStreamGeneratorPlayback


# Each frame: the particles follow the camera; lightning and the ambience.
func update(camera_position: Vector3, delta: float) -> void:
	if _root == null or _profile.is_empty():
		return
	for particles in _particles:
		particles.global_position = camera_position + Vector3.UP * (BOX.y * 0.25)
	if bool(_profile.get("lightning", false)) and _sun != null:
		_next_flash -= delta
		if _next_flash <= 0.0:
			_flash = 1.0
			_next_flash = _rng.randf_range(7.0, 20.0)
			_thunder_in = _rng.randf_range(0.4, 2.5)  # the strike's distance
		_flash = maxf(0.0, _flash - delta * 5.0)
		var flicker := _flash * (0.6 + 0.4 * sin(_flash * 40.0))
		_sun.light_energy = _sun_energy + flicker * 3.0
		if _environment != null:
			_environment.ambient_light_energy = 1.0 + flicker * 1.5
		if _thunder_in >= 0.0:
			_thunder_in -= delta
			if _thunder_in < 0.0:
				_rumble = 1.0
	_gust = 0.6 + 0.4 * sin(Time.get_ticks_msec() * 0.0004) * sin(Time.get_ticks_msec() * 0.00013)
	_fill_audio()


# Rain hisses (bright noise with droplet ticks); wind and sand rush (low,
# gusting noise); thunder rumbles under it.
func _fill_audio() -> void:
	if _playback == null:
		return
	var kind := str(_profile["kind"])
	var frames := _playback.get_frames_available()
	for i in frames:
		var white := _rng.randf_range(-1.0, 1.0)
		# One-pole low-passes: [0] the hiss's body, [1] the wind's rush, [2] thunder.
		_noise_state[0] += (white - _noise_state[0]) * (0.55 if kind == "rain" else 0.25)
		_noise_state[1] += (white - _noise_state[1]) * 0.02
		_noise_state[2] += (white - _noise_state[2]) * 0.004
		var sample := 0.0
		if kind == "rain":
			sample = _noise_state[0] * 0.6 + _noise_state[1] * 0.8
			if _rng.randf() < 0.0015:
				sample += _rng.randf_range(-0.5, 0.5)  # a drop on something near
		else:
			sample = _noise_state[1] * 2.2 * _gust + _noise_state[0] * 0.08
		if _rumble > 0.0:
			sample += _noise_state[2] * 14.0 * _rumble
			_rumble = maxf(0.0, _rumble - 1.0 / (22050.0 * 3.5))
		sample = clampf(sample, -1.0, 1.0)
		_playback.push_frame(Vector2(sample, sample))
