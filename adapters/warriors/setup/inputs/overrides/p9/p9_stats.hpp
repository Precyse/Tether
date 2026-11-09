#pragma once

#include "ps2/generated.hpp"

#include <cstdint>

// The statistics object (0x006fe490: two live PlayerStats records of 0xc0 bytes, two checkpoint copies, the bests) for players 3 and 4. Stats_GetPlayer is base + panel *
// 0xc0, so panels 2 and 3 would write into the checkpoint copies of players 1 and 2: their records live in extra RAM instead.
namespace warriors::p9 {

// Stats_GetPlayer(stats, human): the record of panel 2 or 3 is in extra RAM.
bool stats_get_player(ps2::Cpu &c);
// Stats_SaveCheckpoint / Stats_RestoreCheckpoint / Stats_ResetPlayers: the extra records follow (they run first, the original then runs).
bool stats_save_checkpoint(ps2::Cpu &c);
bool stats_restore_checkpoint(ps2::Cpu &c);
bool stats_reset_players(ps2::Cpu &c);
// Stats_MergePlayer2 folds record 1 (the second player leaving) into record 0; while a player 3 or 4 leaves (set_leaving_panel) it folds that player's record instead.
bool stats_merge_player2(ps2::Cpu &c);
void set_leaving_panel(std::uint32_t panel);

}
