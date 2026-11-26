# Headless checks for Stage 5's quests:
# 1. The quest screen draws offers and the log (with progress, waypoints and
#    rewards), and accepting, tracking and abandoning emit.
# 2. A live Mos Eisley session: the hub's quest givers (and the people quests
#    send players to) and its mission terminals reach the client, the journal
#    syncs, the hub HUD's tracker and objective marker draw, and the radar
#    has the hub's map.
extends SceneTree

const Bf2Art = preload("res://ui/bf2_art.gd")
const QuestScreen = preload("res://ui/quest_screen.gd")
const HubHud = preload("res://ui/hub_hud.gd")
const GameSession = preload("res://scenes/game_session.gd")

var _session: GameSession
var _db := ""
var _frames := 0


func _initialize() -> void:
	var art := Bf2Art.load_from("res://imported_assets/hud")
	assert(art != null, "missing hud art")
	var quest := {"id": "q", "title": "Pests at the Gate", "giver": "Corporal Dask",
			"summary": "Worrts and kreetles have been chewing through the moisture lines outside the city.",
			"steps": ["Kill lesser desert worrts", "Kill kreetles"], "step": 1, "count": 2, "needed": 5,
			"waypoint": Vector3(-3600, 0, -4800), "credits": 300, "experience": 150, "faction_points": 25,
			"items": ["supply_bacta_stim"], "mission": false}
	var screen := QuestScreen.new(art)
	root.add_child(screen)
	var accepted := []
	var abandoned := []
	screen.quest_accepted.connect(func(id: String) -> void: accepted.append(id))
	screen.quest_abandoned.connect(func(id: String) -> void: abandoned.append(id))
	screen.open_offers("Corporal Dask")
	assert(screen.waiting)
	screen.waiting = false
	screen.offers = [quest]
	screen._on_click("accept:q", Vector2.ZERO)
	assert(accepted == ["q"], "accept didn't emit")
	screen.active = [quest]
	screen.completed = ["A Stranger in Mos Eisley"]
	screen.open_log()
	screen._on_click("abandon:q", Vector2.ZERO)
	assert(abandoned.is_empty(), "abandon needs confirming")
	screen._on_click("abandon:q", Vector2.ZERO)
	assert(abandoned == ["q"], "abandon didn't emit")
	screen.queue_redraw()  # drawn with the frames below, in the log

	var hud := HubHud.new(art)
	root.add_child(hud)
	hud.tracker = {"title": "Pests at the Gate", "step": "Kill kreetles", "progress": "2/5", "distance": "120 m",
			"log_key": "L"}
	hud.waypoint = {"at": Vector2(1200, 60), "onscreen": false, "angle": -0.3, "distance": "120 m"}
	hud.queue_redraw()
	print("Stage 5 quest screens: OK")

	_db = ProjectSettings.globalize_path("res://stage5_quest_temp.sqlite")
	_remove_db()
	GameSession.pending_config = {
		"bake": "imported_assets/terrain/tatooine" if FileAccess.file_exists("res://imported_assets/terrain/tatooine.json")
				else "imported_assets/terrain/tatooine_mos_eisley",
		"host": 0,
		"db": _db,
		"hub": "hubs/mos_eisley.ini",
		"name": "Stage5Quester",
		"species": "human",
		"gender": "male",
		"appearance": "{}",
		"solo": true,
	}
	_session = (load("res://scenes/game_session.tscn") as PackedScene).instantiate() as GameSession
	root.add_child(_session)


func _process(_delta: float) -> bool:
	if _session == null:
		return false
	_frames += 1
	var client: Object = _session.get_client()
	if client == null or not client.is_spawned() or int(client.get_quests().get("revision", 0)) == 0:
		if _frames > 400:
			printerr("FAIL: no quest journal arrived")
			_cleanup(1)
		return false
	if _frames < 30:
		return false  # a few frames for the HUD
	var npcs: Array = client.get_quest_npcs()
	var names := []
	var givers := 0
	for npc: Dictionary in npcs:
		names.append(str(npc["name"]))
		givers += 1 if bool(npc["giver"]) else 0
		var at: Vector3 = npc["position"]
		print("  quest person %s%s at (%.1f, %.1f, %.1f)" % [npc["name"], " (gives quests)" if npc["giver"] else "",
				at.x, at.y, at.z])
	assert(givers >= 4, "expected at least 4 quest givers, got %d" % givers)
	assert(names.has("Mos Eisley Junk Dealer"), "the junk dealer (a talk_to step) isn't a quest person")
	assert(client.get_mission_terminals().size() >= 10, "expected at least 10 mission terminals")
	var quests: Dictionary = client.get_quests()
	assert((quests["active"] as Array).is_empty() and (quests["completed"] as Array).is_empty())
	var hub_ui: Node = _session.get("_hub_ui")
	assert(hub_ui != null and hub_ui.radar_markers(client).size() >= 14, "radar: 4 givers and >= 10 terminals")
	var battle_ui: Node = _session.get("_battle_ui")
	var bf2_hud: Object = battle_ui.get("_bf2_hud")
	assert(not (bf2_hud.get("minimap") as Dictionary).is_empty(), "the hub has no radar map")
	print("Stage 5 Mos Eisley quests: OK (%d quest people, %d terminals)" % [npcs.size(),
			client.get_mission_terminals().size()])
	_cleanup(0)
	return true


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
