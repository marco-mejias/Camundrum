
/* Camundrum - Door Element
 * A door that sinks into the ground while something opens it.
 */

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

// ---- State changes ----

// Draw the door raised by some amount, 1 is fully closed and 0 is sunk into the ground
void DrawShown(StaticBody3D* door, double shown)
{
	// Remember it, and shrink the door towards its base so it sinks instead of vanishing
	door->set_meta("shown", shown);
	Node3D* mesh = (Node3D*)door->get_node_or_null("Mesh");
	if (!mesh) return;
	mesh->set_visible(shown > 0.01);
	mesh->set_scale(Vector3(1.0f, (float)shown, 1.0f));
	mesh->set_position(Vector3(0.0f, (float)(-(1.0 - shown) * 0.5), 0.0f));
}

// Make the door behave open or closed, remember which, and start it sinking or rising unless asked to change at once
void ApplyOpen(StaticBody3D* door, bool open, bool instant)
{
	// 1. A real change, not a reset, is heard
	bool changed = (bool)door->get_meta("open", false) != open;
	door->set_meta("open", open);
	if (changed && !instant) PlaySound(door, SOUND_DOOR);

	// 2. An open door does not block, the change waits for the end of the frame so the physics step is not disturbed
	Node* shape = door->get_node_or_null("Collision");
	if (shape) shape->set_deferred("disabled", open);

	// 3. The look follows over the next frames, or right now
	if (instant) DrawShown(door, open ? 0.0 : 1.0);
}

// ---- Commands ----

// Let an activator such as a pressure plate open the door
void Activate(Caller* instance)
{
	// Open it
	ApplyOpen(GetSelf<StaticBody3D>(instance), true, false);
}

// Let an activator such as a pressure plate close the door again
void Deactivate(Caller* instance)
{
	// A door set to stay open ignores this, any other door closes
	auto door = GetSelf<StaticBody3D>(instance);
	if ((bool)door->get_meta("stays_open", false)) return;
	ApplyOpen(door, false, false);
}

// ---- Contract ----

// Part of the element contract, returns the door to its starting state when the level restarts
void Reset(Caller* instance)
{
	// Doors start closed
	ApplyOpen(GetSelf<StaticBody3D>(instance), false, true);
}

// ---- Routines ----

// Called once the node and its children are in the tree, prepares the state of this door
void OnReady(Caller* instance)
{
	// Copy the inspector setting onto the node, where every function can read it safely
	auto door = GetSelf<StaticBody3D>(instance);
	door->set_meta("stays_open", stays_open);

	// Doors start closed
	door->set_meta("open", false);
	ApplyOpen(door, false, true);
}

// Called every frame, sinks an opening door into the ground or raises a closing one
void OnProcess(Caller* instance, double _delta)
{
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
