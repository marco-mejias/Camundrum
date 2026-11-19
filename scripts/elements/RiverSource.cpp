
/* Camundrum - River Source Element
 * Builds a river from where it springs on the board, and reshapes it for each view.
 */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/scene_tree.hpp>
#include <Godot/classes/world3d.hpp>
#include <Godot/classes/collision_object3d.hpp>
#include <Godot/classes/physics_direct_space_state3d.hpp>
#include <Godot/classes/physics_ray_query_parameters3d.hpp>
#include <Godot/classes/packed_scene.hpp>
#include <Godot/classes/resource.hpp>
#include <Godot/classes/resource_loader.hpp>
#include <Godot/classes/cpu_particles3d.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/dictionary.hpp>
#include <Godot/variant/typed_array.hpp>

// Game
#include "scripts/shared/GameTypes.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Physics questions, kept outside the script block because Jenova cannot read these parameter types inside it
// List the bodies that move on their own, so the river's path ignores them: the water carries them, it does not stop at them
static TypedArray<RID> MovingBodies(Node* any)
{
	// The player and every crate
	TypedArray<RID> rids;
	const char* groups[2] = { "player", "crate" };
	for (const char* group : groups)
	{
		TypedArray<Node> nodes = any->get_tree()->get_nodes_in_group(group);
		for (int index = 0; index < nodes.size(); index++)
		{
			Object* object = nodes[index];
			if (object) rids.push_back(((CollisionObject3D*)object)->get_rid());
		}
	}
	return rids;
}

// Look along a straight line for the first solid piece of the world, and say where it was met
static bool CastRay(Node3D* origin, const Vector3& from, const Vector3& to, const TypedArray<RID>& ignored, Vector3& hit)
{
	// Only solid world pieces count, never areas such as plates or other water
	Ref<PhysicsRayQueryParameters3D> query = PhysicsRayQueryParameters3D::create(from, to, LAYER_WORLD, ignored);
	Dictionary result = origin->get_world_3d()->get_direct_space_state()->intersect_ray(query);
	if (result.is_empty()) return false;
	hit = result["position"];
	return true;
}

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every source
JENOVA_PROPERTY(double, flow_speed, 3.5)
JENOVA_PROPERTY(double, change_time, 0.5)

// The state of each source lives in its node's metadata: flow_speed, change_time, view, snap_next, the board segment's measures,
// the length of the deep path (ground_length, fall_length, fall_lands), which part of it holds water now (wet_from, wet_to, from 0 at the board to 1 at the end of the fall), and the board stretch's depth (thickness)

// ---- Building ----

// Add one straight piece of river, a water segment that flows along flow_axis and lies against the surface on the far side of face_axis
Node3D* MakeSegment(Node3D* source, const String& name, const Vector3& flow_axis, const Vector3& face_axis, const Vector3& center, bool on_board)
{
	// 1. Make a copy of the water piece
	Ref<Resource> resource = ResourceLoader::get_singleton()->load("res://scenes/elements/water.tscn");
	PackedScene* packed = (PackedScene*)resource.ptr();
	if (!packed) return nullptr;
	Node3D* segment = (Node3D*)packed->instantiate();
	if (!segment) return nullptr;

	// 2. It belongs to this source, not to the level, so the depth module leaves it where it is
	segment->set_name(name);
	segment->remove_from_group("depth");
	source->add_child(segment);

	// 3. Its own X axis carries the flow and its length, Y points away from the surface it lies on, Z is the width of one unit
	Basis basis;
	basis.set_column(0, flow_axis);
	basis.set_column(1, face_axis);
	basis.set_column(2, flow_axis.normalized().cross(face_axis.normalized()));
	segment->set_global_transform(Transform3D(basis, center));

	// 4. Remember its full length and where it starts, so it can be shown only partly while the water arrives or drains away
	segment->set_meta("full_flow", flow_axis);
	segment->set_meta("full_start", center - flow_axis * 0.5f);

	// 5. Give it this source's speed, and say whether it runs on the board
	segment->set_meta("flow_speed", source->get_meta("flow_speed", 3.5));
	segment->set_meta("on_board", on_board);
	return segment;
}

// Work out the river's path in the real layout and lay it out in straight segments
// It runs down the board, then straight along the ground towards the camera until the ground ends, then falls.
void BuildRiver(Node3D* source)
{
	// 1. The source sits on the face of the board, the water runs in the unit of depth just in front of it
	Vector3 start = source->get_global_position();
	float x = start.x;
	float board_z = start.z;
	float lane_z = board_z + 0.5f;
	TypedArray<RID> ignored = MovingBodies(source);

	// 2. Down the board until the first ground below the source
	Vector3 hit;
	if (!CastRay(source, Vector3(x, start.y, lane_z), Vector3(x, start.y - 30.0f, lane_z), ignored, hit)) return;
	float ground_y = hit.y;
	float board_length = start.y - ground_y;
	if (board_length > 0.05f)
	{
		MakeSegment(source, "BoardStretch", Vector3(0.0f, -board_length, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector3(x, ground_y + board_length * 0.5f, lane_z), true);
		source->set_meta("board_length", board_length);
		source->set_meta("board_center", Vector3(x, ground_y + board_length * 0.5f, lane_z));
	}

	// 3. Along the ground towards the camera: stop at the first solid piece in the way, or where the ground runs out
	float water_y = ground_y + 0.5f;
	float end_z = board_z + 30.0f;
	bool blocked = false;
	if (CastRay(source, Vector3(x, water_y, lane_z), Vector3(x, water_y, end_z), ignored, hit))
	{
		end_z = hit.z;
		blocked = true;
	}
	for (float z = lane_z; z < end_z; z += 0.25f)
	{
		if (CastRay(source, Vector3(x, ground_y + 0.25f, z), Vector3(x, ground_y - 0.25f, z), ignored, hit)) continue;

		// The edge lies within the last quarter unit: narrow it down, so the water ends right at it
		float on_ground = z - 0.25f;
		float past_edge = z;
		for (int step = 0; step < 8; step++)
		{
			float middle = (on_ground + past_edge) * 0.5f;
			if (CastRay(source, Vector3(x, ground_y + 0.25f, middle), Vector3(x, ground_y - 0.25f, middle), ignored, hit)) on_ground = middle;
			else past_edge = middle;
		}
		end_z = (on_ground + past_edge) * 0.5f;
		blocked = false;
		break;
	}
	float ground_length = end_z - board_z;
	Node3D* ground_stretch = nullptr;
	if (ground_length > 0.05f) ground_stretch = MakeSegment(source, "GroundStretch", Vector3(0.0f, 0.0f, ground_length), Vector3(0.0f, 1.0f, 0.0f), Vector3(x, water_y, board_z + ground_length * 0.5f), false);

	// 4. Where the river runs over an edge, it carries a little past it, so whatever it carries gets clear of the ground and falls
	if (ground_stretch && !blocked) ground_stretch->set_meta("overshoot", 0.15);
	source->set_meta("ground_length", ground_length > 0.05f ? ground_length : 0.0f);
	source->set_meta("fall_length", 0.0f);
	source->set_meta("fall_lands", false);

	// 5. Where the board stretch meets the ground, foam shows the river landing there in side view, in front of everything flattened
	Node3D* foot_splash = (Node3D*)source->get_node_or_null("FootSplash");
	float front_z = source->get_parent()->get_meta("flat_front_z", 4.5);
	if (foot_splash) foot_splash->set_global_position(Vector3(x, ground_y + 0.05f, front_z - 0.3f));

	// 6. Over the edge: fall until the next ground below, or a long way into the void
	if (blocked) return;
	float fall_top = ground_y + 0.06f;
	float fall_bottom = ground_y - 15.0f;
	bool lands = CastRay(source, Vector3(x, ground_y - 0.05f, end_z + 0.5f), Vector3(x, fall_bottom, end_z + 0.5f), ignored, hit);
	if (lands) fall_bottom = hit.y;
	float fall_length = fall_top - fall_bottom;
	MakeSegment(source, "Fall", Vector3(0.0f, -fall_length, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector3(x, fall_bottom + fall_length * 0.5f, end_z + 0.5f), false);

	// 7. Along the deep path the fall comes right after the ground stretch
	source->set_meta("fall_length", fall_length);
	source->set_meta("fall_lands", lands);

	// 8. Where the fall lands, foam shows it hitting the ground in iso
	Node3D* fall_splash = (Node3D*)source->get_node_or_null("FallSplash");
	if (fall_splash) fall_splash->set_global_position(Vector3(x, fall_bottom + 0.05f, end_z + 0.5f));
}

// How deep the board stretch is in the flat views, from the board to the front edge, so it carries what is flattened in front of it
double FlatThickness(Node3D* source)
{
	// From the board's face to the level's front edge, never thinner than one unit
	float board_z = source->get_global_position().z;
	float front_z = source->get_parent()->get_meta("flat_front_z", 4.5);
	return (front_z - board_z < 1.0f) ? 1.0 : (double)(front_z - board_z);
}

// Give the stretch on the board a depth: one unit against the board in iso, the whole level in the flat views, or anything between during a change
void FitBoardStretch(Node3D* source, double thickness)
{
	// 1. Nothing to fit without a stretch on the board
	Node3D* stretch = (Node3D*)source->get_node_or_null("BoardStretch");
	if (!stretch) return;

	// 2. It grows out from the board towards the front
	float board_z = source->get_global_position().z;
	float length = source->get_meta("board_length", 1.0);
	Vector3 center = source->get_meta("board_center", Vector3());
	center.z = board_z + (float)thickness * 0.5f;

	// 3. Rebuild its transform with the new thickness
	Basis basis;
	basis.set_column(0, Vector3(0.0f, -length, 0.0f));
	basis.set_column(1, Vector3(0.0f, 0.0f, (float)thickness));
	basis.set_column(2, Vector3(-1.0f, 0.0f, 0.0f));
	stretch->set_global_transform(Transform3D(basis, center));
}

// Show only the wet part of one deep segment, given where the segment starts along the deep path and which part of the path holds water
void ShowPart(Node3D* source, const String& name, double path_start, double wet_from, double wet_to)
{
	// 1. Without the segment there is nothing to show
	Node3D* segment = (Node3D*)source->get_node_or_null(name);
	if (!segment) return;
	Vector3 flow = segment->get_meta("full_flow", Vector3());
	Vector3 start = segment->get_meta("full_start", Vector3());
	double length = flow.length();
	if (length < 0.001) return;

	// 2. The wet stretch inside this segment, measured from its start
	double from = wet_from - path_start;
	double to = wet_to - path_start;
	if (from < 0.0) from = 0.0;
	if (to > length) to = length;

	// 3. Nothing wet here: the segment is hidden, which also stops it carrying anything
	if (to - from < 0.01)
	{
		segment->set_visible(false);
		return;
	}

	// 4. Stretch the segment over the wet part only, keeping its width and the surface it lies on
	Vector3 direction = flow / (float)length;
	Transform3D placement = segment->get_global_transform();
	placement.basis.set_column(0, direction * (float)(to - from));
	placement.origin = start + direction * (float)((from + to) * 0.5);
	segment->set_global_transform(placement);
	segment->set_visible(true);
}

// Turn how far the water has got, 0 at the board and 1 at the end of the fall, into a distance along the deep path
// The ground stretch takes the first 70% so the change along the ground can be seen; the fall, often much longer, takes the rest.
double PathDistance(Node3D* source, double amount)
{
	// Along the ground first
	double ground_length = source->get_meta("ground_length", 0.0);
	double fall_length = source->get_meta("fall_length", 0.0);
	if (amount <= 0.7) return amount / 0.7 * ground_length;

	// Then down the fall
	return ground_length + (amount - 0.7) / 0.3 * fall_length;
}

// Draw the river as it is now: which part of the ground stretch and the fall holds water, how deep the board stretch is, and where foam shows
void ShowWater(Node3D* source)
{
	// 1. The deep path is the ground stretch followed by the fall
	double ground_length = source->get_meta("ground_length", 0.0);
	double fall_length = source->get_meta("fall_length", 0.0);
	double wet_from = PathDistance(source, source->get_meta("wet_from", 0.0));
	double wet_to = PathDistance(source, source->get_meta("wet_to", 0.0));
	ShowPart(source, "GroundStretch", 0.0, wet_from, wet_to);
	ShowPart(source, "Fall", ground_length, wet_from, wet_to);
	FitBoardStretch(source, source->get_meta("thickness", 1.0));

	// 2. In side the river lands on the floor at the foot of the board
	int view = source->get_meta("view", VIEW_SIDE);
	CPUParticles3D* foot_splash = (CPUParticles3D*)source->get_node_or_null("FootSplash");
	if (foot_splash) foot_splash->set_emitting(view == VIEW_SIDE && source->get_node_or_null("BoardStretch") != nullptr);

	// 3. In iso it lands where the fall ends, while water is reaching the bottom of the fall
	double path = ground_length + fall_length;
	bool reaches_bottom = fall_length > 0.0 && wet_to >= path - 0.01 && wet_from < path - 0.5;
	CPUParticles3D* fall_splash = (CPUParticles3D*)source->get_node_or_null("FallSplash");
	if (fall_splash) fall_splash->set_emitting(view == VIEW_ISO && (bool)source->get_meta("fall_lands", false) && reaches_bottom);
}

// Put the river straight into the shape of a view, with no change shown, used when a level starts or restarts
void SettleWater(Node3D* source, int view)
{
	// Iso holds water along the whole deep path, the flat views none of it
	source->set_meta("wet_from", view == VIEW_ISO ? 0.0 : 1.0);
	source->set_meta("wet_to", 1.0);
	source->set_meta("thickness", view == VIEW_ISO ? 1.0 : FlatThickness(source));
	ShowWater(source);
}

// Move a value towards a target by at most one step, without going past it
double StepTowards(double value, double target, double step)
{
	// Step up or down, stopping exactly on the target
	if (value < target) return (value + step > target) ? target : value + step;
	return (value - step < target) ? target : value - step;
}

// Pass a contract call on to the water segments, which are this source's children and not seen by the GameManager
void ForwardToSegments(Node3D* source, const StringName& method, int value)
{
	// Call it on every child that has it
	for (int index = 0; index < source->get_child_count(); index++)
	{
		Node* segment = source->get_child(index);
		if (segment->has_method(method)) segment->call(method, value);
	}
}

// ---- Contract ----

// Part of the element contract, passes the view to the water and fits the stretch on the board to it
void OnViewChanged(Caller* instance, int view)
{
	// 1. Remember the view and tell the segments
	auto source = GetSelf<Node3D>(instance);
	int previous = source->get_meta("view", VIEW_SIDE);
	source->set_meta("view", view);
	ForwardToSegments(source, "OnViewChanged", view);

	// 2. When the level starts or restarts the river takes its shape at once
	if ((bool)source->get_meta("snap_next", true))
	{
		source->set_meta("snap_next", false);
		SettleWater(source, view);
		return;
	}

	// 3. Side and top have no depth, so there the river is only the stretch on the board, the ground part and the fall only exist in iso
	// Leaving iso, the water still on the ground drains away over the edge; entering iso, it runs out along the ground again from the board
	// The per-frame routine plays the change, here a dry path is only made ready to fill from the board
	bool was_dry = (double)source->get_meta("wet_from", 0.0) >= (double)source->get_meta("wet_to", 0.0) - 0.001;
	if (view == VIEW_ISO && previous != VIEW_ISO && was_dry)
	{
		source->set_meta("wet_from", 0.0);
		source->set_meta("wet_to", 0.0);
	}
	ShowWater(source);
}

// Part of the element contract, stops the water while the level changes shape
void SetFrozen(Caller* instance, int frozen)
{
	// Pass it on to the segments
	ForwardToSegments(GetSelf<Node3D>(instance), "SetFrozen", frozen);
}

// Part of the element contract, makes the river take its shape at once in the view the level restarts in
void Reset(Caller* instance)
{
	// The next view the river is told about is applied with no change shown
	GetSelf<Node3D>(instance)->set_meta("snap_next", true);
}

// ---- Routines ----

// Called once the node is in the tree, works out the river's path while the level is still in its real layout
void OnReady(Caller* instance)
{
	// Copy the inspector setting onto the node, where every function can read it safely
	auto source = GetSelf<Node3D>(instance);
	source->set_meta("flow_speed", flow_speed);
	source->set_meta("change_time", change_time);
	source->set_meta("view", VIEW_SIDE);
	source->set_meta("snap_next", true);

	// Lay out the river, full as in iso, until the game says which view it starts in
	BuildRiver(source);
	SettleWater(source, VIEW_ISO);
}

// Called every frame, plays the change of the river between views: water draining off the ground, or running out along it again, while the board stretch deepens or thins
void OnProcess(Caller* instance, double _delta)
{
	// 1. Where the water and the board stretch are heading in this view
	auto source = GetSelf<Node3D>(instance);
	bool iso = (int)source->get_meta("view", VIEW_SIDE) == VIEW_ISO;
	double from_target = iso ? 0.0 : 1.0;
	double thick_target = iso ? 1.0 : FlatThickness(source);

	// 2. Nothing to do once the river has its shape
	double wet_from = source->get_meta("wet_from", 0.0);
	double wet_to = source->get_meta("wet_to", 0.0);
	double thickness = source->get_meta("thickness", 1.0);
	if (wet_from == from_target && wet_to == 1.0 && thickness == thick_target) return;

	// 3. The water covers the whole path in change_time: in iso the front runs out first, in the flat views the tail drains away
	double time = (double)source->get_meta("change_time", 0.5);
	if (time < 0.01) time = 0.01;
	double step = _delta / time;
	source->set_meta("wet_to", StepTowards(wet_to, 1.0, step));
	source->set_meta("wet_from", StepTowards(wet_from, from_target, step));

	// 4. The board stretch changes depth in the same time
	double thick_step = (FlatThickness(source) - 1.0) * _delta / time;
	source->set_meta("thickness", StepTowards(thickness, thick_target, thick_step > 0.0001 ? thick_step : 1.0));
	ShowWater(source);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
