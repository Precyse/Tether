#pragma once

#include "ps2/generated.hpp"

#include <cstdint>

// Radars for players 3 and 4. The HUD holds one radar and one crime panel (the radar's frame) per player in arrays of two: radars at +0x15d0 and +0x3f10
// (stride 0x2940), crime panels at +0x177d0 and +0x18280 (stride 0xab0). Players 3 and 4 keep theirs in HUD2 (p9_hud.hpp) as entries 0 and 1, created by the game's own
// Radar_Setup / HudCrimePanel_Setup with the player numbers 2 and 3, so a radar centres on the human of its own list slot (Radar_Update reads the handle at
// game + 0x228 + 4 * number). On the shared screen the game shows only radar 0 and hides radar 1 every frame; radars 2 and 3 follow, and in split screen each view's
// radar is updated and rendered here. The hooks do nothing until HUD2 exists, so a one or two player game runs the original code.
namespace warriors::p9 {

// HUD2's radars and crime panels exist (created now when they do not; called when HUD2 is built and by HUD_InitLevel for every later level) and are shut down with the level.
void radar_build(ps2::Cpu &c);
void radar_shutdown(ps2::Cpu &c);

// Entry hook of a HUD function that applies an operation to the radars of the HUD in a0 (blips added, removed, recoloured, flashed): the same call is made for HUD2's radars,
// so every radar has the same blips in the same slots. The HUD function is never mirrored from inside a mirrored call (HUD2 is not the HUD it tests for).
void radar_mirror(ps2::Cpu &c, std::uint32_t function);
// Radar_SetBlipType is the one fan-out that sits inside a HUD function that also calls fan-outs: it is mirrored at the radar (a0 is radar 1 of the HUD).
void radar_mirror_blip_type(ps2::Cpu &c);
// HudManager_SetRadarRange (floats in f12, f13) and HUD_SetRadarZoomScale (f12): the same values go to HUD2's radars.
void radar_range(ps2::Cpu &c);
void radar_zoom(ps2::Cpu &c);

// HUD_RadarEnable / HUD_RadarDisable for players 3 and 4, beside the calls the game makes for players 1 and 2. `only_all`: HUD_RadarOn/Off(2), the Lua "both players" form.
void radar_toggle(ps2::Cpu &c, bool enable, bool only_all);

// HUD_Hide and HUD_HideForMenu hide the blips of radars 2 and 3 too.
void radar_hide_blips(ps2::Cpu &c);
void radar_hide_for_menu(ps2::Cpu &c);
// HUD_Update: on the shared screen radars 2 and 3 stay hidden; in split screen they are updated like radar 1, with the listeners 2 and 3 (the 0x20 byte entries after
// listener 0, `listener0`) and, until the views lay them out, at the corners of radar 0 and 1.
void radar_shared_hide(ps2::Cpu &c);
void radar_split_update(ps2::Cpu &c, std::uint32_t listener0);
// HUD_Render, split screen.
void radar_split_render(ps2::Cpu &c);

}
