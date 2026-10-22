
/* Camundrum - Pressure Plate Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/area3d.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/typed_array.hpp>
#include <Godot/classes/scene_tree.hpp>
#include <Godot/variant/node_path.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every plate
JENOVA_PROPERTY(NodePath, target, NodePath())
JENOVA_PROPERTY(bool, stays_pressed, false)

// The state of each plate lives in its node's metadata: target, stays_pressed, pressed, view, settle_frames

// Look
void ShowPressed(Area3D* plate, bool pressed)
{
	// Purpose: make the plate look pushed down or raised.

	// Lower or raise the visible part, if the plate has one
	Node3D* mesh = (Node3D*)plate->get_node_or_null("Mesh");
	if (mesh) mesh->set_position(Vector3(0.0f, pressed ? 0.02f : 0.06f, 0.0f));
}

// Weight
bool SomethingOnIt(Area3D* plate)
{
	// Purpose: tell whether the player or a crate is on the plate, by comparing boxes, which also works for a body that stands still.

	// The plate's trigger
	AABB trigger;
	if (!ShapeWorldBox((CollisionShape3D*)plate->get_node_or_null("Trigger"), 1.0f, trigger)) return false;

	// The player and every crate still in the level
	const char* groups[2] = { "player", "crate" };
	for (const char* group : groups)
	{
		TypedArray<Node> bodies = plate->get_tree()->get_nodes_in_group(group);
		for (int index = 0; index < bodies.size(); index++)
		{
			Object* object = bodies[index];
			Node3D* body = (Node3D*)object;
			if (!body || !body->is_visible_in_tree()) continue;
			AABB box;
			if (ShapeWorldBox((CollisionShape3D*)body->get_node_or_null("Collision"), 0.9f, box) && box.intersects(trigger)) return true;
		}
	}
	return false;
}

// Contract
void OnViewChanged(Caller* instance, int view)
{
	// Purpose: part of the element contract, tells the plate which view is active.

	// Remember the view, weight only exists in the views that have gravity
	GetSelf<Area3D>(instance)->set_meta("view", view);
}
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, returns the plate to its starting state when the level restarts.

	// Raise the plate again, its target resets itself through its own contract
	auto plate = GetSelf<Area3D>(instance);
	plate->set_meta("pressed", false);
	ShowPressed(plate, false);

	// Ignore weight for a couple of frames, the physics still lists whoever stood here before being moved away
	plate->set_meta("settle_frames", 2);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node is in the tree, prepares the state of this plate.

	// Copy the inspector setting onto the node, where every function can read it safely
	auto plate = GetSelf<Area3D>(instance);
	plate->set_meta("target", target);
	plate->set_meta("stays_pressed", stays_pressed);

	// Start raised, in the view the game normally begins in
	plate->set_meta("pressed", false);
	plate->set_meta("view", VIEW_SIDE);
	plate->set_meta("settle_frames", 0);
	ShowPressed(plate, false);

	// Only notice actors such as the player, not the ground the plate lies on
	plate->set_collision_mask(LAYER_ACTORS);
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, presses or releases the plate and tells its target when that changes.

	// Wait out the frames right after a reset
	auto plate = GetSelf<Area3D>(instance);
	int settle_frames = plate->get_meta("settle_frames", 0);
	if (settle_frames > 0)
	{
		plate->set_meta("settle_frames", settle_frames - 1);
		return;
	}

	// Something weighs on the plate when an actor is on it and the view has gravity
	bool pressed = (int)plate->get_meta("view", VIEW_SIDE) != VIEW_TOP && SomethingOnIt(plate);

	// A plate set to stay pressed never comes up again once pressed
	if ((bool)plate->get_meta("pressed", false) && (bool)plate->get_meta("stays_pressed", false)) return;

	// Nothing to do while the state stays the same
	if (pressed == (bool)plate->get_meta("pressed", false)) return;

	// Store and show the new state, a press is heard
	plate->set_meta("pressed", pressed);
	ShowPressed(plate, pressed);
	if (pressed) PlaySound(plate, SOUND_PLATE);

	// Tell the element this plate drives, if one was chosen in the inspector
	Node* driven = plate->get_node_or_null((NodePath)plate->get_meta("target", NodePath()));
	if (driven && driven->has_method(pressed ? "Activate" : "Deactivate")) driven->call(pressed ? "Activate" : "Deactivate");
}

// Jenova Script Block End
JENOVA_SCRIPT_END
