#pragma once

#include "p9_radar.hpp"
#include "ps2/generated.hpp"

#include <cstdint>

// Ground rings for four players. The game's ring code (Reticules_Update 0x24b780, Reticule_QueueHealthRings 0x24a230) keeps its state in arrays of two (one word per
// player slot) and in a fade-in list of four 12 byte entries, and builds the list for one frame in a four entry stack buffer. Players 3 and 4 and their targets need
// room for 8 entries and slots 2 and 3 of every per-slot array, so all of it lives in the range below (extra RAM, zero until the first frame of the ring code takes
// the original words over) and apply_p10 points the ring functions at it (p9_edits_ring.py). Entries 0 to 3 of both lists are still moved by the game's own code,
// which keeps the cycle count of a one or two player game unchanged; entries 4 to 7 follow right behind them and are moved by ring_end_frame.
namespace warriors::p9 {

constexpr std::uint32_t kRingBase = 0x02060000;
constexpr std::uint32_t kRingMagic = kRingBase;                 // kRingMagicValue once the original words are taken over
constexpr std::uint32_t kRingQueued = kRingBase + 0x10;         // 0x00510530: per slot, a ring was queued for this human this frame (cleared at the end of Reticules_Update)
constexpr std::uint32_t kRingRageFull = kRingBase + 0x20;       // 0x00510538: per slot, the rage bar was below its maximum (armed for the full flash)
constexpr std::uint32_t kRingFlashCount = kRingBase + 0x30;     // 0x00510540: per slot, flashes shown since the rage bar was full (3 ends the flash)
constexpr std::uint32_t kRingFlashStamp = kRingBase + 0x40;     // 0x006c72c8: per slot, time of the last flash step
constexpr std::uint32_t kRingFlashStampReady = kRingBase + 0x50;   // 0x006c72d0: the stamps were set once
constexpr std::uint32_t kRingFadeReady = kRingBase + 0x54;      // 0x006c7308: the fade-in list was initialised
constexpr std::uint32_t kRingFadeList = kRingBase + 0x60;       // 0x006c72d8: the fade-in list of the last frame
constexpr std::uint32_t kRingWorkList = kRingBase + 0xC0;       // the list of this frame (the original's stack buffer)
constexpr std::uint32_t kRingBytes = 0x120;

constexpr std::uint32_t kRingSlots = 4;
constexpr std::uint32_t kRingFadeEntries = 8, kRingFadeEntryBytes = 12;
constexpr std::uint32_t kRingFadeOriginalEntries = 4;           // the entries the original code moves itself

// Every ring function starts with its state taken over; the work list starts empty.
void ring_frame_start();
// End of Reticules_Update: the entries beyond the original four go from this frame's list to the fade-in list, the queued flags of slots 2 and 3 are cleared.
void ring_end_frame();
// The flash stamps are set once for all slots, as the original does for slots 0 and 1.
void ring_stamp_all_slots();
// The reticule request byte of a panel (HUD_SetReticuleRequest writes it, Reticules_Update reads it): panels 2 and 3 have theirs in HUD2. `bytes` is the HUD's byte array.
std::uint32_t ring_request_byte(std::uint32_t bytes, std::uint32_t panel);

}
