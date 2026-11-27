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
#   soldier.throw("placed") / throw("utility", model)  # a mine set down low; a tactical pistol drawn and fired
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
const CROUCH_IDLE := &"human_rifle_crouch_idle_emote_full"
const CROUCH_WALK := &"human_rifle_crouch_walkforward"
const CROUCH_BACK := &"human_rifle_crouch_walkbackward"
const CROUCH_SHOOT := &"human_rifle_crouch_shoot_full"
const CROUCH_RELOAD := &"human_rifle_crouch_reload_full"
const ROLL := &"human_rifle_diveforward"  # Battlefront's roll (human_1)
# Battlefront's other animation sets, by the rifle clip each stands in for
# (human_3's pistol, human_2's bazooka); a clip a set doesn't have (the
# pistol's idle, its run backward) is the rifle's, as Battlefront falls back.
const ANIM_SETS := {
	"pistol": {
		RUN: &"human_pistol_stand_runforward",
		SHOOT: &"human_pistol_stand_shoot",
		RELOAD: &"human_pistol_stand_reload",
		CROUCH_WALK: &"human_pistol_crouch_walkforward",
		CROUCH_SHOOT: &"human_pistol_crouch_shoot",
		CROUCH_RELOAD: &"human_pistol_crouch_reload",
	},
	"bazooka": {
		IDLE: &"human_bazooka_stand_idle_emote",
		RUN: &"human_bazooka_stand_runforward",
		RUN_BACK: &"human_bazooka_stand_runbackward",
		SHOOT: &"human_bazooka_stand_shoot_full",
		RELOAD: &"human_bazooka_stand_reload_full",
		CROUCH_IDLE: &"human_bazooka_crouch_idle_emote",
		CROUCH_WALK: &"human_bazooka_crouch_walkforward",
		CROUCH_BACK: &"human_bazooka_crouch_walkbackward",
		CROUCH_SHOOT: &"human_bazooka_crouch_shoot",
		CROUCH_RELOAD: &"human_bazooka_crouch_reload",
	},
}
const CROUCH_WALK_SPEED := 3.5  # m/s the crouch walk plays at its own pace (ASSUMPTION, by eye)
# Swimming (deep water: state "swimming"): Galaxies' own clips, which
# swgtool export-species adds to each species (Battlefront's soldiers don't
# swim): stroking along, and treading water in place.
const SWIM := &"all_b_loc_swim"
const TREAD := &"all_b_idl_tread_water"
const SWIM_SPEED := 3.5  # m/s the stroke plays at its own pace (core swim_factor x run speed)
const SWIM_BLEND := 4.0  # per second, into the water or out
const CROUCH_BLEND := 6.0       # per second, getting down or up
const ROLL_TIME := 0.75         # s a roll lasts (core MoveParams::roll_time)
const RUN_SPEED := 6.0       # m/s the run cycle plays at its own pace (faster, faster; tuned by eye: 4.5 looked hurried)
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
const STRIDE := 2.27             # m a full run cycle (two steps) covers, as RUN_SPEED plays it
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
const SWING_TIME := 0.28        # s a one-handed melee swing takes
const SWING_TIME_2H := 0.42     # s a two-handed one: a heavier, slower chop (ASSUMPTION, by eye)
const PLACE_TIME := 0.7         # s bending low to set a mine down
const QUICK_SHOT := 0.6         # s a tactical pistol (flare, tangle) is out, drawn and fired
const GRIP_2H := 0.3            # m up the haft the left hand holds a two-handed melee weapon

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
var _clip_nodes: Array[Dictionary] = []  # {node, role (the rifle clip), fallback}: what set_anim_set changes
var _anim_set := "rifle"
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
var _crouch := 0.0           # 0 standing .. 1 crouched
var _swim := 0.0             # 0 on foot .. 1 swimming
var _can_swim := false       # the export has the swimming clips
var _rolling := false
var _ride_clip := &""         # riding: the pose it holds (its first frame)
var _dip := 0.0              # first person: 0 .. 1 the gun dipped to reload or throw
var _throwing := 0.0         # s left of a first-person throw's dip
var _swing := 0.0            # s left of a melee swing
var _placing := 0.0          # s left of bending to set a mine down
var _quick := 0.0            # s left of a tactical pistol out in hand (its model shown, held as a pistol)
var _wanted_weapon := "<baked>"  # what set_weapon / set_anim_set last asked for (shown again after a quick shot)
var _wanted_set := "rifle"


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
	# The left hand on the weapon's fore-grip: this point in the right hand's
	# hold point's space (the weapon's: +Z along the barrel), or none (a
	# pistol's, a launcher's set holds it its own way).
	var grip: Variant = null
	var melee_swing := 0.0
	var melee_block := false
	var two_handed := false  # a two-handed melee weapon: the chest swings it, both hands on it
	var place := 0.0         # 0..1 bent low, setting a mine down
	var _spine: Array[int] = []
	var _left_arm := -1
	var _left_forearm := -1
	var _left_wrist := -1
	var _right_arm := -1
	var _right_forearm := -1
	var _hold_left := -1
	var _hold_right := -1
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
		_left_arm = skeleton.find_bone("lArm")
		_left_forearm = skeleton.find_bone("lForeArm")
		_left_wrist = skeleton.find_bone("lWrist")
		_right_arm = skeleton.find_bone("rArm")
		_right_forearm = skeleton.find_bone("rForeArm")
		_hold_left = skeleton.find_bone("hold_l")
		_hold_right = skeleton.find_bone("hold_r")

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
		# A two-handed swing turns the chest (both arms and the weapon with it),
		# before the left hand goes back onto the haft; setting a mine down
		# bends it forward.
		if two_handed and melee_swing > 0.0 and not melee_block:
			var chop := sin(melee_swing * PI)
			_rotate_global(skeleton, _spine[_spine.size() - 1], Basis(Vector3.UP, 0.8 * chop) * Basis(Vector3.RIGHT, 0.5 * chop))
		if place > 0.0:
			var bow := sin(place * PI) * 0.55
			for bone in _spine:
				_rotate_global(skeleton, bone, Basis(Vector3.RIGHT, bow / _spine.size()))
		_grip(skeleton)
		if melee_block and _right_arm >= 0:
			_rotate_global(skeleton, _right_arm, Basis(Vector3.UP, 0.7) * Basis(Vector3.RIGHT, -0.55))
			if _right_forearm >= 0:
				_rotate_global(skeleton, _right_forearm, Basis(Vector3.UP, 0.45))
		elif melee_swing > 0.0 and _right_arm >= 0 and not two_handed:
			var arc := sin(melee_swing * PI)
			_rotate_global(skeleton, _right_arm, Basis(Vector3.RIGHT, -0.9 * arc) * Basis(Vector3.UP, 0.85 * arc))
		# Shrinks the head and whatever is worn on it (helmet, hair).
		if _head >= 0:
			skeleton.set_bone_pose_scale(_head, Vector3.ONE * (0.001 if first_person else 1.0))
			# Read now: once the modifiers are done the skeleton's poses go back
			# to the animation's, without the bending and steadying.
			eye_local = skeleton.get_bone_global_pose(_head).origin

	# The left hand onto the weapon's fore-grip (`grip`): two-bone IK on the
	# upper arm and forearm, bending in the plane the animation bends them in,
	# the hand turned as the animation has it.
	func _grip(skeleton: Skeleton3D) -> void:
		if grip == null or _left_arm < 0 or _left_forearm < 0 or _left_wrist < 0 or _hold_right < 0:
			return
		var target: Vector3 = skeleton.get_bone_global_pose(_hold_right) * (grip as Vector3)
		var wrist := skeleton.get_bone_global_pose(_left_wrist).origin
		if _hold_left >= 0:
			# Where the palm grips, not the wrist.
			target -= skeleton.get_bone_global_pose(_hold_left).origin - wrist
		var a := skeleton.get_bone_global_pose(_left_arm).origin
		var b := skeleton.get_bone_global_pose(_left_forearm).origin
		var upper := (b - a).length()
		var lower := (wrist - b).length()
		if upper < 1e-4 or lower < 1e-4:
			return
		var reach := clampf((target - a).length(), absf(upper - lower) + 1e-3, upper + lower - 1e-3)
		# The elbow: open or close it to the reach.
		var normal := (a - b).cross(wrist - b)
		if normal.length() < 1e-6:
			normal = Vector3.RIGHT
		normal = normal.normalized()
		var now := (a - b).angle_to(wrist - b)
		var wanted := acos(clampf((upper * upper + lower * lower - reach * reach) / (2.0 * upper * lower), -1.0, 1.0))
		var turn := now - wanted
		var tried := b + Basis(normal, turn) * (wrist - b)
		var other := b + Basis(normal, -turn) * (wrist - b)
		if absf((other - a).length() - reach) < absf((tried - a).length() - reach):
			turn = -turn
		_rotate_global(skeleton, _left_forearm, Basis(normal, turn))
		# The shoulder: point the arm at it.
		wrist = skeleton.get_bone_global_pose(_left_wrist).origin
		var from := (wrist - a).normalized()
		var to := (target - a).normalized()
		if from.cross(to).length() > 1e-6:
			_rotate_global(skeleton, _left_arm, Basis(Quaternion(from, to)))

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
	_can_swim = _player.has_animation(SWIM) and _player.has_animation(TREAD)
	for looping: StringName in [IDLE, RUN, RUN_BACK, CROUCH_IDLE, CROUCH_WALK, CROUCH_BACK, SWIM, TREAD]:
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


# Legs: idle <-> run (back or forward) scaled by speed, standing or
# crouched (blended by "posture"). Upper body on top: the raised-rifle firing
# clip (restarted per shot; the crouched one crouched), then reload, throw and
# flinch as one-shots, and the roll over everything.
func _build_tree(upper: Array[NodePath]) -> AnimationNodeBlendTree:
	var root := AnimationNodeBlendTree.new()
	var move := AnimationNodeBlendSpace1D.new()
	move.add_blend_point(_clip(RUN_BACK if _player.has_animation(RUN_BACK) else RUN), -1.0, -1, &"back")
	move.add_blend_point(_clip(IDLE), 0.0, -1, &"idle")
	move.add_blend_point(_clip(RUN), 1.0, -1, &"run")
	root.add_node(&"move", move)
	root.add_node(&"move_speed", AnimationNodeTimeScale.new())
	root.connect_node(&"move_speed", 0, &"move")
	var crouch_move := AnimationNodeBlendSpace1D.new()
	crouch_move.add_blend_point(_clip(CROUCH_BACK, CROUCH_IDLE), -1.0, -1, &"back")
	crouch_move.add_blend_point(_clip(CROUCH_IDLE), 0.0, -1, &"idle")
	crouch_move.add_blend_point(_clip(CROUCH_WALK, CROUCH_IDLE), 1.0, -1, &"walk")
	root.add_node(&"crouch_move", crouch_move)
	root.add_node(&"crouch_speed", AnimationNodeTimeScale.new())
	root.connect_node(&"crouch_speed", 0, &"crouch_move")
	root.add_node(&"posture", AnimationNodeBlend2.new())
	root.connect_node(&"posture", 0, &"move_speed")
	root.connect_node(&"posture", 1, &"crouch_speed")

	root.add_node(&"shoot", _clip(SHOOT))
	root.add_node(&"crouch_shoot", _clip(CROUCH_SHOOT, SHOOT))
	root.add_node(&"shoot_posture", AnimationNodeBlend2.new())
	root.connect_node(&"shoot_posture", 0, &"shoot")
	root.connect_node(&"shoot_posture", 1, &"crouch_shoot")
	root.add_node(&"shoot_seek", AnimationNodeTimeSeek.new())
	root.connect_node(&"shoot_seek", 0, &"shoot_posture")
	var aim := AnimationNodeBlend2.new()
	_filter(aim, upper)
	root.add_node(&"aim", aim)
	root.connect_node(&"aim", 0, &"posture")
	root.connect_node(&"aim", 1, &"shoot_seek")

	root.add_node(&"reload_stand", _clip(RELOAD))
	root.add_node(&"reload_crouch", _clip(CROUCH_RELOAD, RELOAD))
	root.add_node(&"reload_clip", AnimationNodeBlend2.new())
	root.connect_node(&"reload_clip", 0, &"reload_stand")
	root.connect_node(&"reload_clip", 1, &"reload_crouch")
	root.add_node(&"throw_clip", _clip(THROW))
	root.add_node(&"flinch_clip", _clip(HIT))
	var last := &"aim"
	for layer: StringName in [&"reload", &"throw", &"flinch"]:
		var shot := AnimationNodeOneShot.new()
		shot.fadein_time = 0.12
		shot.fadeout_time = 0.2
		_filter(shot, upper)
		root.add_node(layer, shot)
		root.connect_node(layer, 0, last)
		root.connect_node(layer, 1, StringName(str(layer) + "_clip"))
		last = layer
	# The roll: the whole body, played to last as long as the roll does.
	var roll := AnimationNodeOneShot.new()
	roll.fadein_time = 0.08
	roll.fadeout_time = 0.15
	root.add_node(&"roll", roll)
	root.add_node(&"roll_clip", _clip(ROLL))
	root.add_node(&"roll_rate", AnimationNodeTimeScale.new())
	root.connect_node(&"roll_rate", 0, &"roll_clip")
	root.connect_node(&"roll", 0, last)
	root.connect_node(&"roll", 1, &"roll_rate")
	if not _can_swim:
		root.connect_node(&"output", 0, &"roll")
		return root
	# Swimming: the whole body, treading water to stroking by speed.
	var swim_move := AnimationNodeBlendSpace1D.new()
	var tread := AnimationNodeAnimation.new()
	tread.animation = TREAD
	var stroke := AnimationNodeAnimation.new()
	stroke.animation = SWIM
	swim_move.add_blend_point(tread, 0.0, -1, &"tread")
	swim_move.add_blend_point(stroke, 1.0, -1, &"stroke")
	root.add_node(&"swim_move", swim_move)
	root.add_node(&"swim_speed", AnimationNodeTimeScale.new())
	root.connect_node(&"swim_speed", 0, &"swim_move")
	root.add_node(&"swimming", AnimationNodeBlend2.new())
	root.connect_node(&"swimming", 0, &"roll")
	root.connect_node(&"swimming", 1, &"swim_speed")
	root.connect_node(&"output", 0, &"swimming")
	return root


# A clip node; `fallback` (or idle) if the export doesn't have it.
func _clip(animation: StringName, fallback: StringName = IDLE) -> AnimationNodeAnimation:
	var node := AnimationNodeAnimation.new()
	node.animation = animation if _player.has_animation(animation) else fallback
	_clip_nodes.append({"node": node, "role": animation, "fallback": fallback})
	return node


# Holds its weapon as Battlefront's animation set for its kind says
# ("rifle", "pistol", "bazooka"; a WeaponDef's anim_set): its clips where the
# export has them, the rifle's otherwise. A pistol is held raised (its set
# has no idle of its own).
func set_anim_set(anim_set: String) -> void:
	_wanted_set = anim_set
	if _quick <= 0.0:
		_show_anim_set(anim_set)


func _show_anim_set(anim_set: String) -> void:
	if anim_set == _anim_set or _tree == null:
		return
	_anim_set = anim_set
	if _aimer != null:
		_aimer.two_handed = anim_set == "melee_2h"
	_set_grip()
	var clips: Dictionary = ANIM_SETS.get("pistol" if anim_set == "melee" else anim_set, {})
	for entry: Dictionary in _clip_nodes:
		var role: StringName = entry["role"]
		var wanted: StringName = clips.get(role, role)
		if not _player.has_animation(wanted):
			wanted = role if _player.has_animation(role) else entry["fallback"]
		(entry["node"] as AnimationNodeAnimation).animation = wanted
		if role in [IDLE, RUN, RUN_BACK, CROUCH_IDLE, CROUCH_WALK, CROUCH_BACK] and _player.has_animation(wanted):
			_player.get_animation(wanted).loop_mode = Animation.LOOP_LINEAR


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
	var ride := StringName(str(state.get("ride_clip", "")))
	if ride != _ride_clip:
		_set_ride(ride)
	if _ride_clip != &"":
		_seat_hips()
		return
	var rolling := float(state.get("roll", 0.0)) > 0.0
	if rolling and not first_person:
		model.rotation.y = float(state.get("roll_yaw", yaw)) + PI  # it rolls the way it goes
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
	_tree.set("parameters/move_speed/scale", clampf(speed / RUN_SPEED, 0.6, 1.5) if speed > 0.5 else 1.0)
	_tree.set("parameters/crouch_move/blend_position", blend)
	_tree.set("parameters/crouch_speed/scale", clampf(speed / CROUCH_WALK_SPEED, 0.6, 1.5) if speed > 0.5 else 1.0)
	_crouch = move_toward(_crouch, 1.0 if bool(state.get("crouched", false)) else 0.0, CROUCH_BLEND * delta)
	# A slide (a perk's): down at once, gliding on a crouch, legs still and
	# turned the way it goes (no crouch walk).
	if float(state.get("slide", 0.0)) > 0.0:
		_crouch = 1.0
		_tree.set("parameters/crouch_move/blend_position", 0.0)
		_tree.set("parameters/crouch_speed/scale", 1.0)
		if speed > 0.5:
			_leg_yaw = clampf(_signed_angle(facing, flat), -MAX_LEG_TURN, MAX_LEG_TURN)
	for blend_node: String in ["posture", "shoot_posture", "reload_clip"]:
		_tree.set("parameters/%s/blend_amount" % blend_node, _crouch)
	if _can_swim:
		_swim = move_toward(_swim, 1.0 if bool(state.get("swimming", false)) and not first_person else 0.0,
				SWIM_BLEND * delta)
		_tree.set("parameters/swimming/blend_amount", _swim)
		_tree.set("parameters/swim_move/blend_position", clampf(speed / 1.5, 0.0, 1.0))
		_tree.set("parameters/swim_speed/scale", clampf(speed / SWIM_SPEED, 0.6, 1.5) if speed > 0.5 else 1.0)
		if _swim > 0.0:
			_leg_yaw = 0.0
			# Swimming the way it goes, whichever way it looks.
			if speed > 0.5 and _swim > 0.5:
				model.rotation.y = atan2(-flat.x, -flat.y) + PI

	# A roll: Battlefront's dive roll in third person; in first person, the
	# gun dips (the clip would tumble the camera).
	if rolling and not _rolling:
		if first_person:
			_throwing = maxf(_throwing, ROLL_TIME)
		elif _player.has_animation(ROLL):
			_tree.set("parameters/roll_rate/scale", _player.get_animation(ROLL).length / ROLL_TIME)
			_tree.set("parameters/roll/request", AnimationNodeOneShot.ONE_SHOT_REQUEST_FIRE)
	_rolling = rolling
	if rolling:
		_leg_yaw = 0.0

	_since_shot += delta
	_swing = maxf(0.0, _swing - delta)
	if first_person:
		_tree.set("parameters/shoot_seek/seek_request", 0.0)  # the raised pose, without the recoil clip
		_first_person_motion(state, speed, delta)
	_placing = maxf(0.0, _placing - delta)
	if _quick > 0.0:
		_quick -= delta
		if _quick <= 0.0:
			_show_weapon(_wanted_weapon)
			_show_anim_set(_wanted_set)
	var target := 1.0 if _since_shot < AIM_HOLD or first_person or _anim_set in ["pistol", "melee", "melee_2h"] else 0.0
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
		_aimer.melee_block = bool(state.get("blocking", false))
		_aimer.melee_swing = clampf(_swing / _swing_time(), 0.0, 1.0) if _anim_set in ["melee", "melee_2h"] else 0.0
		_aimer.place = 1.0 - _placing / PLACE_TIME if _placing > 0.0 and not first_person else 0.0


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


# Riding: Battlefront's rider pose (a vehicle's Pilot9Pose or PilotAnimation),
# held at its first frame instead of the legs and arms' tree. Its clip lifts
# the hips from where it's put (the vehicle's hardpoint) to the seat. An
# unknown clip (an older export) leaves it crouched.
func _set_ride(clip: StringName) -> void:
	if clip != &"" and _player != null and _player.has_animation(clip):
		if _tree != null:
			_tree.active = false
		_player.play(clip)
		_player.seek(0.0, true)
		_player.pause()
		_ride_clip = clip
		if _aimer != null:
			_aimer.pitch = 0.0
			_aimer.leg_yaw = 0.0
			_aimer.melee_block = false
			_aimer.melee_swing = 0.0
			_aimer.place = 0.0
		return
	if _ride_clip != &"":
		_player.stop()
		if _tree != null:
			_tree.active = true
	_ride_clip = &""


# The ride pose's lift of the hips to the seat is in the model's own size,
# so a body drawn smaller (a short character: model.scale under 1) sat that
# much lower, into its saddle, and a taller one over it. Raised (or lowered)
# by the difference, its hips are where the pose puts a full-sized body's.
func _seat_hips() -> void:
	if _skeleton == null or is_equal_approx(model.scale.y, 1.0):
		return
	var hips := _skeleton.find_bone("root")
	if hips < 0:
		hips = _skeleton.find_bone("bone_pelvis")
	if hips < 0:
		return
	var in_model := model.global_transform.affine_inverse() * (_skeleton.global_transform * _skeleton.get_bone_global_pose(hips).origin)
	model.position.y += in_model.y * (1.0 - model.scale.y)


func _pose_simple(state: Dictionary) -> void:
	if _player == null:
		return
	var velocity: Vector3 = state["velocity"]
	var speed := Vector2(velocity.x, velocity.z).length()
	var animation := RUN if speed > 0.5 else IDLE
	if _player.current_animation != animation and _player.has_animation(animation):
		_player.play(animation, 0.15)
	_player.speed_scale = clampf(speed / RUN_SPEED, 0.6, 1.5) if speed > 0.5 else 1.0


# A shot: raise the rifle (if it isn't) and play the recoil from the start.
func fire() -> void:
	_since_shot = 0.0
	_kick = 1.0
	if _anim_set in ["melee", "melee_2h"]:
		_swing = _swing_time()
	if _tree != null:
		_tree.set("parameters/shoot_seek/seek_request", 0.0)
		# Firing out of a roll (a perk's: Roll Shot, Teras Kasi Master): the
		# roll gives way to the shot, crouched, for the rest of it.
		if _rolling and bool(_tree.get("parameters/roll/active")):
			_tree.set("parameters/roll/request", AnimationNodeOneShot.ONE_SHOT_REQUEST_FADE_OUT)


func _swing_time() -> float:
	return SWING_TIME_2H if _anim_set == "melee_2h" else SWING_TIME


# A throw from the tactical slot, as its kind (an item's archetype) is: a
# grenade thrown; a mine or trap ("placed") set down low, bending to it; a
# tactical pistol ("utility": the flare, the tangle) drawn and fired, its
# model (`held_model`) in hand a moment instead of the held weapon.
func throw(kind: String = "", held_model: String = "") -> void:
	if kind == "utility":
		_quick = QUICK_SHOT
		_show_weapon(held_model)
		_show_anim_set("pistol")
		fire()
		return
	if kind == "placed":
		_placing = PLACE_TIME
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
static var _weapon_models := {}  # model path -> template (null: none there)
var _weapon_path := "<baked>"    # what it holds: "" or "<baked>" the model's own (the E-11 it was exported with)
var _weapon: Node3D              # the weapon it holds in place of that, if any


# Holds the weapon whose model is `path` (game/imported_assets/-relative, as
# a WeaponDef's): on the right hand's hold point, where the model's own (the
# E-11 every character was exported holding) was, hidden for it. "" (or a
# model not exported): that one.
func set_weapon(path: String) -> void:
	_wanted_weapon = path
	if _quick <= 0.0:
		_show_weapon(path)


func _show_weapon(path: String) -> void:
	if path == _weapon_path:
		return
	_weapon_path = path
	var baked := _baked_weapon()
	if _weapon != null:
		_weapon.queue_free()
		_weapon = null
	var template: Node3D = null
	if path != "":
		var full := "res://imported_assets/" + path
		if not _weapon_models.has(full):
			_weapon_models[full] = null
			if FileAccess.file_exists(full):
				var document := GLTFDocument.new()
				var state := GLTFState.new()
				if document.append_from_file(ProjectSettings.globalize_path(full), state) == OK:
					_weapon_models[full] = document.generate_scene(state)
		template = _weapon_models[full]
	if template == null or baked == null:
		if baked != null:
			baked.visible = true
		_muzzle = model.find_child("hp_muzzle", true, false) as Node3D
	else:
		baked.visible = false
		_weapon = template.duplicate() as Node3D
		baked.get_parent().add_child(_weapon)
		_muzzle = _weapon.find_child("hp_muzzle", true, false) as Node3D
	_set_grip()


# Where the left hand holds a rifle: along the barrel, from the grip (the
# weapon's origin, in the right hand) part way to the muzzle, a little under
# it (ASSUMPTION: 55% of the way, 12 to 42 cm out, at 60% of the muzzle's height, by eye: the E-11's falls
# where Battlefront's rifle clips put the hand). None for a pistol's or a
# launcher's set.
func _set_grip() -> void:
	if _aimer == null:
		return
	if _anim_set == "melee_2h":
		_aimer.grip = Vector3(0.0, 0.0, GRIP_2H)  # up the haft (melee models lie along +Z from the hand)
		return
	if _anim_set != "rifle" or _muzzle == null:
		_aimer.grip = null
		return
	var reach := clampf(_muzzle.position.z * 0.55, 0.12, 0.42)
	_aimer.grip = Vector3(0.0, _muzzle.position.y * 0.6, reach)


# The model's own weapon: the mesh on its right hand's hold point.
func _baked_weapon() -> Node3D:
	for attachment: Node in model.find_children("*", "BoneAttachment3D", true, false):
		if (attachment as BoneAttachment3D).bone_name == "hold_r":
			for child: Node in attachment.get_children():
				if child is MeshInstance3D and str(child.name).begins_with("wp_"):
					return child as Node3D
	return null


func muzzle() -> Vector3:
	if _muzzle != null and model.visible:
		return _muzzle.global_position
	return model.global_position + Vector3.UP * 1.4


static func _signed_angle(from: Vector2, to: Vector2) -> float:
	# Positive turns left (counterclockwise seen from above, as Godot's yaw).
	return -from.angle_to(to)
