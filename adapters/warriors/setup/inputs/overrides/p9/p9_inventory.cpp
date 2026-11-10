#include "p9_inventory.hpp"

#include "p9_game.hpp"
#include "p9_hooks.hpp"
#include "ps2/system.hpp"

#include <algorithm>

// The game's inventory holds two live blocks and two checkpoint copies, sized for two players, with the callback names right behind them. Slots 2 and 3
// keep their blocks in the phase 9 area (p9_game.hpp) and follow the same rules, written out from the decompile of the original functions:
// loadout_amount 0x0041e420, loadout_has_item 0x0041e3d8, loadout_give_item 0x0041e5b0, loadout_set_amount 0x0041e9d8, loadout_clear_for_player
// 0x0041ec60, the checkpoint pair 0x0041e0b8 / 0x0041e168, loadout_empty 0x0041e040, loadout_init 0x0041df40, loadout_set_item 0x0041e250.
// Slots 0 and 1 never reach the accessors' code here: a hook that does not own the slot returns false and the original function runs untouched.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kItemFlash = 1, kItemMoney = 2, kItemSprayPaint = 3;
constexpr std::uint32_t kLimitTable = 0x0058B300;                 // per item: minimum, maximum (8 bytes)
constexpr std::uint32_t kUnlockIsSet = 0x00424130, kUnlockManager = 0x006FE998;
constexpr std::uint32_t kUpgradeGroup = 6, kUpgradeFlashRaised = 7;
constexpr std::uint32_t kFlashCountAt = 0x50;                     // the flash entry's count, from the block start
constexpr std::int32_t kFlashRaisedCap = 4;
constexpr std::uint32_t kEntryCountAt = 0x20;
constexpr std::uint64_t kHintSprayFirst = 0x100000000ull, kHintSprayQueued = 0x200000000ull, kHintSprayWithdrawn = 0x400200000ull;   // game state flags at +0
constexpr std::uint32_t kHintBoxRetract = 0x001CE718, kHintBox = 0x609250, kSprayHint = 0x11;
constexpr std::uint32_t kCallbackMoney = 0x1034, kCallbackItem = 0xFD4, kCallbackHuman = 0xFF4;   // callback names, from the inventory object
constexpr std::uint32_t kLuaObjectPointer = 0x00512B04;
constexpr std::uint32_t kLuaExists = 0x48, kLuaPush = 0x68, kLuaCall = 0x88;   // vtable slot pairs: this adjustment (s16) at +n, function at +n+4
constexpr std::uint32_t kNoArgument = 0xFFFFFFFFu;

// A slot is served here only while its player is in the game; for any other slot the original function answers, as it always did.
bool extra_slot(std::uint32_t slot) { return slot >= kOriginalPlayers && slot < kMaxPlayers && slot < player_count(); }
std::uint32_t live_block(std::uint32_t slot) { return kInventoryLive + (slot - kOriginalPlayers) * kInvBlockSize; }
std::uint32_t checkpoint_block(std::uint32_t slot) { return kInventoryCheckpoint + (slot - kOriginalPlayers) * kInvBlockSize; }
std::uint32_t entry_of(std::uint32_t block, std::uint32_t item) { return block + 4 + item * kInvEntrySize; }
std::uint32_t count_at(std::uint32_t block, std::uint32_t item) { return entry_of(block, item) + kEntryCountAt; }
std::int32_t sld(std::uint32_t at) { return static_cast<std::int32_t>(ps2::ld32(at)); }

void ret(ps2::Cpu &c, std::uint32_t value) {
    c.r[2].sd[0] = static_cast<std::int32_t>(value);
    c.pc = c.r[31].w[0];
}

// The Lua virtual machine's calls the money and item callbacks go through (vtable of *0x00512b04): exists(name), push(int), call(args, results).
std::uint32_t lua_call_slot(ps2::Cpu &c, std::uint32_t slot, std::uint32_t a1, std::uint32_t a2) {
    const std::uint32_t object = ps2::ld32(kLuaObjectPointer);
    const std::uint32_t table = ps2::ld32(object);
    const std::int32_t adjust = static_cast<std::int16_t>(ps2::ld16(table + slot));
    return ps2::call_guest(c, ps2::ld32(table + slot + 4), object + static_cast<std::uint32_t>(adjust), a1, a2);
}

// A script callback named at `name` with one or two integer arguments, when it is set and the script defines it.
void call_callback(ps2::Cpu &c, std::uint32_t name, std::uint32_t first, std::uint32_t second) {
    if (!ps2::ld8(name) || !lua_call_slot(c, kLuaExists, name, 0)) return;
    lua_call_slot(c, kLuaPush, first, 0);
    if (second != kNoArgument) lua_call_slot(c, kLuaPush, second, 0);
    lua_call_slot(c, kLuaCall, second == kNoArgument ? 1 : 2, 0);
}

// What the limits table and the flash upgrade make of a count after it changed (the tail loadout_give_item and loadout_set_amount share).
void clamp_count(ps2::Cpu &c, std::uint32_t block, std::uint32_t item) {
    const std::uint32_t at = count_at(block, item);
    if (sld(at) < 0) ps2::st32(at, 0);
    const bool upgraded = ps2::call_guest(c, kUnlockIsSet, kUnlockManager, kUpgradeGroup, kUpgradeFlashRaised) != 0;
    const std::int32_t minimum = sld(kLimitTable + item * 8), maximum = sld(kLimitTable + item * 8 + 4);
    std::int32_t result;
    if (!upgraded || item != kItemFlash) {
        const std::int32_t count = sld(at);
        const bool keep = minimum < count ? count < maximum : minimum < maximum;
        result = keep ? std::max(minimum, count) : maximum;
    } else if (sld(block + kFlashCountAt) > 3) {
        result = kFlashRaisedCap;
    } else {
        result = std::max(minimum, sld(block + kFlashCountAt));
    }
    ps2::st32(at, result < 0 ? 0u : static_cast<std::uint32_t>(result));
}

// The first spray can raises the hint flags (and a queued hint is withdrawn once more are collected).
void spray_paint_hints(ps2::Cpu &c) {
    const std::uint32_t flags = game();
    const std::uint64_t value = ps2::ld64(flags);
    if (!(value & kHintSprayFirst)) {
        ps2::st64(flags, value | kHintSprayFirst);
    } else if (value & kHintSprayQueued) {
        ps2::call_guest(c, kHintBoxRetract, kHintBox, kSprayHint);
        ps2::st64(flags, ps2::ld64(flags) | kHintSprayWithdrawn);
    }
}

}

// A joining player's inventory starts as the original makes one for player 2: the items' names, sounds and durations as the level configured them (slot 1's), no
// counts. Both the live block and its checkpoint copy.
void inventory_init_slot(std::uint32_t slot) {
    const std::uint32_t source = game() + kInventoryOffset + kInvBlockSize;
    for (std::uint32_t i = 0; i < kInvBlockSize; i += 4) ps2::st32(live_block(slot) + i, ps2::ld32(source + i));
    for (std::uint32_t item = 0; item < kInvItems; ++item) ps2::st32(count_at(live_block(slot), item), 0);
    for (std::uint32_t i = 0; i < kInvBlockSize; i += 4) ps2::st32(checkpoint_block(slot) + i, ps2::ld32(live_block(slot) + i));
}

bool loadout_amount(ps2::Cpu &c) {
    const std::uint32_t slot = c.r[5].w[0], item = c.r[6].w[0];
    if (!extra_slot(slot)) return false;
    ret(c, item < kInvItems ? ps2::ld32(count_at(live_block(slot), item)) : 0);
    return true;
}

bool loadout_has_item(ps2::Cpu &c) {
    const std::uint32_t slot = c.r[5].w[0], item = c.r[6].w[0];
    if (!extra_slot(slot)) return false;
    ret(c, item < kInvItems && sld(count_at(live_block(slot), item)) > 0);
    return true;
}

bool loadout_give_item(ps2::Cpu &c) {
    const std::uint32_t inventory = c.r[4].w[0], slot = c.r[5].w[0], item = c.r[6].w[0];
    const std::int32_t amount = static_cast<std::int32_t>(c.r[7].w[0]);
    const bool notify = c.r[8].sd[0] == 1;
    if (!extra_slot(slot)) return false;
    c.pc = c.r[31].w[0];
    if (item >= kInvItems) return true;
    const std::uint32_t block = live_block(slot);
    ps2::st32(count_at(block, item), static_cast<std::uint32_t>(sld(count_at(block, item)) + amount));
    clamp_count(c, block, item);
    if (item == kItemSprayPaint && amount > 0) spray_paint_hints(c);
    if (item == kItemMoney) call_callback(c, inventory + kCallbackMoney, slot, static_cast<std::uint32_t>(amount));
    if (notify) {
        call_callback(c, inventory + kCallbackItem, item, kNoArgument);
        call_callback(c, inventory + kCallbackHuman, slot, item);
    }
    return true;
}

bool loadout_set_amount(ps2::Cpu &c) {
    const std::uint32_t slot = c.r[5].w[0], item = c.r[6].w[0];
    const std::int32_t value = static_cast<std::int32_t>(c.r[7].w[0]);
    if (!extra_slot(slot)) return false;
    c.pc = c.r[31].w[0];
    if (item >= kInvItems) return true;
    const std::uint32_t block = live_block(slot);
    ps2::st32(count_at(block, item), value < 0 ? 0u : static_cast<std::uint32_t>(value));
    clamp_count(c, block, item);
    if (item == kItemSprayPaint && value > 0) spray_paint_hints(c);
    return true;
}

bool loadout_clear_for_player(ps2::Cpu &c) {
    const std::uint32_t slot = c.r[5].w[0];
    if (!extra_slot(slot)) return false;
    for (std::uint32_t item = 0; item < kInvItems; ++item) {
        ps2::st32(count_at(live_block(slot), item), 0);
        ps2::st32(count_at(checkpoint_block(slot), item), 0);
    }
    ret(c, slot);
    return true;
}

// The checkpoint pair and the reset keep the extra blocks in step (only once a third player exists: until then the extra RAM stays untouched).
bool loadout_snapshot_store(ps2::Cpu &) {
    for (std::uint32_t slot = kOriginalPlayers; slot < player_count(); ++slot)
        for (std::uint32_t i = 0; i < kInvBlockSize; i += 4) ps2::st32(checkpoint_block(slot) + i, ps2::ld32(live_block(slot) + i));
    return false;
}

bool loadout_snapshot_load(ps2::Cpu &) {
    for (std::uint32_t slot = kOriginalPlayers; slot < player_count(); ++slot)
        for (std::uint32_t i = 0; i < kInvBlockSize; i += 4) ps2::st32(live_block(slot) + i, ps2::ld32(checkpoint_block(slot) + i));
    return false;
}

bool loadout_empty(ps2::Cpu &c) {
    if (c.r[5].w[0] == 0) return false;
    for (std::uint32_t slot = kOriginalPlayers; slot < player_count(); ++slot)
        for (std::uint32_t item = 0; item < kInvItems; ++item) ps2::st32(count_at(live_block(slot), item), 0);
    return false;
}

}
