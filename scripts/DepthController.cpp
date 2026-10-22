
/* Camundrum - Depth Controller */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/character_body3d.hpp>
#include <Godot/classes/collision_shape3d.hpp>
#include <Godot/classes/shape3d.hpp>
#include <Godot/classes/world3d.hpp>
#include <Godot/classes/physics_direct_space_state3d.hpp>
#include <Godot/classes/physics_shape_query_parameters3d.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/dictionary.hpp>
#include <Godot/classes/standard_material3d.hpp>
#include <Godot/classes/base_material3d.hpp>
#include <Godot/variant/aabb.hpp>
#include <Godot/classes/physics_body3d.hpp>
#include <Godot/classes/scene_tree.hpp>

// C++ Runtime
#include <vector>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Struct of elements in the scene
struct DepthEntry
{
	Node3D* node = nullptr;
	Vector3 home_position;
	double true_z = 0.0;
	double flat_z = 0.0;
	double from_z = 0.0;
	double to_z = 0.0;
	double depth_units = 1.0;
	bool is_dynamic = false;
	bool is_lifted = false;
};

// Vector of the elements
static std::vector<DepthEntry> entries;

// Something glowing red because it is in the way, and how long it still glows
struct BlinkEntry
{
	Node* node = nullptr;
	double left = 0.0;
};
static std::vector<BlinkEntry> blinks;

// The view a layout is being checked for, which decides whether view blocks count as solid
static int checked_view = -1;

// Helpers, kept outside the script block because Jenova cannot read their types inside it
static CollisionShape3D* SolidShape(Node* node)
{
	// Purpose: find the box that makes an element solid, if it has one that is switched on.

	// Blocks, doors, crates and the player carry it as Collision, spikes keep it on their Solid child
	CollisionShape3D* shape = (CollisionShape3D*)node->get_node_or_null("Collision");
	if (!shape) shape = (CollisionShape3D*)node->get_node_or_null("Solid/Collision");
	if (!shape) return nullptr;

	// A view block is solid only in its own view; the check may be about a view not entered yet, so its switch is not trusted
	if (node->has_meta("solid_view")) return ((int)node->get_meta("solid_view") == checked_view) ? shape : nullptr;
	if (shape->is_disabled()) return nullptr;

	// Areas such as plates and exits have a trigger, not a solid box
	if (node->is_class("Area3D") && !node->get_node_or_null("Solid")) return nullptr;
	return shape;
}
static bool SolidBox(Node* node, float shrink, AABB& box)
{
	// Purpose: work out the box an element fills in the world right now, slightly shrunk if asked, so touching does not count.

	// Measure its solid box, if it has one switched on
	return ShapeWorldBox(SolidShape(node), shrink, box);
}
static void AddSolidRid(Node* node, TypedArray<RID>& rids)
{
	// Purpose: add the physics body of a tracked element to a list, so a physics question can leave it out.

	// The body is the element itself, or the Solid child of spikes
	if (node->is_class("PhysicsBody3D")) rids.push_back(((PhysicsBody3D*)node)->get_rid());
	Node* solid = node->get_node_or_null("Solid");
	if (solid && solid->is_class("PhysicsBody3D")) rids.push_back(((PhysicsBody3D*)solid)->get_rid());
}
static Node* PieceBlockerAt(int index, const std::vector<int>& moving)
{
	// Purpose: find what solid thing a fixed piece, such as a depth lift, is inside where it stands now, leaving out the pieces moving with it.

	// Measure the piece, slightly shrunk so touching does not count
	Node3D* piece = entries[index].node;
	AABB own;
	if (!SolidBox(piece, 0.9f, own)) return nullptr;

	// Compare it with every other tracked solid piece that is not moving along
	TypedArray<RID> tracked;
	for (int other = 0; other < (int)entries.size(); other++)
	{
		Node3D* node = entries[other].node;
		AddSolidRid(node, tracked);
		bool along = false;
		for (int member : moving) along = along || member == other;
		if (along || !node->is_visible_in_tree()) continue;
		AABB box;
		if (SolidBox(node, 1.0f, box) && own.intersects(box)) return node;
	}

	// Pieces solid in only one view that are not tracked, such as a fan's wall of air, never block a lift: it is judged as in iso
	TypedArray<Node> view_solids = piece->get_tree()->get_nodes_in_group("view_solid");
	for (int other = 0; other < view_solids.size(); other++)
	{
		Object* object = view_solids[other];
		if (object) AddSolidRid((Node*)object, tracked);
	}

	// Ask the physics engine about everything else, such as the board, which never moves
	CollisionShape3D* shape_node = SolidShape(piece);
	Ref<PhysicsShapeQueryParameters3D> query;
	query.instantiate();
	query->set_shape(shape_node->get_shape());
	Transform3D placement = shape_node->get_global_transform();
	placement.basis = placement.basis.scaled(Vector3(0.9f, 0.9f, 0.9f));
	query->set_transform(placement);
	query->set_collision_mask(LAYER_WORLD);
	query->set_exclude(tracked);
	Array hits = piece->get_world_3d()->get_direct_space_state()->intersect_shape(query, 1);
	if (hits.size() == 0) return nullptr;
	Dictionary hit = hits[0];
	Object* collider = hit["collider"];
	return (Node*)collider;
}
static void StartBlink(Node* node)
{
	// Purpose: make something glow red for half a second, to show what is in the way.

	// Restart the glow if this node is already glowing
	if (!node) return;
	for (BlinkEntry& blink : blinks)
	{
		if (blink.node == node)
		{
			blink.left = 0.5;
			return;
		}
	}

	// Otherwise add it to the list
	BlinkEntry blink;
	blink.node = node;
	blink.left = 0.5;
	blinks.push_back(blink);
}
static void SetLook(Node* node, bool overlay, const Color& color)
{
	// Purpose: draw a coloured layer over every mesh of a piece, or take it away.
	// The shadow disc of the player and crates, and the player's x-ray dots, are left alone.

	// Apply it to this node if it is a mesh
	if (node->is_class("GeometryInstance3D") && String(node->get_name()) != "Shadow" && String(node->get_name()) != "XRay")
	{
		// A coloured layer drawn over the mesh, created once and then only recoloured
		if (overlay)
		{
			Object* layer = node->get("material_overlay");
			if (!layer)
			{
				Ref<StandardMaterial3D> material;
				material.instantiate();
				material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
				material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
				node->set("material_overlay", material);
				layer = node->get("material_overlay");
			}
			layer->set("albedo_color", color);
		}
		else node->set("material_overlay", Variant());
	}

	// And to every child, so pieces made of several meshes change as a whole
	for (int index = 0; index < node->get_child_count(); index++) SetLook(node->get_child(index), overlay, color);
}
static void UpdateBlinks(double delta)
{
	// Purpose: advance every warning glow, pulsing red over whatever is in the way and clearing it at the end.

	// Go through the list from the end, so finished glows can be removed on the way
	for (int index = (int)blinks.size() - 1; index >= 0; index--)
	{
		BlinkEntry& blink = blinks[index];
		blink.left -= delta;

		// A finished glow leaves the piece as it was
		if (blink.left <= 0.0)
		{
			SetLook(blink.node, false, Color());
			blinks.erase(blinks.begin() + index);
			continue;
		}

		// Otherwise pulse a red layer over it about three times
		float pulse = (float)(0.5 + 0.5 * sin(blink.left * 18.0));
		SetLook(blink.node, true, Color(1.0f, 0.15f, 0.1f, 0.25f + 0.45f * pulse));
	}
}

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties
JENOVA_PROPERTY(double, flat_front_z, 4.5)
JENOVA_PROPERTY(double, transition_time, 0.35)

// State
Node* controller = nullptr;
bool is_flattened = false;
bool is_moving = false;
double progress = 1.0;
double default_front_z = 4.5;
double level_front_z = 4.5;
double move_time = 0.35;
int current_view = VIEW_SIDE;

// Measuring
double DepthExtent(int index)
{
	// Purpose: measure how deep an element is right now, taking its scale and any turn into account.

	// Add up how much each of its own axes reaches along the world's depth axis
	DepthEntry& entry = entries[index];
	Vector3 depth_row = entry.node->get_global_transform().basis.rows[2];
	double reach = (depth_row.x < 0 ? -depth_row.x : depth_row.x) + (depth_row.y < 0 ? -depth_row.y : depth_row.y) + (depth_row.z < 0 ? -depth_row.z : depth_row.z);
	return entry.depth_units * reach;
}
double FlatDepth(int index)
{
	// Purpose: the depth an element takes when the level is flat, with its front face lined up with the front edge.

	// Half its own depth behind the front edge
	return level_front_z - DepthExtent(index) * 0.5;
}
int MaskForView(int view)
{
	// Purpose: the collision layers a moving element feels in a view, the edges of the board only count in top view.

	// The world always, the board edges only in top view
	return (view == VIEW_TOP) ? (LAYER_WORLD | LAYER_BOARD_BOUNDS) : LAYER_WORLD;
}
Node* FindBlocker(int index, int view)
{
	// Purpose: find what solid thing a moving element is inside where it stands now, or nothing.
	// Tracked pieces are compared box against box, because the physics engine only sees where they were moved on the next frame.

	// Measure the element, slightly shrunk so touching does not count, in the view being checked
	checked_view = view;
	DepthEntry& entry = entries[index];
	CharacterBody3D* body = (CharacterBody3D*)entry.node;
	if (!body->is_visible_in_tree()) return nullptr;
	AABB own;
	if (!SolidBox(body, 0.9f, own)) return nullptr;

	// Compare it with every other tracked piece that is solid
	TypedArray<RID> tracked;
	for (int other = 0; other < (int)entries.size(); other++)
	{
		Node3D* node = entries[other].node;
		AddSolidRid(node, tracked);
		if (other == index || !node->is_visible_in_tree()) continue;
		AABB box;
		if (SolidBox(node, 1.0f, box) && own.intersects(box)) return node;
	}

	// Pieces solid in only one view that the depth module does not track, such as a fan's wall of air, are judged by box too
	// They are left out of the physics question below, which only knows whether they are solid in the view shown now
	TypedArray<Node> view_solids = controller->get_tree()->get_nodes_in_group("view_solid");
	for (int other = 0; other < view_solids.size(); other++)
	{
		Object* object = view_solids[other];
		Node3D* node = (Node3D*)object;
		if (!node) continue;
		AddSolidRid(node, tracked);
		AABB box;
		if (node->is_visible_in_tree() && SolidBox(node, 1.0f, box) && own.intersects(box)) return node->get_parent();
	}

	// Ask the physics engine about everything else, such as the board and its edges, which never move
	CollisionShape3D* shape_node = SolidShape(body);
	Ref<PhysicsShapeQueryParameters3D> query;
	query.instantiate();
	query->set_shape(shape_node->get_shape());
	Transform3D placement = shape_node->get_global_transform();
	placement.basis = placement.basis.scaled(Vector3(0.9f, 0.9f, 0.9f));
	query->set_transform(placement);
	query->set_collision_mask(MaskForView(view));
	query->set_exclude(tracked);
	Array hits = body->get_world_3d()->get_direct_space_state()->intersect_shape(query, 1);
	if (hits.size() == 0) return nullptr;
	Dictionary hit = hits[0];
	Object* collider = hit["collider"];
	return (Node*)collider;
}
Node* FindCover(Node3D* piece)
{
	// Purpose: find the fixed solid piece that hides something that must stay in sight, such as a lever, or nothing.
	// The flat views look straight along the depth axis, so only the overlap across the board counts.

	// The piece marks the part that must be seen with a switched off box called Footprint
	if (!piece || !piece->is_visible_in_tree()) return nullptr;
	AABB own;
	if (!ShapeWorldBox((CollisionShape3D*)piece->get_node_or_null("Footprint"), 0.9f, own)) return nullptr;

	// Compare it with every fixed solid piece, leaving out the one it is mounted on, the player and crates can simply move away
	for (DepthEntry& entry : entries)
	{
		Node3D* node = entry.node;
		if (entry.is_dynamic || node == piece || node->is_ancestor_of(piece) || !node->is_visible_in_tree()) continue;
		AABB box;
		if (!SolidBox(node, 1.0f, box)) continue;

		// Ignore the depth, whatever is in front or behind covers it the same in a flat view
		box.position.z = own.position.z;
		box.size.z = own.size.z;
		if (own.intersects(box)) return node;
	}
	return nullptr;
}
bool CheckLayout(int view)
{
	// Purpose: tell whether every moving element is free where it stands now, making whatever is in the way blink.

	// Look at each moving element that is still in the level, judging view blocks by this view
	checked_view = view;
	bool free = true;
	for (int index = 0; index < (int)entries.size(); index++)
	{
		if (!entries[index].is_dynamic) continue;

		// Something solid where it stands blocks the change, both the element and the obstacle blink
		Node* blocker = FindBlocker(index, view);
		if (!blocker) continue;

		// A crate already outside the board, such as one falling into the void, does not hold up a switch: it is lost anyway
		if (String(blocker->get_name()) == "BoardBounds" && entries[index].node->is_in_group("crate")) continue;

		// A body raised in top is in front of what it stands on, so in a flat view only a fan's column can still be in its way
		if (view != VIEW_ISO && (bool)entries[index].node->get_meta("raised", false) && !blocker->is_in_group("fan")) continue;
		StartBlink(blocker);
		StartBlink(entries[index].node);
		free = false;
	}

	// In a flat view, pieces that must stay in sight, such as levers, cannot end up hidden behind something fixed
	if (view == VIEW_ISO) return free;
	TypedArray<Node> pieces = controller->get_tree()->get_nodes_in_group("keep_clear");
	for (int index = 0; index < pieces.size(); index++)
	{
		Object* object = pieces[index];
		Node* cover = FindCover((Node3D*)object);
		if (!cover) continue;
		StartBlink(cover);
		StartBlink((Node*)object);
		free = false;
	}
	return free;
}

// Moving
void PlaceAll(double amount)
{
	// Purpose: put every element somewhere between the depth it is leaving and the one it is going to, 0 is start and 1 is end.

	// Ease the amount so the move starts and ends gently, the same way the camera does
	double eased = amount * amount * (3.0 - 2.0 * amount);

	// Slide each element along the depth axis only
	for (DepthEntry& entry : entries)
	{
		Vector3 position = entry.node->get_global_position();
		position.z = entry.from_z + (entry.to_z - entry.from_z) * eased;
		entry.node->set_global_position(position);
	}
}
void FinishMove()
{
	// Purpose: end a depth move, leaving every element exactly at its destination.

	// Place everything at the end of the move
	PlaceAll(1.0);
	is_moving = false;
	progress = 1.0;

}
bool StartMove(int instant)
{
	// Purpose: begin moving every element to its destination depth, and tell whether an animated move is now running.

	// An instant move is finished on the spot
	if (instant != 0 || move_time <= 0.0)
	{
		FinishMove();
		return false;
	}

	// Otherwise the physics routine carries the move out over the next frames
	progress = 0.0;
	is_moving = true;
	return true;
}

// Registering
void RegisterBranch(Node* parent)
{
	// Purpose: add to the list every element marked with the depth group under a node, looking inside carriers too.

	// Look at each direct child
	for (int index = 0; index < parent->get_child_count(); index++)
	{
		Node* child = parent->get_child(index);

		// Pieces placed on a carrier, such as a turntable, are elements of their own
		if (child->is_in_group("carrier")) RegisterBranch(child);
		if (!child->is_in_group("depth") || !child->is_class("Node3D")) continue;

		// Remember where the element starts and how deep it is before any scaling or turning
		DepthEntry entry;
		entry.node = (Node3D*)child;
		entry.home_position = entry.node->get_global_position();
		entry.true_z = entry.home_position.z;
		entry.depth_units = child->get_meta("depth", 1.0);

		// Elements that move by themselves are the ones that can end up inside something
		entry.is_dynamic = child->is_class("CharacterBody3D");
		entries.push_back(entry);
	}
}

// Rotations
bool IsRider(Node* carrier, int index, double axis)
{
	// Purpose: tell whether a moving element stands on a carrier, so a turn of the carrier takes it along.

	// Only moving elements that are not part of the carrier itself can ride it
	DepthEntry& entry = entries[index];
	if (!entry.is_dynamic || carrier->is_ancestor_of(entry.node)) return false;

	// Something on the board is only carried in top view, where the board is the floor
	if (axis > 0.5 && current_view != VIEW_TOP) return false;

	// The carrier's deck is the box above it where riders stand
	CollisionShape3D* deck = (CollisionShape3D*)carrier->get_node_or_null("Deck/Collision");
	if (!deck) return false;
	Ref<Shape3D> deck_shape = deck->get_shape();
	if (deck_shape.is_null()) return false;
	Vector3 half = ((Vector3)deck_shape->get("size")) * 0.5f;

	// The rider is judged by its real depth, the one it has in iso
	Vector3 point = entry.node->get_global_position();
	if (is_flattened && axis < 0.5) point.z = entry.true_z;

	// Inside the deck box, measured in the deck's own space
	Vector3 local = deck->get_global_transform().affine_inverse().xform(point);
	return (local.x >= -half.x && local.x <= half.x && local.y >= -half.y && local.y <= half.y && local.z >= -half.z && local.z <= half.z);
}

// Commands
void SetView(int view)
{
	// Purpose: let the GameManager tell this module which view is active.

	// Remember it, it decides which collision layers count and who rides the board
	current_view = view;
}
void RegisterLevel(Node* level)
{
	// Purpose: let the GameManager hand over a level, so every element marked with the depth group is tracked.

	// Start from an empty list in the unflattened state
	entries.clear();
	blinks.clear();
	is_flattened = false;
	is_moving = false;
	if (!level) return;

	// The level says where its front edge is, which is the plane every element is pushed to when flat
	level_front_z = level->get_meta("flat_front_z", default_front_z);

	// Walk the level and keep every element marked with the depth group
	RegisterBranch(level);
}
void PlaceRaisedBodies()
{
	// Purpose: give every body raised in top the depth right in front of the fixed piece it stands on, so in iso it is against that piece's front face.
	// A raised body over nothing fixed, such as over a fan, keeps the depth it had.

	// Look at each moving body that is up on something
	for (int index = 0; index < (int)entries.size(); index++)
	{
		DepthEntry& entry = entries[index];
		if (!entry.is_dynamic || !(bool)entry.node->get_meta("raised", false)) continue;
		AABB own;
		if (!SolidBox(entry.node, 0.9f, own)) continue;

		// The fixed pieces under it across the board, the one whose front face is nearest the camera counts
		bool found = false;
		double front = 0.0;
		checked_view = VIEW_TOP;
		for (int other = 0; other < (int)entries.size(); other++)
		{
			if (other == index || entries[other].is_dynamic) continue;
			AABB box;
			if (!SolidBox(entries[other].node, 1.0f, box)) continue;
			bool under = own.position.x < box.position.x + box.size.x && own.position.x + own.size.x > box.position.x && own.position.y < box.position.y + box.size.y && own.position.y + own.size.y > box.position.y;
			if (!under) continue;
			double piece_front = entries[other].true_z + DepthExtent(other) * 0.5;
			if (!found || piece_front > front) front = piece_front;
			found = true;
		}

		// Right in front of it, touching
		if (found) entry.true_z = front + DepthExtent(index) * 0.5 + 0.01;
	}
}
bool CanSwitchTo(int view)
{
	// Purpose: let the GameManager ask whether a view can be entered, which is refused when it would leave a moving element inside something.
	// Whatever is in the way blinks.

	// A body raised by a fan in top goes to iso pressed against the front of what it stands on, keeping its height
	if (view == VIEW_ISO && is_flattened) PlaceRaisedBodies();

	// Remember where everything stands now
	std::vector<Vector3> saved;
	for (DepthEntry& entry : entries) saved.push_back(entry.node->get_global_position());

	// Put everything where the new view would put it, flat at the front edge or back at its real depth
	bool to_flat = view != VIEW_ISO;
	if (to_flat != is_flattened)
	{
		for (int index = 0; index < (int)entries.size(); index++)
		{
			Vector3 position = saved[index];
			position.z = to_flat ? FlatDepth(index) : entries[index].true_z;
			entries[index].node->set_global_position(position);
		}
	}

	// Check that layout, then put everything back
	bool free = CheckLayout(view);
	for (int index = 0; index < (int)entries.size(); index++) entries[index].node->set_global_position(saved[index]);
	return free;
}
bool Flatten(int instant)
{
	// Purpose: let the GameManager push every element to the front edge of the level, remembering the depth each one had.
	// Returns whether an animated move started, so the caller knows to wait for the move_finished signal.

	// Do nothing when the level is already flat or a move is still running
	if (is_flattened || is_moving) return false;
	is_flattened = true;

	// Store each element's current depth, and aim it at the flat depth
	for (int index = 0; index < (int)entries.size(); index++)
	{
		DepthEntry& entry = entries[index];
		entry.true_z = entry.node->get_global_position().z;
		entry.flat_z = FlatDepth(index);
		entry.from_z = entry.true_z;
		entry.to_z = entry.flat_z;
	}

	// Carry the move out, at once or over time
	return StartMove(instant);
}
bool Restore(int instant)
{
	// Purpose: let the GameManager return every element to the depth it had before the level was flattened.
	// Returns whether an animated move started, so the caller knows to wait for the move_finished signal.

	// Do nothing when the level is not flat or a move is still running
	if (!is_flattened || is_moving) return false;
	is_flattened = false;

	// Aim each element at its remembered depth, it keeps wherever it moved to across the board
	for (DepthEntry& entry : entries)
	{
		entry.from_z = entry.node->get_global_position().z;
		entry.to_z = entry.true_z;
	}

	// Carry the move out, at once or over time
	return StartMove(instant);
}
bool PrepareRotation(Node* carrier)
{
	// Purpose: let the GameManager ask whether a carrier can turn, and get it ready if so.
	// The turn is tried on the spot and undone: if it would leave a moving element inside something it is refused and the obstacle blinks.
	// In a flat view the pieces and riders are lifted back to their real depth, because the turn happens in the real layout.

	// Ask the carrier how far it wants to turn and around which axis, vertical or depth
	Node3D* table = (Node3D*)carrier;
	double degrees = carrier->call("PendingDelta");
	double axis_value = carrier->call("TurnAxis");
	Vector3 axis = (axis_value < 0.5) ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(0.0f, 0.0f, 1.0f);
	float radians = (float)(degrees * 3.14159265358979 / 180.0);
	Vector3 pivot = table->get_global_position();
	bool lift = is_flattened && axis_value < 0.5;

	// Sort the elements into pieces of the carrier, riders standing on it, and the rest
	std::vector<int> moving;
	for (int index = 0; index < (int)entries.size(); index++)
	{
		if (table->is_ancestor_of(entries[index].node) || IsRider(carrier, index, axis_value)) moving.push_back(index);
	}

	// Remember the carrier's turn and where everything stands now
	Vector3 saved_rotation = table->get_rotation();
	std::vector<Vector3> saved;
	for (DepthEntry& entry : entries) saved.push_back(entry.node->get_global_position());

	// Turn the carrier, and place its pieces and riders where the turn takes them in the real layout
	table->rotate(axis, radians);
	for (int index : moving)
	{
		Vector3 start = saved[index];
		if (lift) start.z = entries[index].true_z;
		Vector3 end = pivot + (start - pivot).rotated(axis, radians);

		// In a flat view the turned element ends at the front edge again, at the depth its turned shape gives it
		if (lift) end.z = FlatDepth(index);
		entries[index].node->set_global_position(end);
	}

	// Check that layout, then undo the turn and put everything back
	bool free = CheckLayout(current_view);
	table->set_rotation(saved_rotation);
	for (int index = 0; index < (int)entries.size(); index++) entries[index].node->set_global_position(saved[index]);
	if (!free) return false;

	// The turn may go ahead, in a flat view lift the pieces and riders to their real depth first
	if (lift)
	{
		for (int index : moving)
		{
			Vector3 position = saved[index];
			position.z = entries[index].true_z;
			entries[index].node->set_global_position(position);
			entries[index].is_lifted = true;
		}
	}
	return true;
}
bool PrepareSwap(Node* swapper)
{
	// Purpose: let the GameManager ask whether the floors can swap, and get the pieces ready if so.
	// The swap is tried on the spot and undone: if it would leave a moving element inside something it is refused and the obstacle blinks.
	// In a flat view the pieces are lifted back to their real depth first, because the swap mirrors the real layout.

	// The hinge is the line where the ground meets the board
	Node3D* hinge = (Node3D*)swapper;
	float hinge_y = hinge->get_global_position().y;
	float hinge_z = hinge->get_global_position().z;

	// Remember how everything stands now
	std::vector<Transform3D> saved;
	for (DepthEntry& entry : entries) saved.push_back(entry.node->get_global_transform());

	// Mirror every tracked piece that the swap moves, from its real layout, and flatten it again in a flat view
	for (int index = 0; index < (int)entries.size(); index++)
	{
		if (IsSwapFixed(entries[index].node)) continue;
		Transform3D real = saved[index];
		if (is_flattened) real.origin.z = entries[index].true_z;
		entries[index].node->set_global_transform(SwapTransform(real, hinge_y, hinge_z, StaysUpright(entries[index].node)));
		if (!is_flattened) continue;
		Vector3 position = entries[index].node->get_global_position();
		position.z = FlatDepth(index);
		entries[index].node->set_global_position(position);
	}

	// Check that layout, then put everything back
	bool free = CheckLayout(current_view);
	for (int index = 0; index < (int)entries.size(); index++) entries[index].node->set_global_transform(saved[index]);
	if (!free) return false;

	// The swap may go ahead, in a flat view lift the pieces to their real depth first
	if (is_flattened)
	{
		for (int index = 0; index < (int)entries.size(); index++)
		{
			if (IsSwapFixed(entries[index].node)) continue;
			Vector3 position = saved[index].origin;
			position.z = entries[index].true_z;
			entries[index].node->set_global_position(position);
			entries[index].is_lifted = true;
		}
	}
	return true;
}
bool PrepareShift(Node* carrier)
{
	// Purpose: let the GameManager ask whether a depth lift can move to its next stop, and get it ready if so.
	// In iso the lift really travels, so its whole way is checked; in a flat view depth cannot be seen, so only the stop it arrives at matters.
	// Either way the layout is judged as it is in iso, the only view with real depth. A refused move makes the obstacle blink.

	// Find the lift among the tracked pieces, and how far it is going
	double shift = carrier->call("PendingShift");
	int lift_index = -1;
	for (int index = 0; index < (int)entries.size(); index++)
	{
		if (entries[index].node == carrier) lift_index = index;
	}
	if (lift_index < 0) return false;

	// Remember where everything stands, and where each piece really is in depth
	std::vector<Vector3> saved;
	std::vector<double> real_z;
	for (DepthEntry& entry : entries)
	{
		saved.push_back(entry.node->get_global_position());
		real_z.push_back(is_flattened ? entry.true_z : (double)entry.node->get_global_position().z);
	}

	// The lift and everything standing on it move together; who rides is judged with every piece at its real depth, the lift included
	for (int index = 0; index < (int)entries.size(); index++)
	{
		Vector3 position = saved[index];
		position.z = real_z[index];
		entries[index].node->set_global_position(position);
	}
	bool was_flattened = is_flattened;
	is_flattened = false;
	std::vector<int> moving;
	moving.push_back(lift_index);
	for (int index = 0; index < (int)entries.size(); index++)
	{
		if (index != lift_index && IsRider(carrier, index, 0.0)) moving.push_back(index);
	}
	is_flattened = was_flattened;

	// Try the move in the real layout: every quarter unit along the way in iso, only the arrival in a flat view
	int steps = 1;
	if (current_view == VIEW_ISO) steps = (int)ceil((shift < 0.0 ? -shift : shift) / 0.25);
	if (steps < 1) steps = 1;
	bool free = true;
	for (int step = 1; step <= steps && free; step++)
	{
		double offset = shift * (double)step / (double)steps;
		for (int index = 0; index < (int)entries.size(); index++)
		{
			Vector3 position = saved[index];
			position.z = real_z[index];
			entries[index].node->set_global_position(position);
		}
		for (int index : moving)
		{
			Vector3 position = entries[index].node->get_global_position();
			position.z += offset;
			entries[index].node->set_global_position(position);
		}

		// The lift itself and each rider have to be free there
		checked_view = VIEW_ISO;
		for (int index : moving)
		{
			Node* blocker = (index == lift_index) ? PieceBlockerAt(index, moving) : FindBlocker(index, VIEW_ISO);
			if (!blocker) continue;
			StartBlink(blocker);
			StartBlink(entries[index].node);
			free = false;
		}
	}

	// Put everything back as it was
	for (int index = 0; index < (int)entries.size(); index++) entries[index].node->set_global_position(saved[index]);
	if (!free) return false;

	// The move may go ahead: tell the lift what it carries, and in a flat view lift those pieces to their real depth first
	Array riders;
	for (int index : moving)
	{
		if (index != lift_index) riders.push_back(entries[index].node);
		if (!is_flattened) continue;
		Vector3 position = saved[index];
		position.z = real_z[index];
		entries[index].node->set_global_position(position);
		entries[index].is_lifted = true;
	}
	carrier->set_meta("riders", riders);
	return true;
}
bool FinishRotation(Node* carrier)
{
	// Purpose: let the GameManager settle a carrier after its turn, sliding lifted pieces and riders back to the front edge.
	// Returns whether an animated move started, so the caller knows to wait for the move_finished signal.

	// Everything not lifted stays where it is during the slide
	bool any = false;
	for (int index = 0; index < (int)entries.size(); index++)
	{
		DepthEntry& entry = entries[index];
		entry.from_z = entry.node->get_global_position().z;
		entry.to_z = entry.from_z;
		if (!entry.is_lifted) continue;

		// A lifted element remembers its new real depth and heads back to the front edge
		entry.true_z = entry.from_z;
		entry.flat_z = FlatDepth(index);
		entry.to_z = entry.flat_z;
		entry.is_lifted = false;
		any = true;
	}

	// Slide them back, or report that nothing has to move
	if (!any) return false;
	return StartMove(0);
}
void ResetAll()
{
	// Purpose: let the GameManager put every element back where the level placed it, used when the level restarts.

	// Return each element to its starting position and forget the depth it had since
	for (DepthEntry& entry : entries)
	{
		entry.node->set_global_position(entry.home_position);
		entry.true_z = entry.home_position.z;
		entry.is_lifted = false;
	}

	// The level is back in its real, unflattened shape, with no move running and nothing glowing
	for (BlinkEntry& blink : blinks) SetLook(blink.node, false, Color());
	blinks.clear();
	is_flattened = false;
	is_moving = false;
	progress = 1.0;
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node is in the tree, prepares the signal and the settings this module uses.

	// Keep this node and create the signal it emits at the end of a move
	controller = GetSelf<Node>(instance);
	if (!controller->has_signal("move_finished")) controller->add_user_signal("move_finished");

	// Copy the settings now, because properties may not be up to date inside calls made by other modules
	default_front_z = flat_front_z;
	move_time = transition_time;
}
void OnDestroy(Caller* instance)
{
	// Purpose: called when the node leaves the scene tree, drops every element reference.

	// Forget the nodes so nothing uses them after they are freed
	controller = nullptr;
	entries.clear();
	blinks.clear();
	is_flattened = false;
	is_moving = false;
}
void OnProcess(Caller* instance, double _delta)
{
	// Purpose: called every frame, runs the blinking of whatever is in the way.

	// Advance every blink
	if (!blinks.empty()) UpdateBlinks(_delta);
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, advances the depth move while one is in progress.

	// Do nothing when no move is running
	if (!is_moving) return;

	// Advance the move by the time passed
	progress += _delta / move_time;

	// Keep sliding the elements until the move is complete
	if (progress < 1.0)
	{
		PlaceAll(progress);
		return;
	}

	// Settle everything at its destination and tell the GameManager the move is over
	FinishMove();
	controller->emit_signal("move_finished");
}

// Jenova Script Block End
JENOVA_SCRIPT_END
