# What a hub shows over Battlefront's HUD, in its style: the tooltip bar
# with what you can do where you stand (at a war terminal), the queue while
# you wait for a battle (your side, where, when), and how your last battle
# went when you're back. Draws only; the keys are the game's.
extends "res://ui/bf2_screen.gd"

const TOOLTIP_COLOR := Color8(192, 224, 224)  # Battlefront's tooltip box's
const VICTORY := Color(0.4, 0.65, 1.0)
const DEFEAT := Color(1.0, 0.35, 0.3)

# Set by the game before each redraw:
var tooltip := ""     # e.g. "Press E to use the War Terminal"; empty: none
var queue := {}       # {side, mode, icon, battlefield, starts_in, in_queue, leave_key}; empty: not queued
var notice := ""      # e.g. "Victory at Endor: 3 kills, 1 deaths, 2 captures"
var notice_alpha := 0.0
var info := {}        # {credits, fp, xp, loadout_name, inv_key}
var switching := {}   # {name, progress}: non-empty while channeling a loadout switch
var tracker := {}     # the tracked quest: {title, step, progress, distance, log_key}; empty: none
# Where the tracked quest's step is, on screen: {at (pixels), onscreen,
# angle (radians: which way it lies, off screen), distance}; empty: none.
var waypoint := {}
var scanner := {}     # {mode, resource_name, concentration, peak_distance, cooldown, hint}; empty: off


func _init(shared_art: RefCounted = null) -> void:
	super._init(shared_art)
	mouse_filter = Control.MOUSE_FILTER_IGNORE


func _paint() -> void:
	if not info.is_empty():
		_paint_info()
	if not waypoint.is_empty():
		_paint_waypoint()
	if not tracker.is_empty():
		_paint_tracker()
	if not scanner.is_empty():
		_paint_scanner()
	if tooltip != "":
		_paint_tooltip()
	if not queue.is_empty():
		_paint_queue()
	if not switching.is_empty():
		_paint_switching()
	if notice != "" and notice_alpha > 0:
		_paint_notice()


func _paint_info() -> void:
	var box := Rect2(Vector2(14, 14) * u, Vector2(280, 56) * u)
	frame(box, "border_3_pieces")
	text("gamefont_tiny", "Loadout: %s   [%s] Gear" % [str(info.get("loadout_name", "Standard Issue")), str(info.get("inv_key", "I"))],
			box.position + Vector2(12, 10) * u, "Left", ACCENT)
	text("gamefont_tiny", "%d FP   |   %d Cr   |   %d XP" % [int(info.get("fp", 0)), int(info.get("credits", 0)), int(info.get("xp", 0))],
			box.position + Vector2(12, 30) * u, "Left", TEXT)


func _paint_switching() -> void:
	var box := Rect2(Vector2(size.x * 0.5 - 150 * u, size.y * 0.72), Vector2(300, 46) * u)
	frame(box)
	text("gamefont_tiny", "Switching to %s..." % str(switching.get("name", "Loadout")),
			Vector2(box.get_center().x, box.position.y + 8 * u), "Center", ACCENT)
	var bar_bg := Rect2(box.position + Vector2(16, 26) * u, Vector2(268, 10) * u)
	draw_rect(bar_bg, Color(0.1, 0.12, 0.16, 0.9))
	var frac := clampf(float(switching.get("progress", 0.0)), 0.0, 1.0)
	draw_rect(Rect2(bar_bg.position, Vector2(bar_bg.size.x * frac, bar_bg.size.y)), ACCENT)


# The tracked quest, at the right: its title, the step it's on and how far.
func _paint_tracker() -> void:
	var step := str(tracker.get("step", ""))
	var progress := str(tracker.get("progress", ""))
	var lines := _wrap_line(step + ("  " + progress if progress != "" else ""), 268 * u)
	var box := Rect2(Vector2(size.x - 296 * u, 150 * u), Vector2(282 * u, (58 + 15 * lines.size()) * u))
	frame(box, "border_3_pieces")
	text("gamefont_tiny", "Quest   [%s] Log" % str(tracker.get("log_key", "L")), box.position + Vector2(12, 8) * u,
			"Left", DIM)
	var distance := str(tracker.get("distance", ""))
	if distance != "":
		text("gamefont_tiny", distance, Vector2(box.end.x - 12 * u, box.position.y + 8 * u), "Right", ACCENT)
	text("gamefont_small", str(tracker.get("title", "")), box.position + Vector2(12, 24) * u, "Left", ACCENT)
	var y := box.position.y + 44 * u
	for line in lines:
		text("gamefont_tiny", line, Vector2(box.position.x + 12 * u, y), "Left", TEXT)
		y += 15 * u


func _wrap_line(value: String, width: float) -> PackedStringArray:
	var lines := PackedStringArray()
	var line := ""
	for word in value.split(" ", false):
		var candidate := word if line == "" else line + " " + word
		if line != "" and art.text_width("gamefont_tiny", candidate, u) > width:
			lines.append(line)
			line = word
		else:
			line = candidate
	lines.append(line)
	return lines


# Battlefront's objective marker: its chevrons over the place, with the
# distance under them; off screen, its other chevrons at the edge, pointing
# the way.
func _paint_waypoint() -> void:
	var at: Vector2 = waypoint["at"]
	var icon_size := 30 * u
	var color := Color(ACCENT, 0.9)
	if bool(waypoint.get("onscreen", false)):
		var t: Texture2D = art.texture("hud_objective_icon")
		if t != null:
			draw_texture_rect(t, Rect2(at - Vector2(icon_size * 0.5, icon_size), Vector2(icon_size, icon_size)), false, color)
		text("gamefont_tiny", str(waypoint.get("distance", "")), at + Vector2(0, 4 * u), "Center", color)
		return
	var t: Texture2D = art.texture("hud_objective_icon_offscreen")
	if t == null:
		return
	# Its chevrons point up: turned to point the way.
	draw_set_transform(at, float(waypoint.get("angle", 0.0)) + PI * 0.5)
	draw_texture_rect(t, Rect2(-Vector2(icon_size, icon_size) * 0.5, Vector2(icon_size, icon_size)), false, color)
	draw_set_transform(Vector2.ZERO)
	var inward := -Vector2.from_angle(float(waypoint.get("angle", 0.0))) * icon_size * 1.1
	text("gamefont_tiny", str(waypoint.get("distance", "")), at + inward - Vector2(0, 6 * u), "Center", color)


# Battlefront's tooltip: its text on a dark bar fading out at the ends.
func _paint_tooltip() -> void:
	var width: float = art.text_width("gamefont_tiny", tooltip, u) + 120 * u
	var bar := Rect2(Vector2((size.x - width) * 0.5, size.y - 92 * u), Vector2(width, 26 * u))
	var t: Texture2D = art.texture("tooltipsborder")
	if t != null:
		# The bar is a strip across the middle of its texture.
		var strip := Rect2(Vector2(20, 106) * t.get_size() / Vector2(512, 256), Vector2(472, 42) * t.get_size() / Vector2(512, 256))
		draw_texture_rect_region(t, bar, strip, Color(TOOLTIP_COLOR, 0.95))
	else:
		draw_rect(bar, Color(0.1, 0.1, 0.1, 0.8))
	text("gamefont_tiny", tooltip, Vector2(bar.get_center().x, bar.get_center().y - art.line_height("gamefont_tiny", u) * 0.5),
			"Center", TOOLTIP_COLOR)


# Queued: a panel at the top, the side's icon, where, and the countdown.
func _paint_queue() -> void:
	var box := Rect2(Vector2(size.x * 0.5 - 170 * u, 14 * u), Vector2(340, 92) * u)
	frame(box)
	art.draw_icon(self, str(queue.get("icon", "")), Rect2(box.position + Vector2(14, 16) * u, Vector2(56, 56) * u), ACCENT)
	var x := box.position.x + 82 * u
	var qmode := str(queue.get("mode", "standard")).capitalize()
	text("gamefont_tiny", "Queued (%s) for the %s" % [qmode, queue.get("side", "")], Vector2(x, box.position.y + 12 * u), "Left", DIM)
	var where := str(queue.get("battlefield", ""))
	text("gamefont_medium", where if where != "" else "Choosing a battlefield", Vector2(x, box.position.y + 30 * u),
			"Left", TEXT)
	text("gamefont_tiny", "%d in the queue     %s  leave" % [int(queue.get("in_queue", 0)), queue.get("leave_key", "X")],
			Vector2(x, box.position.y + 62 * u), "Left", DIM)
	var seconds := ceili(float(queue.get("starts_in", 0)))
	text("gamefont_large", "0:%02d" % seconds if seconds > 0 else "--", Vector2(box.end.x - 16 * u, box.position.y + 24 * u),
			"Right", ACCENT)


# Back from a battle: Victory, Defeat or Draw, and the rest of the notice.
func _paint_notice() -> void:
	var headline := notice
	var detail := ""
	for outcome in ["Victory", "Defeat", "Draw"]:
		if notice.begins_with(outcome + " "):
			headline = outcome
			detail = notice.substr(outcome.length() + 1).trim_prefix("at ")
	var color := VICTORY if headline == "Victory" else (DEFEAT if headline == "Defeat" else ACCENT)
	color.a = notice_alpha
	var top := size.y * 0.24
	text("gamefont_large", headline, Vector2(size.x * 0.5, top), "Center", color, 1.4 if detail != "" else 1.0)
	if detail != "":
		text("gamefont_small", detail, Vector2(size.x * 0.5, top + 48 * u), "Center", Color(TEXT, notice_alpha))


func _paint_scanner() -> void:
	var box := Rect2(Vector2(size.x * 0.5 - 190 * u, size.y - 166 * u), Vector2(380, 62) * u)
	frame(box)
	var mode_str := str(scanner.get("mode_label", scanner.get("mode", "Mineral"))).to_upper()
	var res_name := str(scanner.get("resource_name", "None"))
	var conc := clampf(float(scanner.get("concentration", 0.0)), 0.0, 1.0)
	var dist := float(scanner.get("peak_distance", 0.0))
	var cd := float(scanner.get("cooldown", 0.0))
	text("gamefont_tiny", "SCANNER [%s]  %s" % [mode_str, res_name], Vector2(box.position.x + 12 * u, box.position.y + 8 * u), "Left", ACCENT)
	var dist_txt := "Peak %d m" % roundi(dist) if dist >= 0.0 else "No vein"
	text("gamefont_tiny", "%d%%  |  %s" % [roundi(conc * 100.0), dist_txt], Vector2(box.end.x - 12 * u, box.position.y + 8 * u), "Right", TEXT)
	var bar_bg := Rect2(Vector2(box.position.x + 12 * u, box.position.y + 28 * u), Vector2(box.size.x - 24 * u, 8 * u))
	draw_rect(bar_bg, Color(0.12, 0.16, 0.2, 0.85))
	var bar_col := Color(0.25, 0.92, 0.55) if conc >= 0.6 else (ACCENT if conc >= 0.3 else Color(0.85, 0.45, 0.25))
	draw_rect(Rect2(bar_bg.position, Vector2(bar_bg.size.x * conc, bar_bg.size.y)), bar_col)
	var hint := str(scanner.get("hint", "[E] Extract  [Wheel] Mode  [G] Off"))
	var extracting := float(scanner.get("extract_progress", -1.0))
	if extracting >= 0.0:
		hint = "Extracting... hold still  %d%%" % roundi(extracting * 100.0)
		var track := Rect2(Vector2(box.position.x + 12 * u, box.end.y - 8 * u), Vector2(box.size.x - 24 * u, 4 * u))
		draw_rect(track, Color(0.12, 0.16, 0.2, 0.85))
		draw_rect(Rect2(track.position, Vector2(track.size.x * extracting, track.size.y)), ACCENT)
	elif cd > 0.05:
		hint = "[E] Extract  [Wheel] Mode  [G] Off"
	text("gamefont_tiny", hint, Vector2(box.get_center().x, box.position.y + 42 * u), "Center", DIM)
