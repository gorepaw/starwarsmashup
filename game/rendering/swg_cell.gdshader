// Galaxies interior cell lighting. Each cell of a building is lit only by its
// own lights (from the .pob: ambient, parallel, and point), so this replaces
// Godot's lighting for cell geometry. Lights are in building space, which is
// the mesh's own space (cell meshes sit at the building origin).
//
// Lighting is computed on sRGB values, then converted to linear, to keep
// Galaxies' original look (its fixed-function lighting worked in sRGB).
shader_type spatial;
render_mode unshaded;

const int MAX_LIGHTS = 8;

uniform sampler2D albedo_texture : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec3 ambient = vec3(0.0);
uniform int parallel_count = 0;
uniform vec3 parallel_direction[MAX_LIGHTS];  // the direction the light travels
uniform vec3 parallel_color[MAX_LIGHTS];
uniform int point_count = 0;
uniform vec3 point_position[MAX_LIGHTS];
uniform vec3 point_color[MAX_LIGHTS];
uniform vec3 point_attenuation[MAX_LIGHTS];  // constant, linear, quadratic

varying vec3 building_position;
varying vec3 building_normal;

void vertex() {
	building_position = VERTEX;
	building_normal = NORMAL;
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
	vec3 srgb = clamp(texture(albedo_texture, UV).rgb * light, 0.0, 1.0);
	ALBEDO = mix(srgb / 12.92, pow((srgb + 0.055) / 1.055, vec3(2.4)), step(0.04045, srgb));
}
