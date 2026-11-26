# The flat symbols floating over the world's places, for the ones Battlefront
# has no picture of: an exclamation mark (a quest giver), a stack of coins
# (somewhere to sell, the bank), a chest (a shop), a star (a trainer), a
# cross (a clinic), a ringed cross (a cloning facility), a wrench (crafting:
# Battlefront's own, hud_active_repair_icon, is too small to stay sharp), a
# shuttle (travel) and a starfighter (a space battle terminal). Drawn here in
# white (the hologram tints them): a translucent body with a bright outline,
# as its own hologram icons are.
#
#   var texture := PlaceSymbols.texture("coins")   # null for a name it hasn't
extends RefCounted

const SIZE := 96
const NAMES := ["quest", "coins", "chest", "star", "cross", "clone", "wrench", "shuttle", "fighter"]
const BODY := 0.32      # how solid a symbol's inside is (its outline is 1.0)
const STROKE := 4.5     # outline width, pixels

static var _cache := {}


static func texture(name: String) -> Texture2D:
	if _cache.has(name):
		return _cache[name]
	if not NAMES.has(name):
		return null
	var image := Image.create(SIZE, SIZE, false, Image.FORMAT_RGBA8)
	image.fill(Color(1, 1, 1, 0))
	match name:
		"quest":
			_quest(image)
		"coins":
			_coins(image)
		"chest":
			_chest(image)
		"star":
			_star(image)
		"cross":
			_cross(image)
		"clone":
			_clone(image)
		"wrench":
			_wrench(image)
		"shuttle":
			_shuttle(image)
		"fighter":
			_fighter(image)
	image.generate_mipmaps()
	var made := ImageTexture.create_from_image(image)
	_cache[name] = made
	return made


# --- signed distances (negative inside), in pixels ---

static func _box(p: Vector2, center: Vector2, half: Vector2, round_by := 0.0) -> float:
	var q := (p - center).abs() - half + Vector2.ONE * round_by
	return Vector2(maxf(q.x, 0.0), maxf(q.y, 0.0)).length() + minf(maxf(q.x, q.y), 0.0) - round_by


static func _circle(p: Vector2, center: Vector2, radius: float) -> float:
	return p.distance_to(center) - radius


# Near enough: an ellipse's distance, by scaling space.
static func _ellipse(p: Vector2, center: Vector2, radii: Vector2) -> float:
	return (((p - center) / radii).length() - 1.0) * minf(radii.x, radii.y)


static func _segment(p: Vector2, a: Vector2, b: Vector2) -> float:
	var ab := b - a
	var t := clampf((p - a).dot(ab) / maxf(ab.length_squared(), 0.0001), 0.0, 1.0)
	return p.distance_to(a + ab * t)


# Paints a shape over what's there: its body (`BODY` opaque; an `inside` of 1
# for a solid mark), then its outline when `outline` is set. Painting in
# order covers what's behind, so stacked shapes (coins) show their edges.
static func _paint(image: Image, shape: Callable, outline := true, inside := BODY) -> void:
	for y in SIZE:
		for x in SIZE:
			var d: float = shape.call(Vector2(x + 0.5, y + 0.5))
			var cover := clampf(0.5 - d, 0.0, 1.0)
			if cover <= 0.0 and (not outline or d > STROKE * 0.5 + 0.5):
				continue
			var alpha := image.get_pixel(x, y).a
			if cover > 0.0:
				alpha = lerpf(alpha, inside, cover)
			if outline:
				alpha = maxf(alpha, clampf(STROKE * 0.5 + 0.5 - absf(d), 0.0, 1.0))
			image.set_pixel(x, y, Color(1, 1, 1, alpha))


# A line, outline-bright.
static func _line(image: Image, a: Vector2, b: Vector2, width := STROKE) -> void:
	_paint(image, func(p: Vector2) -> float: return _segment(p, a, b) - width * 0.5, false, 1.0)


static func _quest(image: Image) -> void:
	# The bar tapers down to the dot: a rounded box over a circle.
	_paint(image, func(p: Vector2) -> float:
		return minf(_box(p, Vector2(48, 36), Vector2(11, 27), 9.0), _circle(p, Vector2(48, 79), 10.5)),
			false, 1.0)


static func _coins(image: Image) -> void:
	# Three coins stacked, seen from a little above: each an ellipse over its edge.
	var rx := 36.0
	var ry := 12.0
	var thick := 11.0
	for i in 3:
		var y := 72.0 - i * 20.0
		var top := Vector2(48, y)
		_paint(image, func(p: Vector2) -> float:
			return minf(minf(_ellipse(p, top, Vector2(rx, ry)), _ellipse(p, top + Vector2(0, thick), Vector2(rx, ry))),
					_box(p, top + Vector2(0, thick * 0.5), Vector2(rx, thick * 0.5))))
	# The top coin's rim.
	var face := Vector2(48, 32)
	_paint(image, func(p: Vector2) -> float: return _ellipse(p, face, Vector2(rx - 12.0, ry - 5.0)), true, 0.0)


static func _star(image: Image) -> void:
	# Five points, the lines between them straight: its radius by how far round the point it is.
	var center := Vector2(48, 52)
	_paint(image, func(p: Vector2) -> float:
		var d := p - center
		var a := fposmod(atan2(d.y, d.x) + PI * 0.5, TAU / 5.0) / (TAU / 5.0)  # 0 at a point, 0.5 at a valley
		var t := minf(a, 1.0 - a) * 2.0
		return d.length() - lerpf(42.0, 18.0, t))


static func _cross(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float:
		return minf(_box(p, Vector2(48, 48), Vector2(11, 34), 4.0), _box(p, Vector2(48, 48), Vector2(34, 11), 4.0)))


# A cloning facility's: a bright ring round a cross, larger than a clinic's.
static func _clone(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float: return absf(_circle(p, Vector2(48, 48), 41.0)) - 3.0, false, 1.0)
	_paint(image, func(p: Vector2) -> float:
		return minf(_box(p, Vector2(48, 48), Vector2(9, 27), 3.0), _box(p, Vector2(48, 48), Vector2(27, 9), 3.0)))
	_paint(image, func(p: Vector2) -> float: return _circle(p, Vector2(48, 48), 6.0), false, 1.0)


# An open-ended wrench, on the diagonal: its jaw up and right, a ring at its foot.
static func _wrench(image: Image) -> void:
	var turn := Transform2D(-PI / 4.0, Vector2.ZERO)
	_paint(image, func(p: Vector2) -> float:
		var q := turn * (p - Vector2(48, 48)) + Vector2(48, 48)  # the wrench upright
		var handle := _box(q, Vector2(48, 52), Vector2(7, 24), 3.0)
		var head := _circle(q, Vector2(48, 22), 17.0)
		var foot := _circle(q, Vector2(48, 78), 11.0)
		var jaw := _box(q, Vector2(48, 9), Vector2(6.5, 13), 1.0)
		var eye := _circle(q, Vector2(48, 78), 4.5)
		return maxf(maxf(minf(minf(handle, head), foot), -jaw), -eye))


# A shuttle head-on: its tall middle fin, its body and two wings swept down.
static func _shuttle(image: Image) -> void:
	_paint(image, func(p: Vector2) -> float:
		var fin := _segment(p, Vector2(48, 9), Vector2(48, 48)) - 6.5
		var body := _box(p, Vector2(48, 58), Vector2(21, 14), 7.0)
		var left := _segment(p, Vector2(32, 66), Vector2(11, 90)) - 5.0
		var right := _segment(p, Vector2(64, 66), Vector2(85, 90)) - 5.0
		return minf(minf(fin, body), minf(left, right)))
	_paint(image, func(p: Vector2) -> float: return _box(p, Vector2(48, 56), Vector2(10, 4), 2.0), false, 1.0)  # its window


# A starfighter head-on: its round hull, four wings spread, a cannon at each tip.
static func _fighter(image: Image) -> void:
	var tips := [Vector2(14, 22), Vector2(82, 22), Vector2(14, 74), Vector2(82, 74)]
	_paint(image, func(p: Vector2) -> float:
		var d := _circle(p, Vector2(48, 48), 14.0)
		for tip: Vector2 in tips:
			d = minf(d, _segment(p, Vector2(48, 48), tip) - 5.0)
			d = minf(d, _box(p, tip, Vector2(4, 12), 2.0))
		return d)
	_paint(image, func(p: Vector2) -> float: return _circle(p, Vector2(48, 48), 6.0), false, 1.0)  # its nose


static func _chest(image: Image) -> void:
	# A body with a domed lid, a seam where they meet, two straps and a latch.
	var seam := 48.0
	_paint(image, func(p: Vector2) -> float:
		var body := _box(p, Vector2(48, 66), Vector2(36, 20), 3.0)
		var lid := maxf(_ellipse(p, Vector2(48, seam), Vector2(36, 26)), p.y - seam)
		return minf(body, lid))
	_line(image, Vector2(12, seam), Vector2(84, seam))
	_line(image, Vector2(30, 24), Vector2(30, 86))
	_line(image, Vector2(66, 24), Vector2(66, 86))
	_paint(image, func(p: Vector2) -> float: return _box(p, Vector2(48, seam + 5.0), Vector2(8, 10), 2.0), false, 1.0)
