# Dev tool: screenshots the UI (Battlefront's HUD and the screens built in its
# style) with sample data, without a match: for checking how they look.
#
#   godot --path game --script res://tools/preview_ui.gd -- <output_prefix> [<map bake prefix> <battlefield.ini>]
#
# Writes <output_prefix>hud.png, pause.png, options.png, scores.png, end.png,
# spawn.png, and a hub's: terminal.png (the war terminal), queued.png (its
# screen while queued), hub.png (the tooltip and the queue over the HUD) and
# back.png (how the last battle went). With a map, the minimap uses its image as the battlefield
# file places it. Needs a real window and bf2tool export-hud's output.
extends SceneTree

const Bf2Hud = preload("res://ui/bf2_hud.gd")
const Settings = preload("res://ui/settings.gd")
const SpawnScreen = preload("res://ui/spawn_screen.gd")
const ScoreScreen = preload("res://ui/score_screen.gd")
const PauseMenu = preload("res://ui/pause_menu.gd")
const WarTerminalScreen = preload("res://ui/war_terminal_screen.gd")
const HubHud = preload("res://ui/hub_hud.gd")

const SHOTS := ["hud", "pause", "options", "scores", "end", "spawn", "terminal", "queued", "hub", "back"]

var _prefix := ""
var _hud: Bf2Hud
var _spawn: SpawnScreen
var _scores: ScoreScreen
var _pause: PauseMenu
var _terminal: WarTerminalScreen
var _hub: HubHud
var _index := -1
var _frames := 0
var _minimap := {}


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		printerr("usage: -- <output_prefix> [<map bake prefix> <battlefield.ini>]")
		quit(2)
		return
	_prefix = args[0]
	DisplayServer.window_set_size(Vector2i(1280, 720))
	var backdrop := ColorRect.new()
	backdrop.color = Color(0.22, 0.27, 0.25)
	backdrop.set_anchors_preset(Control.PRESET_FULL_RECT)
	var layer := CanvasLayer.new()
	root.add_child(layer)
	layer.add_child(backdrop)
	_hud = Bf2Hud.new()
	if not _hud.load_layout("res://imported_assets/hud"):
		printerr("no HUD export in game/imported_assets/hud (bf2tool export-hud)")
		quit(1)
		return
	layer.add_child(_hud)
	if args.size() >= 3:
		_minimap = _load_minimap(args[1], args[2])
	_spawn = SpawnScreen.new(_hud.art)
	_scores = ScoreScreen.new(_hud.art)
	_pause = PauseMenu.new(_hud.art)
	_pause.settings = Settings.new()
	_terminal = WarTerminalScreen.new(_hud.art)
	_hub = HubHud.new(_hud.art)
	for screen: Control in [_spawn, _scores, _pause, _terminal, _hub]:
		screen.visible = false
		layer.add_child(screen)
	_sample_hud()
	_sample_screens()
	_next()


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames < 4:
		return false
	var path := "%s%s.png" % [_prefix, SHOTS[_index]]
	root.get_texture().get_image().save_png(path)
	print("wrote ", path)
	_next()
	return false


func _next() -> void:
	_index += 1
	_frames = 0
	if _index >= SHOTS.size():
		quit(0)
		return
	var shot: String = SHOTS[_index]
	_pause.visible = shot in ["pause", "options"]
	if _pause.visible:
		_pause.open()
		if shot == "options":
			_pause._page = "options"
			_pause.in_match = true
	_scores.visible = shot in ["scores", "end"]
	_scores.ended = shot == "end"
	_spawn.visible = shot == "spawn"
	_terminal.visible = shot in ["terminal", "queued"]
	_terminal.queued = 1 if shot == "queued" else -1
	_hub.visible = shot in ["hub", "back"]
	_hub.tooltip = "Press E to use the War Terminal" if shot == "hub" else ""
	_hub.queue = {"side": "Empire", "icon": "imp_icon", "battlefield": "Polis Massa", "starts_in": 6.2,
			"in_queue": 3, "leave_key": "X"} if shot == "hub" else {}
	_hub.notice = "Victory at Endor: 12 kills, 1 death, 3 captures" if shot == "back" else ""
	_hub.notice_alpha = 1.0
	for control: Control in [_hud, _spawn, _scores, _pause, _terminal, _hub]:
		control.queue_redraw()


func _sample_hud() -> void:
	var b := _hud.bindings
	b["player1.weaponsEnable"] = true
	b["player1.weapon1.reticule.position"] = true
	b["player1.health"] = true
	b["player1.healthFraction"] = 0.7
	b["player1.energyFraction"] = 0.45
	b["player1.weapon1.target.teamColor"] = Color(1.0, 0.3, 0.25)
	b["player1.weapon1.target.teamColorBright"] = Color(1.0, 0.5, 0.45)
	b["player1.weapon1.target.hit"] = true
	b["player1.weapon1.target.hitColor"] = Color8(223, 60, 60)
	b["player1.weapon1.name"] = "Rifle"
	b["player1.weapon1.change"] = true
	b["player1.weapon1.mesh"] = "imp_weap_inf_rifle"
	b["player1.weapon1.totalClipFraction"] = 0.6
	b["player1.weapon1.totalAmmoBullets"] = "30/200"
	b["player1.weapon2.name"] = "Thermal Detonator"
	b["player1.weapon2.change"] = true
	b["player1.weapon2.mesh"] = "imp_weap_inf_thermaldetonator"
	b["player1.weapon2.totalAmmoBullets"] = "4"
	b["player1.team1.texture"] = "imp_icon"
	b["player1.team1.reinforcements"] = 112
	b["player1.team2.texture"] = "all_icon"
	b["player1.team2.reinforcements"] = 97
	b["victorytimer"] = 14
	b["player1.map.enable"] = true
	_hud.messages = [{"text": "player 3 eliminated player 8", "age": 0.0, "color": Color8(80, 140, 255)},
			{"text": "Empire captured CP4", "age": 0.0, "color": Color8(80, 140, 255)},
			{"text": "player 11 eliminated you", "age": 0.0, "color": Color8(223, 60, 60)}]
	if not _minimap.is_empty():
		_hud.minimap = _minimap
		_hud.map_player = {"position": Vector2(-30, 20), "yaw": 0.6}
		_hud.map_markers = [
			{"position": Vector2(-10, -60), "kind": "post", "color": Color(0.35, 0.6, 1.0)},
			{"position": Vector2(40, 60), "kind": "post", "color": Color(1.0, 0.3, 0.25)},
			{"position": Vector2(-60, 30), "kind": "post", "color": Color(0.9, 0.9, 0.9)},
			{"position": Vector2(-25, 35), "kind": "soldier", "color": Color(0.35, 0.6, 1.0)},
			{"position": Vector2(-5, 10), "kind": "soldier", "color": Color(1.0, 0.3, 0.25)}]
	_hud.apply_tables()


func _sample_screens() -> void:
	var players := func(prefix: String, count: int, you: int) -> Array:
		var out: Array = []
		for i in count:
			out.append({"name": "You" if i == you else "%s %d" % [prefix, i + 1], "kills": (i * 7) % 11,
					"deaths": (i * 5) % 7, "captures": i % 3, "you": i == you})
		return out
	_scores.sides = [
		{"name": "Empire", "icon": "imp_icon", "color": Color(0.35, 0.6, 1.0), "tickets": 112,
				"players": players.call("player", 7, 2)},
		{"name": "Rebels", "icon": "all_icon", "color": Color(1.0, 0.3, 0.25), "tickets": 97,
				"players": players.call("player", 7, -1)}]
	_scores.own_team = 0
	_scores.winner = 0
	_scores.next_in = 12
	_spawn.title = "Dagobah"
	_spawn.side = {"name": "Empire", "icon": "imp_icon", "color": Color(0.35, 0.6, 1.0)}
	_spawn.loadouts = [
		{"name": "imp_inf_rifleman", "weapons": ["Rifle", "Pistol", "Thermal Detonator"], "max_health": 300},
		{"name": "imp_inf_rocketeer", "weapons": ["Rocket Launcher", "Pistol", "Thermal Detonator"], "max_health": 300},
		{"name": "imp_inf_sniper", "weapons": ["Sniper Rifle", "Pistol"], "max_health": 300},
		{"name": "imp_inf_engineer", "weapons": ["Shotgun", "Fusion Cutter"], "max_health": 300}]
	_spawn.loadout = 1
	_spawn.posts = [
		{"name": "CP1", "position": Vector2(-10, -60), "color": Color(0.35, 0.6, 1.0), "ours": true},
		{"name": "CP2", "position": Vector2(-60, 30), "color": Color(0.9, 0.9, 0.9), "ours": false},
		{"name": "CP3", "position": Vector2(40, 60), "color": Color(1.0, 0.3, 0.25), "ours": false},
		{"name": "CP4", "position": Vector2(10, -10), "color": Color(0.35, 0.6, 1.0), "ours": true}]
	_spawn.post = 0
	_spawn.minimap = _minimap
	_terminal.title = "War Terminal"
	_terminal.sides = [{"name": "Rebellion", "icon": "all_icon"}, {"name": "Empire", "icon": "imp_icon"}]
	_terminal.battlefield = "Polis Massa"
	_terminal.starts_in = 6.2
	_terminal.in_queue = 3
	_terminal.keys = {"leave": "X", "close": "Esc"}


func _load_minimap(bake: String, battlefield: String) -> Dictionary:
	var meta: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://" + bake + ".json"))
	var file := str(meta.get("minimap", ""))
	if file == "":
		return {}
	var image := Image.load_from_file(ProjectSettings.globalize_path("res://" + bake.get_base_dir().path_join(file)))
	var edges := [0.0, 0.0, 0.0, 0.0]
	for line in FileAccess.get_file_as_string("res://" + battlefield).split("\n"):
		if line.strip_edges().begins_with("Minimap"):
			var v := line.split("=")[1].strip_edges().split(" ", false)
			edges = [-float(v[0]), float(v[1]), -float(v[2]), float(v[3])]
	return {"texture": ImageTexture.create_from_image(image), "left_x": edges[0], "top_z": edges[1],
			"right_x": edges[2], "bottom_z": edges[3]}
