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
# A gunner's turret (the hover tank's, the combat speeder's missile pod)
# turns and tilts on the model with the gunner's aim (its seat's TurretNode
# and PitchNode), about the vehicle's own up and right.
# Hurt, a vehicle smokes, sparks and burns as its class's damage effects say;
# a hover vehicle on the move trails its wake (dust).
# Its engine runs while someone drives it (its class's EngineSound: loops
# whose pitch and gain follow its speed, acceleration, health and how long
# it's been on or off), and a walker's feet stomp as they land.
extends RefCounted

const EASE := 12.0  # 1/s: how fast an empty vehicle's drawn place catches up
const WALKING := 0.4  # m/s: a walker going slower is standing
const TURNING := 0.35  # rad/s: a standing walker turning faster turns on the spot
const ENGINE_OFF := 4.0  # s: an engine turned off this long is silent

var classes: Array = []      # the battlefield's "vehicles" (SwmClient.get_battlefield)
var _root: Node3D
var _effects: RefCounted     # the game's Bf2Effects (null: none)
var _audio: RefCounted       # the game's Bf2Audio (null: none)
var _templates := {}         # class index -> Node3D (a loaded model, kept out of the tree)
var _nodes := {}             # vehicle id -> {node, class, alive}


func setup(root: Node3D, vehicle_classes: Array, effects: RefCounted = null, audio: RefCounted = null) -> void:
	clear()
	_root = root
	classes = vehicle_classes
	_effects = effects
	_audio = audio


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
				"last": node.position, "yaw": node.rotation.y, "speed": 0.0, "turn": 0.0},
				"turrets": _find_turrets(node, index)}
		var entry: Dictionary = _nodes[id]
		entry["state"] = v
		var node: Node3D = entry["node"]
		node.visible = bool(v["alive"])
		var driver := int(v["driver"])
		var flyer := str(class_of(index).get("kind", "")) == "flyer"
		if driver != 0 and shown.has(driver):
			var at: Dictionary = shown[driver]
			node.position = at["position"]
			node.rotation.y = float(at["yaw"]) + PI
			if flyer:
				node.rotation.x = -float(at.get("pitch", 0.0))  # its nose, up or down
				node.rotation.z = float(at.get("roll", 0.0))
			else:
				node.rotation.x = 0.0
				node.rotation.z = 0.0
		else:
			var t := minf(1.0, EASE * delta)
			node.position = node.position.lerp(v["position"], t)
			node.rotation.y = lerp_angle(node.rotation.y, float(v["yaw"]) + PI, t)
			if flyer:
				node.rotation.x = lerp_angle(node.rotation.x, -float(v.get("pitch", 0.0)), t)
				node.rotation.z = lerp_angle(node.rotation.z, float(v.get("bank", 0.0)), t)
			else:
				node.rotation.x = 0.0
				node.rotation.z = 0.0
		if str(class_of(index).get("kind", "")) == "walker":
			_step(entry, bool(v["alive"]), delta)
		elif entry["walk"]["player"] != null and (entry["walk"]["player"] as AnimationPlayer).has_animation("walk"):
			_gait(entry, bool(v["alive"]), delta)  # Stage 12: a creature mount
		_aim_turrets(entry, v, shown)
		_update_effects(entry, v)
		_update_engine(entry, v, delta)
	for id: int in _nodes.keys():
		if not seen.has(id):
			if _audio != null and not entry_engine(_nodes[id]).is_empty():
				_audio.stop_engine(entry_engine(_nodes[id]))
			(_nodes[id]["node"] as Node).queue_free()
			_nodes.erase(id)


# Its damage effects for its health, and its wake while it moves (a hover
# vehicle's, from under it).
func _update_effects(entry: Dictionary, v: Dictionary) -> void:
	if _effects == null:
		return
	var node: Node3D = entry["node"]
	var c := class_of(int(entry["class"]))
	var running: Dictionary = entry.get("effects", {})
	var percent := 100.0 * float(v["health"]) / maxf(1.0, float(v["max_health"]))
	var list: Array = c.get("damage_effects", [])
	for i in list.size():
		var e: Dictionary = list[i]
		_run(node, running, "damage%d" % i, str(e["effect"]), e["at"],
				bool(v["alive"]) and percent <= float(e["start"]) and percent > float(e["stop"]))
	var wake := str(c.get("wake_effect", ""))
	if wake != "":
		var moving := (v["velocity"] as Vector3).length() > 3.0 and bool(v["alive"])
		_run(node, running, "wake", wake, Vector3(0, -float(c.get("altitude", 0.0)), 0), moving)
	var contrail := str(c.get("contrail_effect", ""))
	if contrail != "":
		var flying := bool(v["alive"]) and not bool(v.get("grounded", false)) and (v["velocity"] as Vector3).length() > 15.0
		var bmin: Vector3 = c.get("bounds_min", Vector3.ZERO)
		_run(node, running, "contrail", contrail, Vector3(0, 0, bmin.z * 0.85), flying)
	entry["effects"] = running


# Its engine: on while it's driven (rising over TurnOnTime), off once it's
# left (dying away over TurnOffTime), its loops set by how it goes.
# ASSUMPTION: its acceleration is how fast its speed (of its top speed) is
# changing, twice over a second, smoothed.
func _update_engine(entry: Dictionary, v: Dictionary, delta: float) -> void:
	var c := class_of(int(entry["class"]))
	var sound := str(c.get("engine_sound", ""))
	if _audio == null or sound == "" or delta <= 0:
		return
	var state: Dictionary = entry.get("engine_state", {"on": false, "on_time": 99.0, "off_time": 99.0,
			"speed": 0.0, "acceleration": 0.0})
	entry["engine_state"] = state
	var running := int(v["driver"]) != 0 and bool(v["alive"])
	if running and not bool(state["on"]):
		state["on"] = true
		state["on_time"] = 0.0
	elif not running and bool(state["on"]):
		state["on"] = false
		state["off_time"] = 0.0
	if bool(state["on"]):
		state["on_time"] = float(state["on_time"]) + delta
	else:
		state["off_time"] = float(state["off_time"]) + delta
	var engine := entry_engine(entry)
	if not bool(state["on"]) and float(state["off_time"]) > ENGINE_OFF:
		if not engine.is_empty():
			_audio.stop_engine(engine)
		return
	if engine.is_empty():
		engine = _audio.start_engine(sound, entry["node"])
		entry["engine"] = engine
		if engine.is_empty():
			return
	var node: Node3D = entry["node"]
	var top := maxf(1.0, float(c.get("top_speed", 20.0)))
	var speed := (v["velocity"] as Vector3).dot(node.global_basis.z.normalized()) / top  # models face +z
	var change := clampf((speed - float(state["speed"])) / delta * 2.0, -1.0, 1.0)
	state["acceleration"] = lerpf(float(state["acceleration"]), change, minf(1.0, 4.0 * delta))
	state["speed"] = speed
	_audio.update_engine(engine, {
		"speed": speed,
		"acceleration": float(state["acceleration"]),
		"turn_on_time": float(state["on_time"]) if bool(state["on"]) else 99.0,
		"turn_off_time": 0.0 if bool(state["on"]) else float(state["off_time"]),
		"health": float(v["health"]) / maxf(1.0, float(v["max_health"])),
	})


func entry_engine(entry: Dictionary) -> Dictionary:
	return entry.get("engine", {})


# A walker's foot landing: its stomp (dust) and its step's sound, under the
# foot that came down (its feet in turn; under the walker if its bones
# aren't found).
func _footfall(entry: Dictionary) -> void:
	var c := class_of(int(entry["class"]))
	var node: Node3D = entry["node"]
	var walk: Dictionary = entry["walk"]
	var feet: Array = walk.get("feet", [])
	if not walk.has("feet"):
		var skeletons := node.find_children("*", "Skeleton3D", true, false)
		if not skeletons.is_empty():
			var skeleton: Skeleton3D = skeletons[0]
			for i in skeleton.get_bone_count():
				var bone := skeleton.get_bone_name(i).to_lower()
				if bone.ends_with("_foot") and (bone.contains("_l_") or bone.contains("_r_")):
					feet.append([skeleton, i])
		walk["feet"] = feet
	var at := node.global_position
	if not feet.is_empty():
		var k := int(walk.get("foot", 0)) % feet.size()
		walk["foot"] = int(walk.get("foot", 0)) + 1  # (its footfalls so far)
		var skeleton: Skeleton3D = feet[k][0]
		at = skeleton.global_transform * skeleton.get_bone_global_pose(int(feet[k][1])).origin
		at.y = node.global_position.y
	if _effects != null and str(c.get("stomp_effect", "")) != "":
		_effects.spawn_burst(str(c["stomp_effect"]), at)
	if _audio != null and str(c.get("step_sound", "")) != "":
		var camera := _root.get_viewport().get_camera_3d()
		_audio.play_3d(str(c["step_sound"]), at, camera.global_position if camera != null else at)


# An effect trailing from a point on the vehicle while `on`; off, it fades
# out where it was.
func _run(node: Node3D, running: Dictionary, key: String, effect: String, at: Vector3, on: bool) -> void:
	if on and not running.has(key):
		var holder := Node3D.new()
		holder.position = at
		node.add_child(holder)
		var trail: Node3D = _effects.attach_trail(holder, effect)
		running[key] = trail if trail != null else holder
	elif not on and running.has(key):
		var trail: Node3D = running[key]
		running.erase(key)
		if is_instance_valid(trail) and trail.get_parent() != null and trail.name == "Trail":
			var holder := trail.get_parent()
			_effects.detach_trail(trail)
			holder.queue_free()
		elif is_instance_valid(trail):
			trail.queue_free()


# Each seat's turret on a new vehicle's model: its nodes (a skeleton's bones
# or plain nodes) and where they rest, in the vehicle's own space.
func _find_turrets(node: Node3D, index: int) -> Array:
	var out := []
	var seats: Array = class_of(index).get("seats", [])
	var skeletons := node.find_children("*", "Skeleton3D", true, false)
	var skeleton: Skeleton3D = skeletons[0] if not skeletons.is_empty() else null
	for k in seats.size():
		var seat: Dictionary = seats[k]
		var parts := []
		for key in ["turret_node", "pitch_node"]:
			parts.append(_find_part(node, skeleton, str(seat.get(key, ""))))
		out.append({"seat": k, "turret": parts[0], "pitch": parts[1], "yaw": 0.0, "tilt": 0.0})
	return out


func _find_part(root: Node3D, skeleton: Skeleton3D, part_name: String) -> Dictionary:
	if part_name == "":
		return {}
	if skeleton != null:
		var bone := skeleton.find_bone(part_name)
		if bone >= 0:
			return {"bone": bone, "skeleton": skeleton, "rest": skeleton.get_bone_pose(bone),
					"parent_rest": skeleton.get_bone_global_pose(skeleton.get_bone_parent(bone))
							if skeleton.get_bone_parent(bone) >= 0 else Transform3D()}
	var found := root.find_child(part_name, true, false) as Node3D
	if found == null:
		return {}
	return {"node": found, "rest": found.transform}


# Turned by each gunner's aim: its yaw from the vehicle's heading about the
# vehicle's up, its pitch about its right (after the yaw). A seat no one sits
# in keeps where it was left.
func _aim_turrets(entry: Dictionary, v: Dictionary, shown: Dictionary) -> void:
	var gunners: Array = v.get("gunners", [])
	var node: Node3D = entry["node"]
	for turret: Dictionary in entry.get("turrets", []):
		var k := int(turret["seat"])
		if k < gunners.size() and int(gunners[k]) != 0 and shown.has(int(gunners[k])):
			var aim: Dictionary = shown[int(gunners[k])]
			turret["yaw"] = angle_difference(node.rotation.y - PI, float(aim.get("aim_yaw", 0.0)))
			turret["tilt"] = float(aim.get("pitch", 0.0))
		var turn := Basis(Vector3.UP, float(turret["yaw"]))
		_pose_part(node, turret["turret"], turn)
		_pose_part(node, turret["pitch"], turn * Basis(Vector3(-1, 0, 0), float(turret["tilt"])))


# A part turned by `turn` (in the vehicle's space) from where it rests.
func _pose_part(root: Node3D, part: Dictionary, turn: Basis) -> void:
	if part.is_empty():
		return
	if part.has("node"):
		var n: Node3D = part["node"]
		var parent := n.get_parent() as Node3D
		# The turn in its parent's space: turned the same about the same axis.
		var to_parent := (root.global_transform.affine_inverse() * parent.global_transform).basis.orthonormalized()
		var rest: Transform3D = part["rest"]
		n.transform = Transform3D(to_parent.inverse() * turn * to_parent * rest.basis, rest.origin)
		return
	var skeleton: Skeleton3D = part["skeleton"]
	var bone := int(part["bone"])
	var to_skeleton := (root.global_transform.affine_inverse() * skeleton.global_transform).basis.orthonormalized()
	var parent_rest: Transform3D = part["parent_rest"]
	var to_parent := to_skeleton * parent_rest.basis.orthonormalized()
	var rest: Transform3D = part["rest"]
	var local := to_parent.inverse() * turn * to_parent * rest.basis
	skeleton.set_bone_pose_rotation(bone, local.get_rotation_quaternion())


# Stage 12: a creature mount's gait (a Galaxies creature's idle, walk and
# run clips, as `swgtool export-people` exports them), by how fast it moves.
func _gait(entry: Dictionary, alive: bool, delta: float) -> void:
	var walk: Dictionary = entry["walk"]
	var player: AnimationPlayer = walk["player"]
	var node: Node3D = entry["node"]
	if player == null or delta <= 0:
		return
	var moved: Vector3 = node.position - (walk["last"] as Vector3)
	walk["last"] = node.position
	walk["speed"] = lerpf(float(walk["speed"]), Vector2(moved.x, moved.z).length() / delta, minf(1.0, 8.0 * delta))
	var speed := float(walk["speed"])
	var clip := "idle"
	var rate := 1.0
	if not alive and player.has_animation("die"):
		clip = "die"
	elif speed > 5.0 and player.has_animation("run"):
		clip = "run"
		rate = clampf(speed / 7.0, 0.7, 1.8)
	elif speed > 0.4:
		clip = "walk"
		rate = clampf(speed / 2.5, 0.6, 1.8)
	if not player.has_animation(clip):
		return
	if player.current_animation != clip:
		var anim := player.get_animation(clip)
		anim.loop_mode = Animation.LOOP_NONE if clip == "die" else Animation.LOOP_LINEAR
		player.play(clip, 0.2)
	player.speed_scale = rate


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
			_footfall(entry)
			_play(walk, "walk_" + str(walk["next"]), rate, backward)
			walk["next"] = "b" if walk["next"] == "a" else "a"
			walk["state"] = "walk"
		elif state in ["start", "walk"]:
			player.speed_scale = rate
	elif state in ["start", "walk"]:
		if done:
			# Each stride ends where the other starts.
			_footfall(entry)
			_play(walk, "walkstop_" + str(walk["next"]), 1.0)
			walk["state"] = "stop"
			walk["stopped"] = false
	elif state == "stop" and done and not bool(walk.get("stopped", true)):
		_footfall(entry)
		walk["stopped"] = true
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


# World position for a turret operator so their model stays visible at the gun.
func turret_gunner_pos(entity: int, fallback_pos: Vector3, fallback_yaw: float) -> Vector3:
	for entry: Dictionary in _nodes.values():
		var v: Dictionary = entry.get("state", {})
		if int(v.get("driver", 0)) == entity:
			var node: Node3D = entry["node"]
			var main_gunner := node.find_child("hp_main_gunner", true, false) as Node3D
			if main_gunner != null:
				return main_gunner.global_position
			for hp_name: String in ["hp_active", "hp_active_01"]:
				var hp := node.find_child(hp_name, true, false) as Node3D
				if hp != null:
					var p := hp.global_position
					p.y = maxf(node.global_position.y, p.y)
					return p
			return to_world(node.position, node.rotation.y - PI, Vector3(0.0, 0.0, -1.2))
	return to_world(fallback_pos, fallback_yaw, Vector3(0.0, 0.0, -1.2))


# The seat a soldier of `team` would get in the nearest vehicle within reach
# (as the server reckons it: 2.5 m from its body, ASSUMPTION a little less
# to be sure): {vehicle: its state, seat: 0 the driver's, 1.. a gunner's},
# or {}. A gunner's seat only beside one's own side; in the world, only
# one's own (`me`).
func enterable(vehicles: Array, position: Vector3, team := -1, me := 0, is_space := false) -> Dictionary:
	var best := {}
	var best_d := 2.2
	var chest := position + Vector3.UP
	for v: Dictionary in vehicles:
		if not bool(v["alive"]):
			continue
		if is_space and str(class_of(int(v.get("vehicle", -1))).get("kind", "")) == "turret":
			continue
		if int(v.get("owner", 0)) != 0 and int(v["owner"]) != me:
			continue  # in the world, someone else's
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
