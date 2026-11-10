#include "p9_stats.hpp"

#include "p9_game.hpp"
#include "p9_hud.hpp"
#include "ps2/system.hpp"

namespace warriors::p9 {

namespace {

constexpr std::uint32_t kStatsObject = 0x006FE490, kPlayerStatsBytes = 0xC0, kStatsHumanPanel = kPanelOffset;
constexpr std::uint32_t kScoreFoldPlayerTwo = 0x00422DA0, kScoreRecordReset = 0x00420A70;
constexpr std::uint32_t kCheckpointOffset = 0x180;                       // offset of the checkpoint copies inside kStatsBlocks (and of the checkpoint records in the object)
constexpr std::uint32_t kExtraRecords = kMaxPlayers - kOriginalPlayers;
constexpr std::uint32_t kNoPanel = 0;
std::uint32_t g_leaving_panel = kNoPanel;

std::uint32_t record_of(std::uint32_t panel) { return kStatsBlocks + (panel - kOriginalPlayers) * kPlayerStatsBytes; }

void copy_bytes(std::uint32_t to, std::uint32_t from, std::uint32_t bytes) {
    for (std::uint32_t i = 0; i < bytes; i += 4) ps2::st32(to + i, ps2::ld32(from + i));
}

void swap_bytes(std::uint32_t a, std::uint32_t b, std::uint32_t bytes) {
    for (std::uint32_t i = 0; i < bytes; i += 4) {
        const std::uint32_t x = ps2::ld32(a + i), y = ps2::ld32(b + i);
        ps2::st32(a + i, y);
        ps2::st32(b + i, x);
    }
}

bool extra_panel(std::uint32_t panel) { return panel >= kOriginalPlayers && panel < kMaxPlayers; }

}

bool score_record_of_player(ps2::Cpu &c) {
    if (c.r[4].w[0] != kStatsObject) return false;
    const std::uint32_t panel = ps2::ld32(c.r[5].w[0] + kStatsHumanPanel);
    if (!extra_panel(panel)) return false;
    c.r[2].sd[0] = static_cast<std::int32_t>(record_of(panel));
    c.pc = c.r[31].w[0];
    return true;
}

bool score_checkpoint_store(ps2::Cpu &c) {
    if (c.r[4].w[0] == kStatsObject) copy_bytes(kStatsBlocks + kCheckpointOffset, kStatsBlocks, kExtraRecords * kPlayerStatsBytes);
    return false;
}

bool score_checkpoint_load(ps2::Cpu &c) {
    if (c.r[4].w[0] == kStatsObject) copy_bytes(kStatsBlocks, kStatsBlocks + kCheckpointOffset, kExtraRecords * kPlayerStatsBytes);
    return false;
}

bool score_records_clear(ps2::Cpu &c) {
    if (c.r[4].w[0] != kStatsObject || panel_count() != kMaxPlayers) return false;
    for (std::uint32_t panel = kOriginalPlayers; panel < kMaxPlayers; ++panel) ps2::call_guest(c, kScoreRecordReset, record_of(panel));
    return false;
}

void set_leaving_panel(std::uint32_t panel) { g_leaving_panel = panel; }

bool score_fold_player_two(ps2::Cpu &c) {
    if (c.r[4].w[0] != kStatsObject || !extra_panel(g_leaving_panel)) return false;
    const std::uint32_t leaver = record_of(g_leaving_panel), second = kStatsObject + kPlayerStatsBytes;
    g_leaving_panel = kNoPanel;
    swap_bytes(second, leaver, kPlayerStatsBytes);
    ps2::call_guest(c, kScoreFoldPlayerTwo, kStatsObject);
    swap_bytes(second, leaver, kPlayerStatsBytes);
    c.pc = c.r[31].w[0];
    return true;
}

}
