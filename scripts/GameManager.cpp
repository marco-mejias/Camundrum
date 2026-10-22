
/* Camundrum - Game Manager */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/scene_tree.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/callable.hpp>
#include <Godot/classes/display_server.hpp>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Element walking, kept outside the script block because Jenova cannot read these parameter types inside it
static void CallOnElements(Node* parent, const StringName& method, const Variant& argument, bool with_argument)
{
	// Purpose: call a contract function on every level element that has it, including pieces carried by turntables.

	// Look at each direct child of the given node
	for (int index = 0; index < parent->get_child_count(); index++)
	{
		// Call the function on the ones that have it
		Node* element = parent->get_child(index);
		if (element->has_method(method))
		{
			if (with_argument) element->call(method, argument);
			else element->call(method);
		}

		// Pieces placed on a carrier, such as a turntable, are elements too, and so are buttons mounted on a block or door
		if (element->is_in_group("carrier") || element->is_class("StaticBody3D")) CallOnElements(element, method, argument, with_argument);
	}
}
static void ConnectElements(Node* parent, const StringName& signal, const Callable& callable)
{
	// Purpose: connect a function to a signal on every level element that has that signal, including pieces on carriers.

	// Look at each direct child of the given node
	for (int index = 0; index < parent->get_child_count(); index++)
	{
		// Connect the ones that have the signal
		Node* element = parent->get_child(index);
		if (element->has_signal(signal)) element->connect(signal, callable);

		// Pieces placed on a carrier are elements too
		if (element->is_in_group("carrier")) ConnectElements(element, signal, callable);
	}
}

// What is on screen: the level being played, or one of the menus over it
enum Screen
{
	SCREEN_PLAYING = 0,
	SCREEN_MAIN_MENU = 1,
	SCREEN_LEVEL_SELECT = 2,
	SCREEN_PAUSE = 3
};

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties
JENOVA_PROPERTY(int, starting_view, 0)
JENOVA_PROPERTY(int, level_number, -1)
JENOVA_PROPERTY(double, death_time, 0.45)

// State
Node* manager = nullptr;
Node* level_controller = nullptr;
Node* input_controller = nullptr;
Node* camera_controller = nullptr;
Node* depth_controller = nullptr;
Node* ui_controller = nullptr;
Node* background_controller = nullptr;
Node* settings_controller = nullptr;
Node* audio_controller = nullptr;
Node* level = nullptr;
Node* player = nullptr;
Node* turning_carrier = nullptr;
Node* waiting_carrier = nullptr;
int current_view = VIEW_SIDE;
int first_view = VIEW_SIDE;
int default_view = VIEW_SIDE;
int queued_view = -1;
bool is_switching = false;
bool is_busy = false;
bool is_turning = false;
bool is_complete = false;
bool is_dying = false;
bool world_frozen = false;
double death_length = 0.45;
double death_left = 0.0;
int screen = SCREEN_PLAYING;
int select_return = SCREEN_MAIN_MENU;
bool test_mode = false;
bool is_advancing = false;
bool advance_faded = false;
double advance_left = 0.0;

// World
void RefreshFreeze()
{
	// Purpose: pause or release every element, depending on whether anything is currently changing the level.

	// The world waits during a view slide, a turn, a death, after the level is finished and while a menu is open
	bool wanted = is_switching || is_busy || is_complete || is_dying || screen != SCREEN_PLAYING;
	if (wanted == world_frozen) return;
	world_frozen = wanted;

	// Tell every element, the player included, through the contract
	if (level) CallOnElements(level, "SetFrozen", wanted ? 1 : 0, true);
}

// Feedback
void RefuseFeedback()
{
	// Purpose: tell the player that a switch or a turn was refused, with a buzz and a small knock of the camera.

	// Sound and a short, light shake
	PlaySound(manager, SOUND_BLOCKED);
	if (camera_controller) camera_controller->call("Shake", 0.08, 0.15);
}

// Views
void EnterView(int view, bool instant)
{
	// Purpose: make a view the active one, telling each module what that view means for it.

	// A change that would leave the player or a crate inside something is refused, the depth module makes the obstacle blink
	if (!instant && depth_controller && !(bool)depth_controller->call("CanSwitchTo", view))
	{
		RefuseFeedback();
		return;
	}
	if (!instant) PlaySound(manager, SOUND_SWITCH);

	// Record the new view, and tell the depth module about it
	current_view = view;
	if (depth_controller) depth_controller->call("SetView", view);

	// Give the player the physics rules of the view, and let the elements react to it
	if (player) player->call("SetMode", view);
	if (level) CallOnElements(level, "OnViewChanged", view, true);

	// Side and top are flat, iso shows the real depth, the depth module answers whether it started moving the level
	bool level_moving = false;
	if (depth_controller) level_moving = depth_controller->call(view == VIEW_ISO ? "Restore" : "Flatten", instant ? 1 : 0);

	// While the level slides into its new shape the world waits, the move_finished signal releases it
	if (level_moving) is_switching = true;
	RefreshFreeze();

	// Move the camera, either at once or with its animation, and light up the view in the indicator
	if (camera_controller) camera_controller->call(instant ? "SnapTo" : "TransitionTo", view);
	if (ui_controller) ui_controller->call("ShowView", view);
	if (background_controller) background_controller->call(instant ? "SnapView" : "ShowView", view);
}
void RunQueuedView()
{
	// Purpose: carry out a view asked for while the level was busy, once it is free again.

	// Take the waiting request, if it is still a different view
	int waiting_view = queued_view;
	queued_view = -1;
	if (waiting_view >= 0 && waiting_view != current_view && !is_complete && !is_dying) EnterView(waiting_view, false);
}

// Rotations
void TryRotation(Node* carrier)
{
	// Purpose: let a carrier such as a turntable turn, unless the turn would leave something inside a wall.

	// Ask the depth module, which tries the turn, makes the obstacle blink if there is one, and prepares the pieces
	// A floor swap mirrors the level instead of turning part of it, but goes through the same steps
	// A depth lift moves along the depth axis instead, and is checked along its way
	const char* check = "PrepareRotation";
	if (carrier->is_in_group("floor_swap")) check = "PrepareSwap";
	if (carrier->is_in_group("depth_lift")) check = "PrepareShift";
	bool allowed = depth_controller ? (bool)depth_controller->call(check, carrier) : true;
	if (!allowed)
	{
		carrier->call("CancelTurn");
		RefuseFeedback();
		return;
	}
	PlaySound(manager, SOUND_TURN);

	// Pause the world while the carrier turns, the busy_finished signal releases it
	turning_carrier = carrier;
	is_busy = true;
	is_turning = true;
	RefreshFreeze();
	carrier->call("StartTurn");
}
void EndTurn()
{
	// Purpose: hand the controls back once a turn and the slide after it are completely over.

	// The player starts again from rest, so no speed from before the turn pushes it into the new layout
	is_turning = false;
	if (player) player->call("StopMotion");
}
void RunWaitingRotation()
{
	// Purpose: carry out a turn that was asked for while the world was paused, once it is free again.

	// Take the waiting request, if the world is free now
	if (!waiting_carrier || world_frozen) return;
	Node* carrier = waiting_carrier;
	waiting_carrier = nullptr;
	TryRotation(carrier);
}

// Level
void RestartLevel()
{
	// Purpose: put the level and the player back to how they started, used on death and on a restart request.

	// Cancel everything in progress or waiting
	is_switching = false;
	is_busy = false;
	is_turning = false;
	is_complete = false;
	is_dying = false;
	is_advancing = false;
	queued_view = -1;
	turning_carrier = nullptr;
	waiting_carrier = nullptr;
	if (ui_controller) ui_controller->call("ShowComplete", 0);

	// Elements reset first, so a turntable is unturned before positions are restored
	if (level) CallOnElements(level, "Reset", Variant(), false);
	if (depth_controller) depth_controller->call("ResetAll");
	if (player) player->call("Halt");

	// Begin again in the starting view, with no camera animation, and release the world
	EnterView(first_view, true);
	RefreshFreeze();
}
void AttachLevel()
{
	// Purpose: connect the level now placed under the Game node, hand it to the modules and start it in the starting view.

	// Find the level and its player
	level = manager->get_node_or_null("Level");
	player = manager->get_node_or_null("Level/Player");

	// A level can start in a view of its own with the starting_view metadata on its root, otherwise the Game node's setting is used
	first_view = default_view;
	if (level && level->has_meta("starting_view")) first_view = level->get_meta("starting_view");
	if (first_view < VIEW_SIDE || first_view >= VIEW_COUNT) first_view = default_view;

	// Listen for the player's death, the exits and the elements that change the level by themselves
	if (player) player->connect("died", Callable(manager, "OnPlayerDied"));
	if (level) ConnectElements(level, "reached", Callable(manager, "OnExitReached"));
	if (level) ConnectElements(level, "rotation_requested", Callable(manager, "OnRotationRequested"));
	if (level) ConnectElements(level, "busy_finished", Callable(manager, "OnBusyFinished"));

	// Open the holes in its boards and frame it with the camera, then hand it to the depth module so it can track its elements
	if (level_controller && level) level_controller->call("PrepareLevel", level);
	if (camera_controller && level) camera_controller->call("FrameLevel", level);
	if (depth_controller && level) depth_controller->call("RegisterLevel", level);

	// The new pieces start unpaused, so the pause is applied to them again if a menu is open
	world_frozen = false;
	RestartLevel();
}
void StartGameLevel(int index)
{
	// Purpose: load a level of the game by its number and play it, coming out of black.

	// Load and connect it
	if (!level_controller || !(bool)level_controller->call("LoadGameLevel", index)) return;
	test_mode = false;
	screen = SCREEN_PLAYING;
	AttachLevel();

	// Close the menus and reveal the level, naming it as it appears
	if (ui_controller) ui_controller->call("HideMenus");
	String name = level_controller ? (String)level_controller->call("LevelName", index) : String();
	if (ui_controller) ui_controller->call("ShowLevelTitle", String("Level ") + String::num_int64(index + 1) + String("  ") + String::chr(0xB7) + String("  ") + name);
	if (ui_controller) ui_controller->call("FadeTo", 1.0, 0.0);
	if (ui_controller) ui_controller->call("FadeTo", 0.0, 0.35);
}
void OpenMainMenu(int finished_all)
{
	// Purpose: show the main menu over the level, which waits behind it.

	// Pause everything and show the menu, saying whether the whole game was finished
	screen = SCREEN_MAIN_MENU;
	is_advancing = false;
	RefreshFreeze();
	int count = level_controller ? (int)(double)level_controller->call("LevelCount") : 0;
	if (ui_controller) ui_controller->call("ShowComplete", 0);
	if (ui_controller) ui_controller->call("ShowMainMenu", count, finished_all);
	if (ui_controller) ui_controller->call("FadeTo", 0.0, 0.25);
}
void OpenLevelSelect()
{
	// Purpose: show the level selector, remembering which menu to go back to.

	// Note where it was opened from and show it with the levels open so far
	select_return = (screen == SCREEN_PAUSE) ? SCREEN_PAUSE : SCREEN_MAIN_MENU;
	screen = SCREEN_LEVEL_SELECT;
	RefreshFreeze();
	int count = level_controller ? (int)(double)level_controller->call("LevelCount") : 0;
	int open_count = level_controller ? (int)(double)level_controller->call("UnlockedCount") : 0;
	if (ui_controller) ui_controller->call("ShowLevelSelect", count, open_count);
}
void ResumePlaying()
{
	// Purpose: close the menus and let the level go on.

	// Back to playing
	screen = SCREEN_PLAYING;
	if (ui_controller) ui_controller->call("HideMenus");
	RefreshFreeze();
}
void AdvanceLevel(double delta)
{
	// Purpose: after a game level is finished, wait a moment, fade out, record it and load the next level, or go back to the menu after the last one.

	// Count down the celebration, fading out for the last part of it
	advance_left -= delta;
	if (!advance_faded && advance_left <= 0.4)
	{
		advance_faded = true;
		if (ui_controller) ui_controller->call("FadeTo", 1.0, 0.4);
	}
	if (advance_left > 0.0) return;

	// Record the level as finished, which opens the next one, and go there
	is_advancing = false;
	if (!level_controller) return;
	level_controller->call("CompleteCurrent");
	int next = (int)(double)level_controller->call("CurrentLevel") + 1;
	int count = (int)(double)level_controller->call("LevelCount");
	if (next < count) StartGameLevel(next);
	else OpenMainMenu(1);
}
void BeginDeath()
{
	// Purpose: play the player's death before the level restarts: the player vanishes, the camera shakes and the screen fades.

	// Only one death at a time, and none after the level is finished
	if (is_dying || is_complete) return;
	is_dying = true;
	death_left = death_length;
	RefreshFreeze();

	// Show it
	PlaySound(manager, SOUND_DEATH);
	if (player) player->call("ShowDeath");
	if (camera_controller) camera_controller->call("Shake", 0.35, 0.3);
	if (ui_controller) ui_controller->call("FadeTo", 1.0, death_length);
}

// Events
void OnViewRequested(int view)
{
	// Purpose: react to the InputController asking for a view.

	// Ignore requests for a view that does not exist or is already active, and any request in a menu or once the level is finished or lost
	if (screen != SCREEN_PLAYING || view < 0 || view >= VIEW_COUNT || view == current_view || is_complete || is_dying) return;

	// Nothing can be asked for while the level turns, the player has to wait for it to end
	if (is_turning) return;

	// While the level is still sliding into a view, keep the latest request for when it ends
	if (is_switching || is_busy)
	{
		queued_view = view;
		return;
	}

	// Switch to it with the camera animation
	EnterView(view, false);
}
void OnDepthMoveFinished()
{
	// Purpose: react to the depth module reporting that the level has finished sliding into the new view.

	// The switch is over, release the world and carry out a waiting request
	is_switching = false;
	if (is_turning) EndTurn();
	RefreshFreeze();
	RunQueuedView();
	RunWaitingRotation();
}
void OnRotationRequested(Node* carrier)
{
	// Purpose: react to a carrier, such as a turntable, asking to turn.

	// While the world is paused the request waits, otherwise it is tried at once
	if (world_frozen)
	{
		waiting_carrier = carrier;
		return;
	}
	TryRotation(carrier);
}
void OnBusyFinished()
{
	// Purpose: react to a carrier finishing its turn.

	// In a flat view the turned pieces slide back to the front edge, and the world waits for that too
	bool sliding = false;
	if (depth_controller && turning_carrier) sliding = depth_controller->call("FinishRotation", turning_carrier);
	turning_carrier = nullptr;
	is_busy = false;
	if (sliding) is_switching = true;

	// The turn lands with a small knock
	if (camera_controller) camera_controller->call("Shake", 0.06, 0.12);
	RefreshFreeze();

	// When nothing else has to move, the turn is over and what was waiting is carried out
	if (sliding) return;
	EndTurn();
	RunQueuedView();
	RunWaitingRotation();
}
void OnExitReached()
{
	// Purpose: react to an exit reporting that the player touched it.

	// Only the first report counts
	if (is_complete || is_dying) return;
	is_complete = true;

	// Stop the world and show the message, a restart request starts the level again
	RefreshFreeze();
	PlaySound(manager, SOUND_EXIT);
	if (player) player->call("ShowExit");
	if (ui_controller) ui_controller->call("ShowComplete", 1);

	// A level of the game moves on to the next one by itself, a test level waits for R
	if (test_mode) return;
	is_advancing = true;
	advance_faded = false;
	advance_left = 1.3;
}
void OnInteractRequested()
{
	// Purpose: react to the InputController asking to use whatever the player stands next to.

	// Nothing can be used while the world is paused, a menu counts as paused
	if (world_frozen || !level) return;

	// Let every element that can be used decide whether the player is close enough
	CallOnElements(level, "OnInteract", Variant(), false);
}
void OnRestartRequested()
{
	// Purpose: react to the InputController asking to restart the level.

	// Start the level over, clearing any fade left by a death in progress, but not from a menu
	if (screen != SCREEN_PLAYING) return;
	RestartLevel();
	if (ui_controller) ui_controller->call("FadeTo", 0.0, 0.15);
}
void OnPlayPressed()
{
	// Purpose: react to the Play button: continue with the first level not finished yet.

	// Ask the level module which one that is
	if (level_controller) StartGameLevel((int)(double)level_controller->call("NextToPlay"));
}
void OnSelectPressed()
{
	// Purpose: react to a Select level button, in the main menu or the pause menu.

	// Open the selector
	OpenLevelSelect();
}
void OnBackPressed()
{
	// Purpose: react to the Back button of the selector, going back to the menu it was opened from.

	// Back to the pause menu, or to the main menu
	if (select_return == SCREEN_PAUSE)
	{
		screen = SCREEN_PAUSE;
		if (ui_controller) ui_controller->call("ShowPause", 1);
		return;
	}
	OpenMainMenu(0);
}
void OnPauseRequested()
{
	// Purpose: react to the Escape key: pause while playing, go back from a menu.

	// While playing, pause, unless the level is ending
	if (screen == SCREEN_PLAYING)
	{
		if (is_complete || is_dying) return;
		screen = SCREEN_PAUSE;
		RefreshFreeze();
		if (ui_controller) ui_controller->call("ShowPause", 1);
		return;
	}

	// From the pause menu, go back to playing; from the selector, go back where it was opened from
	if (screen == SCREEN_PAUSE) ResumePlaying();
	else if (screen == SCREEN_LEVEL_SELECT) OnBackPressed();
}
void OnLevelChosen(int index)
{
	// Purpose: react to a level being chosen in the selector.

	// Only open levels can be played
	int open_count = level_controller ? (int)(double)level_controller->call("UnlockedCount") : 0;
	if (index >= 0 && index < open_count) StartGameLevel(index);
}
void OnResumePressed()
{
	// Purpose: react to the Resume button of the pause menu.

	// Back to playing
	ResumePlaying();
}
void OnRestartPressed()
{
	// Purpose: react to the Restart button of the pause menu.

	// Close the menu and start the level over
	ResumePlaying();
	RestartLevel();
	if (ui_controller) ui_controller->call("FadeTo", 0.0, 0.15);
}
void OnMenuPressed()
{
	// Purpose: react to the Main menu button of the pause menu.

	// Leave the level waiting behind the main menu
	test_mode = false;
	OpenMainMenu(0);
}
// Options
double SettingValue(const String& key)
{
	// Purpose: read one of the player's options from the settings module.

	// Its default when there is no settings module
	if (!settings_controller) return key == "fullscreen" ? 0.0 : 1.0;
	return (double)settings_controller->call("Get", key);
}
void ApplySetting(const String& key, double value)
{
	// Purpose: hand one option to the module it belongs to: volumes to the audio, shake to the camera, fullscreen to the window.

	// Volumes go to the audio bus of the same name
	if (key == "master" && audio_controller) audio_controller->call("SetVolume", "Master", value);
	if (key == "effects" && audio_controller) audio_controller->call("SetVolume", "Effects", value);
	if (key == "interface" && audio_controller) audio_controller->call("SetVolume", "Interface", value);
	if (key == "shake" && camera_controller) camera_controller->call("SetShakeScale", value);

	// The window fills the screen or goes back to a window
	if (key == "fullscreen")
	{
		DisplayServer* display = DisplayServer::get_singleton();
		DisplayServer::WindowMode wanted = value > 0.5 ? DisplayServer::WINDOW_MODE_FULLSCREEN : DisplayServer::WINDOW_MODE_WINDOWED;
		if (display && display->window_get_mode() != wanted) display->window_set_mode(wanted);
	}
}
void ApplyAllSettings()
{
	// Purpose: put every saved option into effect, when the game starts.

	// Each one in turn
	const char* keys[5] = { "master", "effects", "interface", "shake", "fullscreen" };
	for (const char* key : keys) ApplySetting(key, SettingValue(key));
}
void OnOptionsPressed()
{
	// Purpose: react to an Options button, opening the options screen with the current settings.

	// The interface comes back to the menu it was opened from by itself
	if (ui_controller) ui_controller->call("ShowOptions", SettingValue("master"), SettingValue("effects"), SettingValue("interface"), SettingValue("shake"), SettingValue("fullscreen") > 0.5 ? 1 : 0);
}
void OnSettingChanged(String key, double value)
{
	// Purpose: react to the player changing an option: save it and put it into effect at once.

	// Keep and apply
	if (settings_controller) settings_controller->call("Set", key, value);
	ApplySetting(key, value);
}
void OnQuitPressed()
{
	// Purpose: react to the Quit button of the main menu.

	// Close the game
	manager->get_tree()->quit();
}
void OnPlayerDied()
{
	// Purpose: react to the player reporting its death.

	// Play the death, the level restarts when it ends
	BeginDeath();
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once every module is in the tree, finds them, connects them together and starts the level.

	// Copy the settings now, because properties may not be up to date inside calls made by other modules
	auto game = GetSelf<Node>(instance);
	manager = game;
	default_view = starting_view;
	first_view = default_view;
	death_length = death_time;

	// Find the modules this manager connects
	level_controller = game->get_node_or_null("LevelController");
	input_controller = game->get_node_or_null("InputController");
	camera_controller = game->get_node_or_null("CameraController");
	depth_controller = game->get_node_or_null("DepthController");
	ui_controller = game->get_node_or_null("UI");
	background_controller = game->get_node_or_null("Background");
	settings_controller = game->get_node_or_null("Settings");
	audio_controller = game->get_node_or_null("AudioController");

	// Listen to what the input asks for
	if (input_controller) input_controller->connect("view_requested", Callable(game, "OnViewRequested"));
	if (input_controller) input_controller->connect("restart_requested", Callable(game, "OnRestartRequested"));
	if (input_controller) input_controller->connect("interact_requested", Callable(game, "OnInteractRequested"));
	if (input_controller) input_controller->connect("pause_requested", Callable(game, "OnPauseRequested"));

	// Listen to the end of each slide of the level
	if (depth_controller) depth_controller->connect("move_finished", Callable(game, "OnDepthMoveFinished"));

	// Listen to the menu buttons
	if (ui_controller) ui_controller->connect("play_pressed", Callable(game, "OnPlayPressed"));
	if (ui_controller) ui_controller->connect("select_pressed", Callable(game, "OnSelectPressed"));
	if (ui_controller) ui_controller->connect("back_pressed", Callable(game, "OnBackPressed"));
	if (ui_controller) ui_controller->connect("level_chosen", Callable(game, "OnLevelChosen"));
	if (ui_controller) ui_controller->connect("resume_pressed", Callable(game, "OnResumePressed"));
	if (ui_controller) ui_controller->connect("restart_pressed", Callable(game, "OnRestartPressed"));
	if (ui_controller) ui_controller->connect("menu_pressed", Callable(game, "OnMenuPressed"));
	if (ui_controller) ui_controller->connect("quit_pressed", Callable(game, "OnQuitPressed"));
	if (ui_controller) ui_controller->connect("options_pressed", Callable(game, "OnOptionsPressed"));
	if (ui_controller) ui_controller->connect("setting_changed", Callable(game, "OnSettingChanged"));

	// The player's saved options take effect before anything is seen or heard
	ApplyAllSettings();

	// A level number on the Game node plays that test level straight away, with no menu
	test_mode = level_number >= 0;
	if (test_mode && level_controller) level_controller->call("LoadTestLevel", level_number);

	// Connect whichever level is now in the game, and start in the starting view with no camera animation
	screen = test_mode ? SCREEN_PLAYING : SCREEN_MAIN_MENU;
	AttachLevel();

	// The game itself starts at the main menu, with the level in the scene waiting behind it
	if (!test_mode) OpenMainMenu(0);
}
void OnDestroy(Caller* instance)
{
	// Purpose: called when the node leaves the scene tree, drops every module reference.

	// Forget the modules so nothing uses them after they are freed
	manager = nullptr;
	level_controller = nullptr;
	input_controller = nullptr;
	camera_controller = nullptr;
	depth_controller = nullptr;
	ui_controller = nullptr;
	background_controller = nullptr;
	settings_controller = nullptr;
	audio_controller = nullptr;
	level = nullptr;
	player = nullptr;
	turning_carrier = nullptr;
	waiting_carrier = nullptr;
}
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// Purpose: called every physics frame, carries the input to the player and lets a death play out.

	// When a death has played out, restart the level and clear the screen again
	if (is_dying)
	{
		death_left -= _delta;
		if (death_left <= 0.0)
		{
			RestartLevel();
			if (ui_controller) ui_controller->call("FadeTo", 0.0, 0.3);
		}
	}

	// After a finished level, move on to the next one when the celebration is over
	if (is_advancing) AdvanceLevel(_delta);

	// Do nothing more until both ends of the input exist
	if (!input_controller || !player) return;

	// Read what the input sampled this frame
	Vector2 axis = input_controller->call("GetMoveAxis");
	bool jump_pressed = input_controller->call("IsJumpPressed");
	bool jump_held = input_controller->call("IsJumpHeld");

	// In a menu the player gets no input at all
	if (screen != SCREEN_PLAYING)
	{
		player->call("SetMoveInput", 0.0, 0.0, 0, 0);
		return;
	}

	// Hand it to the player as plain numbers, which moves with it in its own physics step
	player->call("SetMoveInput", (double)axis.x, (double)axis.y, jump_pressed ? 1 : 0, jump_held ? 1 : 0);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
