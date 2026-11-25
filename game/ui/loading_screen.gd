# Battlefront II-styled loading screen: obscures world/map loads between
# Main Menu -> Hub, Hub -> Battle Match, and Battle Match -> Hub.
extends "res://ui/bf2_screen.gd"

static var pending: Dictionary = {}

var title := "MOS EISLEY"
var subtitle := "Loading world..."
var tip := "Tip: Use your Multi-Scanner (G) outside Mos Eisley to find mineral, chemical, and gas veins."
var icon_name := "hud_flag_icon"
var progress := 0.0

var _elapsed := 0.0


func _init(shared_art: RefCounted = null) -> void:
	super._init(shared_art)
	mouse_filter = Control.MOUSE_FILTER_STOP


func show_loading(new_title: String, new_subtitle: String, new_icon := "hud_flag_icon", new_tip := "") -> void:
	title = new_title
	subtitle = new_subtitle
	icon_name = new_icon if new_icon != "" else "hud_flag_icon"
	if new_tip != "":
		tip = new_tip
	progress = 0.25
	_elapsed = 0.0
	visible = true
	queue_redraw()


func _process(delta: float) -> void:
	if not visible:
		return
	_elapsed += delta
	progress = minf(0.95, 0.25 + _elapsed * 0.35)
	queue_redraw()


func _paint() -> void:
	# Deep space backdrop with subtle scanlines so the unbuilt world is completely obscured.
	draw_rect(Rect2(Vector2.ZERO, size), Color(0.02, 0.03, 0.06, 1.0))
	var step_y := maxf(4.0, 4.0 * u)
	var line_col := Color(0.06, 0.09, 0.15, 0.22)
	var y := 0.0
	while y < size.y:
		draw_line(Vector2(0.0, y), Vector2(size.x, y), line_col, 1.0)
		y += step_y

	var box := centered(620, 290, Vector2(0, -10))
	frame(box, "border_3_pieces")

	if icon_name != "":
		var icon_rect := Rect2(Vector2(box.get_center().x - 32 * u, box.position.y + 22 * u), Vector2(64, 64) * u)
		art.draw_icon(self, icon_name, icon_rect, ACCENT)

	text("gamefont_large", title.to_upper(), Vector2(box.get_center().x, box.position.y + 98 * u), "Center", ACCENT)
	text("gamefont_medium", subtitle, Vector2(box.get_center().x, box.position.y + 140 * u), "Center", TEXT)

	# Segmented BF2-style progress bar.
	var bar_bg := Rect2(Vector2(box.position.x + 52 * u, box.position.y + 190 * u), Vector2(box.size.x - 104 * u, 22 * u))
	draw_rect(bar_bg, Color(0.04, 0.07, 0.11, 0.95))
	draw_rect(bar_bg, Color(0.35, 0.55, 0.85, 0.75), false, 1.5)
	var segments := 18
	var pad := 3.0 * u
	var seg_w := (bar_bg.size.x - pad * (segments + 1)) / segments
	var lit := int(round(progress * segments))
	var pulse := int(fmod(_elapsed * 12.0, float(segments)))
	for i in segments:
		var seg_r := Rect2(bar_bg.position.x + pad + i * (seg_w + pad), bar_bg.position.y + pad,
				seg_w, bar_bg.size.y - pad * 2.0)
		var col := Color(0.14, 0.22, 0.34, 0.5)
		if i < lit:
			col = Color(0.45, 0.82, 1.0, 0.95) if i == pulse else Color(0.26, 0.62, 0.95, 0.85)
		draw_rect(seg_r, col)

	# Wrapped tip line at the bottom of the frame.
	var words := tip.split(" ", false)
	var lines := PackedStringArray()
	var line := ""
	var max_w := box.size.x - 56 * u
	for w in words:
		var cand := w if line == "" else line + " " + w
		if line != "" and art.text_width("gamefont_tiny", cand, u) > max_w:
			lines.append(line)
			line = w
		else:
			line = cand
	if line != "":
		lines.append(line)
	var ty := box.position.y + 228 * u
	for ln in lines:
		text("gamefont_tiny", ln, Vector2(box.get_center().x, ty), "Center", DIM)
		ty += art.line_height("gamefont_tiny", u) + 2 * u
