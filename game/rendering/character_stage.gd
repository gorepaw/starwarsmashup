# 3D turntable stage for the Character Creator: loads any exported species
# glTF, attaches hair glTFs to its Skeleton3D, drives blend shapes, height
# scaling, and SwgColors.apply_custom(), and plays the idle animation.
class_name CharacterStage
extends Node3D

const SPECIES_DIR := "res://imported_assets/species"

var model: Node3D
var hair_node: Node3D
var colors_data: Dictionary = {}
var hair_colors_data: Dictionary = {}
var species_meta: Dictionary = {}
var yaw := 0.0
var _skeleton: Skeleton3D
var _meshes: Array[MeshInstance3D] = []
var _blend_indices: Dictionary = {}  # blend_name -> Array[{mesh: MeshInstance3D, idx: int}]


func load_species(species_id: String, gender: String) -> Dictionary:
	clear()
	var sg := "%s_%s" % [species_id, gender]
	var dir := SPECIES_DIR.path_join(sg)
	var meta_path := dir.path_join(sg + ".species.json")
	if FileAccess.file_exists(meta_path):
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(meta_path))
		if parsed is Dictionary:
			species_meta = parsed
	var gltf_path := dir.path_join(sg + ".gltf")
	if not FileAccess.file_exists(gltf_path):
		return species_meta
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	if doc.append_from_file(ProjectSettings.globalize_path(gltf_path), state) != OK:
		return species_meta
	model = doc.generate_scene(state) as Node3D
	if model == null:
		return species_meta
	model.rotation.y = yaw
	add_child(model)
	colors_data = SwgColors.load_colors(ProjectSettings.globalize_path(gltf_path))
	_skeleton = model.find_child("Skeleton3D", true, false) as Skeleton3D
	_index_meshes()
	_play_idle()
	return species_meta


func set_hair(hair_rel_gltf: String, color_overrides: Dictionary) -> void:
	if hair_node != null and is_instance_valid(hair_node):
		hair_node.queue_free()
		hair_node = null
	hair_colors_data.clear()
	if hair_rel_gltf == "" or model == null:
		return
	var sg := "%s_%s" % [str(species_meta.get("species", "human")), str(species_meta.get("gender", "male"))]
	var gltf_path := SPECIES_DIR.path_join(sg).path_join(hair_rel_gltf)
	if not FileAccess.file_exists(gltf_path):
		return
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	if doc.append_from_file(ProjectSettings.globalize_path(gltf_path), state) != OK:
		return
	var scene := doc.generate_scene(state) as Node3D
	if scene == null:
		return
	# Attach hair MeshInstance3D nodes onto the main character's Skeleton3D so
	# they animate and scale with the head.
	var hair_meshes := scene.find_children("*", "MeshInstance3D", true, false)
	if _skeleton != null and not hair_meshes.is_empty():
		var holder := Node3D.new()
		holder.name = "HairAttachment"
		for m in hair_meshes:
			var mi := m as MeshInstance3D
			mi.get_parent().remove_child(mi)
			holder.add_child(mi)
		_skeleton.add_child(holder)
		for child in holder.get_children():
			if child is MeshInstance3D:
				(child as MeshInstance3D).skeleton = NodePath("../..")
		hair_node = holder
		scene.queue_free()
	else:
		model.add_child(scene)
		hair_node = scene
	hair_colors_data = SwgColors.load_colors(ProjectSettings.globalize_path(gltf_path))
	if hair_node != null and not hair_colors_data.is_empty():
		SwgColors.apply_custom(hair_node, hair_colors_data, color_overrides)


func apply_appearance(appearance: Dictionary, scale_min: float, scale_max: float) -> void:
	if model == null:
		return
	var height_t := clampf(float(appearance.get("height", 0.5)), 0.0, 1.0)
	var s := lerpf(scale_min, scale_max, height_t)
	model.scale = Vector3.ONE * s

	var blends: Dictionary = appearance.get("blends", {})
	for blend_name: String in _blend_indices:
		var val := clampf(float(blends.get(blend_name, 0.0)), 0.0, 1.0)
		for entry: Dictionary in _blend_indices[blend_name]:
			(entry["mesh"] as MeshInstance3D).set_blend_shape_value(int(entry["idx"]), val)

	var color_overrides: Dictionary = appearance.get("colors", {})
	if not colors_data.is_empty():
		SwgColors.apply_custom(model, colors_data, color_overrides)
	if hair_node != null and is_instance_valid(hair_node) and not hair_colors_data.is_empty():
		SwgColors.apply_custom(hair_node, hair_colors_data, color_overrides)


func rotate_by(delta_yaw: float) -> void:
	yaw += delta_yaw
	if model != null:
		model.rotation.y = yaw


func eye_y() -> float:
	var base_eye := float(species_meta.get("eye_height", 1.55))
	return base_eye * (model.scale.y if model != null else 1.0)


func clear() -> void:
	_meshes.clear()
	_blend_indices.clear()
	_skeleton = null
	colors_data.clear()
	hair_colors_data.clear()
	species_meta.clear()
	if hair_node != null and is_instance_valid(hair_node):
		hair_node.queue_free()
	hair_node = null
	if model != null and is_instance_valid(model):
		model.queue_free()
	model = null


func _index_meshes() -> void:
	for node in model.find_children("*", "MeshInstance3D", true, false):
		var mi := node as MeshInstance3D
		if mi.mesh == null:
			continue
		_meshes.append(mi)
		for i in mi.mesh.get_blend_shape_count():
			var bname := String(mi.mesh.get_blend_shape_name(i))
			if not _blend_indices.has(bname):
				_blend_indices[bname] = []
			(_blend_indices[bname] as Array).append({"mesh": mi, "idx": i})


func _play_idle() -> void:
	var player := model.find_child("AnimationPlayer", true, false) as AnimationPlayer
	if player == null:
		return
	var clip := "human_rifle_stand_idle_emote_full"
	if player.has_animation(clip):
		player.get_animation(clip).loop_mode = Animation.LOOP_LINEAR
		player.play(clip)
