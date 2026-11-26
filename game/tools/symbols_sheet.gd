# Saves every place symbol (rendering/place_symbols.gd) side by side on a
# dark ground, to look them over:
#   godot --headless --path game --script res://tools/symbols_sheet.gd -- <out.png>
extends SceneTree

const PlaceSymbols = preload("res://rendering/place_symbols.gd")


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var out := args[0] if args.size() > 0 else "symbols.png"
	var n: int = PlaceSymbols.NAMES.size()
	var cell := PlaceSymbols.SIZE + 16
	var sheet := Image.create(cell * n, cell, false, Image.FORMAT_RGBA8)
	sheet.fill(Color(0.08, 0.1, 0.14))
	for i in n:
		var tex := PlaceSymbols.texture(PlaceSymbols.NAMES[i])
		var img := tex.get_image()
		img.decompress()
		img.convert(Image.FORMAT_RGBA8)
		for y in img.get_height():
			for x in img.get_width():
				var c := img.get_pixel(x, y)
				var tint := Color(0.45, 0.85, 1.0, c.a)
				var at := Vector2i(i * cell + 8 + x, 8 + y)
				sheet.set_pixelv(at, sheet.get_pixelv(at).blend(tint))
	sheet.save_png(out)
	print("wrote ", out)
	quit(0)
