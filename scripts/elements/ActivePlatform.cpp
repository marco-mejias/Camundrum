
/* Camundrum - Activatable Platform Element
 * A platform that waits as a see-through ghost until something activates it, then turns solid.
 */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/static_body3d.hpp>
#include <Godot/classes/mesh_instance3d.hpp>
#include <Godot/classes/mesh.hpp>
#include <Godot/classes/standard_material3d.hpp>
#include <Godot/variant/variant.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every platform
JENOVA_PROPERTY(bool, starts_active, false)
JENOVA_PROPERTY(bool, stays_active, false)

// A platform that waits in its place as a see-through ghost, letting everything pass, until a plate, lever or button activates it:
// then it turns solid and opaque with a small pop. Unlike a door it does not sink, so it suits pieces floating in the air.
// The state of each platform lives in its node's metadata: starts_active, stays_active, active, shown (0 ghost to 1 solid), pop

// ---- Look ----

// Draw the platform between a faint ghost (0) and fully solid (1), with a short pop when it becomes solid
void DrawShown(StaticBody3D* platform, double shown)
{
	// Remember it and fade the platform's own material between the two
	platform->set_meta("shown", shown);
	MeshInstance3D* mesh = Object::cast_to<MeshInstance3D>(platform->get_node_or_null("Mesh"));
	if (!mesh) return;
	Ref<StandardMaterial3D> material = mesh->get_material_override();
	if (material.is_valid())
	{
		Color color = material->get_albedo();
		color.a = (float)(0.22 + 0.78 * shown);
		material->set_albedo(color);
		material->set_transparency(shown >= 0.999 ? BaseMaterial3D::TRANSPARENCY_DISABLED : BaseMaterial3D::TRANSPARENCY_ALPHA);
	}

	// The pop: a little bigger for a moment, settling back
	double pop = platform->get_meta("pop", 0.0);
	float grow = (float)(1.0 + 0.08 * sin(pop * 3.14159265358979));
	mesh->set_scale(Vector3(grow, grow, grow));
}

// Make the platform solid or a ghost, remember which, and start the change of look unless asked to change at once
void ApplyActive(StaticBody3D* platform, bool active, bool instant)
{
	// 1. A real change, not a reset, is heard and pops
	bool changed = (bool)platform->get_meta("active", false) != active;
	platform->set_meta("active", active);
	if (changed && !instant)
	{
		PlaySound(platform, SOUND_DOOR);
		if (active) platform->set_meta("pop", 1.0);
	}

	// 2. A ghost blocks nothing, the change waits for the end of the frame so the physics step is not disturbed
	Node* shape = platform->get_node_or_null("Collision");
	if (shape) shape->set_deferred("disabled", !active);

	// 3. The look follows over the next frames, or right now
	if (instant)
	{
		platform->set_meta("pop", 0.0);
		DrawShown(platform, active ? 1.0 : 0.0);
	}
}

// ---- Contract ----

// Let an activator such as a plate make the platform solid
void Activate(Caller* instance)
{
	// Solid
	ApplyActive(GetSelf<StaticBody3D>(instance), true, false);
}

// Let an activator such as a plate turn the platform back into a ghost
void Deactivate(Caller* instance)
{
	// A platform set to stay active ignores this
	auto platform = GetSelf<StaticBody3D>(instance);
	if ((bool)platform->get_meta("stays_active", false)) return;
	ApplyActive(platform, false, false);
}

// Let a lever or button flip the platform between solid and ghost
void Toggle(Caller* instance)
{
	// Flip, a platform that stays active cannot go back
	auto platform = GetSelf<StaticBody3D>(instance);
	bool active = platform->get_meta("active", false);
	if (active && (bool)platform->get_meta("stays_active", false)) return;
	ApplyActive(platform, !active, false);
}

// Part of the element contract, returns the platform to how it starts when the level restarts
void Reset(Caller* instance)
{
	// As set in the inspector, at once
	auto platform = GetSelf<StaticBody3D>(instance);
	ApplyActive(platform, platform->get_meta("starts_active", false), true);
}

// ---- Routines ----

// Called once the node and its children are in the tree, gives the platform its own material and its starting state
void OnReady(Caller* instance)
{
	// 1. Copy the inspector settings onto the node, where every function can read them safely
	auto platform = GetSelf<StaticBody3D>(instance);
	platform->set_meta("starts_active", starts_active);
	platform->set_meta("stays_active", stays_active);

	// 2. Each platform fades on its own, so it gets its own copy of the material
	MeshInstance3D* mesh = Object::cast_to<MeshInstance3D>(platform->get_node_or_null("Mesh"));
	if (mesh && mesh->get_mesh().is_valid())
	{
		Ref<Material> shared = mesh->get_mesh()->surface_get_material(0);
		if (shared.is_valid()) mesh->set_material_override(shared->duplicate());
	}

	// 3. Start as set, with no sound or pop
	platform->set_meta("active", !starts_active);
	ApplyActive(platform, starts_active, true);
}

// Called every frame, fades the platform towards solid or ghost and plays out its pop
void OnProcess(Caller* instance, double _delta)
{
	// Move the drawn amount towards the state, and let the pop die down
	auto platform = GetSelf<StaticBody3D>(instance);
	double shown = platform->get_meta("shown", 0.0);
	double wanted = (bool)platform->get_meta("active", false) ? 1.0 : 0.0;
	double pop = platform->get_meta("pop", 0.0);
	if (shown == wanted && pop <= 0.0) return;
	double step = _delta / 0.25;
	if (shown < wanted) shown = (shown + step > wanted) ? wanted : shown + step;
	else shown = (shown - step < wanted) ? wanted : shown - step;
	pop -= _delta / 0.3;
	platform->set_meta("pop", pop > 0.0 ? pop : 0.0);
	DrawShown(platform, shown);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
