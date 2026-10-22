
/* Camundrum - Floor Swap Element */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/array.hpp>
#include <Godot/variant/typed_array.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties, copied onto the node in OnReady, the only place where Jenova reads them reliably
JENOVA_PROPERTY(double, swap_time, 0.7)

// The state of each swap lives in its node's metadata: swap_time, swapped, target, turning, requested, progress, pieces, from

// Swapping
void SwapAtOnce(Node3D* hinge)
{
	// Purpose: mirror every piece across the hinge in one go, used to undo a swap when the level restarts.

	// Mirror each piece where it stands now
	float hinge_y = hinge->get_global_position().y;
	float hinge_z = hinge->get_global_position().z;
	TypedArray<Node> pieces;
	CollectSwapPieces(hinge->get_parent(), hinge, pieces);
	for (int index = 0; index < pieces.size(); index++)
	{
		Object* object = pieces[index];
		Node3D* piece = (Node3D*)object;
		piece->set_global_transform(SwapTransform(piece->get_global_transform(), hinge_y, hinge_z, StaysUpright(piece)));
	}
}
void PlaceAlongFold(Node3D* hinge, double eased)
{
	// Purpose: put every piece somewhere along its way, 0 is where it started and 1 where the swap takes it.
	// Each piece travels on a quarter circle around the hinge, so the whole level seems to fold over.

	// The pieces and where each one started
	float hinge_y = hinge->get_global_position().y;
	float hinge_z = hinge->get_global_position().z;
	Array pieces = hinge->get_meta("pieces", Array());
	Array from = hinge->get_meta("from", Array());
	for (int index = 0; index < pieces.size() && index < from.size(); index++)
	{
		Object* object = pieces[index];
		Node3D* piece = (Node3D*)object;
		if (!piece) continue;
		Transform3D start = from[index];
		Transform3D end = SwapTransform(start, hinge_y, hinge_z, StaysUpright(piece));

		// The angle around the hinge, measured from the ground towards the board, ends mirrored around the slanted plane
		float height = start.origin.y - hinge_y;
		float depth = start.origin.z - hinge_z;
		float radius = sqrtf(height * height + depth * depth);
		float angle_from = atan2f(height, depth);
		float angle_to = 1.5707963f - angle_from;
		float angle = angle_from + (angle_to - angle_from) * (float)eased;
		Vector3 origin = Vector3(start.origin.x, hinge_y + radius * sinf(angle), hinge_z + radius * cosf(angle));

		// The piece turns over on the way while its proportions blend, turning and stretching apart so it never flattens to nothing
		Quaternion turn = start.basis.get_rotation_quaternion().slerp(end.basis.get_rotation_quaternion(), (float)eased);
		Vector3 size = start.basis.get_scale().lerp(end.basis.get_scale(), (float)eased);
		piece->set_global_transform(Transform3D(Basis(turn).scaled_local(size), origin));
	}
}
void Request(Node3D* hinge)
{
	// Purpose: ask the GameManager for permission to swap, once this frame's work is done.

	// Only ask when the wanted state differs and no question or swap is already running
	if ((bool)hinge->get_meta("target", false) == (bool)hinge->get_meta("swapped", false)) return;
	if ((bool)hinge->get_meta("turning", false) || (bool)hinge->get_meta("requested", false)) return;
	hinge->set_meta("requested", true);
	hinge->call_deferred("emit_signal", "rotation_requested", hinge);
}

// Commands
void Toggle(Caller* instance)
{
	// Purpose: let a lever or a button swap the floors, or swap them back.

	// Aim at the other state and ask
	auto hinge = GetSelf<Node3D>(instance);
	hinge->set_meta("target", !(bool)hinge->get_meta("swapped", false));
	Request(hinge);
}
void Activate(Caller* instance)
{
	// Purpose: let a plate ask for the floors swapped.

	// Aim at the swapped state and ask
	auto hinge = GetSelf<Node3D>(instance);
	hinge->set_meta("target", true);
	Request(hinge);
}
void Deactivate(Caller* instance)
{
	// Purpose: let a plate ask for the floors back as they were.

	// Aim at the starting state and ask
	auto hinge = GetSelf<Node3D>(instance);
	hinge->set_meta("target", false);
	Request(hinge);
}
void CancelTurn(Caller* instance)
{
	// Purpose: let the GameManager refuse the swap because something would end up inside something else.

	// Stay as it is
	auto hinge = GetSelf<Node3D>(instance);
	hinge->set_meta("target", hinge->get_meta("swapped", false));
	hinge->set_meta("requested", false);
}
void StartTurn(Caller* instance)
{
	// Purpose: let the GameManager start the swap once it has been allowed.

	// Note every piece and where it starts, the physics routine folds them over
	auto hinge = GetSelf<Node3D>(instance);
	TypedArray<Node> pieces;
	CollectSwapPieces(hinge->get_parent(), hinge, pieces);
	Array list;
	Array from;
	for (int index = 0; index < pieces.size(); index++)
	{
		Object* object = pieces[index];
		Node3D* piece = (Node3D*)object;
		list.push_back(piece);
		from.push_back(piece->get_global_transform());
	}
	hinge->set_meta("pieces", list);
	hinge->set_meta("from", from);
	hinge->set_meta("requested", false);
	hinge->set_meta("turning", true);
	hinge->set_meta("progress", 0.0);
}

// Contract
void Reset(Caller* instance)
{
	// Purpose: part of the element contract, puts the floors back as they started when the level restarts.

	// A swap cut short is finished first, so every piece is exactly mirrored
	auto hinge = GetSelf<Node3D>(instance);
	if ((bool)hinge->get_meta("turning", false))
	{
		PlaceAlongFold(hinge, 1.0);
		hinge->set_meta("swapped", hinge->get_meta("target", false));
	}

	// Mirroring twice gives back the start, the depth module then returns every moving piece to its starting spot
	if ((bool)hinge->get_meta("swapped", false)) SwapAtOnce(hinge);
	hinge->set_meta("swapped", false);
	hinge->set_meta("target", false);
	hinge->set_meta("turning", false);
	hinge->set_meta("requested", false);
	hinge->set_meta("pieces", Array());
	hinge->set_meta("from", Array());
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node is in the tree, prepares the swap and its signals.

	// Copy the inspector setting onto the node, where every function can read it safely
	auto hinge = GetSelf<Node3D>(instance);
	hinge->set_meta("swap_time", swap_time);

	// The same signals as a turntable, so the GameManager handles both the same way
	if (!hinge->has_signal("rotation_requested")) hinge->add_user_signal("rotation_requested");
	if (!hinge->has_signal("busy_finished")) hinge->add_user_signal("busy_finished");

	// Start with the floors as built
	hinge->set_meta("swapped", false);
	hinge->set_meta("target", false);
	hinge->set_meta("turning", false);
	hinge->set_meta("requested", false);
	hinge->set_meta("progress", 1.0);
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, carries out a swap that the GameManager allowed.

	// Nothing to do while not swapping
	auto hinge = GetSelf<Node3D>(instance);
	if (!(bool)hinge->get_meta("turning", false)) return;

	// Advance the fold with a gentle start and end
	double progress = (double)hinge->get_meta("progress", 1.0) + _delta / (double)hinge->get_meta("swap_time", 0.7);
	if (progress > 1.0) progress = 1.0;
	hinge->set_meta("progress", progress);
	PlaceAlongFold(hinge, progress * progress * (3.0 - 2.0 * progress));

	// At the end, let the GameManager settle the level and release the world
	if (progress >= 1.0)
	{
		hinge->set_meta("turning", false);
		hinge->set_meta("swapped", hinge->get_meta("target", false));
		hinge->call_deferred("emit_signal", "busy_finished");
	}
}

// Jenova Script Block End
JENOVA_SCRIPT_END
