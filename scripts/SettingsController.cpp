
/* Camundrum - Settings Controller */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/config_file.hpp>
#include <Godot/variant/variant.hpp>

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// The player's options, kept in user://settings.cfg so they last between sessions:
// master, effects and interface volumes (0 to 1), shake (0 to 1, how strong the camera shakes) and fullscreen (0 or 1)
// This module only stores them; the GameManager hands each one to the module it belongs to

// State
Ref<ConfigFile> settings;

// Values
double DefaultFor(const String& key)
{
	// Purpose: the value an option has before the player changes it.

	// Comfortable volumes, full shake, a window
	if (key == "master") return 0.8;
	if (key == "effects") return 1.0;
	if (key == "interface") return 0.8;
	if (key == "shake") return 1.0;
	return 0.0;
}

// Commands
double Get(String key)
{
	// Purpose: let the GameManager read an option.

	// The saved value, or its default
	if (settings.is_null()) return DefaultFor(key);
	return (double)settings->get_value("options", key, DefaultFor(key));
}
void Set(String key, double value)
{
	// Purpose: let the GameManager change an option, which is saved at once.

	// Store and write it
	if (settings.is_null()) return;
	settings->set_value("options", key, value);
	settings->save("user://settings.cfg");
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node is in the tree, reads the saved options, if there are any.

	// A missing file simply means every option keeps its default
	settings.instantiate();
	settings->load("user://settings.cfg");
}
void OnDestroy(Caller* instance)
{
	// Purpose: called when the node leaves the scene tree, lets the options go.

	// Drop the file
	settings.unref();
}

// Jenova Script Block End
JENOVA_SCRIPT_END
