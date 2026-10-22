
/* Camundrum - Exit Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/area3d.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/geometry_instance3d.hpp>
#include <Godot/classes/mesh_instance3d.hpp>
#include <Godot/classes/standard_material3d.hpp>
#include <Godot/classes/cpu_particles3d.hpp>
#include <Godot/classes/scene_tree.hpp>
#include <Godot/variant/variant.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably for every exit
// goal_view: -1 counts in any view, 0 only in side, 1 only in top, 2 only in iso
JENOVA_PROPERTY(int, goal_view, -1)

// The state of each exit lives in its node's metadata: goal_view, view, reached, clock

// Look
bool CountsNow(Area3D* exit)
{
	// Purpose: tell whether the flag can be reached in the active view: always for a flag of any view, otherwise only in its own view.

	// Compare the flag's view with the active one
	int wanted = exit->get_meta("goal_view", -1);
	return wanted < 0 || wanted == (int)exit->get_meta("view", VIEW_SIDE);
}
void ShowState(Area3D* exit)
{
	// Purpose: draw the flag in its view's colour, solid when it can be reached and see-through when it cannot.

	// Every piece of the flag fades together
	float fade = CountsNow(exit) ? 0.0f : 0.75f;
	const char* parts[3] = { "Flag/Pole", "Flag/ClothPivot/Cloth", "Flag/Base" };
	for (const char* path : parts)
	{
		GeometryInstance3D* piece = (GeometryInstance3D*)exit->get_node_or_null(path);
		if (piece) piece->set_transparency(fade);
	}
}
void PaintCloth(Area3D* exit)
{
	// Purpose: colour the flag's cloth after the view it belongs to, a warm white for a flag of any view.

	// A material of its own for this flag
	MeshInstance3D* cloth = (MeshInstance3D*)exit->get_node_or_null("Flag/ClothPivot/Cloth");
	if (!cloth) return;
	float r, g, b;
	ViewColor(exit->get_meta("goal_view", -1), r, g, b);
	Ref<StandardMaterial3D> look;
	look.instantiate();
	look->set_albedo(Color(r, g, b, 1.0f));
	look->set_emission(Color(r, g, b, 1.0f) * 0.35f);
	look->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
	cloth->set_material_override(look);
}
bool PlayerAtFlag(Area3D* exit)
{
	// Purpose: tell whether the player is touching the flag, by comparing boxes, which also works when the player arrives standing still.

	// The flag's trigger and the player's body
	Node3D* player = (Node3D*)exit->get_tree()->get_first_node_in_group("player");
	if (!player || !player->is_visible_in_tree()) return false;
	AABB trigger;
	AABB body;
	if (!ShapeWorldBox((CollisionShape3D*)exit->get_node_or_null("Trigger"), 1.0f, trigger)) return false;
	if (!ShapeWorldBox((CollisionShape3D*)player->get_node_or_null("Collision"), 0.9f, body)) return false;
	return trigger.intersects(body);
}

void PlaceFlag(Area3D* exit, double delta)
{
	// Purpose: draw the flag with its own shape even when the exit is stretched to catch the player in depth, turned towards the camera in top so its pole is seen from above.

	// Undo the exit's stretch, then turn around the exit's middle
	Node3D* flag = (Node3D*)exit->get_node_or_null("Flag");
	Vector3 stretch = exit->get_global_transform().basis.get_scale();
	if (!flag || stretch.x < 0.01f || stretch.y < 0.01f || stretch.z < 0.01f) return;
	double turn = StepTopTurn(exit->get_meta("top_turn", -1.0), (int)exit->get_meta("view", VIEW_SIDE) == VIEW_TOP, delta);
	exit->set_meta("top_turn", turn);
	Basis unstretch = Basis::from_scale(Vector3(1.0f / stretch.x, 1.0f / stretch.y, 1.0f / stretch.z));
	flag->set_transform(Transform3D(unstretch * Basis(Vector3(1.0f, 0.0f, 0.0f), TopTurnAngle(turn)), Vector3()));
}

// Contract
void OnViewChanged(Caller* instance, int view)
{
	// Purpose: part of the element contract, remembers the active view, which decides whether a flag of one view can be reached.

	// Store it and redraw the flag
	auto exit = GetSelf<Area3D>(instance);
	exit->set_meta("view", view);
	ShowState(exit);
}
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, returns the exit to its starting state when the level restarts.

	// Allow the exit to be reached again, and draw the flag straight away for the view the level restarts in
	GetSelf<Area3D>(instance)->set_meta("reached", false);
	GetSelf<Area3D>(instance)->set_meta("top_turn", -1.0);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node is in the tree, prepares the signal, the state and the look of this flag.

	// Copy the inspector setting onto the node, and create the signal the GameManager listens to
	auto exit = GetSelf<Area3D>(instance);
	exit->set_meta("goal_view", goal_view < VIEW_COUNT ? goal_view : -1);
	if (!exit->has_signal("reached")) exit->add_user_signal("reached");

	// Start as not reached, in the view the game normally begins in
	exit->set_meta("reached", false);
	exit->set_meta("view", VIEW_SIDE);
	exit->set_meta("clock", 0.0);
	exit->set_collision_mask(LAYER_ACTORS);

	// The flag is drawn turned for the view it starts in
	exit->set_meta("top_turn", -1.0);
	PlaceFlag(exit, 0.0);
	PaintCloth(exit);
	ShowState(exit);
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, reports the moment the player touches the flag in a view where it counts.

	// Nothing to do once the exit has been reached, or in a view where this flag does not count
	auto exit = GetSelf<Area3D>(instance);
	if ((bool)exit->get_meta("reached", false) || !CountsNow(exit)) return;

	// Announce it once when the player is at the flag
	if (!PlayerAtFlag(exit)) return;
	exit->set_meta("reached", true);
	exit->emit_signal("reached");

	// Celebrate with a burst of sparks
	CPUParticles3D* burst = (CPUParticles3D*)exit->get_node_or_null("Burst");
	if (burst)
	{
		burst->restart();
		burst->set_emitting(true);
	}
}
void OnProcess(Caller* instance, double _delta)
{
	// Purpose: called every frame, makes the flag's cloth wave gently so it reads as the goal.

	// The flag turns to be seen from above in top
	auto exit = GetSelf<Area3D>(instance);
	PlaceFlag(exit, _delta);

	// Swing the cloth around the pole, it hangs from a pivot on the pole
	Node3D* cloth = (Node3D*)exit->get_node_or_null("Flag/ClothPivot");
	if (!cloth) return;
	double clock = (double)exit->get_meta("clock", 0.0) + _delta;
	if (clock > 1000.0) clock -= 1000.0;
	exit->set_meta("clock", clock);
	cloth->set_rotation(Vector3(0.0f, (float)(0.3 * sin(clock * 3.0)), (float)(0.05 * sin(clock * 5.0))));
}

// Jenova Script Block End
JENOVA_SCRIPT_END
