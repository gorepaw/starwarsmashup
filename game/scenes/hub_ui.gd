# Hub UI & world recruiter markers: War Terminal screen, queue HUD,
# recruiter pillars/rings, and battle outcome notices.
class_name HubUI
extends Node

const Settings = preload("res://ui/settings.gd")
const WarTerminalScreen = preload("res://ui/war_terminal_screen.gd")
const HubHud = preload("res://ui/hub_hud.gd")

signal queue_requested(side: int)
signal terminal_closed

var _hub_hud: HubHud
var _terminal_screen: WarTerminalScreen
var _settings: Settings
var _world_root: Node3D
var _recruiter_nodes: Array = []
var _notice_id := 0
var _notice := ""
var _notice_time := 0.0
var _notice_lines: PackedStringArray = []


func setup(layer: CanvasLayer, world_root: Node3D, art: RefCounted, settings: Settings) -> void:
	_world_root = world_root
	_settings = settings
	if art == null:
		return
	_hub_hud = HubHud.new(art)
	layer.add_child(_hub_hud)
	_terminal_screen = WarTerminalScreen.new(art)
	_terminal_screen.visible = false
	_terminal_screen.side_chosen.connect(func(side: int) -> void: queue_requested.emit(side))
	_terminal_screen.leave_requested.connect(func() -> void: queue_requested.emit(-1))
	_terminal_screen.closed.connect(close_terminal)
	layer.add_child(_terminal_screen)


func has_ui() -> bool:
	return _terminal_screen != null


func is_terminal_open() -> bool:
	return _terminal_screen != null and _terminal_screen.visible


func try_open_terminal(client: Object) -> bool:
	if _terminal_screen == null or at_recruiter(client).is_empty():
		return false
	_terminal_screen.visible = true
	return true


func close_terminal() -> void:
	if _terminal_screen != null:
		_terminal_screen.visible = false
	terminal_closed.emit()


func clear_recruiters() -> void:
	for node: Node in _recruiter_nodes:
		if is_instance_valid(node):
			node.queue_free()
	_recruiter_nodes.clear()


func on_world_arrived() -> void:
	clear_recruiters()
	if _terminal_screen != null and _terminal_screen.visible:
		close_terminal()
	_notice_id = 0


func add_recruiters(client: Object, world: Node3D) -> void:
	if not _recruiter_nodes.is_empty() or client == null or world == null:
		return
	var recruiters: Array = client.get_recruiters()
	if recruiters.is_empty():
		return
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color(1.0, 0.8, 0.25)
	var terminals: Array[Vector3] = []
	for node: Node in world.get_children():
		if node.has_meta("recruiter"):
			terminals.append((node as Node3D).position)
	for recruiter: Dictionary in recruiters:
		var position: Vector3 = recruiter["position"]
		var drawn := terminals.any(func(at: Vector3) -> bool:
			return Vector2(at.x - position.x, at.z - position.z).length() < 1.5)
		var pillar := MeshInstance3D.new()
		var box := BoxMesh.new()
		box.size = Vector3(0.6, 2.2, 0.6)
		pillar.mesh = box
		pillar.material_override = material
		pillar.position = position + Vector3.UP * 1.1
		var ring := MeshInstance3D.new()
		var torus := TorusMesh.new()
		torus.outer_radius = float(recruiter["radius"])
		torus.inner_radius = float(recruiter["radius"]) - 0.15
		ring.mesh = torus
		ring.material_override = material
		ring.position = position + Vector3.UP * 0.1
		var label := Label3D.new()
		label.text = str(recruiter["name"])
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.fixed_size = true
		label.pixel_size = 0.0015
		label.font_size = 28
		label.outline_size = 8
		label.modulate = material.albedo_color
		label.position = position + Vector3.UP * 3.2
		for node: Node3D in [pillar, ring, label]:
			if drawn:
				node.free()
				continue
			_world_root.add_child(node)
			_recruiter_nodes.append(node)


func at_recruiter(client: Object) -> Dictionary:
	if client == null or not client.is_spawned():
		return {}
	var position: Vector3 = client.get_presented()["position"]
	for recruiter: Dictionary in client.get_recruiters():
		var at: Vector3 = recruiter["position"]
		if Vector2(position.x - at.x, position.z - at.z).length() <= float(recruiter["radius"]):
			return recruiter
	return {}


func take_notice_lines() -> PackedStringArray:
	var out := _notice_lines.duplicate()
	_notice_lines.clear()
	return out


func notice_text() -> String:
	return _notice


func notice_time() -> float:
	return _notice_time


func update_hub(client: Object, world: Node3D, delta: float, pause_visible: bool,
		center_label: Label, match_label: RichTextLabel) -> void:
	_notice_time = maxf(0.0, _notice_time - delta)
	if client == null or not client.is_spawned():
		return
	add_recruiters(client, world)
	var status: Dictionary = client.get_player_status()
	if int(status.get("notice_id", 0)) != _notice_id:
		_notice_id = int(status["notice_id"])
		_notice = str(status.get("notice", ""))
		_notice_time = 10.0
		if _notice != "":
			print("notice: ", _notice)
			_notice_lines.append(_notice)

	if _hub_hud == null or _terminal_screen == null:
		if not client.get_recruiters().is_empty() and client.get_match().is_empty():
			_update_hub_text_hud(client, center_label, match_label)
		return

	var in_hub: bool = client.is_spawned() and not client.get_recruiters().is_empty() and client.get_match().is_empty()
	var terminal: Dictionary = at_recruiter(client) if in_hub else {}
	if _terminal_screen.visible and terminal.is_empty():
		close_terminal()
	var factions: PackedStringArray = client.get_factions()
	var queued: int = int(status.get("team", -1)) if status.get("queue", "none") == "queued" else -1
	var sides: Array = []
	for i in factions.size():
		sides.append({"name": factions[i], "icon": _side_icon(factions[i], i)})
	if _terminal_screen.visible:
		_terminal_screen.title = str(terminal["name"])
		_terminal_screen.sides = sides
		_terminal_screen.queued = queued
		_terminal_screen.battlefield = str(status.get("battlefield", ""))
		_terminal_screen.starts_in = float(status.get("starts_in", 0))
		_terminal_screen.in_queue = int(status.get("in_queue", 0))
		_terminal_screen.keys = {"leave": _key_name("leave_queue"), "close": "Esc"}
		_terminal_screen.queue_redraw()
	_hub_hud.visible = in_hub and not (pause_visible or _terminal_screen.visible)
	_hub_hud.tooltip = "Press %s to use the %s" % [_key_name("interact"), terminal["name"]] \
			if not terminal.is_empty() and queued < 0 else ""
	_hub_hud.queue = {} if queued < 0 or queued >= sides.size() else {"side": sides[queued]["name"],
			"icon": sides[queued]["icon"], "battlefield": str(status.get("battlefield", "")),
			"starts_in": float(status.get("starts_in", 0)), "in_queue": int(status.get("in_queue", 0)),
			"leave_key": _key_name("leave_queue")}
	_hub_hud.notice = _notice
	_hub_hud.notice_alpha = clampf(_notice_time, 0.0, 1.0)
	_hub_hud.queue_redraw()


func _update_hub_text_hud(client: Object, center_label: Label, match_label: RichTextLabel) -> void:
	var factions: PackedStringArray = client.get_factions()
	var status: Dictionary = client.get_player_status()
	if status.get("queue", "none") == "queued" and factions.size() == 2:
		var where := str(status.get("battlefield", ""))
		match_label.text = "[center]Queued for the %s: %d in the queue, the battle%s starts in %d s\n[X] leave the queue[/center]" % [
				factions[int(status["team"])], status["in_queue"], " on " + where if where != "" else "",
				ceili(status["starts_in"])]
		return
	match_label.text = ""
	var terminal := at_recruiter(client)
	if not terminal.is_empty() and factions.size() == 2 and center_label.text == "":
		center_label.text = "%s\n[F] fight for the %s     [G] fight for the %s" % [
				terminal["name"], factions[0], factions[1]]


func _side_icon(side: String, index: int) -> String:
	var sname := side.to_lower()
	if sname.contains("rebel"):
		return "all_icon"
	if sname.contains("empire") or sname.contains("imperial"):
		return "imp_icon"
	if sname.contains("republic"):
		return "rep_icon"
	if sname.contains("separatist") or sname.contains("cis"):
		return "cis_icon"
	return "all_icon" if index == 0 else "imp_icon"


func _key_name(action: String) -> String:
	return Settings.binding_name(_settings.bindings[action]) if _settings != null else action
