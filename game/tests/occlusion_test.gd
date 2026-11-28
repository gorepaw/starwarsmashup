# Worn things hide what they cover (SwgOcclusion, Galaxies' occlusion zones):
# a player in full stormtrooper armor shows none of its body, taking it off
# shows it all again, and a hair style covers a Twi'lek's own lekku. With
# SWBFO_SHOTS set (a path prefix), renders them.
#   godot --path game --script res://tests/occlusion_test.gd
extends SceneTree

const PlayerAppearance = preload("res://rendering/player_appearance.gd")
const SwgOcclusion = preload("res://rendering/swg_occlusion.gd")
const TROOPER := ["helmet", "chest", "bicep_l", "bicep_r", "bracer_l", "bracer_r", "gloves", "belt", "leggings",
		"boots"]

var _failed := false
var _shots := []  # [character, name]
var _frames := 0


func _initialize() -> void:
	var human := PlayerAppearance.load_character("human", "male", '{"height":0.5}')
	_check(not human.is_empty(), "human loads")
	var model: Node3D = human["model"]
	var body_before := _triangles(model, "hum_m_")
	_check(body_before > 1000, "human body has triangles (%d)" % body_before)
	_check(_matches(model), "human body's occlusion matches its surfaces")

	var worn := ",".join(TROOPER.map(func(p: String) -> String: return "armor/stormtrooper/%s_m.gltf" % p))
	PlayerAppearance.set_worn_models(human, worn)
	var body_armored := _triangles(model, "hum_m_")
	var armor := _triangles(human["worn_holder"], "armor_")
	print("human body: %d triangles, %d in armor; armor: %d" % [body_before, body_armored, armor])
	_check(body_armored < body_before / 10, "armor hides the body under it")
	_check(armor > 1000, "armor drawn (%d)" % armor)
	_check(_matches(human["worn_holder"]), "armor's occlusion matches its surfaces")

	var partial := PlayerAppearance.load_character("human", "female", '{"height":0.5}')
	var partial_before := _triangles(partial["model"], "hum_f_")
	PlayerAppearance.set_worn_models(partial, "clothing/jacket_rebel_f.gltf,clothing/pants_cargo_f.gltf")
	var partial_dressed := _triangles(partial["model"], "hum_f_")
	print("human female body: %d triangles, %d dressed" % [partial_before, partial_dressed])
	_check(partial_dressed < partial_before and partial_dressed > partial_before / 5,
			"clothes hide part of the body (head and hands stay)")

	PlayerAppearance.set_worn_models(human, "")
	await process_frame  # (the old worn holder is freed)
	_check(_triangles(model, "hum_m_") == body_before, "taking it off shows the body again")
	PlayerAppearance.set_worn_models(human, worn)

	var twilek := PlayerAppearance.load_character("twilek", "female",
			'{"height":0.5,"hair":"twk_f_hair_s01"}')
	var head_bare := PlayerAppearance.load_character("twilek", "female", '{"height":0.5}')
	var head_with := _triangles(twilek["model"], "twk_f_head")
	var head_without := _triangles(head_bare["model"], "twk_f_head")
	print("twi'lek head: %d triangles bare, %d with hair" % [head_without, head_with])
	_check(head_with < head_without, "a Twi'lek's hair covers her own lekku")

	if OS.get_environment("SWBFO_SHOTS") == "":
		_finish()
		return
	_stage([human, partial, twilek, head_bare])


func _process(_delta: float) -> bool:
	if _shots.is_empty():
		return false
	_frames += 1
	if _frames == 30:
		var image := root.get_texture().get_image()
		var path := OS.get_environment("SWBFO_SHOTS") + "occlusion.png"
		image.save_png(path)
		print("wrote " + path)
		_finish()
	return false


func _stage(characters: Array) -> void:
	var camera := Camera3D.new()
	camera.position = Vector3(0, 1.1, 3.6)
	camera.fov = 45
	root.add_child(camera)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-35, 30, 0)
	root.add_child(light)
	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.background_mode = Environment.BG_COLOR
	environment.environment.background_color = Color(0.25, 0.27, 0.3)
	environment.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.environment.ambient_light_color = Color(0.6, 0.6, 0.6)
	root.add_child(environment)
	for i in characters.size():
		var model: Node3D = characters[i]["model"]
		model.position = Vector3(-1.5 + i, 0, 0)
		model.rotation.y = 0.4 if i % 2 == 0 else -0.4
		root.add_child(model)
	_shots = characters


func _finish() -> void:
	print("occlusion: OK" if not _failed else "occlusion: FAIL")
	quit(1 if _failed else 0)


func _check(condition: bool, what: String) -> void:
	if not condition:
		printerr("FAIL: " + what)
		_failed = true


# The triangles drawn by the meshes under `root` whose names begin `prefix`.
func _triangles(root: Node, prefix: String) -> int:
	var count := 0
	for node in root.find_children("*", "MeshInstance3D", true, false):
		var mi := node as MeshInstance3D
		if mi.mesh == null or not String(mi.name).begins_with(prefix) or mi.is_queued_for_deletion():
			continue
		var mesh := mi.mesh as ArrayMesh
		for s in mesh.get_surface_count():
			var triangles: int = mesh.surface_get_array_index_len(s) / 3
			if triangles > 1:  # (an emptied surface keeps one degenerate triangle)
				count += triangles
	return count


# Every tagged mesh's exported triangle lists match its surfaces (Godot's
# import keeps the triangles' order).
func _matches(root: Node) -> bool:
	var ok := true
	for node in root.find_children("*", "MeshInstance3D", true, false):
		var mi := node as MeshInstance3D
		if not mi.has_meta(SwgOcclusion.META):
			continue
		var mesh: ArrayMesh = mi.get_meta(SwgOcclusion.ORIGINAL, mi.mesh)
		var surfaces: Array = (mi.get_meta(SwgOcclusion.META) as Dictionary).get("surfaces", [])
		if surfaces.size() != mesh.get_surface_count():
			printerr("%s: %d surfaces, %d listed" % [mi.name, mesh.get_surface_count(), surfaces.size()])
			ok = false
			continue
		for s in surfaces.size():
			if (surfaces[s] as Array).size() != mesh.surface_get_array_index_len(s) / 3:
				printerr("%s surface %d: %d triangles, %d listed" % [mi.name, s,
						mesh.surface_get_array_index_len(s) / 3, (surfaces[s] as Array).size()])
				ok = false
	return ok
