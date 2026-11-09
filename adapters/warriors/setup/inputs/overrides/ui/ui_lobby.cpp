#include "ui_hooks.hpp"

#include "netplay/netplay.hpp"
#include "netplay/session_file.hpp"
#include "ps2/pad.hpp"
#include "ui_game.hpp"

#include <algorithm>
#include <cctype>
#include <string>

// The co-op lobby: the game's PM_Continue screen with its widgets replaced by the roster. Four rows (player tag, name, state) built from the game's own
// TextWidgets, the CONTINUE : NEW GAME grid and a usage line. What each row shows comes from the roster names, which pads are plugged and the presses seen
// on them since the lobby opened; all of that is lockstep input, and the few words of state it needs live in guest RAM (kLobbyState).
namespace warriors::ui {

namespace {

constexpr int kSlots = 4;
constexpr std::uint32_t kHostSlot = 0;

// Lobby words in guest RAM (kLobbyState + offset).
constexpr std::uint32_t kReadyMask = kLobbyState + 0x00;       // bit per slot: ready
constexpr std::uint32_t kClaimedMask = kLobbyState + 0x04;     // bit per slot: a local pad that pressed START on an open seat
constexpr std::uint32_t kPrevButtons = kLobbyState + 0x08;     // four u16: the buttons each pad held the frame before
constexpr std::uint32_t kStartHeld = kLobbyState + 0x10;       // frames the host has held START
constexpr std::uint32_t kBegun = kLobbyState + 0x14;           // the lobby has been left forwards
constexpr std::uint32_t kFrames = kLobbyState + 0x18;          // frames in the lobby, for the blinking
constexpr std::uint32_t kShown = kLobbyState + 0x1C;           // what the rows show now, packed (a row is redrawn only when this changes)
constexpr std::uint32_t kRefused = kLobbyState + 0x20;         // START was refused for this press (the cue plays once)
constexpr std::uint32_t kResetBytes = 0x24;                    // the words above are cleared when the lobby opens (the words from +0x24 belong to ui_boot.cpp and ui_front.cpp)

// Widget table.
constexpr int kTitle = 0, kTags = 1, kNames = 5, kStates = 9, kWidgets = 13;

// Pad buttons (PadSample bits).
constexpr std::uint16_t kStartBit = 1u << 3, kCrossBit = 1u << 14;

// Timing, in frames of the game's 60 per second.
constexpr std::uint32_t kForceStartFrames = 120;               // the host holds START this long to begin without everyone ready
constexpr std::uint32_t kBlinkPeriod = 90;                     // PRESS START blinks like the two-player prompt (1,500 ms)

// Where things go (the 1280 x 960 frame of design\ui\mockups\08_lobby.png). A text widget's x is a fraction of the front end's safe width (1,140 px of 1,280,
// from 70 px in) and its y a fraction of the safe height (868.8 px of 960 from 35 px down); the capitals start a little higher the bigger the text. Measured
// on this lobby's own rows against the mockup.
constexpr float kPixelsX = 1140.0f, kPixelsLeft = 70.0f, kSafeHeight = 868.8f, kSafeTop = 35.0f, kCapShiftPerSize = 15.0f;
constexpr float kUsageShift = 13.0f;       // the usage line's own origin
constexpr float kRightAlignShift = 25.0f;  // a right-aligned text ends this far left of its x
constexpr float kTitleCapTop = 410, kSubtitleCapTop = 462, kRowCapTop = 512, kRowStep = 45, kGridCapTop = 712, kUsageCapTop = 780;
constexpr float kTitleSize = 1.4f, kSubtitleSize = 0.8f, kRowSize = 1.0f, kGridSize = 1.15f;
constexpr float kNamePx = 134, kStatePx = 560 + kRightAlignShift;

// Player colours (ABGR words): the markup colours of design\UI_SPEC.md section 1.
constexpr std::uint32_t kPlayerColours[kSlots] = {0xFF3232BE, 0xFF14A0C8, 0xFF32B46E, 0xFFC864A0};
constexpr std::uint32_t kStringContinue = 0x7A;
constexpr std::uint32_t kLine = 0x100;                         // the "line" the game's heap records are tagged with

enum class Seat { open, host, claimable, member };

bool g_call_original = false;   // PM_Continue_OnCommand is hooked: while this is set a call goes through to the game's own code

float screen_x(float pixel) { return (pixel - kPixelsLeft) / kPixelsX; }
float text_y(float cap_top, float size) { return (cap_top - (kSafeTop - kCapShiftPerSize * (size - 1.0f))) / kSafeHeight; }

std::string upper(std::string text) {
    for (char &ch : text) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return text;
}

bool pad_buttons(int slot, std::uint16_t &buttons) {
    ps2::PadSample sample;
    if (!ps2::read_pad_sample(slot, sample)) {
        buttons = 0;
        return false;
    }
    buttons = sample.buttons;
    return true;
}

Seat seat_of(int slot) {
    if (slot == static_cast<int>(kHostSlot)) return Seat::host;
    if (!ps2::netplay::roster_name(slot).empty()) return Seat::member;
    std::uint16_t buttons;
    if (!pad_buttons(slot, buttons)) return Seat::open;
    return (ps2::ld32(kClaimedMask) >> slot) & 1u ? Seat::member : Seat::claimable;
}

std::string name_of(int slot, Seat seat) { return seat == Seat::open ? "OPEN" : upper(ps2::netplay::player_name(slot)); }

std::uint32_t widget(int index) { return ps2::ld32(kWidgetTable + 4u * static_cast<std::uint32_t>(index)); }

std::uint32_t make_text(ps2::Cpu &c, float x, float cap_top, float size, std::uint32_t colour, std::uint32_t flags, const std::string &text) {
    const std::uint32_t w = allocate(c, kTextWidgetSize, kLine);
    guest(c, kTextConstruct, {w});
    ps2::st32(kTempPos, float_bits(x));
    ps2::st32(kTempPos + 4, 0);
    ps2::st32(kTempPos + 8, float_bits(text_y(cap_top, size)));
    ps2::st32(kTempPos + 12, float_bits(1.0f));
    guest(c, kFontSize, {kTempSize}, float_bits(size));
    vcall(c, w, 0x6C, {kTempPos, kTempSize, colour, 1, 1, 0, flags}, {kFrontEndFont});
    guest(c, kTextSetText, {w, put_text(kTempText, text)});
    return w;
}

void set_text(ps2::Cpu &c, std::uint32_t w, const std::string &text, std::uint32_t colour) {
    guest(c, kTextSetText, {w, put_text(kTempText, text)});
    ps2::st32(kTempPos, colour);
    guest(c, kTextSetColour, {w, kTempPos});
}

// What a row says on the right, and its colour.
std::pair<std::string, std::uint32_t> state_text(int slot, Seat seat, std::uint32_t grey, std::uint32_t dim, std::uint32_t red) {
    switch (seat) {
    case Seat::host: return {"HOST", red};
    case Seat::member: return (ps2::ld32(kReadyMask) >> slot) & 1u ? std::pair<std::string, std::uint32_t>{"READY", grey} : std::pair<std::string, std::uint32_t>{"NOT READY", dim};
    case Seat::claimable: return {"PRESS START", red};
    case Seat::open: break;
    }
    return {"", dim};
}

std::uint32_t shown_key() {
    std::uint32_t key = 0;
    for (int slot = 0; slot < kSlots; ++slot) {
        const Seat seat = seat_of(slot);
        key |= (static_cast<std::uint32_t>(seat) | ((ps2::ld32(kReadyMask) >> slot) & 1u) << 2) << (3 * slot);
    }
    return key;
}

void refresh_rows(ps2::Cpu &c) {
    const std::uint32_t grey = ps2::ld32(kColourGrey), dim = ps2::ld32(kColourDim), red = ps2::ld32(kColourRed);
    for (int slot = 0; slot < kSlots; ++slot) {
        const Seat seat = seat_of(slot);
        set_text(c, widget(kNames + slot), name_of(slot, seat), seat == Seat::open ? dim : grey);
        const auto [text, colour] = state_text(slot, seat, grey, dim, red);
        set_text(c, widget(kStates + slot), text, colour);
    }
    ps2::st32(kShown, shown_key());
}

int occupied() {
    int count = 0;
    for (int slot = 0; slot < kSlots; ++slot) count += seat_of(slot) != Seat::open;
    return count;
}

bool everyone_ready() {
    for (int slot = 0; slot < kSlots; ++slot)
        if (seat_of(slot) == Seat::member && !((ps2::ld32(kReadyMask) >> slot) & 1u)) return false;
    return true;
}

// The host starts: the game's own flow takes over. CONTINUE goes straight into the profile the launcher chose, or to the profile screens when it chose
// none; NEW GAME to the profile creation. With two or more players the game starts as its own two-player mode.
void begin(ps2::Cpu &c, std::uint32_t screen) {
    ps2::st32(kBegun, 1);
    guest(c, kSetTwoPlayerActive, {ps2::ld32(kGameStatePointer), occupied() >= 2 ? 1u : 0u});
    const bool continue_game = guest(c, kGridSelected, {ps2::ld32(screen + kScreenGrid)}) == 0;
    const ps2::netplay::SessionInfo *session = ps2::netplay::session();
    if (!continue_game) {
        ps2::st32(screen + kScreenResult, 2);
        play_cue(c, kCueAccept);
    } else if (session && session->profile_slot >= 0) {
        guest(c, kSetProfileSlot, {static_cast<std::uint32_t>(session->profile_slot)});
        g_call_original = true;
        guest(c, kContinueOnCommand, {screen, 4});
        g_call_original = false;
    } else {
        ps2::st32(screen + kScreenResult, 0);
        play_cue(c, kCueAccept);
    }
}

}

bool lobby_init(ps2::Cpu &c) {
    if (!coop_session()) return false;
    const std::uint32_t screen = c.r[4].w[0];
    if (ps2::ld32(screen + kScreenCreated) == 0) {
        for (std::uint32_t offset = 0; offset < kResetBytes; offset += 4) ps2::st32(kLobbyState + offset, 0);
        const std::uint32_t grey = ps2::ld32(kColourGrey), red = ps2::ld32(kColourRed), dim = ps2::ld32(kColourDim);
        const std::string host = upper(ps2::netplay::player_name(static_cast<int>(kHostSlot)));
        ps2::st32(kWidgetTable + 4u * kTitle, make_text(c, 0.0f, kTitleCapTop, kTitleSize, red, kTextLeft, "CO-OP"));
        ps2::st32(screen + kScreenText, make_text(c, 0.0f, kSubtitleCapTop, kSubtitleSize, grey, kTextLeft, host + "'S GAME"));
        for (int slot = 0; slot < kSlots; ++slot) {
            const float top = kRowCapTop + kRowStep * static_cast<float>(slot);
            ps2::st32(kWidgetTable + 4u * static_cast<std::uint32_t>(kTags + slot), make_text(c, 0.0f, top, kRowSize, kPlayerColours[slot], kTextLeft, std::to_string(slot + 1) + "P"));
            ps2::st32(kWidgetTable + 4u * static_cast<std::uint32_t>(kNames + slot), make_text(c, screen_x(kNamePx), top, kRowSize, grey, kTextLeft, "-"));
            ps2::st32(kWidgetTable + 4u * static_cast<std::uint32_t>(kStates + slot), make_text(c, screen_x(kStatePx), top, kRowSize, dim, kTextRight, "-"));
        }
        const std::uint32_t grid = allocate(c, kTextWidgetSize, kLine);
        guest(c, kGridConstruct, {grid});
        for (std::uint32_t i = 0; i < 4; ++i) ps2::st32(kTempSize + 4 * i, i == 0 ? 2 : 0);
        guest(c, kGridSetup, {grid, kTempSize, 1, screen + kScreenPad}, float_bits(text_y(kGridCapTop, kGridSize)));
        ps2::st32(grid + 0x8C, ps2::ld32(kLayoutFloats));
        ps2::st32(grid + 0x88, 1);
        ps2::st32(grid + 0x94, 5);
        const auto add = [&](std::uint32_t text, std::uint32_t colon_after, std::uint32_t code) {
            guest(c, kGridAddItem, {grid, text, colon_after, code, 4, 1, red, 6}, float_bits(kGridSize), {1});
        };
        add(guest(c, kGlobalStringGet, {kStringContinue}), 1, 0);
        add(put_text(kTempText, "NEW GAME"), 0, 2);
        vcall(c, grid, 0x94);
        ps2::st32(screen + kScreenGrid, grid);
        const std::uint32_t usage = allocate(c, 0x2D0, kLine);
        guest(c, kUsageConstruct, {usage});
        ps2::st32(kTempPos, ps2::ld32(kLayoutFloats));
        ps2::st32(kTempPos + 4, 0);
        ps2::st32(kTempPos + 8, float_bits(text_y(kUsageCapTop + kUsageShift, 1.0f)));
        ps2::st32(kTempPos + 12, float_bits(1.0f));
        guest(c, kUsageSetup, {usage, kTempPos, 1});
        guest(c, kUsageSetText, {usage, put_text(kTempText, "<COLOR B2B2B2FF><SIZE 0.9><X> <COLOR AA2B2BFF><SIZE 1.0>ready<CR>START begin<CR><COLOR B2B2B2FF><SIZE 0.9><T> <COLOR AA2B2BFF><SIZE 1.0>back")});
        ps2::st32(screen + kScreenUsage, usage);
        ps2::st32(screen + kScreenCreated, 1);
        ps2::st32(screen + 0x10, 0);
        refresh_rows(c);
        for (std::uint32_t i = 0; i < kTempTextBytes; i += 4) ps2::st32(kTempText + i, 0);
    }
    c.pc = c.r[31].w[0];
    return true;
}

void lobby_shutdown(ps2::Cpu &c) {
    const std::uint32_t screen = c.r[4].w[0];
    if (!coop_session() || ps2::ld32(screen + kScreenCreated) == 0) return;
    for (int i = 0; i < kWidgets; ++i) {
        const std::uint32_t w = widget(i);
        if (!w) continue;
        guest(c, kTextRelease, {w});
        guest(c, kDeleteText, {w, kTagFile, kLine});
        ps2::st32(kWidgetTable + 4u * static_cast<std::uint32_t>(i), 0);
    }
}

bool lobby_command(ps2::Cpu &c) {
    constexpr std::uint32_t kAccept = 4;
    if (!coop_session() || g_call_original || c.r[5].w[0] != kAccept) return false;
    c.r[2].sd[0] = 1;
    c.pc = c.r[31].w[0];
    return true;
}

void lobby_update(ps2::Cpu &c) {
    const std::uint32_t screen = c.r[4].w[0];
    if (!coop_session() || ps2::ld32(screen + kScreenCreated) == 0 || ps2::ld32(kBegun)) return;
    ps2::netplay::report_lobby(occupied());
    ps2::st32(kFrames, ps2::ld32(kFrames) + 1);
    std::uint16_t host_buttons = 0;
    for (int slot = 0; slot < kSlots; ++slot) {
        std::uint16_t buttons;
        pad_buttons(slot, buttons);
        const std::uint32_t at = kPrevButtons + 2u * static_cast<std::uint32_t>(slot);
        const std::uint16_t pressed = static_cast<std::uint16_t>(buttons & ~ps2::ld16(at));
        ps2::st16(at, buttons);
        const Seat seat = seat_of(slot);
        if (slot == static_cast<int>(kHostSlot)) host_buttons = buttons;
        if (seat == Seat::member && (pressed & kCrossBit)) {
            ps2::st32(kReadyMask, ps2::ld32(kReadyMask) ^ (1u << slot));
            play_cue(c, kCueAccept);
        } else if (seat == Seat::claimable && (pressed & kStartBit)) {
            ps2::st32(kClaimedMask, ps2::ld32(kClaimedMask) | (1u << slot));
            play_cue(c, kCueAccept);
        }
    }
    if (shown_key() != ps2::ld32(kShown)) refresh_rows(c);
    const std::uint32_t phase = ps2::ld32(kFrames) % kBlinkPeriod;
    for (int slot = 0; slot < kSlots; ++slot) guest(c, kTextSetVisible, {widget(kStates + slot), seat_of(slot) == Seat::claimable && phase >= kBlinkPeriod / 2 ? 0u : 1u});
    if (host_buttons & kStartBit) {
        const std::uint32_t held = ps2::ld32(kStartHeld) + 1;
        ps2::st32(kStartHeld, held);
        if (everyone_ready() || held >= kForceStartFrames) begin(c, screen);
        else if (held == 1) play_cue(c, kCueRefused);
    } else {
        ps2::st32(kStartHeld, 0);
    }
}

void lobby_render(ps2::Cpu &c) {
    const std::uint32_t screen = c.r[4].w[0];
    if (!coop_session() || ps2::ld32(screen + kScreenCreated) == 0 || vcall(c, screen, 0x4C) == 0) return;
    for (int i = 0; i < kWidgets; ++i)
        if (widget(i)) vcall(c, widget(i), 0x3C);
}

}
