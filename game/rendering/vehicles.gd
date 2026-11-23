# A match's vehicles, drawn: each class's model (bf2tool export-vehicle's
# glTF) where the server says the vehicle is. A driven vehicle is drawn
# where its driver is drawn (the own one where it's predicted, others'
# where they're shown, a little in the past), so the rider and the vehicle
# move together; an empty one eases to where it is. Models face +z; a yaw of
# 0 faces -z, so they turn by yaw + PI.
extends RefCounted

const EASE := 12.0  # 1/s: how fast an empty vehicle's drawn place catches up

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
			_nodes[id] = {"node": node, "class": index}
		var entry: Dictionary = _nodes[id]
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
	for id: int in _nodes.keys():
		if not seen.has(id):
			(_nodes[id]["node"] as Node).queue_free()
			_nodes.erase(id)


# The empty vehicle nearest `position` within reach of getting in (as the
# server reckons it: 2.5 m from its body, ASSUMPTION a little less to be
# sure), or {}.
func enterable(vehicles: Array, position: Vector3) -> Dictionary:
	var best := {}
	var best_d := 2.2
	var chest := position + Vector3.UP
	for v: Dictionary in vehicles:
		if int(v["driver"]) != 0 or not bool(v["alive"]):
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
			best = v
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
