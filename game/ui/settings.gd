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
	["grenade", "Throw Grenade", "mouse:2"],
	["reload", "Reload", KEY_R],
	["primary", "Primary Weapon", KEY_1],
	["sidearm", "Sidearm", KEY_2],
	["toggle_view", "First / Third Person", KEY_V],
	["scores", "Show Scores", KEY_TAB],
	["interact", "Use", KEY_E],
	["leave_queue", "Leave the Queue", KEY_X],
]

var bindings := {}           # action -> physical keycode (int) or "mouse:<button>"
var mouse_sensitivity := 1.0 # times the base sensitivity
var invert_mouse := false
var field_of_view := 65.0    # degrees, vertical
var first_person := false
var fullscreen := false
var vsync := true
var bot_difficulty := "normal"  # easy, normal or hard: bots on a server this game hosts
const DIFFICULTIES := ["easy", "normal", "hard"]


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
		bot_difficulty = str(file.get_value("bots", "difficulty", bot_difficulty))
		if not DIFFICULTIES.has(bot_difficulty):
			bot_difficulty = "normal"
		for action: Array in ACTIONS:
			bindings[action[0]] = file.get_value("controls", action[0], bindings[action[0]])
	apply()


func save_file() -> void:
	var file := ConfigFile.new()
	file.set_value("mouse", "sensitivity", mouse_sensitivity)
	file.set_value("mouse", "invert", invert_mouse)
	file.set_value("view", "field_of_view", field_of_view)
	file.set_value("view", "first_person", first_person)
	file.set_value("display", "fullscreen", fullscreen)
	file.set_value("display", "vsync", vsync)
	file.set_value("bots", "difficulty", bot_difficulty)
	for action: Array in ACTIONS:
		file.set_value("controls", action[0], bindings[action[0]])
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
