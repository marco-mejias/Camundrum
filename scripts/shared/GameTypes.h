
/* Camundrum - Shared Game Types */

#pragma once

// View Modes
enum ViewMode
{
	VIEW_SIDE = 0,
	VIEW_TOP = 1,
	VIEW_ISO = 2,
	VIEW_COUNT = 3
};

// Death Causes
enum DeathCause
{
	DEATH_FALL = 0,
	DEATH_SPIKES = 1,
	DEATH_HOLE = 2
};

// Collision Layers
enum CollisionLayer
{
	LAYER_WORLD = 1,
	LAYER_BOARD_BOUNDS = 2,
	LAYER_ACTORS = 4,
	LAYER_RAISED = 8
};

// Sounds, in the same order as the files the AudioController loads
enum SoundId
{
	SOUND_JUMP = 0,
	SOUND_LAND = 1,
	SOUND_SWITCH = 2,
	SOUND_BLOCKED = 3,
	SOUND_LEVER = 4,
	SOUND_BUTTON = 5,
	SOUND_DOOR = 6,
	SOUND_PLATE = 7,
	SOUND_TURN = 8,
	SOUND_DEATH = 9,
	SOUND_EXIT = 10,
	SOUND_CRATE_LAND = 11,
	SOUND_UI_MOVE = 12,
	SOUND_UI_CONFIRM = 13,
	SOUND_UI_BACK = 14,
	SOUND_COUNT = 15
};

// The colour of each view, shared by the view bar, view blocks and view flags
inline void ViewColor(int view, float& r, float& g, float& b)
{
	// Purpose: give the colour that stands for a view: blue for side, orange for top, teal for iso, a warm white for any view.

	// One colour per view
	if (view == VIEW_SIDE) { r = 0.55f; g = 0.68f; b = 1.0f; return; }
	if (view == VIEW_TOP) { r = 1.0f; g = 0.72f; b = 0.42f; return; }
	if (view == VIEW_ISO) { r = 0.42f; g = 0.9f; b = 0.8f; return; }
	r = 1.0f; g = 0.96f; b = 0.85f;
}
