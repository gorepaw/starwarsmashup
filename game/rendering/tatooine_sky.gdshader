shader_type sky;

uniform vec3 zenith_color : source_color = vec3(0.28, 0.50, 0.78);
uniform vec3 mid_sky_color : source_color = vec3(0.56, 0.70, 0.84);
uniform vec3 horizon_color : source_color = vec3(0.86, 0.77, 0.63);
uniform vec3 ground_color : source_color = vec3(0.72, 0.61, 0.46);
uniform vec3 sun_a_color : source_color = vec3(1.0, 0.96, 0.86);
uniform vec3 sun_b_color : source_color = vec3(1.0, 0.74, 0.45);

void sky() {
	vec3 dir = normalize(EYEDIR);
	float y = dir.y;
	vec3 col;
	if (y >= 0.0) {
		float h_t = pow(clamp(y, 0.0, 1.0), 0.38);
		float z_t = pow(clamp(y, 0.0, 1.0), 0.85);
		col = mix(horizon_color, mid_sky_color, h_t);
		col = mix(col, zenith_color, z_t);
	} else {
		col = mix(horizon_color, ground_color, clamp(-y * 3.0, 0.0, 1.0));
	}

	// Tatoo I (aligned with primary directional sun LIGHT0_DIRECTION)
	vec3 sun_a_dir = normalize(LIGHT0_DIRECTION);
	float cos_a = max(dot(dir, sun_a_dir), 0.0);
	float disc_a = smoothstep(0.99945, 0.99982, cos_a);
	float halo_a = pow(cos_a, 18.0) * 0.28 + pow(cos_a, 180.0) * 0.65;
	col += sun_a_color * (disc_a * 2.2 + halo_a);

	// Tatoo II (smaller warm-orange twin sun offset ~12 deg azimuth, ~7 deg lower)
	vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), sun_a_dir));
	vec3 up = normalize(cross(sun_a_dir, right));
	vec3 sun_b_dir = normalize(sun_a_dir + right * 0.21 - up * 0.12);
	float cos_b = max(dot(dir, sun_b_dir), 0.0);
	float disc_b = smoothstep(0.99968, 0.99990, cos_b);
	float halo_b = pow(cos_b, 26.0) * 0.18 + pow(cos_b, 240.0) * 0.45;
	col += sun_b_color * (disc_b * 1.8 + halo_b);

	COLOR = col;
}
