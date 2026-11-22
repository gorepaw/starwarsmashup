// A Galaxies character's customizable material, alpha blended (hair): see
// swg_person.gdshaderinc. Its solid parts write depth first, so a head of
// hair sorts against itself.
shader_type spatial;
render_mode depth_prepass_alpha;
#define PERSON_ALPHA

#include "res://rendering/swg_person.gdshaderinc"
