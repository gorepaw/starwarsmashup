// Galaxies' additive effects (glowing panels, lamps' glass): the texture
// times the material's color, added to what's behind it (its blend factors
// one and one, so its alpha doesn't matter), lit by nothing but itself.
// swgtool marks such a material with glTF extras {"blend": "add"}.
shader_type spatial;
render_mode blend_add, unshaded, depth_draw_never, fog_disabled;

uniform sampler2D albedo_texture : source_color, hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec4 albedo_color : source_color = vec4(1.0);

void fragment() {
	ALBEDO = texture(albedo_texture, UV).rgb * albedo_color.rgb;
	ALPHA = 1.0;
}
