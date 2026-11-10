#include "ai_game.hpp"
#include "ai_hooks.hpp"

#include "ps2/system.hpp"

// Mission objectives that name the players one by one. The scripts register their mugging callback on `player` and `player2` (HuSetMugCallback: the Lua function a human calls
// at the end of every mugging, stored in the human at +0x5a4), so a mugging by a third or fourth player would never count towards the bonus of the riot mission or the boss
// fight of level 20. Players 3 and 4 take the callback of player 2 as soon as they are in the list. The rank unlock of global.lua adds StatGetScore(player) and StatGetScore(player2):
// with three or four players the answer for player 2 is the score of players 2 to 4. The money and item callbacks are the inventory's (CfgMoneyCallback, CfgInventoryCallback: one name
// for all players, called with the real player number by p9_inventory.cpp), and tags are counted by the tag flag's handler, not by player.
namespace warriors::ai {

namespace {

constexpr std::uint32_t kMugCallbackOffset = 0x5A4;
constexpr std::uint32_t kSecondPlayer = 1;
constexpr std::uint32_t kStatsObject = 0x006FE490, kScoreValueOf = 0x00422998;   // score_value_of(stats, human): the running score of the human's record (p9_stats.cpp serves panels 2 and 3)

std::uint32_t player_human(ps2::Cpu &c, std::uint32_t slot) {
    return p9::resolve_handle(c, ps2::ld32(p9::game() + p9::kPlayerHandlesOffset + slot * 4));
}

}

void share_mug_callback(ps2::Cpu &c) {
    if (players() < kScaledFrom) return;
    const std::uint32_t second = player_human(c, kSecondPlayer);
    if (!second) return;
    for (std::uint32_t slot = kSecondPlayer + 1; slot < static_cast<std::uint32_t>(players()); ++slot) {
        const std::uint32_t human = player_human(c, slot);
        if (human && !ps2::ld32(human + kMugCallbackOffset)) ps2::st32(human + kMugCallbackOffset, ps2::ld32(second + kMugCallbackOffset));
    }
}

bool stat_score(ps2::Cpu &c) {
    if (players() < kScaledFrom) return false;
    const std::uint32_t asked = p9::resolve_handle(c, c.r[4].w[0]);
    if (!asked || s8(asked + kSlotOffset) != static_cast<std::int32_t>(kSecondPlayer)) return false;
    std::int32_t sum = 0;
    for (std::uint32_t slot = kSecondPlayer; slot < static_cast<std::uint32_t>(players()); ++slot)
        if (const std::uint32_t human = player_human(c, slot)) sum += static_cast<std::int32_t>(ps2::call_guest(c, kScoreValueOf, kStatsObject, human));
    AI_TRACE("rank sum for player 2: %d (%d players)", sum, players());
    c.r[2].sd[0] = sum;
    c.pc = c.r[31].w[0];
    return true;
}

}
