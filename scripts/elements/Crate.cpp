
/* Camundrum - Crate Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/character_body3d.hpp>
#include <Godot/variant/variant.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"
#include "scripts/shared/CrateBody.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every crate
JENOVA_PROPERTY(double, crate_gravity, 35.0)
JENOVA_PROPERTY(double, crate_max_fall, 22.0)
JENOVA_PROPERTY(double, crate_fall_limit, -12.0)

// The crate's body (falling, rivers, fans, holes, rows of crates) is shared with the robot and lives in CrateBody.h; this script only connects it to the game

// Raised in top
void SetRaised(Caller* instance, int raised)
{
	// Purpose: let a fan raise the crate in top view, so it can be pushed up onto walls.
	// The parameter is a number because calls between scripts cannot carry a bool.

	// Hand it to the shared body
	CrateBody::SetRaised(GetSelf<CharacterBody3D>(instance), raised);
}
void DropIntoVoid(Caller* instance)
{
	// Purpose: blow the crate off the level in side view, through everything until it is out of the level.

	// Hand it to the shared body
	CrateBody::DropIntoVoid(GetSelf<CharacterBody3D>(instance));
}

// Commands
void Push(Caller* instance, double x, double y, double z)
{
	// Purpose: let the player push the crate this frame, at the speed given on each axis.

	// Store the push, the next physics step moves the crate with it
	GetSelf<CharacterBody3D>(instance)->set_meta("push", Vector3(x, y, z));
}
void SetCurrent(Caller* instance, double x, double y, double z)
{
	// Purpose: let a river segment or a fan carry the crate this frame, combining the currents of every one it touches.

	// Hand it to the shared body
	CrateBody::SetCurrent(GetSelf<CharacterBody3D>(instance), x, y, z);
}
void Break(Caller* instance)
{
	// Purpose: destroy the crate, when it falls out of the level or sinks into a hole.

	// Take it out of the level until the next restart
	CrateBody::SetBroken(GetSelf<CharacterBody3D>(instance), true);
}

// Contract
void OnViewChanged(Caller* instance, int view)
{
	// Purpose: part of the element contract, gives the crate the physics rules of the new view.

	// Hand it to the shared body
	CrateBody::ViewChanged(GetSelf<CharacterBody3D>(instance), view);
}
void SetFrozen(Caller* instance, int frozen)
{
	// Purpose: part of the element contract, pauses the crate while the level changes shape.

	// Store the state, the fall speed is kept so the crate resumes where it left off
	GetSelf<CharacterBody3D>(instance)->set_meta("frozen", frozen != 0);
}
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, returns the crate to its starting state when the level restarts.

	// Hand it to the shared body
	CrateBody::Reset(GetSelf<CharacterBody3D>(instance));
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node and its children are in the tree, prepares the crate.

	// Copy the inspector settings onto the node and start whole and still
	CrateBody::Ready(GetSelf<CharacterBody3D>(instance), crate_gravity, crate_max_fall, crate_fall_limit);
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, moves the crate with its push, the river and gravity.

	// A crate has no drive of its own
	CrateBody::Step(GetSelf<CharacterBody3D>(instance), _delta, Vector3(0.0f, 0.0f, 0.0f));
}

// Jenova Script Block End
JENOVA_SCRIPT_END
