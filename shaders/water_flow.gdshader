// Camundrum - Flowing water
// Light bands slide along each water segment in the direction it flows, so the current can be read at a glance.

shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back;

// How far the water has flowed, set by the water segment every frame; it stops growing while the water is still
instance uniform float phase = 0.0;

// Colours of the water and of its light bands
uniform vec4 deep_color : source_color = vec4(0.2, 0.47, 0.88, 0.78);
uniform vec4 light_color : source_color = vec4(0.72, 0.88, 1.0, 0.92);

// Distance along the flow and across it, in world units, so bands keep their size on segments of any length
varying float along;
varying float across;

void vertex()
{
	// The segment's own X axis is the direction it flows, its Z axis runs across it
	vec3 world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	along = dot(world, normalize(MODEL_MATRIX[0].xyz));
	across = dot(world, normalize(MODEL_MATRIX[2].xyz));
}

void fragment()
{
	// Wide soft bands and thin quick ripples, both carried along by the flow, slightly wavy across it
	float flow = along - phase;
	float bands = smoothstep(0.6, 1.0, sin(flow * 2.6 + sin(across * 4.0) * 0.9) * 0.5 + 0.5);
	float ripples = smoothstep(0.82, 1.0, sin(flow * 6.5 + across * 9.0) * 0.5 + 0.5) * 0.6;
	vec4 color = mix(deep_color, light_color, clamp(bands + ripples, 0.0, 1.0));
	ALBEDO = color.rgb;
	ALPHA = color.a;
	ROUGHNESS = 0.15;
	SPECULAR = 0.6;
}
