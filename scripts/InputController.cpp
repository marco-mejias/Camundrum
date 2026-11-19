
/* Camundrum - Input Controller
 * Reads the keyboard once per frame and turns it into requests for the GameManager.
 */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/input.hpp>
#include <Godot/classes/global_constants.hpp>
#include <Godot/variant/variant.hpp>

// Game
#include "scripts/shared/GameTypes.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// State
Vector2 move_axis = Vector2(0.0f, 0.0f); // Movement keys this frame, -1 to 1 on each axis
bool jump_pressed = false; // Jump went down this frame
bool jump_was_held = false; // Jump state last frame, to catch the press
bool view_key_was_held[VIEW_COUNT] = { false, false, false }; // Keys 1, 2 and 3 last frame
bool restart_was_held = false; // R last frame
bool interact_was_held = false; // E last frame
bool pause_was_held = false; // Escape last frame
bool fullscreen_was_held = false; // F11 last frame

// ---- Sampling ----

// Turn the movement keys into a direction, x for left/right and y for back/forward
void SampleMoveAxis(Input* input)
{
	// 1. Start with no direction
	Vector2 axis = Vector2(0.0f, 0.0f);

	// 2. Horizontal keys, A/D or the left/right arrows
	if (input->is_physical_key_pressed(KEY_D) || input->is_physical_key_pressed(KEY_RIGHT)) axis.x += 1.0f;
	if (input->is_physical_key_pressed(KEY_A) || input->is_physical_key_pressed(KEY_LEFT)) axis.x -= 1.0f;

	// 3. Vertical keys, W/S or the up/down arrows
	if (input->is_physical_key_pressed(KEY_W) || input->is_physical_key_pressed(KEY_UP)) axis.y += 1.0f;
	if (input->is_physical_key_pressed(KEY_S) || input->is_physical_key_pressed(KEY_DOWN)) axis.y -= 1.0f;

	// 4. Keep diagonals from being faster than straight lines, then store the result
	if (axis.length() > 1.0f) axis = axis.normalized();
	move_axis = axis;
}

// Flag the jump only on the frame Space goes down, so holding it does not repeat it
void SampleJump(Input* input)
{
	// Compare this frame's key state with the previous one
	bool held = input->is_physical_key_pressed(KEY_SPACE);
	jump_pressed = held && !jump_was_held;

	// Remember the state for the next frame
	jump_was_held = held;
}

// Controlls the change of view, checking if the user is helding while changing it
void SampleViewKeys(Node* node, Input* input)
{
	// The key that asks for each view, in the same order as the ViewMode values
	const Key view_keys[VIEW_COUNT] = { KEY_1, KEY_2, KEY_3 };

	// Check every view key, and emit the request only when it was just pressed
	for (int view = 0; view < VIEW_COUNT; view++)
	{
		bool held = input->is_physical_key_pressed(view_keys[view]);
		if (held && !view_key_was_held[view]) node->emit_signal("view_requested", view);
		view_key_was_held[view] = held;
	}
}

// Announce a restart request on the frame the R key goes down
void SampleRestart(Node* node, Input* input)
{
	// Emit the request only when the key was just pressed
	bool held = input->is_physical_key_pressed(KEY_R);
	if (held && !restart_was_held) node->emit_signal("restart_requested");

	// Remember the state for the next frame
	restart_was_held = held;
}

// Announce an interact request on the frame the E key goes down
void SampleInteract(Node* node, Input* input)
{
	// Emit the request only when the key was just pressed
	bool held = input->is_physical_key_pressed(KEY_E);
	if (held && !interact_was_held) node->emit_signal("interact_requested");

	// Remember the state for the next frame
	interact_was_held = held;
}

// Announce a pause request on the frame the Escape key goes down
void SamplePause(Node* node, Input* input)
{
	// Emit the request only when the key was just pressed
	bool held = input->is_physical_key_pressed(KEY_ESCAPE);
	if (held && !pause_was_held) node->emit_signal("pause_requested");

	// Remember the state for the next frame
	pause_was_held = held;

	// F11 flips fullscreen at any time
	bool full = input->is_physical_key_pressed(KEY_F11);
	if (full && !fullscreen_was_held) node->emit_signal("fullscreen_requested");
	fullscreen_was_held = full;
}

// ---- Queries ----

// Let the GameManager read the movement direction sampled this frame
Vector2 GetMoveAxis()
{
	// Hand back the stored direction
	return move_axis;
}

// Let the GameManager ask whether a jump was requested this frame
bool IsJumpPressed()
{
	// Hand back the stored flag
	return jump_pressed;
}

// Let the GameManager ask whether the jump key is still down, which decides how high a jump goes
bool IsJumpHeld()
{
	// Hand back the key state sampled this frame
	return jump_was_held;
}

// ---- Routines ----

// Called once the node is in the tree, declares the signals this module emits
void OnReady(Caller* instance)
{
	// Create the signals the GameManager listens to, the view one carries the requested view
	auto node = GetSelf<Node>(instance);
	if (!node->has_signal("view_requested")) node->add_user_signal("view_requested");
	if (!node->has_signal("restart_requested")) node->add_user_signal("restart_requested");
	if (!node->has_signal("interact_requested")) node->add_user_signal("interact_requested");
	if (!node->has_signal("pause_requested")) node->add_user_signal("pause_requested");
	if (!node->has_signal("fullscreen_requested")) node->add_user_signal("fullscreen_requested");
}

// Called when the node leaves the scene tree, resets the script state
void OnDestroy(Caller* instance)
{
	// Forget the last direction and jump
	move_axis = Vector2(0.0f, 0.0f);
	jump_pressed = false;
	jump_was_held = false;

	// Forget which view and restart keys were down
	for (int view = 0; view < VIEW_COUNT; view++) view_key_was_held[view] = false;
	restart_was_held = false;
	interact_was_held = false;
	pause_was_held = false;
}

// Called every physics frame before the other modules, reads the keyboard once per frame
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// 1. Get this node and the input singleton
	auto node = GetSelf<Node>(instance);
	Input* input = Input::get_singleton();

	// 2. Refresh what the other modules will read this frame
	SampleMoveAxis(input);
	SampleJump(input);

	// 3. Report view change, restart, interact and pause requests
	SampleViewKeys(node, input);
	SampleRestart(node, input);
	SampleInteract(node, input);
	SamplePause(node, input);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
