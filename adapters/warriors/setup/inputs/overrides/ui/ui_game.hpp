#pragma once

#include "ps2/generated.hpp"
#include "ps2/guest_call.hpp"
#include "ps2/memory.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>

// The Warriors (SLUS_212.15) front end as the co-op menu and lobby use it: addresses from the decompile export (decomp\index.tsv, the PM_* functions in
// funcs\201b50.c, 204a78.c, 208320.c) and the helpers that build and drive the game's own widgets through call_guest. Local only, like the generated code.
namespace warriors::ui {

// Guest functions.
constexpr std::uint32_t kLocalizedTextGet = 0x0019EE70;        // localized_text_get(id) -> char *
constexpr std::uint32_t kPlayMenuCue = 0x0010FC30;                // play_menu_cue(*kCueTablePointer, id)
constexpr std::uint32_t kTextMeasure = 0x00179808;               // text_measure(f12 = scale, out[16])
constexpr std::uint32_t kLabelInit = 0x001CCF88;          // label_init(widget)
constexpr std::uint32_t kLabelFree = 0x001CD188;            // label_free(widget)
constexpr std::uint32_t kLabelShow = 0x001CD1C0;         // label_show(widget, flag)
constexpr std::uint32_t kLabelTint = 0x001CD1C8;          // label_tint(widget, &rgba)
constexpr std::uint32_t kLabelTextSet = 0x001CD1E0;            // label_text_set(widget, char *) (copies)
constexpr std::uint32_t kDeleteChecked = 0x004E44E0;             // delete_checked(widget, file, line): the game's delete of a TextWidget
constexpr std::uint32_t kOptionListInit = 0x001D3EF0;          // option_list_init(grid)
constexpr std::uint32_t kOptionListConfigure = 0x001D4110;              // option_list_configure(grid, rows[16], 1, pad, f12 = y)
constexpr std::uint32_t kOptionListAddRow = 0x001D4230;            // option_list_add_row(grid, text, colon_after, code, 4, 1, colour, font, [sp] 1, f12 = size): colon_after 1 draws " :" after the item (the first of a row)
constexpr std::uint32_t kOptionListSelected = 0x001D43E8;           // option_list_selected(grid)
constexpr std::uint32_t kOptionListRowCode = 0x001D43F0;           // option_list_row_code(grid, index)
constexpr std::uint32_t kHintRowInit = 0x001CEA70;         // hint_row_init(usage)
constexpr std::uint32_t kHintRowConfigure = 0x001CEB40;             // hint_row_configure(usage, &pos, 1)
constexpr std::uint32_t kHintRowText = 0x001CEC28;           // hint_row_text(usage, char *)
constexpr std::uint32_t kScreenStackTop = 0x001C8628;             // screen_stack_top(controller) -> screen + 0x70
constexpr std::uint32_t kSessionSetTwoPlayer = 0x00419AC0;     // session_set_two_player(game, flag)
constexpr std::uint32_t kProfileSlotSelect = 0x00204078;         // profile_slot_select(slot)
constexpr std::uint32_t kOverlayPadAssign = 0x001AE8D8;             // overlay_pad_assign(pads, virtual pad, physical pad): the pad the front end listens to
constexpr std::uint32_t kContinuePageCommand = 0x00203800;      // continue_page_command(screen, command)

// Game data.
constexpr std::uint32_t kLevelNameOffset = 0x124, kLevelNameBytes = 32, kLevelIndexOffset = 0x56DC;   // the level being loaded, a string in the game state
constexpr std::uint32_t kHudPads = 0x00600840;                // the HUD's table of virtual pads
constexpr std::uint32_t kHeapPointer = 0x005127E4;            // the heap object every widget is allocated from
constexpr std::uint32_t kCueTablePointer = 0x0050AA84;
constexpr std::uint32_t kGameStatePointer = 0x0051489C;
constexpr std::uint32_t kLayoutFloats = 0x0050F5C4;           // page_layout_choose's table: x, then the grid and title rows, usage row at +0x24
constexpr std::uint32_t kTagWidget = 0x00558498, kTagFile = 0x005584A8;   // names the heap records for its debugging; any string will do
constexpr std::uint32_t kColourRed = 0x005FD328, kColourGrey = 0x005FD310, kColourDim = 0x005FD320;   // front-end colours (words in memory, ABGR)

// The game's player list: the count and the handles in the game state, each human's record (the pad record it is bound to at +0x19, -1 for none).
constexpr std::uint32_t kPlayerCountOffset = 0x224, kPlayerHandlesOffset = 0x228, kHandleToObject = 0x00390268;
constexpr std::uint32_t kPlayerRecords = 0x00660F50, kPlayerRecordBytes = 0x2C, kHumanRecordIndex = 0x92, kRecordPad = 0x19;
constexpr std::uint32_t kPlayerSlots = 4;

// Cues of play_menu_cue.
constexpr std::uint32_t kCueAccept = 9, kCueRefused = 0xE;

// PM screen objects (the flow controller's fields) and the PM_Continue object.
constexpr std::uint32_t kControllerGreet = 0x64, kControllerContinue = 0x88, kControllerCreate = 0x80, kControllerProfile = 0x7C;
constexpr std::uint32_t kScreenSubobject = 0x70;              // the flow stores screen + 0x70
constexpr std::uint32_t kScreenCreated = 0x0C, kScreenResult = 0x74, kScreenGrid = 0x8C, kScreenUsage = 0x90, kScreenText = 0x94, kScreenPad = 0x50;

// Text widget flags (label_configure's last argument): 4 is the front end's spacing, plus 1 right-aligned.
constexpr std::uint32_t kTextLeft = 4, kTextRight = 5;
constexpr std::uint32_t kTextWidgetSize = 0xD0;
constexpr std::uint32_t kFrontEndFont = 6;     // label_configure's ninth argument (on the stack): big_font

// Words of guest RAM the front end keeps (they are in every snapshot and digest): the bytes above the main thread's stack, after the seats' words
// (coop_game.hpp kScratch + 0x00 to 0x7F).
constexpr std::uint32_t kScratch = 0x01FFFE00;
constexpr std::uint32_t kLobbyState = kScratch + 0x80;        // 0x30 bytes: the lobby's words (ui_lobby.cpp, cleared when it opens) and the front end's own (below)
constexpr std::uint32_t kSkipNextMovie = kLobbyState + 0x24;   // 1 between video_play_clip and video_session_run of a movie to skip, ui_boot.cpp
constexpr std::uint32_t kAutoEntry = kLobbyState + 0x28;       // the launcher's boot into the lobby: 0 not started, 1 the title is passed, 2 the menu is passed, ui_front.cpp
constexpr std::uint32_t kWidgetTable = kScratch + 0xB0;       // 20 widget pointers
constexpr std::uint32_t kTemp = kScratch + 0x100;             // pos[16], size[16], text[0x60]
constexpr std::uint32_t kTempPos = kTemp, kTempSize = kTemp + 0x10, kTempText = kTemp + 0x20, kTempTextBytes = 0xE0;

inline std::uint32_t signed_half(std::uint32_t address) { return static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(ps2::ld16(address)))); }

// A guest call with up to eight register arguments, the float in f12 and the stack words.
inline std::uint32_t guest(ps2::Cpu &c, std::uint32_t function, std::initializer_list<std::uint32_t> args = {}, std::uint32_t f12_bits = 0,
                           std::initializer_list<std::uint32_t> stack = {}) {
    ps2::GuestArgs call;
    unsigned i = 0;
    for (std::uint32_t a : args) call.a[i++] = a;
    i = 0;
    for (std::uint32_t s : stack) call.stack[i++] = s;
    call.f12 = f12_bits;
    return ps2::call_guest(c, function, call);
}

// A virtual method of a widget: the vtable at the object's start, the method at `slot`, this-adjustment at slot - 4.
inline std::uint32_t vcall(ps2::Cpu &c, std::uint32_t object, std::uint32_t slot, std::initializer_list<std::uint32_t> rest = {},
                           std::initializer_list<std::uint32_t> stack = {}) {
    const std::uint32_t table = ps2::ld32(object);
    ps2::GuestArgs call;
    call.a[0] = object + signed_half(table + slot - 4);
    unsigned i = 1;
    for (std::uint32_t a : rest) call.a[i++] = a;
    i = 0;
    for (std::uint32_t s : stack) call.stack[i++] = s;
    return ps2::call_guest(c, ps2::ld32(table + slot), call);
}

inline std::uint32_t float_bits(float value) {
    std::uint32_t bits;
    __builtin_memcpy(&bits, &value, sizeof bits);
    return bits;
}

inline float layout_float(std::uint32_t offset) {
    const std::uint32_t bits = ps2::ld32(kLayoutFloats + offset);
    float value;
    __builtin_memcpy(&value, &bits, sizeof value);
    return value;
}

// Copies `text` into guest memory at `address` (NUL-terminated, cut to `room` bytes) and returns the address.
inline std::uint32_t put_text(std::uint32_t address, const std::string &text, std::uint32_t room = kTempTextBytes) {
    const std::uint32_t n = static_cast<std::uint32_t>(text.size() < room - 1 ? text.size() : room - 1);
    for (std::uint32_t i = 0; i < n; ++i) ps2::st8(address + i, static_cast<std::uint8_t>(text[i]));
    ps2::st8(address + n, 0);
    return address;
}

// The pad record the player in list slot `slot` is bound to, -1 for a slot without a player or a pad.
inline int player_pad(ps2::Cpu &c, std::uint32_t slot) {
    const std::uint32_t game = ps2::ld32(kGameStatePointer);
    if (!game || slot >= kPlayerSlots) return -1;
    const std::uint32_t human = guest(c, kHandleToObject, {game + kPlayerHandlesOffset + slot * 4});
    return human ? static_cast<std::int8_t>(ps2::ld8(kPlayerRecords + signed_half(human + kHumanRecordIndex) * kPlayerRecordBytes + kRecordPad)) : -1;
}

// A new block from the game's heap (the allocator every PM screen uses).
inline std::uint32_t allocate(ps2::Cpu &c, std::uint32_t bytes, std::uint32_t line) {
    const std::uint32_t heap = ps2::ld32(kHeapPointer), table = ps2::ld32(heap);
    ps2::GuestArgs call;
    call.a[0] = heap + signed_half(table + 0x50);
    call.a[1] = kTagWidget;
    call.a[2] = bytes;
    call.a[3] = 16;
    call.a[4] = kTagFile;
    call.a[5] = line;
    return ps2::call_guest(c, ps2::ld32(table + 0x54), call);
}

inline void play_cue(ps2::Cpu &c, std::uint32_t cue) { guest(c, kPlayMenuCue, {ps2::ld32(kCueTablePointer), cue}); }

}
