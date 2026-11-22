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
var queue := {}       # {side, icon, battlefield, starts_in, in_queue, leave_key}; empty: not queued
var notice := ""      # e.g. "Victory at Endor: 3 kills, 1 deaths, 2 captures"
var notice_alpha := 0.0


func _init(shared_art: RefCounted = null) -> void:
	super._init(shared_art)
	mouse_filter = Control.MOUSE_FILTER_IGNORE


func _paint() -> void:
	if tooltip != "":
		_paint_tooltip()
	if not queue.is_empty():
		_paint_queue()
	if notice != "" and notice_alpha > 0:
		_paint_notice()


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
	text("gamefont_tiny", "Queued for the %s" % queue.get("side", ""), Vector2(x, box.position.y + 12 * u), "Left", DIM)
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
