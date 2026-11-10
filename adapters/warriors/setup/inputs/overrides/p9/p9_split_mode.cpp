#include "p9_camera.hpp"

#include "overlay/overlay.hpp"
#include "p9_game.hpp"
#include "ps2/pad.hpp"
#include "ps2/system.hpp"

// The split screen setting: Auto (one combined view while every player fits one picture) or Always split. It decides what the cameras do, so it is game state: the word
// kSplitSwitchAt is set from the host's session file on every machine at the start of the session (netplay.cpp) and changed here, by the pad stream's request chord
// (ps2::kRequestChord: SELECT, L3 and R3 together on any pad, or the window's key for it). Pad samples reach every machine at the same frame, so every machine changes
// the setting at the same frame.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kPadRecordCount = 4;
constexpr std::uint32_t kPadRecords[kPadRecordCount] = {0, 4, 1, 2};   // the game's pad records of the four pads (p9_pads.cpp)
constexpr const char *kAutoNotice = "SPLIT SCREEN: AUTO", *kAlwaysNotice = "SPLIT SCREEN: ALWAYS";

bool chord_held() {
    for (const std::uint32_t record : kPadRecords) {
        const std::uint16_t buttons = swap_button_bytes(ps2::ld16(kPortRecords + record * kPortRecordStride + kPortButtonsOffset));
        if ((buttons & ps2::kRequestChord) == ps2::kRequestChord) return true;
    }
    return false;
}

}

bool split_always() { return ps2::ld32(kSplitSwitchAt) != 0; }

void poll_split_request() {
    const bool held = chord_held();
    const bool pressed = held && !ps2::ld32(kChordHeldAt);
    ps2::st32(kChordHeldAt, held ? 1 : 0);
    if (!pressed) return;
    ps2::st32(kSplitSwitchAt, split_always() ? 0 : 1);
    P9_TRACE("[p9] split screen %s at frame %llu", split_always() ? "always" : "auto", static_cast<unsigned long long>(ps2::frame_count()));
    ps2::overlay::notice(split_always() ? kAlwaysNotice : kAutoNotice, "");
}

}
