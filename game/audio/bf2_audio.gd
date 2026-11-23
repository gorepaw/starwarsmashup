# Battlefront II audio manager: plays exported WAVs from
# res://imported_assets/audio/sounds.json (bf2tool export-audio) on three
# AudioServer buses: SFX, Music, Voice.
#
#   var audio := Bf2Audio.new(root, "res://imported_assets/audio")
#   audio.apply_volumes(settings)
#   audio.play_2d("imp_weap_inf_rifle_fire")
#   audio.play_3d("imp_weap_ord_exp", pos, camera.global_position)
#   audio.queue_vo("imp_off_com_report_captured_commandpost")
#   audio.set_music("imp_dag_amb_start")
extends RefCounted

const SFX_POOL_3D := 32
const SFX_POOL_2D := 12
const DEDUP_WINDOW := 0.035
const MUSIC_FADE_SECONDS := 1.5
const MAX_VO_QUEUE := 2

var _root: Node
var _dir := ""
var _sounds: Dictionary = {}
var _wav_cache: Dictionary = {}  # rel_path -> AudioStreamWAV
var _players_3d: Array[AudioStreamPlayer3D] = []
var _players_3d_age: PackedFloat64Array = PackedFloat64Array()
var _players_2d: Array[AudioStreamPlayer] = []
var _players_2d_idx := 0
var _vo_player: AudioStreamPlayer
var _vo_queue: Array[String] = []
var _music_players: Array[AudioStreamPlayer] = []
var _music_active := 0
var _music_cue := ""
var _music_loop := true
var _music_fade_t := 0.0
var _music_fading := false
var _music_stopping := false
var _pending_layers: Array[Dictionary] = []
var _recent_plays: Dictionary = {}  # name -> elapsed time
var _elapsed := 0.0


func _init(root: Node, dir: String) -> void:
	_root = root
	_dir = dir
	_ensure_buses()
	var json_path := dir.path_join("sounds.json")
	if FileAccess.file_exists(json_path):
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(json_path))
		if parsed is Dictionary:
			_sounds = parsed.get("sounds", {})
	for i in SFX_POOL_3D:
		var p := AudioStreamPlayer3D.new()
		p.bus = &"SFX"
		p.attenuation_model = AudioStreamPlayer3D.ATTENUATION_INVERSE_DISTANCE
		p.attenuation_filter_cutoff_hz = 20500.0
		_root.add_child(p)
		_players_3d.append(p)
		_players_3d_age.append(-1.0)
	for i in SFX_POOL_2D:
		var p := AudioStreamPlayer.new()
		p.bus = &"SFX"
		_root.add_child(p)
		_players_2d.append(p)
	_vo_player = AudioStreamPlayer.new()
	_vo_player.bus = &"Voice"
	_root.add_child(_vo_player)
	for i in 2:
		var m := AudioStreamPlayer.new()
		m.bus = &"Music"
		m.volume_db = -80.0
		_root.add_child(m)
		_music_players.append(m)


static func _ensure_buses() -> void:
	for bus_name: String in ["SFX", "Music", "Voice"]:
		if AudioServer.get_bus_index(bus_name) < 0:
			var idx := AudioServer.bus_count
			AudioServer.add_bus(idx)
			AudioServer.set_bus_name(idx, bus_name)
			AudioServer.set_bus_send(idx, "Master")


func apply_volumes(settings: RefCounted) -> void:
	if settings == null:
		return
	_set_bus_linear("Master", float(settings.get("master_volume")))
	_set_bus_linear("SFX", float(settings.get("sfx_volume")))
	_set_bus_linear("Music", float(settings.get("music_volume")))
	_set_bus_linear("Voice", float(settings.get("voice_volume")))


static func _set_bus_linear(bus_name: String, value: float) -> void:
	var idx := AudioServer.get_bus_index(bus_name)
	if idx < 0:
		return
	var v := clampf(value, 0.0, 1.0)
	AudioServer.set_bus_mute(idx, v <= 0.001)
	AudioServer.set_bus_volume_db(idx, linear_to_db(maxf(v, 0.0001)))


func has_sound(sound_name: String) -> bool:
	return _sounds.has(sound_name.to_lower())


func play_2d(sound_name: String, volume_scale := 1.0) -> void:
	var key := sound_name.to_lower()
	if key == "" or not _sounds.has(key):
		return
	var spec: Dictionary = _sounds[key]
	if str(spec.get("kind", "")) == "layered":
		_play_layered(spec, false, Vector3.ZERO, Vector3.ZERO, volume_scale)
		return
	if not _allow_play(key, spec):
		return
	var stream := _pick_stream(spec, true)
	if stream == null:
		return
	var player := _next_2d_player()
	player.stream = stream
	player.volume_db = _gain_to_db(spec, volume_scale)
	player.pitch_scale = _random_pitch(spec)
	player.play()


func play_3d(sound_name: String, pos: Vector3, camera_pos: Vector3, volume_scale := 1.0) -> void:
	var key := sound_name.to_lower()
	if key == "" or not _sounds.has(key):
		return
	var spec: Dictionary = _sounds[key]
	if str(spec.get("kind", "")) == "layered":
		_play_layered(spec, true, pos, camera_pos, volume_scale)
		return
	if not bool(spec.get("mode_3d", true)):
		play_2d(key, volume_scale)
		return
	var max_dist := maxf(20.0, float(spec.get("max_distance", 100.0)))
	if pos.distance_squared_to(camera_pos) > max_dist * max_dist:
		return
	if not _allow_play(key, spec):
		return
	var stream := _pick_stream(spec, true)
	if stream == null:
		return
	var player := _acquire_3d_player(pos, camera_pos)
	player.stream = stream
	player.global_position = pos
	player.unit_size = maxf(2.0, float(spec.get("min_distance", 10.0)) * 0.6)
	player.max_distance = max_dist
	player.volume_db = _gain_to_db(spec, volume_scale)
	player.pitch_scale = _random_pitch(spec)
	player.play()


func queue_vo(sound_name: String, urgent := false) -> void:
	var key := sound_name.to_lower()
	if key == "" or not _sounds.has(key):
		return
	if urgent:
		_vo_queue.clear()
		_vo_player.stop()
		_play_vo_now(key)
		return
	if _vo_queue.has(key):
		return
	if _vo_queue.size() >= MAX_VO_QUEUE:
		return
	if not _vo_player.playing and _vo_queue.is_empty():
		_play_vo_now(key)
	else:
		_vo_queue.append(key)


func set_music(cue_name: String, loop := true) -> void:
	var key := cue_name.to_lower()
	if key == "" or not _sounds.has(key):
		return
	if key == _music_cue and not _music_stopping:
		return
	_music_cue = key
	_music_loop = loop
	_music_stopping = false
	_start_music_clip(key)


func stop_music(fade := MUSIC_FADE_SECONDS) -> void:
	if _music_cue == "" and not _music_players[0].playing and not _music_players[1].playing:
		return
	_music_cue = ""
	_music_stopping = true
	_music_fading = true
	_music_fade_t = 0.0 if fade <= 0.0 else 0.001
	if fade <= 0.0:
		for m in _music_players:
			m.stop()
			m.volume_db = -80.0
		_music_fading = false
		_music_stopping = false


func update(delta: float, camera_pos: Vector3) -> void:
	_elapsed += delta
	# Delayed layered sub-sounds
	for i in range(_pending_layers.size() - 1, -1, -1):
		var item: Dictionary = _pending_layers[i]
		item["delay"] = float(item["delay"]) - delta
		if float(item["delay"]) <= 0.0:
			var layer: Dictionary = item["layer"]
			if bool(item["is_3d"]):
				_play_layer_3d(layer, item["pos"], camera_pos, float(item["volume"]))
			else:
				_play_layer_2d(layer, float(item["volume"]))
			_pending_layers.remove_at(i)
	# Voice-over queue
	if not _vo_player.playing and not _vo_queue.is_empty():
		var next_vo: String = _vo_queue.pop_front()
		_play_vo_now(next_vo)
	# Music crossfade & track chaining
	if _music_fading:
		_music_fade_t = minf(1.0, _music_fade_t + delta / MUSIC_FADE_SECONDS)
		var curr := _music_players[_music_active]
		var prev := _music_players[1 - _music_active]
		if _music_stopping:
			curr.volume_db = lerpf(0.0, -80.0, _music_fade_t)
			prev.volume_db = -80.0
			if _music_fade_t >= 1.0:
				curr.stop()
				prev.stop()
				_music_fading = false
				_music_stopping = false
		else:
			var target_db := _gain_to_db(_sounds.get(_music_cue, {}), 1.0)
			curr.volume_db = lerpf(-40.0, target_db, _music_fade_t)
			prev.volume_db = lerpf(target_db, -80.0, _music_fade_t)
			if _music_fade_t >= 1.0:
				prev.stop()
				_music_fading = false
	elif _music_cue != "" and _music_loop and not _music_players[_music_active].playing:
		_start_music_clip(_music_cue)


func clear() -> void:
	_pending_layers.clear()
	_vo_queue.clear()
	_recent_plays.clear()
	_vo_player.stop()
	for p in _players_3d:
		p.stop()
	for p in _players_2d:
		p.stop()
	stop_music(0.5)


func dispose() -> void:
	clear()
	for p in _players_3d:
		if is_instance_valid(p):
			p.queue_free()
	_players_3d.clear()
	for p in _players_2d:
		if is_instance_valid(p):
			p.queue_free()
	_players_2d.clear()
	if is_instance_valid(_vo_player):
		_vo_player.queue_free()
	for m in _music_players:
		if is_instance_valid(m):
			m.queue_free()
	_music_players.clear()
	_wav_cache.clear()


func _play_layered(spec: Dictionary, is_3d: bool, pos: Vector3, camera_pos: Vector3, volume_scale: float) -> void:
	for layer: Dictionary in spec.get("layers", []):
		var delay := float(layer.get("delay", 0.0))
		if delay > 0.001:
			_pending_layers.append({"delay": delay, "layer": layer, "is_3d": is_3d,
					"pos": pos, "volume": volume_scale})
		elif is_3d:
			_play_layer_3d(layer, pos, camera_pos, volume_scale)
		else:
			_play_layer_2d(layer, volume_scale)


func _play_layer_2d(layer: Dictionary, volume_scale: float) -> void:
	var stream := _pick_stream(layer, true)
	if stream == null:
		return
	var p := _next_2d_player()
	p.stream = stream
	p.volume_db = _gain_to_db(layer, volume_scale)
	p.pitch_scale = _random_pitch(layer)
	p.play()


func _play_layer_3d(layer: Dictionary, pos: Vector3, camera_pos: Vector3, volume_scale: float) -> void:
	if not bool(layer.get("mode_3d", true)):
		_play_layer_2d(layer, volume_scale)
		return
	var max_dist := maxf(20.0, float(layer.get("max_distance", 100.0)))
	if pos.distance_squared_to(camera_pos) > max_dist * max_dist:
		return
	var stream := _pick_stream(layer, true)
	if stream == null:
		return
	var p := _acquire_3d_player(pos, camera_pos)
	p.stream = stream
	p.global_position = pos
	p.unit_size = maxf(2.0, float(layer.get("min_distance", 10.0)) * 0.6)
	p.max_distance = max_dist
	p.volume_db = _gain_to_db(layer, volume_scale)
	p.pitch_scale = _random_pitch(layer)
	p.play()


func _play_vo_now(key: String) -> void:
	var spec: Dictionary = _sounds.get(key, {})
	if spec.is_empty():
		return
	var stream := _pick_stream(spec, true)
	if stream == null:
		return
	_vo_player.stream = stream
	_vo_player.volume_db = _gain_to_db(spec, 1.0)
	_vo_player.pitch_scale = 1.0
	_vo_player.play()


func _start_music_clip(key: String) -> void:
	var spec: Dictionary = _sounds.get(key, {})
	if spec.is_empty():
		return
	var stream := _pick_stream(spec, false)
	if stream == null:
		return
	var next_idx := 1 - _music_active
	var next_p := _music_players[next_idx]
	next_p.stream = stream
	next_p.volume_db = -40.0
	next_p.play()
	_music_active = next_idx
	_music_fading = true
	_music_fade_t = 0.0


func _allow_play(key: String, spec: Dictionary) -> bool:
	var prob := float(spec.get("play_probability", 1.0))
	if prob < 1.0 and randf() > prob:
		return false
	var last := float(_recent_plays.get(key, -1.0))
	if last >= 0.0 and _elapsed - last < DEDUP_WINDOW:
		return false
	_recent_plays[key] = _elapsed
	return true


func _next_2d_player() -> AudioStreamPlayer:
	for p in _players_2d:
		if not p.playing:
			return p
	var p := _players_2d[_players_2d_idx]
	_players_2d_idx = (_players_2d_idx + 1) % _players_2d.size()
	return p


func _acquire_3d_player(pos: Vector3, camera_pos: Vector3) -> AudioStreamPlayer3D:
	for i in _players_3d.size():
		if not _players_3d[i].playing:
			_players_3d_age[i] = _elapsed
			return _players_3d[i]
	# Voice stealing: replace farthest from camera (tie-break oldest).
	var best_idx := 0
	var best_score := -1.0
	for i in _players_3d.size():
		var dist2 := _players_3d[i].global_position.distance_squared_to(camera_pos)
		var age := _elapsed - _players_3d_age[i]
		var score := dist2 + age * 100.0
		if score > best_score:
			best_score = score
			best_idx = i
	_players_3d_age[best_idx] = _elapsed
	return _players_3d[best_idx]


func _pick_stream(spec: Dictionary, cache: bool) -> AudioStreamWAV:
	var clips: Array = spec.get("clips", [])
	if clips.is_empty():
		return null
	var total := 0.0
	for c: Dictionary in clips:
		total += maxf(0.01, float(c.get("weight", 1.0)))
	var pick := randf() * total
	var chosen := str(clips[0]["file"])
	for c: Dictionary in clips:
		pick -= maxf(0.01, float(c.get("weight", 1.0)))
		if pick <= 0.0:
			chosen = str(c["file"])
			break
	if cache and _wav_cache.has(chosen):
		return _wav_cache[chosen]
	var abs_path := ProjectSettings.globalize_path(_dir.path_join(chosen))
	if not FileAccess.file_exists(abs_path):
		return null
	var wav := AudioStreamWAV.load_from_file(abs_path)
	if wav != null and cache:
		_wav_cache[chosen] = wav
	return wav


static func _gain_to_db(spec: Dictionary, volume_scale: float) -> float:
	var gain := float(spec.get("gain", 1.0))
	var dev := float(spec.get("gain_dev", 0.0))
	if dev > 0.0:
		gain += randf_range(-dev, dev)
	return linear_to_db(clampf(gain * volume_scale, 0.001, 2.0))


static func _random_pitch(spec: Dictionary) -> float:
	var pitch := float(spec.get("pitch", 1.0))
	var dev := float(spec.get("pitch_dev", 0.0))
	if dev > 0.0:
		pitch += randf_range(-dev, dev)
	return clampf(pitch, 0.25, 4.0)
