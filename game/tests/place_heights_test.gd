# Headless checks, in a live Mos Eisley session, that the places' symbols
# stand just over what's there: each crafting station and mission terminal
# near the start has its model in the city (SwgCity.prop_top), and its
# symbol's height over the server's spot for it puts the symbol a little
# over that model's top, however high the spot came out (a terminal's top,
# or the ground beside its platform). And the tutorial's craft step's
# waypoint (known only across) stands over its station, not under the ground.
extends SceneTree

const GameSession = preload("res://scenes/game_session.gd")
const SwgCity = preload("res://rendering/swg_city.gd")
const HubUI = preload("res://scenes/hub_ui.gd")

var _session: GameSession
var _db := ""
var _frames := 0


func _initialize() -> void:
	_db = ProjectSettings.globalize_path("res://place_heights_temp.sqlite")
	_remove_db()
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/tatooine" if FileAccess.file_exists("res://imported_assets/terrain/tatooine.json")
				else "imported_assets/terrain/tatooine_mos_eisley",
		"host": 0,
		"db": _db,
		"hub": "hubs/mos_eisley.ini",
		"name": "Surveyor",
		"species": "human",
		"gender": "male",
		"appearance": "{}",
		"solo": true,
	}
	_session = (load("res://scenes/game_session.tscn") as PackedScene).instantiate() as GameSession
	root.add_child(_session)


func _process(_delta: float) -> bool:
	_frames += 1
	var client: Object = _session.get_client()
	if client == null or not client.is_spawned() or client.get_workbenches().is_empty():
		if _frames > 900:
			_fail("never spawned")
		return false
	var world: Node = _session.get("_world")
	if world == null or not world.has_meta(SwgCity.PROPS):
		_fail("the city's props weren't recorded")
		return true
	var hub_ui: HubUI = _session.get("_hub_ui")
	var own: Vector3 = client.get_presented()["position"]
	var checked := 0
	var places: Array = []
	for i in (client.get_workbenches() as Array).size():
		places.append(["workbench:%d" % i, client.get_workbenches()[i], HubUI.HOLO_OVER_PROP])
	for t: Dictionary in client.get_mission_terminals():
		places.append(["mt%d" % int(t["index"]), t, HubUI.CHEVRON_OVER_PROP])
	for p: Array in places:
		var at: Vector3 = (p[1] as Dictionary)["position"]
		if Vector2(at.x - own.x, at.z - own.z).length() > 400.0:
			continue
		var top := SwgCity.prop_top(world, at)
		if is_nan(top):
			top = SwgCity.prop_top(world, at, 1.2, 15.0, 15.0)  # (as hub_ui: a spot a floor off)
		if is_nan(top):
			_fail("no model found at %s %s (%s)" % [p[0], str((p[1] as Dictionary).get("name", "")), at])
			return true
		# The server's spot is on the model's floor, not a cellar under it (the
		# Imperial terminal, its middle in a house's wall, once came out 10 m down).
		if top - at.y > 6.0 or top - at.y < -0.5:  # (a kiosk's platform: 4.5)
			_fail("%s: spot y %.2f is off its model (top %.2f)" % [p[0], at.y, top])
			return true
		var height: float = hub_ui._height_over_prop(p[0], at, float(p[2]), -1.0)
		var symbol_y := at.y + height
		# (Mos Eisley's mission kiosks stand on platforms up to 2.5 m over the ground beside them.)
		if absf(symbol_y - (top + float(p[2]))) > 0.01:
			_fail("%s: spot y %.2f, model top %.2f, symbol at %.2f" % [p[0], at.y, top, symbol_y])
			return true
		print("  %s %s: spot %.2f, top %.2f (%.2f over), symbol %.2f" % [p[0], str((p[1] as Dictionary).get("name", "")),
				at.y, top, top - at.y, symbol_y])
		checked += 1
	if checked < 4:
		_fail("only %d places near the start" % checked)
		return true
	# The tutorial's craft step: the Weapon & Droid station, given across only.
	var station := Vector3(-3497.0, 0.0, -4756.9)  # (game x is -Galaxies x)
	var marker: Vector3 = hub_ui._waypoint_marker(client, station, own)
	var station_top := SwgCity.prop_top(world, Vector3(station.x, own.y, station.z), 1.2, 15.0, 15.0)
	if is_nan(station_top) or absf(marker.y - (station_top + HubUI.CHEVRON_OVER_PROP)) > 0.05:
		_fail("craft waypoint at %.2f, the station's top %.2f" % [marker.y, station_top])
		return true
	print("Place heights: OK (%d places; craft waypoint %.2f, over its station's top %.2f)" % [checked, marker.y, station_top])
	_cleanup(0)
	return true


func _fail(why: String) -> void:
	printerr("FAIL: ", why)
	_cleanup(1)


func _remove_db() -> void:
	for suffix in ["", "-wal", "-shm"]:
		if FileAccess.file_exists(_db + suffix):
			DirAccess.remove_absolute(_db + suffix)


func _cleanup(code: int) -> void:
	if _session != null:
		_session.leave_session()
		_session.queue_free()
		_session = null
	_remove_db()
	quit(code)
