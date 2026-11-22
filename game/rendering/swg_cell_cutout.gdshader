// Galaxies interior cell lighting, cut out at alpha 0.5: see swg_cell.gdshaderinc.
shader_type spatial;
render_mode unshaded;
#define CELL_CUTOUT

#include "res://rendering/swg_cell.gdshaderinc"
