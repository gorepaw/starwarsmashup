# The open world's sound, from Galaxies: each planet's ambience (its land's
# loop: desert wind, jungle, swamp...), a town's own loop near its travel
# point, and now and then one of the planet's pieces of music, as Galaxies
# played them, with quiet between. Matches have Battlefront's sound instead:
# this fades out while one's on.
#
# Its files are Galaxies' (game/audio/swg_audio_files.txt), extracted by
# tools/import_swg_audio.ps1 to res://imported_assets/audio/swg/. Without
# them it stays silent.
#
#   var world_audio := WorldAudio.new(root)
#   world_audio.update(delta, "tatooine", client, in_match)   # each frame
extends RefCounted

const DIR := "res://imported_assets/audio/swg"
# Each planet's land (its loop away from towns) and its music.
const PLANETS := {
	"tatooine": {"land": "amb_desert_lp.wav", "town": "amb_tat_city_small_lp.wav",
			"music": ["mus_theme_tatooine.mp3", "mus_desert_amb_a.mp3", "mus_desert_amb_b.mp3", "mus_sunset_a.mp3"]},
	"naboo": {"land": "amb_grass_lp.wav",
			"music": ["mus_theme_naboo.mp3", "grasslands_mus_lp.mp3", "mus_explore_a.mp3", "mus_sunrise_a.mp3"]},
	"corellia": {"land": "amb_grass_lp.wav",
			"music": ["mus_theme_corellia.mp3", "mus_explore_a.mp3", "grasslands_mus_lp.mp3", "mus_theme_generic_a.mp3"]},
	"talus": {"land": "amb_forest_lp.wav",
			"music": ["mus_theme_generic_a.mp3", "mus_theme_generic_c.mp3", "mus_explore_a.mp3"]},
	"rori": {"land": "amb_swamp_lp.wav",
			"music": ["mus_swamp_amb_a.mp3", "mus_swamp_amb_b.mp3", "mus_theme_generic_b.mp3"]},
	"dantooine": {"land": "amb_grass_lp.wav",
			"music": ["grasslands_mus_lp.mp3", "mus_theme_generic_b.mp3", "mus_sunset_a.mp3"]},
	"lok": {"land": "amb_whistling_wind_lp.wav",
			"music": ["mus_gloom_a.mp3", "mus_desert_amb_b.mp3", "mus_theme_generic_c.mp3"]},
	"yavin4": {"land": "amb_jungle_lp.wav",
			"music": ["mus_jungle_amb_a.mp3", "mus_jungle_amb_b.mp3", "mus_theme_generic_a.mp3"]},
	"endor": {"land": "amb_forest_lp.wav",
			"music": ["mus_theme_endor.mp3", "mus_theme_ewoks.mp3", "mus_explore_a.mp3"]},
	"dathomir": {"land": "amb_swamp_lp.wav",
			"music": ["mus_theme_dathomir.mp3", "mus_gloom_b.mp3", "mus_gloom_a.mp3"]},
}
# A town's loop: amb_<planet>_<its name>_lp.wav, or the name Galaxies gave
# its loop where ours differs; a town with none has the generic one.
const TOWN_NAMES := {
	"tatooine": {"bestine": "bestine_township"},
	"lok": {"nyms_stronghold": "nym_stronghold", "scrapyard_landing": "outpost"},
	"yavin4": {"imperial_outpost": "imperial_base", "labor_outpost": "outpost_2"},
	"endor": {"research_outpost": "outpost2"},
	"dathomir": {"science_outpost": "outpost_2"},
	"dantooine": {"agro_outpost": "outpost2"},
}
const GENERIC_TOWN := "amb_city_small_lp.wav"
const TOWN_RADIUS := 420.0         # m from a town's travel point that it sounds like the town
const LAND_DB := -9.0              # the land's loop, under everything
const TOWN_DB := -7.0
const MUSIC_DB := -5.0
const FADE_SECONDS := 3.0
const FIRST_MUSIC := Vector2(12.0, 30.0)   # s after arriving before the first piece
const MUSIC_GAP := Vector2(60.0, 150.0)    # s of quiet between pieces
const TOWN_CHECK_SECONDS := 1.0

var _root: Node
var _amb: Array[AudioStreamPlayer] = []  # two, crossfading
var _amb_active := 0
var _amb_file := ""
var _music: AudioStreamPlayer
var _music_wait := -1.0  # s until the next piece (-1: not counting yet)
var _music_last := ""
var _planet := ""
var _town_file := ""
var _town_check := 0.0
var _cache: Dictionary = {}  # file -> AudioStream (or null: missing)
var _silenced := false


func _init(root: Node) -> void:
	_root = root
	for i in 2:
		var p := AudioStreamPlayer.new()
		p.bus = &"SFX" if AudioServer.get_bus_index("SFX") >= 0 else &"Master"
		p.volume_db = -80.0
		_root.add_child(p)
		_amb.append(p)
	_music = AudioStreamPlayer.new()
	_music.bus = &"Music" if AudioServer.get_bus_index("Music") >= 0 else &"Master"
	_music.volume_db = MUSIC_DB
	_root.add_child(_music)


func dispose() -> void:
	for p: AudioStreamPlayer in _amb:
		p.queue_free()
	_amb.clear()
	if _music != null:
		_music.queue_free()
		_music = null


func has_files() -> bool:
	return DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(DIR))


# Each frame: `planet` its bake's name ("tatooine"), `client` for the towns'
# travel points, `in_match` while a match's sound plays instead.
func update(delta: float, planet: String, client: Object, in_match: bool) -> void:
	if _music == null:
		return
	planet = planet.to_lower()
	if in_match or not PLANETS.has(planet):
		_silence(delta)
		return
	if planet != _planet:
		_planet = planet
		_town_file = ""
		_town_check = 0.0
		_music_wait = randf_range(FIRST_MUSIC.x, FIRST_MUSIC.y)
		if _music.playing:
			_music.stop()
	_silenced = false
	_town_check -= delta
	if _town_check <= 0.0:
		_town_check = TOWN_CHECK_SECONDS
		_town_file = _town_loop(planet, client)
	var spec: Dictionary = PLANETS[planet]
	var wanted := _town_file if _town_file != "" else str(spec.get("land", ""))
	_update_ambience(delta, wanted, TOWN_DB if _town_file != "" else LAND_DB)
	_update_music(delta, spec.get("music", []))


func _silence(delta: float) -> void:
	for p: AudioStreamPlayer in _amb:
		if p.playing:
			p.volume_db = move_toward(p.volume_db, -80.0, 80.0 / FADE_SECONDS * delta)
			if p.volume_db <= -79.0:
				p.stop()
	if _music.playing:
		_music.volume_db = move_toward(_music.volume_db, -80.0, 80.0 / FADE_SECONDS * delta)
		if _music.volume_db <= -79.0:
			_music.stop()
	if not _silenced:
		_silenced = true
		_amb_file = ""
		_planet = ""


# The loop for the town whose travel point is nearest, within TOWN_RADIUS;
# "" out in the land.
func _town_loop(planet: String, client: Object) -> String:
	if client == null or not client.is_spawned():
		return ""
	var at: Vector3 = client.get_presented()["position"]
	var best := TOWN_RADIUS
	var town := ""
	for v: Dictionary in client.get_vendors():
		if str(v.get("kind", "")) != "travel" or str(v.get("detail", "")) == "":
			continue
		var p: Vector3 = v["position"]
		var d := Vector2(p.x - at.x, p.z - at.z).length()
		if d < best:
			best = d
			town = str(v["detail"])
	if town == "":
		return ""
	var key := town.to_lower().replace("'", "").replace(" ", "_")
	key = str((TOWN_NAMES.get(planet, {}) as Dictionary).get(key, key))
	var own := "amb_%s_%s_lp.wav" % [planet, key]
	if _stream(own) != null:
		return own
	var spec: Dictionary = PLANETS.get(planet, {})
	return str(spec.get("town", GENERIC_TOWN))


func _update_ambience(delta: float, file: String, target_db: float) -> void:
	if file != _amb_file:
		var stream := _stream(file)
		_amb_file = file
		if stream != null:
			_amb_active = 1 - _amb_active
			var p := _amb[_amb_active]
			p.stream = stream
			p.volume_db = -60.0
			p.play(randf() * maxf(0.0, stream.get_length() - 1.0))
	var step := 60.0 / FADE_SECONDS * delta
	for i in _amb.size():
		var p := _amb[i]
		if not p.playing:
			continue
		if i == _amb_active:
			p.volume_db = move_toward(p.volume_db, target_db, step)
		else:
			p.volume_db = move_toward(p.volume_db, -80.0, step)
			if p.volume_db <= -79.0:
				p.stop()


func _update_music(delta: float, pieces: Array) -> void:
	if _music.playing:
		_music.volume_db = move_toward(_music.volume_db, MUSIC_DB, 60.0 / FADE_SECONDS * delta)
		return
	if _music_wait < 0.0:
		_music_wait = randf_range(MUSIC_GAP.x, MUSIC_GAP.y)
	_music_wait -= delta
	if _music_wait > 0.0 or pieces.is_empty():
		return
	_music_wait = -1.0  # counts again once this piece is over
	var choices := pieces.filter(func(f: String) -> bool: return f != _music_last)
	if choices.is_empty():
		choices = pieces
	var file := str(choices[randi() % choices.size()])
	var stream := _stream(file)
	if stream == null:
		return
	_music_last = file
	_music.stream = stream
	_music.volume_db = MUSIC_DB
	_music.play()


# A file of DIR, loaded at run time (no import needed): a WAV looping on
# itself (the ambience), an MP3 played once (the music). null if missing.
func _stream(file: String) -> AudioStream:
	if file == "":
		return null
	if _cache.has(file):
		return _cache[file]
	var path := ProjectSettings.globalize_path(DIR.path_join(file))
	var stream: AudioStream = null
	if FileAccess.file_exists(path):
		if file.ends_with(".mp3"):
			var mp3 := AudioStreamMP3.new()
			mp3.data = FileAccess.get_file_as_bytes(path)
			mp3.loop = false
			stream = mp3
		elif file.ends_with(".wav"):
			var wav := AudioStreamWAV.load_from_file(path)
			if wav != null:
				wav.loop_mode = AudioStreamWAV.LOOP_FORWARD
				wav.loop_begin = 0
				wav.loop_end = int(wav.get_length() * wav.mix_rate)
				stream = wav
	_cache[file] = stream
	return stream
