#include "p9_camera.hpp"

#include "p9_game.hpp"
#include "ps2/system.hpp"

// The camera manager's per-player arrays for four players (p9_edits_cam.py moves the generated code's references to the four-entry copy in extra RAM): taking over a
// snapshot, the slot count the manager's two-entry loops cover, and releasing a camera that a further player holds.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kOldCameraArrays = 0x005D9148, kOldArrayBytes = 8;          // nine arrays of two entries, packed
constexpr std::uint32_t kOldViewEnable = 0x0050B1E8, kOldStickSwitch = 0x0050B1B0, kOldZoomSwitch = 0x0050B1B8;   // two words each
constexpr std::uint32_t kOldOptionA = 0x0050B1F0, kOldOptionB = 0x0050B1F8, kOldAutoAdjust = 0x0050B240, kOldAutoFollow = 0x0050B248;   // two words, two words, two bytes, two bytes
constexpr std::uint32_t kOldEntries = kOriginalPlayers;
constexpr std::uint32_t kWord = 4;
constexpr std::uint32_t kStockFovRateCap = 0x40F00000, kFramingFovRateCap = 0x41F00000;   // 7.5 and 30.0 as floats

void take_over(std::uint32_t to, std::uint32_t from) {
    for (std::uint32_t i = 0; i < kOldEntries; ++i) ps2::st32(to + i * kWord, ps2::ld32(from + i * kWord));
}

void take_over_bytes(std::uint32_t to, std::uint32_t from) {
    for (std::uint32_t i = 0; i < kOldEntries; ++i) ps2::st8(to + i, ps2::ld8(from + i));
}

std::uint32_t slot_count() {
    const std::uint32_t used = ps2::ld32(kCamSlots);
    return used > kOriginalPlayers ? used : kOriginalPlayers;
}

}

// The words of a snapshot made before the move (or of a game that has not run camera code yet) are copied once, before any reader looks at the new place.
void ensure_cameras(ps2::Cpu &) {
    if (ps2::ld32(kCamMagic) == kCamMagicValue) return;
    for (std::uint32_t array = 0; array < kCamArrayCount; ++array)
        take_over(kCam + array * kCamArrayBytes, kOldCameraArrays + array * kOldArrayBytes);
    take_over(kCamViewEnable, kOldViewEnable);
    take_over(kCamStickSwitch, kOldStickSwitch);
    take_over(kCamZoomSwitch, kOldZoomSwitch);
    take_over(kCamOptionA, kOldOptionA);
    take_over(kCamOptionB, kOldOptionB);
    take_over_bytes(kCamAutoAdjust, kOldAutoAdjust);
    take_over_bytes(kCamAutoFollow, kOldAutoFollow);
    ps2::st32(kCamSlots, kOriginalPlayers);
    ps2::st32(kCamMagic, kCamMagicValue);
}

// A player beyond the first two starts with the camera switches and options of the second one (scripts and the options menu set them for the original two only).
void cameras_add_player(ps2::Cpu &c) {
    const std::uint32_t slot = c.r[4].w[0];
    if (slot < kOriginalPlayers || slot >= kMaxPlayers) return;
    if (ps2::ld32(kCamSlots) < slot + 1) ps2::st32(kCamSlots, slot + 1);
    const std::uint32_t last = kOriginalPlayers - 1;
    ps2::st32(kCamStickSwitch + slot * kWord, ps2::ld32(kCamStickSwitch + last * kWord));
    ps2::st32(kCamZoomSwitch + slot * kWord, ps2::ld32(kCamZoomSwitch + last * kWord));
    ps2::st32(kCamOptionA + slot * kWord, ps2::ld32(kCamOptionA + last * kWord));
    ps2::st32(kCamOptionB + slot * kWord, ps2::ld32(kCamOptionB + last * kWord));
    ps2::st8(kCamAutoAdjust + slot, ps2::ld8(kCamAutoAdjust + last));
    ps2::st8(kCamAutoFollow + slot, ps2::ld8(kCamAutoFollow + last));
    ps2::st32(kCamViewEnable + slot * kWord, 1);
}

std::uint32_t player_view_camera(std::uint32_t view) { return view < kMaxPlayers ? ps2::ld32(kCamViews + view * kWord) : 0; }

std::uint32_t fov_rate_cap() { return player_count() > kOriginalPlayers ? kFramingFovRateCap : kStockFovRateCap; }

std::uint32_t cam_last_slot() { return slot_count() - 1; }

std::uint32_t release_cam_slots() {
    const std::uint32_t slots = slot_count();
    ps2::st32(kCamSlots, kOriginalPlayers);
    return slots;
}

// A camera that is some player's base camera stays unless the release is forced (the original tests players 1 and 2).
bool release_blocked(ps2::Cpu &c) {
    const std::uint32_t camera = c.r[4].w[0], forced = c.r[5].w[0];
    if (forced || !camera) return false;
    for (std::uint32_t slot = kOriginalPlayers; slot < slot_count(); ++slot) {
        if (ps2::ld32(kCamBases + slot * kWord) != camera) continue;
        c.pc = c.r[31].w[0];
        return true;
    }
    return false;
}

void release_extra_slots(std::uint32_t camera) {
    for (std::uint32_t slot = kOriginalPlayers; slot < kMaxPlayers; ++slot) {
        if (ps2::ld32(kCamBases + slot * kWord) == camera) ps2::st32(kCamBases + slot * kWord, 0);
        if (ps2::ld32(kCamViews + slot * kWord) == camera) ps2::st32(kCamViews + slot * kWord, 0);
    }
}

}
