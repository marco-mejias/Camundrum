
/* Camundrum - Lever Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/area3d.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/node_path.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every lever
JENOVA_PROPERTY(NodePath, target, NodePath())

// The state of each lever lives in its node's metadata: target, on, angle (the handle's tilt drawn now), view, top_turn

// Look
void DrawAngle(Area3D* lever, double angle)
{
	// Purpose: draw the handle tilted by an angle, in degrees around the depth axis.

	// Remember it and turn the handle, the whole lever turned for the view
	lever->set_meta("angle", angle);
	float turn = TopTurnAngle(lever->get_meta("top_turn", 0.0));
	Vector3 pivot = Vector3(0.0f, 0.5f, 0.0f);
	Node3D* base = (Node3D*)lever->get_node_or_null("Base");
	if (base) base->set_transform(TurnedForTop(Transform3D(Basis(), Vector3(0.0f, 0.1f, 0.0f)), pivot, turn));
	Node3D* handle = (Node3D*)lever->get_node_or_null("Handle");
	Basis tilt = Basis(Vector3(0.0f, 0.0f, 1.0f), (float)(angle * 3.14159265358979 / 180.0));
	if (handle) handle->set_transform(TurnedForTop(Transform3D(tilt, Vector3(0.0f, 0.2f, 0.0f)), pivot, turn));
}
void ShowState(Area3D* lever, bool on)
{
	// Purpose: put the handle on one side or the other at once, used when the lever starts or resets.

	// Straight to the side that matches the state
	DrawAngle(lever, on ? -35.0 : 35.0);
}

// Contract
void OnViewChanged(Caller* instance, int view)
{
	// Purpose: part of the element contract, remembers the active view, so the lever can be drawn seen from above in top.

	// Store it, the drawing follows every frame
	GetSelf<Area3D>(instance)->set_meta("view", view);
}
void OnInteract(Caller* instance)
{
	// Purpose: part of the element contract, flips the lever when the player presses the interact key next to it.

	// Only a player standing at the lever can use it
	auto lever = GetSelf<Area3D>(instance);
	if (!HasPlayerInside(lever)) return;

	// Flip and show the new state
	bool on = !(bool)lever->get_meta("on", false);
	lever->set_meta("on", on);
	PlaySound(lever, SOUND_LEVER);

	// Tell the element this lever drives, if one was chosen in the inspector
	Node* driven = lever->get_node_or_null((NodePath)lever->get_meta("target", NodePath()));
	if (!driven) return;

	// Something with two positions of its own, such as a turntable, is simply flipped
	if (driven->has_method("Toggle"))
	{
		driven->call("Toggle");
		return;
	}

	// Anything else is switched on and off with the lever
	if (driven->has_method(on ? "Activate" : "Deactivate")) driven->call(on ? "Activate" : "Deactivate");
}
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, returns the lever to its starting side when the level restarts.

	// Back to off, its target resets itself through its own contract
	auto lever = GetSelf<Area3D>(instance);
	lever->set_meta("on", false);
	lever->set_meta("top_turn", -1.0);
	ShowState(lever, false);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node and its children are in the tree, prepares the lever.

	// Copy the inspector setting onto the node, where every function can read it safely
	auto lever = GetSelf<Area3D>(instance);
	lever->set_meta("target", target);

	// Start off, and only notice actors such as the player
	lever->set_meta("on", false);
	ShowState(lever, false);
	lever->set_collision_mask(LAYER_ACTORS);
}

void OnProcess(Caller* instance, double _delta)
{
	// Purpose: called every frame, swings the handle over to the side of the current state.

	// Turn towards the view: seen from above in top
	auto lever = GetSelf<Area3D>(instance);
	lever->set_meta("top_turn", StepTopTurn(lever->get_meta("top_turn", -1.0), (int)lever->get_meta("view", VIEW_SIDE) == VIEW_TOP, _delta));

	// Move the drawn angle towards the side of the state, with a little overshoot-free snap
	double angle = lever->get_meta("angle", 35.0);
	double wanted = (bool)lever->get_meta("on", false) ? -35.0 : 35.0;
	double step = 520.0 * _delta;
	if (angle < wanted) angle = (angle + step > wanted) ? wanted : angle + step;
	else angle = (angle - step < wanted) ? wanted : angle - step;
	DrawAngle(lever, angle);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
