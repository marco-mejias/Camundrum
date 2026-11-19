
/* Camundrum - Game Manager
 * The only module that knows the others: routes input, view switches, deaths, menus and level flow between them.
 */

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
// Call a contract function on every level element that has it, including pieces carried by turntables
static void CallOnElements(Node* parent, const StringName& method, const Variant& argument, bool with_argument)
{
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

// Connect a function to a signal on every level element that has that signal, including pieces on carriers
static void ConnectElements(Node* parent, const StringName& signal, const Callable& callable)
{
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
Node* manager = nullptr; // This node
Node* level_controller = nullptr; // Loads levels and keeps the progress
Node* input_controller = nullptr; // Turns keys into requests
Node* camera_controller = nullptr; // Moves and frames the camera
Node* depth_controller = nullptr; // Flattens and restores depth, checks every switch
Node* ui_controller = nullptr; // Menus, view bar and fades
Node* background_controller = nullptr; // Neon pyramids behind the level
Node* settings_controller = nullptr; // The player's saved options
Node* audio_controller = nullptr; // Volumes of the sound buses
Node* level = nullptr; // The level being played
Node* player = nullptr; // The player inside that level
Node* turning_carrier = nullptr; // Turntable or lift turning right now
Node* waiting_carrier = nullptr; // Turntable or lift asked for while the world was busy
int current_view = VIEW_SIDE; // The active view
int first_view = VIEW_SIDE; // View the current level starts and restarts in
int default_view = VIEW_SIDE; // Start view for levels that do not set their own
int queued_view = -1; // Switch asked for during another one, -1 when none
bool is_switching = false; // True while a view switch plays out
bool is_busy = false; // True while a carrier turns
bool is_turning = false; // True from a turn request until everything has settled
bool is_complete = false; // True once the flag has been reached
bool is_dying = false; // True while the death animation plays
bool world_frozen = false; // True while elements are paused
double death_length = 0.45; // Copy of death_time, safe to read every frame
double death_left = 0.0; // Time left before the level restarts after a death
int screen = SCREEN_PLAYING; // Playing, or which menu is open
int select_return = SCREEN_MAIN_MENU; // Menu the level selector goes back to
bool test_mode = false; // True when a sandbox level is played straight from the Game node
bool is_advancing = false; // True while moving on to the next level
bool advance_faded = false; // True once the screen has gone black for that move
double advance_left = 0.0; // Time left in that move

// ---- World ----

// Pause or release every element, depending on whether anything is currently changing the level
void RefreshFreeze()
{
	// The world waits during a view slide, a turn, a death, after the level is finished and while a menu is open
	bool wanted = is_switching || is_busy || is_complete || is_dying || screen != SCREEN_PLAYING;
	if (wanted == world_frozen) return;
	world_frozen = wanted;

	// Tell every element, the player included, through the contract
	if (level) CallOnElements(level, "SetFrozen", wanted ? 1 : 0, true);
}

// ---- Feedback ----

// Tell the player that a switch or a turn was refused, with a buzz and a small knock of the camera
void RefuseFeedback()
{
	// Sound and a short, light shake
	PlaySound(manager, SOUND_BLOCKED);
	if (camera_controller) camera_controller->call("Shake", 0.08, 0.15);
}

// ---- Views ----

// Make a view the active one, telling each module what that view means for it
void EnterView(int view, bool instant)
{
	// 1. A change that would leave the player or a crate inside something is refused, the depth module makes the obstacle blink
	if (!instant && depth_controller && !(bool)depth_controller->call("CanSwitchTo", view))
	{
		RefuseFeedback();
		return;
	}
	if (!instant) PlaySound(manager, SOUND_SWITCH);

	// 2. Record the new view, and tell the depth module about it
	current_view = view;
	if (depth_controller) depth_controller->call("SetView", view);

	// 3. Give the player the physics rules of the view, and let the elements react to it
	if (player) player->call("SetMode", view);
	if (level) CallOnElements(level, "OnViewChanged", view, true);

	// 4. Side and top are flat, iso shows the real depth, the depth module answers whether it started moving the level
	bool level_moving = false;
	if (depth_controller) level_moving = depth_controller->call(view == VIEW_ISO ? "Restore" : "Flatten", instant ? 1 : 0);

	// 5. While the level slides into its new shape the world waits, the move_finished signal releases it
	if (level_moving) is_switching = true;
	RefreshFreeze();

	// 6. Move the camera, either at once or with its animation, and light up the view in the indicator
	if (camera_controller) camera_controller->call(instant ? "SnapTo" : "TransitionTo", view);
	if (ui_controller) ui_controller->call("ShowView", view);
	if (background_controller) background_controller->call(instant ? "SnapView" : "ShowView", view);
}

// Carry out a view asked for while the level was busy, once it is free again
void RunQueuedView()
{
	// Take the waiting request, if it is still a different view
	int waiting_view = queued_view;
	queued_view = -1;
	if (waiting_view >= 0 && waiting_view != current_view && !is_complete && !is_dying) EnterView(waiting_view, false);
}

// ---- Rotations ----

// Let a carrier such as a turntable turn, unless the turn would leave something inside a wall
void TryRotation(Node* carrier)
{
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

// Hand the controls back once a turn and the slide after it are completely over
void EndTurn()
{
	// The player starts again from rest, so no speed from before the turn pushes it into the new layout
	is_turning = false;
	if (player) player->call("StopMotion");
}

// Carry out a turn that was asked for while the world was paused, once it is free again
void RunWaitingRotation()
{
	// Take the waiting request, if the world is free now
	if (!waiting_carrier || world_frozen) return;
	Node* carrier = waiting_carrier;
	waiting_carrier = nullptr;
	TryRotation(carrier);
}

// ---- Level ----

// Put the level and the player back to how they started, used on death and on a restart request
void RestartLevel()
{
	// 1. Cancel everything in progress or waiting
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

	// 2. Elements reset first, so a turntable is unturned before positions are restored
	if (level) CallOnElements(level, "Reset", Variant(), false);
	if (depth_controller) depth_controller->call("ResetAll");
	if (player) player->call("Halt");

	// 3. Begin again in the starting view, with no camera animation, and release the world
	EnterView(first_view, true);
	RefreshFreeze();
}

// Connect the level now placed under the Game node, hand it to the modules and start it in the starting view
void AttachLevel()
{
	// 1. Find the level and its player
	level = manager->get_node_or_null("Level");
	player = manager->get_node_or_null("Level/Player");

	// 2. A level can start in a view of its own with the starting_view metadata on its root, otherwise the Game node's setting is used
	first_view = default_view;
	if (level && level->has_meta("starting_view")) first_view = level->get_meta("starting_view");
	if (first_view < VIEW_SIDE || first_view >= VIEW_COUNT) first_view = default_view;

	// 3. Listen for the player's death, the exits and the elements that change the level by themselves
	if (player) player->connect("died", Callable(manager, "OnPlayerDied"));
	if (level) ConnectElements(level, "reached", Callable(manager, "OnExitReached"));
	if (level) ConnectElements(level, "rotation_requested", Callable(manager, "OnRotationRequested"));
	if (level) ConnectElements(level, "busy_finished", Callable(manager, "OnBusyFinished"));

	// 4. Open the holes in its boards and frame it with the camera, then hand it to the depth module so it can track its elements
	if (level_controller && level) level_controller->call("PrepareLevel", level);
	if (camera_controller && level) camera_controller->call("FrameLevel", level);
	if (depth_controller && level) depth_controller->call("RegisterLevel", level);

	// 5. The new pieces start unpaused, so the pause is applied to them again if a menu is open
	world_frozen = false;
	RestartLevel();
}

// Load a level of the game by its number and play it, coming out of black
void StartGameLevel(int index)
{
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

// Show the main menu over the level, which waits behind it
void OpenMainMenu(int finished_all)
{
	// Pause everything and show the menu, saying whether the whole game was finished
	screen = SCREEN_MAIN_MENU;
	is_advancing = false;
	RefreshFreeze();
	int count = level_controller ? (int)(double)level_controller->call("LevelCount") : 0;
	if (ui_controller) ui_controller->call("ShowComplete", 0);
	if (ui_controller) ui_controller->call("ShowMainMenu", count, finished_all);
	if (ui_controller) ui_controller->call("FadeTo", 0.0, 0.25);
}

// Show the level selector, remembering which menu to go back to
void OpenLevelSelect()
{
	// Note where it was opened from and show it with the levels open so far
	select_return = (screen == SCREEN_PAUSE) ? SCREEN_PAUSE : SCREEN_MAIN_MENU;
	screen = SCREEN_LEVEL_SELECT;
	RefreshFreeze();
	int count = level_controller ? (int)(double)level_controller->call("LevelCount") : 0;
	int open_count = level_controller ? (int)(double)level_controller->call("UnlockedCount") : 0;
	if (ui_controller) ui_controller->call("ShowLevelSelect", count, open_count);
}

// Close the menus and let the level go on
void ResumePlaying()
{
	// Back to playing
	screen = SCREEN_PLAYING;
	if (ui_controller) ui_controller->call("HideMenus");
	RefreshFreeze();
}

// After a game level is finished, wait a moment, fade out, record it and load the next level, or go back to the menu after the last one
void AdvanceLevel(double delta)
{
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

// Play the player's death before the level restarts: the player vanishes, the camera shakes and the screen fades
void BeginDeath()
{
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

// ---- Events ----

// React to the InputController asking for a view
void OnViewRequested(int view)
{
	// 1. Ignore requests for a view that does not exist or is already active, and any request in a menu or once the level is finished or lost
	if (screen != SCREEN_PLAYING || view < 0 || view >= VIEW_COUNT || view == current_view || is_complete || is_dying) return;

	// 2. Nothing can be asked for while the level turns, the player has to wait for it to end
	if (is_turning) return;

	// 3. While the level is still sliding into a view, keep the latest request for when it ends
	if (is_switching || is_busy)
	{
		queued_view = view;
		return;
	}

	// 4. Switch to it with the camera animation
	EnterView(view, false);
}

// React to the depth module reporting that the level has finished sliding into the new view
void OnDepthMoveFinished()
{
	// The switch is over, release the world and carry out a waiting request
	is_switching = false;
	if (is_turning) EndTurn();
	RefreshFreeze();
	RunQueuedView();
	RunWaitingRotation();
}

// React to a carrier, such as a turntable, asking to turn
void OnRotationRequested(Node* carrier)
{
	// While the world is paused the request waits, otherwise it is tried at once
	if (world_frozen)
	{
		waiting_carrier = carrier;
		return;
	}
	TryRotation(carrier);
}

// React to a carrier finishing its turn
void OnBusyFinished()
{
	// 1. In a flat view the turned pieces slide back to the front edge, and the world waits for that too
	bool sliding = false;
	if (depth_controller && turning_carrier) sliding = depth_controller->call("FinishRotation", turning_carrier);
	turning_carrier = nullptr;
	is_busy = false;
	if (sliding) is_switching = true;

	// 2. The turn lands with a small knock
	if (camera_controller) camera_controller->call("Shake", 0.06, 0.12);
	RefreshFreeze();

	// 3. When nothing else has to move, the turn is over and what was waiting is carried out
	if (sliding) return;
	EndTurn();
	RunQueuedView();
	RunWaitingRotation();
}

// React to an exit reporting that the player touched it
void OnExitReached()
{
	// 1. Only the first report counts
	if (is_complete || is_dying) return;
	is_complete = true;

	// 2. Stop the world and show the message, a restart request starts the level again
	RefreshFreeze();
	PlaySound(manager, SOUND_EXIT);
	if (player) player->call("ShowExit");
	if (ui_controller) ui_controller->call("ShowComplete", 1);

	// 3. A level of the game moves on to the next one by itself, a test level waits for R
	if (test_mode) return;
	is_advancing = true;
	advance_faded = false;
	advance_left = 1.3;
}

// React to the InputController asking to use whatever the player stands next to
void OnInteractRequested()
{
	// Nothing can be used while the world is paused, a menu counts as paused
	if (world_frozen || !level) return;

	// Let every element that can be used decide whether the player is close enough
	CallOnElements(level, "OnInteract", Variant(), false);
}

// React to the InputController asking to restart the level
void OnRestartRequested()
{
	// Start the level over, clearing any fade left by a death in progress, but not from a menu
	if (screen != SCREEN_PLAYING) return;
	RestartLevel();
	if (ui_controller) ui_controller->call("FadeTo", 0.0, 0.15);
}

// React to the Play button: continue with the first level not finished yet
void OnPlayPressed()
{
	// Ask the level module which one that is
	if (level_controller) StartGameLevel((int)(double)level_controller->call("NextToPlay"));
}

// React to a Select level button, in the main menu or the pause menu
void OnSelectPressed()
{
	// Open the selector
	OpenLevelSelect();
}

// React to the Back button of the selector, going back to the menu it was opened from
void OnBackPressed()
{
	// Back to the pause menu, or to the main menu
	if (select_return == SCREEN_PAUSE)
	{
		screen = SCREEN_PAUSE;
		if (ui_controller) ui_controller->call("ShowPause", 1);
		return;
	}
	OpenMainMenu(0);
}

// React to the Escape key: pause while playing, go back from a menu
void OnPauseRequested()
{
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

// React to a level being chosen in the selector
void OnLevelChosen(int index)
{
	// Only open levels can be played
	int open_count = level_controller ? (int)(double)level_controller->call("UnlockedCount") : 0;
	if (index >= 0 && index < open_count) StartGameLevel(index);
}

// React to the Resume button of the pause menu
void OnResumePressed()
{
	// Back to playing
	ResumePlaying();
}

// React to the Restart button of the pause menu
void OnRestartPressed()
{
	// Close the menu and start the level over
	ResumePlaying();
	RestartLevel();
	if (ui_controller) ui_controller->call("FadeTo", 0.0, 0.15);
}

// React to the Main menu button of the pause menu
void OnMenuPressed()
{
	// Leave the level waiting behind the main menu
	test_mode = false;
	OpenMainMenu(0);
}
// Options
// Read one of the player's options from the settings module
double SettingValue(const String& key)
{
	// Its default when there is no settings module
	if (!settings_controller) return key == "fullscreen" ? 0.0 : 1.0;
	return (double)settings_controller->call("Get", key);
}

// Hand one option to the module it belongs to: volumes to the audio, shake to the camera, fullscreen to the window
void ApplySetting(const String& key, double value)
{
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

// Put every saved option into effect, when the game starts
void ApplyAllSettings()
{
	// Each one in turn
	const char* keys[5] = { "master", "effects", "interface", "shake", "fullscreen" };
	for (const char* key : keys) ApplySetting(key, SettingValue(key));
}

// React to an Options button, opening the options screen with the current settings
void OnOptionsPressed()
{
	// The interface comes back to the menu it was opened from by itself
	if (ui_controller) ui_controller->call("ShowOptions", SettingValue("master"), SettingValue("effects"), SettingValue("interface"), SettingValue("shake"), SettingValue("fullscreen") > 0.5 ? 1 : 0);
}

// React to the player changing an option: save it and put it into effect at once
void OnSettingChanged(String key, double value)
{
	// Keep and apply
	if (settings_controller) settings_controller->call("Set", key, value);
	ApplySetting(key, value);
}

// React to the Quit button of the main menu
// F11: flip fullscreen and remember it like the options switch does
void OnFullscreenRequested()
{
	OnSettingChanged("fullscreen", SettingValue("fullscreen") > 0.5 ? 0.0 : 1.0);
}

void OnQuitPressed()
{
	// Close the game
	manager->get_tree()->quit();
}

// React to the player reporting its death
void OnPlayerDied()
{
	// Play the death, the level restarts when it ends
	BeginDeath();
}

// ---- Routines ----

// Called once every module is in the tree, finds them, connects them together and starts the level
void OnReady(Caller* instance)
{
	// 1. Copy the settings now, because properties may not be up to date inside calls made by other modules
	auto game = GetSelf<Node>(instance);
	manager = game;
	default_view = starting_view;
	first_view = default_view;
	death_length = death_time;

	// 2. Find the modules this manager connects
	level_controller = game->get_node_or_null("LevelController");
	input_controller = game->get_node_or_null("InputController");
	camera_controller = game->get_node_or_null("CameraController");
	depth_controller = game->get_node_or_null("DepthController");
	ui_controller = game->get_node_or_null("UI");
	background_controller = game->get_node_or_null("Background");
	settings_controller = game->get_node_or_null("Settings");
	audio_controller = game->get_node_or_null("AudioController");

	// 3. Listen to what the input asks for
	if (input_controller) input_controller->connect("view_requested", Callable(game, "OnViewRequested"));
	if (input_controller) input_controller->connect("restart_requested", Callable(game, "OnRestartRequested"));
	if (input_controller) input_controller->connect("interact_requested", Callable(game, "OnInteractRequested"));
	if (input_controller) input_controller->connect("pause_requested", Callable(game, "OnPauseRequested"));
	if (input_controller) input_controller->connect("fullscreen_requested", Callable(game, "OnFullscreenRequested"));

	// 4. Listen to the end of each slide of the level
	if (depth_controller) depth_controller->connect("move_finished", Callable(game, "OnDepthMoveFinished"));

	// 5. Listen to the menu buttons
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

	// 6. The player's saved options take effect before anything is seen or heard
	ApplyAllSettings();

	// 7. A level number on the Game node plays that test level straight away, with no menu
	test_mode = level_number >= 0;
	if (test_mode && level_controller) level_controller->call("LoadTestLevel", level_number);

	// 8. Connect whichever level is now in the game, and start in the starting view with no camera animation
	screen = test_mode ? SCREEN_PLAYING : SCREEN_MAIN_MENU;
	AttachLevel();

	// 9. The game itself starts at the main menu, with the level in the scene waiting behind it
	if (!test_mode) OpenMainMenu(0);
}

// Called when the node leaves the scene tree, drops every module reference
void OnDestroy(Caller* instance)
{
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

// Called every physics frame, carries the input to the player and lets a death play out
void OnPhysicsProcess(Caller* instance, double _delta)
{
	// 1. When a death has played out, restart the level and clear the screen again
	if (is_dying)
	{
		death_left -= _delta;
		if (death_left <= 0.0)
		{
			RestartLevel();
			if (ui_controller) ui_controller->call("FadeTo", 0.0, 0.3);
		}
	}

	// 2. After a finished level, move on to the next one when the celebration is over
	if (is_advancing) AdvanceLevel(_delta);

	// 3. Do nothing more until both ends of the input exist
	if (!input_controller || !player) return;

	// 4. Read what the input sampled this frame
	Vector2 axis = input_controller->call("GetMoveAxis");
	bool jump_pressed = input_controller->call("IsJumpPressed");
	bool jump_held = input_controller->call("IsJumpHeld");

	// 5. In a menu the player gets no input at all
	if (screen != SCREEN_PLAYING)
	{
		player->call("SetMoveInput", 0.0, 0.0, 0, 0);
		return;
	}

	// 6. Hand it to the player as plain numbers, which moves with it in its own physics step
	player->call("SetMoveInput", (double)axis.x, (double)axis.y, jump_pressed ? 1 : 0, jump_held ? 1 : 0);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
