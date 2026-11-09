#pragma once

#include <cstdint>

// Audio listeners for four players. The sound code holds one listener (a position and a facing, 0x20 bytes) per player in arrays of two: the array Gm_Level_Update
// fills each frame (a stack buffer), the copies Tasks_Update makes into the audio manager (+0x20 and +0x40, with other fields right behind at +0x60), and the per-player
// volume pairs of SoundTask_Update (a stack buffer whose third pair is its scratch). apply_p10 points each reader and writer at the ranges below (p9_edits_audio.py).
// The manager keeps its first two listeners where the original does, so a snapshot made before this exists loads and a one or two player game runs the original code;
// only listeners 2 and 3 live in extra RAM (zero until the first frame with a third player). The range is 0x02210000 to 0x0221FFFF of the extra RAM.
namespace warriors::p9 {

constexpr std::uint32_t kAudioBase = 0x02210000;
constexpr std::uint32_t kLevelListeners = kAudioBase;               // Gm_Level_Update's listener array (was its stack buffer): four entries, passed to AudioManager_Update and HUD_Update
constexpr std::uint32_t kManagerListeners = kAudioBase + 0x100;     // the audio manager's listeners 2 and 3 (0 and 1 are at manager +0x20 and +0x40)
constexpr std::uint32_t kSoundPairs = kAudioBase + 0x200;           // SoundTask_Update's left/right volume of each player (was its stack buffer): four pairs of floats

constexpr std::uint32_t kListenerBytes = 0x20;

// The listener that follows `pointer` when a loop walks the manager's listeners (Listeners_NearestDistance, SoundTask_Update): the first one past the manager's
// second is listener 2 in extra RAM. A one or two player game gets pointer + 0x20 as before.
std::uint32_t listener_step(std::uint32_t pointer);

// Right after Tasks_Update copied the two listeners of the original: copies listeners 2 and 3 into the manager's extra entries. `source_end` is the caller's array
// after its two entries. Gm_Level_Update's array holds four; every other caller supplies two, so its last entry stands for the rest. Does nothing for two players or fewer.
void copy_extra_listeners(std::uint32_t source_end);

// AmbientManager_UpdateEmitters keeps a "this player can hear the emitter" flag per player in a four word stack array it fills for two: players 3 and 4 start set,
// and an emitter is heard when any flag is still set.
void ambient_extra_flags(std::uint32_t flags);
bool ambient_extra_heard(std::uint32_t flags);

}
