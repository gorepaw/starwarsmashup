extends SceneTree

# A blaster mark on a sliding door goes with the door when it opens, instead
# of hanging in the doorway. Run:
#   godot --headless --path game --script res://tests/door_scorch_test.gd

const Effects = preload("res://rendering/bf2_effects.gd")
const SwgCity = preload("res://rendering/swg_city.gd")

var _failures := 0


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failures += 1
		printerr("FAIL: ", what)


func _process(_delta: float) -> bool:
	_run()
	return true


func _run() -> void:
	var world := Node3D.new()
	root.add_child(world)
	# A door leaf, 2 m wide and 3 high, in a doorway at the origin, that slides 2 m along +x.
	var door := Node3D.new()
	var leaf := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(2.0, 3.0, 0.1)
	leaf.mesh = box
	leaf.position = Vector3(0.0, 1.5, 0.0)
	door.add_child(leaf)
	door.set_meta("door", {"closed": Vector3.ZERO, "slide": Vector3(2.0, 0.0, 0.0), "open_time": 1.0,
			"close_time": 1.0, "radius": 4.0, "open": 0.0})
	door.add_to_group(SwgCity.DOORS_GROUP)
	world.add_child(door)

	var effects := Effects.new(world, "res://imported_assets/effects")
	effects.spawn_scorch(Vector3(0.3, 1.4, 0.06), Vector3(0, 0, 1), 0.28)   # on the door
	effects.spawn_scorch(Vector3(5.0, 1.4, 0.06), Vector3(0, 0, 1), 0.28)   # on the wall beside it
	var marks: Array = []
	for node: Node in root.find_children("*", "MeshInstance3D", true, false):
		if node.has_meta("scorch"):
			marks.append(node)
	_check(marks.size() == 2, "two marks were made")
	var on_door: Node3D = null
	var on_wall: Node3D = null
	for mark: Node3D in marks:
		if mark.get_parent() == door:
			on_door = mark
		else:
			on_wall = mark
	_check(on_door != null, "the mark on the door is the door's")
	_check(on_wall != null and on_wall.get_parent() == world, "the mark on the wall is the world's")
	if on_door == null:
		quit(1)
		return
	var before := on_door.global_position
	# Someone walks up: it opens.
	var people: Array[Vector3] = [Vector3(0, 0, 2)]
	for i in 60:
		SwgCity.update_doors([door], people, 0.05)
	_check(door.position.is_equal_approx(Vector3(2.0, 0.0, 0.0)), "the door is open")
	_check(on_door.global_position.is_equal_approx(before + Vector3(2.0, 0.0, 0.0)), "the mark went with it")
	_check(on_wall.global_position.is_equal_approx(Vector3(5.0, 1.4, 0.06) + Vector3(0, 0, 0.018)), "the wall's mark stayed")
	print("door_scorch_test: ", "ok" if _failures == 0 else "%d failed" % _failures)
	quit(0 if _failures == 0 else 1)
