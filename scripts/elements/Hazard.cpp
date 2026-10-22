
/* Camundrum - Hazard Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/area3d.hpp>
#include <Godot/variant/variant.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every hazard
JENOVA_PROPERTY(bool, on_board, false)

// The state of each hazard lives in its node's metadata: on_board, view, frozen, settle_frames

// Contract
void OnViewChanged(Caller* instance, int view)
{
	// Purpose: part of the element contract, tells the hazard which view is active.

	// Remember the view, it decides whether the hazard is dangerous
	GetSelf<Area3D>(instance)->set_meta("view", view);
}
void SetFrozen(Caller* instance, int frozen)
{
	// Purpose: part of the element contract, keeps the hazard harmless while the level changes shape.

	// Store the state
	GetSelf<Area3D>(instance)->set_meta("frozen", frozen != 0);
}
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, returns the hazard to its starting state when the level restarts.

	// Ignore touches for a couple of frames, the physics still lists whoever stood here before being moved away
	auto hazard = GetSelf<Area3D>(instance);
	hazard->set_meta("settle_frames", 2);
	hazard->set_meta("frozen", false);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node is in the tree, prepares the hazard.

	// Copy the inspector setting onto the node, where every function can read it safely
	auto hazard = GetSelf<Area3D>(instance);
	hazard->set_meta("on_board", on_board);

	// Start harmless until told the view, and only notice actors such as the player
	hazard->set_meta("view", VIEW_SIDE);
	hazard->set_meta("frozen", false);
	hazard->set_meta("settle_frames", 0);
	hazard->set_collision_mask(LAYER_ACTORS);
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, kills the player when it touches the hazard in a view where it is dangerous.

	// Nothing happens while the level changes shape
	auto hazard = GetSelf<Area3D>(instance);
	if ((bool)hazard->get_meta("frozen", false)) return;

	// Wait out the frames right after a reset
	int settle_frames = hazard->get_meta("settle_frames", 0);
	if (settle_frames > 0)
	{
		hazard->set_meta("settle_frames", settle_frames - 1);
		return;
	}

	// A hazard on the board only hurts where the board is the floor, which is top view, one on the ground hurts everywhere else
	int view = hazard->get_meta("view", VIEW_SIDE);
	bool dangerous = (bool)hazard->get_meta("on_board", false) ? (view == VIEW_TOP) : (view != VIEW_TOP);
	if (!dangerous) return;

	// Kill the player if it is touching, telling it the cause so it shows being hurt
	Node* player = FirstBodyInGroup(hazard, "player");
	if (player) player->call("Kill", DEATH_SPIKES);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
