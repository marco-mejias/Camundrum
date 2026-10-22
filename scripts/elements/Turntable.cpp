
/* Camundrum - Turntable Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/area3d.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/typed_array.hpp>

// Game
#include "scripts/shared/GameTypes.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every turntable
JENOVA_PROPERTY(double, turn_time, 0.6)
JENOVA_PROPERTY(int, turn_axis, 0)

// The state of each turntable lives in its node's metadata: axis, view, turning, requested, angle, target, from, progress

// Turning
Vector3 AxisVector(Node3D* table)
{
	// Purpose: the direction the turntable turns around, vertical for ground pieces and depth for board pieces.

	// 0 turns around the vertical axis, anything else around the depth axis
	return ((int)table->get_meta("axis", 0) == 0) ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(0.0f, 0.0f, 1.0f);
}
void CarryRiders(Node3D* table, double degrees)
{
	// Purpose: turn everything standing on the turntable by the same amount the turntable just turned.

	// Something on the board is only carried in top view, where the board is the floor
	if ((int)table->get_meta("axis", 0) != 0 && (int)table->get_meta("view", VIEW_SIDE) != VIEW_TOP) return;

	// The deck is the area above the turntable where riders stand
	Area3D* deck = (Area3D*)table->get_node_or_null("Deck");
	if (!deck) return;

	// Rotate each rider's position around the turntable's centre, riders stay upright
	Vector3 pivot = table->get_global_position();
	TypedArray<Node3D> riders = deck->get_overlapping_bodies();
	for (int index = 0; index < riders.size(); index++)
	{
		Object* object = riders[index];
		Node3D* rider = (Node3D*)object;
		if (!rider) continue;
		Vector3 offset = rider->get_global_position() - pivot;
		offset = offset.rotated(AxisVector(table), (float)(degrees * 3.14159265358979 / 180.0));
		rider->set_global_position(pivot + offset);
	}
}
void ApplyAngle(Node3D* table, double degrees)
{
	// Purpose: set the turntable's rotation, which turns every piece placed on it as its children.

	// Turn around the chosen axis only
	table->set_meta("angle", degrees);
	if ((int)table->get_meta("axis", 0) == 0) table->set_rotation_degrees(Vector3(0.0f, (float)degrees, 0.0f));
	else table->set_rotation_degrees(Vector3(0.0f, 0.0f, (float)degrees));
}
void Request(Node3D* table)
{
	// Purpose: ask the GameManager for permission to turn towards the target, once this frame's work is done.

	// Only ask when there is something to turn and no question is already waiting
	double target = table->get_meta("target", 0.0);
	double angle = table->get_meta("angle", 0.0);
	if (target == angle || (bool)table->get_meta("turning", false) || (bool)table->get_meta("requested", false)) return;
	table->set_meta("requested", true);
	table->call_deferred("emit_signal", "rotation_requested", table);
}

// Commands
void Toggle(Caller* instance)
{
	// Purpose: let a lever flip the turntable between its two positions, a quarter turn apart.

	// Aim at the other position and ask to turn
	auto table = GetSelf<Node3D>(instance);
	table->set_meta("target", ((double)table->get_meta("angle", 0.0) == 0.0) ? 90.0 : 0.0);
	Request(table);
}
void Activate(Caller* instance)
{
	// Purpose: let a plate ask for the turned position.

	// Aim at the turned position and ask to turn
	auto table = GetSelf<Node3D>(instance);
	table->set_meta("target", 90.0);
	Request(table);
}
void Deactivate(Caller* instance)
{
	// Purpose: let a plate ask for the starting position.

	// Aim at the starting position and ask to turn
	auto table = GetSelf<Node3D>(instance);
	table->set_meta("target", 0.0);
	Request(table);
}
double PendingDelta(Caller* instance)
{
	// Purpose: let the DepthController ask how many degrees the waiting turn covers.

	// The distance from where the turntable is to where it wants to be
	auto table = GetSelf<Node3D>(instance);
	return (double)table->get_meta("target", 0.0) - (double)table->get_meta("angle", 0.0);
}
double TurnAxis(Caller* instance)
{
	// Purpose: let the DepthController ask which axis the turntable turns around, 0 vertical and 1 depth.
	// It is returned as a decimal number because calls between scripts cannot return a whole number reliably.

	// Hand back the axis stored on the node
	auto table = GetSelf<Node3D>(instance);
	return (double)(int)table->get_meta("axis", 0);
}
void CancelTurn(Caller* instance)
{
	// Purpose: let the GameManager refuse the waiting turn because something is in the way.

	// Stay where the turntable is
	auto table = GetSelf<Node3D>(instance);
	table->set_meta("target", table->get_meta("angle", 0.0));
	table->set_meta("requested", false);
}
void StartTurn(Caller* instance)
{
	// Purpose: let the GameManager start the waiting turn once it has been allowed.

	// Begin turning from the current position, the physics routine carries it out
	auto table = GetSelf<Node3D>(instance);
	table->set_meta("requested", false);
	table->set_meta("turning", true);
	table->set_meta("from", table->get_meta("angle", 0.0));
	table->set_meta("progress", 0.0);
}

// Contract
void OnViewChanged(Caller* instance, int view)
{
	// Purpose: part of the element contract, tells the turntable which view is active.

	// Remember the view, a turntable on the board only carries riders in top view
	GetSelf<Node3D>(instance)->set_meta("view", view);
}
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, returns the turntable to its starting position when the level restarts.

	// Stop any turn or question and go back to the start at once
	auto table = GetSelf<Node3D>(instance);
	table->set_meta("turning", false);
	table->set_meta("requested", false);
	table->set_meta("target", 0.0);
	table->set_meta("progress", 1.0);
	ApplyAngle(table, 0.0);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node and its children are in the tree, prepares the turntable and its signals.

	// Copy the inspector settings onto the node, where every function can read them safely
	auto table = GetSelf<Node3D>(instance);
	table->set_meta("axis", turn_axis);
	table->set_meta("turn_time", turn_time);

	// Create the signals the GameManager listens to, one to ask for a turn and one when it ends
	if (!table->has_signal("rotation_requested")) table->add_user_signal("rotation_requested");
	if (!table->has_signal("busy_finished")) table->add_user_signal("busy_finished");

	// Start unturned, and let the deck notice actors standing on it
	table->set_meta("view", VIEW_SIDE);
	table->set_meta("turning", false);
	table->set_meta("requested", false);
	table->set_meta("target", 0.0);
	table->set_meta("progress", 1.0);
	ApplyAngle(table, 0.0);
	Area3D* deck = (Area3D*)table->get_node_or_null("Deck");
	if (deck) deck->set_collision_mask(LAYER_ACTORS);
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, carries out a turn that the GameManager allowed.

	// Nothing to do while not turning
	auto table = GetSelf<Node3D>(instance);
	if (!(bool)table->get_meta("turning", false)) return;

	// Advance the turn with the same gentle start and end as the camera
	double progress = (double)table->get_meta("progress", 1.0) + _delta / (double)table->get_meta("turn_time", 0.6);
	if (progress > 1.0) progress = 1.0;
	table->set_meta("progress", progress);
	double eased = progress * progress * (3.0 - 2.0 * progress);
	double from = table->get_meta("from", 0.0);
	double angle = from + ((double)table->get_meta("target", 0.0) - from) * eased;

	// Turn the riders by the step taken this frame, then the turntable and its pieces
	CarryRiders(table, angle - (double)table->get_meta("angle", 0.0));
	ApplyAngle(table, angle);

	// At the end, let the GameManager settle the level and release the world
	if (progress >= 1.0)
	{
		table->set_meta("turning", false);
		table->call_deferred("emit_signal", "busy_finished");
	}
}

// Jenova Script Block End
JENOVA_SCRIPT_END
