#pragma once

#include <cstdint>

// Where the game keeps the screen's grid of views and where a PlayerPanel keeps its origin, for the hooks that place and draw the HUD panels.
namespace warriors::p9 {

constexpr std::uint32_t kViewViews = 0x0050B198, kViewColumns = 0x0050B1A8, kViewRows = 0x0050B1AA;   // how many views the screen is split into, and the grid's columns and rows
constexpr std::uint32_t kPlayerHudPanel = 0x40F4;  // PlayerPanel: the panel it draws
constexpr std::uint32_t kPlayerHudBase = 0x40E0;   // PlayerPanel: the panel's origin (x, 0, vertical, w floats)
constexpr std::uint32_t kBaseVertical = 8;         // the vertical word of an origin and of the layout bases: the screen conversion (screen_to_overlay_convert) drops the second

}
