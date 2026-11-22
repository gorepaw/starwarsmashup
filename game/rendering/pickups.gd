# Draws dropped health, ammo, dual and energy canisters left by dead soldiers
# in a match, using Battlefront II's own powerup models (bf2tool export-hud).
# Each canister hovers just above the ground, turns slowly, and blinks during
# the last few seconds before disappearing.
#
#   var pickups := Pickups.new(root, HUD_DIR)
#   pickups.update(client.get_pickups(), delta)
#   pickups.clear()     # on server transfer
#   pickups.dispose()   # on exit
extends RefCounted

const HOVER_HEIGHT := 0.14  # m over the ground
const BOB_AMOUNT := 0.05    # m up and down
const BOB_RATE := 2.4       # radians a second
const SPIN_RATE := 1.6      # radians a second
const BLINK_TIME := 5.0     # seconds left when it starts blinking
const EMISSION := 0.45      # subtle glow so canisters read in dark rooms

const FALLBACK_COLORS := {
	"health_small": Color(0.25, 0.9, 0.35),
	"health_large": Color(0.95, 0.25, 0.25),
	"ammo": Color(0.3, 0.6, 1.0),
	"dual": Color(0.25, 0.9, 0.85),
	"energy": Color(0.95, 0.85, 0.25),
}

var _parent: Node
var _models_dir := ""
var _templates := {}  # model name -> Node3D (or null if missing)
var _nodes := {}      # pickup id -> {node: Node3D, phase: float}
var _time := 0.0


func _init(parent: Node, hud_dir: String) -> void:
	_parent = parent
	_models_dir = hud_dir.path_join("models")


func update(pickups: Array, delta: float) -> void:
	_time += delta
	var seen := {}
	for p: Dictionary in pickups:
		var id := int(p["id"])
		seen[id] = true
		if not _nodes.has(id):
			var node := _make(str(p.get("model", "")), str(p.get("kind", "")))
			_parent.add_child(node)
			_nodes[id] = {"node": node, "phase": float(id % 97) * 0.73}
		var entry: Dictionary = _nodes[id]
		var node: Node3D = entry["node"]
		var phase: float = entry["phase"]
		var base: Vector3 = p["position"]
		node.position = base + Vector3.UP * (HOVER_HEIGHT + BOB_AMOUNT * sin(_time * BOB_RATE + phase))
		node.rotation.y = _time * SPIN_RATE + phase
		var left := float(p.get("time_left", 30.0))
		if left < BLINK_TIME:
			# Blink faster as the canister is about to vanish.
			var rate := lerpf(16.0, 4.0, clampf(left / BLINK_TIME, 0.0, 1.0))
			node.visible = sin(_time * rate * PI) >= 0.0
		else:
			node.visible = true
	for id: int in _nodes.keys():
		if not seen.has(id):
			var node: Node3D = _nodes[id]["node"]
			if is_instance_valid(node):
				node.queue_free()
			_nodes.erase(id)


func clear() -> void:
	for entry: Dictionary in _nodes.values():
		var node: Node3D = entry["node"]
		if is_instance_valid(node):
			node.queue_free()
	_nodes.clear()


func dispose() -> void:
	clear()
	for template: Variant in _templates.values():
		if template != null and is_instance_valid(template):
			(template as Node).free()
	_templates.clear()


func _make(model_name: String, kind: String) -> Node3D:
	var template := _template(model_name)
	if template != null:
		return template.duplicate() as Node3D
	# Fallback when the HUD/pickup models haven't been exported yet.
	var mesh := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(0.28, 0.42, 0.28)
	mesh.mesh = box
	var color: Color = FALLBACK_COLORS.get(kind, Color.WHITE)
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.emission_enabled = true
	material.emission = color
	material.emission_energy_multiplier = 0.8
	mesh.material_override = material
	mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var root := Node3D.new()
	mesh.position = Vector3.UP * 0.21
	root.add_child(mesh)
	return root


func _template(model_name: String) -> Node3D:
	if model_name == "":
		return null
	if _templates.has(model_name):
		return _templates[model_name]
	_templates[model_name] = null
	var path := ProjectSettings.globalize_path(_models_dir.path_join(model_name + ".gltf"))
	if not FileAccess.file_exists(path):
		return null
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		return null
	var scene := document.generate_scene(state) as Node3D
	if scene == null:
		return null
	for mesh: MeshInstance3D in scene.find_children("*", "MeshInstance3D", true, false):
		mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		for surface in mesh.mesh.get_surface_count():
			var source := mesh.mesh.surface_get_material(surface) as StandardMaterial3D
			if source == null:
				continue
			var mat := source.duplicate() as StandardMaterial3D
			mat.emission_enabled = true
			if mat.albedo_texture != null:
				mat.emission_texture = mat.albedo_texture
				mat.emission = Color.WHITE
			else:
				mat.emission = mat.albedo_color
			mat.emission_energy_multiplier = EMISSION
			mesh.set_surface_override_material(surface, mat)
	_templates[model_name] = scene
	return scene
