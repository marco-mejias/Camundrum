// Camundrum - Block look
// Gives every box a cleaner, deeper look: soft bevelled edges that catch the light,
// thin tile seams every unit so sizes read at a glance, and a darker base so height reads too.
// Works on a BoxMesh scaled by its node, measuring everything in world units.
shader_type spatial;

uniform vec4 albedo : source_color = vec4(0.66, 0.68, 0.73, 1.0);
uniform float edge_light = 0.35;   // How much brighter the bevelled edges are
uniform float edge_width = 0.07;   // Width of that bevel, in world units
uniform float seam_dark = 0.16;    // How dark the tile seams are
uniform float base_dark = 0.3;     // How dark the bottom of a box gets
uniform vec3 mesh_size = vec3(1.0); // Size of the BoxMesh itself, for crates that are not one unit

varying vec3 box_pos;      // Position inside the box, 0 to its size on each axis
varying vec3 box_size;     // Size of the box in world units
varying vec3 box_normal;   // Which face this is, in the box's own axes
varying float world_y;     // Height in the world, for the darker base

void vertex()
{
	// The box's size is the mesh size stretched by the node's scale
	vec3 scale = vec3(length(MODEL_MATRIX[0].xyz), length(MODEL_MATRIX[1].xyz), length(MODEL_MATRIX[2].xyz));
	box_size = mesh_size * scale;
	box_pos = VERTEX * scale + box_size * 0.5;
	box_normal = NORMAL;
	world_y = (MODEL_MATRIX * vec4(VERTEX, 1.0)).y;
}

// Distance to the nearest edge along the axes that lie on this face
float face_distance(vec3 distances, vec3 normal)
{
	float nearest = 1000.0;
	if (abs(normal.x) < 0.5) nearest = min(nearest, distances.x);
	if (abs(normal.y) < 0.5) nearest = min(nearest, distances.y);
	if (abs(normal.z) < 0.5) nearest = min(nearest, distances.z);
	return nearest;
}

void fragment()
{
	vec3 color = albedo.rgb;

	// 1. Tile seams: a thin dark line every unit across the face
	vec3 cell = fract(box_pos);
	float seam = 1.0 - smoothstep(0.0, 0.025, face_distance(min(cell, 1.0 - cell), box_normal));
	color *= 1.0 - seam * seam_dark;

	// 2. Bevel: the outer edges of the box are lighter, like a rounded corner catching the light
	float edge = 1.0 - smoothstep(0.0, edge_width, face_distance(min(box_pos, box_size - box_pos), box_normal));
	color = mix(color, min(color * 1.25 + 0.08, vec3(1.0)), edge * edge_light);

	// 3. Base: the lower part of a box fades darker, so tall pieces read as tall
	color *= mix(1.0 - base_dark, 1.0, smoothstep(-1.0, 1.0, world_y));

	ALBEDO = color;
	ROUGHNESS = 0.75;
}
