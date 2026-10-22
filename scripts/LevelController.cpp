
/* Camundrum - Level Controller */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/packed_scene.hpp>
#include <Godot/classes/resource.hpp>
#include <Godot/classes/resource_loader.hpp>
#include <Godot/classes/dir_access.hpp>
#include <Godot/classes/config_file.hpp>
#include <Godot/variant/packed_string_array.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/mesh.hpp>
#include <Godot/classes/mesh_instance3d.hpp>
#include <Godot/classes/box_mesh.hpp>
#include <Godot/classes/material.hpp>
#include <Godot/classes/multi_mesh.hpp>
#include <Godot/classes/multi_mesh_instance3d.hpp>

// C++ Runtime
#include <cmath>
#include <vector>

// Game
#include "scripts/shared/GameTypes.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Where the levels and the progress live
// The game's levels are the scenes in the game folder, in order of name; test levels stay in the levels folder
static const char* game_folder = "res://scenes/levels/game";
static const char* test_folder = "res://scenes/levels";
static const char* progress_file = "user://progress.cfg";

// Level lists, kept outside the script block because Jenova cannot read these types inside it
static PackedStringArray ListScenes(const String& folder)
{
	// Purpose: list every scene in a folder, sorted by name, so each one gets a stable number.

	// Read the folder, a packaged game lists scenes with an extra .remap ending
	PackedStringArray found = DirAccess::get_files_at(folder);
	PackedStringArray scenes;
	for (int index = 0; index < found.size(); index++)
	{
		String file = found[index];
		if (file.ends_with(".tscn.remap")) file = file.trim_suffix(".remap");
		if (file.ends_with(".tscn")) scenes.push_back(folder.path_join(file));
	}

	// Sort by name, so the order only changes when a level is added or renamed
	scenes.sort();
	return scenes;
}
static bool SwapLevel(Node* game, const String& path)
{
	// Purpose: take the level out of the game and put a fresh copy of another level scene in its place, named Level.

	// Load the new level and make a copy of it
	Ref<Resource> resource = ResourceLoader::get_singleton()->load(path);
	PackedScene* packed = (PackedScene*)resource.ptr();
	if (!packed) return false;
	Node* fresh = packed->instantiate();
	if (!fresh) return false;

	// The old level is freed at once, before the new one starts, so its scripts let go of their nodes first
	Node* old = game->get_node_or_null("Level");
	if (old)
	{
		game->remove_child(old);
		memdelete(old);
	}

	// Put the new level in its place under the same name
	fresh->set_name("Level");
	game->add_child(fresh);
	return true;
}

// Real holes, kept outside the script block because Jenova cannot read these types inside it
static void CutBoard(Node3D* board, const std::vector<Node3D*>& holes)
{
	// Purpose: rebuild a board as one-unit tiles, leaving out the cells that hold a hole, so the holes are real openings.
	// Each hole is moved onto the centre of its cell and told that the board around it is open.

	// The board is drawn by its Mesh child, a box whose width and height must be whole units
	MeshInstance3D* mesh = (MeshInstance3D*)board->get_node_or_null("Mesh");
	if (!mesh || holes.empty()) return;
	Ref<Mesh> shape = mesh->get_mesh();
	if (shape.is_null()) return;
	Variant size_value = shape->get("size");
	if (size_value.get_type() != Variant::VECTOR3) return;
	Vector3 size = size_value;
	int columns = (int)roundf(size.x);
	int rows = (int)roundf(size.y);
	if (columns < 1 || rows < 1 || fabsf(size.x - (float)columns) > 0.01f || fabsf(size.y - (float)rows) > 0.01f) return;

	// Find the cell of each hole, measured in the board's own space so a turned board works too
	Transform3D to_world = mesh->get_global_transform();
	Transform3D to_board = to_world.affine_inverse();
	std::vector<bool> open(columns * rows, false);
	std::vector<int> cells(holes.size(), -1);
	for (size_t index = 0; index < holes.size(); index++)
	{
		Vector3 local = to_board.xform(holes[index]->get_global_position());
		int column = (int)floorf(local.x + size.x * 0.5f);
		int row = (int)floorf(local.y + size.y * 0.5f);
		if (column < 0 || column >= columns || row < 0 || row >= rows) continue;
		open[row * columns + column] = true;
		cells[index] = row * columns + column;
	}

	// Each hole sits right at the centre of its cell, on the board's face, and learns whether board goes on above it
	for (size_t index = 0; index < holes.size(); index++)
	{
		if (cells[index] < 0) continue;
		int column = cells[index] % columns;
		int row = cells[index] / columns;
		Vector3 center = Vector3(-size.x * 0.5f + (float)column + 0.5f, -size.y * 0.5f + (float)row + 0.5f, size.z * 0.5f);
		holes[index]->set_global_position(to_world.xform(center));
		bool wall_above = row + 1 < rows && !open[(row + 1) * columns + column];
		holes[index]->call("MarkCut", wall_above ? 1 : 0);
	}

	// One tile for every cell that is not open, all drawn together, with the board's own material
	Ref<BoxMesh> tile;
	tile.instantiate();
	tile->set_size(Vector3(1.0f, 1.0f, size.z));
	tile->set("material", shape->get("material"));
	int count = 0;
	for (bool cell : open) count += cell ? 0 : 1;
	Ref<MultiMesh> tiles;
	tiles.instantiate();
	tiles->set_transform_format(MultiMesh::TRANSFORM_3D);
	tiles->set_mesh(tile);
	tiles->set_instance_count(count);
	int placed = 0;
	for (int row = 0; row < rows; row++)
	{
		for (int column = 0; column < columns; column++)
		{
			if (open[row * columns + column]) continue;
			Vector3 center = Vector3(-size.x * 0.5f + (float)column + 0.5f, -size.y * 0.5f + (float)row + 0.5f, 0.0f);
			tiles->set_instance_transform(placed, Transform3D(Basis(), center));
			placed++;
		}
	}

	// The tiles take the place of the whole board's look, its collision stays as it is
	MultiMeshInstance3D* drawn = memnew(MultiMeshInstance3D);
	drawn->set_name("Tiles");
	drawn->set_multimesh(tiles);
	board->add_child(drawn);
	drawn->set_transform(mesh->get_transform());
	mesh->set_visible(false);
}
static void CutBoards(Node* parent)
{
	// Purpose: find every board in a level, including boards on turning layers, and open the holes placed next to each one.

	// A board is a node called Board, its holes are the holes beside it under the same parent
	for (int index = 0; index < parent->get_child_count(); index++)
	{
		Node* child = parent->get_child(index);
		if (String(child->get_name()) == "Board" && child->is_class("Node3D"))
		{
			std::vector<Node3D*> holes;
			for (int other = 0; other < parent->get_child_count(); other++)
			{
				Node* sibling = parent->get_child(other);
				if (sibling->is_in_group("board_hole")) holes.push_back((Node3D*)sibling);
			}
			CutBoard((Node3D*)child, holes);
		}
		CutBoards(child);
	}
}

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// State
Node* controller = nullptr;
int unlocked = 1;
int current = -1;

// Progress
void SaveProgress()
{
	// Purpose: write how far the player has got to the save file, so it is kept when the game closes.

	// One value: how many levels are open
	Ref<ConfigFile> file;
	file.instantiate();
	file->set_value("progress", "unlocked", unlocked);
	file->save(progress_file);
}
void LoadProgress()
{
	// Purpose: read how far the player has got from the save file, starting with only the first level open.

	// Without a save file only the first level is open
	unlocked = 1;
	Ref<ConfigFile> file;
	file.instantiate();
	if (file->load(progress_file) != OK) return;
	int saved = file->get_value("progress", "unlocked", 1);
	if (saved > unlocked) unlocked = saved;
}

// Queries
double LevelCount()
{
	// Purpose: let the other modules ask how many levels the game has.
	// Numbers are returned as decimals because calls between scripts cannot return a whole number reliably.

	// Count the scenes in the game folder
	return (double)ListScenes(game_folder).size();
}
double UnlockedCount()
{
	// Purpose: let the other modules ask how many levels are open, the finished ones and the next one.

	// Never more than the levels there are
	int count = ListScenes(game_folder).size();
	return (double)(unlocked < count ? unlocked : count);
}
double CurrentLevel()
{
	// Purpose: let the other modules ask which game level is being played, or -1 for a test level or none.

	// Hand back the number of the level loaded last
	return (double)current;
}
double NextToPlay()
{
	// Purpose: let the menu ask which level "Play" should start: the first one not finished yet, or the last one if all are.

	// The last open level, within the levels there are
	int count = ListScenes(game_folder).size();
	if (count == 0) return 0.0;
	int next = unlocked - 1;
	if (next > count - 1) next = count - 1;
	return (double)next;
}
String LevelName(int index)
{
	// Purpose: let the menu ask for the name of a game level, from its file name without the leading number.

	// Take the file name, drop the extension and a leading "01_" style number
	PackedStringArray levels = ListScenes(game_folder);
	if (index < 0 || index >= levels.size()) return String();
	String name = levels[index].get_file().get_basename();
	int cut = name.find("_");
	if (cut > 0 && name.substr(0, cut).is_valid_int()) name = name.substr(cut + 1);
	return name.replace("_", " ");
}

// Commands
bool LoadGameLevel(int index)
{
	// Purpose: let the GameManager load a game level by its number in the game folder.

	// Ignore numbers that do not exist
	PackedStringArray levels = ListScenes(game_folder);
	if (index < 0 || index >= levels.size()) return false;
	if (!SwapLevel(controller->get_parent(), levels[index])) return false;
	current = index;
	Output("Playing game level %d: %s", index + 1, GetCStr(levels[index].get_file().get_basename()));
	return true;
}
bool LoadTestLevel(int number)
{
	// Purpose: let the GameManager load a test level by its number in the levels folder, the level_number setting on the Game node.

	// Print the numbered list, so the right number can be looked up in the output panel
	PackedStringArray levels = ListScenes(test_folder);
	for (int index = 0; index < levels.size(); index++) Output("Level %d: %s", index, GetCStr(levels[index].get_file().get_basename()));

	// Ignore numbers that do not exist
	if (number < 0 || number >= levels.size())
	{
		Output("Level %d does not exist, playing the level placed in the scene", number);
		return false;
	}
	if (!SwapLevel(controller->get_parent(), levels[number])) return false;
	current = -1;
	Output("Playing level %d: %s", number, GetCStr(levels[number].get_file().get_basename()));
	return true;
}
void CompleteCurrent()
{
	// Purpose: let the GameManager record that the current game level was finished, which opens the next one.

	// Test levels do not count towards progress
	if (current < 0) return;
	if (current + 2 > unlocked) unlocked = current + 2;
	SaveProgress();
}

void PrepareLevel(Node* level)
{
	// Purpose: let the GameManager get a level ready once it is in the game: its boards are opened where holes were placed.

	// Open the holes in every board
	if (level) CutBoards(level);
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node is in the tree, reads the saved progress.

	// Keep this node, its parent is the Game node where levels are placed
	controller = GetSelf<Node>(instance);
	LoadProgress();
}
void OnDestroy(Caller* instance)
{
	// Purpose: called when the node leaves the scene tree, drops the node reference.

	// Forget it so nothing uses it after it is freed
	controller = nullptr;
}

// Jenova Script Block End
JENOVA_SCRIPT_END
