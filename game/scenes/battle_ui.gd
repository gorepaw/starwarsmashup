# Battle & shared HUD/screens: Battlefront II HUD, SpawnScreen, ScoreScreen,
# ScopeOverlay, fallback text HUD, command post holograms/beams, team tags,
# and match music/VO triggers.
class_name BattleUI
extends Node

const CENTER_FONT := "gamefont_large"  # the middle of the screen's messages ("Eliminated")
const Bf2Hud = preload("res://ui/bf2_hud.gd")
const Bf2Map = preload("res://rendering/bf2_map.gd")
const Bf2Audio = preload("res://audio/bf2_audio.gd")
const ScopeOverlay = preload("res://ui/scope_overlay.gd")
const SpawnScreen = preload("res://ui/spawn_screen.gd")
const ScoreScreen = preload("res://ui/score_screen.gd")
const TeamTags = preload("res://rendering/team_tags.gd")
const BattleFlags = preload("res://rendering/battle_flags.gd")  # Stage 11: flag modes

const AMMO_NUDGE := Vector2(0.025, 0)
const TARGET_CONE := 0.035
const TARGET_RANGE := 150.0
const SHOT_TARGET_TIME := 3.0  # s the HUD keeps showing what you last hit
const SEEN_FIRING := 2.0
const PERK_FLASH := 1.5  # s a perk's name flashes when it fires
const FRIEND_COLOR := Color(0.35, 0.6, 1.0)
const ENEMY_COLOR := Color(1.0, 0.3, 0.25)
const NEUTRAL_COLOR := Color(0.9, 0.9, 0.9)
const HUB_MAP_RANGE := 250.0  # meters to the radar's edge in a hub's open world (ASSUMPTION: by eye)
const STRUCTURE_CELL := 128.0  # m: the radar's structures are filed by cells this big
var _structure_cells := {}      # Vector2i -> [outline]
var _structures_cell := Vector2i(1 << 30, 0)  # the cell _structures_shown is for
var _structures_shown: Array = []
const POST_BEAM_HEIGHT := 30.0
const HOLO_LENS_HEIGHT := 0.32
const HOLO_LENS_RADIUS := 0.15

signal spawn_requested(loadout: int, post: int)
signal loadout_selected(idx: int)
signal post_selected(idx: int)

var _world_root: Node3D
var _audio: Bf2Audio
var _bf2_hud: Bf2Hud
var _scope: ScopeOverlay
var _spawn_screen: SpawnScreen
var _score_screen: ScoreScreen

var _hud: Label
var _crosshair: Label
var _feed: Label
var _center: Label
var _match_label: RichTextLabel
var _feed_lines: PackedStringArray = []

var _posts: Array = []
var _flag_view := BattleFlags.new()
var _cone_shader: Shader
var _team_tags: TeamTags
var _fired := {}
var _perk_flash := {}       # a triggered perk's index -> seconds its name still flashes
var _perk_was_active := {}  # ...and whether it was active last frame (a roll's, an overheat's: seen here)
var _hit_flash := 0.0
var _shield_flash := 0.0       # a capital ship's shield took our shot
var _shield_notice := 0.0      # until we're told again
var _aimed := {}            # what the reticle is on this frame (an aim candidate; {} nothing)
var _shot_target := ""      # what you last hit (its candidate key), shown for SHOT_TARGET_TIME
var _shot_time := 0.0

var _prev_post_owners: Array[int] = []
var _prev_phase := ""
var _start_tickets := [0.0, 0.0]
var _vo_enemy_low := false
var _vo_us_low := false
var _outside := 0.0
var _was_choosing := false

var _map_meta: Dictionary = {}
var hub_ui_ref: Object      # the hub's UI, for its practice targets' readouts (null in a match)
var hub_markers: Array = []  # a hub's radar markers (HubUI.radar_markers), set before each update
var driving := {}            # the vehicle we drive ({label, health, max_health, ...}), set before each update
var _world_bake := ""
var _battlefield: Dictionary = {}
var _display_names: Dictionary = {}

var spawn_loadout := 0
var spawn_post := -1
var spawn_requested_flag := false


func set_catalog_names(catalog: Array) -> void:
	_display_names.clear()
	for def: Dictionary in catalog:
		_display_names[str(def.get("id", ""))] = str(def.get("name", ""))


func setup(layer: CanvasLayer, world_root: Node3D, audio: Bf2Audio, hud_dir: String) -> void:
	_world_root = world_root
	_audio = audio
	_bf2_hud = Bf2Hud.new()
	if _bf2_hud.load_layout(hud_dir):
		_scope = ScopeOverlay.new(_bf2_hud.art)
		_scope.visible = false
		layer.add_child(_scope)
		for ammo in ["player1weapon1ammo", "player1weapon2ammo", "player1weapon1ammoInfinite",
				"player1weapon2ammoInfinite"]:
			_bf2_hud.nudges[ammo] = AMMO_NUDGE
		layer.add_child(_bf2_hud)
		_spawn_screen = SpawnScreen.new(_bf2_hud.art)
		_spawn_screen.visible = false
		_spawn_screen.loadout_chosen.connect(func(i: int) -> void:
			spawn_loadout = i
			loadout_selected.emit(i)
			if _audio != null:
				_audio.play_2d("shell_select_unit"))
		_spawn_screen.post_chosen.connect(func(i: int) -> void:
			spawn_post = i
			post_selected.emit(i)
			if _audio != null:
				_audio.play_2d("shell_select_unit"))
		_spawn_screen.spawn_requested.connect(func() -> void: spawn_requested.emit(spawn_loadout, spawn_post))
		_score_screen = ScoreScreen.new(_bf2_hud.art)
		_score_screen.visible = false
	else:
		_bf2_hud = null

	_hud = Label.new()
	_hud.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_LEFT)
	_hud.grow_vertical = Control.GROW_DIRECTION_BEGIN
	_hud.offset_left = 12
	_hud.offset_bottom = -8
	_hud.add_theme_color_override("font_outline_color", Color.BLACK)
	_hud.add_theme_constant_override("outline_size", 4)
	layer.add_child(_hud)
	_crosshair = Label.new()
	_crosshair.text = "+"
	_crosshair.add_theme_font_size_override("font_size", 28)
	_crosshair.add_theme_color_override("font_outline_color", Color.BLACK)
	_crosshair.add_theme_constant_override("outline_size", 4)
	_crosshair.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	_crosshair.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_crosshair.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	layer.add_child(_crosshair)
	_feed = Label.new()
	_feed.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	_feed.position = Vector2(-320, 8)
	_feed.size = Vector2(310, 140)
	_feed.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_feed.add_theme_color_override("font_outline_color", Color.BLACK)
	_feed.add_theme_constant_override("outline_size", 4)
	layer.add_child(_feed)
	_center = Label.new()
	_center.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	_center.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_center.grow_vertical = Control.GROW_DIRECTION_BOTH
	_center.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_center.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_center.add_theme_font_size_override("font_size", 20)
	_center.add_theme_color_override("font_outline_color", Color.BLACK)
	_center.add_theme_constant_override("outline_size", 6)
	# In Battlefront's own large font where there's one, with its drop shadow
	# (sized with the screen in _update_match_hud).
	if _bf2_hud != null and _bf2_hud.art != null and _bf2_hud.art.godot_font(CENTER_FONT) != null:
		_center.add_theme_font_override("font", _bf2_hud.art.godot_font(CENTER_FONT))
		_center.add_theme_constant_override("outline_size", 0)
		_center.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.8))
		_center.add_theme_constant_override("shadow_offset_x", 2)
		_center.add_theme_constant_override("shadow_offset_y", 2)
	layer.add_child(_center)
	_match_label = RichTextLabel.new()
	_match_label.bbcode_enabled = true
	_match_label.fit_content = true
	_match_label.scroll_active = false
	_match_label.set_anchors_and_offsets_preset(Control.PRESET_CENTER_TOP)
	_match_label.position = Vector2(-300, 8)
	_match_label.size = Vector2(600, 90)
	_match_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_match_label.add_theme_constant_override("outline_size", 4)
	layer.add_child(_match_label)
	for screen: Control in [_spawn_screen, _score_screen]:
		if screen != null:
			layer.add_child(screen)


func has_bf2_hud() -> bool:
	return _bf2_hud != null


func art() -> RefCounted:
	return _bf2_hud.art if _bf2_hud != null else null


func center_label() -> Label:
	return _center


# The radar's picture of the map ({texture, left_x, top_z, right_x, bottom_z}; {} none).
func minimap() -> Dictionary:
	return _bf2_hud.minimap if _bf2_hud != null else {}


func match_label() -> RichTextLabel:
	return _match_label


func set_world_context(bake: String, meta: Dictionary, battlefield: Dictionary) -> void:
	_world_bake = bake
	_map_meta = meta
	_battlefield = battlefield


func clear_posts() -> void:
	_team_tags = null
	_flag_view.clear()
	for post: Dictionary in _posts:
		var nodes: Array = post.get("nodes", [])
		if post.has("hologram"):
			nodes = [post["hologram"]["pivot"], post["hologram"]["light"], post["hologram"]["cone"]]
		for node: Node in nodes:
			if is_instance_valid(node):
				node.queue_free()
	_posts.clear()


func on_world_arrived(battlefield: Dictionary) -> void:
	clear_posts()
	_battlefield = battlefield
	_prev_post_owners.clear()
	_prev_phase = ""
	_start_tickets = [0.0, 0.0]
	_vo_enemy_low = false
	_vo_us_low = false
	spawn_requested_flag = false


func add_posts() -> void:
	if _battlefield.is_empty():
		return
	var holograms := _post_models()
	for entry: Dictionary in holograms.values():
		var spec: Dictionary = entry["hologram"]
		if not (spec.get("images", {}) as Dictionary).is_empty():
			var tags := TeamTags.new("res://" + str(_map_meta["dir"]), spec)
			if tags.available():
				_team_tags = tags
				break
	for post: Dictionary in _battlefield["posts"]:
		var model: Dictionary = holograms.get(str(post["name"]).to_lower(), {})
		if not model.is_empty() and not ((model["hologram"] as Dictionary).get("images", {}) as Dictionary).is_empty():
			_posts.append({"hologram": _add_hologram(model)})
			continue
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		material.albedo_color = NEUTRAL_COLOR
		var position: Vector3 = post["position"]
		var beam := MeshInstance3D.new()
		var cylinder := CylinderMesh.new()
		cylinder.top_radius = 0.25
		cylinder.bottom_radius = 0.25
		cylinder.height = POST_BEAM_HEIGHT
		beam.mesh = cylinder
		beam.material_override = material
		beam.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		beam.position = position + Vector3.UP * POST_BEAM_HEIGHT / 2
		_world_root.add_child(beam)
		var ring := MeshInstance3D.new()
		var torus := TorusMesh.new()
		torus.outer_radius = float(post["radius"])
		torus.inner_radius = float(post["radius"]) - 0.3
		ring.mesh = torus
		ring.material_override = material
		ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		ring.position = position + Vector3.UP * 0.15
		_world_root.add_child(ring)
		var label := Label3D.new()
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.fixed_size = true
		label.pixel_size = 0.0015
		label.font_size = 28
		label.outline_size = 8
		label.no_depth_test = true
		label.position = position + Vector3.UP * 6
		_world_root.add_child(label)
		_posts.append({"material": material, "label": label, "nodes": [beam, ring, label]})
	_flag_view.build(_world_root, _battlefield)


func show_team_tag(id: int, model: Node3D, team: int, own_team: int, alive: bool, friend := false,
		height := TeamTags.HEIGHT) -> bool:
	if _team_tags == null or own_team < 0 or team < 0:
		return false
	# A native (team 2) shows a side's insignia too, as Battlefront gives its
	# locals one (Kashyyyk's Wookiees the Rebels'): yours when it's your
	# friend (Jawas, your side's Ewoks or Geonosians), the other side's when
	# it isn't (Gamorrean guards; the other side's natives).
	var side := _holo_side((own_team if friend else 1 - own_team) if team > 1 else team)
	if side == "":
		return false
	_team_tags.show(id, model, side, friend or team == own_team, alive, height)
	return true


func forget_team_tag(id: int) -> void:
	if _team_tags != null:
		_team_tags.forget(id)


func update_team_tags(camera: Camera3D, delta: float, raycast: Callable) -> void:
	if _team_tags != null:
		_team_tags.update(camera, delta, raycast)


# A triggered perk the server fired (its index in the loadout's), flashed on
# the HUD (docs/PERKS.md).
func note_perk_fired(index: int) -> void:
	_perk_flash[index] = PERK_FLASH


# The HUD's perk lines: those active (a held one's name, a timed one's
# seconds left), and any that just fired, flashed for a moment. A roll's or
# an overheat's the client sees at once; the server's come as events.
func _perk_lines(client: Object, delta: float) -> Array:
	var out: Array = []
	var perks: Array = client.get_perks()
	for i in perks.size():
		var p: Dictionary = perks[i]
		var active := bool(p["active"])
		if str(p["where"]) == "simulation" and active and not bool(_perk_was_active.get(i, false)):
			_perk_flash[i] = PERK_FLASH
		_perk_was_active[i] = active
		var flash := float(_perk_flash.get(i, 0.0))
		_perk_flash[i] = maxf(0.0, flash - delta)
		if not active and flash <= 0.0:
			continue
		var held := str(p["where"]) == "stance" or str(p["trigger"]) in ["unhit", "low_health"]
		var time := float(p["time"]) if active and not held and not bool(p["cooldown"]) else -1.0
		out.append({"name": str(p["name"]), "time": time, "flash": flash / PERK_FLASH})
	return out


func note_fired(entity_id: int) -> void:
	_fired[entity_id] = 0.0


# Whether the reticle is on one of the world's townsfolk (shots are held while it is).
func aiming_at_peaceful() -> bool:
	return bool(_aimed.get("peaceful", false))


# A shot of ours a capital ship's shield took: the reticle's target marker
# flashes blue, and now and then the feed says so.
func note_shielded() -> void:
	_shield_flash = 0.15
	if _shield_notice <= 0.0:
		add_feed_line("Shield absorbing fire: destroy its shield generator first")
		_shield_notice = 6.0


func note_hit() -> void:
	_hit_flash = 0.15
	if not _aimed.is_empty():
		_shot_target = str(_aimed["key"])
		_shot_time = SHOT_TARGET_TIME


func add_feed_line(line: String) -> void:
	_feed_lines.append(line)
	if _feed_lines.size() > 5:
		_feed_lines.remove_at(0)


func set_scope_visible(vis: bool, weapon_name: String) -> void:
	if _scope != null:
		_scope.visible = vis
		_scope.side = weapon_name.substr(0, 3)


func is_choosing_spawn(client: Object) -> bool:
	return client != null and client.is_spawned() and not _battlefield.is_empty() and client.get_presented()["life"] == "waiting"


func mobile_spawns(client: Object, team: int) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	if client == null or _battlefield.is_empty():
		return out
	var classes: Array = _battlefield.get("vehicles", [])
	for v: Dictionary in client.get_vehicles():
		if not bool(v.get("alive", false)) or not bool(v.get("mobile_spawn", false)) or int(v.get("team", -1)) != team:
			continue
		var cls_idx := int(v.get("vehicle", -1))
		var label := "Transport"
		if cls_idx >= 0 and cls_idx < classes.size():
			label = str((classes[cls_idx] as Dictionary).get("label", label))
		out.append({"vehicle": v, "label": label + " (Mobile)", "position": v["position"]})
	return out


func owned_posts(game: Dictionary, team: int, client: Object = null) -> Array[int]:
	var owned: Array[int] = []
	var posts: Array = game.get("posts", [])
	for i in posts.size():
		if int(posts[i]["owner"]) == team:
			owned.append(i)
	if client != null and game.get("phase", "") != "ended":
		var mobiles := mobile_spawns(client, team)
		for m in mobiles.size():
			owned.append(posts.size() + m)
	return owned


func cycle_post(client: Object, step: int) -> void:
	var owned := owned_posts(client.get_match(), int(client.get_presented()["team"]), client)
	if owned.is_empty():
		return
	var at := owned.find(spawn_post)
	spawn_post = owned[posmod(at + step, owned.size())]


func spawn_post_position(client: Object, post_index: int) -> Vector3:
	var posts: Array = _battlefield.get("posts", [])
	if post_index >= 0 and post_index < posts.size():
		return posts[post_index]["position"]
	if client != null and post_index >= posts.size():
		var own: Dictionary = client.get_presented()
		var mobiles := mobile_spawns(client, int(own.get("team", -1)))
		var m_idx := post_index - posts.size()
		if m_idx >= 0 and m_idx < mobiles.size():
			return mobiles[m_idx]["position"]
	return Vector3.ZERO


func update_posts(game: Dictionary, own_team: int, elapsed: float) -> void:
	if game.is_empty():
		return
	_flag_view.update(game, own_team, elapsed)
	for i in mini(_posts.size(), (game["posts"] as Array).size()):
		var state: Dictionary = game["posts"][i]
		if _posts[i].has("hologram"):
			_update_hologram(_posts[i]["hologram"], state, own_team, elapsed)
			continue
		var color := _post_color(int(state["owner"]), own_team)
		var material: StandardMaterial3D = _posts[i]["material"]
		material.albedo_color = Color(color, 0.75)
		var label: Label3D = _posts[i]["label"]
		var text: String = _battlefield["posts"][i]["name"]
		if bool(state["contested"]):
			text += " (contested)"
		elif float(state["charge"]) < 1.0 and int(state["holder"]) >= 0:
			text += " %d%%" % roundi(float(state["charge"]) * 100)
		label.text = text
		label.modulate = color


func update_hud(client: Object, weapons: Array, remotes_count: int, max_health: float,
		show_stats: bool, delta: float, pause_visible: bool, terminal_open: bool,
		hub_ui: Node, generation: int, bot_mode: String, camera: Camera3D,
		yaw: float, capture_mouse_cb: Callable, who_cb: Callable) -> void:
	var stats: Dictionary = client.get_stats()
	var lines := PackedStringArray()
	lines.append("%s | character %d | %d other player(s) | rtt %.0f ms | corrections %d (max %.2f m)" % [
			client.get_status(), client.get_character_id(), remotes_count, stats.get("rtt_ms", 0.0),
			stats.get("corrections", 0), stats.get("max_correction", 0.0)])
	if client.is_spawned():
		var own: Dictionary = client.get_presented()
		var slot: int = own["weapon"]
		var weapon: Dictionary = weapons[slot] if slot < weapons.size() else {}
		var ammo := ""
		if int(weapon.get("rounds_per_clip", 0)) > 0:
			var carried := "" if int(own["reserve"]) < 0 else " (+%d)" % own["reserve"]
			ammo = "%d / %d%s%s" % [own["rounds"], weapon["rounds_per_clip"], carried, "  reloading" if own["reloading"] else ""]
		else:
			ammo = "heat %d%%%s" % [roundi(float(own["heat"]) * 100), "  OVERHEATED" if own["overheated"] else ""]
		var secondary: Dictionary = client.get_secondary()
		var grenades := "" if secondary.is_empty() else " | %s x%d" % [secondary["name"], own["grenades"]]
		lines.append("health %d / %d | %s: %s%s | energy %d" % [roundi(own["health"]), roundi(max_health),
				weapon.get("name", "?"), ammo, grenades, roundi(own["energy"])])
	if _bf2_hud != null:
		lines.resize(1 if show_stats else 0)
		_update_bf2_hud(client, weapons, max_health, delta, camera, yaw)
		_update_screens(client, pause_visible, terminal_open, bot_mode, capture_mouse_cb, who_cb)
	_hud.text = "\n".join(lines)
	_update_match_hud(client, delta, hub_ui, generation, who_cb)
	_hit_flash = maxf(0.0, _hit_flash - delta)
	_shield_flash = maxf(0.0, _shield_flash - delta)
	_shield_notice = maxf(0.0, _shield_notice - delta)
	_shot_time = maxf(0.0, _shot_time - delta)
	_crosshair.visible = _bf2_hud == null
	_feed.visible = _bf2_hud == null
	_crosshair.text = "x" if _hit_flash > 0 else "+"
	_crosshair.modulate = Color(1, 0.3, 0.3) if _hit_flash > 0 else Color.WHITE
	_feed.text = "\n".join(_feed_lines)


func update_match_audio(game: Dictionary, own: Dictionary) -> void:
	if _audio == null:
		return
	if game.is_empty():
		if _prev_phase != "":
			_audio.stop_music()
			_prev_phase = ""
		return
	var posts: Array = game.get("posts", [])
	_prev_post_owners.resize(posts.size())
	for i in posts.size():
		_prev_post_owners[i] = int(posts[i]["owner"])
	var own_team := int(own.get("team", -1))
	var side := _side_code(own_team)
	if side == "":
		return
	var planet := _map_code()
	var phase := str(game.get("phase", ""))
	var tickets: Array = game.get("tickets", [0.0, 0.0])
	if phase != _prev_phase:
		if phase == "warmup" or phase == "live":
			_start_tickets = [maxf(1.0, float(tickets[0])), maxf(1.0, float(tickets[1]))]
			_vo_enemy_low = false
			_vo_us_low = false
		elif phase == "ended":
			var winner := int(game.get("winner", -1))
			if winner == own_team:
				_audio.queue_vo(side + "_off_victory_im", true)
				_play_music_cue(side, planet, "victory", false)
			elif winner >= 0:
				_audio.queue_vo(side + "_off_defeat_im", true)
				_play_music_cue(side, planet, "defeat", false)
	_prev_phase = phase
	if phase == "warmup":
		_play_music_cue(side, planet, "start", true)
	elif phase == "live":
		var enemy := 1 - own_team
		var own_ratio := float(tickets[own_team]) / maxf(1.0, float(_start_tickets[own_team]))
		var enemy_ratio := float(tickets[enemy]) / maxf(1.0, float(_start_tickets[enemy]))
		if not _vo_enemy_low and enemy_ratio <= 0.35:
			_vo_enemy_low = true
			_audio.queue_vo(side + "_off_com_report_enemy_losing")
		if not _vo_us_low and own_ratio <= 0.35:
			_vo_us_low = true
			_audio.queue_vo(side + "_off_com_report_us_overwhelmed")
		var min_ratio := minf(own_ratio, enemy_ratio)
		var cue := "start" if min_ratio > 0.70 else ("middle" if min_ratio > 0.35 else "end")
		if int(game.get("dominant", -1)) >= 0:
			cue = "end"
		_play_music_cue(side, planet, cue, true)


func handle_capture_event(event: Dictionary, own_team: int) -> void:
	if _battlefield.is_empty():
		return
	var post_idx := int(event["victim"])
	var att_team := int(event["attacker"])
	if _audio != null and own_team >= 0:
		var oside := _side_code(own_team)
		if event["kind"] == "captured":
			if att_team == own_team:
				_audio.play_2d("com_blg_commandpost_capture")
				if oside != "":
					_audio.queue_vo(oside + "_off_com_report_captured_commandpost")
			else:
				_audio.play_2d("com_blg_commandpost_lost")
		elif event["kind"] == "neutralized":
			var prev_owner := _prev_post_owners[post_idx] if post_idx < _prev_post_owners.size() else -1
			if prev_owner == own_team:
				_audio.play_2d("com_blg_commandpost_lost")
				if oside != "":
					_audio.queue_vo(oside + "_off_com_report_lost_commandpost")
	add_feed_line("%s %s %s" % [_team_name(att_team), event["kind"], _battlefield["posts"][post_idx]["name"]])


# Stage 11: a flag taken, dropped, returned or captured (attacker: the
# soldier, victim: the flag, damage: the soldier's team).
func handle_flag_event(event: Dictionary, own_team: int, who: Callable) -> void:
	var team := int(event["damage"])
	var soldier := int(event["attacker"])
	var by := str(who.call(soldier)) if soldier != 0 else _team_name(team)
	var ours := team == own_team
	match str(event["kind"]):
		"flag_taken":
			add_feed_line("%s took the flag" % by)
			if _audio != null:
				_audio.play_2d("com_blg_commandpost_capture" if ours else "com_blg_commandpost_lost")
		"flag_dropped":
			add_feed_line("%s dropped the flag" % by)
		"flag_returned":
			add_feed_line("The flag went home" if soldier == 0 else "%s returned their flag" % by)
		"flag_captured":
			add_feed_line("%s captured the flag!" % by)
			if _audio != null:
				_audio.play_2d("com_blg_commandpost_capture" if ours else "com_blg_commandpost_lost")


func push_hud_message(event: Dictionary, client: Object) -> void:
	if _bf2_hud == null or _feed_lines.is_empty() or not (event["kind"] in ["kill", "captured", "neutralized",
			"flag_taken", "flag_dropped", "flag_returned", "flag_captured"]):
		return
	_bf2_hud.messages.append({"text": _feed_lines[-1], "age": 0.0, "color": _message_color(event, client)})
	if _bf2_hud.messages.size() > 16:
		_bf2_hud.messages.remove_at(0)


func side_code(team: int) -> String:
	return _side_code(team)


func _update_bf2_hud(client: Object, weapons: Array, max_health: float, delta: float,
		camera: Camera3D, yaw: float) -> void:
	for id: int in _fired.keys():
		_fired[id] = float(_fired[id]) + delta
	var b := _bf2_hud.bindings
	b.clear()
	_bf2_hud.map_markers.clear()
	_bf2_hud.map_player = {}
	if not client.is_spawned():
		_bf2_hud.queue_redraw()
		return
	var own: Dictionary = client.get_presented()
	var team := int(own["team"])
	var game: Dictionary = client.get_match()
	var alive := bool(own["alive"]) and str(own.get("life", "alive")) == "alive"
	_bf2_hud.perks = _perk_lines(client, delta) if alive else []
	if alive:
		b["player1.weaponsEnable"] = true
		b["player1.weapon1.reticule.position"] = true
		# Flying: the reticle where the guns really point (the nose, turned
		# toward the look within the guns' limits), not the screen's middle
		# the look leads it to while the ship turns.
		var aim_at := _flyer_aim_point(own, camera)
		if aim_at != Vector2(-1, -1):
			b["player1.weapon1.reticule.position"] = aim_at
		b["player1.health"] = true
		b["player1.healthFraction"] = clampf(float(own["health"]) / maxf(1.0, max_health), 0.0, 1.0)
		if not driving.is_empty():
			# Driving: the vehicle's health.
			b["player1.healthFraction"] = clampf(float(driving["health"]) / maxf(1.0, float(driving["max_health"])),
					0.0, 1.0)
		b["player1.energyFraction"] = clampf(float(own["energy"]) / 100.0, 0.0, 1.0)
		var max_shield := maxf(float(own.get("max_shield", 0.0)), float(client.get_max_shield()))
		_bf2_hud.shield_fraction = clampf(float(own.get("shield", 0.0)) / max_shield, 0.0, 1.0) \
				if max_shield > 0.0 and driving.is_empty() else -1.0
		var candidates := _aim_candidates(client, team)
		var target := _aimed_at(client, camera, candidates)
		_aimed = target
		if target.is_empty() and _shot_time > 0.0:
			for c: Dictionary in candidates:
				if str(c["key"]) == _shot_target:
					target = c
					break
		var tint := Color8(220, 220, 220)
		if not target.is_empty():
			tint = FRIEND_COLOR if bool(target["friend"]) else (ENEMY_COLOR if bool(target["hostile"]) else tint)
			# Battlefront's target readout: the name under the reticle, and the
			# health of what you fight (aimed at, or hit in the last few seconds).
			if str(target["name"]) != "":
				b["player1.weapon1.target.className"] = str(target["name"])
			if bool(target["hostile"]) and float(target["max_health"]) > 0.0:
				b["player1.weapon1.target.healthFraction"] = maxf(0.001,
						clampf(float(target["health"]) / float(target["max_health"]), 0.0, 1.0))
		b["player1.weapon1.target.teamColor"] = tint
		b["player1.weapon1.target.teamColorBright"] = tint.lightened(0.3)
		if _hit_flash > 0:
			b["player1.weapon1.target.hit"] = true
			b["player1.weapon1.target.hitColor"] = Color8(223, 60, 60)
		elif _shield_flash > 0:
			b["player1.weapon1.target.hit"] = true
			b["player1.weapon1.target.hitColor"] = Color8(80, 170, 255)
		var slot: int = own["weapon"]
		var weapon: Dictionary = weapons[slot] if slot < weapons.size() else {}
		if not weapon.is_empty():
			b["player1.weapon1.name"] = _display_name(str(weapon["name"]))
			b["player1.weapon1.change"] = true
			# Its own model, where it has one (Battlefront's weapons' HUD meshes otherwise).
			b["player1.weapon1.mesh"] = str(weapon.get("held_model", "")) if str(weapon.get("held_model", "")) != "" \
					else str(weapon["name"])
			if int(weapon.get("rounds_per_clip", 0)) > 0:
				b["player1.weapon1.totalClipFraction"] = float(own["rounds"]) / float(weapon["rounds_per_clip"])
				if int(own["reserve"]) < 0:
					b["player1.weapon1.totalAmmoBullets"] = str(own["rounds"])
				else:
					b["player1.weapon1.totalAmmoBullets"] = "%d/%d" % [own["rounds"], own["reserve"]]
			elif str(weapon.get("projectile", "")) == "melee":
				b["player1.weapon1.totalAmmoBullets"] = "MELEE"
				b["player1.weapon1.ammoInfinite"] = true
			else:
				b["player1.weapon1.heat"] = clampf(float(own["heat"]), 0.0, 1.0)
				b["player1.weapon1.ammoInfinite"] = true
		# Driving something with two weapons (a starfighter's cannons and
		# torpedoes): both always on the HUD, each its own panel, not one
		# swapping into the other while the secondary's button is held.
		var slots: Array = own.get("slots", [])
		if not driving.is_empty() and weapons.size() >= 2 and not (weapons[1] as Dictionary).is_empty() \
				and slots.size() >= 2:
			_vehicle_weapon_bindings(b, "player1.weapon1", weapons[0], slots[0])
			_vehicle_weapon_bindings(b, "player1.weapon2", weapons[1], slots[1])
		var secondary: Dictionary = client.get_secondary() if driving.is_empty() else {}
		if not secondary.is_empty():
			b["player1.weapon2.name"] = _display_name(str(secondary["name"]))
			b["player1.weapon2.change"] = true
			b["player1.weapon2.mesh"] = str(secondary.get("held_model", "")) if str(secondary.get("held_model", "")) != "" \
					else str(secondary["name"])
			b["player1.weapon2.totalAmmoBullets"] = str(own["grenades"])
		var position: Vector3 = own["position"]
		_bf2_hud.map_player = {"position": Vector2(position.x, position.z), "yaw": yaw}
	else:
		_bf2_hud.shield_fraction = -1.0
	if not game.is_empty() and team >= 0:
		var score_limit := int(game.get("score_limit", 0))
		var scores: Array = game.get("scores", [0, 0])
		for side in 2:
			var slot := "player1.team%d" % (1 if side == team else 2)
			b[slot + ".texture"] = _team_icon(side)
			b[slot + ".reinforcements"] = int(scores[side]) if score_limit > 0 else ceili(game["tickets"][side])
		var dominant := int(game.get("dominant", -1))
		if game["phase"] == "live" and dominant >= 0:
			b["victorytimer" if dominant == team else "defeattimer"] = ceili(game["victory_left"])
	if alive and not game.is_empty():
		_update_capture_ring(own, team, game)
	_update_minimap(client, team, game)
	_bf2_hud.apply_tables()
	_bf2_hud.queue_redraw()


func _update_screens(client: Object, pause_visible: bool, terminal_open: bool, bot_mode: String,
		capture_mouse_cb: Callable, who_cb: Callable) -> void:
	var game: Dictionary = client.get_match() if client.is_spawned() else {}
	var own: Dictionary = client.get_presented() if client.is_spawned() else {}
	var team := int(own.get("team", -1))
	var choosing: bool = is_choosing_spawn(client) and not game.is_empty() and game["phase"] != "ended"
	_spawn_screen.visible = choosing and not pause_visible
	if choosing and not _was_choosing and bot_mode == "":
		capture_mouse_cb.call(false)
	elif not choosing and _was_choosing and bot_mode == "" and not pause_visible:
		capture_mouse_cb.call(true)
	_was_choosing = choosing
	if _spawn_screen.visible:
		var custom_lo: Array = client.get_custom_loadouts()
		var raw_loadouts: Array = custom_lo if not custom_lo.is_empty() else _battlefield["teams"][team]["loadouts"]
		var loadouts: Array = []
		for entry: Dictionary in raw_loadouts:
			var weapons: Array = []
			for weapon: String in entry["weapons"] + ([entry["secondary"]] if entry["secondary"] != "" else []):
				weapons.append(_display_name(weapon))
			loadouts.append({"name": str(entry["name"]) if not custom_lo.is_empty() else _unit_name(str(entry["name"])),
					"weapons": weapons, "max_health": entry["max_health"]})
		var posts: Array = []
		var owned := owned_posts(game, team, client)
		var map_posts: Array = game["posts"]
		for i in map_posts.size():
			var at: Vector3 = _battlefield["posts"][i]["position"]
			posts.append({"name": _battlefield["posts"][i]["name"], "position": Vector2(at.x, at.z),
					"color": _post_color(int(map_posts[i]["owner"]), team), "ours": owned.has(i)})
		var mobiles := mobile_spawns(client, team)
		for m in mobiles.size():
			var mat: Vector3 = mobiles[m]["position"]
			posts.append({"name": mobiles[m]["label"], "position": Vector2(mat.x, mat.z),
					"color": FRIEND_COLOR, "ours": true, "mobile": true})
		_spawn_screen.title = str(_battlefield["name"])
		_spawn_screen.side = {"name": _team_name(team), "icon": _team_icon(team), "color": FRIEND_COLOR}
		_spawn_screen.loadouts = loadouts
		_spawn_screen.loadout = spawn_loadout
		_spawn_screen.posts = posts
		_spawn_screen.post = spawn_post
		_spawn_screen.minimap = _bf2_hud.minimap
		_spawn_screen.waiting = spawn_requested_flag
		var score_limit := int(game.get("score_limit", 0))
		_spawn_screen.notice = "No reinforcements left" \
				if game["phase"] == "live" and score_limit <= 0 and ceili(game["tickets"][team]) <= 0 else ""
		_spawn_screen.queue_redraw()
	var ended: bool = not game.is_empty() and game["phase"] == "ended"
	_score_screen.visible = not game.is_empty() and team >= 0 and not pause_visible \
			and (ended or (Input.is_action_pressed("scores") and bot_mode == ""))
	_bf2_hud.visible = not (pause_visible or _spawn_screen.visible or _score_screen.visible or terminal_open)
	if _score_screen.visible:
		var everyone: Array = client.get_remotes()
		var me := own.duplicate()
		me["id"] = client.get_entity_id()
		everyone.append(me)
		var score_limit := int(game.get("score_limit", 0))
		var scores: Array = game.get("scores", [0, 0])
		var sides: Array = []
		for side in 2:
			var players: Array = []
			for p: Dictionary in everyone:
				if int(p["team"]) == side:
					var pname: String = who_cb.call(p["id"])
					players.append({"name": pname.capitalize() if int(p["id"]) == client.get_entity_id() else pname,
							"kills": p["kills"], "deaths": p["deaths"], "captures": p["captures"],
							"you": int(p["id"]) == client.get_entity_id()})
			sides.append({"name": _team_name(side), "icon": _team_icon(side),
					"color": FRIEND_COLOR if side == team else ENEMY_COLOR, "tickets": game["tickets"][side],
					"score": int(scores[side]), "score_limit": score_limit, "players": players})
		_score_screen.sides = sides
		_score_screen.own_team = team
		_score_screen.ended = ended
		_score_screen.winner = int(game.get("winner", -1))
		_score_screen.next_in = float(game.get("time_left", 0.0))
		_score_screen.queue_redraw()


func _update_capture_ring(own: Dictionary, team: int, game: Dictionary) -> void:
	var at: Vector3 = own["position"]
	for i in (game["posts"] as Array).size():
		var post: Dictionary = _battlefield["posts"][i]
		var center: Vector3 = post["position"]
		var radius := float(post["radius"])
		if Vector2(at.x - center.x, at.z - center.z).length_squared() > radius * radius:
			continue
		var state: Dictionary = game["posts"][i]
		var holder := int(state["holder"])
		var b := _bf2_hud.bindings
		b["player1.commandPost.charge"] = clampf(float(state["charge"]), 0.0, 1.0) if holder >= 0 else 0.0
		b["player1.commandPost.color"] = NEUTRAL_COLOR if holder < 0 else (FRIEND_COLOR if holder == team else ENEMY_COLOR)
		b["player1.commandPost.disputeColor"] = NEUTRAL_COLOR if bool(state["contested"]) else Color(0.75, 0.75, 0.75)
		return


# A hub's radar: the planet's terrain (its bake's ground preview, north up)
# under the quests' places, their givers and the mission terminals.
func _update_hub_minimap() -> void:
	var bake := str(_map_meta.get("bake", ""))
	if bake == "" or _bf2_hud.map_player.is_empty():
		return
	_bf2_hud.map_structures = _structures_near(_bf2_hud.map_player["position"])
	var file := bake + "_ground_preview.png"
	if _bf2_hud.minimap.get("file", "") != file:
		var path := ProjectSettings.globalize_path(file if file.begins_with("res://") else "res://" + file)
		var image := Image.load_from_file(path) if FileAccess.file_exists(path) else null
		if image == null:
			return
		var origin_x := float(_map_meta.get("origin_x", 0))
		var origin_z := float(_map_meta.get("origin_z", 0))
		var extent := (float(_map_meta.get("samples", 1)) - 1) * float(_map_meta.get("spacing", 1))
		_bf2_hud.minimap = {"file": file, "texture": ImageTexture.create_from_image(image), "left_x": origin_x,
				"top_z": origin_z + extent, "right_x": origin_x + extent, "bottom_z": origin_z}
	_bf2_hud.map_range = HUB_MAP_RANGE
	_bf2_hud.bindings["player1.map.enable"] = true
	_bf2_hud.map_markers.append_array(hub_markers)


# The world's structures' outlines (SwgCity's footprints) for the radar,
# filed by STRUCTURE_CELL so only those near are looked at.
func set_structures(outlines: Array) -> void:
	_structure_cells.clear()
	_structures_cell = Vector2i(1 << 30, 0)
	for outline: PackedVector2Array in outlines:
		var center := Vector2.ZERO
		for p in outline:
			center += p / outline.size()
		var cell := Vector2i(floori(center.x / STRUCTURE_CELL), floori(center.y / STRUCTURE_CELL))
		if not _structure_cells.has(cell):
			_structure_cells[cell] = []
		(_structure_cells[cell] as Array).append(outline)


func _structures_near(at: Vector2) -> Array:
	var cell := Vector2i(floori(at.x / STRUCTURE_CELL), floori(at.y / STRUCTURE_CELL))
	if cell != _structures_cell:
		_structures_cell = cell
		_structures_shown = []
		var reach := ceili(HUB_MAP_RANGE * 1.5 / STRUCTURE_CELL)
		for dx in range(-reach, reach + 1):
			for dz in range(-reach, reach + 1):
				_structures_shown.append_array(_structure_cells.get(cell + Vector2i(dx, dz), []))
	return _structures_shown


func _update_minimap(client: Object, team: int, game: Dictionary) -> void:
	if _battlefield.is_empty():
		_update_hub_minimap()
		return
	_bf2_hud.map_structures = []
	_bf2_hud.map_range = Bf2Hud.MAP_RANGE
	var file := str(_map_meta.get("minimap", ""))
	if _bf2_hud.minimap.get("file", "") != file and file != "":
		var image := Image.load_from_file(ProjectSettings.globalize_path("res://" + str(_map_meta["dir"]).path_join(file)))
		var edges: Array = _battlefield.get("minimap", [0, 0, 0, 0])
		if image == null:
			return
		if edges.is_empty() or (float(edges[0]) == 0 and float(edges[2]) == 0):
			edges = [_battlefield["min_x"], _battlefield["min_z"], _battlefield["max_x"], _battlefield["max_z"]]
		_bf2_hud.minimap = {"file": file, "texture": ImageTexture.create_from_image(image), "left_x": edges[0],
				"top_z": edges[1], "right_x": edges[2], "bottom_z": edges[3]}
	if _bf2_hud.map_player.is_empty():
		return
	_bf2_hud.bindings["player1.map.enable"] = true
	if not game.is_empty():
		for i in (game["posts"] as Array).size():
			var post: Dictionary = _battlefield["posts"][i]
			var at: Vector3 = post["position"]
			_bf2_hud.map_markers.append({"position": Vector2(at.x, at.z), "kind": "post",
					"color": _post_color(int(game["posts"][i]["owner"]), team)})
		var dest_health: PackedFloat32Array = game.get("destructibles", PackedFloat32Array())
		for sys: Dictionary in _battlefield.get("space_systems", []):
			var d_idx := int(sys.get("destructible", -1))
			if d_idx < 0 or d_idx >= dest_health.size() or dest_health[d_idx] <= 0.0:
				continue
			var pos: Vector3 = sys["position"]
			var steam := int(sys.get("owner_team", -1))
			_bf2_hud.map_markers.append({"position": Vector2(pos.x, pos.z), "kind": "waypoint",
					"color": FRIEND_COLOR if steam == team else ENEMY_COLOR})
	for m: Dictionary in mobile_spawns(client, team):
		var mat: Vector3 = m["position"]
		_bf2_hud.map_markers.append({"position": Vector2(mat.x, mat.z), "kind": "waypoint",
				"color": FRIEND_COLOR})
	for droid: Node3D in get_tree().get_nodes_in_group(Bf2Map.POWERUPS):
		if not droid.visible:
			continue
		var at := droid.global_position
		_bf2_hud.map_markers.append({"position": Vector2(at.x, at.z), "kind": "droid", "color": Color.WHITE,
				"icon": str((droid.get_meta("powerup") as Dictionary).get("map_texture", ""))})
	for remote: Dictionary in client.get_remotes():
		if not bool(remote["alive"]):
			continue
		var friend: bool = team >= 0 and client.is_allied(team, int(remote["team"]))
		if not friend and float(_fired.get(int(remote["id"]), 1e9)) > SEEN_FIRING:
			continue
		var at: Vector3 = remote["position"]
		_bf2_hud.map_markers.append({"position": Vector2(at.x, at.z), "kind": "soldier",
				"color": FRIEND_COLOR if friend else ENEMY_COLOR})


# Where a ray (from `origin`, unit `dir`) enters an axis-aligned box: its
# distance, 0 if it starts inside, -1 if it misses.
static func _ray_box(origin: Vector3, dir: Vector3, center: Vector3, half: Vector3) -> float:
	var t_near := -INF
	var t_far := INF
	for axis in 3:
		var o := origin[axis] - center[axis]
		var d := dir[axis]
		if absf(d) < 1e-6:
			if absf(o) > half[axis]:
				return -1.0
			continue
		var t1 := (-half[axis] - o) / d
		var t2 := (half[axis] - o) / d
		t_near = maxf(t_near, minf(t1, t2))
		t_far = minf(t_far, maxf(t1, t2))
	if t_near > t_far or t_far < 0.0:
		return -1.0
	return maxf(t_near, 0.0)


# What the reticle can name: {key, name, at (where to aim), team, friend,
# hostile, health, max_health}. Soldiers and players always; in the world
# also its people (citizens, creatures, bandits), the practice targets, and
# the workbenches and terminals.
func _aim_candidates(client: Object, team: int) -> Array:
	var out: Array = []
	var in_world: bool = client.get_match().is_empty()
	for remote: Dictionary in client.get_remotes():
		if not bool(remote["alive"]):
			continue
		var person := int(remote.get("look", 0)) > 0
		if person and (not in_world or str(remote.get("name", "")) == ""):
			continue  # (a shop's keeper is named by their shop, below)
		var kind := int(remote.get("actor_kind", 0))
		var friend: bool = not person and team >= 0 and client.is_allied(team, int(remote["team"]))
		out.append({"key": "e%d" % int(remote["id"]), "name": str(remote.get("name", "")),
				"at": (remote["position"] as Vector3) + Vector3.UP * (0.7 if kind == 2 else 1.1),
				"team": int(remote["team"]), "friend": friend,
				"hostile": kind >= 2 or (not person and not friend), "peaceful": person and kind < 2,
				"health": float(remote.get("health", 0.0)), "max_health": float(remote.get("max_health", 0.0))})
	if not in_world:
		var classes: Array = _battlefield.get("vehicles", [])
		var own_id := int(client.get_entity_id())
		for v: Dictionary in client.get_vehicles():
			if not bool(v.get("alive", false)) or int(v.get("driver", 0)) == own_id:
				continue
			var vteam := int(v.get("team", -1))
			if vteam < 0:
				continue
			var cls_idx := int(v.get("vehicle", -1))
			var vlabel := "Vehicle"
			if cls_idx >= 0 and cls_idx < classes.size():
				vlabel = str((classes[cls_idx] as Dictionary).get("label", vlabel))
			var friend: bool = team >= 0 and client.is_allied(team, vteam)
			out.append({"key": "v%d" % int(v["id"]), "name": vlabel, "at": v["position"], "team": vteam,
					"friend": friend, "hostile": not friend, "peaceful": false,
					"health": float(v.get("health", 0.0)), "max_health": float(v.get("max_health", 0.0)),
					"range": 1000.0, "margin": 8.0})
		var game: Dictionary = client.get_match()
		var dest_health: PackedFloat32Array = game.get("destructibles", PackedFloat32Array())
		for sys: Dictionary in _battlefield.get("space_systems", []):
			var d_idx := int(sys.get("destructible", -1))
			if d_idx < 0 or d_idx >= dest_health.size() or dest_health[d_idx] <= 0.0:
				continue
			var steam := int(sys.get("owner_team", -1))
			var friend: bool = team >= 0 and steam >= 0 and client.is_allied(team, steam)
			var ctrl := int(sys.get("shield_controller", -1))
			var shielded := ctrl >= 0 and ctrl < dest_health.size() and dest_health[ctrl] > 0.0
			var sname := str(sys.get("label", "System")) + (" [SHIELDED]" if shielded else "")
			out.append({"key": "s%d" % d_idx, "name": sname, "at": sys["position"], "team": steam,
					"friend": friend, "hostile": not friend, "peaceful": false,
					"health": float(dest_health[d_idx]), "max_health": float(sys.get("max_health", 0.0)),
					"range": 1400.0, "margin": 65.0, "box": sys.get("half_size", Vector3.ZERO)})
	if in_world and hub_ui_ref != null:
		for t: Dictionary in client.get_targets():
			if not bool(t["alive"]):
				continue
			var dps := float(hub_ui_ref.call("target_dps", int(t["id"])))
			out.append({"key": "t%d" % int(t["id"]),
					"name": str(t["name"]) + ("  -  %d DPS" % roundi(dps) if dps > 0.5 else ""),
					"at": (t["position"] as Vector3) + Vector3.UP * 1.0, "team": -1, "friend": false, "hostile": true,
					"health": float(t["health"]), "max_health": float(t["max_health"])})
		var givers: Array = client.get_quest_npcs().filter(func(npc: Dictionary) -> bool: return bool(npc.get("giver", false)))
		for group: Array in [client.get_vendors(), client.get_quartermasters(), givers, client.get_workbenches(),
				client.get_recruiters(), client.get_mission_terminals()]:
			for place: Dictionary in group:
				if str(place.get("kind", "")) == "travel_offworld":
					continue  # another planet's starport (Stage 13)
				out.append({"key": "p" + str(place["name"]) + str(place["position"]), "name": str(place["name"]),
						"at": (place["position"] as Vector3) + Vector3.UP * 1.0, "team": -1, "friend": false,
						"hostile": false, "health": 0.0, "max_health": 0.0})
	return out


# The candidate nearest the reticle, within its cone (wider up close), and
# not behind the map.
func _aimed_at(client: Object, camera: Camera3D, candidates: Array) -> Dictionary:
	var from := camera.global_position
	var forward := -camera.global_basis.z
	var best := {}
	var best_angle := TARGET_CONE
	# A big target (a ship's system, a frigate: hundreds of meters across) is
	# aimed at when the reticle's ray goes into its bounds, the nearest such;
	# what's precisely under the reticle (a fighter in front of it) wins.
	var box_hit := {}
	var box_t := INF
	for c: Dictionary in candidates:
		var half: Vector3 = c.get("box", Vector3.ZERO)
		if half == Vector3.ZERO:
			continue
		var t := _ray_box(from, forward, c["at"], half.abs())
		if t >= 0.0 and t < box_t and t <= float(c.get("range", TARGET_RANGE)):
			box_t = t
			box_hit = c.duplicate()
			box_hit["at"] = from + forward * maxf(t, 0.5)
			box_hit["margin"] = 6.0
	if not box_hit.is_empty():
		best = box_hit
		best_angle = TARGET_CONE * 0.5
	for c: Dictionary in candidates:
		if c.has("box") and c["box"] != Vector3.ZERO:
			continue
		var to: Vector3 = (c["at"] as Vector3) - from
		var distance := to.length()
		var max_range := float(c.get("range", TARGET_RANGE))
		if distance > max_range or distance < 0.5:
			continue
		var angle := forward.angle_to(to)
		var pad := float(c.get("margin", 1.0))
		if angle < maxf(best_angle, 0.6 * pad / distance) and angle < best_angle + 0.6 * pad / distance:
			best = c
			best_angle = angle
	if not best.is_empty():
		# A wall between (more than a margin short of it: its own collision doesn't count).
		var along := float(client.raycast(from, best["at"]))
		var distance := from.distance_to(best["at"])
		var margin := float(best.get("margin", 1.0))
		if along >= 0.0 and along * distance < distance - margin:
			return {}
	return best


func _update_match_hud(client: Object, delta: float, hub_ui: Node, generation: int, who_cb: Callable) -> void:
	_center.text = ""
	if _center.has_theme_font_override("font"):
		# Battlefront's fonts are drawn for a 600-pixel-high screen.
		var font_size := roundi(float(_bf2_hud.art.line_height(CENTER_FONT)) * _center.get_viewport_rect().size.y / 600.0)
		if _center.get_theme_font_size("font_size") != font_size:
			_center.add_theme_font_size_override("font_size", font_size)
	var game: Dictionary = client.get_match()
	if game.is_empty() or not client.is_spawned():
		_match_label.text = ""
		var own_state: Dictionary = client.get_presented() if client.is_spawned() else {}
		if client.get_status() in ["transferring", "connecting"] and generation > 0:
			_center.text = "Traveling..."
		elif not own_state.is_empty() and not own_state["alive"]:
			_center.text = "Eliminated. Respawning..."
		elif hub_ui != null and hub_ui.notice_time() > 0 and hub_ui.notice_text() != "" and not hub_ui.has_ui():
			_center.text = hub_ui.notice_text()
		return
	var own: Dictionary = client.get_presented()
	var team := int(own["team"])
	var score_limit := int(game.get("score_limit", 0))
	var scores: Array = game.get("scores", [0, 0])
	var text := "[center][color=#%s]%s %d[/color]     [color=#%s]%s %d[/color]\n" % [
			_post_color(0, team).to_html(false), _team_name(0), int(scores[0]) if score_limit > 0 else ceili(game["tickets"][0]),
			_post_color(1, team).to_html(false), _team_name(1), int(scores[1]) if score_limit > 0 else ceili(game["tickets"][1])]
	match game["phase"]:
		"warmup":
			text += "Warmup: the match starts in %d s\n" % ceili(game["time_left"])
		"live":
			if score_limit > 0:
				text += "Goal: %d pts\n" % score_limit
			elif int(game["dominant"]) >= 0:
				text += "%s holds every command post: victory in %d s\n" % [_team_name(int(game["dominant"])),
						ceili(game["victory_left"])]
			else:
				text += "\n"
	var names := PackedStringArray()
	for i in (game["posts"] as Array).size():
		names.append("[color=#%s]%s[/color]" % [_post_color(int(game["posts"][i]["owner"]), team).to_html(false),
				_battlefield["posts"][i]["name"]])
	_match_label.text = text + "  ".join(names) + "[/center]"
	if _bf2_hud != null:
		var warmup: bool = game["phase"] == "warmup"
		if warmup:
			_match_label.text = "[center]\n\n\n\n\nWarmup: the match starts in %d s[/center]" % ceili(game["time_left"])
		elif str(game.get("mode", "")) in ["ctf", "1flag", "hunt"]:
			_match_label.text = _mode_banner(game, team, score_limit)
		elif score_limit > 0:
			_match_label.text = _space_systems_banner(game, team, score_limit)
		else:
			_match_label.text = ""

	var position: Vector3 = own["position"]
	var inside: bool = client.in_battlefield(position)
	_outside = 0.0 if inside or not own["alive"] else _outside + delta

	if _spawn_screen != null and (game["phase"] == "ended" or own["life"] == "waiting"):
		pass
	elif game["phase"] == "ended":
		_center.text = _results(client, game, team, who_cb)
	elif own["life"] == "waiting":
		_center.text = _spawn_menu(game, team)
	elif own["life"] == "dead":
		_center.text = "Eliminated"
	elif _outside > 0:
		_center.text = "Return to the battlefield!"


# Stage 11: the flag modes' and Hunt's banner: the score to reach, the time
# left, and where the flags are.
func _mode_banner(game: Dictionary, team: int, score_limit: int) -> String:
	var ally := team if team >= 0 and team < 2 else 0
	var scores: Array = game.get("scores", [0, 0])
	var mode := str(game.get("mode", ""))
	var goal := "%d captures" % score_limit if mode != "hunt" else "%d kills" % score_limit
	var secs := maxi(0, ceili(float(game.get("time_left", 0.0))))
	var line := "[center][color=#%s]%s %d[/color]  |  %s %s  |  %d:%02d  |  [color=#%s]%d %s[/color]" % [
			FRIEND_COLOR.to_html(false), _team_name(ally), int(scores[ally]),
			str(_battlefield.get("mode_name", mode)), goal, secs / 60, secs % 60,
			ENEMY_COLOR.to_html(false), int(scores[1 - ally]), _team_name(1 - ally)]
	var flags: Array = game.get("flags", [])
	var defs: Array = _battlefield.get("flags", [])
	var notes := PackedStringArray()
	for i in mini(flags.size(), defs.size()):
		var owner := int(defs[i]["owner"])
		var whose := "The flag" if owner < 0 else ("Your flag" if owner == ally else "Their flag")
		var state: Dictionary = flags[i]
		if int(state.get("carrier", 0)) != 0:
			notes.append("%s is carried" % whose)
		elif not bool(state.get("home", true)):
			notes.append("%s lies dropped (%ds)" % [whose, maxi(0, 30 - int(float(state.get("dropped_for", 0.0))))])
		else:
			notes.append("%s is home" % whose)
	if not notes.is_empty():
		line += "\n" + "   ".join(notes)
	return line + "[/center]"


# Driving a flyer: where its guns point, on screen (0..1 of its size), as core
# vehicle_aim reckons it (the camera's look standing in for the pilot's);
# (-1, -1) when not flying or it's off screen.
func _flyer_aim_point(own: Dictionary, camera: Camera3D) -> Vector2:
	if driving.is_empty() or str(driving.get("kind", "")) != "flyer" or int(own.get("seat", 0)) != 0 or camera == null:
		return Vector2(-1, -1)
	var forward := -camera.global_basis.z
	var look_yaw := atan2(-forward.x, -forward.z)
	var look_pitch := asin(clampf(forward.y, -1.0, 1.0))
	var nose_yaw := float(own.get("yaw", 0.0))
	var nose_pitch := float(own.get("pitch", 0.0))
	var yaw := nose_yaw + clampf(wrapf(look_yaw - nose_yaw, -PI, PI), float(driving.get("yaw_min", -0.26)),
			float(driving.get("yaw_max", 0.26)))
	var pitch := nose_pitch + clampf(look_pitch - nose_pitch, float(driving.get("pitch_min", -0.09)),
			float(driving.get("pitch_max", 0.09)))
	var dir := Vector3(-sin(yaw) * cos(pitch), sin(pitch), -cos(yaw) * cos(pitch))
	var at: Vector3 = (own["position"] as Vector3) + dir * 250.0
	if camera.is_position_behind(at):
		return Vector2(-1, -1)
	var screen := camera.unproject_position(at)
	var view := camera.get_viewport().get_visible_rect().size
	if view.x <= 0.0 or view.y <= 0.0:
		return Vector2(-1, -1)
	return Vector2(clampf(screen.x / view.x, 0.02, 0.98), clampf(screen.y / view.y, 0.02, 0.98))


# A vehicle weapon's HUD panel (weapon1 or weapon2): its name, model, and
# its own ammo or heat, whichever slot is the active one.
func _vehicle_weapon_bindings(b: Dictionary, prefix: String, weapon: Dictionary, state: Dictionary) -> void:
	if weapon.is_empty():
		return
	b[prefix + ".name"] = _display_name(str(weapon["name"]))
	b[prefix + ".change"] = true
	var model := str(weapon.get("held_model", ""))
	b[prefix + ".mesh"] = model if model != "" else str(weapon["name"])
	b.erase(prefix + ".totalClipFraction")
	b.erase(prefix + ".heat")
	b.erase(prefix + ".ammoInfinite")
	if int(weapon.get("rounds_per_clip", 0)) > 0:
		var rounds := int(state.get("rounds", 0))
		var reserve := int(state.get("reserve", -1))
		b[prefix + ".totalClipFraction"] = float(rounds) / float(weapon["rounds_per_clip"])
		b[prefix + ".totalAmmoBullets"] = str(rounds) if reserve < 0 else "%d/%d" % [rounds, reserve]
	else:
		b[prefix + ".heat"] = clampf(float(state.get("heat", 0.0)), 0.0, 1.0)
		b[prefix + ".ammoInfinite"] = true
		b.erase(prefix + ".totalAmmoBullets")


func _space_systems_banner(game: Dictionary, team: int, score_limit: int) -> String:
	var dest_health: PackedFloat32Array = game.get("destructibles", PackedFloat32Array())
	var counts := [[0, 0], [0, 0]]
	var shields := [false, false]
	for sys: Dictionary in _battlefield.get("space_systems", []):
		var steam := int(sys.get("owner_team", -1))
		var d_idx := int(sys.get("destructible", -1))
		if steam < 0 or steam > 1 or d_idx < 0 or d_idx >= dest_health.size():
			continue
		counts[steam][1] += 1
		if dest_health[d_idx] > 0.0:
			counts[steam][0] += 1
			if str(sys.get("kind", "")) == "shieldgen":
				shields[steam] = true
	var ally := team if team >= 0 and team < 2 else 0
	var enemy := 1 - ally
	return "[center][color=#%s]Allied Shields %s (%d/%d)[/color]  |  Goal %d  |  [color=#%s]Enemy Shields %s (%d/%d)[/color][/center]" % [
			FRIEND_COLOR.to_html(false), "ON" if shields[ally] else "OFF", counts[ally][0], counts[ally][1],
			score_limit,
			ENEMY_COLOR.to_html(false), "ON" if shields[enemy] else "OFF", counts[enemy][0], counts[enemy][1]]


func _spawn_menu(game: Dictionary, team: int) -> String:
	var lines := PackedStringArray()
	lines.append("%s: you fight for the %s" % [_battlefield["name"], _team_name(team)])
	lines.append("")
	var loadouts: Array = _battlefield["teams"][team]["loadouts"]
	spawn_loadout = mini(spawn_loadout, loadouts.size() - 1)
	for i in loadouts.size():
		var loadout: Dictionary = loadouts[i]
		lines.append("%s %d  %s  (%s, %d health)" % [">" if i == spawn_loadout else "  ", i + 1, loadout["name"],
				", ".join(loadout["weapons"] + ([loadout["secondary"]] if loadout["secondary"] != "" else [])),
				roundi(loadout["max_health"])])
	lines.append("")
	var post_count := (_battlefield.get("posts", []) as Array).size()
	if spawn_post < 0:
		lines.append("Your team holds no command posts")
	elif spawn_post < post_count:
		lines.append("Command post (Q / E):  < %s >" % _battlefield["posts"][spawn_post]["name"])
		lines.append("")
		lines.append("Spawning..." if spawn_requested_flag else "Space or click to spawn")
	else:
		lines.append("Command post (Q / E):  < Mobile Transport >")
		lines.append("")
		lines.append("Spawning..." if spawn_requested_flag else "Space or click to spawn")
	if game["phase"] == "live" and int(game.get("score_limit", 0)) <= 0 and ceili(game["tickets"][team]) <= 0:
		lines.append("No reinforcements left")
	return "\n".join(lines)


func _results(client: Object, game: Dictionary, team: int, who_cb: Callable) -> String:
	var winner := int(game["winner"])
	var lines := PackedStringArray()
	lines.append("Draw" if winner < 0 else ("Victory! " if winner == team else "Defeat. ") + "%s wins" % _team_name(winner))
	lines.append("")
	var everyone: Array = client.get_remotes()
	var own: Dictionary = client.get_presented().duplicate()
	own["id"] = client.get_entity_id()
	everyone.append(own)
	for side in 2:
		lines.append(_team_name(side))
		var players := everyone.filter(func(p: Dictionary) -> bool: return int(p["team"]) == side)
		players.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return int(a["kills"]) > int(b["kills"]))
		for p: Dictionary in players:
			lines.append("%-12s %3d kills %3d deaths %3d captures" % [
					who_cb.call(p["id"]), p["kills"], p["deaths"], p["captures"]])
		lines.append("")
	lines.append("Next match in %d s" % ceili(game["time_left"]))
	return "\n".join(lines)


func _post_models() -> Dictionary:
	var out := {}
	var path := "res://" + str(_map_meta.get("bake", "")) + ".objects.json"
	if _map_meta.get("bake", "") == "" or not FileAccess.file_exists(path):
		return out
	var data: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not data is Dictionary:
		return out
	var insignia := {}
	for object: Dictionary in data.get("objects", []):
		if object.has("hologram"):
			var at: Array = object["position"]
			var hologram: Dictionary = object["hologram"]
			# A space map's posts show com_icon_swap for every side: one model
			# Battlefront retextures for the side holding it. Drawn as it is,
			# it's no side's insignia; each side's own, from a map that has them.
			var images: Dictionary = hologram.get("images", {})
			if images.values().any(func(f: Variant) -> bool: return str(f).contains("com_icon_swap")):
				if insignia.is_empty():
					insignia = _insignia_elsewhere()
				if not insignia.is_empty():
					hologram = hologram.duplicate()
					hologram["images"] = insignia
			out[str(object["name"]).to_lower()] = {"position": Vector3(at[0], at[1], at[2]), "hologram": hologram}
	return out


# Each side's insignia model from an exported map that has all four (as a
# post hologram's images: relative to this map's folder).
func _insignia_elsewhere() -> Dictionary:
	var files := {"alliance": "com_icon_alliance", "empire": "com_icon_imperial", "republic": "com_icon_republic",
			"cis": "com_icon_cis"}
	var maps := DirAccess.open("res://imported_assets/maps")
	if maps == null:
		return {}
	for map_name: String in maps.get_directories():
		var found := {}
		for side: String in files:
			var rel := "%s/models/%s.gltf" % [map_name, files[side]]
			if FileAccess.file_exists("res://imported_assets/maps/" + rel):
				found[side] = "../" + rel
		if found.size() == files.size():
			return found
	return {}


func _add_hologram(model: Dictionary) -> Dictionary:
	var spec: Dictionary = model["hologram"]
	var pivot := Node3D.new()
	pivot.position = model["position"] + Vector3.UP * float(spec["height"])
	pivot.scale = Vector3.ONE * float(spec["size"])
	_world_root.add_child(pivot)
	var images := {}
	for side: String in spec["images"]:
		var document := GLTFDocument.new()
		var state := GLTFState.new()
		var file := ProjectSettings.globalize_path("res://" + str(_map_meta["dir"]).path_join(spec["images"][side]))
		if document.append_from_file(file, state) != OK:
			continue
		var scene: Node3D = document.generate_scene(state)
		scene.visible = false
		pivot.add_child(scene)
		var materials: Array = []
		for mesh: MeshInstance3D in scene.find_children("*", "MeshInstance3D", true, false):
			mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			for surface in mesh.mesh.get_surface_count():
				var source := mesh.mesh.surface_get_material(surface) as StandardMaterial3D
				var glow := source.duplicate() as StandardMaterial3D if source != null else StandardMaterial3D.new()
				glow.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
				glow.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
				glow.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
				glow.cull_mode = BaseMaterial3D.CULL_DISABLED
				glow.no_depth_test = false
				mesh.set_surface_override_material(surface, glow)
				materials.append(glow)
		images[side] = {"node": scene, "materials": materials}
	var light := OmniLight3D.new()
	light.omni_range = float(spec["light_radius"])
	light.light_energy = 0.5
	light.visible = false
	light.position = model["position"] + Vector3.UP * float(spec["height"])
	_world_root.add_child(light)
	var height := float(spec["height"]) - HOLO_LENS_HEIGHT
	var cylinder := CylinderMesh.new()
	cylinder.bottom_radius = HOLO_LENS_RADIUS
	cylinder.top_radius = 0.34 * float(spec["size"])
	cylinder.height = height
	cylinder.radial_segments = 32
	cylinder.rings = 1
	cylinder.cap_top = false
	cylinder.cap_bottom = false
	var cone_material := ShaderMaterial.new()
	cone_material.shader = _holo_cone_shader()
	cone_material.set_shader_parameter("cone_height", height)
	var cone := MeshInstance3D.new()
	cone.mesh = cylinder
	cone.material_override = cone_material
	cone.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	cone.visible = false
	cone.position = model["position"] + Vector3.UP * (HOLO_LENS_HEIGHT + height / 2)
	_world_root.add_child(cone)
	return {"pivot": pivot, "images": images, "light": light, "cone": cone, "cone_material": cone_material,
			"rate": float(spec["rotate_rate"])}


func _holo_cone_shader() -> Shader:
	if _cone_shader == null:
		_cone_shader = Shader.new()
		_cone_shader.code = """
shader_type spatial;
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, shadows_disabled;
uniform vec3 color : source_color = vec3(1.0);
uniform float strength = 1.0;
uniform float cone_height = 3.0;
varying float up;
void vertex() {
	up = clamp(VERTEX.y / cone_height + 0.5, 0.0, 1.0);
}
void fragment() {
	float rim = pow(1.0 - abs(dot(normalize(NORMAL), normalize(VIEW))), 2.0);
	float fade = mix(1.0, 0.15, up) * smoothstep(0.0, 0.06, up) * smoothstep(1.0, 0.85, up);
	float scan = 0.8 + 0.2 * sin(up * 70.0 - TIME * 6.0);
	float streaks = 0.85 + 0.15 * sin(UV.x * 6.2832 * 9.0 + TIME * 0.7);
	ALBEDO = color;
	ALPHA = clamp((0.12 + 0.5 * rim) * fade * scan * streaks * strength, 0.0, 1.0);
}
"""
	return _cone_shader


func _update_hologram(holo: Dictionary, state: Dictionary, own_team: int, elapsed: float) -> void:
	var owner := int(state["owner"])
	var shown := owner if owner >= 0 else int(state["holder"])
	var side := _holo_side(shown) if shown >= 0 else ""
	var color := _post_color(owner, own_team)
	if bool(state["contested"]):
		color = NEUTRAL_COLOR
	var flicker := 0.85 + 0.15 * sin(elapsed * 23.0) * sin(elapsed * 7.0)
	(holo["pivot"] as Node3D).rotation.y = elapsed * float(holo["rate"]) * TAU
	for key: String in holo["images"]:
		var image: Dictionary = holo["images"][key]
		(image["node"] as Node3D).visible = key == side
		if key == side:
			for material: StandardMaterial3D in image["materials"]:
				material.albedo_color = Color(color, 0.55 * flicker)
	var light: OmniLight3D = holo["light"]
	light.visible = side != ""
	light.light_color = color
	(holo["cone"] as Node3D).visible = side != ""
	var cone_material: ShaderMaterial = holo["cone_material"]
	cone_material.set_shader_parameter("color", color)
	cone_material.set_shader_parameter("strength", flicker)


func _post_color(owner: int, own_team: int) -> Color:
	if owner < 0:
		return NEUTRAL_COLOR
	return FRIEND_COLOR if owner == own_team else ENEMY_COLOR


func _team_name(team: int) -> String:
	if _battlefield.is_empty() or team < 0 or team > 1:
		return "nobody"
	return str(_battlefield["teams"][team]["name"])


func _team_icon(team: int) -> String:
	if team < 0 or team > 1:
		return ""  # a battlefield's natives (team 2): no side's icon
	var tname := str(_battlefield["teams"][team]["name"]).to_lower() if not _battlefield.is_empty() else ""
	if tname.contains("rebel"):
		return "all_icon"
	if tname.contains("empire") or tname.contains("imperial"):
		return "imp_icon"
	if tname.contains("republic"):
		return "rep_icon"
	if tname.contains("separatist") or tname.contains("cis"):
		return "cis_icon"
	return "all_icon" if team == 0 else "imp_icon"


func _side_code(team: int) -> String:
	if team < 0 or _battlefield.is_empty():
		return ""
	match _team_icon(team):
		"all_icon": return "all"
		"imp_icon": return "imp"
		"rep_icon": return "rep"
		"cis_icon": return "cis"
	return ""


func _holo_side(team: int) -> String:
	match _team_icon(team):
		"all_icon": return "alliance"
		"imp_icon": return "empire"
		"rep_icon": return "republic"
		"cis_icon": return "cis"
	return ""


func _map_code() -> String:
	var stem := _world_bake.get_file().get_basename().to_lower()
	if stem.length() >= 3:
		return stem.substr(0, 3)
	return "dag"


func _play_music_cue(side: String, planet: String, cue: String, loop: bool) -> void:
	var primary := "%s_%s_amb_%s" % [side, planet, cue]
	if _audio.has_sound(primary):
		_audio.set_music(primary, loop)
		return
	var fallback_planet := "dag" if side in ["all", "imp"] else "geo"
	var fallback := "%s_%s_amb_%s" % [side, fallback_planet, cue]
	if _audio.has_sound(fallback):
		_audio.set_music(fallback, loop)


func _message_color(event: Dictionary, client: Object) -> Color:
	var own_team := int(client.get_presented().get("team", -1)) if client.is_spawned() else -1
	var scorer := -1
	if event["kind"] == "kill":
		var attacker := int(event["attacker"])
		if attacker == client.get_entity_id():
			scorer = own_team
		else:
			for remote: Dictionary in client.get_remotes():
				if int(remote["id"]) == attacker:
					scorer = int(remote["team"])
	else:
		scorer = int(event["attacker"])
	if scorer < 0 or own_team < 0:
		return Color8(223, 223, 223)
	return Color8(80, 140, 255) if scorer == own_team else Color8(223, 60, 60)


static func _unit_name(loadout: String) -> String:
	var at := loadout.find("_inf_")
	return (loadout.substr(at + 5) if at >= 0 else loadout).replace("_", " ").capitalize()


func _display_name(weapon_class: String) -> String:
	if _display_names.has(weapon_class):
		return str(_display_names[weapon_class])
	var wname := weapon_class
	for prefix in ["_weap_inf_", "_weap_"]:
		var at := wname.find(prefix)
		if at >= 0:
			wname = wname.substr(at + prefix.length())
			break
	return wname.replace("thermaldetonator", "thermal_detonator").replace("_", " ").capitalize()
