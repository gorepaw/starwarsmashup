// The body of the Battlefront II terrain shaders (bf2_terrain.gdshader,
// bf2_terrain_sunlit.gdshader).

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
// A row per cut: (center x, center z, radius, plane count), then its planes
// (nx, ny, nz, d), game space; inside when n . p + d <= 0 for every one.
uniform sampler2D cuts : filter_nearest, repeat_disable;
uniform int cut_count = 0;

varying vec2 bf_xz;
varying vec3 game_position;

void vertex() {
	bf_xz = vec2(-VERTEX.x, VERTEX.z);
	game_position = VERTEX;
}

void fragment() {
	for (int c = 0; c < cut_count; c++) {
		vec4 head = texelFetch(cuts, ivec2(0, c), 0);
		vec2 d = game_position.xz - head.xy;
		if (dot(d, d) > head.z * head.z) {
			continue;
		}
		bool inside = true;
		for (int i = 0; i < int(head.w); i++) {
			vec4 plane = texelFetch(cuts, ivec2(1 + i, c), 0);
			if (dot(plane.xyz, game_position) + plane.w > 0.0) {
				inside = false;
				break;
			}
		}
		if (inside) {
			discard;
		}
	}
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
	vec3 baked = texture(light_map, uv).rgb * light_scale;
#ifdef SUNLIT
	// The sun (a Godot light, with shadows) on the textures; the baked
	// lighting adds to it.
	ALBEDO = color;
	EMISSION = color * baked;
#else
	ALBEDO = color * baked;
#endif
}
