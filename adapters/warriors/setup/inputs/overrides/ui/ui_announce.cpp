#include "ui_hooks.hpp"

#include "netplay/netplay.hpp"
#include "netplay/wire.hpp"
#include "ps2/system.hpp"
#include "ui_game.hpp"

#include <cctype>
#include <string>

// A player leaving or coming back, said the way the game says its own messages: the scroll-in line of the HUD with its cue (HUD_QueueScrollIn, as HUD_SetObjective
// queues an objective). It changes the game's state (the HUD's queue), and the runtime calls it at the frame the player's pad is unplugged or plugged in, which is
// the same frame on every machine, so lockstep holds. Outside a level (the front end, a level change) the runtime shows its own notice instead.
namespace warriors::ui {

namespace {

constexpr std::uint32_t kQueueScrollIn = 0x001B3918;     // HUD_QueueScrollIn(f12 speed, f13 scale, f14 x, f15 y, hud, prefix, text, -1, cue, 0, 1, 0)
constexpr std::uint32_t kGetCueTable = 0x00111130;       // AudioManager_GetCueTable(audio, id)
constexpr std::uint32_t kScrollPrefix = 0x00556410;      // the prefix string the objectives pass
constexpr std::uint32_t kScrollX = 0x0050EDE4, kScrollY = 0x0050EDE0;   // where the objective line sits (floats)
constexpr std::uint32_t kScrollSpeedBits = 0x3D4CCCCD, kScrollScaleBits = 0x3F800000;   // 0.05 and 1.0, as the objectives pass them
constexpr float kScrollRaise = 0.02f;                    // the line goes this much above the objectives' own
constexpr std::uint32_t kJoinLeaveCue = 0x15;
constexpr std::uint32_t kNoOwner = 0xFFFFFFFF;

// The player colours of UI_SPEC section 1 as markup, for the name.
constexpr const char *kNameMarkup[] = {"<COLOR BE3232FF>", "<COLOR C8A014FF>", "<COLOR 6EB432FF>", "<COLOR A064C8FF>"};
constexpr const char *kTextMarkup = "<COLOR B2B2B2FF>";

constexpr std::uint32_t kLevelFlowIdleOffset = 0x14C;   // W_GameState: 0 while no level change is under way
constexpr const char kFrontEndLevel[] = "level100";

// A level is loaded and not changing: its HUD exists, so its scroll-in queue takes the line (shown when the HUD is: not in a cutscene or the pause menu).
bool level_running() {
    const std::uint32_t game = ps2::ld32(kGameStatePointer);
    if (!game || ps2::ld32(game + kLevelFlowIdleOffset) != 0) return false;
    std::string name;
    for (std::uint32_t i = 0; i < kLevelNameBytes; ++i) {
        const char ch = static_cast<char>(ps2::ld8(game + kLevelNameOffset + i));
        if (!ch) break;
        name += ch;
    }
    return !name.empty() && name != kFrontEndLevel;
}

float guest_float(std::uint32_t address) {
    const std::uint32_t bits = ps2::ld32(address);
    float value;
    __builtin_memcpy(&value, &bits, sizeof value);
    return value;
}

std::string sentence(std::uint8_t event) {
    switch (event) {
    case ps2::netplay::kLeftCrashed: return "'S GAME CRASHED";
    case ps2::netplay::kLeftLost: return " LOST CONNECTION";
    case ps2::netplay::kJoinedBack: return " IS BACK";
    default: return " LEFT THE GAME";
    }
}

bool announce(int port, std::uint8_t event, const std::string &name) {
    ps2::Cpu &c = ps2::g_cpu;
    if (!level_running()) return false;
    std::string upper_name = name;
    for (char &ch : upper_name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    const std::string text = std::string(kNameMarkup[port & 3]) + upper_name + kTextMarkup + sentence(event);
    ps2::GuestArgs call;
    call.f12 = kScrollSpeedBits;
    call.f13 = kScrollScaleBits;
    call.f14 = ps2::ld32(kScrollX);
    call.f15 = float_bits(guest_float(kScrollY) - kScrollRaise);
    call.a[0] = kHudPads;
    call.a[1] = kScrollPrefix;
    call.a[2] = put_text(kTempText, text);
    call.a[3] = kNoOwner;
    call.a[4] = guest(c, kGetCueTable, {ps2::ld32(kCueTablePointer), kJoinLeaveCue});
    call.a[6] = 1;
    ps2::call_guest(c, kQueueScrollIn, call);
    return true;
}

}

void level_frame(ps2::Cpu &) {
    static const bool installed = (ps2::netplay::set_announcer(announce), true);
    (void)installed;
}

}
