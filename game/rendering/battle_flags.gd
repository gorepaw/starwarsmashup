# The flags of Battlefront II's flag modes (Capture the Flag, 1-Flag CTF):
# each flag a pole and a cloth in its side's color (yours blue, theirs red,
# the 1-flag neutral gold) with a light beam over it so it can be found from
# afar, carried along over its carrier's head or lying where they fell; and
# each capture region a ring on the ground in the color of the side that
# scores there. The server says where each flag is (get_match()["flags"]).
extends RefCounted

const FRIEND := Color(0.35, 0.6, 1.0)
const ENEMY := Color(1.0, 0.3, 0.25)
const NEUTRAL := Color(1.0, 0.8, 0.3)
const BEAM_HEIGHT := 40.0

var _flags: Array = []   # [{root, cloth, beam, owner}]
var _rings: Array = []   # [{node, material, team}]


func build(root: Node3D, battlefield: Dictionary) -> void:
	clear()
	if root == null:
		return
	for flag: Dictionary in battlefield.get("flags", []):
		var holder := Node3D.new()
		holder.position = flag["home"]
		root.add_child(holder)
		var pole := MeshInstance3D.new()
		var pole_mesh := CylinderMesh.new()
		pole_mesh.top_radius = 0.03
		pole_mesh.bottom_radius = 0.03
		pole_mesh.height = 2.4
		pole.mesh = pole_mesh
		pole.position = Vector3(0, 1.2, 0)
		var metal := StandardMaterial3D.new()
		metal.albedo_color = Color(0.7, 0.7, 0.72)
		metal.metallic = 0.8
		pole.material_override = metal
		holder.add_child(pole)
		var cloth := MeshInstance3D.new()
		var cloth_mesh := BoxMesh.new()
		cloth_mesh.size = Vector3(0.9, 0.55, 0.02)
		cloth.mesh = cloth_mesh
		cloth.position = Vector3(0.47, 2.1, 0)
		var cloth_material := StandardMaterial3D.new()
		cloth_material.emission_enabled = true
		cloth_material.emission_energy_multiplier = 0.6
		cloth.material_override = cloth_material
		holder.add_child(cloth)
		var beam := MeshInstance3D.new()
		var beam_mesh := CylinderMesh.new()
		beam_mesh.top_radius = 0.12
		beam_mesh.bottom_radius = 0.12
		beam_mesh.height = BEAM_HEIGHT
		beam.mesh = beam_mesh
		beam.position = Vector3(0, BEAM_HEIGHT * 0.5 + 2.4, 0)
		beam.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		var beam_material := StandardMaterial3D.new()
		beam_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		beam_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		beam.material_override = beam_material
		holder.add_child(beam)
		_flags.append({"root": holder, "cloth": cloth_material, "beam": beam_material, "owner": int(flag["owner"])})
	for capture: Dictionary in battlefield.get("flag_captures", []):
		var ring := MeshInstance3D.new()
		var torus := TorusMesh.new()
		var radius := maxf(1.0, float(capture["radius"]))
		torus.outer_radius = radius
		torus.inner_radius = radius - 0.2
		ring.mesh = torus
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		ring.material_override = material
		ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		var at: Vector3 = capture["position"]
		ring.position = at + Vector3.UP * 0.1
		root.add_child(ring)
		_rings.append({"node": ring, "material": material, "team": int(capture["team"])})


func clear() -> void:
	for flag: Dictionary in _flags:
		if is_instance_valid(flag["root"]):
			(flag["root"] as Node).queue_free()
	for ring: Dictionary in _rings:
		if is_instance_valid(ring["node"]):
			(ring["node"] as Node).queue_free()
	_flags.clear()
	_rings.clear()


func update(game: Dictionary, own_team: int, elapsed: float) -> void:
	var states: Array = game.get("flags", [])
	for i in mini(_flags.size(), states.size()):
		var flag: Dictionary = _flags[i]
		var state: Dictionary = states[i]
		var holder: Node3D = flag["root"]
		var at: Vector3 = state["position"]
		var carried := int(state.get("carrier", 0)) != 0
		# Carried: over the carrier's head; dropped or home: on the ground.
		holder.position = at + (Vector3.UP * 1.2 if carried else Vector3.ZERO)
		holder.rotation.y = elapsed * (0.0 if carried else 0.6)
		var owner := int(flag["owner"])
		var color := NEUTRAL if owner < 0 else (FRIEND if owner == own_team else ENEMY)
		(flag["cloth"] as StandardMaterial3D).albedo_color = color
		(flag["cloth"] as StandardMaterial3D).emission = color
		# The beam pulses while it lies dropped, waiting to go home.
		var alpha := 0.35
		if not carried and not bool(state.get("home", true)):
			alpha = 0.25 + 0.25 * (0.5 + 0.5 * sin(elapsed * 6.0))
		(flag["beam"] as StandardMaterial3D).albedo_color = Color(color, alpha)
	for ring: Dictionary in _rings:
		var color: Color = FRIEND if int(ring["team"]) == own_team else ENEMY
		(ring["material"] as StandardMaterial3D).albedo_color = Color(color, 0.6)


# Where each flag is, for the minimap: [{position, color}].
func map_markers(game: Dictionary, own_team: int) -> Array:
	var out: Array = []
	var states: Array = game.get("flags", [])
	for i in mini(_flags.size(), states.size()):
		var owner := int(_flags[i]["owner"])
		out.append({"position": states[i]["position"],
				"color": NEUTRAL if owner < 0 else (FRIEND if owner == own_team else ENEMY)})
	return out
