// Battlefront II water (bf2tool export-map's "water"): flat surfaces whose
// look comes from the map's Effect("Water"). The main texture (murky, with
// its own opacity) scrolls over a body tinted by the refraction color;
// animated normal maps ripple it. It reflects the sky (the map's fog color
// at the horizon, darker overhead) tinted by the reflection color, more so
// at grazing angles (fresnel). Unshaded like the terrain, with a glint of
// the sun. Shallow water fades out at its edges, so the shore has no hard
// line.
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_back;

uniform sampler2D main_texture : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2DArray normal_maps : filter_linear_mipmap, repeat_enable;
uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;
uniform int normal_frames = 0;
uniform float normal_rate = 8.0;       // frames per second
uniform float repeat_meters = 8.0;     // main texture: meters per repeat
uniform float normal_meters = 4.0;     // normal maps: meters per repeat
uniform vec2 velocity = vec2(0.0);     // main texture scrolling, repeats per second
uniform vec3 refraction_color : source_color = vec3(0.4, 0.53, 0.55);
uniform vec3 reflection_color : source_color = vec3(0.59);
uniform vec2 fresnel = vec2(0.2, 0.7); // reflection's share, facing and grazing
uniform vec3 sun_direction = vec3(0.0, -1.0, 0.0);  // the way the light travels
uniform vec3 sun_color : source_color = vec3(1.0);
uniform vec3 sky_color : source_color = vec3(0.37, 0.44, 0.49);
uniform float edge_fade = 0.6;         // meters of depth over which the shore fades in

varying vec3 world_position;

void vertex() {
	world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec2 xz = world_position.xz;
	vec4 murk = texture(main_texture, xz / repeat_meters + velocity * TIME);

	// Ripples: two normal map frames blended, sampled twice at different
	// scales and directions so the tiling doesn't show.
	vec3 ripple = vec3(0.0, 0.0, 1.0);
	if (normal_frames > 0) {
		float frame = TIME * normal_rate;
		float a = mod(floor(frame), float(normal_frames));
		float b = mod(a + 1.0, float(normal_frames));
		float t = fract(frame);
		vec2 uv1 = xz / normal_meters;
		vec2 uv2 = xz.yx / (normal_meters * 2.7) + vec2(0.37, 0.71);
		vec3 n1 = mix(texture(normal_maps, vec3(uv1, a)).xyz, texture(normal_maps, vec3(uv1, b)).xyz, t);
		vec3 n2 = mix(texture(normal_maps, vec3(uv2, a)).xyz, texture(normal_maps, vec3(uv2, b)).xyz, t);
		ripple = normalize((n1 + n2) - 1.0);
	}
	// Tangent space (x along world x, y along world z) to world: up is y.
	vec3 normal_world = normalize(vec3(ripple.x * 0.35, 1.0, ripple.y * 0.35));
	vec3 normal_view = normalize((VIEW_MATRIX * vec4(normal_world, 0.0)).xyz);

	float facing = clamp(dot(normal_view, VIEW), 0.0, 1.0);
	float reflect_share = mix(fresnel.x, fresnel.y, pow(1.0 - facing, 3.0));
	vec3 view_world = normalize((INV_VIEW_MATRIX * vec4(VIEW, 0.0)).xyz);
	vec3 reflected = reflect(-view_world, normal_world);
	vec3 sky = mix(sky_color, sky_color * 0.5, clamp(reflected.y * 1.5, 0.0, 1.0)) * reflection_color * 1.5;
	vec3 body = mix(refraction_color * 0.3, murk.rgb, murk.a * 0.8);
	vec3 color = mix(body, sky, reflect_share);

	// The sun's glint.
	vec3 to_sun = normalize((VIEW_MATRIX * vec4(-sun_direction, 0.0)).xyz);
	vec3 halfway = normalize(to_sun + VIEW);
	color += sun_color * pow(max(dot(normal_view, halfway), 0.0), 120.0) * 0.6;

	// Depth of water under this point: thin at the shore.
	float scene_depth = texture(depth_texture, SCREEN_UV).r;
	vec4 scene = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, scene_depth, 1.0);
	float below_distance = -scene.z / scene.w - (-VERTEX.z);
	float shore = clamp(below_distance / edge_fade, 0.0, 1.0);

	ALBEDO = color;
	ALPHA = clamp(mix(0.6, 0.95, reflect_share) * shore, 0.0, 1.0);
}
