// Galaxies interior cell lighting. Each cell of a building is lit only by its
// own lights (from the .pob: ambient, parallel, and point), so this replaces
// Godot's lighting for cell geometry. Lights are in building space, which is
// the mesh's own space for cell meshes (they sit at the building origin);
// furniture in a cell gives its own place in the building (to_building).
//
// Lighting is computed on sRGB values, then converted to linear, to keep
// Galaxies' original look (its fixed-function lighting worked in sRGB).
//
// Shared by swg_cell.gdshader (opaque), swg_cell_alpha.gdshader (alpha
// blended: glass, grates) and swg_cell_cutout.gdshader (cut out at alpha 0.5),
// as the material's effect draws.
const int MAX_LIGHTS = 8;

uniform sampler2D albedo_texture : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec4 albedo_color = vec4(1.0);  // the material's base color, sRGB (a customizable shader's)
uniform vec3 ambient = vec3(0.0);
uniform int parallel_count = 0;
uniform vec3 parallel_direction[MAX_LIGHTS];  // the direction the light travels
uniform vec3 parallel_color[MAX_LIGHTS];
uniform int point_count = 0;
uniform vec3 point_position[MAX_LIGHTS];
uniform vec3 point_color[MAX_LIGHTS];
uniform vec3 point_attenuation[MAX_LIGHTS];  // constant, linear, quadratic

uniform mat4 to_building = mat4(1.0);  // the mesh's space to the building's

varying vec3 building_position;
varying vec3 building_normal;

void vertex() {
	building_position = (to_building * vec4(VERTEX, 1.0)).xyz;
	building_normal = mat3(to_building) * NORMAL;
}

void fragment() {
	vec3 n = normalize(building_normal);
	if (!FRONT_FACING) {
		n = -n;
	}
	vec3 light = ambient;
	for (int i = 0; i < parallel_count; i++) {
		light += parallel_color[i] * max(dot(n, -parallel_direction[i]), 0.0);
	}
	for (int i = 0; i < point_count; i++) {
		vec3 to_light = point_position[i] - building_position;
		float d = length(to_light);
		vec3 a = point_attenuation[i];
		float falloff = 1.0 / max(a.x + a.y * d + a.z * d * d, 0.001);
		light += point_color[i] * max(dot(n, to_light / max(d, 0.0001)), 0.0) * falloff;
	}
	vec4 texel = texture(albedo_texture, UV) * albedo_color;
	vec3 srgb = clamp(texel.rgb * light, 0.0, 1.0);
	ALBEDO = mix(srgb / 12.92, pow((srgb + 0.055) / 1.055, vec3(2.4)), step(0.04045, srgb));
#if defined(CELL_ALPHA)
	ALPHA = texel.a;
#elif defined(CELL_CUTOUT)
	ALPHA = texel.a;
	ALPHA_SCISSOR_THRESHOLD = 0.5;
#endif
}
