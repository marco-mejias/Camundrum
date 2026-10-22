
/* Camundrum - Depth Lift Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/static_body3d.hpp>
#include <Godot/classes/collision_shape3d.hpp>
#include <Godot/classes/mesh_instance3d.hpp>
#include <Godot/classes/material.hpp>
#include <Godot/classes/scene_tree.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/array.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every lift
// The lift is placed at its first stop, stop 0; each further stop is stop_step units deeper, towards the board
JENOVA_PROPERTY(int, stop_count, 2)
JENOVA_PROPERTY(double, stop_step, 1.0)
JENOVA_PROPERTY(double, shift_time, 0.6)

// The state of each lift lives in its node's metadata: stops, step, time, stop, target, requested, moving, progress, from_z, shift, riders

// Display
void ShowStop(Node3D* lift, int stop)
{
	// Purpose: show the number of the stop on the lift's digital display, lighting the segments of that digit.

	// The segments a to g that are lit for each digit, from the top clockwise and the middle last
	const char* lit[10] = { "abcdef", "bc", "abdeg", "abcdg", "bcfg", "acdfg", "acdefg", "abc", "abcdefg", "abcdfg" };
	int digit = stop % 10;
	MeshInstance3D* on_look = (MeshInstance3D*)lift->get_node_or_null("Display/LitLook");
	MeshInstance3D* off_look = (MeshInstance3D*)lift->get_node_or_null("Display/DimLook");
	if (!on_look || !off_look) return;
	Ref<Material> on_material = on_look->get_surface_override_material(0);
	Ref<Material> off_material = off_look->get_surface_override_material(0);

	// Give each segment the lit or the dim look
	const char* names = "abcdefg";
	for (int index = 0; index < 7; index++)
	{
		char name[2] = { names[index], 0 };
		MeshInstance3D* segment = (MeshInstance3D*)lift->get_node_or_null(NodePath(String("Display/") + String(name)));
		if (!segment) continue;
		bool on = false;
		for (const char* letter = lit[digit]; *letter; letter++) on = on || *letter == names[index];
		segment->set_surface_override_material(0, on ? on_material : off_material);
	}
}

// Riding
bool PlayerOnDeck(Node3D* lift)
{
	// Purpose: tell whether the player stands on the lift, by comparing the player's body with the space above the lift.

	// The deck above the lift and the player's body
	Node3D* player = (Node3D*)lift->get_tree()->get_first_node_in_group("player");
	if (!player || !player->is_visible_in_tree()) return false;
	AABB deck;
	AABB body;
	if (!ShapeWorldBox((CollisionShape3D*)lift->get_node_or_null("Deck/Collision"), 1.0f, deck)) return false;
	if (!ShapeWorldBox((CollisionShape3D*)player->get_node_or_null("Collision"), 0.9f, body)) return false;
	return deck.intersects(body);
}
void MoveBy(Node3D* lift, double depth)
{
	// Purpose: move the lift and everything riding it by some depth.

	// The lift itself
	Vector3 position = lift->get_global_position();
	position.z += (float)depth;
	lift->set_global_position(position);

	// And what the depth module found standing on it
	Array riders = lift->get_meta("riders", Array());
	for (int index = 0; index < riders.size(); index++)
	{
		Object* object = riders[index];
		Node3D* rider = (Node3D*)object;
		if (!rider) continue;
		Vector3 at = rider->get_global_position();
		at.z += (float)depth;
		rider->set_global_position(at);
	}
}

// Commands
void OnInteract(Caller* instance)
{
	// Purpose: part of the element contract, sends the lift to its next stop when the player presses the interact key standing on it.
	// After the last stop it goes back to the first one.

	// Only from the deck, and not while a move is asked for or running
	auto lift = GetSelf<Node3D>(instance);
	if ((bool)lift->get_meta("moving", false) || (bool)lift->get_meta("requested", false) || !PlayerOnDeck(lift)) return;

	// Aim at the next stop and ask the GameManager, once this frame's work is done
	int stops = lift->get_meta("stops", 2);
	lift->set_meta("target", ((int)lift->get_meta("stop", 0) + 1) % stops);
	lift->set_meta("requested", true);
	lift->call_deferred("emit_signal", "rotation_requested", lift);
}
double PendingShift(Caller* instance)
{
	// Purpose: let the depth module ask how far in depth the waiting move goes; deeper stops are closer to the board.

	// From the current stop to the target one
	auto lift = GetSelf<Node3D>(instance);
	int from = lift->get_meta("stop", 0);
	int to = lift->get_meta("target", 0);
	return -(double)(to - from) * (double)lift->get_meta("step", 1.0);
}
void CancelTurn(Caller* instance)
{
	// Purpose: let the GameManager refuse the move because something is in the way.

	// Stay at the current stop
	auto lift = GetSelf<Node3D>(instance);
	lift->set_meta("target", lift->get_meta("stop", 0));
	lift->set_meta("requested", false);
}
void StartTurn(Caller* instance)
{
	// Purpose: let the GameManager start the move once it has been allowed, the physics routine carries it out.

	// Note where the move starts and how far it goes
	auto lift = GetSelf<Node3D>(instance);
	lift->set_meta("shift", PendingShift(instance));
	lift->set_meta("from_z", (double)lift->get_global_position().z);
	lift->set_meta("progress", 0.0);
	lift->set_meta("requested", false);
	lift->set_meta("moving", true);
}

// Contract
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, sends the lift back to its first stop when the level restarts.
	// Its position is restored by the depth module, with every other tracked piece.

	// Stop any move and show stop 0
	auto lift = GetSelf<Node3D>(instance);
	lift->set_meta("stop", 0);
	lift->set_meta("target", 0);
	lift->set_meta("requested", false);
	lift->set_meta("moving", false);
	lift->set_meta("riders", Array());
	ShowStop(lift, 0);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node and its children are in the tree, prepares the lift and its signals.

	// Copy the inspector settings onto the node, where every function can read them safely
	auto lift = GetSelf<Node3D>(instance);
	lift->set_meta("stops", stop_count < 2 ? 2 : (stop_count > 10 ? 10 : stop_count));
	lift->set_meta("step", stop_step);
	lift->set_meta("time", shift_time > 0.05 ? shift_time : 0.05);

	// The same signals as a turntable, so the GameManager handles both the same way
	if (!lift->has_signal("rotation_requested")) lift->add_user_signal("rotation_requested");
	if (!lift->has_signal("busy_finished")) lift->add_user_signal("busy_finished");

	// Start at the first stop
	lift->set_meta("stop", 0);
	lift->set_meta("target", 0);
	lift->set_meta("requested", false);
	lift->set_meta("moving", false);
	ShowStop(lift, 0);

	// The deck only notices actors such as the player and crates
	CollisionObject3D* deck = (CollisionObject3D*)lift->get_node_or_null("Deck");
	if (deck) deck->set_collision_mask(LAYER_ACTORS);
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, carries out a move the GameManager allowed, with a gentle start and end.

	// Nothing to do while not moving
	auto lift = GetSelf<Node3D>(instance);
	if (!(bool)lift->get_meta("moving", false)) return;

	// Advance and place the lift and its riders where they should be now
	double progress = (double)lift->get_meta("progress", 1.0) + _delta / (double)lift->get_meta("time", 0.6);
	if (progress > 1.0) progress = 1.0;
	lift->set_meta("progress", progress);
	double eased = progress * progress * (3.0 - 2.0 * progress);
	double wanted = (double)lift->get_meta("from_z", 0.0) + (double)lift->get_meta("shift", 0.0) * eased;
	MoveBy(lift, wanted - (double)lift->get_global_position().z);

	// At the end, show the new stop and let the GameManager settle the level and release the world
	if (progress < 1.0) return;
	lift->set_meta("moving", false);
	lift->set_meta("stop", lift->get_meta("target", 0));
	ShowStop(lift, lift->get_meta("stop", 0));
	PlaySound(lift, SOUND_SWITCH);
	lift->call_deferred("emit_signal", "busy_finished");
}

// Jenova Script Block End
JENOVA_SCRIPT_END
