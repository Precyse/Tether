#include "p9_game.hpp"
#include "p9_hooks.hpp"
#include "p9_stats.hpp"
#include "p9_world.hpp"
#include "ps2/pad.hpp"
#include "ps2/system.hpp"

// Leaving as player 3 or 4. The original game has one way out, GameState_SyncPlayers' removal loop (player 2 drops out when the pause menu quits him, with
// everything else that joined through SyncPlayers). This is the body of that loop for ONE chosen player, with the game's own calls in the game's own order,
// so a player 3 or 4 can leave while the others stay: camera target and camera, the pair broken, Human_ReleasePlayer (pad, panel, list slot, inventory, AI
// again), the join mask bit, the target list compacted and the views laid out again. The level scripts' multiplayer callback (GameState_CallMultiplayerCallback,
// called by SyncPlayers for a leaver) is left out: the scripts know one second player, and the lesson's moves Ash (player 2, who stays) under the map when told that
// someone left.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kCameraTargetsContains = 0x00122248, kCameraTargetsRemove = 0x00122438, kCamerasRemovePlayer = 0x0011ED60, kCamerasLayoutViews = 0x00122ED0;
constexpr std::uint32_t kBreakPair = 0x00258A88, kReleasePlayer = 0x0022A2A8, kHandleArrayCompact = 0x004DAD40;
constexpr std::uint32_t kCameraTargetList = 0x005D91A8, kCameraTargetCount = 0x0050B194;
constexpr std::uint32_t kJoinMaskOffset = 0x456;       // u16 of game state: one bit per pad record index that joined through SyncPlayers
constexpr std::uint32_t kFirstExtraRecordIndex = 1;    // pad record indexes 1 and 2 are the runtime's pads 3 and 4

bool joined_through_sync(std::uint32_t human) {
    const std::uint32_t pad = ps2::ld8(record_of(human) + kRecordPadOffset);
    return pad != kNone && (ps2::ld16(game() + kJoinMaskOffset) >> (pad & 0x1F) & 1u);
}

// True when the player's pad was one of the runtime's pads 3 and 4 and is no longer plugged.
bool pad_gone(std::uint32_t human) {
    const std::uint32_t pad = ps2::ld8(record_of(human) + kRecordPadOffset);
    if (pad < kFirstExtraRecordIndex || pad >= kFirstExtraRecordIndex + kExtraPadRecords) return false;
    ps2::PadSample sample;
    return !ps2::read_pad_sample(kFirstExtraPad + static_cast<int>(pad - kFirstExtraRecordIndex), sample);
}

}

void leave_player(ps2::Cpu &c, std::uint32_t slot) {
    const std::uint32_t human = resolve_handle(c, ps2::ld32(game() + kPlayerHandlesOffset + slot * 4));
    if (!human) return;
    P9_TRACE("[p9] player %u (human %08x) leaves", slot, human);
    if (ps2::call_guest(c, kCameraTargetsContains, human) == 1) {
        ps2::call_guest(c, kCamerasRemovePlayer, slot);
        ps2::call_guest(c, kCameraTargetsRemove, human, 1);
    }
    const std::uint32_t record = record_of(human);
    if (joined_through_sync(human)) {
        const std::uint32_t bit = 1u << (ps2::ld8(record + kRecordPadOffset) & 0x1F);
        ps2::st32(kCarryMask, ps2::ld32(kCarryMask) & ~bit);
        ps2::call_guest(c, kBreakPair, human);
        set_leaving_panel(ps2::ld32(human + kPanelOffset));
        ps2::call_guest(c, kReleasePlayer, human, 1, 1);
        set_leaving_panel(0);
        ps2::st16(game() + kJoinMaskOffset, static_cast<std::uint16_t>(ps2::ld16(game() + kJoinMaskOffset) & ~bit));
    }
    ps2::st32(kCameraTargetCount, ps2::call_guest(c, kHandleArrayCompact, kCameraTargetList));
    ps2::call_guest(c, kCamerasLayoutViews);
}

// Once per frame: a player 3 or 4 whose pad is gone leaves as soon as the game is in plain play (the pad reads neutral until then, see pad_port_step).
void leave_check(ps2::Cpu &c) {
    if (!game() || player_count() <= kOriginalPlayers || !normal_play(c)) return;
    for (std::uint32_t slot = player_count(); slot-- > kOriginalPlayers;) {
        const std::uint32_t human = resolve_handle(c, ps2::ld32(game() + kPlayerHandlesOffset + slot * 4));
        if (human && pad_gone(human)) leave_player(c, slot);
    }
}

}
