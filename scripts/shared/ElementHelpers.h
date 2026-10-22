
/* Camundrum - Shared Element Helpers */

#pragma once

// C++ Runtime
#include <cmath>

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/area3d.hpp>
#include <Godot/variant/typed_array.hpp>
#include <Godot/variant/string_name.hpp>
#include <Godot/variant/dictionary.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/world3d.hpp>
#include <Godot/classes/physics_direct_space_state3d.hpp>
#include <Godot/classes/physics_ray_query_parameters3d.hpp>
#include <Godot/classes/collision_object3d.hpp>
#include <Godot/classes/collision_shape3d.hpp>
#include <Godot/classes/shape3d.hpp>
#include <Godot/variant/aabb.hpp>
#include <Godot/classes/scene_tree.hpp>
#include <Godot/classes/light3d.hpp>
#include <Godot/classes/mesh_instance3d.hpp>
#include <Godot/classes/quad_mesh.hpp>
#include <Godot/classes/standard_material3d.hpp>
#include <Godot/variant/transform3d.hpp>
#include <Godot/variant/basis.hpp>

// Bodies
inline godot::Node* FirstBodyInGroup(godot::Area3D* area, const godot::StringName& group)
{
	// Purpose: find the first body touching an area that belongs to a group, such as "player" or "crate".

	// Look through everything the area is touching right now
	godot::TypedArray<godot::Node3D> bodies = area->get_overlapping_bodies();
	for (int index = 0; index < bodies.size(); index++)
	{
		// Keep the first one that is in the group
		godot::Object* object = bodies[index];
		godot::Node* node = (godot::Node*)object;
		if (node && node->is_in_group(group)) return node;
	}

	// Nothing from that group is touching the area
	return nullptr;
}
inline bool HasPlayerInside(godot::Area3D* area)
{
	// Purpose: tell whether the player is touching an area.

	// The player is the only node in the "player" group
	return FirstBodyInGroup(area, "player") != nullptr;
}

// Sound
inline void PlaySound(godot::Node* any, int sound)
{
	// Purpose: ask the AudioController, the node in the "audio" group, to play one of the game's sounds.

	// Without a tree or an audio module the game simply stays silent
	if (!any || !any->is_inside_tree()) return;
	godot::Node* audio = any->get_tree()->get_first_node_in_group("audio");
	if (audio) audio->call("Play", sound);
}

// Measuring
inline bool ShapeWorldBox(godot::CollisionShape3D* shape, float shrink, godot::AABB& box)
{
	// Purpose: work out the box a box shape fills in the world right now, slightly shrunk if asked, taking turns and scaling into account.

	// Only box shapes can be measured
	if (!shape) return false;
	godot::Ref<godot::Shape3D> resource = shape->get_shape();
	if (resource.is_null()) return false;
	godot::Variant size_value = resource->get("size");
	if (size_value.get_type() != godot::Variant::VECTOR3) return false;

	// Turn the half size into how far the box reaches along each world axis
	godot::Transform3D placement = shape->get_global_transform();
	godot::Vector3 half = ((godot::Vector3)size_value) * 0.5f * shrink;
	godot::Vector3 reach;
	for (int axis = 0; axis < 3; axis++)
	{
		godot::Vector3 row = placement.basis.rows[axis];
		reach[axis] = (row.x < 0 ? -row.x : row.x) * half.x + (row.y < 0 ? -row.y : row.y) * half.y + (row.z < 0 ? -row.z : row.z) * half.z;
	}
	box = godot::AABB(placement.origin - reach, reach * 2.0f);
	return true;
}

// Void
inline godot::Node3D* VoidAt(godot::Node* any, float x, float y)
{
	// Purpose: find the open hole in the board whose cell holds a point across the board, or nothing.
	// Each hole is one cell, one unit square, centred where the hole is; a filled hole is floor again.

	// Look through every hole in the level
	if (!any || !any->is_inside_tree()) return nullptr;
	godot::TypedArray<godot::Node> holes = any->get_tree()->get_nodes_in_group("board_hole");
	for (int index = 0; index < holes.size(); index++)
	{
		godot::Object* object = holes[index];
		godot::Node3D* hole = (godot::Node3D*)object;
		if (!hole || !hole->is_visible_in_tree() || (bool)hole->get_meta("filled", false)) continue;
		godot::Vector3 center = hole->get_global_position();
		float dx = x - center.x;
		float dy = y - center.y;
		// A point right on the edge between two cells belongs to one of them, never to neither
		if (dx >= -0.5f && dx < 0.5f && dy >= -0.5f && dy < 0.5f) return hole;
	}
	return nullptr;
}
inline bool FootprintOverVoid(godot::Node3D* body)
{
	// Purpose: tell whether the whole base of a body lies over open holes, which is when it has nothing to rest on in top view.
	// The base is sampled every quarter unit, a little inside its edges, so a body only just touching the board still rests on it.

	// The body's box across the board
	godot::AABB box;
	if (!ShapeWorldBox((godot::CollisionShape3D*)body->get_node_or_null("Collision"), 1.0f, box)) return false;
	float inset = 0.06f;
	float left = box.position.x + inset;
	float right = box.position.x + box.size.x - inset;
	float bottom = box.position.y + inset;
	float top = box.position.y + box.size.y - inset;

	// Every sampled point has to be over a hole
	int columns = (int)((right - left) / 0.25f) + 1;
	int rows = (int)((top - bottom) / 0.25f) + 1;
	for (int column = 0; column <= columns; column++)
	{
		float x = left + (right - left) * (float)column / (float)columns;
		for (int row = 0; row <= rows; row++)
		{
			float y = bottom + (top - bottom) * (float)row / (float)rows;
			if (!VoidAt(body, x, y)) return false;
		}
	}
	return true;
}
inline godot::Node3D* FillVoidUnder(godot::Node3D* body)
{
	// Purpose: plug the one open hole nearest to a body, used when a crate sinks into the void and becomes floor.
	// A crate fills a single cell even when it sinks across two of them. Returns the hole it plugged, or nothing.

	// The middle of the body across the board
	godot::AABB box;
	if (!ShapeWorldBox((godot::CollisionShape3D*)body->get_node_or_null("Collision"), 1.0f, box)) return nullptr;
	godot::Vector3 middle = box.get_center();

	// The open hole closest to it is filled
	godot::Node3D* nearest = nullptr;
	float best = 0.0f;
	godot::TypedArray<godot::Node> holes = body->get_tree()->get_nodes_in_group("board_hole");
	for (int index = 0; index < holes.size(); index++)
	{
		godot::Object* object = holes[index];
		godot::Node3D* hole = (godot::Node3D*)object;
		if (!hole || !hole->is_visible_in_tree() || (bool)hole->get_meta("filled", false)) continue;
		godot::Vector3 center = hole->get_global_position();
		float distance = (center.x - middle.x) * (center.x - middle.x) + (center.y - middle.y) * (center.y - middle.y);
		if (nearest && distance >= best) continue;
		nearest = hole;
		best = distance;
	}
	if (nearest) nearest->call("Fill");
	return nearest;
}

// Raised in top
inline int RaisedGroundUnder(godot::Node3D* body)
{
	// Purpose: tell what a body raised by a fan in top view is over: 0 nothing, 1 a fixed solid piece to stand on, 2 a fan.
	// Seen from above, being over a wall means standing on top of it; over free board it comes down again.

	// The body's box across the board, a little shrunk so only really standing over something counts
	godot::AABB own;
	if (!ShapeWorldBox((godot::CollisionShape3D*)body->get_node_or_null("Collision"), 0.9f, own)) return false;
	own.position.z = -1000.0f;
	own.size.z = 2000.0f;

	// Fixed pieces of the level that are solid in this view: blocks, doors, lifts, spikes, view blocks in their view
	godot::TypedArray<godot::Node> pieces = body->get_tree()->get_nodes_in_group("depth");
	for (int index = 0; index < pieces.size(); index++)
	{
		godot::Object* object = pieces[index];
		godot::Node* piece = (godot::Node*)object;
		if (!piece || piece == body || piece->is_class("CharacterBody3D")) continue;
		const char* path = piece->is_class("StaticBody3D") ? "Collision" : "Solid/Collision";
		godot::CollisionShape3D* shape = (godot::CollisionShape3D*)piece->get_node_or_null(path);
		if (!shape || shape->is_disabled() || !shape->is_visible_in_tree()) continue;
		godot::AABB box;
		if (ShapeWorldBox(shape, 1.0f, box) && own.intersects(box)) return 1;
	}

	// The fans themselves, which lift whatever stands on them
	godot::TypedArray<godot::Node> fans = body->get_tree()->get_nodes_in_group("fan");
	for (int index = 0; index < fans.size(); index++)
	{
		godot::Object* object = fans[index];
		godot::Node3D* fan = (godot::Node3D*)object;
		if (!fan || !(bool)fan->get_meta("on", true)) continue;
		godot::Vector3 center = fan->get_global_position();
		godot::Vector3 scale = fan->get_global_transform().basis.get_scale();
		if (fabsf(own.get_center().x - center.x) < 0.5f * scale.x + own.size.x * 0.5f && fabsf(own.get_center().y - center.y) < 0.5f * scale.y + own.size.y * 0.5f) return 2;
	}
	return 0;
}
inline bool OverRaisedGround(godot::Node3D* body)
{
	// Purpose: tell whether a body raised by a fan in top view is over anything to stand on up there, a fixed solid piece or a fan.

	// Anything at all counts
	return RaisedGroundUnder(body) != 0;
}
inline void ShowRaiseShadow(godot::Node3D* body, float height, float width, float tall)
{
	// Purpose: draw the shadow of a body raised in top view, a dark square on what is below it, slipping off to one side the higher it is.
	// height goes from 0, on the ground, to 1, fully up; the shadow is made the first time it is needed.

	// Find or make the shadow, a flat dark square facing the camera
	godot::MeshInstance3D* shadow = (godot::MeshInstance3D*)body->get_node_or_null("RaiseShadow");
	if (!shadow)
	{
		if (height < 0.01f) return;
		godot::Ref<godot::QuadMesh> square;
		square.instantiate();
		square->set_size(godot::Vector2(1.0f, 1.0f));
		godot::Ref<godot::StandardMaterial3D> dark;
		dark.instantiate();
		dark->set_shading_mode(godot::BaseMaterial3D::SHADING_MODE_UNSHADED);
		dark->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_ALPHA);
		dark->set_albedo(godot::Color(0.0f, 0.0f, 0.0f, 0.35f));
		square->set_material(dark);
		shadow = memnew(godot::MeshInstance3D);
		shadow->set_name("RaiseShadow");
		shadow->set_mesh(square);
		shadow->set_cast_shadows_setting(godot::GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		body->add_child(shadow);
	}

	// Lying just in front of the flattened pieces, offset down and to the right by the height
	shadow->set_visible(height > 0.01f);
	shadow->set_scale(godot::Vector3(width, tall, 1.0f));
	shadow->set_position(godot::Vector3(0.2f * height, -0.2f * height, 0.52f));
}


// Floor swap
inline godot::Transform3D SwapTransform(const godot::Transform3D& placement, float hinge_y, float hinge_z, bool upright)
{
	// Purpose: mirror a placement across the slanted plane through the hinge, where the ground meets the board.
	// Height above the ground and distance in front of the board trade places, so what stood on the ground now sticks out of the board and the other way round.
	// Bodies that walk or fall, such as the player and crates, keep standing upright and only move.

	// The position trades its height above the hinge for its depth in front of it
	godot::Vector3 origin = placement.origin;
	godot::Vector3 moved = godot::Vector3(origin.x, hinge_y + (origin.z - hinge_z), hinge_z + (origin.y - hinge_y));
	if (upright) return godot::Transform3D(placement.basis, moved);

	// The world mirror swaps the vertical and depth axes, a second mirror across the piece's own width keeps it a real turn instead of a flip
	// Pieces are symmetric across their width, so that second mirror cannot be seen
	godot::Basis mirror;
	mirror.set_column(0, godot::Vector3(1.0f, 0.0f, 0.0f));
	mirror.set_column(1, godot::Vector3(0.0f, 0.0f, 1.0f));
	mirror.set_column(2, godot::Vector3(0.0f, 1.0f, 0.0f));
	godot::Basis width_flip;
	width_flip.set_column(0, godot::Vector3(-1.0f, 0.0f, 0.0f));
	return godot::Transform3D(mirror * placement.basis * width_flip, moved);
}
inline bool StaysUpright(godot::Node* piece)
{
	// Purpose: tell whether a piece is a body that walks or falls, which keeps standing upright when the floors swap.

	// The player and crates are the character bodies of the level
	return piece->is_class("CharacterBody3D");
}
inline bool IsSwapFixed(godot::Node* node)
{
	// Purpose: tell whether a piece stays where it is when the floors swap: the board, the ground, the board edges, lights and pieces marked swap_fixed.

	// Marked by hand, or one of the parts every level is built on
	if (node->is_in_group("swap_fixed") || node->is_class("Light3D")) return true;
	godot::String name = node->get_name();
	return name == "Board" || name == "Floor" || name == "BoardBounds";
}
inline void CollectSwapPieces(godot::Node* parent, godot::Node* swapper, godot::TypedArray<godot::Node>& pieces)
{
	// Purpose: list every piece the floor swap moves, looking inside carriers such as turntables, whose pieces move one by one.

	// Look at each child of the level
	for (int index = 0; index < parent->get_child_count(); index++)
	{
		godot::Node* child = parent->get_child(index);
		if (child == swapper || !child->is_class("Node3D") || IsSwapFixed(child)) continue;
		if (child->is_in_group("carrier")) CollectSwapPieces(child, swapper, pieces);
		else pieces.push_back(child);
	}
}

// Visuals
inline void UpdateDropShadow(godot::CollisionObject3D* body, bool shown)
{
	// Purpose: place the dark disc called Shadow on the ground right below a body, so its height and landing spot can be read in iso.

	// Without a shadow piece there is nothing to do, and it is hidden when not wanted
	godot::Node3D* shadow = (godot::Node3D*)body->get_node_or_null("Shadow");
	if (!shadow) return;
	if (!shown || !body->is_visible_in_tree())
	{
		shadow->set_visible(false);
		return;
	}

	// Look straight down for the first solid surface below the body
	godot::Vector3 from = body->get_global_position();
	godot::Vector3 to = from + godot::Vector3(0.0f, -20.0f, 0.0f);
	godot::TypedArray<godot::RID> ignored;
	ignored.push_back(body->get_rid());
	godot::Ref<godot::PhysicsRayQueryParameters3D> query = godot::PhysicsRayQueryParameters3D::create(from, to, 1, ignored);
	godot::Dictionary hit = body->get_world_3d()->get_direct_space_state()->intersect_ray(query);
	if (hit.is_empty())
	{
		shadow->set_visible(false);
		return;
	}

	// Lay the disc just above the surface, smaller the higher the body is
	godot::Vector3 ground = hit["position"];
	float height = from.y - ground.y;
	float size = 1.0f - height / 8.0f;
	if (size < 0.35f) size = 0.35f;
	if (size > 1.0f) size = 1.0f;
	shadow->set_visible(true);
	shadow->set_global_position(ground + godot::Vector3(0.0f, 0.02f, 0.0f));
	shadow->set_global_rotation(godot::Vector3(0.0f, 0.0f, 0.0f));
	shadow->set_scale(godot::Vector3(size, 1.0f, size));
}

// Seen from above
// In top the camera looks at the board as if it were the floor, so pieces that stand on the ground are drawn turned towards the camera:
// their up points at the viewer, and the robot's antenna, the flag's pole or the top of the player's head show in the middle. Only the look turns, never the body.
inline double StepTopTurn(double turn, bool top, double delta)
{
	// Purpose: move how far a piece is turned to be seen from above, 0 as in side and iso and 1 in top, over the length of a camera move.
	// A negative turn means "just restarted": it jumps straight to where the view wants it.

	// Jump or walk towards the target
	double wanted = top ? 1.0 : 0.0;
	if (turn < 0.0) return wanted;
	double step = delta / 0.35;
	if (turn < wanted) return (turn + step > wanted) ? wanted : turn + step;
	return (turn - step < wanted) ? wanted : turn - step;
}
inline float TopTurnAngle(double turn)
{
	// Purpose: the angle a piece is turned by for a turn amount, eased so it starts and ends gently like the camera.

	// A quarter turn at most
	double eased = turn * turn * (3.0 - 2.0 * turn);
	return (float)(eased * 1.5707963267948966);
}
inline godot::Transform3D TurnedForTop(const godot::Transform3D& local, const godot::Vector3& pivot, float angle)
{
	// Purpose: turn a piece's own placement around a pivot so its up points towards the camera by the given angle.
	// Pieces on the ground turn around the middle of the cell above it, so seen from above they fill that cell.

	// Move to the pivot, turn around the left-right axis, move back
	godot::Transform3D to_pivot(godot::Basis(), pivot);
	godot::Transform3D turn(godot::Basis(godot::Vector3(1.0f, 0.0f, 0.0f), angle), godot::Vector3());
	godot::Transform3D back(godot::Basis(), -pivot);
	return to_pivot * turn * back * local;
}
