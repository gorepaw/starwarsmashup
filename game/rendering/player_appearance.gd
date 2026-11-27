# Shared SWG character appearance loader & customizer for both the local
# player and replicated remote players: loads the species glTF, scales it to
# the species' height & height slider, applies blend shapes, SwgColors, and
# attaches hair to Skeleton3D.
class_name PlayerAppearance
extends RefCounted

const Soldier = preload("res://rendering/soldier.gd")
const SwgOcclusion = preload("res://rendering/swg_occlusion.gd")
const SPECIES_DIR := "res://imported_assets/species"
const DEFAULT_FALLBACK := "res://imported_assets/characters/stormtrooper/stormtrooper.gltf"
# (Worn things aren't stood off the skin: what they cover isn't drawn, by
# Galaxies' occlusion zones, SwgOcclusion.)

static var _species_by_id: Dictionary = {}
static var _meta_cache: Dictionary = {}
static var _colors_cache: Dictionary = {}
static var _invisible_material: Material


static func _ensure_species_table() -> void:
	if not _species_by_id.is_empty():
		return
	if ClassDB.class_exists("SwmCharacterStore"):
		var list: Array = SwmCharacterStore.get_species_list()
		for sp: Dictionary in list:
			_species_by_id[str(sp.get("id", ""))] = sp


static func species_info(species_id: String) -> Dictionary:
	_ensure_species_table()
	if _species_by_id.has(species_id):
		return _species_by_id[species_id]
	return {
		"id": species_id,
		"male_height": 1.78,
		"female_height": 1.69,
		"male_eye_height": 1.60,
		"female_eye_height": 1.52,
		"scale_min": 0.88,
		"scale_max": 1.12,
	}


static func species_meta(sg: String) -> Dictionary:
	if _meta_cache.has(sg):
		return _meta_cache[sg]
	var meta_path := SPECIES_DIR.path_join(sg).path_join(sg + ".species.json")
	var out: Dictionary = {}
	if FileAccess.file_exists(meta_path):
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(meta_path))
		if parsed is Dictionary:
			out = parsed
	_meta_cache[sg] = out
	return out


static func _load_gltf_colors(gltf_path: String) -> Dictionary:
	var os_path := ProjectSettings.globalize_path(gltf_path)
	if not _colors_cache.has(os_path):
		_colors_cache[os_path] = SwgColors.load_colors(os_path)
	return _colors_cache[os_path]


static func override_path_from_worn(worn_csv: String) -> String:
	if worn_csv.begins_with("override:"):
		return worn_csv.substr(9).strip_edges()
	return ""


static func load_character(species_id: String, gender: String, appearance_json: String,
		fallback_gltf: String = "", override_rel: String = "") -> Dictionary:
	var sp := species_id.to_lower() if species_id != "" else "human"
	var gen := "female" if gender.to_lower() == "female" else "male"
	var sg := "%s_%s" % [sp, gen]
	var override_gltf := ("res://imported_assets/" + override_rel) if override_rel != "" else ""
	var has_override := override_gltf != "" and FileAccess.file_exists(override_gltf)
	var species_gltf := SPECIES_DIR.path_join(sg).path_join(sg + ".gltf")
	var use_species := not has_override and FileAccess.file_exists(species_gltf) and (
			not appearance_json.is_empty() or sp != "human" or fallback_gltf == "")
	var gltf_path := override_gltf if has_override else (species_gltf if use_species else (fallback_gltf if fallback_gltf != "" else DEFAULT_FALLBACK))
	if not FileAccess.file_exists(gltf_path):
		gltf_path = DEFAULT_FALLBACK

	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	var os_gltf := ProjectSettings.globalize_path(gltf_path)
	if doc.append_from_file(os_gltf, state) != OK:
		printerr("PlayerAppearance: cannot load ", gltf_path)
		return {}
	var model: Node3D = doc.generate_scene(state) as Node3D
	if model == null:
		return {}
	SwgOcclusion.tag(model, gltf_path)

	var sp_info := species_info(sp)
	var base_height := float(sp_info.get("female_height" if gen == "female" else "male_height", 1.78))
	var base_eye := float(sp_info.get("female_eye_height" if gen == "female" else "male_eye_height", 1.60))
	var scale_min := float(sp_info.get("scale_min", 0.88))
	var scale_max := float(sp_info.get("scale_max", 1.12))

	var appearance: Dictionary = {}
	if not appearance_json.is_empty():
		var parsed: Variant = JSON.parse_string(appearance_json)
		if parsed is Dictionary:
			appearance = parsed

	var height_t := clampf(float(appearance.get("height", 0.5)), 0.0, 1.0)
	var height_mult := lerpf(scale_min, scale_max, height_t)
	var hair_node: Node3D = null

	if use_species:
		var meta := species_meta(sg)
		var bounds: Dictionary = meta.get("bounds", {})
		var b_min: Array = bounds.get("min", [0.0, 0.0, 0.0])
		var b_max: Array = bounds.get("max", [0.0, 1.8, 0.0])
		var mesh_height := maxf(1.2, float(b_max[1]) - float(b_min[1]))
		var visual_scale := (base_height * height_mult) / mesh_height
		model.scale = Vector3.ONE * visual_scale

		# Blend shapes from slider values.
		var sliders: Dictionary = appearance.get("sliders", {})
		var blends := {}
		for c: Dictionary in meta.get("customizations", []):
			if str(c.get("type", "")) != "hslider" or bool(c.get("discrete", false)) or bool(c.get("is_scale", false)):
				continue
			var cname := str(c.get("name", ""))
			var vars_str := str(c.get("variables", ""))
			if vars_str == "" or vars_str.begins_with("index_"):
				continue
			var t := float(sliders.get(cname, 0.5))
			if bool(c.get("reverse", false)):
				t = 1.0 - t
			var parts := vars_str.split(",", false)
			if parts.size() == 2:
				blends[parts[0]] = maxf(0.0, (0.5 - t) * 2.0)
				blends[parts[1]] = maxf(0.0, (t - 0.5) * 2.0)
			elif parts.size() == 1:
				blends[parts[0]] = t
		if not blends.is_empty():
			for node in model.find_children("*", "MeshInstance3D", true, false):
				var mi := node as MeshInstance3D
				if mi.mesh == null:
					continue
				for i in mi.mesh.get_blend_shape_count():
					var bname := String(mi.mesh.get_blend_shape_name(i))
					if blends.has(bname):
						mi.set_blend_shape_value(i, clampf(float(blends[bname]), 0.0, 1.0))

		# The hair style replaces the head's own default pieces.
		var chosen_hair := str(appearance.get("hair", ""))
		hide_replaced_head_parts(model, SPECIES_DIR.path_join(sg).path_join("hairs").path_join(chosen_hair + ".gltf")
				if chosen_hair != "" else "")

		# Palette & face markings.
		var color_overrides: Dictionary = appearance.get("colors", {})
		var colors_data := _load_gltf_colors(gltf_path)
		if not colors_data.is_empty():
			SwgColors.apply_custom(model, colors_data, color_overrides)

		# Hair attachment onto Skeleton3D.
		var hair_id := str(appearance.get("hair", ""))
		if hair_id != "":
			var hair_gltf := SPECIES_DIR.path_join(sg).path_join("hairs").path_join(hair_id + ".gltf")
			if FileAccess.file_exists(hair_gltf):
				hair_node = _attach_hair(model, hair_gltf, color_overrides)
		SwgOcclusion.apply(model)
	else:
		var colors_data := _load_gltf_colors(gltf_path)
		if not colors_data.is_empty():
			SwgColors.apply(model, colors_data, 0)

	var eye_height := base_eye * height_mult
	var crouched_eye := eye_height * (1.1 / 1.6)
	var key := "%s|%s|%s|%s" % [sp, gen, appearance_json, gltf_path]
	return {
		"model": model,
		"soldier": Soldier.new(model),
		"hair": hair_node,
		"species": sp,
		"gender": gen,
		"appearance_key": key,
		"eye_height": eye_height,
		"crouched_eye_height": crouched_eye,
	}


# A hair style may be drawn in place of a piece of the head's own: a Twi'lek's
# default lekku, which the style's own replace ("twk_m_leccu_swap_hcs16" swaps
# "twk_m_leccu"; its skull cap, "twk_m_skull_hs9", goes over the head's own).
# Hides the pieces of the model's head the style (`hair_gltf`, its file; ""
# shows them all again) swaps, so two sets of lekku don't stand one over the other.
static func hide_replaced_head_parts(model: Node, hair_gltf: String) -> void:
	var replaced := {}
	if hair_gltf != "" and FileAccess.file_exists(hair_gltf):
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(hair_gltf))
		if parsed is Dictionary:
			for material: Dictionary in (parsed as Dictionary).get("materials", []):
				var material_name := str(material.get("name", ""))
				var swap := material_name.find("_swap")
				if swap > 0:
					replaced[material_name.substr(0, swap)] = true
				elif material_name.contains("leccu") or material_name.contains("lekku"):
					replaced[material_name] = true
	for node: Node in model.find_children("*", "MeshInstance3D", true, false):
		var mi := node as MeshInstance3D
		if mi.mesh == null:
			continue
		var hidden := {}
		for surface in mi.mesh.get_surface_count():
			var source := mi.mesh.surface_get_material(surface)
			if source != null and replaced.has(source.resource_name):
				hidden[surface] = true
		var before: Dictionary = mi.get_meta("hidden_surfaces", {})
		if hidden.is_empty() and before.is_empty():
			continue
		for surface: int in before:
			mi.set_surface_override_material(surface, null)  # (its colors are applied again after)
		if hidden.is_empty():
			mi.remove_meta("hidden_surfaces")
			continue
		mi.set_meta("hidden_surfaces", hidden)
		for surface: int in hidden:
			mi.set_surface_override_material(surface, _invisible())


static func _invisible() -> Material:
	if _invisible_material == null:
		var material := StandardMaterial3D.new()
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		material.albedo_color = Color(1, 1, 1, 0)
		_invisible_material = material
	return _invisible_material


static func _attach_hair(model: Node3D, hair_gltf: String, color_overrides: Dictionary) -> Node3D:
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	if doc.append_from_file(ProjectSettings.globalize_path(hair_gltf), state) != OK:
		return null
	var scene := doc.generate_scene(state) as Node3D
	if scene == null:
		return null
	SwgOcclusion.tag(scene, hair_gltf)
	var skeleton := model.find_child("Skeleton3D", true, false) as Skeleton3D
	var hair_meshes := scene.find_children("*", "MeshInstance3D", true, false)
	var hair_node: Node3D
	if skeleton != null and not hair_meshes.is_empty():
		var holder := Node3D.new()
		holder.name = "HairAttachment"
		for m in hair_meshes:
			var mi := m as MeshInstance3D
			mi.get_parent().remove_child(mi)
			holder.add_child(mi)
		skeleton.add_child(holder)
		for child in holder.get_children():
			if child is MeshInstance3D:
				(child as MeshInstance3D).skeleton = NodePath("../..")
		hair_node = holder
		scene.queue_free()
	else:
		model.add_child(scene)
		hair_node = scene
	var hcolors := _load_gltf_colors(hair_gltf)
	if hair_node != null and not hcolors.is_empty():
		SwgColors.apply_custom(hair_node, hcolors, color_overrides)
	return hair_node


static func set_first_person(character: Dictionary, enabled: bool) -> void:
	if character.has("soldier") and character["soldier"] != null:
		(character["soldier"] as Soldier).first_person = enabled
	var helmets: Array = character.get("helmet_nodes", [])
	for h: Variant in helmets:
		if h is Node3D and is_instance_valid(h):
			(h as Node3D).visible = not enabled
	var hair: Variant = character.get("hair", null)
	if hair is Node3D and is_instance_valid(hair):
		(hair as Node3D).visible = not enabled and helmets.is_empty()


static func set_worn_models(character: Dictionary, worn_csv: String) -> void:
	if character.is_empty() or str(character.get("worn_csv", "")) == worn_csv:
		return
	character["worn_csv"] = worn_csv
	var model: Variant = character.get("model")
	if not (model is Node3D) or not is_instance_valid(model):
		return
	var skeleton := (model as Node3D).find_child("Skeleton3D", true, false) as Skeleton3D
	if skeleton == null:
		return
	var old_holder: Variant = character.get("worn_holder")
	if old_holder is Node and is_instance_valid(old_holder):
		(old_holder as Node).queue_free()
	character["worn_holder"] = null
	character["helmet_nodes"] = []

	var paths: PackedStringArray = PackedStringArray() if worn_csv.begins_with("override:") else worn_csv.split(",", false)
	var sp_id := str(character.get("species", ""))
	_set_wookiee_fur_hidden(model as Node3D, sp_id == "wookiee" and not paths.is_empty())
	if paths.is_empty():
		var hair: Variant = character.get("hair", null)
		var fp := character.has("soldier") and character["soldier"] != null and (character["soldier"] as Soldier).first_person
		if hair is Node3D and is_instance_valid(hair):
			(hair as Node3D).visible = not fp
		SwgOcclusion.apply(model as Node3D)
		return

	# Read body blend weights (muscle, fat, skinny) so armor/clothing fits the body.
	var body_blends := {}
	for n: Node in (model as Node3D).find_children("*", "MeshInstance3D", true, false):
		var mi := n as MeshInstance3D
		if mi.mesh == null:
			continue
		for i in mi.mesh.get_blend_shape_count():
			var bname := String(mi.mesh.get_blend_shape_name(i))
			if bname in ["blend_muscle", "blend_fat", "blend_skinny"]:
				body_blends[bname] = mi.get_blend_shape_value(i)

	# The body's own material: a worn piece's "skin_body" surface (a Wookiee's
	# Ceremonial leggings leave some fur showing) is the wearer's skin, as in
	# Galaxies.
	var body_material: Material = null
	for n: Node in (model as Node3D).find_children("*", "MeshInstance3D", true, false):
		var mi := n as MeshInstance3D
		if mi.mesh == null or body_material != null:
			continue
		for surface in mi.mesh.get_surface_count():
			var source := mi.mesh.surface_get_material(surface)
			if source != null and source.resource_name.ends_with("_body"):
				body_material = mi.get_active_material(surface)
				break

	var holder := Node3D.new()
	holder.name = "WornEquipment"
	skeleton.add_child(holder)
	character["worn_holder"] = holder
	var helmets: Array = []
	var fp := character.has("soldier") and character["soldier"] != null and (character["soldier"] as Soldier).first_person

	for raw_rel: String in paths:
		var rel := raw_rel.strip_edges()
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
		var colors := _load_gltf_colors(gltf_path)
		if not colors.is_empty():
			SwgColors.apply(scene, colors, 0)
		SwgOcclusion.tag(scene, gltf_path)
		var is_helmet := rel.contains("helmet")
		for m: Node in scene.find_children("*", "MeshInstance3D", true, false):
			var mi := m as MeshInstance3D
			mi.get_parent().remove_child(mi)
			mi.owner = null
			holder.add_child(mi)
			mi.skeleton = NodePath("../..")
			if mi.mesh != null and body_material != null:
				for surface in mi.mesh.get_surface_count():
					var worn_mat := mi.mesh.surface_get_material(surface)
					if worn_mat != null and worn_mat.resource_name.begins_with("skin_body"):
						mi.set_surface_override_material(surface, body_material)
			if mi.mesh != null and not body_blends.is_empty():
				for i in mi.mesh.get_blend_shape_count():
					var bname := String(mi.mesh.get_blend_shape_name(i))
					if body_blends.has(bname):
						mi.set_blend_shape_value(i, float(body_blends[bname]))
			if is_helmet:
				mi.visible = not fp
				helmets.append(mi)
		scene.queue_free()

	character["helmet_nodes"] = helmets
	var hair: Variant = character.get("hair", null)
	if hair is Node3D and is_instance_valid(hair):
		(hair as Node3D).visible = not fp and helmets.is_empty()
	# What's worn hides what it covers (a hair style under a helmet, nothing).
	SwgOcclusion.apply(model as Node3D, [hair] if not helmets.is_empty() else [])


static func _set_wookiee_fur_hidden(model: Node3D, hidden: bool) -> void:
	for n: Node in model.find_children("*", "MeshInstance3D", true, false):
		var mi := n as MeshInstance3D
		if mi.mesh == null:
			continue
		for surface in mi.mesh.get_surface_count():
			var source := mi.mesh.surface_get_material(surface)
			if source != null and source.resource_name.begins_with("wke_alpha"):
				mi.set_surface_override_material(surface, _invisible() if hidden else null)
