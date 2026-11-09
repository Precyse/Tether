#include "p9_ring.hpp"

#include "p9_game.hpp"
#include "p9_hud.hpp"
#include "ps2/system.hpp"

namespace warriors::p9 {

namespace {

constexpr std::uint32_t kRingMagicValue = 0x52494E47;   // "RING"
constexpr std::uint32_t kOriginalSlotWords = 2;

// The original places of the words the range replaces.
constexpr std::uint32_t kOldQueued = 0x00510530, kOldRageFull = 0x00510538, kOldFlashCount = 0x00510540, kOldFlashStamp = 0x006C72C8;
constexpr std::uint32_t kOldFlashStampReady = 0x006C72D0, kOldFadeList = 0x006C72D8, kOldFadeReady = 0x006C7308;

constexpr std::uint32_t kRequestBytesOffset = 0x225D0;   // HUD_SetReticuleRequest: hud + 0x225d0 + panel

void zero(std::uint32_t at, std::uint32_t bytes) {
    for (std::uint32_t i = 0; i < bytes; i += 4) ps2::st32(at + i, 0);
}

void copy(std::uint32_t to, std::uint32_t from, std::uint32_t bytes) {
    for (std::uint32_t i = 0; i < bytes; i += 4) ps2::st32(to + i, ps2::ld32(from + i));
}

// The original words of a snapshot (or of a game that has not run the ring code yet) become the first entries of the range. A flash stamp already set belongs to
// all four players: slots 2 and 3 take the stamp of slot 1, as ring_stamp_all_slots does when the original sets it.
void take_over() {
    zero(kRingBase, kRingBytes);
    copy(kRingQueued, kOldQueued, kOriginalSlotWords * 4);
    copy(kRingRageFull, kOldRageFull, kOriginalSlotWords * 4);
    copy(kRingFlashCount, kOldFlashCount, kOriginalSlotWords * 4);
    copy(kRingFlashStamp, kOldFlashStamp, kOriginalSlotWords * 4);
    ps2::st32(kRingFlashStampReady, ps2::ld32(kOldFlashStampReady));
    ps2::st32(kRingFadeReady, ps2::ld32(kOldFadeReady));
    copy(kRingFadeList, kOldFadeList, kRingFadeOriginalEntries * kRingFadeEntryBytes);
    if (ps2::ld32(kRingFlashStampReady)) ring_stamp_all_slots();
    ps2::st32(kRingMagic, kRingMagicValue);
}

}

void ring_frame_start() {
    if (ps2::ld32(kRingMagic) != kRingMagicValue) take_over();
    zero(kRingWorkList, kRingFadeEntries * kRingFadeEntryBytes);
}

void ring_end_frame() {
    const std::uint32_t first = kRingFadeOriginalEntries * kRingFadeEntryBytes;
    copy(kRingFadeList + first, kRingWorkList + first, (kRingFadeEntries - kRingFadeOriginalEntries) * kRingFadeEntryBytes);
    for (std::uint32_t slot = kOriginalSlotWords; slot < kRingSlots; ++slot) ps2::st32(kRingQueued + slot * 4, 0);
}

void ring_stamp_all_slots() {
    const std::uint32_t stamp = ps2::ld32(kRingFlashStamp + (kOriginalSlotWords - 1) * 4);
    for (std::uint32_t slot = kOriginalSlotWords; slot < kRingSlots; ++slot) ps2::st32(kRingFlashStamp + slot * 4, stamp);
}

std::uint32_t ring_request_byte(std::uint32_t bytes, std::uint32_t panel) {
    return panel >= kOriginalPlayers && panel_count() > kOriginalPlayers ? kHud2 + kRequestBytesOffset + (panel - kOriginalPlayers) : bytes + panel;
}

}
