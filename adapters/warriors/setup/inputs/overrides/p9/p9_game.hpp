#pragma once

#include "ps2/generated.hpp"
#include "ps2/memory.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Phase 9: genuine players 3 and 4. Addresses of the game structures the hooks extend from two players to four, with the decompiled function each
// comes from (decomp\names.tsv), and the guest RAM the relocated storage lives in.
namespace warriors::p9 {

constexpr std::uint32_t kAny = 0xFFFFFFFFu;
constexpr int kMaxPlayers = 4;
constexpr int kOriginalPlayers = 2;

// W_GameState: heap block at *0x0051489c.
constexpr std::uint32_t kGameStatePointer = 0x0051489C;
constexpr std::uint32_t kPlayerCountOffset = 0x224;      // s16
constexpr std::uint32_t kPlayerHandlesOffset = 0x228;    // four handles after the move of the tick counter
constexpr std::uint32_t kTickOffset = 0x230;             // GameState_Tick's counter: moved to kTick so the handle list can grow in place
constexpr std::uint32_t kPlayerStatesOffset = 0x168, kPlayerStateSize = 0x5C;   // Human_GetPlayerState: per HUD panel
constexpr std::uint32_t kInventoryOffset = 0x480;        // Inventory object: live blocks, checkpoint blocks, callback names
constexpr std::uint32_t kThrowArcsOffset = 0x5740, kThrowArcSize = 0x30;

// Human fields.
constexpr std::uint32_t kPlayerSlotOffset = 0x1B0;       // s8, -1 for non-players
constexpr std::uint32_t kPanelOffset = 0x380;            // s32, the HUD panel (and inventory and player state slot) of a player, -1 otherwise

// Inventory (player-state.md): blocks of 0x3f4 bytes, entries of 0x2c from block +4, 23 items.
constexpr std::uint32_t kInvBlockSize = 0x3F4, kInvEntrySize = 0x2C, kInvItems = 23;
constexpr std::uint32_t kInvCountAt = 0x24, kInvSoundAt = 0x28, kInvDurationAt = 0x2C;   // from the block start (entry +0x20, +0x24, +0x28 of an entry that starts at +4)
constexpr std::uint32_t kInvCheckpointShift = 0x7E8;     // the checkpoint copy of live block k is at +0x7e8 + k*0x3f4 relative to the object

// Guest RAM the phase 9 hooks own: the runtime's extra RAM above the PS2's 32 MB (ps2::kExtraBase, runtime_p9 memory.hpp). It is plain RAM to the game, zero until a
// hook writes, and part of every state file, digest and netplay snapshot. Fixed layout, no allocator: each hook file owns a named range.
constexpr std::uint32_t kBase = 0x02000000;
constexpr std::uint32_t kMagic = kBase + 0x00;           // kMagicValue once the area is set up
constexpr std::uint32_t kTick = kBase + 0x04;
constexpr std::uint32_t kField234 = kBase + 0x08;        // the word after the tick counter, written once, never read
constexpr std::uint32_t kThrowArcs = kBase + 0x100;      // four arcs; arcs 0 and 1 and the three words above are the original's, moved; all from kExtraStart on is players 3 and 4's
constexpr std::uint32_t kExtraStart = kBase + 0x100 + 2 * kThrowArcSize;
constexpr std::uint32_t kPlayerStates = kBase + 0x200;   // panels 2 and 3
constexpr std::uint32_t kInventoryLive = kBase + 0x400;  // slots 2 and 3
constexpr std::uint32_t kInventoryCheckpoint = kBase + 0xC00;
constexpr std::uint32_t kMagicValue = 0x50393031;
constexpr std::uint32_t kCarryMask = kBase + 0x1C;       // bit per pad record (1 and 2): the extra players who are in the game, kept across level changes (below kExtraStart: a new game state does not clear it)
constexpr std::uint32_t kBrainSpot = kBase + 0x3000;     // per brain (60): the 'has seen player' and sticky bytes of panels 2 and 3 (p9_brain.cpp), 4 bytes each
constexpr std::uint32_t kStatsBlocks = kBase + 0x2000;   // PlayerStats records (0xc0 bytes) of panels 2 and 3 (p9_stats.cpp), then their checkpoint copies at +0x180


// The HUD (hud.md): one static object whose per-player arrays hold two entries each. Players 3 and 4 keep theirs in a second object of the same layout, built by the
// game's own HUD_Construct (HUD2), so any address `hud + array + index * stride` reaches them with the index folded to 0 or 1 and the difference of the two objects added.
constexpr std::uint32_t kHud = 0x00600840, kHudSize = 0x22600;
constexpr std::uint32_t kHudConstruct = 0x001ACEE0;
constexpr std::uint32_t kHudPanelPointers = 8;           // offset of the PlayerHUD pointer array (four entries once the CrewStatus widget that sat at +0x10 has moved)
constexpr std::uint32_t kCrewOffset = 0x10, kCrewBytes = 0x15A0;   // the CrewStatus widget: +0x10 to the HudToggle at +0x15b0 (its children end at +0x1300, its message text is at +0x13c0)
constexpr std::uint32_t kHud2 = kBase + 0x10000;         // HUD2: 0x22600 bytes, players 3 and 4 as entries 0 and 1 of every per-player array
constexpr std::uint32_t kHud2Delta = kHud2 - kHud;
constexpr std::uint32_t kCrew = kBase + 0x40000;         // the CrewStatus widget once HUD2 exists
constexpr std::uint32_t kTableFirst = 0x0050F900, kTableBytes = 0x800;   // PlayerHUD's layout tables: bases, 0x240 byte entries, tally marks, counter slots
constexpr std::uint32_t kTableCopy = kBase + 0x42000;    // players 3 and 4's copy (entries 0 and 1 stand for panels 2 and 3)
constexpr std::uint32_t kTableDelta = kTableCopy - kTableFirst;
constexpr std::uint32_t kHudState = kBase + 0x0C;        // kHudReady once HUD2 is built (outside the area that a new game state clears)
constexpr std::uint32_t kHudReady = 0x48554432;

// Pad records (0x50 bytes, the game's view of a pad; index = port * 4 + slot) and the libpad port records (0x140 bytes) PadPort_Step fills.
constexpr std::uint32_t kPortRecords = 0x005DE3C0, kPortRecordStride = 0x140;
constexpr std::uint32_t kPortSlotOffset = 0x108, kPortReadyOffset = 0x10C;
constexpr std::uint32_t kPortSmallMotorOffset = 0x122, kPortLargeMotorOffset = 0x123;   // Pad_SetVibration's two bytes: the small motor on or off, the large one's strength
constexpr std::uint32_t kPortLeftXOffset = 0x110, kPortButtonsOffset = 0x114, kPortPressureOffset = 0x116;
constexpr int kExtraPadRecords = 2;                      // records 1 and 2 (port 0 slots 1 and 2) are the runtime's pads 3 and 4
constexpr int kFirstExtraPad = 2;                        // the runtime's pad index of record 1

// PS2_P9_TRACE=1 prints the hooks' decisions to stderr (join, panels, HUD2) for run logs.
inline bool tracing() {
    static const bool on = std::getenv("PS2_P9_TRACE") != nullptr;
    return on;
}
#define P9_TRACE(format, ...) do { if (::warriors::p9::tracing()) std::fprintf(stderr, format "\n", ##__VA_ARGS__); } while (0)

// PS2_P9_OFF=name,name turns hooks off for bisecting (place, layout, recruit).
inline bool hook_off(const char *name) {
    static const char *list = std::getenv("PS2_P9_OFF");
    return list && std::strstr(list, name);
}

inline std::uint32_t game() { return ps2::ld32(kGameStatePointer); }
inline std::uint32_t player_count() {
    const std::uint32_t g = game();
    return g ? ps2::ld16(g + kPlayerCountOffset) : 0;
}

// Texture pack (p9_textures.cpp), extra RAM 0x02100000 to 0x0215ffff: the alias table is written at boot, everything else when the first extra player is added.
constexpr std::uint32_t kTexState = kBase + 0x100000;    // words: pack hash (0: no pack), first added sheet record, resource being loaded, sprite sheet rectangles added
constexpr std::uint32_t kTexPackHash = kTexState, kTexFirstSheetRecord = kTexState + 4, kTexLoading = kTexState + 8, kTexPageExtended = kTexState + 12;
constexpr std::uint32_t kTexNames = kBase + 0x100100;    // object names the markers use, 32 bytes each
constexpr std::uint32_t kWadAliasCount = kBase + 0x110000, kWadAliasEntries = kBase + 0x110010, kWadIndexBytes = 0x30000;      // WARRIORS.DIR's entries (12 bytes) and the pack's added names
constexpr std::uint32_t kObjectListCopy = kBase + 0x140000, kObjectListBytes = 0x10000;  // the object list's records (36 bytes) and the markers' four
constexpr std::uint32_t kSheetTableCopy = kBase + 0x150000, kSheetTableBytes = 0x2000;   // the sprite sheet table (8 bytes a record) and the ten banners
constexpr std::uint32_t kPageRectCopy = kBase + 0x152000, kPageRectBytes = 0x2000;       // part_page0's rectangles (16 bytes) and the six radar sprites

// Cameras and views (p9_camera.cpp, p9_views.cpp), extra RAM 0x02200000 to 0x0220ffff; 0x02210000 to 0x0221ffff are the audio listener buffers (p9_audio.hpp).
// The camera manager's nine per-player arrays (0x005d9148 + 8 * array: base cameras, view list, follow, rail, hood, mug, mini, transition, power cameras) hold two entries
// each, packed. They live here with four entries each, 16 bytes apart; so do the per-player switches (0x0050b1b0, 0x0050b1b8) and the view-enable words (0x0050b1e8).
// A snapshot made before the move is taken over once (ensure_cameras). p9_edits_cam.py holds the same addresses for the generated code.
constexpr std::uint32_t kCam = kBase + 0x200000;
constexpr std::uint32_t kCamArrayBytes = 4 * kMaxPlayers;
constexpr std::uint32_t kCamBases = kCam, kCamViews = kCam + kCamArrayBytes, kCamFollow = kCam + 2 * kCamArrayBytes;   // arrays 0, 1, 2
constexpr std::uint32_t kCamArrayCount = 9;
constexpr std::uint32_t kCamViewEnable = kCam + kCamArrayCount * kCamArrayBytes;                                     // 0x0050b1e8: the view of player i counts
constexpr std::uint32_t kCamStickSwitch = kCamViewEnable + kCamArrayBytes, kCamZoomSwitch = kCamStickSwitch + kCamArrayBytes;   // 0x0050b1b0, 0x0050b1b8
constexpr std::uint32_t kCamOptionA = kCam + 0xD0, kCamOptionB = kCam + 0xE0;       // 0x0050b1f0, 0x0050b1f8: a player's right stick options (words; invert)
constexpr std::uint32_t kCamAutoAdjust = kCam + 0xF0, kCamAutoFollow = kCam + 0xF4;  // 0x0050b240, 0x0050b248: a player's auto-adjust option and auto-follow switch (bytes)
constexpr std::uint32_t kCamMagic = kCam + 0xC0, kCamSlots = kCam + 0xC4;           // kCamMagicValue once taken over; list slots with cameras (at least two)
constexpr std::uint32_t kCamMagicValue = 0x43414D34;
constexpr std::uint32_t kCamViewState = kCam + 0x100;   // which players share a view (p9_views.cpp) and how their camera frames them (p9_framing.cpp), 0x100 bytes

// Level flows of players 3 and 4 (p9_flow_boxes.cpp), extra RAM 0x02310000 to 0x0231ffff (0x02300000 to 0x0230ffff are the AI area's).
constexpr std::uint32_t kFlow = kBase + 0x310000;
constexpr std::uint32_t kPlayerBoxMessage = kFlow, kPlayerBoxMessageBytes = 0x30;        // the message a player box delivers to its script (scratch)
constexpr std::uint32_t kPlayerBoxOccupants = kFlow + 0x100;                              // per player box of the pool, the handles of players 3 and 4 inside it (8 bytes a box)
constexpr std::uint32_t kPlayerBoxLimit = 256;                                            // boxes the table holds (the biggest level uses 36)

}
