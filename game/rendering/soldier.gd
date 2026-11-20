# A soldier model (swgtool export-character with Battlefront II animations)
# animated from its networked state: legs run, walk or stand from its
# velocity, while the upper body raises the rifle and fires, reloads or throws
# on top (Battlefront's *_full clips, upper-body bones only). The spine bends
# with the aim pitch, and when strafing the legs turn toward where it's going
# while the chest keeps facing the aim, as in Battlefront.
#
#   var soldier := Soldier.new(model)     # the glTF scene, already in the tree
#   soldier.pose(state, delta)            # each frame; state from SwmClient
#   soldier.fire() / throw() / flinch()   # on its shots and hits
#   soldier.muzzle()                      # where its bolts leave the barrel
#   soldier.first_person = true           # own soldier seen from its eyes: no head, rifle up
#   soldier.eye()                         # where a first-person camera sits
extends RefCounted

const IDLE := &"human_rifle_stand_idle_emote_full"
const RUN := &"human_rifle_stand_runforward"
const RUN_BACK := &"human_rifle_stand_runbackward"
const SHOOT := &"human_rifle_stand_shoot_full"
const RELOAD := &"human_rifle_stand_reload_full"
const THROW := &"human_rifle_stand_shoot_secondary_full"
const HIT := &"human_rifle_stand_hitfront"
const DEATHS: Array[StringName] = [&"human_rifle_stand_death_forward", &"human_rifle_stand_death_backward"]
const RUN_SPEED := 4.5       # m/s the run cycle matches
const AIM_HOLD := 1.2        # seconds the rifle stays raised after a shot
const AIM_BLEND := 8.0       # per second, raising and lowering it
const MAX_LEG_TURN := deg_to_rad(70)
const CORPSE_TIME := 4.0     # seconds a body stays after dying

# Driven by the upper-body layers; the legs and hips keep the locomotion.
const UPPER_BODY := ["spine1", "spine2", "spine3", "neck", "Head",
		"lClav", "lArm", "lForeArm", "lUlna", "lWrist", "lThumb01", "lThumb02", "lIndex01", "lIndex02",
		"lRing01", "lRing02", "hold_l",
		"rClav", "rArm", "rForeArm", "rUlna", "rWrist", "rThumb01", "rThumb02", "rIndex01", "rIndex02",
		"rRing01", "rRing02", "hold_r"]
const SPINE := ["spine1", "spine2", "spine3"]

var model: Node3D
var first_person := false:
	set(value):
		first_person = value
		if _aimer != null:
			_aimer.hide_head = value
var _player: AnimationPlayer
var _skeleton: Skeleton3D
var _head := -1
var _tree: AnimationTree
var _aimer: Aimer
var _muzzle: Node3D
var _aim := 0.0             # 0 relaxed .. 1 rifle raised
var _since_shot := 1e9
var _reloading := false
var _alive := true
var _dead_time := 0.0
var _leg_yaw := 0.0


# Bends the spine with the aim pitch and turns the legs, after the animation.
class Aimer extends SkeletonModifier3D:
	var pitch := 0.0      # radians, aim up positive
	var leg_yaw := 0.0    # radians the legs turn from the aim, left positive
	var hide_head := false  # first person: the camera is inside it
	var _spine: Array[int] = []
	var _root := -1
	var _head := -1

	func setup(skeleton: Skeleton3D) -> void:
		for bone_name: String in SPINE:
			var bone := skeleton.find_bone(bone_name)
			if bone >= 0:
				_spine.append(bone)
		_root = skeleton.find_bone("root")
		_head = skeleton.find_bone("Head")

	func _process_modification_with_delta(_delta: float) -> void:
		var skeleton := get_skeleton()
		if skeleton == null or _spine.is_empty():
			return
		# Legs: turn the hips toward the movement; the spine turns back.
		if _root >= 0 and absf(leg_yaw) > 0.001:
			_rotate_global(skeleton, _root, Basis(Vector3.UP, leg_yaw))
			_rotate_global(skeleton, _spine[0], Basis(Vector3.UP, -leg_yaw))
		# Aim: the model faces +Z, so raising the aim turns about -X.
		var share := Basis(Vector3.RIGHT, -pitch / _spine.size())
		for bone in _spine:
			_rotate_global(skeleton, bone, share)
		# Shrinks the head and whatever is worn on it (helmet, hair).
		if _head >= 0:
			skeleton.set_bone_pose_scale(_head, Vector3.ONE * (0.001 if hide_head else 1.0))

	# Rotates a bone by `turn` in skeleton space, about its own origin.
	static func _rotate_global(skeleton: Skeleton3D, bone: int, turn: Basis) -> void:
		var parent := skeleton.get_bone_parent(bone)
		var parent_basis := skeleton.get_bone_global_pose(parent).basis if parent >= 0 else Basis()
		var local := Basis(skeleton.get_bone_pose_rotation(bone))
		var rotated := parent_basis.inverse() * turn * parent_basis * local
		skeleton.set_bone_pose_rotation(bone, rotated.get_rotation_quaternion())


func _init(scene: Node3D) -> void:
	model = scene
	_player = model.find_child("AnimationPlayer", true, false) as AnimationPlayer
	_muzzle = model.find_child("hp_muzzle", true, false) as Node3D
	var skeleton := model.find_child("Skeleton3D", true, false) as Skeleton3D
	_skeleton = skeleton
	if skeleton != null:
		_head = skeleton.find_bone("Head")
		_aimer = Aimer.new()
		_aimer.setup(skeleton)
		skeleton.add_child(_aimer)
	if _player == null or not _player.has_animation(SHOOT):
		return  # an older export without the firing clips: run and idle only
	for looping: StringName in [IDLE, RUN, RUN_BACK]:
		if _player.has_animation(looping):
			_player.get_animation(looping).loop_mode = Animation.LOOP_LINEAR
	_tree = AnimationTree.new()
	_player.get_parent().add_child(_tree)
	_tree.anim_player = _tree.get_path_to(_player)
	_tree.tree_root = _build_tree(_upper_body_paths())
	_tree.active = true


# Legs: idle <-> run (back or forward) scaled by speed. Upper body on top:
# the raised-rifle firing clip (restarted per shot), then reload, throw and
# flinch as one-shots.
func _build_tree(upper: Array[NodePath]) -> AnimationNodeBlendTree:
	var root := AnimationNodeBlendTree.new()
	var move := AnimationNodeBlendSpace1D.new()
	move.add_blend_point(_clip(RUN_BACK if _player.has_animation(RUN_BACK) else RUN), -1.0, -1, &"back")
	move.add_blend_point(_clip(IDLE), 0.0, -1, &"idle")
	move.add_blend_point(_clip(RUN), 1.0, -1, &"run")
	root.add_node(&"move", move)
	root.add_node(&"move_speed", AnimationNodeTimeScale.new())
	root.connect_node(&"move_speed", 0, &"move")

	root.add_node(&"shoot", _clip(SHOOT))
	root.add_node(&"shoot_seek", AnimationNodeTimeSeek.new())
	root.connect_node(&"shoot_seek", 0, &"shoot")
	var aim := AnimationNodeBlend2.new()
	_filter(aim, upper)
	root.add_node(&"aim", aim)
	root.connect_node(&"aim", 0, &"move_speed")
	root.connect_node(&"aim", 1, &"shoot_seek")

	var last := &"aim"
	for layer: Array in [[&"reload", RELOAD], [&"throw", THROW], [&"flinch", HIT]]:
		var shot := AnimationNodeOneShot.new()
		shot.fadein_time = 0.12
		shot.fadeout_time = 0.2
		_filter(shot, upper)
		root.add_node(layer[0], shot)
		root.add_node(StringName(str(layer[0]) + "_clip"), _clip(layer[1]))
		root.connect_node(layer[0], 0, last)
		root.connect_node(layer[0], 1, StringName(str(layer[0]) + "_clip"))
		last = layer[0]
	root.connect_node(&"output", 0, last)
	return root


func _clip(animation: StringName) -> AnimationNodeAnimation:
	var node := AnimationNodeAnimation.new()
	node.animation = animation if _player.has_animation(animation) else IDLE
	return node


static func _filter(node: AnimationNode, paths: Array[NodePath]) -> void:
	node.filter_enabled = true
	for path in paths:
		node.set_filter_path(path, true)


# The upper-body bones' track paths, as the clips name them.
func _upper_body_paths() -> Array[NodePath]:
	var out: Array[NodePath] = []
	var animation := _player.get_animation(IDLE)
	for track in animation.get_track_count():
		var path := animation.track_get_path(track)
		if path.get_subname_count() > 0 and UPPER_BODY.has(str(path.get_subname(0))) and not out.has(path):
			out.append(NodePath(str(path.get_concatenated_names()) + ":" + str(path.get_subname(0))))
	return out


# state: position, velocity, yaw, pitch, alive, reloading (SwmClient).
func pose(state: Dictionary, delta: float) -> void:
	var alive := bool(state.get("alive", true))
	if alive != _alive:
		_alive = alive
		_dead_time = 0.0
		if not alive:
			_die()
		elif _tree != null:
			_player.stop()  # the death clip; the tree drives it again
			_tree.active = true
	if not alive:
		_dead_time += delta
		model.visible = _dead_time < CORPSE_TIME
		return
	model.visible = true
	model.position = state["position"]
	var yaw := float(state["yaw"])
	model.rotation.y = yaw + PI  # the model faces +Z; yaw 0 faces -Z
	if _tree == null:
		_pose_simple(state)
		return

	var velocity: Vector3 = state["velocity"]
	var flat := Vector2(velocity.x, velocity.z)
	var speed := flat.length()
	var facing := Vector2(-sin(yaw), -cos(yaw))
	var forward := flat.dot(facing)
	var blend := 0.0
	var leg_yaw := 0.0
	if speed > 0.5:
		var backward := forward < -0.25 * speed
		blend = -1.0 if backward else 1.0
		# The legs turn toward the movement (or away, running backward).
		var heading := -flat if backward else flat
		leg_yaw = clampf(_signed_angle(facing, heading), -MAX_LEG_TURN, MAX_LEG_TURN)
		blend *= clampf(speed / 2.0, 0.0, 1.0)
	_leg_yaw = lerp_angle(_leg_yaw, leg_yaw, minf(1.0, 10.0 * delta))
	_tree.set("parameters/move/blend_position", blend)
	_tree.set("parameters/move_speed/scale", clampf(speed / RUN_SPEED, 0.6, 1.9) if speed > 0.5 else 1.0)

	_since_shot += delta
	var target := 1.0 if _since_shot < AIM_HOLD or first_person else 0.0
	_aim = move_toward(_aim, target, AIM_BLEND * delta)
	_tree.set("parameters/aim/blend_amount", _aim)

	var reloading := bool(state.get("reloading", false))
	if reloading and not _reloading:
		_tree.set("parameters/reload/request", AnimationNodeOneShot.ONE_SHOT_REQUEST_FIRE)
	_reloading = reloading

	if _aimer != null:
		# Relaxed, the rifle points down; raised, the whole aim shows.
		_aimer.pitch = float(state.get("pitch", 0.0)) * lerpf(0.5, 1.0, _aim)
		_aimer.leg_yaw = _leg_yaw


func _pose_simple(state: Dictionary) -> void:
	if _player == null:
		return
	var velocity: Vector3 = state["velocity"]
	var speed := Vector2(velocity.x, velocity.z).length()
	var animation := RUN if speed > 0.5 else IDLE
	if _player.current_animation != animation and _player.has_animation(animation):
		_player.play(animation, 0.15)
	_player.speed_scale = clampf(speed / RUN_SPEED, 0.6, 1.9) if speed > 0.5 else 1.0


# A shot: raise the rifle (if it isn't) and play the recoil from the start.
func fire() -> void:
	_since_shot = 0.0
	if _tree != null:
		_tree.set("parameters/shoot_seek/seek_request", 0.0)


func throw() -> void:
	_since_shot = AIM_HOLD  # lowers the rifle
	if _tree != null:
		_tree.set("parameters/throw/request", AnimationNodeOneShot.ONE_SHOT_REQUEST_FIRE)


func flinch() -> void:
	if _tree != null:
		_tree.set("parameters/flinch/request", AnimationNodeOneShot.ONE_SHOT_REQUEST_FIRE)


func _die() -> void:
	if _tree == null or _player == null:
		return
	_tree.active = false
	if _aimer != null:
		_aimer.pitch = 0.0
		_aimer.leg_yaw = 0.0
	var death: StringName = DEATHS[randi() % DEATHS.size()]
	if _player.has_animation(death):
		_player.play(death, 0.1)
		_player.speed_scale = 1.0


# The head's position (world space), from the last drawn pose but where the
# model is now; a little above the waist if the skeleton has no head.
func eye() -> Vector3:
	if _skeleton == null or _head < 0:
		return model.global_position + Vector3.UP * 1.6
	return _skeleton.global_transform * _skeleton.get_bone_global_pose(_head).origin


# Where bolts leave the barrel (world space); the chest if the weapon has no
# muzzle point.
func muzzle() -> Vector3:
	if _muzzle != null and model.visible:
		return _muzzle.global_position
	return model.global_position + Vector3.UP * 1.4


static func _signed_angle(from: Vector2, to: Vector2) -> float:
	# Positive turns left (counterclockwise seen from above, as Godot's yaw).
	return -from.angle_to(to)
