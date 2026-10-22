/* Camundrum - Shared Crate Body */

#pragma once

// C++ Runtime
#include <cmath>
#include <vector>

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/character_body3d.hpp>
#include <Godot/classes/kinematic_collision3d.hpp>
#include <Godot/classes/collision_shape3d.hpp>
#include <Godot/classes/scene_tree.hpp>
#include <Godot/variant/typed_array.hpp>
#include <Godot/variant/variant.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// The body every crate-like element shares: crates, and robots that drive themselves.
// It falls, is carried by rivers, raised by fans, sinks into holes and is lost out of the level; the scripts only add what makes each one different.
// Its state lives in the node's metadata: gravity, max_fall, fall_limit, view, frozen, broken, fall, push, flow, drift, sink, raised, air_left, dropping, lift
namespace CrateBody
{
using namespace godot;

// Rows
inline bool Box(Node* crate, AABB& box)
{
	// Purpose: measure the box a crate fills in the world right now.

	// Its collision box, as placed in the world
	return ShapeWorldBox((CollisionShape3D*)crate->get_node_or_null("Collision"), 1.0f, box);
}
inline CharacterBody3D* NextInRow(CharacterBody3D* crate, const Vector3& heading, bool flat, const std::vector<CharacterBody3D*>& row)
{
	// Purpose: find the crate touching this one straight ahead along a push, which the push moves too, or nothing.
	// Robots are never moved this way, they drive themselves and stand in the way like a wall.

	// Where this crate is, and along which axis it is pushed
	AABB own;
	if (!Box(crate, own)) return nullptr;
	int axis = 0;
	if (fabsf(heading.y) > fabsf(heading[axis])) axis = 1;
	if (fabsf(heading.z) > fabsf(heading[axis])) axis = 2;
	float sign = heading[axis] < 0.0f ? -1.0f : 1.0f;
	float front = (sign > 0.0f) ? own.position[axis] + own.size[axis] : own.position[axis];

	// Look at every other crate still in the level
	TypedArray<Node> crates = crate->get_tree()->get_nodes_in_group("crate");
	for (int index = 0; index < crates.size(); index++)
	{
		Object* object = crates[index];
		CharacterBody3D* other = (CharacterBody3D*)object;
		if (!other || !other->is_visible_in_tree() || other->is_in_group("robot")) continue;
		bool listed = false;
		for (CharacterBody3D* member : row) listed = listed || member == other;
		if (listed) continue;
		AABB box;
		if (!Box(other, box)) continue;

		// Its near face has to be right at this crate's front face
		float back = (sign > 0.0f) ? box.position[axis] : box.position[axis] + box.size[axis];
		float gap = (back - front) * sign;
		if (gap < -0.15f || gap > 0.08f) continue;

		// And it has to really stand in the way on the other axes, not just brush a corner; the depth does not count in a flat view
		bool in_the_way = true;
		for (int other_axis = 0; other_axis < 3; other_axis++)
		{
			if (other_axis == axis || (flat && other_axis == 2)) continue;
			float low = own.position[other_axis] > box.position[other_axis] ? own.position[other_axis] : box.position[other_axis];
			float high_own = own.position[other_axis] + own.size[other_axis];
			float high_box = box.position[other_axis] + box.size[other_axis];
			float high = high_own < high_box ? high_own : high_box;
			if (high - low < 0.2f) in_the_way = false;
		}
		if (in_the_way) return other;
	}
	return nullptr;
}
inline void FindRiders(CharacterBody3D* carrier, bool flat, std::vector<CharacterBody3D*>& riders)
{
	// Purpose: list the player and the crates standing on top of a body, which travel with it when it drives itself.

	// The top face of the carrier
	AABB own;
	if (!Box(carrier, own)) return;
	float top = own.position.y + own.size.y;

	// Anyone whose feet rest on that face, overlapping it across, and in depth too in iso
	const char* groups[2] = { "player", "crate" };
	for (const char* group : groups)
	{
		TypedArray<Node> bodies = carrier->get_tree()->get_nodes_in_group(group);
		for (int index = 0; index < bodies.size(); index++)
		{
			Object* object = bodies[index];
			CharacterBody3D* body = (CharacterBody3D*)object;
			if (!body || body == carrier || !body->is_visible_in_tree()) continue;
			AABB box;
			if (!ShapeWorldBox((CollisionShape3D*)body->get_node_or_null("Collision"), 1.0f, box)) continue;
			float feet = box.position.y;
			if (feet < top - 0.1f || feet > top + 0.15f) continue;
			float across = fminf(own.position.x + own.size.x, box.position.x + box.size.x) - fmaxf(own.position.x, box.position.x);
			float deep = fminf(own.position.z + own.size.z, box.position.z + box.size.z) - fmaxf(own.position.z, box.position.z);
			if (across < 0.1f || (!flat && deep < 0.1f)) continue;
			riders.push_back(body);
		}
	}
}
inline float MoveRow(CharacterBody3D* leader, const Vector3& motion, bool flat, const std::vector<CharacterBody3D*>& riders)
{
	// Purpose: move a crate and every crate lined up in front of it as one block, in this same frame, and carry whoever rides on the leader.
	// Each crate is tested against the world with the others of the row left out, and the whole row moves as far as the most blocked one allows.
	// Returns how far the row really went.

	// Line up the crates touching one after the other along the push
	float length = motion.length();
	if (length < 0.00001f) return 0.0f;
	Vector3 heading = motion / length;
	std::vector<CharacterBody3D*> row;
	row.push_back(leader);
	while (row.size() < 12)
	{
		CharacterBody3D* next = NextInRow(row.back(), heading, flat, row);
		if (!next) break;
		row.push_back(next);
	}

	// The crates of the row and the riders do not stop each other while they move together
	std::vector<CharacterBody3D*> group = row;
	for (CharacterBody3D* rider : riders) group.push_back(rider);
	for (size_t first = 0; first < group.size(); first++)
		for (size_t second = first + 1; second < group.size(); second++) group[first]->add_collision_exception_with(group[second]);

	// The row goes as far as its most blocked crate can
	float travel = length;
	for (CharacterBody3D* member : row)
	{
		Ref<KinematicCollision3D> hit = member->move_and_collide(heading * travel, true);
		if (hit.is_valid())
		{
			float allowed = hit->get_travel().length();
			if (allowed < travel) travel = allowed;
		}
	}

	// Move them all by that much, the riders along with them as far as they can go
	if (travel > 0.0001f)
	{
		for (CharacterBody3D* member : row) member->move_and_collide(heading * travel);
		for (CharacterBody3D* rider : riders) rider->move_and_collide(heading * travel);
	}

	// Let them collide with each other again
	for (size_t first = 0; first < group.size(); first++)
		for (size_t second = first + 1; second < group.size(); second++) group[first]->remove_collision_exception_with(group[second]);
	return travel;
}

// Modes
inline void ApplyMode(CharacterBody3D* crate)
{
	// Purpose: configure the crate for the current view, so the right physics rules apply to it.

	// Top view has no floor to rest on, so the crate floats there and sits on the ground otherwise
	bool top = (int)crate->get_meta("view", VIEW_SIDE) == VIEW_TOP;
	crate->set_motion_mode(top ? CharacterBody3D::MOTION_MODE_FLOATING : CharacterBody3D::MOTION_MODE_GROUNDED);

	// The edges of the board only stop the crate in top view; raised by a fan, only they and other raised bodies do
	bool raised = top && (bool)crate->get_meta("raised", false);
	if (raised) crate->set_collision_mask(LAYER_BOARD_BOUNDS | LAYER_RAISED);
	else crate->set_collision_mask(top ? (LAYER_WORLD | LAYER_BOARD_BOUNDS) : LAYER_WORLD);
	crate->set_collision_layer(raised ? (LAYER_ACTORS | LAYER_RAISED) : (LAYER_WORLD | LAYER_ACTORS));
}
inline void ClearForces(CharacterBody3D* crate)
{
	// Purpose: forget the push and the river current received for this frame.

	// Drop both
	crate->set_meta("push", Vector3(0.0f, 0.0f, 0.0f));
	crate->set_meta("flow", Vector3(0.0f, 0.0f, 0.0f));
}
inline void SetBroken(CharacterBody3D* crate, bool broken)
{
	// Purpose: take the crate out of the level or bring it back.

	// Remember the state and stop any movement
	crate->set_meta("broken", broken);
	crate->set_meta("fall", 0.0);
	crate->set_meta("drift", Vector3(0.0f, 0.0f, 0.0f));
	crate->set_velocity(Vector3(0.0f, 0.0f, 0.0f));
	ClearForces(crate);

	// A broken crate is not drawn and blocks nothing, the change waits for the end of the frame
	crate->set_visible(!broken);
	Node* shape = crate->get_node_or_null("Collision");
	if (shape) shape->set_deferred("disabled", broken);
}

// Raised in top
inline void SetRaised(CharacterBody3D* crate, int raised)
{
	// Purpose: let a fan raise the crate in top view, so it can be pushed up onto walls.

	// Only in top, and only when it changes
	if ((int)crate->get_meta("view", VIEW_SIDE) != VIEW_TOP || (bool)crate->get_meta("raised", false) == (raised != 0)) return;
	crate->set_meta("raised", raised != 0);
	crate->set_meta("air_left", 0.3);
	ApplyMode(crate);
}
inline void DropIntoVoid(CharacterBody3D* crate)
{
	// Purpose: blow the crate off the level in side view: tossed up a little, in front of everything, it falls through it all and is lost when out of the level.

	// Only once
	if ((bool)crate->get_meta("broken", false) || (bool)crate->get_meta("dropping", false)) return;
	crate->set_meta("dropping", true);

	// It no longer touches anything, and is drawn in front of the whole level
	crate->set_collision_mask(0);
	crate->set_collision_layer(0);
	Vector3 position = crate->get_global_position();
	position.z = (float)(double)crate->get_parent()->get_meta("flat_front_z", 4.5) + 1.0f;
	crate->set_global_position(position);
	crate->set_meta("fall", 4.0);
}
inline void AnimateRaise(CharacterBody3D* crate, double delta)
{
	// Purpose: draw a crate raised in top: it comes forward towards the camera and its shadow slips off to the side; coming down is quick.

	// Ease towards up or down
	bool up = (int)crate->get_meta("view", VIEW_SIDE) == VIEW_TOP && (bool)crate->get_meta("raised", false);
	double lift = crate->get_meta("lift", 0.0);
	lift += ((up ? 1.0 : 0.0) - lift) * (1.0 - exp(-(up ? 18.0 : 30.0) * delta));
	if (lift < 0.002) lift = 0.0;
	crate->set_meta("lift", lift);

	// The look follows, sized after the crate's own box
	Node3D* mesh = (Node3D*)crate->get_node_or_null("Mesh");
	if (mesh) mesh->set_position(Vector3(0.0f, 0.0f, (float)(0.7 * lift)));
	AABB box;
	Vector3 size = Vector3(0.96f, 0.96f, 0.96f);
	if (ShapeWorldBox((CollisionShape3D*)crate->get_node_or_null("Collision"), 1.0f, box)) size = box.size;
	ShowRaiseShadow(crate, (float)lift, size.x, size.y);
}

// Void
inline void StartSinking(CharacterBody3D* crate)
{
	// Purpose: start the crate sinking into the holes under it, after which it plugs them.

	// It stops blocking at once and is heard landing in the hole, which counts as floor from now on so nothing falls in behind it
	crate->set_meta("sink", 0.0);
	Node3D* hole = FillVoidUnder(crate);

	// It sinks into the middle of the cell it plugs, so it is clear which one it filled
	if (hole)
	{
		Vector3 at = crate->get_global_position();
		Vector3 center = hole->get_global_position();
		crate->set_global_position(Vector3(center.x, center.y, at.z));
	}
	crate->set_velocity(Vector3(0.0f, 0.0f, 0.0f));
	ClearForces(crate);
	Node* shape = crate->get_node_or_null("Collision");
	if (shape) shape->set_deferred("disabled", true);
	PlaySound(crate, SOUND_CRATE_LAND);
}
inline void AnimateSinking(CharacterBody3D* crate, double delta)
{
	// Purpose: shrink the crate away into the void over a third of a second, then plug the holes it covered.

	// Advance and shrink
	double sink = (double)crate->get_meta("sink", -1.0) + delta / 0.3;
	crate->set_meta("sink", sink);
	Node3D* mesh = (Node3D*)crate->get_node_or_null("Mesh");
	float size = (float)(1.0 - (sink > 1.0 ? 1.0 : sink) * 0.85);
	if (mesh) mesh->set_scale(Vector3(size, size, size));
	if (sink < 1.0) return;

	// Done: the crate is gone until the level restarts, the holes it covered stay plugged
	if (mesh) mesh->set_scale(Vector3(1.0f, 1.0f, 1.0f));
	crate->set_meta("sink", -1.0);
	SetBroken(crate, true);
}

// Commands
inline void SetCurrent(CharacterBody3D* crate, double x, double y, double z)
{
	// Purpose: let a river segment or a fan carry the crate this frame, combining the currents of every one it touches.

	// Keep, on each axis, whichever push is stronger, so where two stretches meet the crate feels both
	Vector3 incoming = Vector3(x, y, z);
	Vector3 stored = crate->get_meta("flow", Vector3(0.0f, 0.0f, 0.0f));
	for (int axis = 0; axis < 3; axis++)
	{
		float a = incoming[axis] < 0.0f ? -incoming[axis] : incoming[axis];
		float b = stored[axis] < 0.0f ? -stored[axis] : stored[axis];
		if (a > b) stored[axis] = incoming[axis];
	}
	crate->set_meta("flow", stored);
}

// Contract
inline void ViewChanged(CharacterBody3D* crate, int view)
{
	// Purpose: give the crate the physics rules of the new view.

	// Top view has no momentum, so a fall in progress is dropped when entering or leaving it
	int old_view = crate->get_meta("view", VIEW_SIDE);

	// Leaving top while up on something: in side it hangs in front of it with nothing below and is lost; in iso the depth module placed it against it
	if ((bool)crate->get_meta("raised", false) && old_view == VIEW_TOP && view == VIEW_SIDE && OverRaisedGround(crate)) DropIntoVoid(crate);
	if (view != VIEW_TOP) crate->set_meta("raised", false);
	if ((view == VIEW_TOP) != (old_view == VIEW_TOP)) crate->set_meta("fall", 0.0);

	// Store the view and configure the body for it
	crate->set_meta("view", view);
	ApplyMode(crate);
}
inline void Reset(CharacterBody3D* crate)
{
	// Purpose: return the crate to its starting state when the level restarts.

	// Bring the crate back, its position is restored by the DepthController
	SetBroken(crate, false);
	crate->set_meta("frozen", false);
	crate->set_meta("sink", -1.0);
	crate->set_meta("raised", false);
	crate->set_meta("dropping", false);
	crate->set_meta("lift", 0.0);
	ApplyMode(crate);
	Node3D* mesh = (Node3D*)crate->get_node_or_null("Mesh");
	if (mesh) mesh->set_scale(Vector3(1.0f, 1.0f, 1.0f));
}
inline void Ready(CharacterBody3D* crate, double gravity, double max_fall, double fall_limit)
{
	// Purpose: prepare the crate once it is in the tree.

	// Keep the settings on the node, where every function can read them safely
	crate->set_meta("gravity", gravity);
	crate->set_meta("max_fall", max_fall);
	crate->set_meta("fall_limit", fall_limit);

	// A robot carrying the crate moves it itself, so Godot must not add the floor's speed on top
	crate->set_platform_floor_layers(0);

	// Start whole, still, in the view the game normally begins in
	crate->set_meta("view", VIEW_SIDE);
	crate->set_meta("frozen", false);
	crate->set_meta("sink", -1.0);
	SetBroken(crate, false);
	ApplyMode(crate);
}

// Routine
inline float Step(CharacterBody3D* crate, double delta, const Vector3& drive)
{
	// Purpose: move the crate for one physics frame with its push, its own drive, the river and gravity.
	// The drive is how a robot moves itself; it pushes crates ahead and carries riders. Returns the share of the drive the crate really managed this frame, 1 when it had none to do.

	// A broken or frozen crate does nothing, apart from keeping its shadow in place
	int view = crate->get_meta("view", VIEW_SIDE);
	UpdateDropShadow(crate, view == VIEW_ISO && !(bool)crate->get_meta("broken", false));
	if ((bool)crate->get_meta("broken", false)) return 1.0f;
	AnimateRaise(crate, delta);

	// Blown off the level in side, the crate only falls, through everything, until it is out of the level
	if ((bool)crate->get_meta("dropping", false))
	{
		double falling = (double)crate->get_meta("fall", 0.0) - (double)crate->get_meta("gravity", 35.0) * delta;
		crate->set_meta("fall", falling);
		crate->set_velocity(Vector3(0.0f, (float)falling, 0.0f));
		crate->move_and_slide();
		if (crate->get_global_position().y < (double)crate->get_meta("fall_limit", -12.0)) SetBroken(crate, true);
		return 1.0f;
	}

	// A crate sinking into the void finishes doing so, even while the world waits
	if ((double)crate->get_meta("sink", -1.0) >= 0.0)
	{
		AnimateSinking(crate, delta);
		return 1.0f;
	}
	if ((bool)crate->get_meta("frozen", false)) return 1.0f;
	Vector3 push = crate->get_meta("push", Vector3(0.0f, 0.0f, 0.0f));
	Vector3 flow = crate->get_meta("flow", Vector3(0.0f, 0.0f, 0.0f));
	double fall = crate->get_meta("fall", 0.0);

	// The push moves the crate, and any crates lined up in front of it, as one block, only along the axes the view allows
	Vector3 before = crate->get_global_position();
	std::vector<CharacterBody3D*> no_riders;
	Vector3 shove = Vector3(push.x, (view == VIEW_TOP) ? push.y : 0.0f, (view == VIEW_ISO) ? push.z : 0.0f);
	if (shove.length() > 0.01f) MoveRow(crate, shove * (float)delta, view != VIEW_ISO, no_riders);

	// Its own drive moves it the same way, carrying whoever stands on it; in top nothing stands on anything
	float managed = 1.0f;
	Vector3 own = Vector3(drive.x, (view == VIEW_TOP) ? drive.y : 0.0f, (view == VIEW_ISO) ? drive.z : 0.0f);
	if (own.length() > 0.01f)
	{
		std::vector<CharacterBody3D*> riders;
		if (view != VIEW_TOP) FindRiders(crate, view != VIEW_ISO, riders);
		float wanted = own.length() * (float)delta;
		managed = MoveRow(crate, own * (float)delta, view != VIEW_ISO, riders) / wanted;
	}

	// Across the ground the crate otherwise only moves while a river carries it; thrown off an edge by the current it keeps drifting while it falls
	Vector3 drift = crate->get_meta("drift", Vector3(0.0f, 0.0f, 0.0f));
	Vector3 carried = Vector3(flow.x, 0.0f, (view == VIEW_ISO) ? flow.z : 0.0f);
	if (carried.length() > 0.01f) drift = carried;
	else if (view == VIEW_TOP || crate->is_on_floor()) drift = Vector3(0.0f, 0.0f, 0.0f);
	else if (flow.y < -0.01f) drift *= (float)exp(-10.0 * delta);
	crate->set_meta("drift", drift);
	Vector3 velocity = Vector3(drift.x, 0.0f, 0.0f);

	// Top view has no gravity, the push and the board decide everything
	if (view == VIEW_TOP) fall = 0.0;
	else
	{
		// In side and iso the crate falls, up to a top speed, and rests when on the ground
		if (crate->is_on_floor()) fall = 0.0;
		fall -= (double)crate->get_meta("gravity", 35.0) * delta;
		double max_fall = crate->get_meta("max_fall", 22.0);
		if (fall < -max_fall) fall = -max_fall;
		velocity.y = (float)fall + flow.y;

		// Depth only moves in iso
		if (view == VIEW_ISO) velocity.z = drift.z;
	}
	crate->set_meta("fall", fall);

	// Fall and drift with the river, colliding with everything
	crate->set_velocity(velocity);
	crate->move_and_slide();

	// Forget this frame's push and current
	ClearForces(crate);

	// Landing after a real fall is heard
	if (view != VIEW_TOP && fall < -6.0 && crate->is_on_floor()) PlaySound(crate, SOUND_CRATE_LAND);

	// Side and top keep the crate on its plane, done here rather than with an axis lock so the depth module can still move it
	Vector3 after = crate->get_global_position();
	if (view != VIEW_ISO && after.z != before.z) crate->set_global_position(Vector3(after.x, after.y, before.z));

	// Raised in top, the crate stays up for a moment only after a fan's jump; pushed off a wall it comes straight down
	bool raised = view == VIEW_TOP && (bool)crate->get_meta("raised", false);
	bool lands = false;
	if (raised)
	{
		int under = RaisedGroundUnder(crate);
		double air_left = crate->get_meta("air_left", 0.3);
		if (under == 2) air_left = 0.3;
		else if (under == 1) air_left = 0.0;
		lands = under == 0 && air_left <= 0.0;
		if (under == 0) air_left -= delta;
		crate->set_meta("air_left", air_left);
	}
	if (lands)
	{
		crate->set_meta("raised", false);
		ApplyMode(crate);
	}

	// In top the board is the floor: a crate whose whole base is over open holes has nothing under it and sinks into them, unless it is up on something
	if (view == VIEW_TOP && !(bool)crate->get_meta("raised", false) && FootprintOverVoid(crate)) StartSinking(crate);

	// A crate that falls out of the level is lost until the next restart
	if (after.y < (double)crate->get_meta("fall_limit", -12.0)) SetBroken(crate, true);
	return managed;
}
}
