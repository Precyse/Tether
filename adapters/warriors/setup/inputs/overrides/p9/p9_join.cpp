#include "p9_game.hpp"
#include "p9_hooks.hpp"
#include "p9_world.hpp"
#include "ps2/system.hpp"

// Joining and leaving as players 3 and 4. The game's own join (session_match_player_count, started by a pad's START in level_driver_step) takes the next free member of
// player 1's gang (crew_next_playable) and makes him a player on that pad (spawn_player_human). The original game has exactly one such member, Ash. For the third and
// fourth player the Warriors' crew has no free member in most missions, so the game's own crew_take_member recruits one of the mission's other Warriors (Vermin, Cleon:
// brain type 3, no pad, no player slot) into player 1's gang first; the game's code does the rest.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kCrewNextPlayable = 0x0022A770, kSpawnPlayerHuman = 0x00229C40, kCrewTakeMember = 0x00166308;
constexpr std::uint32_t kPersonAttachInput = 0x002384F8, kWarpPersonBeside = 0x0023B138;
constexpr std::uint32_t kBrains = 0x006D53F0, kBrainStride = 0x2F0, kBrainTypeOffset = 4, kBrainActiveOffset = 8, kBrainGangOffset = 0x20C;
constexpr std::uint32_t kGangWarriorBrain = 3;
constexpr std::uint32_t kFirstExtraRecord = 1;            // pad record indexes 1 and 2 are the runtime's pads 3 and 4
constexpr std::uint32_t kAnyRecord = 0xFFFFFFFF;          // spawn_player_human's pad record when the join names no pad (the lobby's two-player mode)
constexpr std::uint32_t kSecondPlayerRecord = 4;          // port 1 slot 0: pad 2's record, the original game's second player
constexpr int kSecondPlayerPad = 1;                       // the runtime's pad index of pad 2
constexpr std::uint32_t kSceneStartFrames = 20;           // the join scene has started by then (a teleport before it makes the game see a player in a state that ends the game)

std::uint32_t brain_at(std::uint32_t i) { return kBrains + i * kBrainStride; }
std::uint32_t gang_of(std::uint32_t i) { return ps2::ld32(brain_at(i) + kBrainGangOffset); }

// The gang the leader (list slot 0) belongs to: the crew a joining player is taken from.
std::uint32_t leader_gang() {
    const std::uint32_t handle = ps2::ld32(game() + kPlayerHandlesOffset);
    for (std::uint32_t i = 0; i < kHumanCount; ++i)
        if (handle_of(human_at(i)) == handle) return gang_of(i);
    return 0;
}

// The first mission Warrior by slot who is alive, run by the AI and drives no pad: every machine makes the same choice from guest memory alone.
std::uint32_t recruit_candidate(std::uint32_t crew) {
    for (std::uint32_t i = 0; i < kHumanCount; ++i) {
        const std::uint32_t human = human_at(i);
        if (handle_of(human) == kAny || !ps2::ld8(brain_at(i) + kBrainActiveOffset) || ps2::ld32(brain_at(i) + kBrainTypeOffset) != kGangWarriorBrain) continue;
        if (ps2::ld8(human + kPlayerSlotOffset) != kNone || ps2::ld8(kPlayerRecords + i * kPlayerRecordStride + kRecordPadOffset) != kNone) continue;
        if (gang_of(i) != crew) return human;
    }
    return 0;
}

bool g_in_pick = false, g_in_make = false;

}

// crew_next_playable(gang, only_standing): with nobody free in a crew whose leader has room for more players, one of the mission's Warriors joins the crew.
bool pick_next_player(ps2::Cpu &c) {
    const std::uint32_t crew = c.r[4].w[0], standing = c.r[5].w[0];
    if (g_in_pick || standing || player_count() < kOriginalPlayers || player_count() >= kMaxPlayers) return false;
    g_in_pick = true;
    std::uint32_t pick = ps2::call_guest(c, kCrewNextPlayable, crew, standing);
    if (!pick) {
        const std::uint32_t human = recruit_candidate(crew);
        if (human) {
            P9_TRACE("[p9] recruit human %08x into gang %08x", human, crew);
            ps2::call_guest(c, kCrewTakeMember, crew, human);
            pick = ps2::call_guest(c, kCrewNextPlayable, crew, standing);
        }
    }
    g_in_pick = false;
    c.r[2].sd[0] = static_cast<std::int32_t>(pick);
    c.pc = c.r[31].w[0];
    return true;
}

// spawn_player_human(human, pad record, panel, list position): a player after the second appears beside the leader, as Ash does in the original co-op. A second player who
// joins without a pad of his own (the lobby's two-player mode) gets the first ready pad record, which is a third or fourth player's once pad 3 or 4 is plugged: pad 2's record
// is named instead when pad 2 is there.
bool spawn_player_human(ps2::Cpu &c) {
    ps2::PadSample sample;
    if (player_count() == 1 && c.r[5].w[0] == kAnyRecord && ps2::read_pad_sample(kSecondPlayerPad, sample)) c.r[5].w[0] = kSecondPlayerRecord;
    if (g_in_make || player_count() < kOriginalPlayers || player_count() >= kMaxPlayers) return false;
    g_in_make = true;
    const std::uint32_t human = c.r[4].w[0];
    const std::uint32_t joined = ps2::call_guest(c, kSpawnPlayerHuman, human, c.r[5].w[0], c.r[6].w[0], c.r[7].w[0]);
    g_in_make = false;
    if (joined) {
        ps2::call_guest(c, kPersonAttachInput, handle_of(human), 1);
        std::uint32_t pending = 0;
        for (std::uint32_t slot = kOriginalPlayers; slot < player_count(); ++slot) pending |= 1u << slot;
        ps2::st32(kArrivalWord, pending);
        if (c.r[5].w[0] >= kFirstExtraRecord && c.r[5].w[0] < kFirstExtraRecord + kExtraPadRecords) ps2::st32(kCarryMask, ps2::ld32(kCarryMask) | 1u << c.r[5].w[0]);
        ps2::st32(kArrivalFrame, static_cast<std::uint32_t>(ps2::frame_count()));
        P9_TRACE("[p9] human %08x is player %u on pad %u, panel %d", human, player_count() - 1, c.r[5].w[0], static_cast<std::int32_t>(ps2::ld32(human + kPanelOffset)));
    }
    c.r[2].sd[0] = static_cast<std::int32_t>(joined);
    c.pc = c.r[31].w[0];
    return true;
}

}

namespace warriors::p9 {

// session_run_coop_callback(game, human, joined): the level scripts know one second player, and their callback for a join (the lesson's wakes Ash, adds his blip and
// puts him at the join spot) would do that again for every extra player. It is left out for players 3 and 4, as it is for their leaving.
bool skip_extra_player_callback(ps2::Cpu &c) {
    const std::uint32_t slot = ps2::ld8(c.r[5].w[0] + kPlayerSlotOffset);
    if (slot < kOriginalPlayers || slot >= kMaxPlayers) return false;
    c.pc = c.r[31].w[0];
    return true;
}

// After a join: players 3 and 4 appear beside the leader once the scene the join starts (the game's own, as for player 2) is over and the follow camera is back.
// The scene puts every player where its script wants him, so every extra player is placed again after each join.
void place_new_players(ps2::Cpu &c) {
    const std::uint32_t pending = ps2::ld32(kArrivalWord);
    if (!pending || hook_off("place") || ps2::frame_count() < ps2::ld32(kArrivalFrame) + kSceneStartFrames) return;
    if (!normal_play(c)) return;
    const std::uint32_t leader_handle = ps2::ld32(game() + kPlayerHandlesOffset);
    ps2::st32(kArrivalWord, 0);
    for (std::uint32_t slot = kOriginalPlayers; slot < kMaxPlayers; ++slot) {
        if (!(pending & (1u << slot))) continue;
        const std::uint32_t handle = ps2::ld32(game() + kPlayerHandlesOffset + slot * 4);
        const std::uint32_t result = ps2::call_guest(c, kWarpPersonBeside, handle, leader_handle);
        P9_TRACE("[p9] player %u placed beside the leader (%u)", slot, result);
    }
}

}
