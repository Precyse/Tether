#include "ui_hooks.hpp"

#include "netplay/netplay.hpp"
#include "ui_game.hpp"

#include <string>

// The game's own level-load screen, for the progress every player sees: the runtime counts the disc sectors the load reads (netplay.hpp). Nothing here writes
// guest memory or changes what the game does.
namespace warriors::ui {

namespace {

std::string level_name() {
    const std::uint32_t game = ps2::ld32(kGameStatePointer);
    std::string name;
    for (std::uint32_t i = 0; game && i < kLevelNameBytes; ++i) {
        const char ch = static_cast<char>(ps2::ld8(game + kLevelNameOffset + i));
        if (!ch) break;
        name += ch;
    }
    return name;
}

// The mission number as the player sees it: the game's level table index of the level being loaded (0 is the front end, no mission).
std::string mission_number() {
    const std::uint32_t game = ps2::ld32(kGameStatePointer);
    const std::uint32_t index = game ? ps2::ld16(game + kLevelIndexOffset) : 0;
    return index ? std::to_string(index) : std::string();
}

}

void load_screen_begin(ps2::Cpu &) {
    if (coop_session()) ps2::netplay::load_begin(level_name(), mission_number());
}

void load_screen_end(ps2::Cpu &) {
    ps2::netplay::load_end();
}

}
