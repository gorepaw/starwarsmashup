# Headless check for Stage 12's client side: the extension reads
# specialists.ini for the screens, and the ability bar, datapad, bazaar and
# field effects draw with real data without errors.
#
#   godot --headless --path game --script res://tests/stage12_test.gd
extends SceneTree

const Bf2Art = preload("res://ui/bf2_art.gd")
const AbilityBar = preload("res://ui/ability_bar.gd")
const DatapadScreen = preload("res://ui/datapad_screen.gd")
const BazaarScreen = preload("res://ui/bazaar_screen.gd")
const FieldEffects = preload("res://rendering/field_effects.gd")
const SpecialistUI = preload("res://scenes/specialist_ui.gd")

var _frames := 0
var _screens: Array = []
var _field: RefCounted
var _world: Node3D


func _initialize() -> void:
	var client: Object = ClassDB.instantiate("SwmClient")
	assert(client.has_method("get_specialist") and client.has_method("get_field"))
	var book: Dictionary = client.get_specialist_book(ProjectSettings.globalize_path("res://professions/specialists.ini"))
	var abilities: Dictionary = book.get("abilities", {})
	assert(abilities.size() >= 40, "abilities: %d" % abilities.size())
	assert(str((abilities["cm_heal_dart"] as Dictionary)["profession"]) == "combat_medic")
	assert((book.get("companions", {}) as Dictionary).has("dewback"))
	assert((book.get("modules", []) as Array).size() >= 8 and (book.get("slices", []) as Array).size() >= 7)
	assert((client.get_specialist() as Dictionary).is_empty())  # not connected
	print("specialist book: OK")
	_check_bar_layout()

	var art: RefCounted = Bf2Art.load_from("res://imported_assets/hud")
	var layer := CanvasLayer.new()
	root.add_child(layer)
	var spec := {
		"revision": 3,
		"abilities": [{"id": "cm_heal_dart", "cooldown": 2.5}, {"id": "ch_tame", "cooldown": 0.0}],
		"effects": [{"id": "inspired", "name": "Inspired", "remaining": 12.0, "duration": 20.0, "flags": 8}],
		"companions": [{"id": 1, "template": "womp_rat", "name": "Biter", "origin": "tamed", "quality": 0.6,
				"health": 1.1, "damage": 1.0, "speed": 1.0, "modules": PackedStringArray(), "mount": false},
				{"id": 2, "template": "droid_battle", "name": "B1", "origin": "assembled", "quality": 0.7,
				"health": 1.1, "damage": 1.1, "speed": 1.0, "modules": PackedStringArray(["dmod_blaster"]), "mount": false}],
		"active_companion": 1, "companion_entity": 9, "companion_cooldown": 0.0, "companion_mode": 0, "companion_slot": 2,
		"samples": [{"id": 4, "template": "dewback", "quality": 0.7}],
		"contracts": [{"id": 5, "kind": "track", "title": "Track: a worrt", "reward": 120, "expires_in": 600.0,
				"progress": 0.0, "needed": 0.0, "profession": "ranger"}],
		"offers": [], "offer_source": "",
		"bazaar": [{"id": 8, "seller": "Gubb", "shop": "Gubb's Shop", "def_id": "wp_rifle_e11", "quality": 0.5,
				"custom_name": "", "price": 900}],
		"my_listings": [], "prices": [{"def_id": "wp_rifle_e11", "count": 2, "low": 800, "high": 1000, "average": 900, "last": 1000}],
		"performing": true, "bounty_flag": false,
		"channel": "", "channel_remaining": 0.0,
	}
	var bar := AbilityBar.new(art)
	bar.slots = [{"key": "3", "name": "Healing Dart", "cooldown": 2.5, "total": 6.0}, {"key": "4", "name": "Tame", "cooldown": 0.0, "total": 10.0}]
	bar.effects = [{"name": "Inspired", "remaining": 12.0, "debuff": false}]
	bar.companion = {"name": "Biter", "health": 120.0, "max_health": 300.0, "mode": "Following"}
	bar.channel = {"name": "Tame... hold still", "progress": 0.4}
	bar.performing = true
	layer.add_child(bar)
	var items := [{"id": 11, "def_id": "wp_rifle_e11", "quality": 0.5, "custom_name": "", "color_json": "{\"slice\":\"slice_overclock\"}"},
			{"id": 12, "def_id": "bio_creature_egg", "quality": 0.6, "custom_name": "Dewback Egg", "color_json": "{}"}]
	var catalog := [{"id": "wp_rifle_e11", "name": "E-11", "kind": "weapon"}, {"id": "bio_creature_egg", "name": "Egg", "kind": "supply"}]
	for tab in DatapadScreen.TABS.size():
		var pad := DatapadScreen.new(art)
		pad.spec = spec
		pad.book = book
		pad.items = items
		pad.catalog = catalog
		pad.levels = {"creature_handler": 60, "droid_engineer": 80, "smuggler": 100, "bio_engineer": 90}
		pad.terminal = {"index": 0, "kind": "bounty", "name": "Bounty Mission Terminal"}
		layer.add_child(pad)
		pad.open_datapad(tab)
		_screens.append(pad)
	var bazaar := BazaarScreen.new(art)
	bazaar.spec = spec
	bazaar.items = items
	bazaar.catalog = catalog
	bazaar.credits = 1500
	bazaar.merchant_level = 60
	layer.add_child(bazaar)
	bazaar.visible = true
	_screens.append(bazaar)
	_screens.append(bar)

	_world = Node3D.new()
	root.add_child(_world)


# The ability bar's layout rules: keys fill themselves in the kit's order until
# the player chooses, and their choices hold across edits and loadouts.
func _check_bar_layout() -> void:
	var kit := ["bh_a", "bh_b", "bh_c", "bh_d", "bh_e", "ent_perform", "ch_tame", "ch_mend"]
	var ui: GDScript = SpecialistUI
	# By default: the first six.
	assert(ui.resolve_bar([], kit, []) == ["bh_a", "bh_b", "bh_c", "bh_d", "bh_e", "ent_perform"])
	# A short kit leaves the rest empty.
	assert(ui.resolve_bar([], ["bh_a", "bh_b"], []) == ["bh_a", "bh_b", "", "", "", ""])
	# Tame onto the first key: the bar keeps everything else where it was.
	var edited: Dictionary = ui.edit_bar([], [], kit, "ch_tame", 0)
	assert(ui.resolve_bar(edited["layout"], kit, edited["off"]) == ["ch_tame", "bh_b", "bh_c", "bh_d", "bh_e", "ent_perform"])
	# bh_a, bumped off the bar by it, takes no empty key while the bar is full of others.
	var bumped: Array = ui.resolve_bar(edited["layout"], kit, edited["off"])
	assert(not bumped.has("bh_a"))
	# Taking an ability off leaves its key to the next one, and it stays off.
	var off: Dictionary = ui.edit_bar([], [], kit, "bh_b", -1)
	var after: Array = ui.resolve_bar(off["layout"], kit, off["off"])
	assert(not after.has("bh_b") and after.has("ent_perform") and after.size() == 6, str(after))
	# Moving one to another key swaps nothing else: it leaves its old key empty.
	var moved: Dictionary = ui.edit_bar([], [], kit, "bh_a", 5)
	var moved_bar: Array = ui.resolve_bar(moved["layout"], kit, moved["off"])
	assert(moved_bar[5] == "bh_a" and moved_bar.count("bh_a") == 1, str(moved_bar))
	# A layout from another kit: keys whose ability isn't here fill themselves.
	var other: Array = ui.resolve_bar(["zz_gone", "ch_tame"], ["bh_a", "ch_tame"], [])
	assert(other == ["bh_a", "ch_tame", "", "", "", ""], str(other))
	# Resetting clears the choices.
	var reset: Dictionary = ui.edit_bar(edited["layout"], edited["off"], kit, "", -1)
	assert(reset["layout"].is_empty() and reset["off"].is_empty())
	print("ability bar layout: OK")


func _process(_delta: float) -> bool:
	_frames += 1
	for s: Control in _screens:
		s.queue_redraw()
	if _frames == 1:
		_field = FieldEffects.new(_world)
		_field.update({
			"marks": [{"entity": 2, "remaining": 5.0, "kind": 0}],
			"cloaked": PackedInt64Array([3]),
			"performers": PackedInt64Array([1]),
			"companions": {9: 1},
			"deployables": [{"id": 1, "kind": "spawn_beacon", "owner": 1, "friendly": true, "position": Vector3(1, 0, 1), "radius": 3.0, "remaining": 80.0},
					{"id": 2, "kind": "trap", "owner": 2, "friendly": false, "position": Vector3(3, 0, 1), "radius": 2.5, "remaining": 80.0}],
			"hazards": [{"id": 3, "kind": "bio", "position": Vector3.ZERO, "radius": 5.0, "remaining": 9.0},
					{"id": 0, "kind": "strike", "position": Vector3(5, 0, 5), "radius": 7.0, "remaining": 2.0}],
			"flair": {1: 1},
		}, {1: Vector3.ZERO, 2: Vector3(10, 0, 0), 3: Vector3(0, 0, 10), 9: Vector3(1, 0, 0)}, 1, 0.016)
		assert(_world.get_child_count() >= 8, "field nodes: %d" % _world.get_child_count())
	if _frames < 4:
		return false
	_field.update({}, {}, 1, 0.016)
	_field.clear()
	print("stage12 screens and field effects: OK")
	print("stage12_test: all passed")
	return true
