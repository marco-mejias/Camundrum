
/* Camundrum - Player Controller */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/character_body3d.hpp>
#include <Godot/classes/physics_server3d.hpp>
#include <Godot/classes/kinematic_collision3d.hpp>
#include <Godot/classes/mesh_instance3d.hpp>
#include <Godot/classes/standard_material3d.hpp>
#include <Godot/classes/cpu_particles3d.hpp>
#include <Godot/variant/variant.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties
JENOVA_PROPERTY(double, move_speed, 6.0)
JENOVA_PROPERTY(double, ground_acceleration, 70.0)
JENOVA_PROPERTY(double, ground_deceleration, 90.0)
JENOVA_PROPERTY(double, air_acceleration, 40.0)
JENOVA_PROPERTY(double, jump_speed, 9.0)
JENOVA_PROPERTY(double, rise_gravity, 25.0)
JENOVA_PROPERTY(double, fall_gravity, 45.0)
JENOVA_PROPERTY(double, max_fall_speed, 22.0)
JENOVA_PROPERTY(double, jump_cut, 0.45)
JENOVA_PROPERTY(double, coyote_time, 0.1)
JENOVA_PROPERTY(double, jump_buffer_time, 0.12)
JENOVA_PROPERTY(double, fall_limit, -12.0)
JENOVA_PROPERTY(double, push_speed, 3.5)

// State
CharacterBody3D* body = nullptr;
int current_mode = VIEW_SIDE;
Vector2 input_axis = Vector2(0.0f, 0.0f);
bool jump_requested = false;
bool jump_held = false;
bool is_jump_rising = false;
double coyote_left = 0.0;
double buffer_left = 0.0;
bool death_reported = false;
bool is_frozen = false;
Vector3 current_flow = Vector3(0.0f, 0.0f, 0.0f);
Vector3 applied_flow = Vector3(0.0f, 0.0f, 0.0f);
Vector3 own_before_move = Vector3(0.0f, 0.0f, 0.0f);
int death_cause = DEATH_FALL;
bool death_playing = false;
double death_clock = 0.0;
bool exit_playing = false;
double exit_clock = 0.0;
bool jumped_now = false;
bool was_grounded = true;
double squash = 0.0;
double squash_speed = 0.0;
double lean_x = 0.0;
double lean_z = 0.0;
double lift = 0.0;
double top_turn = -1.0;

// Modes
void ApplyMode()
{
	// Purpose: configure the body for the current view mode, so the right physics rules apply to it.

	// Do nothing until the body is known
	if (!body) return;

	// Top view has no floor to stand on, so it floats, the others walk on the ground
	if (current_mode == VIEW_TOP) body->set_motion_mode(CharacterBody3D::MOTION_MODE_FLOATING);
	else body->set_motion_mode(CharacterBody3D::MOTION_MODE_GROUNDED);

	// The edges of the board only stop the player in top view; raised by a fan, only they and other raised bodies do
	bool raised = current_mode == VIEW_TOP && (bool)body->get_meta("raised", false);
	if (raised) body->set_collision_mask(LAYER_BOARD_BOUNDS | LAYER_RAISED);
	else if (current_mode == VIEW_TOP) body->set_collision_mask(LAYER_WORLD | LAYER_BOARD_BOUNDS);
	else body->set_collision_mask(LAYER_WORLD);
	body->set_collision_layer(raised ? (LAYER_ACTORS | LAYER_RAISED) : (LAYER_WORLD | LAYER_ACTORS));
}
void ClearJumpState()
{
	// Purpose: forget every pending or running jump, used whenever the player is reset or changes rules.

	// Drop the request, the buffered press and the grace period after leaving a ledge
	jump_requested = false;
	buffer_left = 0.0;
	coyote_left = 0.0;

	// No jump is on its way up any more
	is_jump_rising = false;
}

// Look
void ResetLook()
{
	// Purpose: forget any squash or lean, so the player is drawn plain again.

	// No spring motion left
	squash = 0.0;
	squash_speed = 0.0;
	lean_x = 0.0;
	lean_z = 0.0;
	top_turn = -1.0;
	was_grounded = true;
	jumped_now = false;
}
void Puff()
{
	// Purpose: throw a small cloud of dust from the player's feet, on a jump or a hard landing.

	// The Dust particles start again from the beginning
	CPUParticles3D* dust = body ? (CPUParticles3D*)body->get_node_or_null("Dust") : nullptr;
	if (!dust) return;
	dust->restart();
	dust->set_emitting(true);
}
void Kick(double amount)
{
	// Purpose: push the squash spring, positive stretches the player tall and thin, negative squashes it flat and wide.

	// Give the spring a push, it swings back to rest by itself
	squash_speed += amount;
}
void UpdateLook(double delta)
{
	// Purpose: draw the player's body with its squash and its lean into the movement, every frame.

	// Nothing to draw while a death plays, the death animation owns the body then
	MeshInstance3D* mesh = (MeshInstance3D*)body->get_node_or_null("Mesh");
	if (!mesh || death_playing || death_reported) return;

	// Getting into the exit: spin around and shrink away
	if (exit_playing)
	{
		exit_clock += delta;
		double amount = exit_clock / 0.45;
		if (amount > 1.0) amount = 1.0;
		float size = (float)(1.0 - amount * amount);
		mesh->set_scale(Vector3(size, size, size));
		mesh->set_position(Vector3(0.0f, 0.0f, 0.0f));
		mesh->set_rotation(Vector3(0.0f, (float)(amount * 12.0), 0.0f));
		mesh->set_transform(TurnedForTop(mesh->get_transform(), Vector3(0.0f, 0.0f, 0.0f), TopTurnAngle(top_turn)));
		return;
	}

	// The squash is a spring: it swings around zero and settles quickly
	double pull = -260.0 * squash - 18.0 * squash_speed;
	squash_speed += pull * delta;
	squash += squash_speed * delta;
	if (squash > 0.4) squash = 0.4;
	if (squash < -0.4) squash = -0.4;

	// The body leans a little into its horizontal speed, towards the side it moves to
	Vector3 velocity = body->get_velocity() - applied_flow;
	double wanted_z = (current_mode == VIEW_TOP) ? 0.0 : -velocity.x / move_speed * 0.12;
	double wanted_x = (current_mode == VIEW_ISO) ? velocity.z / move_speed * 0.12 : 0.0;
	double follow = 1.0 - exp(-14.0 * delta);
	lean_z += (wanted_z - lean_z) * follow;
	lean_x += (wanted_x - lean_x) * follow;

	// Raised by a fan in top, the body comes forward towards the camera and its shadow slips off to the side; coming down is quick
	bool up = current_mode == VIEW_TOP && (bool)body->get_meta("raised", false);
	lift += ((up ? 1.0 : 0.0) - lift) * (1.0 - exp(-(up ? 18.0 : 30.0) * delta));
	if (lift < 0.002) lift = 0.0;
	ShowRaiseShadow(body, (float)lift, 0.9f, 0.9f);

	// Stretch on one axis and thin out on the others, keeping the feet on the ground
	float tall = (float)(1.0 + squash);
	float wide = (float)(1.0 - squash * 0.5);
	mesh->set_scale(Vector3(wide, tall, wide));
	mesh->set_position(Vector3(0.0f, (tall - 1.0f) * 0.5f, 0.0f));
	mesh->set_rotation(Vector3((float)lean_x, 0.0f, (float)lean_z));

	// In top the body is seen from above, turned so the top of the head faces the camera; raised by a fan it also comes forward
	top_turn = StepTopTurn(top_turn, current_mode == VIEW_TOP, delta);
	Transform3D look = TurnedForTop(mesh->get_transform(), Vector3(0.0f, 0.0f, 0.0f), TopTurnAngle(top_turn));
	look.origin.z += (float)(0.7 * lift);
	mesh->set_transform(look);
}

// Commands
void SetMoveInput(double axis_x, double axis_y, int jump_pressed, int jump_is_held)
{
	// Purpose: let the GameManager hand over this frame's movement direction and the state of the jump key.
	// The parameters are plain numbers because calls between scripts cannot carry a Vector2 or a bool.

	// Rebuild and store the direction, the next physics step moves the player with it
	input_axis = Vector2(axis_x, axis_y);

	// Note a fresh press so it can be buffered, and whether the key is still down for the jump height
	if (jump_pressed != 0) jump_requested = true;
	jump_held = jump_is_held != 0;
}
void SetMode(int new_mode)
{
	// Purpose: let the GameManager change which physics rules the player follows.

	// Top view has no momentum, so nothing carries over when entering or leaving it
	bool touches_top = (new_mode == VIEW_TOP) != (current_mode == VIEW_TOP);
	if (body && touches_top) body->set_velocity(Vector3(0.0f, 0.0f, 0.0f));
	if (touches_top) ClearJumpState();
	if (touches_top) applied_flow = Vector3(0.0f, 0.0f, 0.0f);

	// Leaving top while up on something: going to side means hanging in front of it with nothing below, a fall into the void
	// Going to iso the depth module already placed the player against the front of what was below
	bool was_raised = body && (bool)body->get_meta("raised", false);
	if (was_raised && current_mode == VIEW_TOP && new_mode == VIEW_SIDE && OverRaisedGround(body)) body->call_deferred("DropIntoVoid");
	if (body && new_mode != VIEW_TOP) body->set_meta("raised", false);

	// Store the mode and configure the body for it right away
	current_mode = new_mode;
	ApplyMode();

}
void SetFrozen(int frozen)
{
	// Purpose: let the GameManager pause the player while the level changes shape, and resume it afterwards.
	// The parameter is a number because calls between scripts cannot carry a bool.

	// Store the state, the velocity is kept so movement resumes exactly where it left off
	is_frozen = frozen != 0;
}
void SetCurrent(double x, double y, double z)
{
	// Purpose: let a river segment carry the player this frame, combining the currents of every segment it touches.
	// Where two stretches meet, such as the foot of the board, the player feels both: on each axis the strongest push counts.

	// Keep, on each axis, whichever push is stronger
	Vector3 incoming = Vector3(x, y, z);
	for (int axis = 0; axis < 3; axis++)
	{
		float a = incoming[axis] < 0.0f ? -incoming[axis] : incoming[axis];
		float b = current_flow[axis] < 0.0f ? -current_flow[axis] : current_flow[axis];
		if (a > b) current_flow[axis] = incoming[axis];
	}
}
void Kill(int cause)
{
	// Purpose: let a hazard kill the player, saying how, so the right death is shown.

	// Report the death once, after this frame's work is done so the hazard is not interrupted
	if (!body || death_reported) return;
	death_reported = true;
	death_cause = cause;
	body->call_deferred("emit_signal", "died");
}
void ShowDeath()
{
	// Purpose: let the GameManager start the death animation that matches its cause.

	// A fall simply vanishes, the others play out over the next frames
	MeshInstance3D* mesh = body ? (MeshInstance3D*)body->get_node_or_null("Mesh") : nullptr;
	if (!mesh) return;
	if (death_cause == DEATH_FALL)
	{
		mesh->set_visible(false);
		return;
	}
	death_playing = true;
	death_clock = 0.0;

	// Being hurt by spikes turns the player red
	if (death_cause == DEATH_SPIKES)
	{
		Ref<StandardMaterial3D> hurt;
		hurt.instantiate();
		hurt->set_albedo(Color(1.0f, 0.15f, 0.15f, 1.0f));
		mesh->set_surface_override_material(0, hurt);
	}
}
void AnimateDeath(double delta)
{
	// Purpose: play the death animation frame by frame: a hurt bounce for spikes, sinking away for a hole.

	// Advance the clock, the animation lasts a third of a second and then the player is hidden
	MeshInstance3D* mesh = (MeshInstance3D*)body->get_node_or_null("Mesh");
	if (!mesh) return;
	death_clock += delta;
	double amount = death_clock / 0.35;
	if (amount >= 1.0)
	{
		mesh->set_visible(false);
		death_playing = false;
		return;
	}

	// Spikes throw the player up a little and blink it
	if (death_cause == DEATH_SPIKES)
	{
		mesh->set_position(Vector3(0.0f, (float)(0.7 * sin(amount * 3.14159265358979)), 0.0f));
		mesh->set_visible(((int)(death_clock * 20.0)) % 2 == 0);
		return;
	}

	// A hole swallows the player, which shrinks away
	float size = (float)(1.0 - amount);
	mesh->set_scale(Vector3(size, size, size));
}
void ShowExit()
{
	// Purpose: let the GameManager play the player's way out of the level, spinning and shrinking into the exit.

	// The animation runs in the per-frame routine
	exit_playing = true;
	exit_clock = 0.0;
}
void SetRaised(int raised)
{
	// Purpose: let a fan raise the player in top view, like a jump in a game seen from above, so it can go up onto walls.
	// The parameter is a number because calls between scripts cannot carry a bool.

	// Only in top, and only when it changes
	if (!body || current_mode != VIEW_TOP || (bool)body->get_meta("raised", false) == (raised != 0)) return;
	body->set_meta("raised", raised != 0);
	body->set_meta("air_left", 0.3);
	ApplyMode();
	if (raised != 0)
	{
		PlaySound(body, SOUND_JUMP);
		Kick(3.0);
	}
}
void DropIntoVoid()
{
	// Purpose: blow the player off the level in side view: it is thrown up a little, in front of everything, and falls through it all out of sight.
	// The fall out of the level is what kills it, so the drop is seen to the end.

	// Only once, and not while a death is already playing
	if (!body || death_reported || (bool)body->get_meta("dropping", false)) return;
	body->set_meta("dropping", true);

	// It no longer touches anything, and is drawn in front of the whole level
	body->set_collision_mask(0);
	body->set_collision_layer(0);
	Vector3 position = body->get_global_position();
	position.z = (float)(double)body->get_parent()->get_meta("flat_front_z", 4.5) + 1.0f;
	body->set_global_position(position);

	// A little toss before the fall
	Vector3 velocity = body->get_velocity();
	body->set_velocity(Vector3(velocity.x * 0.3f, 4.0f, 0.0f));
	ClearJumpState();
	PlaySound(body, SOUND_JUMP);
}
void StopMotion()
{
	// Purpose: let the GameManager stop the player's movement after the level turned, without touching anything else.

	// Drop the speed, any jump and any current
	if (body) body->set_velocity(Vector3(0.0f, 0.0f, 0.0f));
	ClearJumpState();
	current_flow = Vector3(0.0f, 0.0f, 0.0f);
	applied_flow = Vector3(0.0f, 0.0f, 0.0f);
}
void Halt()
{
	// Purpose: let the GameManager stop the player dead, used when the level restarts.

	// Drop all movement, pending input and any river current
	if (body) body->set_velocity(Vector3(0.0f, 0.0f, 0.0f));
	input_axis = Vector2(0.0f, 0.0f);
	jump_held = false;
	ClearJumpState();
	current_flow = Vector3(0.0f, 0.0f, 0.0f);
	applied_flow = Vector3(0.0f, 0.0f, 0.0f);

	// Show the player again as it was, and allow a new death to be reported
	MeshInstance3D* mesh = body ? (MeshInstance3D*)body->get_node_or_null("Mesh") : nullptr;
	if (mesh)
	{
		mesh->set_visible(true);
		mesh->set_position(Vector3(0.0f, 0.0f, 0.0f));
		mesh->set_scale(Vector3(1.0f, 1.0f, 1.0f));
		mesh->set_rotation(Vector3(0.0f, 0.0f, 0.0f));
		mesh->set_surface_override_material(0, Ref<Material>());
	}
	death_playing = false;
	death_reported = false;
	exit_playing = false;
	if (body) body->set_meta("raised", false);
	if (body) body->set_meta("dropping", false);
	lift = 0.0;
	ApplyMode();
	ResetLook();
}

// River
Vector3 OwnVelocity(Vector3 moved_at)
{
	// Purpose: work out the player's own speed after last frame's move, without the river current that was added to it.
	// A collision can only have taken speed away: standing on the ground while the water pushes down must not turn into a push up.

	// What is left after taking the current off, on each axis
	Vector3 own = moved_at - applied_flow;
	for (int axis = 0; axis < 3; axis++)
	{
		// Where no current was added, the move is the player's own
		if (applied_flow[axis] == 0.0f) continue;

		// Otherwise keep the result between zero and the speed the player had of its own before the move
		float before = own_before_move[axis];
		float low = before < 0.0f ? before : 0.0f;
		float high = before > 0.0f ? before : 0.0f;
		if (own[axis] < low) own[axis] = low;
		if (own[axis] > high) own[axis] = high;
	}
	return own;
}

// Movement
double Approach(double current, double target, double step)
{
	// Purpose: move a value towards a target by at most one step, without going past it.

	// Step up or down, stopping exactly on the target
	if (current < target) return (current + step > target) ? target : current + step;
	if (current > target) return (current - step < target) ? target : current - step;
	return target;
}
double Steer(double current, double wanted, bool grounded, double delta)
{
	// Purpose: bring the speed on one axis towards the speed the input asks for, with acceleration instead of an instant change.

	// Pick how hard to push: braking and speeding up differ on the ground, and the air gives less control
	double rate = air_acceleration;
	if (grounded) rate = (wanted == 0.0) ? ground_deceleration : ground_acceleration;

	// Close the gap by what that push allows this frame
	return Approach(current, wanted, rate * delta);
}
double SolveVertical(double vertical, double delta)
{
	// Purpose: work out the vertical speed for this frame, covering gravity and every rule of the jump.

	// Standing on something refills the grace period that still allows a jump just after leaving a ledge
	if (body->is_on_floor()) coyote_left = coyote_time;
	else coyote_left -= delta;

	// A fresh press is remembered for a moment, so pressing slightly before landing still jumps
	if (jump_requested) buffer_left = jump_buffer_time;
	else buffer_left -= delta;
	jump_requested = false;

	// Gravity pulls harder on the way down than on the way up, up to a top falling speed
	vertical -= ((vertical > 0.0) ? rise_gravity : fall_gravity) * delta;
	if (vertical < -max_fall_speed) vertical = -max_fall_speed;

	// Jump when a remembered press meets ground or the grace period, and use both up
	if (buffer_left > 0.0 && coyote_left > 0.0)
	{
		vertical = jump_speed;
		buffer_left = 0.0;
		coyote_left = 0.0;
		is_jump_rising = true;
		jumped_now = true;
	}

	// Letting go of the key while still rising cuts the jump short, holding it gives the full height
	if (is_jump_rising && vertical <= 0.0) is_jump_rising = false;
	if (is_jump_rising && !jump_held)
	{
		vertical *= jump_cut;
		is_jump_rising = false;
	}
	return vertical;
}
Vector3 MoveSide(Vector2 axis, double delta)
{
	// Purpose: classic platformer movement, left/right on X with gravity and a jump on Y.

	// Start from the current velocity so speed carries over between frames
	Vector3 velocity = body->get_velocity();

	// Run on X with acceleration and never drift in depth
	velocity.x = Steer(velocity.x, axis.x * move_speed, body->is_on_floor(), delta);
	velocity.z = 0.0f;

	// Fall and jump
	velocity.y = SolveVertical(velocity.y, delta);
	return velocity;
}
Vector3 MoveTop(Vector2 axis, double delta)
{
	// Purpose: free movement over the board on X and Y, with no gravity and no jump.

	// Start from the current velocity so speed carries over between frames
	Vector3 velocity = body->get_velocity();

	// Both axes follow the input with ground acceleration, since the board is the floor here
	velocity.x = Steer(velocity.x, axis.x * move_speed, true, delta);
	velocity.y = Steer(velocity.y, axis.y * move_speed, true, delta);
	velocity.z = 0.0f;
	return velocity;
}
Vector3 MoveIso(Vector2 axis, double delta)
{
	// Purpose: 3D movement, walking on X and Z with gravity and a jump on Y.

	// Start from the current velocity so speed carries over between frames
	Vector3 velocity = body->get_velocity();

	// Walk on the ground plane with acceleration, forward goes away from the viewer which is negative Z
	bool grounded = body->is_on_floor();
	velocity.x = Steer(velocity.x, axis.x * move_speed, grounded, delta);
	velocity.z = Steer(velocity.z, -axis.y * move_speed, grounded, delta);

	// Fall and jump
	velocity.y = SolveVertical(velocity.y, delta);
	return velocity;
}
void KeepDepth(double plane_z)
{
	// Purpose: put the player back on its depth plane if a collision nudged it off, used in the flat views.

	// Only correct an actual drift
	Vector3 position = body->get_global_position();
	if (position.z == plane_z) return;
	position.z = plane_z;
	body->set_global_position(position);
}

// Pushing
Vector3 WantedDirection()
{
	// Purpose: turn the input direction into a direction in the world, using the axes the current view moves on.

	// Side moves on X, top on X and Y, iso on X and depth with forward going away from the viewer
	if (current_mode == VIEW_TOP) return Vector3(input_axis.x, input_axis.y, 0.0f);
	if (current_mode == VIEW_ISO) return Vector3(input_axis.x, 0.0f, -input_axis.y);
	return Vector3(input_axis.x, 0.0f, 0.0f);
}
Vector3 PushAxis(Vector3 direction)
{
	// Purpose: snap a push direction to the single strongest axis the current view allows, so crates move along the grid.

	// Pick the axes allowed: side only X, top X and Y, iso X and depth
	float x = direction.x;
	float y = (current_mode == VIEW_TOP) ? direction.y : 0.0f;
	float z = (current_mode == VIEW_ISO) ? direction.z : 0.0f;

	// Keep the strongest of them, with its sign, or no push at all when none of them is allowed
	float ax = x < 0.0f ? -x : x;
	float ay = y < 0.0f ? -y : y;
	float az = z < 0.0f ? -z : z;
	if (ax < 0.001f && ay < 0.001f && az < 0.001f) return Vector3(0.0f, 0.0f, 0.0f);
	if (ax >= ay && ax >= az) return Vector3(x < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
	if (ay >= az) return Vector3(0.0f, y < 0.0f ? -1.0f : 1.0f, 0.0f);
	return Vector3(0.0f, 0.0f, z < 0.0f ? -1.0f : 1.0f);
}
void PushTouchedCrates()
{
	// Purpose: after moving, push any crate the player walked into in the direction the player wants to go.

	// No push without a direction
	Vector3 wanted = WantedDirection();
	if (wanted.length() < 0.1f) return;
	wanted = wanted.normalized();

	// Look at everything the player bumped into during this move
	for (int index = 0; index < body->get_slide_collision_count(); index++)
	{
		// Only crates can be pushed
		Ref<KinematicCollision3D> hit = body->get_slide_collision(index);
		Node* other = (Node*)hit->get_collider();
		if (!other || !other->is_in_group("pushable")) continue;

		// Standing on a crate or bumping it from below is not a push, in side and iso only a touch from the side counts
		Vector3 normal = hit->get_normal();
		if (current_mode != VIEW_TOP && (normal.y > 0.5f || normal.y < -0.5f)) continue;

		// The push goes into the crate, snapped to one axis, and only if that is where the player is heading
		Vector3 push = PushAxis(-hit->get_normal());
		if (push.dot(wanted) < 0.5f) continue;
		other->call("Push", (double)(push.x * push_speed), (double)(push.y * push_speed), (double)(push.z * push_speed));

		// Keep walking at the crate's speed along the push, so the bump does not stop the player and contact is kept
		Vector3 velocity = body->get_velocity();
		if (push.x != 0.0f) velocity.x = push.x * push_speed + applied_flow.x;
		if (push.y != 0.0f) velocity.y = push.y * push_speed + applied_flow.y;
		if (push.z != 0.0f) velocity.z = push.z * push_speed + applied_flow.z;
		body->set_velocity(velocity);
	}
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node and its children are in the tree, prepares the body and its signal.

	// Keep the body this script moves
	body = GetSelf<CharacterBody3D>(instance);

	// Create the signal the GameManager listens to for deaths
	if (!body->has_signal("died")) body->add_user_signal("died");

	// Whatever carries the player (a robot, a lift) moves it itself, so Godot must not add the floor's speed on top
	body->set_platform_floor_layers(0);

	// Configure the body for the starting mode
	ApplyMode();
}
void OnDestroy(Caller* instance)
{
	// Purpose: called when the node leaves the scene tree, resets the script state.

	// Forget the body so nothing uses it after it is freed
	body = nullptr;
	current_mode = VIEW_SIDE;

	// Forget the last input received, any jump and any current
	input_axis = Vector2(0.0f, 0.0f);
	jump_held = false;
	ClearJumpState();
	current_flow = Vector3(0.0f, 0.0f, 0.0f);
	applied_flow = Vector3(0.0f, 0.0f, 0.0f);

	// Back to the starting flags
	death_reported = false;
	is_frozen = false;
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, moves the player for the current mode using the input it was given.

	// Do nothing until the body is known
	if (!body) return;

	// A death in progress keeps playing even though the world is paused
	if (death_playing) AnimateDeath(_delta);

	// Blown off the level in side, the player only falls, through everything, until the fall out of the level ends it
	if ((bool)body->get_meta("dropping", false))
	{
		Vector3 falling = body->get_velocity();
		falling.y -= (float)(fall_gravity * _delta);
		body->set_velocity(falling);
		body->move_and_slide();
		if (body->get_global_position().y < fall_limit) Kill(DEATH_FALL);
		return;
	}

	// While frozen the player holds still, and a jump pressed meanwhile is not kept for later
	if (is_frozen)
	{
		jump_requested = false;
		return;
	}

	// Take away the river current added last frame, so the movement rules only see the player's own speed
	body->set_velocity(OwnVelocity(body->get_velocity()));

	// Work out the velocity using the rules of the current mode
	Vector3 velocity;
	if (current_mode == VIEW_TOP) velocity = MoveTop(input_axis, _delta);
	else if (current_mode == VIEW_ISO) velocity = MoveIso(input_axis, _delta);
	else velocity = MoveSide(input_axis, _delta);

	// Top view has no jump, so a press there is dropped instead of waiting for another view
	if (current_mode == VIEW_TOP) jump_requested = false;

	// Add this frame's river current on top, the water already leaves out what the view does not allow
	applied_flow = (current_mode == VIEW_TOP) ? Vector3(0.0f, 0.0f, 0.0f) : current_flow;
	current_flow = Vector3(0.0f, 0.0f, 0.0f);

	// Hand the velocity to the body and let it move and collide
	Vector3 before_move = body->get_global_position();
	double plane_z = before_move.z;
	own_before_move = velocity;
	body->set_velocity(velocity + applied_flow);
	body->move_and_slide();

	// A jump stretches the player and kicks up dust
	if (jumped_now)
	{
		jumped_now = false;
		Kick(4.5);
		Puff();
		PlaySound(body, SOUND_JUMP);
	}

	// Touching down squashes it, harder the faster it fell, and a hard landing also raises dust
	bool grounded = current_mode != VIEW_TOP && body->is_on_floor();
	if (grounded && !was_grounded && velocity.y < -3.0f)
	{
		double impact = -velocity.y / max_fall_speed;
		Kick(-2.0 - 6.0 * impact);
		if (velocity.y < -8.0f) Puff();
		PlaySound(body, SOUND_LAND);
	}
	was_grounded = grounded || current_mode == VIEW_TOP;

	// Side and top keep the player on its plane, done here rather than with an axis lock so the depth module can still move it
	if (current_mode != VIEW_ISO) KeepDepth(plane_z);

	// Raised in top, the player stays up for a moment in the air after leaving what it stood on, like a jump, then comes down
	// Reaching something to stand on in that moment keeps it up there
	// The moment in the air only comes from a fan's jump: walking off a wall brings the player straight down
	bool raised = (bool)body->get_meta("raised", false);
	if (current_mode == VIEW_TOP && raised)
	{
		int under = RaisedGroundUnder(body);
		double air_left = body->get_meta("air_left", 0.3);
		if (under == 2) air_left = 0.3;
		else if (under == 1) air_left = 0.0;
		bool lands = under == 0 && air_left <= 0.0;
		if (under == 0) air_left -= _delta;
		body->set_meta("air_left", air_left);
		if (lands)
		{
			body->set_meta("raised", false);
			ApplyMode();
			Kick(-2.5);
			PlaySound(body, SOUND_LAND);
			raised = false;
		}
	}

	// In top the board is the floor: with its centre over an open hole the player has nothing under it and falls into the void, unless it is up on something
	Vector3 center = body->get_global_position();
	if (current_mode == VIEW_TOP && !raised && VoidAt(body, center.x, center.y)) Kill(DEATH_HOLE);

	// Push the crates the player walked into
	PushTouchedCrates();

	// Show the shadow below the player in iso, where it tells the depth and the landing spot
	UpdateDropShadow(body, current_mode == VIEW_ISO);

	// Falling out of the level kills the player
	if (body->get_global_position().y < fall_limit) Kill(DEATH_FALL);
}

void OnProcess(Caller* instance, double _delta)
{
	// Purpose: called every frame, draws the squash and the lean, which are only looks and can run at the screen's rate.

	// Do nothing until the body is known
	if (!body) return;
	UpdateLook(_delta);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
