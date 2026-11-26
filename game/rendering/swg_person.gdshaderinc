// A Galaxies character's customizable material (see SwgColors): its texture
// times a palette color, or for a two-color one, times two colors blended by
// the texture's alpha (0: MAIN, 1: HUEB), as Galaxies' pixel programs do.
//
// Shared by swg_person.gdshader (opaque), swg_person_alpha.gdshader (alpha
// blended: hair) and swg_person_cutout.gdshader (cut out at alpha 0.5), as
// the material's effect draws.

uniform sampler2D albedo_texture : source_color, filter_linear_mipmap, repeat_enable;
uniform vec3 main_color : source_color = vec3(1.0);
uniform vec3 hueb_color : source_color = vec3(1.0);
uniform bool two_colors = false;
uniform float roughness = 1.0;
uniform float metallic = 0.0;
// (Worn things aren't stood off the skin: what they cover isn't drawn, by
// Galaxies' occlusion zones: SwgOcclusion, swgtool's export.)

void fragment() {
	vec4 texel = texture(albedo_texture, UV);
	vec3 hue = two_colors ? mix(main_color, hueb_color, texel.a) : main_color;
	ALBEDO = texel.rgb * hue;
	ROUGHNESS = roughness;
	METALLIC = metallic;
#if defined(PERSON_ALPHA)
	ALPHA = texel.a;
#elif defined(PERSON_CUTOUT)
	ALPHA = texel.a;
	ALPHA_SCISSOR_THRESHOLD = 0.5;
#endif
}
