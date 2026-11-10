#pragma once

#include "p9_stats.hpp"
#include "ps2/generated.hpp"

// The HUD's virtual pads (HUD +0 and +4: one PadSeatRecord per panel that the pause menu and the pad binding read) for players 3 and 4: they live in HUD2's first two
// entries. Entry hooks of overlay_pad_fetch, overlay_pad_assign, overlay_pads_prepare and overlay_pad_clear; each returns true when it has done the call's work.
namespace warriors::p9 {

bool vpad_get(ps2::Cpu &c);
bool vpad_bind(ps2::Cpu &c);
bool vpad_ensure(ps2::Cpu &c);
bool vpad_reset(ps2::Cpu &c);

}
