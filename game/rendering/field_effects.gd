# Stage 12: what specialists put in the field, drawn from each snapshot's
# field state (SwbfoClient.get_field()): marks over spotted enemies (red),
# tracked creatures (amber) and bounty marks (orange); deployables (sensors,
# traps, bacta dispensers, holo-emitters, spawn beacons, camps) with their
# reach; bio clouds, smoke and incoming strikes; performers and holo flair;
# your side's cloaked soldiers, faint. Placeholder art (ASSUMPTION: Battlefront
# has no such props; these stand in until Galaxies' are exported).
class_name FieldEffects
extends RefCounted

const MARK_COLORS := [Color(1.0, 0.25, 0.2), Color(1.0, 0.75, 0.2), Color(1.0, 0.5, 0.1)]
const DEPLOY_COLORS := {
	"heal_aura": Color(0.3, 1.0, 0.5),
	"buff_aura": Color(0.9, 0.6, 1.0),
	"sensor": Color(0.4, 0.8, 1.0),
	"trap": Color(1.0, 0.45, 0.2),
	"spawn_beacon": Color(0.3, 0.6, 1.0),
	"camp": Color(1.0, 0.85, 0.4),
}
const DEPLOY_NAMES := {
	"heal_aura": "Bacta Dispenser", "buff_aura": "Holo-Emitter", "sensor": "Sensor",
	"trap": "Trap", "spawn_beacon": "Squad Beacon", "camp": "Forward Camp",
}
const HAZARD_COLORS := {
	"bio": Color(0.35, 0.9, 0.2, 0.28),
	"smoke": Color(0.6, 0.62, 0.65, 0.75),
	"strike": Color(1.0, 0.2, 0.1, 0.25),
	"electro": Color(0.4, 0.6, 1.0, 0.3),
}

var tag_heights := {}   # entity -> how tall it stands (m), for tags over companions
var _root: Node3D
var _marks := {}        # entity -> Label3D
var _deployables := {}  # id -> Node3D
var _hazards := {}      # key -> MeshInstance3D
var _tags := {}         # "perf:<entity>" / "flair:<entity>" / "cloak:<entity>" / "pet:<entity>" -> Label3D
var _time := 0.0


func _init(root: Node3D) -> void:
	_root = root


func _label(text_value: String, color: Color, size := 48) -> Label3D:
	var label := Label3D.new()
	label.text = text_value
	label.modulate = color
	label.font_size = size
	label.outline_size = 8
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.no_depth_test = true
	label.fixed_size = true
	label.pixel_size = 0.0009
	_root.add_child(label)
	return label


func _material(color: Color, unshaded := true) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = color
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA if color.a < 1.0 else BaseMaterial3D.TRANSPARENCY_DISABLED
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED if unshaded else BaseMaterial3D.SHADING_MODE_PER_PIXEL
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	return m


func _deployable_node(kind: String, radius: float, friendly: bool) -> Node3D:
	var root := Node3D.new()
	var color: Color = DEPLOY_COLORS.get(kind, Color.WHITE)
	var post := MeshInstance3D.new()
	var cylinder := CylinderMesh.new()
	cylinder.top_radius = 0.12
	cylinder.bottom_radius = 0.22
	cylinder.height = 0.9 if kind != "spawn_beacon" else 1.6
	post.mesh = cylinder
	post.position.y = cylinder.height * 0.5
	post.material_override = _material(Color(color.r * 0.5, color.g * 0.5, color.b * 0.5), false)
	root.add_child(post)
	var light := MeshInstance3D.new()
	var bulb := SphereMesh.new()
	bulb.radius = 0.14
	bulb.height = 0.28
	light.mesh = bulb
	light.position.y = cylinder.height + 0.1
	light.material_override = _material(color)
	root.add_child(light)
	if kind != "trap" or friendly:
		var ring := MeshInstance3D.new()
		var disc := CylinderMesh.new()
		disc.top_radius = radius
		disc.bottom_radius = radius
		disc.height = 0.04
		ring.mesh = disc
		ring.position.y = 0.05
		ring.material_override = _material(Color(color.r, color.g, color.b, 0.12 if friendly else 0.08))
		root.add_child(ring)
	var label := Label3D.new()
	label.text = str(DEPLOY_NAMES.get(kind, kind)) + ("" if friendly else " (enemy)")
	label.modulate = color
	label.font_size = 32
	label.outline_size = 6
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.fixed_size = true
	label.pixel_size = 0.0008
	label.position.y = cylinder.height + 0.6
	root.add_child(label)
	_root.add_child(root)
	return root


# `positions`: entity -> Vector3 (feet), everyone drawn this frame; `own`: our entity.
func update(field: Dictionary, positions: Dictionary, own: int, delta: float) -> void:
	_time += delta
	# Marks.
	var seen := {}
	for m: Dictionary in field.get("marks", []):
		var e := int(m["entity"])
		if not positions.has(e):
			continue
		seen[e] = true
		var label: Label3D = _marks.get(e)
		if label == null:
			label = _label("v", MARK_COLORS[clampi(int(m.get("kind", 0)), 0, 2)], 64)
			_marks[e] = label
		label.modulate = MARK_COLORS[clampi(int(m.get("kind", 0)), 0, 2)]
		label.global_position = (positions[e] as Vector3) + Vector3(0, 2.6 + 0.12 * sin(_time * 5.0), 0)
	for e: int in _marks.keys():
		if not seen.has(e):
			(_marks[e] as Node).queue_free()
			_marks.erase(e)
	# Deployables.
	seen.clear()
	for d: Dictionary in field.get("deployables", []):
		var id := int(d["id"])
		seen[id] = true
		var node: Node3D = _deployables.get(id)
		if node == null:
			node = _deployable_node(str(d["kind"]), float(d.get("radius", 1.0)), bool(d.get("friendly", true)))
			_deployables[id] = node
		node.global_position = d["position"]
	for id: int in _deployables.keys():
		if not seen.has(id):
			(_deployables[id] as Node).queue_free()
			_deployables.erase(id)
	# Clouds and strikes.
	seen.clear()
	for h: Dictionary in field.get("hazards", []):
		var key := "%s:%s:%s" % [str(h["kind"]), str(h["id"]), str(h["position"])]
		seen[key] = true
		var mesh: MeshInstance3D = _hazards.get(key)
		if mesh == null:
			mesh = MeshInstance3D.new()
			var r := float(h.get("radius", 3.0))
			if str(h["kind"]) == "strike":
				var disc := CylinderMesh.new()
				disc.top_radius = r
				disc.bottom_radius = r
				disc.height = 0.08
				mesh.mesh = disc
			else:
				var ball := SphereMesh.new()
				ball.radius = r
				ball.height = r * 1.2
				mesh.mesh = ball
			mesh.material_override = _material(HAZARD_COLORS.get(str(h["kind"]), Color(0.8, 0.8, 0.8, 0.3)))
			_root.add_child(mesh)
			_hazards[key] = mesh
		mesh.global_position = (h["position"] as Vector3) + Vector3(0, 0.1 if str(h["kind"]) == "strike" else 0.6, 0)
		if str(h["kind"]) == "strike":
			mesh.visible = fmod(_time, 0.4) < 0.25
	for key: String in _hazards.keys():
		if not seen.has(key):
			(_hazards[key] as Node).queue_free()
			_hazards.erase(key)
	# Tags: performers, flair, cloaked allies, companions.
	seen.clear()
	var want := {}
	for e: int in field.get("performers", PackedInt64Array()):
		want["perf:%d" % e] = ["~ performing ~", Color(1.0, 0.85, 0.4), e, 2.3]
	var flair: Dictionary = field.get("flair", {})
	for e: int in flair:
		if int(flair[e]) & 1:
			want["flair:%d" % e] = ["*", Color(0.5, 0.95, 1.0), e, 2.05 + 0.15 * sin(_time * 3.0 + e)]
	for e: int in field.get("cloaked", PackedInt64Array()):
		if e != own:
			want["cloak:%d" % e] = ["(cloaked)", Color(0.6, 0.8, 1.0, 0.6), e, 2.0]
	var pets: Dictionary = field.get("companions", {})
	for e: int in pets:
		want["pet:%d" % e] = ["yours" if int(pets[e]) == own else "companion", Color(0.45, 1.0, 0.55), e,
				float(tag_heights.get(e, 1.5)) + 0.4]
	for key: String in want:
		var w: Array = want[key]
		var e := int(w[2])
		if not positions.has(e):
			continue
		seen[key] = true
		var label: Label3D = _tags.get(key)
		if label == null:
			label = _label(str(w[0]), w[1], 30)
			_tags[key] = label
		label.global_position = (positions[e] as Vector3) + Vector3(0, float(w[3]), 0)
	for key: String in _tags.keys():
		if not seen.has(key):
			(_tags[key] as Node).queue_free()
			_tags.erase(key)


func clear() -> void:
	for group: Dictionary in [_marks, _deployables, _hazards, _tags]:
		for node: Variant in group.values():
			if node != null and is_instance_valid(node):
				(node as Node).queue_free()
		group.clear()
