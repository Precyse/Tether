#include "p9_vpad.hpp"

#include "p9_game.hpp"
#include "p9_hud.hpp"
#include "ps2/system.hpp"

// Without these the HUD's two-entry virtual pad array would hand a PlayerPanel object to the pause menu as player 3's or 4's pad record (HUD +8 and +0xc hold the
// PlayerPanel pointers), and binding a pad to panel 2 or 3 would write into that PlayerPanel.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kOverlayPadsPrepare = 0x001AE4D8, kOverlayPadClear = 0x001AE5A0;
constexpr std::uint32_t kPadRecords = 0x005DD810, kPadRecordStride = 0x50, kPadRecordCount = 8, kRecordOwnerOffset = 0x42;   // owner: the panel the pad is bound to, -1 for none
constexpr std::uint32_t kRecordPadOffset = 0x19, kRecordBoundOffset = 0x1B;     // PadSeatRecord: the pad index (-1 for none) and the bound flag
constexpr std::uint16_t kNoOwner = 0xFFFF;

bool extra_panel(std::uint32_t hud, std::uint32_t panel) { return hud == kHud && panel >= kOriginalPlayers && panel < kMaxPlayers && panel_count() == kMaxPlayers; }
std::uint32_t slot_of(std::uint32_t panel) { return kHud2 + (panel - kOriginalPlayers) * 4; }

}

bool vpad_get(ps2::Cpu &c) {
    const std::uint32_t hud = c.r[4].w[0], panel = c.r[5].w[0];
    if (!extra_panel(hud, panel)) return false;
    ps2::call_guest(c, kOverlayPadsPrepare, kHud2);
    c.r[2].sd[0] = static_cast<std::int32_t>(ps2::ld32(slot_of(panel)));
    c.pc = c.r[31].w[0];
    return true;
}

// overlay_pad_assign(hud, panel, pad): the pad's record names the panel as its owner, the panel's virtual pad names the pad. Panels 2 and 3 do this with their entries in HUD2.
bool vpad_bind(ps2::Cpu &c) {
    const std::uint32_t hud = c.r[4].w[0], panel = c.r[5].w[0];
    const std::int8_t pad = static_cast<std::int8_t>(c.r[6].w[0]);
    if (!extra_panel(hud, panel)) return false;
    ps2::call_guest(c, kOverlayPadsPrepare, kHud2);
    for (std::uint32_t i = 0; i < kPadRecordCount; ++i)
        if (ps2::ld16(kPadRecords + i * kPadRecordStride + kRecordOwnerOffset) == panel) ps2::st16(kPadRecords + i * kPadRecordStride + kRecordOwnerOffset, kNoOwner);
    const std::uint32_t record = ps2::ld32(slot_of(panel));
    ps2::st8(record + kRecordPadOffset, static_cast<std::uint8_t>(pad));
    ps2::st8(record + kRecordBoundOffset, pad < 0 ? 0 : 1);
    if (pad >= 0) ps2::st16(kPadRecords + static_cast<std::uint32_t>(pad) * kPadRecordStride + kRecordOwnerOffset, static_cast<std::uint16_t>(panel));
    c.pc = c.r[31].w[0];
    return true;
}

// The game's own routines run for HUD2's entries too, then for the HUD as always.
bool vpad_ensure(ps2::Cpu &c) {
    if (c.r[4].w[0] == kHud && panel_count() == kMaxPlayers) ps2::call_guest(c, kOverlayPadsPrepare, kHud2);
    return false;
}

bool vpad_reset(ps2::Cpu &c) {
    if (c.r[4].w[0] == kHud && panel_count() == kMaxPlayers) ps2::call_guest(c, kOverlayPadClear, kHud2);
    return false;
}

}
