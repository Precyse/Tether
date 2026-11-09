#include "ui_hooks.hpp"

#include "netplay/netplay.hpp"
#include "netplay/session_file.hpp"
#include "overlay/overlay.hpp"
#include "ps2/system.hpp"
#include "ui_game.hpp"

#include <atomic>

namespace warriors::ui {

namespace {

constexpr std::uint32_t kLobbyControllerWord = kLobbyState + 0x2C;   // the flow controller the lobby's transitions were added to
constexpr std::uint32_t kTransitionContinueProfile = 0, kTransitionContinueCreate = 2, kTransitionModeContinue = 4;
constexpr std::uint32_t kFadeObject = 0x005FDEB8, kFadeOffset = 0x1D8;   // PM_Mode_OnCommand ignores input while this float is not zero
constexpr std::uint32_t kCoopCode = 4, kCoopIndex = 1;   // CO-OP is the second item
constexpr std::uint32_t kStringQuickRumble = 0x79, kStringExtras = 0x8A, kCodeQuickRumble = 1, kCodeExtras = 5;
constexpr std::uint32_t kGridItemFont = 6, kGridItemSpacing = 4, kGridItemFlag = 1, kGridStackFlag = 1;
constexpr std::uint32_t kGridSelectionOffset = 0xA4;   // the selected item; OptionGrid_Focus (run next, by the original) focuses it
constexpr float kMenuItemSize = 1.15f;
constexpr std::uint64_t kRowsWithCoop = 0x200000002ull;   // items per row, one 32-bit word per row: two rows of two (the game's own is 2 and 1)
constexpr std::uint32_t kGreetStartPressed = 0x94;   // PM_Greet: START was pressed (the game's own Update reads it, then leaves for the menu)
constexpr std::uint32_t kEntryNone = 0, kEntryTitlePassed = 1, kEntryMenuPassed = 2;
constexpr std::uint64_t kTitleStaleFrames = 3;     // the title probe says "not showing" this many frames after the controller last saw it

std::atomic<std::uint64_t> g_title_frame{0};

// Display only: read by the overlay on the emulation thread, never by the game.
bool title_showing() {
    const std::uint64_t seen = g_title_frame.load();
    return seen && ps2::frame_count() - seen <= kTitleStaleFrames;
}

void handled(ps2::Cpu &c, std::uint32_t result) {
    c.r[2].sd[0] = static_cast<std::int32_t>(result);
    c.pc = c.r[31].w[0];
}

void add_lobby_transitions(ps2::Cpu &c, std::uint32_t controller) {
    const auto link = [&](std::uint32_t from, std::uint32_t code, std::uint32_t to) {
        const std::uint32_t flow = ps2::ld32(controller + 8);
        ps2::GuestArgs call;
        call.a[0] = controller + signed_half(flow + 0x18);
        call.a[1] = ps2::ld32(controller + from) + kScreenSubobject;
        call.a[2] = code;
        call.a[3] = ps2::ld32(controller + to) + kScreenSubobject;
        ps2::call_guest(c, ps2::ld32(flow + 0x1C), call);
    };
    link(kControllerContinue, kTransitionContinueProfile, kControllerProfile);
    link(kControllerContinue, kTransitionContinueCreate, kControllerCreate);
    ps2::st32(kLobbyControllerWord, controller);
}

}

bool coop_session() {
    const ps2::netplay::SessionInfo *session = ps2::netplay::session();
    return session && (session->role == ps2::netplay::SessionRole::local || ps2::netplay::locked());
}

void controller_update(ps2::Cpu &c) {
    static const bool installed = [] {
        ps2::overlay::set_title_probe(title_showing);
        return true;
    }();
    (void)installed;
    const std::uint32_t controller = c.r[4].w[0];
    const std::uint32_t top = guest(c, kFlowGetTop, {controller});
    if (top && top == ps2::ld32(controller + kControllerGreet) + kScreenSubobject) g_title_frame = ps2::frame_count();
    if (coop_session() && ps2::ld32(kLobbyControllerWord) != controller) add_lobby_transitions(c, controller);
}

std::uint64_t menu_rows(std::uint32_t address) {
    if (coop_session()) return kRowsWithCoop;
    return static_cast<std::uint64_t>(ps2::ld32(address)) | static_cast<std::uint64_t>(ps2::ld32(address + 4)) << 32;
}

bool menu_add_coop_items(ps2::Cpu &c) {
    if (!coop_session()) return false;
    const std::uint32_t screen = c.r[18].w[0];
    const std::uint32_t grid = ps2::ld32(screen + kScreenGrid);
    const std::uint32_t colour = ps2::ld32(kColourRed);
    const auto add = [&](std::uint32_t text, std::uint32_t colon_after, std::uint32_t code) {
        guest(c, kGridAddItem, {grid, text, colon_after, code, kGridItemSpacing, kGridItemFlag, colour, kGridItemFont}, float_bits(kMenuItemSize), {kGridStackFlag});
    };
    add(put_text(kTempText, "CO-OP"), 0, kCoopCode);
    add(guest(c, kGlobalStringGet, {kStringQuickRumble}), 1, kCodeQuickRumble);
    add(guest(c, kGlobalStringGet, {kStringExtras}), 0, kCodeExtras);
    ps2::st32(grid + kGridSelectionOffset, kCoopIndex);
    return true;
}

// The launcher starts a game straight into the lobby: once, at boot, the title takes START and the menu takes CO-OP without a pad (and the host's pad is bound to the front end). Both are words of guest RAM
// set from the same frames on every machine, so lockstep holds; the way back (triangle) shows the title and the menu as they are.
void greet_update(ps2::Cpu &c) {
    if (!coop_session() || ps2::ld32(kAutoEntry) != kEntryNone) return;
    ps2::st32(c.r[4].w[0] + kGreetStartPressed, 1);
    ps2::st32(kAutoEntry, kEntryTitlePassed);
}

void menu_update(ps2::Cpu &c) {
    if (!coop_session() || ps2::ld32(kAutoEntry) != kEntryTitlePassed) return;
    const std::uint32_t screen = c.r[4].w[0];
    if (ps2::ld32(screen + kScreenCreated) == 0 || ps2::ld32(screen + kScreenGrid) == 0 || ps2::ld32(ps2::ld32(kFadeObject) + kFadeOffset) != 0) return;
    ps2::st32(kAutoEntry, kEntryMenuPassed);
    guest(c, kHudBindPad, {kHudPads, 0, 0});   // the front end listens to the pad that pressed START (the game binds it then); here it is the host's
    ps2::st32(screen + kScreenResult, kTransitionModeContinue);
}

bool menu_command(ps2::Cpu &c) {
    constexpr std::uint32_t kAccept = 4;
    const std::uint32_t screen = c.r[4].w[0];
    if (c.r[5].w[0] != kAccept || ps2::ld32(ps2::ld32(kFadeObject) + kFadeOffset) != 0) return false;
    const std::uint32_t grid = ps2::ld32(screen + kScreenGrid);
    if (guest(c, kGridItemCode, {grid, guest(c, kGridSelected, {grid})}) != kCoopCode) return false;
    play_cue(c, kCueAccept);
    ps2::st32(screen + kScreenResult, kTransitionModeContinue);
    handled(c, 1);
    return true;
}

}
