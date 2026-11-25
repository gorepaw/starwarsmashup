# 3D turntable stage for the Character Creator: loads any exported species
# glTF, attaches hair glTFs to its Skeleton3D, drives blend shapes, height
# scaling, and SwgColors.apply_custom(), and plays the idle animation.
class_name CharacterStage
extends Node3D

const SPECIES_DIR := "res://imported_assets/species"
const CLOTHING_GROW := 0.015
const ARMOR_GROW := 0.018
const WOOKIEE_CLOTHING_GROW := 0.025

var model: Node3D
var hair_node: Node3D
var worn_node: Node3D
var weapon_node: Node3D
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


func load_override(rel_gltf: String) -> void:
	clear()
	var gltf_path := "res://imported_assets/" + rel_gltf
	if not FileAccess.file_exists(gltf_path):
		return
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	if doc.append_from_file(ProjectSettings.globalize_path(gltf_path), state) != OK:
		return
	model = doc.generate_scene(state) as Node3D
	if model == null:
		return
	model.rotation.y = yaw
	add_child(model)
	colors_data = SwgColors.load_colors(ProjectSettings.globalize_path(gltf_path))
	_skeleton = model.find_child("Skeleton3D", true, false) as Skeleton3D
	_index_meshes()
	_play_idle()


func set_hair(hair_rel_gltf: String, color_overrides: Dictionary) -> void:
	if hair_node != null and is_instance_valid(hair_node):
		hair_node.queue_free()
		hair_node = null
	hair_colors_data.clear()
	if hair_rel_gltf == "" or model == null:
		if model != null:
			PlayerAppearance.hide_replaced_head_parts(model, "")
		return
	var sg := "%s_%s" % [str(species_meta.get("species", "human")), str(species_meta.get("gender", "male"))]
	var gltf_path := SPECIES_DIR.path_join(sg).path_join(hair_rel_gltf)
	if not FileAccess.file_exists(gltf_path):
		return
	PlayerAppearance.hide_replaced_head_parts(model, gltf_path)
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


func apply_appearance(appearance: Dictionary, scale_min: float, scale_max: float, body_height: float = 1.78) -> void:
	if model == null:
		return
	var height_t := clampf(float(appearance.get("height", 0.5)), 0.0, 1.0)
	var bounds: Dictionary = species_meta.get("bounds", {})
	var b_min: Array = bounds.get("min", [0.0, 0.0, 0.0])
	var b_max: Array = bounds.get("max", [0.0, 1.8, 0.0])
	var mesh_height := maxf(1.2, float(b_max[1]) - float(b_min[1]))
	var s := (body_height * lerpf(scale_min, scale_max, height_t)) / mesh_height
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


func set_worn_models(worn_paths: Array, weapon_model: String) -> void:
	if worn_node != null and is_instance_valid(worn_node):
		worn_node.queue_free()
		worn_node = null
	if weapon_node != null and is_instance_valid(weapon_node):
		weapon_node.queue_free()
		weapon_node = null
	if model == null or _skeleton == null:
		return

	var sp_id := str(species_meta.get("species", "human"))
	var has_helmet := false
	if model != null:
		for mi: MeshInstance3D in _meshes:
			if mi.mesh == null:
				continue
			for surface in mi.mesh.get_surface_count():
				var src := mi.mesh.surface_get_material(surface)
				if src != null and src.resource_name.begins_with("wke_alpha"):
					mi.set_surface_override_material(surface, null)
		if sp_id == "wookiee" and not worn_paths.is_empty():
			var invis := StandardMaterial3D.new()
			invis.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			invis.albedo_color = Color(1, 1, 1, 0)
			for mi: MeshInstance3D in _meshes:
				if mi.mesh == null:
					continue
				for surface in mi.mesh.get_surface_count():
					var src := mi.mesh.surface_get_material(surface)
					if src != null and src.resource_name.begins_with("wke_alpha"):
						mi.set_surface_override_material(surface, invis)
	if not worn_paths.is_empty():
		var body_blends := {}
		for mi: MeshInstance3D in _meshes:
			if mi.mesh == null:
				continue
			for i in mi.mesh.get_blend_shape_count():
				var bname := String(mi.mesh.get_blend_shape_name(i))
				if bname in ["blend_muscle", "blend_fat", "blend_skinny"]:
					body_blends[bname] = mi.get_blend_shape_value(i)
		var holder := Node3D.new()
		holder.name = "StageWorn"
		_skeleton.add_child(holder)
		worn_node = holder
		for raw_rel: Variant in worn_paths:
			var rel := str(raw_rel).strip_edges()
			if rel == "":
				continue
			var gltf_path := "res://imported_assets/" + rel
			if not FileAccess.file_exists(gltf_path):
				continue
			var doc := GLTFDocument.new()
			var state := GLTFState.new()
			if doc.append_from_file(ProjectSettings.globalize_path(gltf_path), state) != OK:
				continue
			var scene := doc.generate_scene(state) as Node3D
			if scene == null:
				continue
			var cdata := SwgColors.load_colors(ProjectSettings.globalize_path(gltf_path))
			if not cdata.is_empty():
				SwgColors.apply(scene, cdata, 0)
			if rel.contains("helmet"):
				has_helmet = true
			for m: Node in scene.find_children("*", "MeshInstance3D", true, false):
				var mi := m as MeshInstance3D
				mi.get_parent().remove_child(mi)
				mi.owner = null
				holder.add_child(mi)
				mi.skeleton = NodePath("../..")
				var grow := 0.0
				if rel.begins_with("armor/"):
					grow = ARMOR_GROW
				elif rel.begins_with("clothing/"):
					grow = WOOKIEE_CLOTHING_GROW if sp_id == "wookiee" else CLOTHING_GROW
				if grow > 0.0:
					for surface in mi.get_surface_override_material_count():
						var material := mi.get_surface_override_material(surface) as ShaderMaterial
						if material != null:
							material.set_shader_parameter("grow", grow)
				if mi.mesh != null and not body_blends.is_empty():
					for i in mi.mesh.get_blend_shape_count():
						var bname := String(mi.mesh.get_blend_shape_name(i))
						if body_blends.has(bname):
							mi.set_blend_shape_value(i, float(body_blends[bname]))
			scene.queue_free()

	if hair_node != null and is_instance_valid(hair_node):
		hair_node.visible = not has_helmet

	if weapon_model != "":
		var wpath := "res://imported_assets/" + weapon_model
		if FileAccess.file_exists(wpath):
			var doc := GLTFDocument.new()
			var state := GLTFState.new()
			if doc.append_from_file(ProjectSettings.globalize_path(wpath), state) == OK:
				var wscene := doc.generate_scene(state) as Node3D
				if wscene != null:
					var bone_idx := _skeleton.find_bone("hold_r")
					if bone_idx >= 0:
						var attach := BoneAttachment3D.new()
						attach.bone_idx = bone_idx
						_skeleton.add_child(attach)
						attach.add_child(wscene)
						weapon_node = attach
					else:
						wscene.queue_free()


func clear() -> void:
	_meshes.clear()
	_blend_indices.clear()
	_skeleton = null
	colors_data.clear()
	hair_colors_data.clear()
	species_meta.clear()
	if worn_node != null and is_instance_valid(worn_node):
		worn_node.queue_free()
	worn_node = null
	if weapon_node != null and is_instance_valid(weapon_node):
		weapon_node.queue_free()
	weapon_node = null
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
