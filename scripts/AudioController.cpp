
/* Camundrum - Audio Controller */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/audio_stream.hpp>
#include <Godot/classes/audio_stream_player.hpp>
#include <Godot/classes/resource_loader.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/variant/utility_functions.hpp>

// Game
#include "scripts/shared/GameTypes.h"

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Properties
JENOVA_PROPERTY(double, sound_volume, 0.0)

// State
AudioStreamPlayer* players[SOUND_COUNT] = { nullptr };

// Commands
void Play(int sound)
{
	// Purpose: let any module play one of the game's sounds, given by its SoundId.

	// Ignore sounds that do not exist or failed to load
	if (sound < 0 || sound >= SOUND_COUNT || !players[sound]) return;

	// Vary the pitch a little each time, so repeated sounds do not feel mechanical
	players[sound]->set_pitch_scale((float)UtilityFunctions::randf_range(0.94, 1.06));
	players[sound]->play();
}

// Routines
void OnReady(Caller* instance)
{
	// Purpose: called once the node is in the tree, creates one player per sound so other modules can find this one by its group.

	// Other modules look for the node in the "audio" group
	auto controller = GetSelf<Node>(instance);
	controller->add_to_group("audio");

	// The file and loudness of each sound, in the same order as the SoundId values
	const char* files[SOUND_COUNT] = { "jump", "land", "switch", "blocked", "lever", "button", "door", "plate", "turn", "death", "exit", "crate_land" };
	const float volumes[SOUND_COUNT] = { -8.0f, -10.0f, -6.0f, -6.0f, -4.0f, -4.0f, -6.0f, -6.0f, -5.0f, -3.0f, -4.0f, -8.0f };

	// Make a player for each, a few copies of a sound may overlap
	for (int sound = 0; sound < SOUND_COUNT; sound++)
	{
		Ref<AudioStream> stream = ResourceLoader::get_singleton()->load(String("res://audio/") + files[sound] + ".wav");
		if (stream.is_null()) continue;
		AudioStreamPlayer* player = memnew(AudioStreamPlayer);
		player->set_name(files[sound]);
		player->set_stream(stream);
		player->set_volume_db(volumes[sound] + (float)sound_volume);
		player->set_max_polyphony(3);
		controller->add_child(player);
		players[sound] = player;
	}
}
void OnDestroy(Caller* instance)
{
	// Purpose: called when the node leaves the scene tree, drops the players, which are freed with it.

	// Forget them so nothing plays a freed node
	for (int sound = 0; sound < SOUND_COUNT; sound++) players[sound] = nullptr;
}

// Jenova Script Block End
JENOVA_SCRIPT_END
