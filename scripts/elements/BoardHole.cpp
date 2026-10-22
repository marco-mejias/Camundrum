
/* Camundrum - Board Hole Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/variant/variant.hpp>

// Game
#include "scripts/shared/GameTypes.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// The state of each hole lives in its node's metadata: filled, cut (whether the board around it was really opened), wall_above (whether board continues above it)
// A hole is one cell of the board, one unit square; the player and crates check the holes themselves (VoidAt, FootprintOverVoid)

// Look
void ShowState(Node3D* hole)
{
	// Purpose: draw the hole as it is: an opening in the board with a dark pit behind it, or plugged by a crate.
	// Where the board could not be opened, the flat marker used in the editor stays as the look of the hole.

	// Read the state
	bool filled = hole->get_meta("filled", false);
	bool cut = hole->get_meta("cut", false);

	// The editor marker only shows where the board is whole, the pit only where it is open
	Node* rim = hole->get_node_or_null("Rim");
	Node* marker = hole->get_node_or_null("Marker");
	Node* pit = hole->get_node_or_null("Pit");
	Node* inner_wall = hole->get_node_or_null("InnerWall");
	Node* plug = hole->get_node_or_null("Filled");
	if (rim) rim->set("visible", !cut && !filled);
	if (marker) marker->set("visible", !cut && !filled);
	if (pit) pit->set("visible", cut && !filled);
	if (plug) plug->set("visible", filled);

	// Where the board goes on above the hole, a strip of its inner wall shows, so it reads as deep even seen straight on
	if (inner_wall) inner_wall->set("visible", cut && !filled && (bool)hole->get_meta("wall_above", false));
}

// Commands
void Fill(Caller* instance)
{
	// Purpose: let a crate that sank into the hole plug it, so it is floor again.

	// Store and show it
	auto hole = GetSelf<Node3D>(instance);
	hole->set_meta("filled", true);
	ShowState(hole);
}
void MarkCut(Caller* instance, int wall_above)
{
	// Purpose: let the LevelController say that the board around this hole was really opened, so the pit is shown instead of the marker.
	// It also says whether the board goes on right above the hole, as a number because calls between scripts cannot carry a bool.

	// Store and show it
	auto hole = GetSelf<Node3D>(instance);
	hole->set_meta("cut", true);
	hole->set_meta("wall_above", wall_above != 0);
	ShowState(hole);
}

// Contract
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, opens the hole again when the level restarts.

	// Open it
	auto hole = GetSelf<Node3D>(instance);
	hole->set_meta("filled", false);
	ShowState(hole);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node is in the tree, starts the hole open, drawn with its marker until the board is opened.

	// Open, and not cut yet
	auto hole = GetSelf<Node3D>(instance);
	hole->set_meta("filled", false);
	hole->set_meta("cut", false);
	hole->set_meta("wall_above", false);
	ShowState(hole);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
