
/* Camundrum - View Block Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/static_body3d.hpp>
#include <Godot/classes/collision_shape3d.hpp>
#include <Godot/classes/mesh_instance3d.hpp>
#include <Godot/classes/geometry_instance3d.hpp>
#include <Godot/classes/standard_material3d.hpp>
#include <Godot/classes/base_material3d.hpp>
#include <Godot/variant/variant.hpp>

// Game
#include "scripts/shared/GameTypes.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every block
JENOVA_PROPERTY(int, solid_view, 0)

// The state of each block lives in its node's metadata: solid_view (0 side, 1 top, 2 iso), whose presence also tells the depth module it is a view block

// Look and feel
void ApplyView(StaticBody3D* block, int view)
{
	// Purpose: make the block solid and opaque in its own view, and see-through and passable in the other two, always in its view's colour.

	// Solid only in its own view
	bool solid = view == (int)block->get_meta("solid_view", VIEW_SIDE);
	CollisionShape3D* shape = (CollisionShape3D*)block->get_node_or_null("Collision");
	if (shape) shape->set_disabled(!solid);

	// A fresh material for this block, made once, in the colour of its view
	MeshInstance3D* mesh = (MeshInstance3D*)block->get_node_or_null("Mesh");
	if (!mesh) return;
	Ref<StandardMaterial3D> look = mesh->get_material_override();
	if (look.is_null())
	{
		look.instantiate();
		mesh->set_material_override(look);
	}
	float r, g, b;
	ViewColor((int)block->get_meta("solid_view", VIEW_SIDE), r, g, b);

	// Opaque when solid, faint and casting no shadow when not
	look->set_transparency(solid ? BaseMaterial3D::TRANSPARENCY_DISABLED : BaseMaterial3D::TRANSPARENCY_ALPHA);
	look->set_albedo(Color(r, g, b, solid ? 1.0f : 0.22f));
	mesh->set_cast_shadows_setting(solid ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
}

// Contract
void OnViewChanged(Caller* instance, int view)
{
	// Purpose: part of the element contract, turns the block solid or see-through for the view that starts.

	// Apply the view
	ApplyView(GetSelf<StaticBody3D>(instance), view);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node and its children are in the tree, prepares the block.

	// Copy the inspector setting onto the node, where every function and the depth module can read it
	auto block = GetSelf<StaticBody3D>(instance);
	int view = solid_view;
	if (view < VIEW_SIDE || view >= VIEW_COUNT) view = VIEW_SIDE;
	block->set_meta("solid_view", view);

	// Start in the view the game normally begins in, the GameManager sets the real one right after
	ApplyView(block, VIEW_SIDE);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
