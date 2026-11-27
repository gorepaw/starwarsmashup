# Stage 12 playtest in a hosted Mos Eisley session, in two runs on one
# database (SWM_PHASE):
#   create  a new character spawns in Mos Eisley, then leaves (the database
#           is kept for seeding: professions, a datapad, a mount item).
#   play    the seeded character (Creature Handler learned) fields a bantha and
#           then an R2 unit (their models drawn), rides the bantha mount, and
#           opens the datapad. Screenshots go to SWM_SHOTS (<path>NN.png).
#
#   SWM_PHASE=create <godot> --path game --script res://tests/stage12_playtest.gd
#   (seed the database: professions, trained:<id>, datapad, mount_bantha)
#   SWM_PHASE=play SWM_SHOTS=<dir>/s12_ <godot> --path game --script res://tests/stage12_playtest.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")
const SUMMON := 1
const DISMISS := 2

var _session: GameSession
var _db := ""
var _phase_name := ""
var _frames := 0
var _step := 0
var _step_frame := 0
var _shots := ""
var _shot := 0
var _ride_from := Vector3.ZERO


func _initialize() -> void:
	_phase_name = OS.get_environment("SWM_PHASE")
	_shots = OS.get_environment("SWM_SHOTS")
	_db = OS.get_environment("SWM_DB")
	if _db == "":
		_db = ProjectSettings.globalize_path("res://stage12_playtest_temp.sqlite")
	if _phase_name == "create":
		_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/tatooine",
		"host": 0,
		"db": _db,
		"hub": "hubs/mos_eisley.ini",
		"name": "Stage12Handler",
		"species": "human",
		"gender": "male",
		"appearance": "{}",
		"solo": true,
	}
	_session = packed.instantiate() as GameSession
	root.add_child(_session)


func _process(_delta: float) -> bool:
	if _session == null:
		return false
	_frames += 1
	var client: Object = _session.get_client()
	if client == null or not client.is_spawned():
		if _frames > 900:
			_fail("the character did not spawn")
		return false
	if _phase_name == "create":
		if _frames > 60:
			print("create: spawned at %s" % [client.get_presented()["position"]])
			_quit(0, false)
		return false
	var spec: Dictionary = client.get_specialist()
	var waited := _frames - _step_frame
	match _step:
		0:
			# The seeded datapad and levels arrive.
			if (spec.get("companions", []) as Array).size() >= 3:
				print("play: %d companions, %d abilities: %s" % [(spec["companions"] as Array).size(),
						(spec.get("abilities", []) as Array).size(), _ids(spec.get("abilities", []))])
				_next()
			elif waited > 600:
				_fail("no seeded datapad arrived: %s" % [spec.keys()])
		1:
			if "ch_tame" in _ids(spec.get("abilities", [])):
				print("play: learned; abilities %s" % [_ids(spec.get("abilities", []))])
				_summon(client, spec, "bantha")
				_next()
			elif waited > 300:
				_fail("Creature Handler's abilities didn't unlock: %s" % [_ids(spec.get("abilities", []))])
		2:
			if int(spec.get("companion_entity", 0)) != 0:
				if waited == 1 or _step_frame == _frames - 1:
					print("play: bantha fielded as entity %d" % int(spec["companion_entity"]))
				if _face_entity(client, int(spec["companion_entity"])) and waited > 150:
					_screenshot("bantha companion")
					client.specialist(DISMISS, "", 0, Vector3.ZERO, 0, 0, "")
					_next()
			elif waited > 300:
				_fail("the bantha wasn't fielded")
		3:
			if int(spec.get("active_companion", 0)) == 0 and waited > 30:
				_summon(client, spec, "droid_r2")
				_next()
			elif waited > 300:
				_fail("the bantha wasn't dismissed")
		4:
			if int(spec.get("companion_entity", 0)) != 0:
				if _face_entity(client, int(spec["companion_entity"])) and waited > 150:
					_screenshot("R2 companion")
					client.specialist(DISMISS, "", 0, Vector3.ZERO, 0, 0, "")
					_next()
			elif waited > 300:
				_fail("the R2 unit wasn't fielded (companion cooldown?) %s" % [spec.get("companion_cooldown", 0)])
		5:
			if waited == 30:
				var mount := 0
				for item: Dictionary in client.get_inventory():
					if str(item.get("def_id", "")) == "mount_bantha":
						mount = int(item["id"])
				if mount == 0:
					_fail("no mount_bantha item")
				client.use_item(mount)
			elif waited == 120:
				# Called, it stands by us: climb on (the use key).
				_session.set("_mouse_captured", true)
				Input.action_press("interact")
			elif waited == 130:
				Input.action_release("interact")
			elif waited > 30 and int(client.get_presented().get("vehicle", -1)) >= 0:
				_ride_from = client.get_presented()["position"]
				print("play: riding vehicle %d" % int(client.get_presented()["vehicle"]))
				_next()
			elif waited > 400:
				_fail("the bantha mount wasn't called")
		6:
			if waited == 60:
				_screenshot("mounted, standing")
				_session.set("_bot", "circle")
			if waited == 200:
				_screenshot("mounted, moving")
			if waited > 220:
				_session.set("_bot", "")
				var moved: float = (client.get_presented()["position"] as Vector3).distance_to(_ride_from)
				print("play: rode %.1f m" % moved)
				if moved < 3.0:
					_fail("the mount didn't move")
				# Riding mustn't cost the soldier their ammo (it once deleted the stack).
				var ammo := false
				for item: Dictionary in client.get_inventory():
					ammo = ammo or str(item.get("def_id", "")).begins_with("ammo_")
				if not ammo:
					_fail("the ammo stack was lost while mounted")
				_next()
		7:
			var ui: Object = _session.get("_specialist_ui")
			if waited == 10:
				# Their own settings (not the player's file): choose the bar.
				var stub := GDScript.new()
				stub.source_code = "extends RefCounted\nvar bindings := {}\nvar bar_layout := []\nvar bar_off := []\nfunc save_file() -> void:\n\tpass\n"
				stub.reload()
				var own_settings: RefCounted = stub.new()
				own_settings.set("bindings", (ui.get("_settings") as RefCounted).get("bindings"))
				ui.set("_settings", own_settings)
				var before: Array = ui.call("bar_ids")
				if before[0] != "bh_seeker_probe" or before.has("ch_tame"):
					_fail("expected Tame out of reach on the default bar: %s" % [before])
			if waited == 20:
				ui.call("_fill_datapad", client)
				ui.get("_datapad").call("open_datapad")
				# What a click on the datapad's key-3 button for Tame sends.
				ui.get("_datapad").call("_on_click", "bar:ch_tame:0", Vector2.ZERO)
				var after: Array = ui.call("bar_ids")
				print("play: bar now %s" % [after])
				# Tame takes the first key and bumps the Seeker Probe; the rest stay.
				if after[0] != "ch_tame" or after[1] != "bh_bolas" or after.has("bh_seeker_probe"):
					_fail("Tame didn't take the first key: %s" % [after])
			if waited == 60:
				_screenshot("datapad")
				ui.call("close_screens")
			if waited == 90:
				_screenshot("bar with Tame first")
				print("Stage 12 playtest: OK")
				_quit(0, true)
	return false


func _ids(list: Array) -> Array:
	var out := []
	for a: Dictionary in list:
		out.append(str(a.get("id", "")))
	return out


func _summon(client: Object, spec: Dictionary, template: String) -> void:
	for c: Dictionary in spec.get("companions", []):
		if str(c.get("template", "")) == template:
			client.specialist(SUMMON, "", 0, Vector3.ZERO, int(c["id"]), 0, "")
			return
	_fail("no %s on the datapad" % template)


# Turns the camera onto an entity; true once it's known to the client.
func _face_entity(client: Object, entity: int) -> bool:
	var own: Vector3 = client.get_presented()["position"]
	for r: Dictionary in client.get_remotes():
		if int(r.get("id", 0)) == entity:
			var to: Vector3 = (r["position"] as Vector3) - own
			_session.set("_yaw", atan2(-to.x, -to.z))
			return true
	return false


func _next() -> void:
	_step += 1
	_step_frame = _frames


func _screenshot(what: String) -> void:
	if _shots == "" or DisplayServer.get_name().to_lower().contains("headless"):
		print("(no screenshot: %s)" % what)
		return
	_shot += 1
	var path := "%s%02d.png" % [_shots, _shot]
	root.get_viewport().get_texture().get_image().save_png(path)
	print("wrote ", path, " (", what, ")")


func _fail(why: String) -> void:
	printerr("FAIL: ", why)
	_quit(1, true)


func _quit(code: int, remove: bool) -> void:
	if _session != null:
		_session.leave_session()
		_session.queue_free()
		_session = null
	if remove and OS.get_environment("SWM_KEEP_DB") == "":
		_remove_db()
	quit(code)


func _remove_db() -> void:
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(_db + suffix):
			DirAccess.remove_absolute(_db + suffix)
