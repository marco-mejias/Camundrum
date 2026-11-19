
/* Camundrum - Settings Controller
 * Keeps the player's options (volumes, screen shake, fullscreen) in user://settings.cfg.
 */

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
Ref<ConfigFile> settings; // The options file in memory

// ---- Values ----

// The value an option has before the player changes it
double DefaultFor(const String& key)
{
	// Comfortable volumes, full shake, a window
	if (key == "master") return 0.8;
	if (key == "effects") return 1.0;
	if (key == "interface") return 0.8;
	if (key == "shake") return 1.0;
	if (key == "fullscreen") return 1.0; // The game opens fullscreen until the player chooses otherwise
	return 0.0;
}

// ---- Commands ----

// Let the GameManager read an option
double Get(String key)
{
	// The saved value, or its default
	if (settings.is_null()) return DefaultFor(key);
	return (double)settings->get_value("options", key, DefaultFor(key));
}

// Let the GameManager change an option, which is saved at once
void Set(String key, double value)
{
	// Store and write it
	if (settings.is_null()) return;
	settings->set_value("options", key, value);
	settings->save("user://settings.cfg");
}

// ---- Routines ----

// Called once the node is in the tree, reads the saved options, if there are any
void OnReady(Caller* instance)
{
	// A missing file simply means every option keeps its default
	settings.instantiate();
	settings->load("user://settings.cfg");
}

// Called when the node leaves the scene tree, lets the options go
void OnDestroy(Caller* instance)
{
	// Drop the file
	settings.unref();
}

// Jenova Script Block End
JENOVA_SCRIPT_END
