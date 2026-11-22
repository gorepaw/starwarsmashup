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
#
# In first person the upper body is steadied (see Aimer): the chest rides on
# the hips' average over the run cycle, not their every bob and turn; steps
# up and down are smoothed; the recoil and flinch clips give way to a small
# kick, bob and sway of the arms; reloads and throws move only the arms.
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
# First person: one-shot clips (reload, throw) leave these to the steadied chest.
const STEADY := ["spine1", "spine2", "spine3", "neck", "Head"]
const KICK_DECAY := 14.0         # per second, the first-person recoil settling
const KICK_BACK := 0.025         # m a shot pushes the arms back
const KICK_UP := deg_to_rad(2.5) # and turns the muzzle up
const BOB := Vector2(0.007, 0.005)  # m sideways and up at a full run
const STRIDE := 1.7              # m a full run cycle (two steps) covers
const SWAY := 0.012              # radians of lag per radian a second of turning
const SWAY_LIMIT := 0.05
const STEP_SNAP := 0.06          # m: a height change in one frame this big is a step
const STEP_SETTLE := 12.0        # per second, a step smoothed out
const VIEW_OFFSET := Vector3(-0.015, -0.045, 0.0)  # m the arms sit lower and to the right in first person
const DIP_SPEED := 5.0           # per second, the gun dipping for a first-person reload or throw
const DIP_TURN := Vector2(deg_to_rad(16), deg_to_rad(10))  # its muzzle down, and rolled
const DIP_DROP := 0.05           # m it drops
const DIP_PIVOT := Vector3(-0.12, -0.2, 0.35)  # about where the hands are, from the eyes (+X left, +Z forward)
const THROW_DIP := 0.45         # s the gun stays dipped for a first-person throw

var model: Node3D
var first_person := false:
	set(value):
		if value != first_person and _tree != null:
			for layer: StringName in [&"reload", &"throw"]:
				var node: AnimationNode = (_tree.tree_root as AnimationNodeBlendTree).get_node(layer)
				for path in _steady_paths:
					node.set_filter_path(path, not value)
		first_person = value
		if _aimer != null:
			_aimer.first_person = value
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
var _steady_paths: Array[NodePath] = []  # the STEADY bones' track paths
var _kick := 0.0             # first person: 1 just after a shot, settling to 0
var _bob_phase := 0.0
var _sway := 0.0
var _last_yaw := 0.0
var _last_y := 0.0
var _step := 0.0             # m the chest still lags a step up or down
var _dip := 0.0              # first person: 0 .. 1 the gun dipped to reload or throw
var _throwing := 0.0         # s left of a first-person throw's dip


# Bends the spine with the aim pitch and turns the legs, after the animation.
# In first person it also steadies the upper body: the chest is carried on
# the hips' average pose (over about a third of a second) rather than their
# every bob and turn through the run cycle, raised or lowered by `step` (a
# step being smoothed out), and the arms move by `arms` about the chest.
# Skeleton space: the model faces +Z, up is +Y.
class Aimer extends SkeletonModifier3D:
	const AVERAGE := 3.0  # per second, the hips' average following them
	const FIRST_PERSON_BEND := 0.3  # of the aim pitch the spine takes in first person
	var pitch := 0.0      # radians, aim up positive
	var leg_yaw := 0.0    # radians the legs turn from the aim, left positive
	var first_person := false:  # the camera is in the head: shrink it, steady the chest
		set(value):
			first_person = value
			_averaged = false
	var step := 0.0       # m the chest is carried over (+) or under the hips
	var arms := Transform3D()  # first person: the arms' kick, bob and sway, about the chest
	var eye_local := Vector3.ZERO  # the head as last drawn (skeleton space)
	var _spine: Array[int] = []
	var _root := -1
	var _head := -1
	var _clavicles: Array[int] = []
	var _average := Transform3D()
	var _averaged := false

	func setup(skeleton: Skeleton3D) -> void:
		for bone_name: String in SPINE:
			var bone := skeleton.find_bone(bone_name)
			if bone >= 0:
				_spine.append(bone)
		_root = skeleton.find_bone("root")
		_head = skeleton.find_bone("Head")
		for bone_name: String in ["rClav", "lClav"]:
			var bone := skeleton.find_bone(bone_name)
			if bone >= 0:
				_clavicles.append(bone)

	func _process_modification_with_delta(delta: float) -> void:
		var skeleton := get_skeleton()
		if skeleton == null or _spine.is_empty():
			return
		var steady := first_person and _root >= 0
		if steady:
			var hips := skeleton.get_bone_global_pose(_root)
			if not _averaged:
				_average = hips
				_averaged = true
			var f := minf(1.0, AVERAGE * delta)
			_average = Transform3D(_average.basis.slerp(hips.basis, f).orthonormalized(),
					_average.origin.lerp(hips.origin, f))
		# Legs: turn the hips toward the movement; the spine turns back (in
		# first person the chest is set from the average below instead).
		if _root >= 0 and absf(leg_yaw) > 0.001:
			_rotate_global(skeleton, _root, Basis(Vector3.UP, leg_yaw))
			if not steady:
				_rotate_global(skeleton, _spine[0], Basis(Vector3.UP, -leg_yaw))
		if steady:
			# The chest where it sits on the averaged hips.
			var carried := Transform3D(_average.basis, _average.origin + Vector3.UP * step)
			_set_global(skeleton, _spine[0], carried * _local(skeleton, _spine[0]))
		# Aim: the model faces +Z, so raising the aim turns about -X. In first
		# person the spine takes only some of it, so the eyes don't drop far
		# looking down; the arms turn the rest about the eyes, as a shooter's
		# view model does.
		var bend := pitch * (FIRST_PERSON_BEND if steady else 1.0)
		var share := Basis(Vector3.RIGHT, -bend / _spine.size())
		for bone in _spine:
			_rotate_global(skeleton, bone, share)
		if steady and _head >= 0:
			# The rest of the aim and the arms' motion, in the model's frame
			# turned with the bend, about the eyes.
			var eyes := skeleton.get_bone_global_pose(_head).origin
			var about := Transform3D(Basis(Vector3.RIGHT, -bend), eyes)
			var move := about * Transform3D(Basis(Vector3.RIGHT, bend - pitch)) * arms * about.affine_inverse()
			for bone in _clavicles:
				_set_global(skeleton, bone, move * skeleton.get_bone_global_pose(bone))
		# Shrinks the head and whatever is worn on it (helmet, hair).
		if _head >= 0:
			skeleton.set_bone_pose_scale(_head, Vector3.ONE * (0.001 if first_person else 1.0))
			# Read now: once the modifiers are done the skeleton's poses go back
			# to the animation's, without the bending and steadying.
			eye_local = skeleton.get_bone_global_pose(_head).origin

	static func _local(skeleton: Skeleton3D, bone: int) -> Transform3D:
		return Transform3D(Basis(skeleton.get_bone_pose_rotation(bone)).scaled(skeleton.get_bone_pose_scale(bone)),
				skeleton.get_bone_pose_position(bone))

	# Poses a bone so its skeleton-space transform is `target` (its scale kept).
	static func _set_global(skeleton: Skeleton3D, bone: int, target: Transform3D) -> void:
		var parent := skeleton.get_bone_parent(bone)
		var parent_pose := skeleton.get_bone_global_pose(parent) if parent >= 0 else Transform3D()
		var local := parent_pose.affine_inverse() * target
		skeleton.set_bone_pose_position(bone, local.origin)
		skeleton.set_bone_pose_rotation(bone, local.basis.get_rotation_quaternion())

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
	var upper := _upper_body_paths()
	for path in upper:
		if STEADY.has(str(path.get_subname(0))):
			_steady_paths.append(path)
	_tree.tree_root = _build_tree(upper)
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
	if first_person:
		_tree.set("parameters/shoot_seek/seek_request", 0.0)  # the raised pose, without the recoil clip
		_first_person_motion(state, speed, delta)
	var target := 1.0 if _since_shot < AIM_HOLD or first_person else 0.0
	_aim = move_toward(_aim, target, AIM_BLEND * delta)
	_tree.set("parameters/aim/blend_amount", _aim)

	var reloading := bool(state.get("reloading", false))
	if reloading and not _reloading and not first_person:  # first person dips the gun instead
		_tree.set("parameters/reload/request", AnimationNodeOneShot.ONE_SHOT_REQUEST_FIRE)
	_reloading = reloading

	if _aimer != null:
		# Relaxed, the rifle points down; raised, the whole aim shows.
		_aimer.pitch = float(state.get("pitch", 0.0)) * lerpf(0.5, 1.0, _aim)
		_aimer.leg_yaw = _leg_yaw


# First person: the arms kick on a shot, bob with the stride and lag behind a
# turn; steps up and down are smoothed out of the chest's height.
func _first_person_motion(state: Dictionary, speed: float, delta: float) -> void:
	var yaw := float(state["yaw"])
	var y: float = (state["position"] as Vector3).y
	if absf(y - _last_y) > STEP_SNAP:
		_step = clampf(_step - (y - _last_y), -0.5, 0.5)
	_last_y = y
	_step *= exp(-STEP_SETTLE * delta)
	_kick *= exp(-KICK_DECAY * delta)
	_throwing = maxf(0.0, _throwing - delta)
	var dipped := bool(state.get("reloading", false)) or _throwing > 0.0
	_dip = move_toward(_dip, 1.0 if dipped else 0.0, DIP_SPEED * delta)
	var dip := smoothstep(0.0, 1.0, _dip)
	var run := clampf(speed / RUN_SPEED, 0.0, 1.0)
	_bob_phase = fmod(_bob_phase + speed * delta / STRIDE * TAU, TAU)
	var turn_rate := angle_difference(_last_yaw, yaw) / maxf(delta, 1e-4)
	_last_yaw = yaw
	_sway = lerpf(_sway, clampf(-turn_rate * SWAY, -SWAY_LIMIT, SWAY_LIMIT), minf(1.0, 10.0 * delta))
	if _aimer == null:
		return
	_aimer.step = _step
	# The chest's frame: +Z forward, +Y up, +X the soldier's left.
	var offset := VIEW_OFFSET + Vector3(BOB.x * sin(_bob_phase) * run,
			BOB.y * sin(2.0 * _bob_phase) * run - DIP_DROP * dip, -KICK_BACK * _kick)
	var turn := Basis(Vector3.UP, _sway) * Basis(Vector3.RIGHT, -KICK_UP * _kick)
	# The dip turns the gun about the hands, not the eyes.
	var tilt := Basis(Vector3.RIGHT, DIP_TURN.x * dip) * Basis(Vector3.BACK, DIP_TURN.y * dip)
	var dip_about := Transform3D(Basis(), DIP_PIVOT) * Transform3D(tilt) * Transform3D(Basis(), -DIP_PIVOT)
	_aimer.arms = Transform3D(turn, offset) * dip_about


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
	_kick = 1.0
	if _tree != null:
		_tree.set("parameters/shoot_seek/seek_request", 0.0)


func throw() -> void:
	_since_shot = AIM_HOLD  # lowers the rifle
	if first_person:
		_throwing = THROW_DIP  # the gun dips instead: the clip swings the arm behind the camera
	elif _tree != null:
		_tree.set("parameters/throw/request", AnimationNodeOneShot.ONE_SHOT_REQUEST_FIRE)


func flinch() -> void:
	if _tree != null and not first_person:
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


# The head's position (world space), as last drawn (bent with the aim and,
# in first person, steadied) but where the model is now; a little above the
# waist if the skeleton has no head.
func eye() -> Vector3:
	if _skeleton == null or _head < 0:
		return model.global_position + Vector3.UP * 1.6
	if _aimer != null and _aimer.eye_local != Vector3.ZERO:
		return _skeleton.global_transform * _aimer.eye_local
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
