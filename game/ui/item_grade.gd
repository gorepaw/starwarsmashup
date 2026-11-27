# Item grades (docs/DESIGN.md, Grades): their names and colors, and the grade
# an owned item shows: its def's Grade, a graded item's own (a weapon, armor or
# a Deflector Shield), else none, shown white.
extends RefCounted

const NAMES := ["Flawed", "Standard", "Fine", "Exceptional", "Legendary"]
const WHITE := Color(0.92, 0.92, 0.92)
const COLORS := [
	WHITE,                      # Flawed
	Color(0.35, 0.9, 0.35),     # Standard
	Color(0.3, 0.6, 1.0),       # Fine
	Color(0.75, 0.42, 1.0),     # Exceptional
	Color(1.0, 0.58, 0.12),     # Legendary
]
const GRADED_KINDS := ["weapon", "armor", "deflector_shield"]


# The grade an item shows (-1: none). `def` is a catalog entry, `inst` an owned
# copy or loot entry ({grade}).
static func shown(def: Dictionary, inst: Dictionary) -> int:
	var fixed := int(def.get("grade", -1))
	if fixed >= 0:
		return clampi(fixed, 0, NAMES.size() - 1)
	var graded: bool = def.get("graded", str(def.get("kind", "")) in GRADED_KINDS)
	if graded and not inst.is_empty():
		return clampi(int(inst.get("grade", 1)), 0, NAMES.size() - 1)
	return -1


static func color_of(grade: int) -> Color:
	return COLORS[grade] if grade >= 0 and grade < COLORS.size() else WHITE


static func color(def: Dictionary, inst: Dictionary) -> Color:
	return color_of(shown(def, inst))


# "Fine" ("" for an ungraded item).
static func label(def: Dictionary, inst: Dictionary) -> String:
	var g := shown(def, inst)
	return NAMES[g] if g >= 0 else ""
