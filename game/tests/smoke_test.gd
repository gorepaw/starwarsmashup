# Headless check that the engine is a double-precision build and that the
# GDExtension loads and calls into core/.
#
#   godot --headless --path game --import
#   godot --headless --path game --script res://tests/smoke_test.gd
extends SceneTree


func _init() -> void:
	var failures := 0

	# Vector3 uses real_t. Single precision can't represent .125 at 2^24.
	var v := Vector3(16777216.0 + 0.125, 0.0, 0.0)
	if v.x - 16777216.0 != 0.125:
		printerr("FAIL: engine is not a double-precision build")
		failures += 1

	if not ClassDB.class_exists("SwmInfo"):
		printerr("FAIL: SwmInfo not registered (GDExtension did not load)")
		failures += 1
	else:
		var info: Object = ClassDB.instantiate("SwmInfo")
		var version: String = info.get_core_version()
		var real_size: int = info.get_real_size()
		print("core version: ", version)
		print("extension real_t bytes: ", real_size)
		if version.is_empty():
			printerr("FAIL: empty core version")
			failures += 1
		if real_size != 8:
			printerr("FAIL: extension is not built with double precision")
			failures += 1

	print("smoke_test: ", "all passed" if failures == 0 else "%d failed" % failures)
	quit(failures)
