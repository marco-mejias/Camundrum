
/* Camundrum - Camera Controller */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/camera3d.hpp>
#include <Godot/classes/rendering_server.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/aabb.hpp>
#include <Godot/classes/mesh_instance3d.hpp>
#include <Godot/classes/multi_mesh_instance3d.hpp>
#include <Godot/classes/geometry_instance3d.hpp>
#include <Godot/classes/viewport.hpp>
#include <Godot/classes/directional_light3d.hpp>
#include <Godot/core/object.hpp>

// C++ Runtime
#include <cmath>

// Game
#include "scripts/shared/GameTypes.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Framing, kept outside the script block because Jenova cannot read these parameter types inside it
static void GrowByPiece(Node* piece, AABB& box, bool& any)
{
	// Purpose: add what a piece draws to a box, measured in the world, through its meshes.

	// The board draws through Mesh, or through Tiles once its holes are cut; other pieces through Mesh
	const char* parts[2] = { "Mesh", "Tiles" };
	for (const char* name : parts)
	{
		GeometryInstance3D* drawn = (GeometryInstance3D*)piece->get_node_or_null(name);
		if (!drawn || !drawn->is_visible_in_tree()) continue;
		AABB part = drawn->get_global_transform().xform(drawn->get_aabb());
		box = any ? box.merge(part) : part;
		any = true;
	}
}
static void GrowByLevel(Node* parent, AABB& box, bool& any)
{
	// Purpose: add the shape of a level to a box: its boards and every fixed piece, looking inside turning layers too.
	// Moving pieces such as the player and crates, and effects such as rivers, are left out, so the frame does not depend on them.

	// Look at each child
	for (int index = 0; index < parent->get_child_count(); index++)
	{
		Node* child = parent->get_child(index);
		bool board = String(child->get_name()) == "Board";
		bool fixed_piece = child->is_in_group("depth") && child->is_class("StaticBody3D");
		if (board || fixed_piece) GrowByPiece(child, box, any);
		if (child->is_in_group("carrier")) GrowByLevel(child, box, any);
	}
}
static void FitPose(Camera3D* pose, const Transform3D& authored, double authored_size, const AABB& box, float aspect, float margin)
{
	// Purpose: aim one view's camera at the middle of the level and open it enough to show all of it.
	// The camera keeps the angle it was given in the editor and never shows less than it was set to, so a level of the usual size looks as before.

	// Measure the level across the camera's screen, from the camera's own place
	Vector3 right = authored.basis.get_column(0).normalized();
	Vector3 up = authored.basis.get_column(1).normalized();
	float low_x = 1e9f, high_x = -1e9f, low_y = 1e9f, high_y = -1e9f;
	for (int corner = 0; corner < 8; corner++)
	{
		Vector3 offset = box.get_endpoint(corner) - authored.origin;
		float x = offset.dot(right);
		float y = offset.dot(up);
		low_x = fminf(low_x, x);
		high_x = fmaxf(high_x, x);
		low_y = fminf(low_y, y);
		high_y = fmaxf(high_y, y);
	}

	// Slide the camera across its screen so the level is in the middle, keeping its distance
	Vector3 center = authored.origin + right * ((low_x + high_x) * 0.5f) + up * ((low_y + high_y) * 0.5f);
	pose->set_global_transform(Transform3D(authored.basis, center));

	// Open it so the level fits both ways with some room around it, but never closer than it was set
	float needed = fmaxf(high_y - low_y, (high_x - low_x) / (aspect > 0.1f ? aspect : 1.0f)) * margin;
	pose->set_size(needed > (float)authored_size ? needed : (float)authored_size);
}

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties
JENOVA_PROPERTY(double, transition_time, 0.35)
JENOVA_PROPERTY(double, frame_margin, 1.15)
JENOVA_PROPERTY(Color, side_background, Color(0.13, 0.15, 0.22))
JENOVA_PROPERTY(Color, top_background, Color(0.23, 0.17, 0.13))
JENOVA_PROPERTY(Color, iso_background, Color(0.11, 0.2, 0.19))

// State
Node* controller = nullptr;
Camera3D* camera = nullptr;
Camera3D* poses[VIEW_COUNT] = { nullptr, nullptr, nullptr };
Transform3D from_transform;
Transform3D to_transform;
double from_size = 1.0;
double to_size = 1.0;
double progress = 1.0;
int target_view = VIEW_SIDE;
int from_view = VIEW_SIDE;
Color view_colors[VIEW_COUNT];
Color from_color;
Color to_color;
Color shown_color;
Transform3D rest_transform;
double shake_strength = 0.0;
double shake_length = 0.0;
double shake_left = 0.0;
double shake_clock = 0.0;
Transform3D authored_poses[VIEW_COUNT];
double authored_sizes[VIEW_COUNT] = { 1.0, 1.0, 1.0 };
double margin_copy = 1.15;
double shake_scale = 1.0;
uint64_t light_id = 0;
Basis light_rest;

// Blending
void ApplyBlend(double amount)
{
	// Purpose: place the camera somewhere between the starting pose and the target pose, 0 is start and 1 is target.

	// Ease the amount so the move starts and ends gently
	double eased = amount * amount * (3.0 - 2.0 * amount);

	// Blend the position in a straight line and the rotation along the shortest turn
	Vector3 origin = from_transform.origin.lerp(to_transform.origin, eased);
	Quaternion from_rotation = from_transform.basis.get_rotation_quaternion();
	Quaternion to_rotation = to_transform.basis.get_rotation_quaternion();
	Quaternion rotation = from_rotation.slerp(to_rotation, eased);

	// Give the camera its blended pose and zoom, and remember the pose so a shake can return to it
	// Halfway through the zoom dips a little, a small punch that makes the switch feel like a movement
	rest_transform = Transform3D(Basis(rotation), origin);
	camera->set_global_transform(rest_transform);
	double punch = 1.0 - 0.05 * sin(amount * 3.14159265358979);
	camera->set_size((from_size + (to_size - from_size) * eased) * punch);

	// Blend the background colour behind the level the same way, so each view has its own
	shown_color = from_color.lerp(to_color, eased);
	RenderingServer::get_singleton()->set_default_clear_color(shown_color);

	// In top the level is seen from above, so the light comes from over the camera
	double from_top = from_view == VIEW_TOP ? 1.0 : 0.0;
	double to_top = target_view == VIEW_TOP ? 1.0 : 0.0;
	double top = from_top + (to_top - from_top) * eased;
	DirectionalLight3D* light = (DirectionalLight3D*)ObjectDB::get_instance(light_id);
	if (light)
	{
		Basis overhead = Basis::looking_at(Vector3(-0.3f, -0.35f, -0.89f), Vector3(0.0f, 1.0f, 0.0f));
		Quaternion rest = light_rest.get_rotation_quaternion();
		light->set_basis(Basis(rest.slerp(overhead.get_rotation_quaternion(), (float)top)));
	}
}

// Commands
void FrameLevel(Node* level)
{
	// Purpose: let the GameManager fit the three views to a new level, so a level of any size is seen whole.
	// A level can ask for more or less room around it with the camera_margin metadata on its root.

	// Start again from the poses set in the editor
	for (int view = 0; view < VIEW_COUNT; view++)
	{
		if (!poses[view]) continue;
		poses[view]->set_global_transform(authored_poses[view]);
		poses[view]->set_size(authored_sizes[view]);
	}

	// Keep the new level's light, so top can light it from above
	light_id = 0;
	if (level)
	{
		DirectionalLight3D* light = (DirectionalLight3D*)level->get_node_or_null("Light");
		if (light && light->is_class("DirectionalLight3D"))
		{
			light_id = light->get_instance_id();
			light_rest = light->get_basis();
		}
	}

	// Measure the level; with nothing to measure the editor poses stay
	if (!level || !controller) return;
	AABB box;
	bool any = false;
	GrowByLevel(level, box, any);
	if (!any) return;

	// Fit each view, using the screen's shape so the level fits across as well as up and down
	Vector2 screen = controller->get_viewport()->get_visible_rect().size;
	float aspect = screen.y > 0.0f ? screen.x / screen.y : 1.7777f;
	float margin = (float)(double)level->get_meta("camera_margin", margin_copy);
	for (int view = 0; view < VIEW_COUNT; view++)
	{
		if (poses[view]) FitPose(poses[view], authored_poses[view], authored_sizes[view], box, aspect, margin);
	}
}
void SnapTo(int view)
{
	// Purpose: let the GameManager put the camera on a view instantly, with no animation.

	// Ignore the call if the view does not exist or the nodes are missing
	if (view < 0 || view >= VIEW_COUNT || !camera || !poses[view]) return;

	// Make the target pose both the start and the end, then mark the move as finished
	to_transform = poses[view]->get_global_transform();
	to_size = poses[view]->get_size();
	from_transform = to_transform;
	from_size = to_size;
	to_color = view_colors[view];
	from_color = to_color;
	target_view = view;
	from_view = view;
	progress = 1.0;

	// Place the camera on it
	ApplyBlend(1.0);
}
void TransitionTo(int view)
{
	// Purpose: let the GameManager start an animated move of the camera to a view.

	// Ignore the call if the view does not exist or the nodes are missing
	if (view < 0 || view >= VIEW_COUNT || !camera || !poses[view]) return;

	// Start from wherever the camera is right now, even in the middle of another move
	from_transform = camera->get_global_transform();
	from_size = camera->get_size();
	from_color = shown_color;
	from_view = target_view;

	// Aim at the pose and the background colour of the requested view
	to_transform = poses[view]->get_global_transform();
	to_size = poses[view]->get_size();
	to_color = view_colors[view];
	target_view = view;

	// Restart the move, the per-frame routine carries it out
	progress = 0.0;
}
void SetShakeScale(double amount)
{
	// Purpose: let the GameManager apply the player's screen shake setting, 0 is none and 1 is full.

	// Keep it for every shake from now on
	shake_scale = amount < 0.0 ? 0.0 : amount;
}
void Shake(double strength, double seconds)
{
	// Purpose: let the GameManager shake the camera for a moment, used when the player dies.

	// Start a shake that fades out over the given time, as strong as the player's setting allows
	shake_strength = strength * shake_scale;
	shake_length = seconds;
	shake_left = seconds;
	shake_clock = 0.0;
}

// Shaking
void ApplyShake(double delta)
{
	// Purpose: move the camera a little around its resting pose while a shake lasts.

	// Count the shake down, and settle the camera back on its pose when it ends
	shake_left -= delta;
	shake_clock += delta;
	if (shake_left <= 0.0)
	{
		shake_left = 0.0;
		camera->set_global_transform(rest_transform);
		return;
	}

	// Wobble sideways and up and down across the screen, weaker as the shake runs out
	double weight = shake_strength * (shake_left / shake_length);
	Vector3 offset = rest_transform.basis.get_column(0) * (float)(sin(shake_clock * 71.0) * weight);
	offset += rest_transform.basis.get_column(1) * (float)(cos(shake_clock * 53.0) * weight);
	camera->set_global_transform(Transform3D(rest_transform.basis, rest_transform.origin + offset));
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node and its children are in the tree, finds the camera and the three poses.

	// Keep this node and create the signal it emits when a move ends, it carries the view reached
	controller = GetSelf<Node>(instance);
	if (!controller->has_signal("transition_finished")) controller->add_user_signal("transition_finished");

	// Find the camera that renders the game
	camera = (Camera3D*)controller->get_node_or_null("Camera");

	// Find the pose of each view, in the same order as the ViewMode values
	poses[VIEW_SIDE] = (Camera3D*)controller->get_node_or_null("SidePose");
	poses[VIEW_TOP] = (Camera3D*)controller->get_node_or_null("TopPose");
	poses[VIEW_ISO] = (Camera3D*)controller->get_node_or_null("IsoPose");

	// Remember the poses as set in the editor, every level is framed starting from them
	for (int view = 0; view < VIEW_COUNT; view++)
	{
		if (!poses[view]) continue;
		authored_poses[view] = poses[view]->get_global_transform();
		authored_sizes[view] = poses[view]->get_size();
	}
	margin_copy = frame_margin;

	// Copy the background colours now, because properties may not be up to date inside calls made by other modules
	view_colors[VIEW_SIDE] = side_background;
	view_colors[VIEW_TOP] = top_background;
	view_colors[VIEW_ISO] = iso_background;
}
void OnDestroy(Caller* instance)
{
	// Purpose: called when the node leaves the scene tree, drops every node reference.

	// Forget the nodes so nothing uses them after they are freed
	controller = nullptr;
	camera = nullptr;
	for (int view = 0; view < VIEW_COUNT; view++) poses[view] = nullptr;

	// Leave no move in progress
	progress = 1.0;
}
void OnProcess(Caller* instance, double _delta)
{
	// Purpose: called every frame, advances the camera move while one is in progress.

	// Do nothing without a camera
	if (!camera) return;

	// Advance a move in progress, finishing at once if the duration is zero
	if (progress < 1.0)
	{
		if (transition_time > 0.0) progress += _delta / transition_time;
		else progress = 1.0;
		if (progress > 1.0) progress = 1.0;

		// Place the camera for the new progress, and tell the GameManager when it has arrived
		ApplyBlend(progress);
		if (progress >= 1.0) controller->emit_signal("transition_finished", target_view);
	}

	// Shake on top of the pose while a shake lasts
	if (shake_left > 0.0) ApplyShake(_delta);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
