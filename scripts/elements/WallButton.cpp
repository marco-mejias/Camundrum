
/* Camundrum - Wall Button Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/area3d.hpp>
#include <Godot/classes/scene_tree.hpp>
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

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every button
JENOVA_PROPERTY(NodePath, target, NodePath())

// The state of each button lives in its node's metadata: target, on, view

// Look
void ShowState(Area3D* button, bool on)
{
	// Purpose: show the button raised when off and pushed in when on.

	// Swap the raised cap for the pushed one
	Node3D* cap_off = (Node3D*)button->get_node_or_null("CapOff");
	Node3D* cap_on = (Node3D*)button->get_node_or_null("CapOn");
	if (cap_off) cap_off->set_visible(!on);
	if (cap_on) cap_on->set_visible(on);
}

// Reach
bool PlayerInReach(Area3D* button)
{
	// Purpose: tell whether the player is close enough to press the button in the active view.

	// In iso the player has to really stand in front of the button
	if ((int)button->get_meta("view", VIEW_SIDE) == VIEW_ISO) return HasPlayerInside(button);

	// In the flat views the depth is ignored, so a button on the board can be pressed from the front edge
	Node3D* player = (Node3D*)button->get_tree()->get_first_node_in_group("player");
	AABB reach;
	if (!player || !ShapeWorldBox((CollisionShape3D*)button->get_node_or_null("Trigger"), 1.0f, reach)) return false;
	Vector3 point = player->get_global_position();
	Vector3 low = reach.position;
	Vector3 high = reach.position + reach.size;
	return point.x >= low.x && point.x <= high.x && point.y >= low.y && point.y <= high.y;
}

// Contract
void OnInteract(Caller* instance)
{
	// Purpose: part of the element contract, presses or releases the button when the player uses the interact key next to it.

	// Only a player within reach can use it
	auto button = GetSelf<Area3D>(instance);
	if (!PlayerInReach(button)) return;

	// Flip and show the new state
	bool on = !(bool)button->get_meta("on", false);
	button->set_meta("on", on);
	ShowState(button, on);
	PlaySound(button, SOUND_BUTTON);

	// Tell the element this button drives, if one was chosen in the inspector
	Node* driven = button->get_node_or_null((NodePath)button->get_meta("target", NodePath()));
	if (!driven) return;

	// Something with two positions of its own, such as a turntable, is simply flipped
	if (driven->has_method("Toggle"))
	{
		driven->call("Toggle");
		return;
	}

	// Anything else is switched on and off with the button
	if (driven->has_method(on ? "Activate" : "Deactivate")) driven->call(on ? "Activate" : "Deactivate");
}
void OnViewChanged(Caller* instance, int view)
{
	// Purpose: part of the element contract, remembers the active view, which decides how close the player must be.

	// Store the view
	GetSelf<Area3D>(instance)->set_meta("view", view);
}
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, releases the button when the level restarts.

	// Back to off, its target resets itself through its own contract
	auto button = GetSelf<Area3D>(instance);
	button->set_meta("on", false);
	ShowState(button, false);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node and its children are in the tree, prepares the button.

	// Copy the inspector setting onto the node, where every function can read it safely
	auto button = GetSelf<Area3D>(instance);
	button->set_meta("target", target);

	// Start released in the view the game normally begins in, and only notice actors such as the player
	button->set_meta("on", false);
	button->set_meta("view", VIEW_SIDE);
	ShowState(button, false);
	button->set_collision_mask(LAYER_ACTORS);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
