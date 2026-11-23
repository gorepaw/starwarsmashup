# A match's vehicles, drawn: each class's model (bf2tool export-vehicle's
# glTF) where the server says the vehicle is. A driven vehicle is drawn
# where its driver is drawn (the own one where it's predicted, others'
# where they're shown, a little in the past), so the rider and the vehicle
# move together; an empty one eases to where it is. Models face +z; a yaw of
# 0 faces -z, so they turn by yaw + PI.
# A walker steps as it goes (its export's clips, in place): a start, then
# its two strides in turn at its speed (StrideSpeed is theirs), and a stop
# from whichever pose it's in; it idles, turns on the spot, and falls when
# destroyed.
extends RefCounted

const EASE := 12.0  # 1/s: how fast an empty vehicle's drawn place catches up
const WALKING := 0.4  # m/s: a walker going slower is standing
const TURNING := 0.35  # rad/s: a standing walker turning faster turns on the spot

var classes: Array = []      # the battlefield's "vehicles" (SwmClient.get_battlefield)
var _root: Node3D
var _templates := {}         # class index -> Node3D (a loaded model, kept out of the tree)
var _nodes := {}             # vehicle id -> {node, class, alive}


func setup(root: Node3D, vehicle_classes: Array) -> void:
	clear()
	_root = root
	classes = vehicle_classes


func clear() -> void:
	for entry: Dictionary in _nodes.values():
		var node: Node = entry["node"]
		if is_instance_valid(node):
			node.queue_free()
	_nodes.clear()
	for template: Node in _templates.values():
		if is_instance_valid(template):
			template.free()
	_templates.clear()


func class_of(index: int) -> Dictionary:
	return classes[index] if index >= 0 and index < classes.size() else {}


# A point in a vehicle's model space (+z forward) in the world, for a vehicle
# at `position` with heading `yaw`.
static func to_world(position: Vector3, yaw: float, local: Vector3) -> Vector3:
	return position + Basis(Vector3.UP, yaw + PI) * local


func _template(index: int) -> Node3D:
	if _templates.has(index):
		return _templates[index]
	var model := str(class_of(index).get("model", ""))
	var node: Node3D = null
	if model != "" and FileAccess.file_exists(model):
		var document := GLTFDocument.new()
		var state := GLTFState.new()
		if document.append_from_file(model, state) == OK:
			node = document.generate_scene(state) as Node3D
	_templates[index] = node
	return node


# `vehicles`: SwmClient.get_vehicles(); `shown`: entity id -> its drawn
# {position, yaw} (the own character's predicted state, others' presented
# ones), for driven vehicles.
func update(vehicles: Array, shown: Dictionary, delta: float) -> void:
	var seen := {}
	for v: Dictionary in vehicles:
		var id := int(v["id"])
		seen[id] = true
		var index := int(v["vehicle"])
		if not _nodes.has(id):
			var template := _template(index)
			var node: Node3D = template.duplicate() if template != null else _stand_in(index)
			_root.add_child(node)
			node.position = v["position"]
			node.rotation.y = float(v["yaw"]) + PI
			var players := node.find_children("*", "AnimationPlayer", true, false)
			_nodes[id] = {"node": node, "class": index, "walk": {
				"player": players[0] if not players.is_empty() else null, "state": "", "next": "a",
				"last": node.position, "yaw": node.rotation.y, "speed": 0.0, "turn": 0.0}}
		var entry: Dictionary = _nodes[id]
		entry["state"] = v
		var node: Node3D = entry["node"]
		node.visible = bool(v["alive"])
		var driver := int(v["driver"])
		if driver != 0 and shown.has(driver):
			var at: Dictionary = shown[driver]
			node.position = at["position"]
			node.rotation.y = float(at["yaw"]) + PI
		else:
			var t := minf(1.0, EASE * delta)
			node.position = node.position.lerp(v["position"], t)
			node.rotation.y = lerp_angle(node.rotation.y, float(v["yaw"]) + PI, t)
		if str(class_of(index).get("kind", "")) == "walker":
			_step(entry, bool(v["alive"]), delta)
	for id: int in _nodes.keys():
		if not seen.has(id):
			(_nodes[id]["node"] as Node).queue_free()
			_nodes.erase(id)


# A walker's legs: what it's doing from how its drawn self moved this frame.
func _step(entry: Dictionary, alive: bool, delta: float) -> void:
	var walk: Dictionary = entry["walk"]
	var player: AnimationPlayer = walk["player"]
	var node: Node3D = entry["node"]
	if player == null or delta <= 0:
		return
	var moved: Vector3 = node.position - (walk["last"] as Vector3)
	var forward := node.basis.z  # models face +z
	var speed := Vector2(moved.x, moved.z).length() / delta * signf(moved.dot(forward) + 1e-4)
	var turn := angle_difference(float(walk["yaw"]), node.rotation.y) / delta
	walk["last"] = node.position
	walk["yaw"] = node.rotation.y
	# Smoothed: drawn positions step with the snapshots.
	walk["speed"] = lerpf(float(walk["speed"]), speed, minf(1.0, 8.0 * delta))
	walk["turn"] = lerpf(float(walk["turn"]), turn, minf(1.0, 8.0 * delta))
	speed = float(walk["speed"])
	var state := str(walk["state"])
	if not alive:
		if state != "dead":
			_play(walk, "death01", 1.0)
			walk["state"] = "dead"
		return
	var stride := float(class_of(int(entry["class"])).get("stride_speed", 0.0))
	var rate := clampf(absf(speed) / stride, 0.35, 2.0) if stride > 0 else 1.0
	var backward := speed < 0
	var done := not player.is_playing()
	if absf(speed) > WALKING:
		if state in ["", "idle", "turn"] or (state == "stop" and done):
			_play(walk, "walkstart_" + str(walk["next"]), rate, backward)
			walk["state"] = "start"
		elif state in ["start", "walk"] and done:
			_play(walk, "walk_" + str(walk["next"]), rate, backward)
			walk["next"] = "b" if walk["next"] == "a" else "a"
			walk["state"] = "walk"
		elif state in ["start", "walk"]:
			player.speed_scale = rate
	elif state in ["start", "walk"]:
		if done:
			# Each stride ends where the other starts.
			_play(walk, "walkstop_" + str(walk["next"]), 1.0)
			walk["state"] = "stop"
	elif state != "stop" or done:
		var turning := absf(float(walk["turn"])) > TURNING
		if turning:
			var clip := "turnl" if float(walk["turn"]) > 0 else "turnr"
			if state != "turn" or done or player.current_animation != clip:
				_play(walk, clip, 1.0)
			walk["state"] = "turn"
		elif state != "idle" or done:
			_play(walk, "idle", 1.0)
			walk["state"] = "idle"


func _play(walk: Dictionary, clip: String, rate: float, backward := false) -> void:
	var player: AnimationPlayer = walk["player"]
	if not player.has_animation(clip):
		return
	player.speed_scale = rate
	if backward:
		player.play_backwards(clip)
	else:
		player.play(clip)


# Where the vehicle `entity` drives or gunners is drawn: {position, yaw,
# seat (0 driving)}, or {} if it rides in none.
func riding(entity: int) -> Dictionary:
	for entry: Dictionary in _nodes.values():
		var v: Dictionary = entry.get("state", {})
		var seat := -1
		if int(v.get("driver", 0)) == entity:
			seat = 0
		else:
			var gunners: Array = v.get("gunners", [])
			if gunners.has(entity):
				seat = gunners.find(entity) + 1
		if seat >= 0:
			var node: Node3D = entry["node"]
			return {"position": node.position, "yaw": node.rotation.y - PI, "seat": seat, "class": entry["class"]}
	return {}


# The seat a soldier of `team` would get in the nearest vehicle within reach
# (as the server reckons it: 2.5 m from its body, ASSUMPTION a little less
# to be sure): {vehicle: its state, seat: 0 the driver's, 1.. a gunner's},
# or {}. A gunner's seat only beside one's own side.
func enterable(vehicles: Array, position: Vector3, team := -1) -> Dictionary:
	var best := {}
	var best_d := 2.2
	var chest := position + Vector3.UP
	for v: Dictionary in vehicles:
		if not bool(v["alive"]):
			continue
		var seat := -1
		var gunners: Array = v.get("gunners", [])
		var aboard := int(v["driver"]) != 0 or gunners.any(func(g: int) -> bool: return g != 0)
		if int(v["driver"]) == 0 and not (aboard and int(v["team"]) != team):
			seat = 0
		elif int(v["team"]) == team and gunners.has(0):
			seat = gunners.find(0) + 1
		if seat < 0:
			continue
		var c := class_of(int(v["vehicle"]))
		if c.is_empty():
			continue
		var local: Vector3 = Basis(Vector3.UP, float(v["yaw"]) + PI).inverse() * (chest - (v["position"] as Vector3))
		var lo: Vector3 = c["bounds_min"]
		var hi: Vector3 = c["bounds_max"]
		var d := Vector3(maxf(maxf(lo.x - local.x, 0.0), local.x - hi.x), maxf(maxf(lo.y - local.y, 0.0), local.y - hi.y),
				maxf(maxf(lo.z - local.z, 0.0), local.z - hi.z)).length()
		if d <= best_d:
			best_d = d
			best = {"vehicle": v, "seat": seat}
	return best


# Without its model: a box its size.
func _stand_in(index: int) -> Node3D:
	var c := class_of(index)
	var lo: Vector3 = c.get("bounds_min", Vector3(-0.5, 0, -1.5))
	var hi: Vector3 = c.get("bounds_max", Vector3(0.5, 1.2, 1.5))
	var root := Node3D.new()
	var box := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = hi - lo
	box.mesh = mesh
	box.position = (hi + lo) * 0.5
	root.add_child(box)
	return root
