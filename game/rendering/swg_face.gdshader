// A Galaxies texture renderer's texture (a face: its skin, highlights,
// brows, makeup, freckles, a Zabrak's tattoos), composited as it's drawn
// (see SwgColors): each layer its texture times its color, within its
// rectangle of the face's texture, copied (replacing what's under it) or
// blended over it by its alpha, in order, as Galaxies' texture renderers do.
shader_type spatial;

const int MAX_LAYERS = 8;

uniform int layer_count = 0;
uniform sampler2D layer_textures[MAX_LAYERS] : source_color, filter_linear_mipmap, repeat_disable;
uniform vec4 layer_rects[MAX_LAYERS];   // x0, y0, x1, y1 of the face's texture (u, v)
uniform vec4 layer_uvs[MAX_LAYERS];     // the layer texture's u, v at (x0, y0) and (x1, y1)
uniform vec3 layer_colors[MAX_LAYERS];  // linear
uniform int layer_blends[MAX_LAYERS];   // 1: blended by its alpha; 0: copied
uniform float roughness = 1.0;
uniform float metallic = 0.0;

vec3 draw_layer(vec3 under, vec2 uv, int i, vec4 texel) {
	vec4 rect = layer_rects[i];
	bool inside = uv.x >= rect.x && uv.x <= rect.z && uv.y >= rect.y && uv.y <= rect.w;
	if (i >= layer_count || !inside) {
		return under;
	}
	vec3 color = texel.rgb * layer_colors[i];
	return layer_blends[i] == 1 ? mix(under, color, texel.a) : color;
}

// Where in a layer's texture this point of the face's falls.
vec2 layer_uv(vec2 uv, int i) {
	vec4 rect = layer_rects[i];
	vec2 t = (uv - rect.xy) / max(rect.zw - rect.xy, vec2(1e-6));
	return mix(layer_uvs[i].xy, layer_uvs[i].zw, t);
}

void fragment() {
	// Every layer's texture is sampled (constant indices, and smooth
	// derivatives across a layer's edge), then used where it lies.
	vec3 color = vec3(1.0);
	color = draw_layer(color, UV, 0, texture(layer_textures[0], layer_uv(UV, 0)));
	color = draw_layer(color, UV, 1, texture(layer_textures[1], layer_uv(UV, 1)));
	color = draw_layer(color, UV, 2, texture(layer_textures[2], layer_uv(UV, 2)));
	color = draw_layer(color, UV, 3, texture(layer_textures[3], layer_uv(UV, 3)));
	color = draw_layer(color, UV, 4, texture(layer_textures[4], layer_uv(UV, 4)));
	color = draw_layer(color, UV, 5, texture(layer_textures[5], layer_uv(UV, 5)));
	color = draw_layer(color, UV, 6, texture(layer_textures[6], layer_uv(UV, 6)));
	color = draw_layer(color, UV, 7, texture(layer_textures[7], layer_uv(UV, 7)));
	ALBEDO = color;
	ROUGHNESS = roughness;
	METALLIC = metallic;
}
