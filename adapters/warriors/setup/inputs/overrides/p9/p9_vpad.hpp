#pragma once

#include "p9_stats.hpp"
#include "ps2/generated.hpp"

// The HUD's virtual pads (HUD +0 and +4: one PlayerRecord per panel that the pause menu and the pad binding read) for players 3 and 4: they live in HUD2's first two
// entries. Entry hooks of HUD_GetVirtualPad, HUD_BindPad, HUD_EnsureVirtualPads and HUD_ResetPadBindings; each returns true when it has done the call's work.
namespace warriors::p9 {

bool vpad_get(ps2::Cpu &c);
bool vpad_bind(ps2::Cpu &c);
bool vpad_ensure(ps2::Cpu &c);
bool vpad_reset(ps2::Cpu &c);

}
