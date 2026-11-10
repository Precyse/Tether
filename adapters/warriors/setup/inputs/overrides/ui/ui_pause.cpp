#include "ui_hooks.hpp"

#include "netplay/session_file.hpp"
#include "overlay/overlay.hpp"
#include "ui_game.hpp"

#include <string>

// Whose pause menu it is. The game's pause menu answers to the player who opened it (the menu's +0x1bc0 is that player's slot, written before the menu opens); with
// four players nobody else can tell who holds it. The overlay shows "PAUSED BY <name>" on every machine while it is open: the name is the roster name of the pad of
// that player, read from guest memory at the entry of pause_screen_open and cleared at the entry of pause_screen_close. Display only, nothing is written to the game.
namespace warriors::ui {

namespace {

constexpr std::uint32_t kPauserSlotOffset = 0x1BC0;

// The game's pad records: 0 and 4 are the first two ports, 1 and 2 the runtime's pads 3 and 4 (p9_pads.cpp).
int port_of_pad(int pad) {
    switch (pad) {
    case 0: return 0;
    case 4: return 1;
    case 1: return 2;
    case 2: return 3;
    default: return -1;
    }
}

}

void pause_open(ps2::Cpu &c) {
    if (!coop_session()) return;
    const int port = port_of_pad(player_pad(c, ps2::ld32(c.r[4].w[0] + kPauserSlotOffset)));
    if (port >= 0) ps2::overlay::paused_by(ps2::netplay::player_name(port));
}

void pause_close(ps2::Cpu &) {
    ps2::overlay::paused_by(std::string());
}

}
