# The specialist abilities' icons on the ability bar, drawn here in the
# world's place symbols' style (PlaceSymbols: white, a translucent body with
# a bright outline; the bar tints them): Battlefront II has pictures for few
# of them. Made once each, as they're first shown.
#
#   var texture := AbilityIcons.texture("ent_perform")   # null for an ability it has none for
extends RefCounted

const PlaceSymbols = preload("res://rendering/place_symbols.gd")
const SIZE := PlaceSymbols.SIZE
const C := Vector2(48, 48)

static var _cache := {}


static func texture(ability: String) -> Texture2D:
	if _cache.has(ability):
		return _cache[ability]
	var image := Image.create(SIZE, SIZE, false, Image.FORMAT_RGBA8)
	image.fill(Color(1, 1, 1, 0))
	if not _draw(image, ability):
		_cache[ability] = null
		return null
	image.generate_mipmaps()
	var made := ImageTexture.create_from_image(image)
	_cache[ability] = made
	return made


static func _draw(image: Image, ability: String) -> bool:
	match ability:
		"bh_seeker_probe": _probe(image, false)
		"bh_recon_probe": _probe(image, true)
		"bh_hvt", "bh_hvt_world": _crosshair(image)
		"bh_wrist_rocket": _rocket(image)
		"bh_toxin_dart": _dart(image, "drop")
		"smg_flashbang": _burst(image)
		"smg_smoke": _cloud(image)
		"smg_emp": _bolt(image, C, 1.0)
		"smg_getaway": _jump(image)
		"rg_bolas": _bolas(image)
		"sl_rally": _flag(image)
		"sl_supply": _crate(image)
		"sl_strike": _strike(image)
		"doc_heal_dart": _dart(image, "plus")
		"doc_cleanse": _drop(image, C + Vector2(0, 4), 1.0)
		"rg_track": _tracks(image)
		"rg_motion_sensor": _waves(image, Vector2(48, 78), true)
		"rg_cloak": _eye(image, true)
		"rg_trip_trap": _trap(image)
		"ent_perform": _note(image)
		"ent_flourish": _sparkle(image, C, 40.0)
		"ent_holo_emitter": _projector(image)
		"pl_field_repair": _wrench(image)
		"pl_evasive": _swerve(image)
		"pl_afterburner": _afterburner(image)
		"pl_flare": _flares(image)
		"bio_sample": _helix(image)
		"ch_tame": _paw(image, false)
		"ch_frenzy": _claws(image)
		"ch_mend": _paw(image, true)
		"do_remote_repair": _remote_repair(image)
		"do_overcharge": _overcharge(image)
		_: return false
	return true


# --- the helpers' shorthands ---

static func _paint(image: Image, shape: Callable, outline := true, inside := PlaceSymbols.BODY) -> void:
	PlaceSymbols._paint(image, shape, outline, inside)


static func _solid(image: Image, shape: Callable) -> void:
	PlaceSymbols._paint(image, shape, false, 1.0)


static func _line(image: Image, a: Vector2, b: Vector2, width := PlaceSymbols.STROKE) -> void:
	PlaceSymbols._line(image, a, b, width)


static func _box(p: Vector2, center: Vector2, half: Vector2, round_by := 0.0) -> float:
	return PlaceSymbols._box(p, center, half, round_by)


static func _circle(p: Vector2, center: Vector2, radius: float) -> float:
	return PlaceSymbols._circle(p, center, radius)


static func _segment(p: Vector2, a: Vector2, b: Vector2) -> float:
	return PlaceSymbols._segment(p, a, b)


static func _ring(image: Image, center: Vector2, radius: float, width := 3.5) -> void:
	_solid(image, func(p: Vector2) -> float: return absf(_circle(p, center, radius)) - width * 0.5)


# An arc of a ring, `from` to `to` (radians, 0 along +x, turning down the image).
static func _arc(image: Image, center: Vector2, radius: float, from: float, to: float, width := 4.0) -> void:
	_solid(image, func(p: Vector2) -> float:
		var d := p - center
		var a := atan2(d.y, d.x)
		var mid := (from + to) * 0.5
		var half := (to - from) * 0.5
		if absf(wrapf(a - mid, -PI, PI)) <= half:
			return absf(d.length() - radius) - width * 0.5
		var e1 := center + Vector2(cos(from), sin(from)) * radius
		var e2 := center + Vector2(cos(to), sin(to)) * radius
		return minf(p.distance_to(e1), p.distance_to(e2)) - width * 0.5)


static func _plus(image: Image, center: Vector2, arm: float, width: float) -> void:
	_solid(image, func(p: Vector2) -> float:
		return minf(_box(p, center, Vector2(width, arm), 1.5), _box(p, center, Vector2(arm, width), 1.5)))


# --- the icons ---

static func _probe(image: Image, eye: bool) -> void:
	_line(image, Vector2(40, 60), Vector2(28, 86))
	_line(image, Vector2(48, 62), Vector2(48, 90))
	_line(image, Vector2(56, 60), Vector2(68, 86))
	_line(image, Vector2(56, 30), Vector2(66, 8), 3.0)
	_paint(image, func(p: Vector2) -> float: return _circle(p, Vector2(48, 44), 20.0))
	if eye:
		_waves(image, Vector2(48, 44), false)
	else:
		_solid(image, func(p: Vector2) -> float: return _circle(p, Vector2(48, 44), 6.0))


static func _bolas(image: Image) -> void:
	_arc(image, Vector2(48, 70), 36.0, PI * 1.18, PI * 1.82, 4.0)
	_line(image, Vector2(48, 36), Vector2(48, 70), 4.0)
	for at: Vector2 in [Vector2(20, 48), Vector2(76, 48), Vector2(48, 78)]:
		_paint(image, func(p: Vector2) -> float: return _circle(p, at, 11.0))


static func _net(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float: return _circle(p, C, 38.0), true, 0.12)
	for k in range(-2, 3):
		var o := k * 14.0
		var h := sqrt(maxf(0.0, 38.0 * 38.0 - o * o))
		_line(image, C + Vector2(o, -h), C + Vector2(o, h), 3.0)
		_line(image, C + Vector2(-h, o), C + Vector2(h, o), 3.0)


static func _crosshair(image: Image) -> void:
	_ring(image, C, 30.0, 4.5)
	for d: Vector2 in [Vector2.UP, Vector2.DOWN, Vector2.LEFT, Vector2.RIGHT]:
		_line(image, C + d * 20.0, C + d * 44.0, 5.0)
	_solid(image, func(p: Vector2) -> float: return _circle(p, C, 6.0))


static func _rocket(image: Image) -> void:
	var turn := Transform2D(PI / 4.0, Vector2.ZERO)
	_paint(image, func(p: Vector2) -> float:
		var q := turn * (p - C) + C  # upright
		var body := minf(_box(q, Vector2(48, 52), Vector2(10, 22), 4.0), _segment(q, Vector2(48, 18), Vector2(48, 36)) - 9.0)
		var fins := maxf(_box(q, Vector2(48, 72), Vector2(20, 6), 2.0), -(q.y - 66.0))
		return minf(body, fins))
	_solid(image, func(p: Vector2) -> float:
		var q := turn * (p - C) + C
		return _segment(q, Vector2(48, 82), Vector2(48, 92)) - 4.0)


static func _jump(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float:
		var head := maxf(maxf(-(p.y - 10.0), p.y - 46.0), absf(p.x - 48.0) - (p.y - 10.0) * 0.8)
		return minf(head, _box(p, Vector2(48, 56), Vector2(10, 14), 2.0)))
	for x: float in [38.0, 58.0]:
		_solid(image, func(p: Vector2) -> float:
			var tip := Vector2(x, 92)
			return maxf(_segment(p, Vector2(x, 74), tip) - lerpf(5.0, 1.0, clampf((p.y - 74.0) / 18.0, 0.0, 1.0)), -1e9))


static func _burst(image: Image) -> void:
	for i in 8:
		var a := TAU * i / 8.0
		var d := Vector2(cos(a), sin(a))
		_line(image, C + d * 22.0, C + d * (42.0 if i % 2 == 0 else 34.0), 5.0)
	_paint(image, func(p: Vector2) -> float: return _circle(p, C, 15.0), true, 0.9)


static func _cloud(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float:
		return minf(minf(_circle(p, Vector2(34, 56), 18.0), _circle(p, Vector2(58, 50), 24.0)),
				minf(_circle(p, Vector2(70, 64), 15.0), _box(p, Vector2(50, 70), Vector2(28, 10), 8.0))))
	_arc(image, Vector2(58, 50), 14.0, PI * 1.1, PI * 1.6, 3.0)


static func _bolt(image: Image, center: Vector2, scale: float) -> void:
	var pts := PackedVector2Array([Vector2(56, 6), Vector2(26, 54), Vector2(46, 54), Vector2(38, 90), Vector2(70, 40),
			Vector2(50, 40), Vector2(62, 6)])
	for i in pts.size():
		pts[i] = center + (pts[i] - C) * scale
	_paint(image, func(p: Vector2) -> float: return _polygon(p, pts))


# A polygon's signed distance (negative inside).
static func _polygon(p: Vector2, pts: PackedVector2Array) -> float:
	var d := INF
	var inside := false
	var j := pts.size() - 1
	for i in pts.size():
		var a := pts[i]
		var b := pts[j]
		var e := b - a
		var w := p - a
		var t := clampf(w.dot(e) / maxf(e.length_squared(), 0.0001), 0.0, 1.0)
		d = minf(d, (w - e * t).length())
		if (a.y > p.y) != (b.y > p.y) and p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x:
			inside = not inside
		j = i
	return -d if inside else d


static func _flag(image: Image) -> void:
	_line(image, Vector2(28, 10), Vector2(28, 90), 5.0)
	_paint(image, func(p: Vector2) -> float:
		return _polygon(p, PackedVector2Array([Vector2(30, 12), Vector2(80, 22), Vector2(64, 34), Vector2(80, 48), Vector2(30, 48)])))


static func _pin(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float:
		var top := _circle(p, Vector2(48, 38), 24.0)
		var point := _polygon(p, PackedVector2Array([Vector2(28, 50), Vector2(68, 50), Vector2(48, 90)]))
		return maxf(minf(top, point), -_circle(p, Vector2(48, 38), 9.0)))


static func _beacon(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float:
		return _polygon(p, PackedVector2Array([Vector2(40, 46), Vector2(56, 46), Vector2(66, 90), Vector2(30, 90)])))
	_solid(image, func(p: Vector2) -> float: return _circle(p, Vector2(48, 38), 7.0))
	_arc(image, Vector2(48, 38), 18.0, PI * 1.2, PI * 1.8, 4.0)
	_arc(image, Vector2(48, 38), 30.0, PI * 1.22, PI * 1.78, 4.0)


static func _crate(image: Image) -> void:
	_arc(image, Vector2(48, 44), 34.0, PI * 1.05, PI * 1.95, 4.0)
	_line(image, Vector2(18, 36), Vector2(38, 56), 3.0)
	_line(image, Vector2(78, 36), Vector2(58, 56), 3.0)
	_paint(image, func(p: Vector2) -> float: return _box(p, Vector2(48, 72), Vector2(18, 16), 2.0))
	_line(image, Vector2(32, 58), Vector2(64, 86), 3.0)
	_line(image, Vector2(64, 58), Vector2(32, 86), 3.0)


static func _strike(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float:
		var shaft := _box(p, Vector2(48, 26), Vector2(7, 20), 1.0)
		var head := _polygon(p, PackedVector2Array([Vector2(28, 44), Vector2(68, 44), Vector2(48, 66)]))
		return minf(shaft, head))
	_arc(image, Vector2(48, 92), 26.0, PI * 1.1, PI * 1.9, 4.0)
	_arc(image, Vector2(48, 92), 14.0, PI * 1.15, PI * 1.85, 4.0)


static func _medkit(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float:
		return minf(_box(p, Vector2(48, 56), Vector2(36, 28), 6.0), maxf(_box(p, Vector2(48, 26), Vector2(14, 8), 3.0),
				-_box(p, Vector2(48, 28), Vector2(8, 4), 1.0))))
	_plus(image, Vector2(48, 56), 17.0, 6.0)


static func _drop(image: Image, center: Vector2, scale: float) -> void:
	_paint(image, func(p: Vector2) -> float:
		var q := (p - center) / scale
		var bulb := _circle(q, Vector2(0, 12), 24.0)
		var top := _polygon(q, PackedVector2Array([Vector2(0, -38), Vector2(-21, 0), Vector2(21, 0)]))
		return minf(bulb, top) * scale)


static func _syringe(image: Image) -> void:
	var turn := Transform2D(-PI / 4.0, Vector2.ZERO)
	_paint(image, func(p: Vector2) -> float:
		var q := turn * (p - C) + C
		var barrel := _box(q, Vector2(48, 50), Vector2(10, 22), 2.0)
		var grip := _box(q, Vector2(48, 78), Vector2(18, 3.5), 1.0)
		var plunger := _box(q, Vector2(48, 86), Vector2(3.5, 8), 1.0)
		return minf(barrel, minf(grip, plunger)))
	_solid(image, func(p: Vector2) -> float:
		var q := turn * (p - C) + C
		return _segment(q, Vector2(48, 6), Vector2(48, 28)) - 2.0)


static func _tank(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float: return _box(p, Vector2(48, 50), Vector2(22, 38), 14.0))
	_line(image, Vector2(26, 26), Vector2(70, 26), 3.0)
	_line(image, Vector2(26, 76), Vector2(70, 76), 3.0)
	for b: Vector3 in [Vector3(40, 60, 4.5), Vector3(54, 48, 3.5), Vector3(46, 38, 3.0)]:
		_solid(image, func(p: Vector2) -> float: return _circle(p, Vector2(b.x, b.y), b.z))


static func _heart(image: Image, pulse: bool) -> void:
	_paint(image, func(p: Vector2) -> float:
		var lobes := minf(_circle(p, Vector2(33, 36), 19.0), _circle(p, Vector2(63, 36), 19.0))
		var point := _polygon(p, PackedVector2Array([Vector2(15, 42), Vector2(81, 42), Vector2(48, 84)]))
		return minf(lobes, point))
	if pulse:
		var pts := [Vector2(10, 52), Vector2(32, 52), Vector2(40, 36), Vector2(50, 70), Vector2(58, 46), Vector2(64, 52),
				Vector2(86, 52)]
		for i in pts.size() - 1:
			_line(image, pts[i], pts[i + 1], 4.0)


static func _triage(image: Image) -> void:
	_ring(image, C, 40.0, 3.0)
	for at: Vector2 in [Vector2(48, 30), Vector2(32, 60), Vector2(64, 60)]:
		_paint(image, func(p: Vector2) -> float:
			return minf(_box(p, at, Vector2(4.5, 12), 1.5), _box(p, at, Vector2(12, 4.5), 1.5)), true, 0.8)


static func _dart(image: Image, mark: String) -> void:
	var turn := Transform2D(-PI / 4.0, Vector2.ZERO)
	_paint(image, func(p: Vector2) -> float:
		var q := turn * (p - C) + C
		var shaft := _box(q, Vector2(48, 46), Vector2(4.5, 30), 1.0)
		var tip := _polygon(q, PackedVector2Array([Vector2(48, 4), Vector2(41, 18), Vector2(55, 18)]))
		var flights := _polygon(q, PackedVector2Array([Vector2(48, 64), Vector2(62, 84), Vector2(48, 78), Vector2(34, 84)]))
		return minf(minf(shaft, tip), flights))
	match mark:
		"plus":
			_plus(image, Vector2(24, 26), 10.0, 3.5)
		"drop":
			_drop(image, Vector2(24, 28), 0.42)
		"moon":
			_solid(image, func(p: Vector2) -> float:
				return maxf(_circle(p, Vector2(24, 26), 13.0), -_circle(p, Vector2(31, 21), 11.0)))


static func _grenade(image: Image, mark: String) -> void:
	_paint(image, func(p: Vector2) -> float:
		return minf(_circle(p, Vector2(48, 56), 30.0), _box(p, Vector2(48, 22), Vector2(10, 8), 2.0)))
	_line(image, Vector2(58, 18), Vector2(76, 10), 4.0)
	if mark == "plus":
		_plus(image, Vector2(48, 56), 16.0, 5.5)
	else:
		for i in 3:
			var a := TAU * i / 3.0 - PI * 0.5
			var at := Vector2(48, 56) + Vector2(cos(a), sin(a)) * 11.0
			_ring(image, at, 8.0, 3.5)
		_solid(image, func(p: Vector2) -> float: return _circle(p, Vector2(48, 56), 3.5))


static func _warning(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float:
		return _polygon(p, PackedVector2Array([Vector2(48, 8), Vector2(90, 86), Vector2(6, 86)])))
	_solid(image, func(p: Vector2) -> float: return _box(p, Vector2(48, 50), Vector2(4.5, 16), 2.0))
	_solid(image, func(p: Vector2) -> float: return _circle(p, Vector2(48, 75), 5.0))


static func _tracks(image: Image) -> void:
	for f: Vector3 in [Vector3(34, 64, -0.2), Vector3(62, 34, 0.2)]:
		var at := Vector2(f.x, f.y)
		_paint(image, func(p: Vector2) -> float:
			var q := (p - at).rotated(-f.z)
			return PlaceSymbols._ellipse(q, Vector2(0, 6), Vector2(10, 15)), true, 0.6)
		for t in 3:
			var toe := at + Vector2(-9 + t * 9, -16).rotated(f.z)
			_solid(image, func(p: Vector2) -> float: return _circle(p, toe, 3.8))


static func _waves(image: Image, center: Vector2, with_dot: bool) -> void:
	if with_dot:
		_solid(image, func(p: Vector2) -> float: return _circle(p, center, 7.0))
	for r: float in ([16.0, 30.0, 44.0] if with_dot else [10.0, 15.0]):
		_arc(image, center, r, PI * 1.22, PI * 1.78, 4.5 if with_dot else 3.0)


static func _eye(image: Image, struck: bool) -> void:
	_paint(image, func(p: Vector2) -> float:
		return maxf(_circle(p, Vector2(48, 90), 58.0), _circle(p, Vector2(48, 6), 58.0)))
	_solid(image, func(p: Vector2) -> float: return _circle(p, C, 11.0))
	if struck:
		_line(image, Vector2(16, 82), Vector2(80, 14), 6.0)


static func _trap(image: Image) -> void:
	_arc(image, Vector2(48, 50), 34.0, 0.0, PI, 5.0)
	_line(image, Vector2(10, 50), Vector2(86, 50), 5.0)
	for i in 5:
		var x := 22.0 + i * 13.0
		_solid(image, func(p: Vector2) -> float:
			return _polygon(p, PackedVector2Array([Vector2(x - 5, 50), Vector2(x + 5, 50), Vector2(x, 34)])))
	_line(image, Vector2(48, 84), Vector2(48, 92), 4.0)


static func _tent(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float:
		return _polygon(p, PackedVector2Array([Vector2(48, 14), Vector2(90, 82), Vector2(6, 82)])))
	_solid(image, func(p: Vector2) -> float:
		return _polygon(p, PackedVector2Array([Vector2(48, 44), Vector2(62, 82), Vector2(34, 82)])))
	_line(image, Vector2(48, 14), Vector2(48, 4), 3.5)


static func _note(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float:
		var heads := minf(PlaceSymbols._ellipse(p, Vector2(30, 74), Vector2(13, 10)),
				PlaceSymbols._ellipse(p, Vector2(70, 64), Vector2(13, 10)))
		var stems := minf(_box(p, Vector2(41, 46), Vector2(3.5, 28), 1.0), _box(p, Vector2(81, 36), Vector2(3.5, 28), 1.0))
		var beam := _polygon(p, PackedVector2Array([Vector2(38, 18), Vector2(85, 8), Vector2(85, 20), Vector2(38, 30)]))
		return minf(minf(heads, stems), beam))


static func _sparkle(image: Image, center: Vector2, radius: float) -> void:
	_paint(image, func(p: Vector2) -> float:
		var d := (p - center).abs()
		# A four-pointed star: concave sides between its points.
		var r := radius
		return minf(_segment(d, Vector2(0, 0), Vector2(0, r)) - r * 0.06 - maxf(0.0, r * 0.26 - d.y * 0.26),
				_segment(d, Vector2(0, 0), Vector2(r, 0)) - r * 0.06 - maxf(0.0, r * 0.26 - d.x * 0.26)))


static func _projector(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float:
		return _polygon(p, PackedVector2Array([Vector2(44, 76), Vector2(52, 76), Vector2(84, 14), Vector2(12, 14)])), true, 0.12)
	_paint(image, func(p: Vector2) -> float: return _box(p, Vector2(48, 84), Vector2(18, 6), 2.0))
	_solid(image, func(p: Vector2) -> float:
		return minf(_circle(p, Vector2(48, 30), 7.0), _box(p, Vector2(48, 50), Vector2(9, 12), 3.0)))


static func _wrench(image: Image) -> void:
	PlaceSymbols._wrench(image)


static func _swerve(image: Image) -> void:
	_arc(image, Vector2(30, 52), 24.0, -PI * 0.5, PI * 0.5, 6.0)
	_arc(image, Vector2(66, 52), 12.0, PI * 0.5, PI * 1.5, 6.0)
	_solid(image, func(p: Vector2) -> float:
		return _polygon(p, PackedVector2Array([Vector2(66, 26), Vector2(86, 40), Vector2(66, 52)])))
	_line(image, Vector2(30, 76), Vector2(10, 76), 6.0)


static func _afterburner(image: Image) -> void:
	# Three chevrons, flat out to the right.
	for i in 3:
		var x := 22.0 + i * 20.0
		_line(image, Vector2(x, 26), Vector2(x + 18, 48), 6.0)
		_line(image, Vector2(x + 18, 48), Vector2(x, 70), 6.0)


static func _flares(image: Image) -> void:
	# Flares fanning down and away from a ship's tail, each a bright dot.
	for i in 3:
		var angle := PI * (0.3 + 0.2 * i)
		var tip := Vector2(48, 22) + Vector2(cos(angle), sin(angle)) * 52.0
		_line(image, Vector2(48, 22), tip, 3.5)
		_solid(image, func(p: Vector2) -> float: return _circle(p, tip, 8.0))


static func _helix(image: Image) -> void:
	for strand in 2:
		var phase := PI * strand
		for i in 24:
			var y0 := 6.0 + i * 3.6
			var y1 := y0 + 3.6
			_line(image, Vector2(48 + sin(y0 * 0.075 + phase) * 22.0, y0), Vector2(48 + sin(y1 * 0.075 + phase) * 22.0, y1), 4.5)
	for k in 6:
		var y := 12.0 + k * 14.0
		var a := sin(y * 0.075) * 22.0
		_line(image, Vector2(48 - a, y), Vector2(48 + a, y), 2.5)


static func _paw(image: Image, mend: bool) -> void:
	_paint(image, func(p: Vector2) -> float: return PlaceSymbols._ellipse(p, Vector2(48, 64), Vector2(20, 16)))
	for t: Vector2 in [Vector2(24, 40), Vector2(39, 28), Vector2(57, 28), Vector2(72, 40)]:
		_paint(image, func(p: Vector2) -> float: return PlaceSymbols._ellipse(p, t, Vector2(7, 9)))
	if mend:
		_plus(image, Vector2(48, 64), 9.0, 3.0)


static func _claws(image: Image) -> void:
	for i in 3:
		var x := 26.0 + i * 20.0
		_solid(image, func(p: Vector2) -> float:
			var t := clampf((p.y - 10.0) / 76.0, 0.0, 1.0)
			return _segment(p, Vector2(x + 10, 10), Vector2(x - 10, 86)) - lerpf(1.5, 6.0, sin(t * PI)))


static func _remote_repair(image: Image) -> void:
	var small := Image.create(SIZE, SIZE, false, Image.FORMAT_RGBA8)
	small.fill(Color(1, 1, 1, 0))
	PlaceSymbols._wrench(small)
	small.resize(64, 64, Image.INTERPOLATE_BILINEAR)
	image.blend_rect(small, Rect2i(0, 0, 64, 64), Vector2i(0, 32))
	_arc(image, Vector2(70, 26), 10.0, -PI * 0.75, PI * 0.25, 3.5)
	_arc(image, Vector2(70, 26), 20.0, -PI * 0.75, PI * 0.25, 3.5)


static func _overcharge(image: Image) -> void:
	_ring(image, C, 40.0, 4.0)
	_bolt(image, C, 0.8)
