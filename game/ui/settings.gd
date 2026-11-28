# Player settings: controls (rebindable, as Godot input actions), mouse,
# view and display. Saved in user://settings.cfg.
#
#   var settings := Settings.new()
#   settings.load_file()      # also applies them: input actions, window
#   Input.is_action_pressed("move_forward")
extends RefCounted

const PATH := "user://settings.cfg"

# Actions in the order the options screen lists them: name, label, default
# (a physical key, or a mouse button as "mouse:<button>").
const ACTIONS := [
	["move_forward", "Move Forward", KEY_W],
	["move_back", "Move Back", KEY_S],
	["move_left", "Strafe Left", KEY_A],
	["move_right", "Strafe Right", KEY_D],
	["sprint", "Sprint", KEY_SHIFT],
	["jump", "Jump", KEY_SPACE],
	["crouch", "Crouch / Roll", KEY_C],
	["zoom", "Zoom", KEY_Z],
	["fire", "Fire", "mouse:1"],
	["block", "Block / Parry (Melee)", KEY_F],
	["grenade", "Use Tactical", "mouse:2"],
	["reload", "Reload", KEY_R],
	["primary", "Primary Weapon", KEY_1],
	["sidearm", "Sidearm", KEY_2],
	["toggle_view", "First / Third Person", KEY_V],
	["scores", "Show Scores", KEY_TAB],
	["interact", "Use", KEY_E],
	["leave_queue", "Leave the Queue", KEY_X],
	["inventory", "Inventory & Loadouts", KEY_I],
	["quest_log", "Quest Log", KEY_L],
	["world_map", "Map", KEY_M],
	["professions", "Professions", KEY_K],
	["scanner", "Multi-Scanner", KEY_G],
	["workbench", "Workbench & Material Vault", KEY_B],
	["summon_mount", "Mount / Speeder", KEY_H],
	["loadout_1", "Loadout 1", KEY_F5],
	["loadout_2", "Loadout 2", KEY_F6],
	["loadout_3", "Loadout 3", KEY_F7],
	["loadout_4", "Loadout 4", KEY_F8],
	["loadout_5", "Loadout 5", KEY_F9],
	# Stage 12: the specialist kit, the datapad and companions.
	["ability_1", "Ability 1", KEY_3],
	["ability_2", "Ability 2", KEY_4],
	["ability_3", "Ability 3", KEY_5],
	["ability_4", "Ability 4", KEY_6],
	["ability_5", "Ability 5", KEY_7],
	["ability_6", "Ability 6", KEY_8],
	["datapad", "Datapad", KEY_P],
	["companion_command", "Companion: Attack / Guard / Follow", KEY_T],
	["companion_summon", "Companion: Field / Store", KEY_Y],
	["beacon_spawn", "Spawn at Squad Beacon", KEY_N],
]

var bindings := {}           # action -> physical keycode (int) or "mouse:<button>"
var mouse_sensitivity := 1.0 # times the base sensitivity
var invert_mouse := false
var field_of_view := 65.0    # degrees, vertical
var first_person := false
var fullscreen := false
var vsync := true
var master_volume := 0.8
var sfx_volume := 1.0
var music_volume := 0.65
var voice_volume := 1.0
var server_address := ""     # the last server joined ("host:port"); empty: none yet
# The specialist ability bar (SpecialistUI.resolve_bar): what the player put on
# each of its six keys ("" leaves a key to fill itself, "-" leaves it empty),
# and the abilities they took off it, which stay off the keys left to fill.
var bar_layout := []
var bar_off := []


func _init() -> void:
	for action: Array in ACTIONS:
		bindings[action[0]] = action[2]


func load_file() -> void:
	var file := ConfigFile.new()
	if file.load(PATH) == OK:
		mouse_sensitivity = float(file.get_value("mouse", "sensitivity", mouse_sensitivity))
		invert_mouse = bool(file.get_value("mouse", "invert", invert_mouse))
		field_of_view = float(file.get_value("view", "field_of_view", field_of_view))
		first_person = bool(file.get_value("view", "first_person", first_person))
		fullscreen = bool(file.get_value("display", "fullscreen", fullscreen))
		vsync = bool(file.get_value("display", "vsync", vsync))
		master_volume = clampf(float(file.get_value("audio", "master", master_volume)), 0.0, 1.0)
		sfx_volume = clampf(float(file.get_value("audio", "sfx", sfx_volume)), 0.0, 1.0)
		music_volume = clampf(float(file.get_value("audio", "music", music_volume)), 0.0, 1.0)
		voice_volume = clampf(float(file.get_value("audio", "voice", voice_volume)), 0.0, 1.0)
		server_address = str(file.get_value("network", "server", server_address))
		for action: Array in ACTIONS:
			bindings[action[0]] = file.get_value("controls", action[0], bindings[action[0]])
		var layout: Variant = file.get_value("bar", "layout", bar_layout)
		var off: Variant = file.get_value("bar", "off", bar_off)
		bar_layout = (layout as Array).duplicate() if layout is Array else []
		bar_off = (off as Array).duplicate() if off is Array else []
	apply()


func save_file() -> void:
	var file := ConfigFile.new()
	file.set_value("mouse", "sensitivity", mouse_sensitivity)
	file.set_value("mouse", "invert", invert_mouse)
	file.set_value("view", "field_of_view", field_of_view)
	file.set_value("view", "first_person", first_person)
	file.set_value("display", "fullscreen", fullscreen)
	file.set_value("display", "vsync", vsync)
	file.set_value("audio", "master", master_volume)
	file.set_value("audio", "sfx", sfx_volume)
	file.set_value("audio", "music", music_volume)
	file.set_value("audio", "voice", voice_volume)
	file.set_value("network", "server", server_address)
	for action: Array in ACTIONS:
		file.set_value("controls", action[0], bindings[action[0]])
	file.set_value("bar", "layout", bar_layout)
	file.set_value("bar", "off", bar_off)
	file.save(PATH)


# Sets up the input actions from the bindings, and the window.
func apply() -> void:
	for action: Array in ACTIONS:
		var name: String = action[0]
		if not InputMap.has_action(name):
			InputMap.add_action(name)
		InputMap.action_erase_events(name)
		InputMap.action_add_event(name, event_for(bindings[name]))
	if DisplayServer.get_name() != "headless":
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_FULLSCREEN if fullscreen
				else DisplayServer.WINDOW_MODE_WINDOWED)
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_ENABLED if vsync else DisplayServer.VSYNC_DISABLED)
	for pair: Array in [["Master", master_volume], ["SFX", sfx_volume], ["Music", music_volume], ["Voice", voice_volume]]:
		var idx := AudioServer.get_bus_index(pair[0])
		if idx >= 0:
			var v := clampf(float(pair[1]), 0.0, 1.0)
			AudioServer.set_bus_mute(idx, v <= 0.001)
			AudioServer.set_bus_volume_db(idx, linear_to_db(maxf(v, 0.0001)))


static func event_for(binding: Variant) -> InputEvent:
	if binding is String and str(binding).begins_with("mouse:"):
		var button := InputEventMouseButton.new()
		button.button_index = int(str(binding).substr(6)) as MouseButton
		return button
	var key := InputEventKey.new()
	key.physical_keycode = int(binding) as Key
	return key


# A binding from a pressed key or mouse button; null for other events.
static func binding_from(event: InputEvent) -> Variant:
	if event is InputEventKey and event.pressed:
		var key := event as InputEventKey
		return int(key.physical_keycode if key.physical_keycode != KEY_NONE else key.keycode)
	if event is InputEventMouseButton and event.pressed:
		return "mouse:%d" % int((event as InputEventMouseButton).button_index)
	return null


static func binding_name(binding: Variant) -> String:
	if binding is String and str(binding).begins_with("mouse:"):
		match int(str(binding).substr(6)):
			MOUSE_BUTTON_LEFT: return "Left Mouse"
			MOUSE_BUTTON_RIGHT: return "Right Mouse"
			MOUSE_BUTTON_MIDDLE: return "Middle Mouse"
			MOUSE_BUTTON_WHEEL_UP: return "Wheel Up"
			MOUSE_BUTTON_WHEEL_DOWN: return "Wheel Down"
			var other: return "Mouse %d" % other
	return OS.get_keycode_string(int(binding) as Key)


func label_of(action: String) -> String:
	for entry: Array in ACTIONS:
		if entry[0] == action:
			return entry[1]
	return action
