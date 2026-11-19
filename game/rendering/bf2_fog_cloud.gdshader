// Battlefront II fog cloud (Effect("FogCloud"), from bf2tool export-map's
// environment): big soft sprites drifting at a height in a ring around the
// camera, where the view fades into the fog (Dagobah: 60 to 100 m, the scene
// being gone by 80), so the edge of sight is fog banks rather than a flat
// wall. They aren't fogged themselves. ASSUMPTION: Range is the ring's
// inner and outer radius.
//
// Each instance's custom data is its seed: where it sits in a box that
// follows the camera (x, z), its height offset (y) and its turn (w). The
// sprites stay put in the world as the camera moves (bar their drift) and
// wrap around the box. They fade in and out at the ring's edges, and where
// they meet the ground and trees (soft particles).
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled, skip_vertex_transform, fog_disabled;

uniform sampler2D cloud_texture : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;
uniform vec4 color : source_color = vec4(0.66, 0.67, 0.71, 0.5);
uniform vec2 range = vec2(60.0, 100.0);   // the ring's inner and outer radius (meters)
uniform vec2 velocity = vec2(0.0);        // drift, meters per second (x, z)
uniform float spin = 0.1;                 // radians per second
uniform float height = 16.0;              // the layer's middle
uniform float spread = 6.0;               // up and down from it
uniform float size = 28.0;                // sprite width (meters)
uniform float soft = 4.0;                 // meters over which a sprite fades into what's behind it

varying float fade;

void vertex() {
	vec3 camera = INV_VIEW_MATRIX[3].xyz;
	float box = 2.0 * range.y;
	vec2 anchored = INSTANCE_CUSTOM.xz * box + velocity * TIME;
	vec2 around = mod(anchored - camera.xz + 0.5 * box, box) - 0.5 * box;
	vec3 center = vec3(camera.x + around.x, height + (INSTANCE_CUSTOM.y - 0.5) * 2.0 * spread, camera.z + around.y);

	float angle = INSTANCE_CUSTOM.w * 6.2832 + spin * TIME * (INSTANCE_CUSTOM.x > 0.5 ? 1.0 : -1.0);
	vec2 corner = mat2(vec2(cos(angle), sin(angle)), vec2(-sin(angle), cos(angle))) * VERTEX.xy * size;
	vec3 view_center = (VIEW_MATRIX * vec4(center, 1.0)).xyz;
	VERTEX = view_center + vec3(corner, 0.0);
	NORMAL = vec3(0.0, 0.0, 1.0);

	float distance = length(around);
	fade = smoothstep(0.7 * range.x, range.x, distance) * (1.0 - smoothstep(0.85 * range.y, range.y, distance));
}

void fragment() {
	vec4 cloud = texture(cloud_texture, UV);
	float scene_depth = texture(depth_texture, SCREEN_UV).r;
	vec4 scene = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, scene_depth, 1.0);
	float behind = -scene.z / scene.w - (-VERTEX.z);
	ALBEDO = color.rgb * cloud.rgb;
	ALPHA = cloud.a * color.a * fade * clamp(behind / soft, 0.0, 1.0);
}
