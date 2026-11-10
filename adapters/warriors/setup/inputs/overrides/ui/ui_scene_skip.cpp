#include "ui_hooks.hpp"

#include "overlay/overlay.hpp"
#include "ps2/system.hpp"
#include "ui_game.hpp"

#include <cstdio>
#include <string>

// Cutscene skip as a vote (three or four players; two keep the original rule). cutscene_task_step (0x0039cbf0) lets any player's cross or START skip a skippable scene once its countdown (+0xe0) has run out, so one
// impatient player would skip the story for the others. In a session the skip logic only sees a skippable scene while every player holds cross or START: the scene's
// skippable byte (+0xe5) is cleared at the entry of each update that has no unanimous vote and put back by the update that has one, and the countdown the original
// runs inside that logic goes on here meanwhile. A skip that has started (+0xe4) is never interrupted. The overlay says how many players hold (SKIP 1/2).
namespace warriors::ui {

namespace {

constexpr std::uint32_t kSceneEntersSkipLogic = 5;
constexpr std::uint32_t kVoteFromPlayers = 3;   // with two players the original rule stays: either one skips
constexpr std::uint32_t kSceneState = 0xA0, kStateMode = 0x1C, kSceneSkipDelay = 0xE0, kSceneSkipStarted = 0xE4, kSceneSkippable = 0xE5, kGameSceneBlocked = 0x56E4;
constexpr std::uint32_t kPadRecords = 0x005DD810, kPadRecordBytes = 0x50, kPadHistoryHead = 0x2C, kPadButtons = 0x1C;
constexpr std::uint32_t kCrossBit = 0x40, kStartBit = 0x800;
constexpr std::uint32_t kMaskedScene = kLobbyState + 0x2C;   // the scene task whose skippable byte this file cleared, 0 for none

void release(std::uint32_t scene) {
    ps2::st8(scene + kSceneSkippable, 1);
    ps2::st32(kMaskedScene, 0);
    std::fprintf(stderr, "[ui] scene %08x: skip allowed (frame %llu)\n", scene, static_cast<unsigned long long>(ps2::frame_count()));
}

bool holds_skip(ps2::Cpu &c, std::uint32_t slot) {
    const int pad = player_pad(c, slot);
    if (pad < 0) return false;
    const std::uint32_t record = kPadRecords + static_cast<std::uint32_t>(pad) * kPadRecordBytes;
    return (ps2::ld16(record + ps2::ld32(record + kPadHistoryHead) * 2 + kPadButtons) & (kCrossBit | kStartBit)) != 0;
}

std::uint32_t holders(ps2::Cpu &c, std::uint32_t players) {
    std::uint32_t count = 0;
    for (std::uint32_t slot = 0; slot < players; ++slot) count += holds_skip(c, slot);
    return count;
}

// The vote as the overlay shows it on this machine (display only: it reads the pads, every machine counts the same, and it writes nothing to the game).
void show_vote(std::uint32_t count, std::uint32_t players) {
    static std::uint32_t shown = 0;
    if (count != shown && count > 0) ps2::overlay::notice("SKIP " + std::to_string(count) + "/" + std::to_string(players), "");
    shown = count;
}

}

void scene_skip_vote(ps2::Cpu &c) {
    const std::uint32_t scene = c.r[4].w[0], game = ps2::ld32(kGameStatePointer);
    const bool masked = ps2::ld32(kMaskedScene) == scene;
    const std::uint32_t players = game ? ps2::ld16(game + kPlayerCountOffset) : 0;
    const bool in_skip_logic = ps2::ld16(ps2::ld32(scene + kSceneState) + kStateMode) == kSceneEntersSkipLogic;
    const bool voting = coop_session() && players >= kVoteFromPlayers && in_skip_logic && !ps2::ld8(scene + kSceneSkipStarted);
    if (!voting || (!masked && !ps2::ld8(scene + kSceneSkippable))) {
        if (masked) release(scene);
        return;
    }
    const std::uint32_t holding = holders(c, players);
    show_vote(holding, players);
    if (holding == players) {
        if (masked) release(scene);
        return;
    }
    if (!masked) std::fprintf(stderr, "[ui] scene %08x: skip needs all %u players (frame %llu)\n", scene, players, static_cast<unsigned long long>(ps2::frame_count()));
    ps2::st8(scene + kSceneSkippable, 0);
    ps2::st32(kMaskedScene, scene);
    if (!ps2::ld8(game + kGameSceneBlocked)) ps2::st32(scene + kSceneSkipDelay, ps2::ld32(scene + kSceneSkipDelay) - 1);
}

}
