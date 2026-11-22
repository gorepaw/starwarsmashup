# Battlefront II's weapon scope over the whole screen while a scoped weapon
# (ZoomFirstPerson: the sniper rifles) is zoomed in. Its textures are one
# quarter of the overlay (the top-left), mirrored into the other three.
#
#   var scope := ScopeOverlay.new(art)   # Bf2Art: the HUD's textures
#   scope.side = "imp"                   # whose scope: imp, all, rep, cis
#   scope.visible = true
#
# ASSUMPTION: which side's scope is which of weapon_scope .. weapon_scope4
# (the HUD export lists them, not who uses them).
extends Control

const TEXTURES := {"imp": "weapon_scope", "all": "weapon_scope2", "rep": "weapon_scope3", "cis": "weapon_scope4"}

var side := "imp":
	set(value):
		side = value
		queue_redraw()
var _art: RefCounted


func _init(art: RefCounted) -> void:
	_art = art
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE


func _draw() -> void:
	var texture: Texture2D = _art.texture(TEXTURES.get(side, "weapon_scope")) if _art != null else null
	if texture == null:
		return
	# Drawn for a 4:3 screen: a 4:3 area the screen's height, in the middle
	# (on a wider screen the sides stay clear, as the art is outside its ring).
	var half := Vector2(size.y * 4.0 / 3.0, size.y) / 2
	# Each quarter faces its corner: mirrored across the middle.
	for corner: Vector2 in [Vector2(1, 1), Vector2(-1, 1), Vector2(1, -1), Vector2(-1, -1)]:
		draw_set_transform(size / 2, 0.0, corner)
		draw_texture_rect(texture, Rect2(-half, half), false)
	draw_set_transform(Vector2.ZERO)
