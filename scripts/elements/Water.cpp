
/* Camundrum - Water Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/area3d.hpp>
#include <Godot/classes/geometry_instance3d.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/typed_array.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every segment
JENOVA_PROPERTY(double, flow_speed, 3.5)
JENOVA_PROPERTY(bool, on_board, false)

// The state of each water segment lives in its node's metadata: flow_speed, on_board, view, frozen, clock

// Flow
Vector3 CurrentForView(Area3D* water)
{
	// Purpose: work out the current this segment applies in the active view.

	// The water is still in top view
	int view = water->get_meta("view", VIEW_SIDE);
	if (view == VIEW_TOP) return Vector3(0.0f, 0.0f, 0.0f);

	// The segment flows along its own X axis, so rotating it in the editor points the river
	Vector3 current = water->get_global_transform().basis.get_column(0).normalized() * (float)(double)water->get_meta("flow_speed", 3.5);

	// Side view only keeps the part of the flow that lies across the board
	if (view == VIEW_SIDE) current.z = 0.0f;
	return current;
}

// Contract
void OnViewChanged(Caller* instance, int view)
{
	// Purpose: part of the element contract, tells the water which view is active.

	// Remember the view, it decides how the water flows
	GetSelf<Area3D>(instance)->set_meta("view", view);
}
void SetFrozen(Caller* instance, int frozen)
{
	// Purpose: part of the element contract, stops the water while the level changes shape.

	// Store the state
	GetSelf<Area3D>(instance)->set_meta("frozen", frozen != 0);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node is in the tree, prepares the water.

	// Copy the inspector settings onto the node, where every function can read them safely
	auto water = GetSelf<Area3D>(instance);
	water->set_meta("flow_speed", flow_speed);
	water->set_meta("on_board", on_board);

	// Start in the view the game normally begins in, and only notice actors such as the player and crates
	water->set_meta("view", VIEW_SIDE);
	water->set_meta("frozen", false);
	water->set_meta("clock", 0.0);
	water->set_collision_mask(LAYER_ACTORS);
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, carries everything inside the water along the current.

	// Nothing is carried while the level changes shape, or by a part of the river the view hides
	auto water = GetSelf<Area3D>(instance);
	if ((bool)water->get_meta("frozen", false) || !water->is_visible_in_tree()) return;

	// Nothing is carried when the water is still
	Vector3 current = CurrentForView(water);
	if (current.length() < 0.001f) return;

	// The water's own box, as placed in the world right now
	AABB stream;
	if (!ShapeWorldBox((CollisionShape3D*)water->get_node_or_null("Trigger"), 1.0f, stream)) return;

	// A stretch that runs over an edge carries a little past its end, so what it carries gets clear of the ground
	double overshoot = water->get_meta("overshoot", 0.0);
	if (overshoot > 0.0)
	{
		Vector3 ahead = water->get_global_transform().basis.get_column(0).normalized() * (float)overshoot;
		AABB moved = stream;
		moved.position += ahead;
		stream = stream.merge(moved);
	}

	// Hand the current to the player and every crate inside it, found by comparing boxes
	// The physics engine does not always list a body that stands still inside an area, so its list is not used
	bool reaches_wall_only = (bool)water->get_meta("on_board", false) && (int)water->get_meta("view", VIEW_SIDE) == VIEW_ISO;
	const char* groups[2] = { "player", "crate" };
	for (const char* group : groups)
	{
		TypedArray<Node> bodies = water->get_tree()->get_nodes_in_group(group);
		for (int index = 0; index < bodies.size(); index++)
		{
			Object* object = bodies[index];
			Node3D* body = (Node3D*)object;
			if (!body || !body->is_visible_in_tree() || !body->has_method("SetCurrent")) continue;
			AABB box;
			if (!ShapeWorldBox((CollisionShape3D*)body->get_node_or_null("Collision"), 0.9f, box) || !box.intersects(stream)) continue;

			// A river on the board only reaches, in iso, what is pressed against the back wall
			if (reaches_wall_only && body->get_global_position().z > water->get_global_position().z + 1.0f) continue;
			body->call("SetCurrent", (double)current.x, (double)current.y, (double)current.z);
		}
	}
}
void OnProcess(Caller* instance, double _delta)
{
	// Purpose: called every frame, moves the light bands of the water along the flow, so the current can be seen.

	// The bands only move while the water flows: not in top, where it is still, nor while the level changes shape
	auto water = GetSelf<Area3D>(instance);
	GeometryInstance3D* mesh = (GeometryInstance3D*)water->get_node_or_null("Mesh");
	if (!mesh) return;
	if ((bool)water->get_meta("frozen", false) || (int)water->get_meta("view", VIEW_SIDE) == VIEW_TOP) return;

	// The distance flowed grows at the river's speed, kept short so it never loses precision
	double clock = (double)water->get_meta("clock", 0.0) + _delta * (double)water->get_meta("flow_speed", 3.5);
	if (clock > 1000.0) clock -= 1000.0;
	water->set_meta("clock", clock);
	mesh->set_instance_shader_parameter("phase", clock);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
