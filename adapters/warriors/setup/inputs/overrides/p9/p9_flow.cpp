#include "p9_flow.hpp"

#include "p9_game.hpp"
#include "p9_hooks.hpp"
#include "p9_world.hpp"
#include "ps2/pad.hpp"
#include "ps2/system.hpp"

// How players 3 and 4 live through the game's level flows. The game rebuilds its players with every level load (InitLevel -> GameState_SyncPlayers(-1) makes player 2 from
// the game mode's two-player flag); nothing remembers players 3 and 4, so carry_players brings back the ones that were in. The level scripts move player 1 and player 2
// only (Teleport, TeleportToFlag, HuTeleportNearHuman); the hooks put players 3 and 4 beside whoever moved.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kSyncPlayers = 0x0041A460, kTeleportNear = 0x0023B138, kHeal = 0x0022EB40;
constexpr std::uint32_t kObjectTeleport = 0x00385BB8, kObjectTeleportToFlag = 0x00385DB0;
constexpr std::uint32_t kJoinAllowedOffset = 0x56E8, kGameModeOffset = 0x158;   // CfgMultiplayerJoin's flag; the game mode
constexpr std::uint32_t kStoryMode = 0, kArmiesMode = 3;                        // the modes that take a drop-in player
constexpr std::uint32_t kFlowStateOffset = 0x14C, kFlowRunning = 0, kFlowFailed = 1;   // the level flow state of the game: idle, mission failed screen up
constexpr std::uint32_t kPreviousFlow = kFlow + 0x10;     // the flow state of the last frame
constexpr std::uint32_t kFullHealth = 1;                  // Human_Heal's second argument
constexpr std::uint32_t kFirstExtraRecord = 1;            // pad record indexes 1 and 2 are the runtime's pads 3 and 4
constexpr float kFollowDistance = 8.0f;                   // an extra player closer than this to player 1 or 2 stays where he is when one of them is moved

bool g_following = false;   // the teleport the hook runs itself is not hooked again

// The game's own gate of a join (GameState_SyncPlayers): the script allows joining in this section and the mode is one that takes drop-in players.
bool join_allowed() {
    const std::uint32_t mode = ps2::ld32(game() + kGameModeOffset);
    return ps2::ld32(game() + kJoinAllowedOffset) != 0 && (mode == kStoryMode || mode == kArmiesMode);
}

bool pad_is_player(ps2::Cpu &c, std::uint32_t record) {
    for (std::uint32_t slot = 0; slot < player_count(); ++slot) {
        const std::uint32_t human = resolve_handle(c, ps2::ld32(game() + kPlayerHandlesOffset + slot * 4));
        if (human && ps2::ld8(record_of(human) + kRecordPadOffset) == record) return true;
    }
    return false;
}

// The human of list slot 0 or 1 whose handle this is, 0 for anything else (the other players, the AI, scenery).
std::uint32_t original_player(ps2::Cpu &c, std::uint32_t handle) {
    for (std::uint32_t slot = 0; slot < kOriginalPlayers && slot < player_count(); ++slot)
        if (ps2::ld32(game() + kPlayerHandlesOffset + slot * 4) == handle) return resolve_handle(c, handle);
    return 0;
}

// Players 3 and 4 farther than kFollowDistance from both original players stand beside the one who just moved (the game's own placement, as after a join). A player who
// has just joined waits for place_new_players; one who is in a scene is the scene's.
void gather_extras(ps2::Cpu &c, std::uint32_t mover_handle) {
    std::uint32_t originals[kOriginalPlayers];
    for (std::uint32_t slot = 0; slot < kOriginalPlayers; ++slot) originals[slot] = resolve_handle(c, ps2::ld32(game() + kPlayerHandlesOffset + slot * 4));
    const std::uint32_t arriving = ps2::ld32(kArrivalWord);
    for (std::uint32_t slot = kOriginalPlayers; slot < player_count(); ++slot) {
        const std::uint32_t handle = ps2::ld32(game() + kPlayerHandlesOffset + slot * 4);
        const std::uint32_t human = resolve_handle(c, handle);
        if (!human || (arriving & (1u << slot)) || in_scene(c, human)) continue;
        bool beside = false;
        for (std::uint32_t original : originals) beside = beside || (original && human_distance(human, original) <= kFollowDistance);
        if (beside) continue;
        ps2::call_guest(c, kTeleportNear, handle, mover_handle);
        P9_TRACE("[p9] %llu player %u follows the move of %08x", static_cast<unsigned long long>(ps2::frame_count()), slot, mover_handle);
    }
}

// Runs the teleport function `address` as the game does when player 1 or 2 is the one moved, then gathers players 3 and 4.
bool follow_teleport(ps2::Cpu &c, std::uint32_t address) {
    if (g_following || player_count() <= kOriginalPlayers || hook_off("follow")) return false;
    const std::uint32_t handle = c.r[4].w[0];
    if (!original_player(c, handle)) return false;
    g_following = true;
    ps2::call_guest(c, address, handle, c.r[5].w[0], c.r[6].w[0]);
    gather_extras(c, handle);
    g_following = false;
    c.pc = c.r[31].w[0];
    return true;
}

// The mission failed screen's "last checkpoint" restarts some chapters in place (the script's own restart function, no level load): players 1 and 2 are the script's, and
// players 3 and 4, who may be down, stand up beside the leader like the players the script revives.
void revive_extras(ps2::Cpu &c) {
    const std::uint32_t leader = ps2::ld32(game() + kPlayerHandlesOffset);
    for (std::uint32_t slot = kOriginalPlayers; slot < player_count(); ++slot) {
        const std::uint32_t human = resolve_handle(c, ps2::ld32(game() + kPlayerHandlesOffset + slot * 4));
        if (human) ps2::call_guest(c, kHeal, human, kFullHealth);
    }
    gather_extras(c, leader);
    P9_TRACE("[p9] %llu chapter restarted in place: players 3 and 4 stand up", static_cast<unsigned long long>(ps2::frame_count()));
}

// After a level change, a checkpoint reload or a restart the game builds a new player list. Players 3 and 4 who were in come back by the game's own join, SyncPlayers on their
// pad (what their START would do), one per frame, once the second player is in, the section allows joining and the game's own join gate is open (plain play or a rail section): the join and its scene are the game's.
void carry_players(ps2::Cpu &c) {
    const std::uint32_t mask = ps2::ld32(kCarryMask);
    if (!mask || hook_off("carry") || player_count() < kOriginalPlayers || player_count() >= kMaxPlayers || !join_allowed() || !join_gate_open(c)) return;
    for (std::uint32_t record = kFirstExtraRecord; record < kFirstExtraRecord + kExtraPadRecords; ++record) {
        if (!(mask & (1u << record))) continue;
        ps2::PadSample sample;
        if (!ps2::read_pad_sample(kFirstExtraPad + static_cast<int>(record - kFirstExtraRecord), sample)) { ps2::st32(kCarryMask, mask & ~(1u << record)); continue; }
        if (pad_is_player(c, record)) continue;
        P9_TRACE("[p9] %llu pad record %u comes back (carried over)", static_cast<unsigned long long>(ps2::frame_count()), record);
        ps2::call_guest(c, kSyncPlayers, game(), record);
        return;
    }
}

}

bool teleport_object(ps2::Cpu &c) { return follow_teleport(c, kObjectTeleport); }
bool teleport_to_flag(ps2::Cpu &c) { return follow_teleport(c, kObjectTeleportToFlag); }
bool teleport_near(ps2::Cpu &c) { return follow_teleport(c, kTeleportNear); }

// Once per frame: the flows that rebuild or restart the level.
void flow_frame(ps2::Cpu &c) {
    if (game()) {
        const std::uint32_t flow = ps2::ld32(game() + kFlowStateOffset);
        const bool extras = player_count() > kOriginalPlayers;
        const bool restarted_in_place = extras && ps2::ld32(kPreviousFlow) == kFlowFailed && flow == kFlowRunning;
        if (extras || ps2::ld32(kPreviousFlow) != 0) ps2::st32(kPreviousFlow, extras ? flow : 0);   // a 1 or 2 player game leaves the extra RAM untouched
        if (restarted_in_place) revive_extras(c);
    }
    carry_players(c);
}

}
