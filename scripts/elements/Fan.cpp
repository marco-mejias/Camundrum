
/* Camundrum - Fan Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/static_body3d.hpp>
#include <Godot/classes/collision_shape3d.hpp>
#include <Godot/classes/box_shape3d.hpp>
#include <Godot/classes/cpu_particles3d.hpp>
#include <Godot/classes/scene_tree.hpp>
#include <Godot/classes/world3d.hpp>
#include <Godot/classes/physics_direct_space_state3d.hpp>
#include <Godot/classes/physics_ray_query_parameters3d.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/dictionary.hpp>
#include <Godot/variant/typed_array.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Physics questions, kept outside the script block because Jenova cannot read these parameter types inside it
static float WindReach(Node3D* fan, float face_z, float front_z)
{
	// Purpose: find how far the wind gets from the board: up to the first solid piece in front of the fan, or the front edge of the level.
	// The player and crates do not stop the wind, it blows them along.

	// Leave out the bodies that move by themselves, and the fan's own wall of air
	TypedArray<RID> ignored;
	const char* groups[2] = { "player", "crate" };
	for (const char* group : groups)
	{
		TypedArray<Node> nodes = fan->get_tree()->get_nodes_in_group(group);
		for (int index = 0; index < nodes.size(); index++)
		{
			Object* object = nodes[index];
			if (object) ignored.push_back(((CollisionObject3D*)object)->get_rid());
		}
	}
	CollisionObject3D* wall = (CollisionObject3D*)fan->get_node_or_null("Wall");
	if (wall) ignored.push_back(wall->get_rid());

	// Look straight out from the middle of the fan
	Vector3 center = fan->get_global_position();
	Ref<PhysicsRayQueryParameters3D> query = PhysicsRayQueryParameters3D::create(Vector3(center.x, center.y, face_z + 0.05f), Vector3(center.x, center.y, front_z), LAYER_WORLD, ignored);
	Dictionary hit = fan->get_world_3d()->get_direct_space_state()->intersect_ray(query);
	if (hit.is_empty()) return front_z;
	Vector3 position = hit["position"];
	return position.z;
}

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every fan
JENOVA_PROPERTY(double, wind_speed, 7.0)
JENOVA_PROPERTY(bool, starts_on, true)

// The state of each fan lives in its node's metadata: wind_speed, starts_on, on, view, frozen, face_z, front_z, spin
// A fan sits on the board and blows straight out of it, towards the camera: it pushes in iso, blows into the void in side, and raises in top like a jump seen from above

// Look and feel
void ApplyState(Node3D* fan)
{
	// Purpose: make the fan behave as its switch says, and show the gust while it runs.

	// The column of air never stops anything by itself: in side it blows bodies into the void instead
	// The depth module still reads solid_view on it, so a switch to side that would leave something inside the column is refused; -1 means never
	bool on = fan->get_meta("on", true);
	Node* wall = fan->get_node_or_null("Wall");
	if (wall)
	{
		wall->set_meta("solid_view", on ? VIEW_SIDE : -1);
		CollisionShape3D* shape = (CollisionShape3D*)wall->get_node_or_null("Collision");
		if (shape) shape->set_disabled(true);
	}

	// The gust of air shows while the fan runs
	CPUParticles3D* gust = (CPUParticles3D*)fan->get_node_or_null("Gust");
	if (gust) gust->set_emitting(on);
}
void BuildColumn(Node3D* fan)
{
	// Purpose: size the column of air from the board to the front edge of the level, for the wall of side and the gust.

	// From the fan, on the board's face, to the level's front edge
	float face_z = fan->get_global_position().z;
	float front_z = (float)(double)fan->get_parent()->get_meta("flat_front_z", 4.5);
	float length = front_z - face_z;
	if (length < 0.5f) length = 0.5f;
	fan->set_meta("face_z", face_z);
	fan->set_meta("front_z", front_z);

	// The wall of air fills the fan's cell along the whole depth, so in side it stands wherever the player is flattened to
	CollisionShape3D* shape = (CollisionShape3D*)fan->get_node_or_null("Wall/Collision");
	if (shape)
	{
		Ref<BoxShape3D> box;
		box.instantiate();
		box->set_size(Vector3(0.96f, 0.96f, length));
		shape->set_shape(box);
		shape->set_position(Vector3(0.0f, 0.0f, length * 0.5f));
	}

	// The gust lives long enough to cross the whole column
	CPUParticles3D* gust = (CPUParticles3D*)fan->get_node_or_null("Gust");
	double speed = fan->get_meta("wind_speed", 7.0);
	if (gust && speed > 0.1) gust->set_lifetime(length / speed);
}

void BlowFlat(Node3D* fan, int view)
{
	// Purpose: blow on what is over the fan in a flat view: in side it is blown off the level into the void, in top it is raised like a jump.

	// The fan's cell across the board
	Vector3 center = fan->get_global_position();
	Vector3 scale = fan->get_global_transform().basis.get_scale();
	float half_x = 0.48f * scale.x;
	float half_y = 0.48f * scale.y;

	// The player and every crate
	const char* groups[2] = { "player", "crate" };
	for (const char* group : groups)
	{
		TypedArray<Node> bodies = fan->get_tree()->get_nodes_in_group(group);
		for (int index = 0; index < bodies.size(); index++)
		{
			Object* object = bodies[index];
			Node3D* body = (Node3D*)object;
			if (!body || !body->is_visible_in_tree()) continue;
			AABB box;
			if (!ShapeWorldBox((CollisionShape3D*)body->get_node_or_null("Collision"), 0.9f, box)) continue;

			// In side, touching the column at all blows it into the void
			if (view == VIEW_SIDE)
			{
				bool touches = box.position.x < center.x + half_x && box.position.x + box.size.x > center.x - half_x && box.position.y < center.y + half_y && box.position.y + box.size.y > center.y - half_y;
				if (!touches) continue;
				if (body->has_method("DropIntoVoid")) body->call("DropIntoVoid");
				continue;
			}

			// In top, standing with its centre on the fan raises it
			Vector3 at = box.get_center();
			if (fabsf(at.x - center.x) < half_x && fabsf(at.y - center.y) < half_y && body->has_method("SetRaised")) body->call("SetRaised", 1);
		}
	}
}

// Contract
void OnViewChanged(Caller* instance, int view)
{
	// Purpose: part of the element contract, sets the fan up for the view that starts.

	// Store the view and apply it
	auto fan = GetSelf<Node3D>(instance);
	fan->set_meta("view", view);
	ApplyState(fan);
}
void SetFrozen(Caller* instance, int frozen)
{
	// Purpose: part of the element contract, stops the wind while the level changes shape.

	// Store it
	GetSelf<Node3D>(instance)->set_meta("frozen", frozen != 0);
}
void Activate(Caller* instance)
{
	// Purpose: let a plate, lever or button switch the fan on.

	// On, and apply it
	auto fan = GetSelf<Node3D>(instance);
	fan->set_meta("on", true);
	ApplyState(fan);
}
void Deactivate(Caller* instance)
{
	// Purpose: let a plate, lever or button switch the fan off.

	// Off, and apply it
	auto fan = GetSelf<Node3D>(instance);
	fan->set_meta("on", false);
	ApplyState(fan);
}
void Toggle(Caller* instance)
{
	// Purpose: let a lever or button flip the fan between on and off.

	// Flip, and apply it
	auto fan = GetSelf<Node3D>(instance);
	fan->set_meta("on", !(bool)fan->get_meta("on", true));
	ApplyState(fan);
}
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, puts the fan back as it started when the level restarts.

	// Back to its starting switch
	auto fan = GetSelf<Node3D>(instance);
	fan->set_meta("on", fan->get_meta("starts_on", true));
	fan->set_meta("frozen", false);
	ApplyState(fan);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node and its children are in the tree, builds the column of air and starts the fan.

	// Copy the inspector settings onto the node, where every function can read them safely
	auto fan = GetSelf<Node3D>(instance);
	fan->set_meta("wind_speed", wind_speed);
	fan->set_meta("starts_on", starts_on);
	fan->set_meta("on", starts_on);
	fan->set_meta("view", VIEW_SIDE);
	fan->set_meta("frozen", false);
	fan->set_meta("spin", 0.0);

	// Size the column and apply the starting state
	BuildColumn(fan);
	ApplyState(fan);
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, blows on the player and crates as the view says: away from the board in iso, into the void in side, up in top.

	// Only while running and while the world is not waiting
	auto fan = GetSelf<Node3D>(instance);
	if (!(bool)fan->get_meta("on", true) || (bool)fan->get_meta("frozen", false)) return;
	int view = fan->get_meta("view", VIEW_SIDE);
	if (view != VIEW_ISO)
	{
		BlowFlat(fan, view);
		return;
	}

	// The column of air, across the fan's cell and from the board to the first solid piece in front
	float face_z = (float)(double)fan->get_meta("face_z", 0.0);
	float reach = WindReach(fan, face_z, (float)(double)fan->get_meta("front_z", 4.5));
	Vector3 center = fan->get_global_position();
	Vector3 scale = fan->get_global_transform().basis.get_scale();
	AABB column = AABB(Vector3(center.x - 0.48f * scale.x, center.y - 0.48f * scale.y, face_z), Vector3(0.96f * scale.x, 0.96f * scale.y, reach - face_z));

	// Every body in it is blown away from the board, combined with any river current it is in
	double speed = fan->get_meta("wind_speed", 7.0);
	const char* groups[2] = { "player", "crate" };
	for (const char* group : groups)
	{
		TypedArray<Node> bodies = fan->get_tree()->get_nodes_in_group(group);
		for (int index = 0; index < bodies.size(); index++)
		{
			Object* object = bodies[index];
			Node3D* body = (Node3D*)object;
			if (!body || !body->is_visible_in_tree() || !body->has_method("SetCurrent")) continue;
			AABB box;
			if (!ShapeWorldBox((CollisionShape3D*)body->get_node_or_null("Collision"), 0.9f, box) || !box.intersects(column)) continue;
			body->call("SetCurrent", 0.0, 0.0, speed);
		}
	}
}
void OnProcess(Caller* instance, double _delta)
{
	// Purpose: called every frame, spins the blades while the fan runs.

	// Turn the blades around the fan's axis
	auto fan = GetSelf<Node3D>(instance);
	if (!(bool)fan->get_meta("on", true)) return;
	Node3D* blades = (Node3D*)fan->get_node_or_null("Blades");
	if (!blades) return;
	double spin = (double)fan->get_meta("spin", 0.0) + _delta * 14.0;
	if (spin > 6.2831853) spin -= 6.2831853;
	fan->set_meta("spin", spin);
	blades->set_rotation(Vector3(0.0f, 0.0f, (float)spin));
}

// Jenova Script Block End
JENOVA_SCRIPT_END
