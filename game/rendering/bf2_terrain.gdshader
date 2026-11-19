// Battlefront II terrain (bf2tool export-map): up to eight texture layers,
// blended by splat maps (RGBA: four layers' weights per terrain sample),
// times the map's baked lighting. Unshaded: the baked lighting already holds
// the sun and shadows. The mesh must be built at world coordinates (Godot
// x = -Battlefront x), with an identity transform.
shader_type spatial;
render_mode unshaded;

const int MAX_LAYERS = 8;

uniform sampler2D splat0 : filter_linear, repeat_disable;
uniform sampler2D splat1 : filter_linear, repeat_disable;
uniform sampler2D light_map : source_color, filter_linear, repeat_disable;
uniform sampler2DArray layers : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float layer_scale[MAX_LAYERS];  // meters per texture repeat (ASSUMPTION)
uniform int layer_count;
uniform vec2 map_origin;  // Battlefront (x, z) of sample (0, 0)
uniform float map_spacing;
uniform int map_samples;
uniform float light_scale = 1.0;

varying vec2 bf_xz;

void vertex() {
	bf_xz = vec2(-VERTEX.x, VERTEX.z);
}

void fragment() {
	vec2 uv = ((bf_xz - map_origin) / map_spacing + 0.5) / float(map_samples);
	vec4 a = texture(splat0, uv);
	vec4 b = texture(splat1, uv);
	float weights[MAX_LAYERS] = {a.r, a.g, a.b, a.a, b.r, b.g, b.b, b.a};
	vec3 color = vec3(0.0);
	float total = 0.0;
	for (int i = 0; i < layer_count; i++) {
		if (weights[i] > 0.002) {
			color += texture(layers, vec3(bf_xz / layer_scale[i], float(i))).rgb * weights[i];
			total += weights[i];
		}
	}
	color /= max(total, 0.001);
	ALBEDO = color * texture(light_map, uv).rgb * light_scale;
}
