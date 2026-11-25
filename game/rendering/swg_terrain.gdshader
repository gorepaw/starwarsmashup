// Galaxies ground textures on a baked terrain region (swgtool bake-terrain).
// Each bake sample has one texture family; the four samples around a point
// are blended bilinearly. The mesh must be built at world coordinates
// (Godot x = -SWG x), with an identity transform.
shader_type spatial;

const int MAX_FAMILIES = 32;

uniform sampler2D family_map : filter_nearest, repeat_disable;  // R: family index / 255 (255: none)
uniform sampler2DArray family_textures : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float family_size[MAX_FAMILIES];  // meters per texture repeat
uniform vec2 map_origin;                  // SWG (x, z) of sample (0, 0)
uniform float map_spacing;
uniform int map_samples;
// Where the terrain is taken away (a city's rooms: core::TerrainHoles), game x z.
// A planet's towns are a patch each (Stage 13): up to 8.
const int MAX_HOLE_PATCHES = 8;
uniform sampler2D holes : filter_nearest, repeat_disable;  // R > 0: a hole
uniform sampler2D holes_1 : filter_nearest, repeat_disable;
uniform sampler2D holes_2 : filter_nearest, repeat_disable;
uniform sampler2D holes_3 : filter_nearest, repeat_disable;
uniform sampler2D holes_4 : filter_nearest, repeat_disable;
uniform sampler2D holes_5 : filter_nearest, repeat_disable;
uniform sampler2D holes_6 : filter_nearest, repeat_disable;
uniform sampler2D holes_7 : filter_nearest, repeat_disable;
uniform bool has_holes = false;
uniform int holes_count = 1;
uniform vec2 holes_origin;  // game (x, z) of cell (0, 0)'s corner (the first patch)
uniform vec2 holes_size;    // in meters
uniform vec4 holes_rects[MAX_HOLE_PATCHES];  // the others': origin (x, z), size (x, z)

varying vec2 swg_xz;
varying vec2 game_xz;
varying vec3 world_pos;

void vertex() {
	swg_xz = vec2(-VERTEX.x, VERTEX.z);
	game_xz = VERTEX.xz;
	world_pos = VERTEX;
}

vec3 family_color(ivec2 sample_index, float macro_blend) {
	ivec2 s = clamp(sample_index, ivec2(0), ivec2(map_samples - 1));
	float index = texelFetch(family_map, s, 0).r * 255.0;
	if (index > 254.5) {
		return vec3(0.5);
	}
	int family = int(index + 0.5);
	vec3 detail = texture(family_textures, vec3(swg_xz / family_size[family], float(family))).rgb;
	if (macro_blend <= 0.001) {
		return detail;
	}
	vec3 macro = texture(family_textures, vec3(swg_xz / (family_size[family] * 8.0), float(family))).rgb;
	return mix(detail, macro, macro_blend);
}

bool in_hole(sampler2D patch, vec2 origin, vec2 size, vec2 xz) {
	vec2 h = (xz - origin) / size;
	return all(greaterThanEqual(h, vec2(0.0))) && all(lessThan(h, vec2(1.0))) && texture(patch, h).r > 0.001;
}

void fragment() {
	if (has_holes) {
		if (in_hole(holes, holes_origin, holes_size, game_xz)
				|| (holes_count > 1 && in_hole(holes_1, holes_rects[1].xy, holes_rects[1].zw, game_xz))
				|| (holes_count > 2 && in_hole(holes_2, holes_rects[2].xy, holes_rects[2].zw, game_xz))
				|| (holes_count > 3 && in_hole(holes_3, holes_rects[3].xy, holes_rects[3].zw, game_xz))
				|| (holes_count > 4 && in_hole(holes_4, holes_rects[4].xy, holes_rects[4].zw, game_xz))
				|| (holes_count > 5 && in_hole(holes_5, holes_rects[5].xy, holes_rects[5].zw, game_xz))
				|| (holes_count > 6 && in_hole(holes_6, holes_rects[6].xy, holes_rects[6].zw, game_xz))
				|| (holes_count > 7 && in_hole(holes_7, holes_rects[7].xy, holes_rects[7].zw, game_xz))) {
			discard;
		}
	}
	float dist = length(CAMERA_POSITION_WORLD - world_pos);
	float macro_blend = smoothstep(350.0, 2000.0, dist) * 0.65;
	vec2 grid = (swg_xz - map_origin) / map_spacing;
	ivec2 base = ivec2(floor(grid));
	vec2 f = fract(grid);
	vec3 bottom = mix(family_color(base, macro_blend), family_color(base + ivec2(1, 0), macro_blend), f.x);
	vec3 top = mix(family_color(base + ivec2(0, 1), macro_blend), family_color(base + ivec2(1, 1), macro_blend), f.x);
	ALBEDO = mix(bottom, top, f.y);
	ROUGHNESS = 0.95;
}
