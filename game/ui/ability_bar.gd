# Stage 12: the specialist kit on the HUD, in Battlefront II's style. Along
# the bottom, the active loadout's abilities (their keys, their cooldowns);
# above them the buffs and debuffs on you; to the left your fielded
# companion; in the middle a channel's progress (taming, sampling). Draws
# only: SpecialistUI sets what it shows and handles the keys.
extends "res://ui/bf2_screen.gd"

const AbilityIcons = preload("res://ui/ability_icons.gd")

const SLOT := 54.0          # units, a slot's side
const GAP := 6.0
const READY := Color(0.55, 0.85, 1.0)
const COOLING := Color(0.05, 0.06, 0.09, 0.72)
const BUFF := Color(0.4, 0.85, 0.55)
const DEBUFF := Color(1.0, 0.4, 0.35)

# Set by SpecialistUI before each redraw:
var slots: Array = []        # [{key, id, name, cooldown, total, where}]; at most six
var effects: Array = []      # [{name, remaining, debuff}]
var companion := {}          # {name, health, max_health, mode, cooldown}; empty: none
var channel := {}            # {name, progress (0..1)}; empty: none
var hint := ""               # e.g. "Aim at an ally: Z heals" (the last ability used, or a target prompt)
var performing := false


func _init(shared_art: RefCounted = null) -> void:
	super._init(shared_art)
	mouse_filter = Control.MOUSE_FILTER_IGNORE


func _paint() -> void:
	var bottom := size.y - 5 * u  # (it was 18: low enough to clear the prompts and popups over it)
	var width := slots.size() * (SLOT + GAP) * u - GAP * u
	var x0 := (size.x - width) * 0.5
	var top := bottom - SLOT * u
	for i in slots.size():
		var s: Dictionary = slots[i]
		var rect := Rect2(x0 + i * (SLOT + GAP) * u, top, SLOT * u, SLOT * u)
		draw_rect(rect, Color(0.06, 0.08, 0.12, 0.8))
		frame(rect, "border_popup")
		var name := str(s.get("name", ""))
		var icon := AbilityIcons.texture(str(s.get("id", "")))
		if icon != null:
			# Its icon, its name small under it.
			var side := 34.0 * u
			draw_texture_rect(icon, Rect2(rect.get_center().x - side * 0.5, rect.position.y + 4 * u, side, side), false, READY)
			var short := name.split(" ", false)[0] if name != "" else ""
			while short.length() > 1 and art.text_width("gamefont_tiny", short, u * 0.75) > (SLOT - 18.0) * u:
				short = short.left(short.length() - 1)
			text("gamefont_tiny", short, Vector2(rect.get_center().x + 6 * u, rect.end.y - 13 * u), "Center", READY, 0.75)
		else:
			var words := name.split(" ", false)
			var first := (words[0] if words.size() > 0 else name).substr(0, 9)
			var second := (words[1] if words.size() > 1 else "").substr(0, 9)
			text("gamefont_tiny", first, Vector2(rect.get_center().x, rect.position.y + 16 * u), "Center", READY, 0.9)
			if second != "":
				text("gamefont_tiny", second, Vector2(rect.get_center().x, rect.position.y + 28 * u), "Center", READY, 0.9)
		var cd := float(s.get("cooldown", 0.0))
		var total := maxf(0.001, float(s.get("total", 1.0)))
		if cd > 0.05:
			var share := clampf(cd / total, 0.0, 1.0)
			draw_rect(Rect2(rect.position.x, rect.position.y + rect.size.y * (1.0 - share), rect.size.x, rect.size.y * share), COOLING)
			text("gamefont_small", "%d" % ceili(cd), Vector2(rect.get_center().x, rect.position.y + 22 * u), "Center", TEXT)
		text("gamefont_tiny", str(s.get("key", "")), Vector2(rect.position.x + 4 * u, rect.end.y - 14 * u), "Left", ACCENT, 0.9)
	# Effects, over the bar.
	var ex := x0
	var ey := top - 22 * u
	for e: Dictionary in effects:
		var label := "%s %ds" % [str(e.get("name", "")), ceili(float(e.get("remaining", 0.0)))]
		var w: float = art.text_width("gamefont_tiny", label, u * 0.9) + 14 * u
		var pill := Rect2(ex, ey, w, 18 * u)
		var tone: Color = DEBUFF if bool(e.get("debuff", false)) else BUFF
		draw_rect(pill, Color(tone.r * 0.2, tone.g * 0.2, tone.b * 0.2, 0.8))
		draw_rect(pill, tone, false, 1.0)
		text("gamefont_tiny", label, Vector2(pill.position.x + 7 * u, ey + 2 * u), "Left", tone, 0.9)
		ex += w + 6 * u
		if ex > x0 + maxf(width, 360 * u):
			ex = x0
			ey -= 22 * u
	if performing:
		text("gamefont_small", "Performing", Vector2(size.x * 0.5, ey - 24 * u), "Center", ACCENT)
	# A fielded companion, to the left.
	if not companion.is_empty():
		var box := Rect2(x0 - 190 * u, top, 176 * u, SLOT * u)
		draw_rect(box, Color(0.06, 0.08, 0.12, 0.8))
		frame(box, "border_popup")
		var cooldown := float(companion.get("cooldown", 0.0))
		if str(companion.get("name", "")) != "":
			text("gamefont_tiny", str(companion["name"]), Vector2(box.position.x + 8 * u, box.position.y + 6 * u), "Left", TEXT)
			var hp := clampf(float(companion.get("health", 0.0)) / maxf(1.0, float(companion.get("max_health", 1.0))), 0.0, 1.0)
			var bar := Rect2(box.position.x + 8 * u, box.position.y + 24 * u, box.size.x - 16 * u, 6 * u)
			draw_rect(bar, Color(0.2, 0.05, 0.05, 0.9))
			draw_rect(Rect2(bar.position, Vector2(bar.size.x * hp, bar.size.y)), Color(0.35, 0.9, 0.4))
			text("gamefont_tiny", str(companion.get("mode", "")), Vector2(box.position.x + 8 * u, box.position.y + 34 * u), "Left", DIM, 0.85)
		elif cooldown > 0:
			text("gamefont_tiny", "Companion recovering %ds" % ceili(cooldown), Vector2(box.position.x + 8 * u, box.position.y + 18 * u), "Left", DIM)
	# A channel, in the middle.
	if not channel.is_empty():
		var bar := Rect2(size.x * 0.5 - 120 * u, size.y * 0.62, 240 * u, 12 * u)
		draw_rect(bar, Color(0.05, 0.07, 0.1, 0.85))
		draw_rect(Rect2(bar.position, Vector2(bar.size.x * clampf(float(channel.get("progress", 0.0)), 0.0, 1.0), bar.size.y)), READY)
		frame(bar.grow(3 * u), "border_popup")
		text("gamefont_small", str(channel.get("name", "")), Vector2(size.x * 0.5, bar.position.y - 24 * u), "Center", TEXT)
	if hint != "":
		text("gamefont_tiny", hint, Vector2(size.x * 0.5, top - 48 * u - (22 * u if effects.size() > 0 else 0.0)), "Center", DIM)
