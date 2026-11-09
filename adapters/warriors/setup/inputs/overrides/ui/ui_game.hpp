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
constexpr std::uint32_t kGlobalStringGet = 0x0019EE70;        // GlobalString_Get(id) -> char *
constexpr std::uint32_t kPlayCue = 0x0010FC30;                // Sound_PlayInterfaceCue(*kCueTablePointer, id)
constexpr std::uint32_t kFontSize = 0x00179808;               // Font_Size(f12 = scale, out[16])
constexpr std::uint32_t kTextConstruct = 0x001CCF88;          // TextWidget_Construct(widget)
constexpr std::uint32_t kTextRelease = 0x001CD188;            // TextWidget_Release(widget)
constexpr std::uint32_t kTextSetVisible = 0x001CD1C0;         // TextWidget_SetVisible(widget, flag)
constexpr std::uint32_t kTextSetColour = 0x001CD1C8;          // TextWidget_SetColour(widget, &rgba)
constexpr std::uint32_t kTextSetText = 0x001CD1E0;            // TextWidget_SetText(widget, char *) (copies)
constexpr std::uint32_t kDeleteText = 0x004E44E0;             // SafeDelete_CreditsShutdown(widget, file, line): the game's delete of a TextWidget
constexpr std::uint32_t kGridConstruct = 0x001D3EF0;          // OptionGrid_Construct(grid)
constexpr std::uint32_t kGridSetup = 0x001D4110;              // OptionGrid_Setup(grid, rows[16], 1, pad, f12 = y)
constexpr std::uint32_t kGridAddItem = 0x001D4230;            // OptionGrid_AddItem(grid, text, colon_after, code, 4, 1, colour, font, [sp] 1, f12 = size): colon_after 1 draws " :" after the item (the first of a row)
constexpr std::uint32_t kGridSelected = 0x001D43E8;           // OptionGrid_GetSelected(grid)
constexpr std::uint32_t kGridItemCode = 0x001D43F0;           // OptionGrid_GetItemCode(grid, index)
constexpr std::uint32_t kUsageConstruct = 0x001CEA70;         // UsageInfo_Construct(usage)
constexpr std::uint32_t kUsageSetup = 0x001CEB40;             // UsageInfo_Setup(usage, &pos, 1)
constexpr std::uint32_t kUsageSetText = 0x001CEC28;           // UsageInfo_SetText(usage, char *)
constexpr std::uint32_t kFlowGetTop = 0x001C8628;             // ScreenFlowController_GetTop(controller) -> screen + 0x70
constexpr std::uint32_t kSetTwoPlayerActive = 0x00419AC0;     // GameState_SetTwoPlayerActive(game, flag)
constexpr std::uint32_t kSetProfileSlot = 0x00204078;         // PM_SetProfileSlot(slot)
constexpr std::uint32_t kHudBindPad = 0x001AE8D8;             // HUD_BindPad(pads, virtual pad, physical pad): the pad the front end listens to
constexpr std::uint32_t kContinueOnCommand = 0x00203800;      // PM_Continue_OnCommand(screen, command)

// Game data.
constexpr std::uint32_t kLevelNameOffset = 0x124, kLevelNameBytes = 32, kLevelIndexOffset = 0x56DC;   // the level being loaded, a string in the game state
constexpr std::uint32_t kHudPads = 0x00600840;                // the HUD's table of virtual pads
constexpr std::uint32_t kHeapPointer = 0x005127E4;            // the heap object every widget is allocated from
constexpr std::uint32_t kCueTablePointer = 0x0050AA84;
constexpr std::uint32_t kGameStatePointer = 0x0051489C;
constexpr std::uint32_t kLayoutFloats = 0x0050F5C4;           // PM_PickLayout's table: x, then the grid and title rows, usage row at +0x24
constexpr std::uint32_t kTagWidget = 0x00558498, kTagFile = 0x005584A8;   // names the heap records for its debugging; any string will do
constexpr std::uint32_t kColourRed = 0x005FD328, kColourGrey = 0x005FD310, kColourDim = 0x005FD320;   // front-end colours (words in memory, ABGR)

// The game's player list: the count and the handles in the game state, each human's record (the pad record it is bound to at +0x19, -1 for none).
constexpr std::uint32_t kPlayerCountOffset = 0x224, kPlayerHandlesOffset = 0x228, kHandleResolve = 0x00390268;
constexpr std::uint32_t kPlayerRecords = 0x00660F50, kPlayerRecordBytes = 0x2C, kHumanRecordIndex = 0x92, kRecordPad = 0x19;
constexpr std::uint32_t kPlayerSlots = 4;

// Cues of Sound_PlayInterfaceCue.
constexpr std::uint32_t kCueAccept = 9, kCueRefused = 0xE;

// PM screen objects (the flow controller's fields) and the PM_Continue object.
constexpr std::uint32_t kControllerGreet = 0x64, kControllerContinue = 0x88, kControllerCreate = 0x80, kControllerProfile = 0x7C;
constexpr std::uint32_t kScreenSubobject = 0x70;              // the flow stores screen + 0x70
constexpr std::uint32_t kScreenCreated = 0x0C, kScreenResult = 0x74, kScreenGrid = 0x8C, kScreenUsage = 0x90, kScreenText = 0x94, kScreenPad = 0x50;

// Text widget flags (TextWidget_Setup's last argument): 4 is the front end's spacing, plus 1 right-aligned.
constexpr std::uint32_t kTextLeft = 4, kTextRight = 5;
constexpr std::uint32_t kTextWidgetSize = 0xD0;
constexpr std::uint32_t kFrontEndFont = 6;     // TextWidget_Setup's ninth argument (on the stack): big_font

// Words of guest RAM the front end keeps (they are in every snapshot and digest): the bytes above the main thread's stack, after the seats' words
// (coop_game.hpp kScratch + 0x00 to 0x7F).
constexpr std::uint32_t kScratch = 0x01FFFE00;
constexpr std::uint32_t kLobbyState = kScratch + 0x80;        // 0x30 bytes: the lobby's words (ui_lobby.cpp, cleared when it opens) and the front end's own (below)
constexpr std::uint32_t kSkipNextMovie = kLobbyState + 0x24;   // 1 between Movie_Play and BinkMovie_Play of a movie to skip, ui_boot.cpp
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
    const std::uint32_t human = guest(c, kHandleResolve, {game + kPlayerHandlesOffset + slot * 4});
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

inline void play_cue(ps2::Cpu &c, std::uint32_t cue) { guest(c, kPlayCue, {ps2::ld32(kCueTablePointer), cue}); }

}
