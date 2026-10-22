// Camundrum - X-ray dots
// Draws a body as a grid of dots wherever something stands between it and the camera, so it can always be found.

shader_type spatial;
render_mode unshaded, depth_test_disabled, depth_draw_never, cull_back, shadows_disabled;

// Look of the dots
uniform vec4 tint : source_color = vec4(1.0, 0.62, 0.22, 0.95);
uniform float spacing = 7.0;
uniform float dot_radius = 1.9;

// How much nearer an obstacle must be before the body counts as hidden behind it
uniform float hidden_margin = 0.0008;

// The depth of the scene already drawn, which holds whatever is in front of the body
uniform sampler2D scene_depth : hint_depth_texture, filter_nearest;

void fragment()
{
	// Where the body is not hidden, it is drawn normally, so nothing is added there
	// The depth buffer is reversed: a bigger value is nearer to the camera
	float drawn = texture(scene_depth, SCREEN_UV).r;
	if (drawn <= FRAGCOORD.z + hidden_margin) discard;

	// Where it is hidden, keep only round dots on a fixed grid on the screen
	vec2 cell = mod(FRAGCOORD.xy, spacing) - vec2(spacing * 0.5);
	if (length(cell) > dot_radius) discard;
	ALBEDO = tint.rgb;
	ALPHA = tint.a;
}
