
/* Camundrum - Board Plate Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/scene_tree.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/node_path.hpp>
#include <Godot/variant/typed_array.hpp>

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

// The state of each plate lives in its node's metadata: target, stays_pressed, view, pressed
// A board plate is one cell of the board, pressed in top by whatever lies on it there

// Look
void ShowPressed(Node3D* plate, bool pressed)
{
	// Purpose: draw the button of the plate pushed into the board or standing out of it.

	// Move the button towards the board when pressed
	Node3D* button = (Node3D*)plate->get_node_or_null("Button");
	if (button) button->set_position(Vector3(0.0f, 0.0f, pressed ? 0.02f : 0.07f));
}

// Weight
bool SomethingOnTop(Node3D* plate)
{
	// Purpose: tell whether something lies on the plate in top view: the player with its centre on it, or a crate covering at least half of it.

	// The plate's cell across the board
	Vector3 center = plate->get_global_position();

	// The player, judged by its centre, the same way the void judges it
	Node3D* player = (Node3D*)plate->get_tree()->get_first_node_in_group("player");
	if (player && player->is_visible_in_tree())
	{
		Vector3 at = player->get_global_position();
		if (fabsf(at.x - center.x) < 0.5f && fabsf(at.y - center.y) < 0.5f) return true;
	}

	// Crates, judged by how much of the cell they cover
	TypedArray<Node> crates = plate->get_tree()->get_nodes_in_group("crate");
	for (int index = 0; index < crates.size(); index++)
	{
		Object* object = crates[index];
		Node3D* crate = (Node3D*)object;
		if (!crate || !crate->is_visible_in_tree()) continue;
		AABB box;
		if (!ShapeWorldBox((CollisionShape3D*)crate->get_node_or_null("Collision"), 1.0f, box)) continue;
		float overlap_x = fminf(box.position.x + box.size.x, center.x + 0.5f) - fmaxf(box.position.x, center.x - 0.5f);
		float overlap_y = fminf(box.position.y + box.size.y, center.y + 0.5f) - fmaxf(box.position.y, center.y - 0.5f);
		if (overlap_x > 0.0f && overlap_y > 0.0f && overlap_x * overlap_y >= 0.5f) return true;
	}
	return false;
}

// Contract
void OnViewChanged(Caller* instance, int view)
{
	// Purpose: part of the element contract, remembers the active view: the plate only feels weight in top, where the board is the floor.

	// Store it
	GetSelf<Node3D>(instance)->set_meta("view", view);
}
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, raises the plate again when the level restarts.

	// Back up, its target resets itself through its own contract
	auto plate = GetSelf<Node3D>(instance);
	plate->set_meta("pressed", false);
	ShowPressed(plate, false);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node is in the tree, prepares the plate.

	// Copy the inspector settings onto the node, where every function can read them safely
	auto plate = GetSelf<Node3D>(instance);
	plate->set_meta("target", target);
	plate->set_meta("stays_pressed", stays_pressed);

	// Start raised, in the view the game normally begins in
	plate->set_meta("view", VIEW_SIDE);
	plate->set_meta("pressed", false);
	ShowPressed(plate, false);
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, presses or releases the plate and tells its target when that changes.

	// Weight only counts in top; outside top whatever lay on it falls off the board, so the plate comes up again
	auto plate = GetSelf<Node3D>(instance);
	bool was_pressed = plate->get_meta("pressed", false);
	bool pressed = (int)plate->get_meta("view", VIEW_SIDE) == VIEW_TOP && SomethingOnTop(plate);

	// A plate set to stay pressed never comes up again once pressed
	if (was_pressed && (bool)plate->get_meta("stays_pressed", false)) return;
	if (pressed == was_pressed) return;

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
