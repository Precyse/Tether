#include "p9_flow.hpp"

#include "p9_game.hpp"
#include "p9_world.hpp"
#include "ps2/system.hpp"

// Player boxes (the level scripts' trigger zones that only a player can set off: rail cameras, fixed cameras, jump hints, chase starts). PlayerBox_Update visits the two
// handles at game + 0x228 and + 0x22c and keeps two occupants per box, so players 3 and 4 never set one off. They are visited here with the same rules (enter message 3, still
// inside message 5 once per repeat period, left message 4), their occupancy in a table of its own beside the box pool.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kPlayerBoxUpdate = 0x004134F8, kBoxContainsObject = 0x004129C0, kHasStateFlag100000000 = 0x00227EB0;
constexpr std::uint32_t kBoxPool = 0x00514814, kBoxSize = 0x100;                   // PlayerBoxPool_Create: the pool object holds the box array and the count
constexpr std::uint32_t kBoxEnabledOffset = 0x68, kBoxNextMessageOffset = 0xF8;   // the box's enabled byte; the time of the next message 5
constexpr std::uint32_t kGameTimer = 0x0050B734, kTimerNowOffset = 0x48;
constexpr std::uint32_t kHandlerCallOffset = 0x44, kHandlerAdjustOffset = 0x40;   // vtable: the message handler and the s16 `this` adjustment before it
constexpr std::uint32_t kMessageIdOffset = 0x20, kMessageHumanOffset = 0x24;
constexpr std::uint32_t kEntered = 3, kLeft = 4, kInside = 5;
constexpr std::uint32_t kOccupantBytes = 8;

bool g_in_update = false;

// The slot of the table for a box of the pool, or kPlayerBoxLimit when the address is none of the pool's.
std::uint32_t box_index(std::uint32_t box) {
    const std::uint32_t pool = ps2::ld32(kBoxPool);
    if (!pool) return kPlayerBoxLimit;
    const std::uint32_t first = ps2::ld32(pool);
    const std::uint32_t index = (box - first) / kBoxSize;
    return box >= first && index < ps2::ld32(pool + 8) && index < kPlayerBoxLimit ? index : kPlayerBoxLimit;
}

void send(ps2::Cpu &c, std::uint32_t box, std::uint32_t message, std::uint32_t human) {
    for (std::uint32_t at = 0; at < kPlayerBoxMessageBytes; at += 4) ps2::st32(kPlayerBoxMessage + at, 0);
    ps2::st32(kPlayerBoxMessage + kMessageIdOffset, message);
    ps2::st32(kPlayerBoxMessage + kMessageHumanOffset, human);
    const std::uint32_t table = ps2::ld32(box);
    const std::uint32_t self = box + static_cast<std::uint32_t>(static_cast<std::int16_t>(ps2::ld16(table + kHandlerAdjustOffset)));
    ps2::call_guest(c, ps2::ld32(table + kHandlerCallOffset), self, kPlayerBoxMessage);
}

// One player of list slot 2 or 3 against one box: the body of PlayerBox_Update's loop. `due` is whether the repeat period of the box had passed before this update.
void visit(ps2::Cpu &c, std::uint32_t box, std::uint32_t slot, bool due) {
    const std::uint32_t occupant = kPlayerBoxOccupants + box_index(box) * kOccupantBytes + (slot - kOriginalPlayers) * 4;
    const std::uint32_t handle = ps2::ld32(game() + kPlayerHandlesOffset + slot * 4);
    const std::uint32_t human = resolve_handle(c, handle);
    if (!human) return;
    const bool inside = !ps2::call_guest(c, kHasStateFlag100000000, human) && ps2::call_guest(c, kBoxContainsObject, box, human) == 1;
    const bool known = ps2::ld32(occupant) == handle;
    if (inside && !known) {
        ps2::st32(occupant, handle);
        send(c, box, kEntered, human);
    } else if (inside && due) {
        send(c, box, kInside, human);
    } else if (!inside && known) {
        ps2::st32(occupant, 0);
        send(c, box, kLeft, human);
    }
}

}

bool player_box_update(ps2::Cpu &c) {
    const std::uint32_t box = c.r[4].w[0];
    if (g_in_update || player_count() <= kOriginalPlayers || box_index(box) == kPlayerBoxLimit || !ps2::ld8(box + kBoxEnabledOffset)) return false;
    const bool due = ps2::ld32(ps2::ld32(kGameTimer) + kTimerNowOffset) > ps2::ld32(box + kBoxNextMessageOffset);
    g_in_update = true;
    ps2::call_guest(c, kPlayerBoxUpdate, box);
    for (std::uint32_t slot = kOriginalPlayers; slot < player_count() && ps2::ld8(box + kBoxEnabledOffset); ++slot) visit(c, box, slot, due);
    g_in_update = false;
    c.pc = c.r[31].w[0];
    return true;
}

void player_boxes_reset(ps2::Cpu &) {
    for (std::uint32_t at = 0; at < kPlayerBoxLimit * kOccupantBytes; at += 4) ps2::st32(kPlayerBoxOccupants + at, 0);
}

}
