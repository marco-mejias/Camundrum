
/* Camundrum - Door Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/static_body3d.hpp>
#include <Godot/variant/variant.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every door
JENOVA_PROPERTY(bool, stays_open, false)

// The state of each door lives in its node's metadata: stays_open, open, shown (how much of it is drawn, 1 closed and 0 open)

// State changes
void DrawShown(StaticBody3D* door, double shown)
{
	// Purpose: draw the door raised by some amount, 1 is fully closed and 0 is sunk into the ground.

	// Remember it, and shrink the door towards its base so it sinks instead of vanishing
	door->set_meta("shown", shown);
	Node3D* mesh = (Node3D*)door->get_node_or_null("Mesh");
	if (!mesh) return;
	mesh->set_visible(shown > 0.01);
	mesh->set_scale(Vector3(1.0f, (float)shown, 1.0f));
	mesh->set_position(Vector3(0.0f, (float)(-(1.0 - shown) * 0.5), 0.0f));
}
void ApplyOpen(StaticBody3D* door, bool open, bool instant)
{
	// Purpose: make the door behave open or closed, remember which, and start it sinking or rising unless asked to change at once.

	// A real change, not a reset, is heard
	bool changed = (bool)door->get_meta("open", false) != open;
	door->set_meta("open", open);
	if (changed && !instant) PlaySound(door, SOUND_DOOR);

	// An open door does not block, the change waits for the end of the frame so the physics step is not disturbed
	Node* shape = door->get_node_or_null("Collision");
	if (shape) shape->set_deferred("disabled", open);

	// The look follows over the next frames, or right now
	if (instant) DrawShown(door, open ? 0.0 : 1.0);
}

// Commands
void Activate(Caller* instance)
{
	// Purpose: let an activator such as a pressure plate open the door.

	// Open it
	ApplyOpen(GetSelf<StaticBody3D>(instance), true, false);
}
void Deactivate(Caller* instance)
{
	// Purpose: let an activator such as a pressure plate close the door again.

	// A door set to stay open ignores this, any other door closes
	auto door = GetSelf<StaticBody3D>(instance);
	if ((bool)door->get_meta("stays_open", false)) return;
	ApplyOpen(door, false, false);
}

// Contract
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, returns the door to its starting state when the level restarts.

	// Doors start closed
	ApplyOpen(GetSelf<StaticBody3D>(instance), false, true);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node and its children are in the tree, prepares the state of this door.

	// Copy the inspector setting onto the node, where every function can read it safely
	auto door = GetSelf<StaticBody3D>(instance);
	door->set_meta("stays_open", stays_open);

	// Doors start closed
	door->set_meta("open", false);
	ApplyOpen(door, false, true);
}
void OnProcess(Caller* instance, double _delta)
{
	// Purpose: called every frame, sinks an opening door into the ground or raises a closing one.

	// Move the drawn amount towards the state, quickly
	auto door = GetSelf<StaticBody3D>(instance);
	double shown = door->get_meta("shown", 1.0);
	double wanted = (bool)door->get_meta("open", false) ? 0.0 : 1.0;
	if (shown == wanted) return;
	double step = _delta / 0.18;
	if (shown < wanted) shown = (shown + step > wanted) ? wanted : shown + step;
	else shown = (shown - step < wanted) ? wanted : shown - step;
	DrawShown(door, shown);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
