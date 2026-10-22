
/* Camundrum - Robot Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/character_body3d.hpp>
#include <Godot/classes/scene_tree.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/typed_array.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"
#include "scripts/shared/CrateBody.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every robot
JENOVA_PROPERTY(bool, starts_on, false)
JENOVA_PROPERTY(bool, starts_forward, true)
JENOVA_PROPERTY(double, move_speed, 2.0)

// A robot is a crate that drives itself: its body is the crate's (CrateBody.h), and this script adds the motor.
// It goes up and down in top, left and right in side, and in iso keeps the line of the flat view it came from: depth after top, left and right after side.
// Two senses are kept, one for left and right (sign_x) and one shared by top's up and down and iso's depth (sign_v): going up in top is going towards the camera in iso.
// The state of each robot lives in its node's metadata: the crate body's, plus starts_on, starts_forward, move_speed, on, sign_x, sign_v, iso_axis (0 left and right, 2 depth), blink

// Motor
Vector3 DriveVelocity(CharacterBody3D* robot)
{
	// Purpose: the velocity the robot drives itself at in the current view, or nothing while it is off.

	// Off, it is just a crate that cannot be pushed
	if (!(bool)robot->get_meta("on", false)) return Vector3(0.0f, 0.0f, 0.0f);
	float speed = (float)(double)robot->get_meta("move_speed", 2.0);
	float sign_x = (float)(double)robot->get_meta("sign_x", 1.0);
	float sign_v = (float)(double)robot->get_meta("sign_v", 1.0);

	// Each view has its own line, iso takes the one of the last flat view
	int view = robot->get_meta("view", VIEW_SIDE);
	if (view == VIEW_TOP) return Vector3(0.0f, sign_v * speed, 0.0f);
	if (view == VIEW_SIDE) return Vector3(sign_x * speed, 0.0f, 0.0f);
	if ((int)robot->get_meta("iso_axis", 2) == 2) return Vector3(0.0f, 0.0f, sign_v * speed);
	return Vector3(sign_x * speed, 0.0f, 0.0f);
}
void TurnAround(CharacterBody3D* robot)
{
	// Purpose: send the robot back the other way along the line it is driving on, after it bumped into something.

	// Flip the sense that the current line uses
	int view = robot->get_meta("view", VIEW_SIDE);
	bool vertical = view == VIEW_TOP || (view == VIEW_ISO && (int)robot->get_meta("iso_axis", 2) == 2);
	const char* key = vertical ? "sign_v" : "sign_x";
	robot->set_meta(key, -(double)robot->get_meta(key, 1.0));
}

// Look
void ShowAntenna(CharacterBody3D* robot, double delta)
{
	// Purpose: light the tip of the antenna while the robot runs, blinking, and leave it dark while it is off.

	// The lit tip sits over the dark one, and only shows while on
	Node3D* lit = (Node3D*)robot->get_node_or_null("Mesh/TipLit");
	if (!lit) return;
	if (!(bool)robot->get_meta("on", false))
	{
		lit->set_visible(false);
		return;
	}

	// Blink twice a second, mostly lit
	double blink = (double)robot->get_meta("blink", 0.0) + delta;
	if (blink > 0.5) blink -= 0.5;
	robot->set_meta("blink", blink);
	lit->set_visible(blink < 0.38);
}
void ShowFromAbove(CharacterBody3D* robot, double delta)
{
	// Purpose: turn the robot's look towards the camera in top, so its head and antenna are seen from above, and back in the other views.

	// The body's own look keeps its size (it shrinks when sinking) and its forward lift when raised; the turn goes on top
	Node3D* mesh = (Node3D*)robot->get_node_or_null("Mesh");
	if (!mesh) return;
	double turn = StepTopTurn(robot->get_meta("top_turn", -1.0), (int)robot->get_meta("view", VIEW_SIDE) == VIEW_TOP, delta);
	robot->set_meta("top_turn", turn);
	Vector3 size = mesh->get_scale();
	Vector3 lifted = mesh->get_position();
	Transform3D look = TurnedForTop(Transform3D(Basis::from_scale(size), Vector3()), Vector3(0.0f, 0.0f, 0.0f), TopTurnAngle(turn));
	look.origin = lifted;
	mesh->set_transform(look);
}
void SwitchTo(CharacterBody3D* robot, bool on)
{
	// Purpose: switch the robot on or off, heard when it changes.

	// Only a real change is heard, and the blink starts lit
	if ((bool)robot->get_meta("on", false) == on) return;
	robot->set_meta("on", on);
	robot->set_meta("blink", 0.0);
	PlaySound(robot, SOUND_BUTTON);
}

// Use
bool PlayerNextTo(CharacterBody3D* robot)
{
	// Purpose: tell whether the player is right next to the robot, close enough to switch it with the interact key.

	// The robot's box, grown by half a cell on every side
	AABB own;
	if (!CrateBody::Box(robot, own)) return false;
	own = own.grow(0.5f);

	// Touching that is touching the robot
	TypedArray<Node> players = robot->get_tree()->get_nodes_in_group("player");
	for (int index = 0; index < players.size(); index++)
	{
		Object* object = players[index];
		Node* player = (Node*)object;
		AABB box;
		if (player && ShapeWorldBox((CollisionShape3D*)player->get_node_or_null("Collision"), 1.0f, box) && box.intersects(own)) return true;
	}
	return false;
}

// Raised in top
void SetRaised(Caller* instance, int raised)
{
	// Purpose: let a fan raise the robot in top view, like a crate.
	// The parameter is a number because calls between scripts cannot carry a bool.

	// Hand it to the shared body
	CrateBody::SetRaised(GetSelf<CharacterBody3D>(instance), raised);
}
void DropIntoVoid(Caller* instance)
{
	// Purpose: blow the robot off the level in side view, like a crate.

	// Hand it to the shared body
	CrateBody::DropIntoVoid(GetSelf<CharacterBody3D>(instance));
}

// Commands
void SetCurrent(Caller* instance, double x, double y, double z)
{
	// Purpose: let a river segment or a fan carry the robot this frame, like a crate.

	// Hand it to the shared body
	CrateBody::SetCurrent(GetSelf<CharacterBody3D>(instance), x, y, z);
}
void Break(Caller* instance)
{
	// Purpose: destroy the robot, when it falls out of the level or sinks into a hole.

	// Take it out of the level until the next restart
	CrateBody::SetBroken(GetSelf<CharacterBody3D>(instance), true);
}
void Activate(Caller* instance)
{
	// Purpose: let a plate, lever or button switch the robot on.

	// On
	SwitchTo(GetSelf<CharacterBody3D>(instance), true);
}
void Deactivate(Caller* instance)
{
	// Purpose: let a plate, lever or button switch the robot off.

	// Off
	SwitchTo(GetSelf<CharacterBody3D>(instance), false);
}
void Toggle(Caller* instance)
{
	// Purpose: let a lever or button flip the robot between on and off.

	// Flip
	auto robot = GetSelf<CharacterBody3D>(instance);
	SwitchTo(robot, !(bool)robot->get_meta("on", false));
}

// Contract
void OnViewChanged(Caller* instance, int view)
{
	// Purpose: part of the element contract, gives the robot the physics rules of the new view and picks its line for iso.

	// A flat view decides which line the robot follows when iso comes back
	auto robot = GetSelf<CharacterBody3D>(instance);
	if (view == VIEW_TOP) robot->set_meta("iso_axis", 2);
	if (view == VIEW_SIDE) robot->set_meta("iso_axis", 0);
	CrateBody::ViewChanged(robot, view);
}
void OnInteract(Caller* instance)
{
	// Purpose: part of the element contract, switches the robot on or off when the player presses the interact key next to it.

	// Only a player at its side can use it
	auto robot = GetSelf<CharacterBody3D>(instance);
	if ((bool)robot->get_meta("broken", false) || !PlayerNextTo(robot)) return;
	SwitchTo(robot, !(bool)robot->get_meta("on", false));
}
void SetFrozen(Caller* instance, int frozen)
{
	// Purpose: part of the element contract, pauses the robot while the level changes shape.

	// Store the state, it drives on where it left off
	GetSelf<CharacterBody3D>(instance)->set_meta("frozen", frozen != 0);
}
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, returns the robot to its starting state when the level restarts.

	// The body comes back, the motor goes back to its inspector settings; depth is the line until a flat view says otherwise
	auto robot = GetSelf<CharacterBody3D>(instance);
	CrateBody::Reset(robot);
	double sign = (bool)robot->get_meta("starts_forward", true) ? 1.0 : -1.0;
	robot->set_meta("on", robot->get_meta("starts_on", false));
	robot->set_meta("sign_x", sign);
	robot->set_meta("sign_v", sign);
	robot->set_meta("iso_axis", 2);
	robot->set_meta("blink", 0.0);
	robot->set_meta("top_turn", -1.0);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node and its children are in the tree, prepares the robot.

	// The crate body, with the usual crate settings
	auto robot = GetSelf<CharacterBody3D>(instance);
	CrateBody::Ready(robot, 35.0, 22.0, -12.0);

	// Copy the inspector settings onto the node, where every function can read them safely, and set the motor up
	robot->set_meta("starts_on", starts_on);
	robot->set_meta("starts_forward", starts_forward);
	robot->set_meta("move_speed", move_speed);
	double sign = starts_forward ? 1.0 : -1.0;
	robot->set_meta("on", starts_on);
	robot->set_meta("sign_x", sign);
	robot->set_meta("sign_v", sign);
	robot->set_meta("iso_axis", 2);
	robot->set_meta("blink", 0.0);
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, drives the robot along its line and turns it around when something stops it.

	// Move with the body's rules plus its own drive
	auto robot = GetSelf<CharacterBody3D>(instance);
	Vector3 drive = DriveVelocity(robot);
	float managed = CrateBody::Step(robot, _delta, drive);
	ShowAntenna(robot, _delta);
	ShowFromAbove(robot, _delta);

	// Stopped short by a wall, a blocked row of crates, another robot or the player: go back the other way
	if (drive.length() > 0.01f && managed < 0.5f) TurnAround(robot);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
