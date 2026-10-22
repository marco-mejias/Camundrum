
/* Camundrum - UI Controller */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/color.hpp>
#include <Godot/variant/vector2.hpp>
#include <Godot/variant/callable.hpp>
#include <Godot/classes/button.hpp>
#include <Godot/classes/control.hpp>

// Game
#include "scripts/shared/GameTypes.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// State
Node* ui = nullptr;
Node* fade = nullptr;
Node* main_menu = nullptr;
Node* level_select = nullptr;
Node* pause_menu = nullptr;
Node* level_grid = nullptr;
Node* complete_label = nullptr;
Node* view_labels[VIEW_COUNT] = { nullptr, nullptr, nullptr };
double fade_alpha = 0.0;
double fade_from = 0.0;
double fade_to = 0.0;
double fade_length = 0.0;
double fade_progress = 1.0;
double pop_clock = 1.0;

// Look
void ApplyFade(double alpha)
{
	// Purpose: cover the screen with black at a given strength, 0 is clear and 1 is fully black.

	// Remember the strength and paint the cover with it
	fade_alpha = alpha;
	if (fade) fade->set("color", Color(0.0f, 0.0f, 0.0f, (float)alpha));
}

void ApplyPop()
{
	// Purpose: size the level complete message for its pop in, growing past its size and settling back.

	// An eased curve that overshoots a little before resting at full size
	if (!complete_label) return;
	double amount = pop_clock / 0.35;
	if (amount > 1.0) amount = 1.0;
	double back = amount - 1.0;
	double size = 1.0 + 2.7 * back * back * back + 1.7 * back * back;
	complete_label->set("scale", Vector2((float)size, (float)size));
}

// Commands
void FadeTo(double alpha, double seconds)
{
	// Purpose: let the GameManager darken or clear the screen over some time.

	// Start from the current strength and aim at the new one
	fade_from = fade_alpha;
	fade_to = alpha;
	fade_length = seconds;
	fade_progress = 0.0;

	// A fade with no duration happens at once
	if (seconds <= 0.0)
	{
		fade_progress = 1.0;
		ApplyFade(alpha);
	}
}
void ShowView(int view)
{
	// Purpose: let the GameManager light up the active view in the indicator at the top of the screen.

	// The active view is fully drawn in its own colour, the others are faded
	const Color colors[VIEW_COUNT] = { Color(0.55f, 0.68f, 1.0f), Color(1.0f, 0.72f, 0.42f), Color(0.42f, 0.9f, 0.8f) };
	for (int index = 0; index < VIEW_COUNT; index++)
	{
		if (!view_labels[index]) continue;
		Color color = colors[index];
		color.a = (index == view) ? 1.0f : 0.35f;
		view_labels[index]->set("modulate", color);
	}
}
void ShowComplete(int shown)
{
	// Purpose: let the GameManager show or hide the level complete message.
	// The parameter is a number because calls between scripts cannot carry a bool.

	// Show or hide the message, it pops in from small
	if (!complete_label) return;
	complete_label->set("visible", shown != 0);
	pop_clock = (shown != 0) ? 0.0 : 1.0;
	complete_label->set("pivot_offset", (Vector2)complete_label->get("size") * 0.5f);
	ApplyPop();
}

// Menus
void Announce(const String& signal_name)
{
	// Purpose: tell the GameManager that a menu button was pressed.
	// The signal waits for the end of the frame, because the GameManager answers by changing the menus.

	// Emit it once this frame's work is done
	ui->call_deferred("emit_signal", signal_name);
}
bool FocusFirst(Node* parent)
{
	// Purpose: put the keyboard focus on the first button that can be pressed, so a menu works with the arrow keys and Enter.
	// Returns whether a button was found.

	// Look through the children, and inside containers
	for (int index = 0; index < parent->get_child_count(); index++)
	{
		Node* child = parent->get_child(index);
		if (child->is_class("Button") && !(bool)child->get("disabled"))
		{
			((Control*)child)->call_deferred("grab_focus");
			return true;
		}
		if (FocusFirst(child)) return true;
	}
	return false;
}
void ShowOnly(Node* shown)
{
	// Purpose: show one menu and hide the others, or hide them all when given nothing.

	// Each menu is visible only when it is the one asked for
	Node* menus[3] = { main_menu, level_select, pause_menu };
	for (Node* menu : menus)
	{
		if (menu) menu->set("visible", menu == shown);
	}
	if (shown) FocusFirst(shown);
}

// Commands
void ShowMainMenu(int level_count, int finished_all)
{
	// Purpose: let the GameManager open the main menu, saying how many levels there are and whether every one has been finished.

	// Play needs at least one level, and a message tells what is going on when there are none or all are done
	ShowOnly(main_menu);
	if (!main_menu) return;
	Node* play = main_menu->get_node_or_null("Center/Box/Play");
	if (play) play->set("disabled", level_count <= 0);
	Node* message = main_menu->get_node_or_null("Center/Box/Message");
	if (!message) return;
	String text = "";
	if (level_count <= 0) text = "No levels yet: add scenes to scenes/levels/game";
	if (finished_all != 0) text = "You finished every level!";
	message->set("text", text);
	message->set("visible", text.length() > 0);
	if (play && level_count > 0) ((Control*)play)->call_deferred("grab_focus");
}
void ShowLevelSelect(int level_count, int open_count)
{
	// Purpose: let the GameManager open the level selector, with one button per level: finished ones marked, the next one open, the rest locked.

	// Empty the grid of the buttons made last time
	ShowOnly(level_select);
	if (!level_grid) return;
	while (level_grid->get_child_count() > 0)
	{
		Node* old = level_grid->get_child(0);
		level_grid->remove_child(old);
		old->queue_free();
	}

	// Make a button for each level, named after its file
	Node* levels = ui->get_node_or_null("../LevelController");
	for (int index = 0; index < level_count; index++)
	{
		Button* button = memnew(Button);
		String name = levels ? (String)levels->call("LevelName", index) : String();
		button->set_text(String::num_int64(index + 1) + "\n" + name);
		button->set_custom_minimum_size(Vector2(170.0f, 90.0f));

		// Finished levels are tinted, the next one is plain, and the ones after it cannot be pressed
		bool finished = index < open_count - 1;
		bool locked = index >= open_count;
		button->set_disabled(locked);
		if (finished) button->set_modulate(Color(0.6f, 1.0f, 0.75f, 1.0f));
		button->connect("pressed", Callable(ui, "OnLevelButton").bind(index));
		level_grid->add_child(button);
	}
	FocusFirst(level_grid);
}
void ShowPause(int shown)
{
	// Purpose: let the GameManager open or close the pause menu.
	// The parameter is a number because calls between scripts cannot carry a bool.

	// Show it alone, or hide every menu
	ShowOnly(shown != 0 ? pause_menu : nullptr);
}
void HideMenus()
{
	// Purpose: let the GameManager close every menu, to go back to playing.

	// Nothing shown
	ShowOnly(nullptr);
}

// Buttons
void OnPlayButton()
{
	// Purpose: called when the Play button is pressed.

	// Tell the GameManager
	Announce("play_pressed");
}
void OnSelectButton()
{
	// Purpose: called when a Select level button is pressed.

	// Tell the GameManager
	Announce("select_pressed");
}
void OnQuitButton()
{
	// Purpose: called when the Quit button is pressed.

	// Tell the GameManager
	Announce("quit_pressed");
}
void OnBackButton()
{
	// Purpose: called when the Back button of the selector is pressed.

	// Tell the GameManager
	Announce("back_pressed");
}
void OnResumeButton()
{
	// Purpose: called when the Resume button is pressed.

	// Tell the GameManager
	Announce("resume_pressed");
}
void OnRestartButton()
{
	// Purpose: called when the Restart button is pressed.

	// Tell the GameManager
	Announce("restart_pressed");
}
void OnMenuButton()
{
	// Purpose: called when the Main menu button is pressed.

	// Tell the GameManager
	Announce("menu_pressed");
}
void OnLevelButton(int index)
{
	// Purpose: tell the GameManager which level was chosen in the selector.

	// The level number travels with the signal, once this frame's work is done
	ui->call_deferred("emit_signal", "level_chosen", index);
}
void ConnectButton(const String& path, const String& method)
{
	// Purpose: make a button of the menus call one of this module's functions when it is pressed.

	// Find the button and connect it, if it is there
	Node* button = ui->get_node_or_null(NodePath(path));
	if (button) button->connect("pressed", Callable(ui, method));
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node and its children are in the tree, finds the pieces of the interface.

	// Find the black cover and the message
	ui = GetSelf<Node>(instance);
	fade = ui->get_node_or_null("Fade");
	complete_label = ui->get_node_or_null("CompleteLabel");

	// Find the three labels of the view indicator, in the same order as the ViewMode values
	view_labels[VIEW_SIDE] = ui->get_node_or_null("ViewBar/Side");
	view_labels[VIEW_TOP] = ui->get_node_or_null("ViewBar/Top");
	view_labels[VIEW_ISO] = ui->get_node_or_null("ViewBar/Iso");

	// Find the menus, and wire their buttons to the functions that announce them
	main_menu = ui->get_node_or_null("Menus/MainMenu");
	level_select = ui->get_node_or_null("Menus/LevelSelect");
	pause_menu = ui->get_node_or_null("Menus/PauseMenu");
	level_grid = ui->get_node_or_null("Menus/LevelSelect/Center/Box/Grid");
	ConnectButton("Menus/MainMenu/Center/Box/Play", "OnPlayButton");
	ConnectButton("Menus/MainMenu/Center/Box/Select", "OnSelectButton");
	ConnectButton("Menus/MainMenu/Center/Box/Quit", "OnQuitButton");
	ConnectButton("Menus/LevelSelect/Center/Box/Back", "OnBackButton");
	ConnectButton("Menus/PauseMenu/Center/Box/Resume", "OnResumeButton");
	ConnectButton("Menus/PauseMenu/Center/Box/Restart", "OnRestartButton");
	ConnectButton("Menus/PauseMenu/Center/Box/Select", "OnSelectButton");
	ConnectButton("Menus/PauseMenu/Center/Box/Menu", "OnMenuButton");

	// The signals the GameManager listens to, the level one carries the chosen level
	const char* signals[8] = { "play_pressed", "select_pressed", "quit_pressed", "back_pressed", "resume_pressed", "restart_pressed", "menu_pressed", "level_chosen" };
	for (int index = 0; index < 8; index++)
	{
		if (!ui->has_signal(signals[index])) ui->add_user_signal(signals[index]);
	}

	// Start with a clear screen, no message and no menu
	ApplyFade(0.0);
	ShowComplete(0);
	ShowOnly(nullptr);
}
void OnDestroy(Caller* instance)
{
	// Purpose: called when the node leaves the scene tree, drops every node reference.

	// Forget the nodes so nothing uses them after they are freed
	ui = nullptr;
	fade = nullptr;
	main_menu = nullptr;
	level_select = nullptr;
	pause_menu = nullptr;
	level_grid = nullptr;
	complete_label = nullptr;
	for (int index = 0; index < VIEW_COUNT; index++) view_labels[index] = nullptr;
	fade_progress = 1.0;
}
void OnProcess(Caller* instance, double _delta)
{
	// Purpose: called every frame, advances a fade while one is running.

	// Advance the pop in of the message while it runs
	if (pop_clock < 1.0)
	{
		pop_clock += _delta;
		ApplyPop();
	}

	// Do nothing when no fade is running
	if (fade_progress >= 1.0) return;

	// Advance by the time passed and paint the strength for the new progress
	fade_progress += _delta / fade_length;
	if (fade_progress > 1.0) fade_progress = 1.0;
	ApplyFade(fade_from + (fade_to - fade_from) * fade_progress);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
