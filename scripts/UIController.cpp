
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
#include <Godot/classes/label.hpp>
#include <Godot/classes/range.hpp>
#include <Godot/classes/h_box_container.hpp>
#include <Godot/classes/tween.hpp>
#include <Godot/classes/property_tweener.hpp>

// C++ Runtime
#include <cmath>

// Game
#include "scripts/shared/GameTypes.h"
#include "scripts/shared/ElementHelpers.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// The interface: the view bar, the fades, the level complete message, the menus with their animations and sounds, and the options screen.
// Menus only announce what was pressed; the GameManager decides what happens. The options screen sends every change as setting_changed(key, value).

// State
Node* ui = nullptr;
Node* fade = nullptr;
Node* main_menu = nullptr;
Node* level_select = nullptr;
Node* pause_menu = nullptr;
Node* options_menu = nullptr;
Node* level_grid = nullptr;
Node* complete_label = nullptr;
Node* title_letters = nullptr;
Node* level_title = nullptr;
Node* options_return = nullptr;
Node* view_labels[VIEW_COUNT] = { nullptr, nullptr, nullptr };
double fade_alpha = 0.0;
double fade_from = 0.0;
double fade_to = 0.0;
double fade_length = 0.0;
double fade_progress = 1.0;
double pop_clock = 1.0;
double menu_clock = 0.0;
double quiet_left = 0.0;
double tick_left = 0.0;

// The colour of each view, brighter for the interface
Color ViewTint(int view)
{
	// Purpose: the colour the interface uses for a view: blue for side, red for top, green for iso.

	// The same order as the ViewMode values
	if (view == VIEW_TOP) return Color(1.0f, 0.45f, 0.4f);
	if (view == VIEW_ISO) return Color(0.42f, 0.95f, 0.8f);
	return Color(0.55f, 0.7f, 1.0f);
}

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

// Animation
void CenterPivot(Control* control)
{
	// Purpose: make a control grow and shrink around its middle.

	// Its pivot sits at half its size
	if (control) control->set_pivot_offset(control->get_size() * 0.5f);
}
void TweenScale(Control* control, float scale, double seconds)
{
	// Purpose: ease a control to a size, springing slightly past it, replacing any size change still running on it.

	// Stop the last one, then start the new one around the control's middle
	if (!control) return;
	if (control->has_meta("scale_tween"))
	{
		Ref<Tween> running = control->get_meta("scale_tween");
		if (running.is_valid()) running->kill();
	}
	CenterPivot(control);
	Ref<Tween> tween = control->create_tween();
	tween->tween_property(control, "scale", Vector2(scale, scale), seconds)->set_trans(Tween::TRANS_BACK)->set_ease(Tween::EASE_OUT);
	control->set_meta("scale_tween", tween);
}
void CenterPivots(NodePath path)
{
	// Purpose: centre the pivot of every piece of a box once it has been laid out, so pieces grow around their middle.

	// Each direct child
	Node* box = ui ? ui->get_node_or_null(path) : nullptr;
	if (!box) return;
	for (int index = 0; index < box->get_child_count(); index++) CenterPivot(Object::cast_to<Control>(box->get_child(index)));
}
void AnimateEntries(Node* box)
{
	// Purpose: bring the pieces of a menu in one after another: each fades in and grows into place.

	// Every visible piece of the box, in order, a little later than the one before
	if (!box) return;
	int order = 0;
	for (int index = 0; index < box->get_child_count(); index++)
	{
		Control* piece = Object::cast_to<Control>(box->get_child(index));
		if (!piece || !piece->is_visible()) continue;
		double delay = 0.04 + 0.05 * order;
		order++;
		Color start = piece->get_modulate();
		Color shown = start;
		start.a = 0.0f;
		shown.a = (String(piece->get_name()) == "Subtitle") ? 0.7f : 1.0f;
		piece->set_modulate(start);
		piece->set_scale(Vector2(0.86f, 0.86f));
		Ref<Tween> tween = piece->create_tween();
		tween->set_parallel(true);
		tween->tween_property(piece, "modulate", shown, 0.22)->set_delay(delay);
		tween->tween_property(piece, "scale", Vector2(1.0f, 1.0f), 0.32)->set_delay(delay)->set_trans(Tween::TRANS_BACK)->set_ease(Tween::EASE_OUT);
	}

	// The pivots are only right once the box has laid its pieces out, at the end of this frame
	ui->call_deferred("CenterPivots", box->get_path());
}
void DropTitle()
{
	// Purpose: drop the letters of the title in one after another, each popping into place.

	// Every letter, a little later than the one before
	if (!title_letters) return;
	for (int index = 0; index < title_letters->get_child_count(); index++)
	{
		Control* letter = Object::cast_to<Control>(title_letters->get_child(index));
		if (!letter) continue;
		letter->set_scale(Vector2(0.0f, 0.0f));
		Ref<Tween> tween = letter->create_tween();
		tween->tween_property(letter, "scale", Vector2(1.0f, 1.0f), 0.45)->set_delay(0.05 * index)->set_trans(Tween::TRANS_BACK)->set_ease(Tween::EASE_OUT);
	}
	ui->call_deferred("CenterPivots", title_letters->get_path());
}
void WaveTitle(double delta)
{
	// Purpose: keep the title alive while the main menu shows: the letters bob in a wave and their colours drift through the three views.

	// Only while the main menu is on screen
	if (!title_letters || !main_menu || !(bool)main_menu->get("visible")) return;
	for (int index = 0; index < title_letters->get_child_count(); index++)
	{
		Control* letter = Object::cast_to<Control>(title_letters->get_child(index));
		if (!letter) continue;

		// Up and down, each letter a little behind the one before
		Vector2 at = letter->get_position();
		at.y = (float)(7.0 * sin(menu_clock * 2.6 - index * 0.55));
		letter->set_position(at);

		// Blue, red and green flowing along the word
		double wave = fmod(menu_clock * 0.35 + index * 0.09, 3.0);
		int from = (int)wave;
		float blend = (float)(wave - from);
		Color tint = ViewTint(from % 3).lerp(ViewTint((from + 1) % 3), blend);
		letter->set_modulate(Color(tint.r, tint.g, tint.b, letter->get_modulate().a));
	}
}

// Sounds
void PlayUi(int sound)
{
	// Purpose: play a menu sound, except right after a menu opens, when the first button takes the focus by itself.

	// Quiet for a moment after opening
	if (sound == SOUND_UI_MOVE && quiet_left > 0.0) return;
	if (ui) PlaySound(ui, sound);
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
	for (int index = 0; index < VIEW_COUNT; index++)
	{
		if (!view_labels[index]) continue;
		Color color = ViewTint(index);
		color.a = (index == view) ? 1.0f : 0.35f;
		view_labels[index]->set("modulate", color);
	}

	// The active one gives a little hop
	Control* active = (view >= 0 && view < VIEW_COUNT) ? Object::cast_to<Control>(view_labels[view]) : nullptr;
	if (active)
	{
		CenterPivot(active);
		active->set_scale(Vector2(1.35f, 1.35f));
		TweenScale(active, 1.0f, 0.3);
	}
}
void ShowLevelTitle(String text)
{
	// Purpose: let the GameManager show the level's name when it starts: it grows in under the view bar and fades out after a moment.

	// The text, starting see-through and a little small
	Label* title = Object::cast_to<Label>(level_title);
	if (!title) return;
	title->set_text(text);
	title->set_visible(true);
	title->set_modulate(Color(1.0f, 1.0f, 1.0f, 0.0f));
	CenterPivot(title);
	title->set_scale(Vector2(0.8f, 0.8f));

	// In, wait, out
	if (title->has_meta("title_tween"))
	{
		Ref<Tween> running = title->get_meta("title_tween");
		if (running.is_valid()) running->kill();
	}
	Ref<Tween> tween = title->create_tween();
	tween->set_parallel(true);
	tween->tween_property(title, "modulate", Color(1.0f, 1.0f, 1.0f, 1.0f), 0.3)->set_delay(0.15);
	tween->tween_property(title, "scale", Vector2(1.0f, 1.0f), 0.45)->set_delay(0.15)->set_trans(Tween::TRANS_BACK)->set_ease(Tween::EASE_OUT);
	tween->tween_property(title, "modulate", Color(1.0f, 1.0f, 1.0f, 0.0f), 0.5)->set_delay(1.9);
	title->set_meta("title_tween", tween);
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
	// Purpose: tell the GameManager that a menu button was pressed, with the sound that goes with it.
	// The signal waits for the end of the frame, because the GameManager answers by changing the menus.

	// Going back sounds different from going forward
	bool back = signal_name == "back_pressed" || signal_name == "resume_pressed" || signal_name == "menu_pressed";
	PlayUi(back ? SOUND_UI_BACK : SOUND_UI_CONFIRM);
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
		if ((child->is_class("Button") || child->is_class("HSlider")) && !(bool)child->get("disabled"))
		{
			((Control*)child)->call_deferred("grab_focus");
			return true;
		}
		if (FocusFirst(child)) return true;
	}
	return false;
}
Node* BoxOf(Node* menu)
{
	// Purpose: the column of pieces inside a menu.

	// Every menu keeps them in Center/Box
	return menu ? menu->get_node_or_null("Center/Box") : nullptr;
}
void ShowOnly(Node* shown)
{
	// Purpose: show one menu and hide the others, or hide them all when given nothing; the menu shown fades in and its pieces come in one by one.

	// Each menu is visible only when it is the one asked for
	Node* menus[4] = { main_menu, level_select, pause_menu, options_menu };
	for (Node* menu : menus)
	{
		if (menu) menu->set("visible", menu == shown);
	}
	if (!shown) return;

	// Fade the backdrop in and bring the pieces in, quietly, then give the first one the focus
	Control* backdrop = Object::cast_to<Control>(shown);
	if (backdrop)
	{
		backdrop->set_modulate(Color(1.0f, 1.0f, 1.0f, 0.0f));
		Ref<Tween> tween = backdrop->create_tween();
		tween->tween_property(backdrop, "modulate", Color(1.0f, 1.0f, 1.0f, 1.0f), 0.2);
	}
	AnimateEntries(BoxOf(shown));
	if (shown == main_menu) DropTitle();
	quiet_left = 0.25;
	FocusFirst(shown);
}
void ConnectFeel(Control* control)
{
	// Purpose: give a button or slider its feel: the mouse over it takes the focus, and the focus makes it grow and tick.

	// Mouse and focus both lead to the same reaction
	if (!control) return;
	control->connect("mouse_entered", Callable(ui, "OnPieceHovered").bind(control->get_path()));
	control->connect("focus_entered", Callable(ui, "OnPieceFocused").bind(control->get_path()));
	control->connect("focus_exited", Callable(ui, "OnPieceUnfocused").bind(control->get_path()));
}
void ConnectFeelUnder(Node* parent)
{
	// Purpose: give every button and slider under a node its feel.

	// Look through the children, and inside containers
	for (int index = 0; index < parent->get_child_count(); index++)
	{
		Node* child = parent->get_child(index);
		if (child->is_class("Button") || child->is_class("HSlider")) ConnectFeel(Object::cast_to<Control>(child));
		ConnectFeelUnder(child);
	}
}

// Commands
void ShowMainMenu(int level_count, int finished_all)
{
	// Purpose: let the GameManager open the main menu, saying how many levels there are and whether every one has been finished.

	// Play needs at least one level, and a message tells what is going on when there are none or all are done
	if (!main_menu) return;
	Node* play = main_menu->get_node_or_null("Center/Box/Play");
	if (play) play->set("disabled", level_count <= 0);
	Node* message = main_menu->get_node_or_null("Center/Box/Message");
	if (message)
	{
		String text = "";
		if (level_count <= 0) text = "No levels yet: add scenes to scenes/levels/game";
		if (finished_all != 0) text = "You finished every level!";
		message->set("text", text);
		message->set("visible", text.length() > 0);
	}
	ShowOnly(main_menu);
}
void ShowLevelSelect(int level_count, int open_count)
{
	// Purpose: let the GameManager open the level selector, with one button per level: finished ones marked, the next one open, the rest locked.

	// Empty the grid of the buttons made last time
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
		bool finished = index < open_count - 1;
		bool locked = index >= open_count;
		String mark = finished ? String("  ") + String::chr(0x2713) : String();
		button->set_text(String::num_int64(index + 1) + mark + "\n" + name);
		button->set_custom_minimum_size(Vector2(170.0f, 90.0f));

		// Finished levels are tinted, the next one is plain, and the ones after it cannot be pressed
		button->set_disabled(locked);
		if (finished) button->set_modulate(Color(0.6f, 1.0f, 0.75f, 1.0f));
		button->connect("pressed", Callable(ui, "OnLevelButton").bind(index));
		level_grid->add_child(button);
		ConnectFeel(button);
	}
	ShowOnly(level_select);
	AnimateEntries(level_grid);
	FocusFirst(level_grid);
}
void ShowPause(int shown)
{
	// Purpose: let the GameManager open or close the pause menu.
	// The parameter is a number because calls between scripts cannot carry a bool.

	// Show it alone, or hide every menu
	ShowOnly(shown != 0 ? pause_menu : nullptr);
}
void ShowOptions(double master, double effects, double interface_volume, double shake, int fullscreen)
{
	// Purpose: let the GameManager open the options screen with the player's current settings.
	// It comes back to the menu it was opened from, which the interface remembers by itself.

	// Put each control on its value without announcing a change
	if (!options_menu) return;
	const char* names[4] = { "Master", "Effects", "Interface", "Shake" };
	double values[4] = { master, effects, interface_volume, shake };
	for (int index = 0; index < 4; index++)
	{
		Range* slider = Object::cast_to<Range>(options_menu->get_node_or_null(NodePath(String("Center/Box/Rows/") + names[index])));
		if (slider) slider->set_value_no_signal(values[index]);
	}
	Button* toggle = Object::cast_to<Button>(options_menu->get_node_or_null("Center/Box/Rows/Fullscreen"));
	if (toggle) toggle->set_pressed_no_signal(fullscreen != 0);
	ShowOnly(options_menu);
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
void OnOptionsButton()
{
	// Purpose: called when an Options button is pressed, in the main menu or the pause menu.

	// Remember where to come back to, and ask the GameManager for the current settings
	options_return = (pause_menu && (bool)pause_menu->get("visible")) ? pause_menu : main_menu;
	Announce("options_pressed");
}
void OnOptionsBack()
{
	// Purpose: called when the Back button of the options screen is pressed.

	// Straight back to the menu it was opened from, nothing else changes
	PlayUi(SOUND_UI_BACK);
	ShowOnly(options_return ? options_return : main_menu);
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
	PlayUi(SOUND_UI_CONFIRM);
	ui->call_deferred("emit_signal", "level_chosen", index);
}
void OnSliderChanged(double value, String key)
{
	// Purpose: called when an options slider moves: the change goes to the GameManager, with a soft tick so the volume can be heard.

	// Announce it at the end of the frame, and tick at most a dozen times a second
	ui->call_deferred("emit_signal", "setting_changed", key, value);
	if (tick_left > 0.0) return;
	tick_left = 0.08;
	PlaySound(ui, key == "effects" ? SOUND_LAND : SOUND_UI_MOVE);
}
void OnFullscreenPressed()
{
	// Purpose: called when the fullscreen switch is flipped.

	// Its new state goes to the GameManager as a number
	Button* toggle = options_menu ? Object::cast_to<Button>(options_menu->get_node_or_null("Center/Box/Rows/Fullscreen")) : nullptr;
	if (!toggle) return;
	PlayUi(SOUND_UI_CONFIRM);
	ui->call_deferred("emit_signal", "setting_changed", "fullscreen", toggle->is_pressed() ? 1.0 : 0.0);
}

// Feel
void OnPieceHovered(NodePath path)
{
	// Purpose: called when the mouse goes over a button or slider: it takes the focus, so mouse and keys share one highlight.

	// Only pieces that can be used
	Control* piece = ui ? Object::cast_to<Control>(ui->get_node_or_null(path)) : nullptr;
	if (piece && !(bool)piece->get("disabled")) piece->grab_focus();
}
void OnPieceFocused(NodePath path)
{
	// Purpose: called when a button or slider takes the focus: it grows a little and ticks.

	// Grow and tick
	Control* piece = ui ? Object::cast_to<Control>(ui->get_node_or_null(path)) : nullptr;
	if (!piece) return;
	TweenScale(piece, piece->is_class("HSlider") ? 1.03f : 1.07f, 0.16);
	PlayUi(SOUND_UI_MOVE);
}
void OnPieceUnfocused(NodePath path)
{
	// Purpose: called when a button or slider loses the focus: it settles back to its size.

	// Back to normal
	Control* piece = ui ? Object::cast_to<Control>(ui->get_node_or_null(path)) : nullptr;
	if (piece) TweenScale(piece, 1.0f, 0.14);
}
void ConnectButton(const String& path, const String& method)
{
	// Purpose: make a button of the menus call one of this module's functions when it is pressed.

	// Find the button and connect it, if it is there
	Node* button = ui->get_node_or_null(NodePath(path));
	if (button) button->connect("pressed", Callable(ui, method));
}
void ConnectSlider(const String& name, const String& key)
{
	// Purpose: make an options slider report its changes under a setting's name.

	// Find it in the options screen and connect it
	Node* slider = options_menu ? options_menu->get_node_or_null(NodePath(String("Center/Box/Rows/") + name)) : nullptr;
	if (slider) slider->connect("value_changed", Callable(ui, "OnSliderChanged").bind(key));
}
void BuildTitle()
{
	// Purpose: split the main menu title into one label per letter, so the letters can drop in and wave on their own.

	// The title label gives the text and the size, and is then hidden
	Label* title = main_menu ? Object::cast_to<Label>(main_menu->get_node_or_null("Center/Box/Title")) : nullptr;
	if (!title) return;
	HBoxContainer* row = memnew(HBoxContainer);
	row->set_name("Letters");
	row->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	row->add_theme_constant_override("separation", 2);
	String text = title->get_text();
	for (int index = 0; index < text.length(); index++)
	{
		Label* letter = memnew(Label);
		letter->set_text(text.substr(index, 1));
		letter->add_theme_font_size_override("font_size", 76);
		row->add_child(letter);
	}
	Node* box = title->get_parent();
	box->add_child(row);
	box->move_child(row, title->get_index());
	title->set_visible(false);
	title_letters = row;
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node and its children are in the tree, finds the pieces of the interface and gives them their feel.

	// Find the black cover and the messages
	ui = GetSelf<Node>(instance);
	fade = ui->get_node_or_null("Fade");
	complete_label = ui->get_node_or_null("CompleteLabel");
	level_title = ui->get_node_or_null("LevelTitle");

	// Find the three labels of the view indicator, in the same order as the ViewMode values
	view_labels[VIEW_SIDE] = ui->get_node_or_null("ViewBar/Side");
	view_labels[VIEW_TOP] = ui->get_node_or_null("ViewBar/Top");
	view_labels[VIEW_ISO] = ui->get_node_or_null("ViewBar/Iso");

	// Find the menus, and wire their buttons to the functions that announce them
	main_menu = ui->get_node_or_null("Menus/MainMenu");
	level_select = ui->get_node_or_null("Menus/LevelSelect");
	pause_menu = ui->get_node_or_null("Menus/PauseMenu");
	options_menu = ui->get_node_or_null("Menus/OptionsMenu");
	level_grid = ui->get_node_or_null("Menus/LevelSelect/Center/Box/Grid");
	ConnectButton("Menus/MainMenu/Center/Box/Play", "OnPlayButton");
	ConnectButton("Menus/MainMenu/Center/Box/Select", "OnSelectButton");
	ConnectButton("Menus/MainMenu/Center/Box/Options", "OnOptionsButton");
	ConnectButton("Menus/MainMenu/Center/Box/Quit", "OnQuitButton");
	ConnectButton("Menus/LevelSelect/Center/Box/Back", "OnBackButton");
	ConnectButton("Menus/PauseMenu/Center/Box/Resume", "OnResumeButton");
	ConnectButton("Menus/PauseMenu/Center/Box/Restart", "OnRestartButton");
	ConnectButton("Menus/PauseMenu/Center/Box/Select", "OnSelectButton");
	ConnectButton("Menus/PauseMenu/Center/Box/Options", "OnOptionsButton");
	ConnectButton("Menus/PauseMenu/Center/Box/Menu", "OnMenuButton");
	ConnectButton("Menus/OptionsMenu/Center/Box/Back", "OnOptionsBack");
	ConnectButton("Menus/OptionsMenu/Center/Box/Rows/Fullscreen", "OnFullscreenPressed");
	ConnectSlider("Master", "master");
	ConnectSlider("Effects", "effects");
	ConnectSlider("Interface", "interface");
	ConnectSlider("Shake", "shake");

	// Every button and slider grows and ticks when it is reached, and the title gets its own letters
	Node* menus = ui->get_node_or_null("Menus");
	if (menus) ConnectFeelUnder(menus);
	BuildTitle();

	// The signals the GameManager listens to: the level one carries the chosen level, the setting one a name and a value
	const char* signals[10] = { "play_pressed", "select_pressed", "quit_pressed", "back_pressed", "resume_pressed", "restart_pressed", "menu_pressed", "level_chosen", "options_pressed", "setting_changed" };
	for (int index = 0; index < 10; index++)
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
	options_menu = nullptr;
	level_grid = nullptr;
	complete_label = nullptr;
	title_letters = nullptr;
	level_title = nullptr;
	options_return = nullptr;
	for (int index = 0; index < VIEW_COUNT; index++) view_labels[index] = nullptr;
	fade_progress = 1.0;
}
void OnProcess(Caller* instance, double _delta)
{
	// Purpose: called every frame, keeps the menus alive and advances a fade while one is running.

	// Clocks for the title wave and the sound pauses
	menu_clock += _delta;
	if (menu_clock > 10000.0) menu_clock -= 10000.0;
	if (quiet_left > 0.0) quiet_left -= _delta;
	if (tick_left > 0.0) tick_left -= _delta;
	WaveTitle(_delta);

	// Advance the pop in of the message while it runs
	if (pop_clock < 1.0)
	{
		pop_clock += _delta;
		ApplyPop();
	}

	// Do nothing more when no fade is running
	if (fade_progress >= 1.0) return;

	// Advance by the time passed and paint the strength for the new progress
	fade_progress += _delta / fade_length;
	if (fade_progress > 1.0) fade_progress = 1.0;
	ApplyFade(fade_from + (fade_to - fade_from) * fade_progress);
}

// Jenova Script Block End
JENOVA_SCRIPT_END
