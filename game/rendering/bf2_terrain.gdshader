// Battlefront II terrain (bf2tool export-map): up to eight texture layers,
// blended by splat maps (RGBA: four layers' weights per terrain sample),
// times the map's baked lighting, with holes where it's cut away (its
// terrain cutters: the core's TerrainCut). Unshaded: the baked lighting already holds
// the sun and shadows. The mesh must be built at world coordinates (Godot
// x = -Battlefront x), with an identity transform.
shader_type spatial;
render_mode unshaded;

#include "res://rendering/bf2_terrain.gdshaderinc"
