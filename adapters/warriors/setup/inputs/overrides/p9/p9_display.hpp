#pragma once

#include "ps2/generated.hpp"

#include <cstdint>

// Display-only changes for three or more players (p9_edits_display.py puts a call to each at the start of the function it names). Each one works on a copy or restores what
// it touched before the draw ends, so the game's own state is as the original leaves it and every machine of a session does the same.
namespace warriors::p9 {

constexpr float kPanelScale = 0.75f;   // size of a HUD panel with three or more players (UI_SPEC 3.3), so four fit

// player_panel_draw(panel): draws the panel through the game's own code with its sprites scaled (p9_hud_scale.cpp). True when handled.
bool panel_render(ps2::Cpu &c);
// sprite_submit(instance, sprite): while a panel is drawn, the sprite handed on is a copy scaled about the panel's corner.
void scale_sprite(ps2::Cpu &c);

// person_model_draw(render, view): a player's model that is closer to the camera of the view being drawn than kNearFadeStart fades out, and is gone at kNearFadeEnd
// (p9_near_fade.cpp). True when handled.
bool human_draw(ps2::Cpu &c);

}
