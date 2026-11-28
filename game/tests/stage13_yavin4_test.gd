# Stage 13 check: Yavin 4's people and services. A hosted session on Yavin 4
# (the Mining Outpost's hub and its Labor and Imperial Outposts) with a new
# character on the plaza outside the Mining Outpost's starport:
#   - every outpost's trainers, banks, clinics, vendors and starports are on
#     offer (yavin4_quests.ini's givers too), and the Labor and Imperial
#     Outposts' starports;
#   - the Mining Outpost's combat trainer stands in his shed's main hall, on its
#     floor (Height), not somewhere under the terrain.
# Screenshots go to the path in SWBFO_SHOTS (if set), as <path>NN.png.
#
#   <godot> --path game --script res://tests/stage13_yavin4_test.gd
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")

const OUTSIDE := Vector2(-267.0, 4892.0)  # Galaxies x z: the plaza outside the starport's door
const ARVANE := Vector2(-336.99, 4838.0)    # in the shed's main hall
const ARVANE_FLOOR := 35.1                  # the shed's 35 m, and the hall's 0.1

var _session: GameSession
var _db := ""
var _frames := 0
var _phase := 0
var _phase_frame := 0
var _shots := ""
var _shot := 0


func _initialize() -> void:
	_shots = OS.get_environment("SWBFO_SHOTS")
	_db = ProjectSettings.globalize_path("res://stage13_yavin4_temp.sqlite")
	_remove_db()
	var packed := load("res://scenes/game_session.tscn") as PackedScene
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/yavin4",
		"host": 0,
		"db": _db,
		"hub": "hubs/yavin4_mining_outpost.ini",
		"name": "Stage13Yavin",
		"species": "human",
		"gender": "male",
		"appearance": "{}",
		"solo": true,
		"spawn": OUTSIDE,
		"third_person": true,
	}
	_session = packed.instantiate() as GameSession
	root.add_child(_session)


func _process(_delta: float) -> bool:
	if _session == null:
		return false
	_frames += 1
	var client: Object = _session.get_client()
	if client == null or not client.is_spawned():
		if _frames > 1800:
			_fail("not spawned")
		return false
	var pos: Vector3 = client.get_presented()["position"]
	var at := Vector2(-pos.x, pos.z)
	match _phase:
		0:
			var kinds := {}
			var names := {}
			for v: Dictionary in client.get_vendors():
				kinds[str(v["kind"])] = int(kinds.get(str(v["kind"]), 0)) + 1
				names[str(v["name"])] = true
			print("Yavin 4's services: ", kinds)
			if int(kinds.get("trainer", 0)) < 23:
				_fail("expected Yavin 4's 23 trainers, got %d" % int(kinds.get("trainer", 0)))
			for wanted: String in ["Mining Outpost Junk Dealer", "Mining Outpost Gear & Supplies", "Mining Outpost Starport",
					"Mining Outpost Bank", "Labor Outpost Bank", "Labor Outpost Starport", "Labor Outpost Junk Dealer",
					"Imperial Outpost Starport", "Imperial Outpost Quartermaster", "Imperial Outpost Junk Dealer"]:
				if not names.has(wanted):
					_fail("no " + wanted)
			var givers := {}
			for n: Dictionary in client.get_quest_npcs():
				if bool(n["giver"]):
					givers[str(n["name"])] = true
			print("Quest givers: ", givers.keys())
			for wanted: String in ["Foreman Dessa Korr", "Prospector Hale Brannock", "Overseer Mirra Tolle",
					"Storekeeper Wenn Arrow", "Archivist Ressa Quill", "Lieutenant Arden Voss"]:
				if not givers.has(wanted):
					_fail("no quest giver " + wanted)
			_next_phase()
		1:
			# Sergeant Arvane in the shed's main hall, on its floor.
			var found := false
			for remote: Dictionary in client.get_remotes():
				var p: Vector3 = remote["position"]
				if Vector2(-p.x, p.z).distance_to(ARVANE) < 1.0:
					found = true
					print("Sergeant Arvane: at (%.2f, %.2f, %.2f)" % [-p.x, p.y, p.z])
					if absf(p.y - ARVANE_FLOOR) > 0.3:
						_fail("expected him on the hall's floor (%.1f m), at %.2f" % [ARVANE_FLOOR, p.y])
			if found:
				_next_phase()
			elif _frames - _phase_frame > 600:
				_fail("Sergeant Arvane isn't there")
		2:
			if _frames - _phase_frame == 30:
				_screenshot()
			if _frames - _phase_frame > 40:
				print("Stage 13 Yavin 4: OK")
				_quit(0)
	return false


func _next_phase() -> void:
	_phase += 1
	_phase_frame = _frames


func _screenshot() -> void:
	if _shots == "" or DisplayServer.get_name().to_lower().contains("headless"):
		return
	_shot += 1
	var path := "%s%02d.png" % [_shots, _shot]
	root.get_viewport().get_texture().get_image().save_png(path)
	print("wrote ", path)


func _fail(why: String) -> void:
	printerr("FAIL: ", why)
	_quit(1)


func _quit(code: int) -> void:
	if _session != null:
		_session.leave_session()
		_session.queue_free()
		_session = null
	_remove_db()
	quit(code)


func _remove_db() -> void:
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(_db + suffix):
			DirAccess.remove_absolute(_db + suffix)
