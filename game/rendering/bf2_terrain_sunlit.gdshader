// Battlefront II terrain on a map whose sun isn't baked into its lighting
// (Endor): as bf2_terrain.gdshader, with Godot's sun (and its shadows) on
// top of the baked lighting, which already holds the ambient light.
shader_type spatial;
render_mode ambient_light_disabled, specular_disabled;

#define SUNLIT
#include "res://rendering/bf2_terrain.gdshaderinc"
