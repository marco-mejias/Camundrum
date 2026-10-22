
/* Jenova C++ Node Base Script (Meteora) */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/$BASE_TYPE_HEADER$>
#include <Godot/variant/variant.hpp>

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties
JENOVA_PROPERTY(Variant, self, Variant::NIL, Usage:PROPERTY_USAGE_NO_EDITOR)

// Routines
void OnAwake(Caller* instance)
{
	// Called when Node enters SceneTree
	self = GetSelf<$BASE_TYPE$>(instance);
}
void OnDestroy()
{
	// Called when Node exits SceneTree
	self = Variant::NIL;
}
void OnReady()
{
	// Called when Node and all of its children entered SceneTree
	auto _this = GetSelf<$BASE_TYPE$>(self);
}
void OnProcess(double _delta)
{
	// Called on every frame
	auto _this = GetSelf<$BASE_TYPE$>(self);
}

// Jenova Script Block End
JENOVA_SCRIPT_END