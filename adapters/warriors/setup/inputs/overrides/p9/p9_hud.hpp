#pragma once

#include "ps2/generated.hpp"

#include <cstdint>

// The HUD for four players (apply_p9.py puts calls to these into the generated HUD, PlayerHUD and join functions). Players 3 and 4 get a second HUD object (HUD2)
// built by the game's own constructor the first time a third player is added; until then every function here answers as the original two player game does.
namespace warriors::p9 {

// 2 until HUD2 exists, then 4: the number of PlayerHUD panels the HUD loops visit. last_panel() is the same minus one (loops that count down to zero).
std::uint32_t panel_count();
std::uint32_t last_panel();

// Builds HUD2 (once): the second object, the CrewStatus widget moved off the pointer array, the layout table copy. Called by PlayerList_Add for a third player.
void build_hud2(ps2::Cpu &c);

// Where the CrewStatus widget of a HUD is (it moved when HUD2 was built).
std::uint32_t crew_widget(std::uint32_t hud);

// mult for `index * stride` of the HUD's per-player arrays and PlayerHUD's layout tables: indices 2 and 3 land in HUD2 (or the table copy) as entries 0 and 1.
void hud_mult(ps2::Cpu &c, int pipe, std::uint32_t index, std::uint32_t stride);

// `panel << 4`, the offset of a panel's base position in the layout tables.
std::uint32_t panel_base_offset(std::uint32_t panel);

// The next element of a per-player array whose elements are stepped through by a pointer: the one after element 1 is HUD2's element 0.
std::uint32_t array_step(std::uint32_t pointer, std::uint32_t array_offset, std::uint32_t stride);

}
