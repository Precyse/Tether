#pragma once

#include "ps2/generated.hpp"

#include <cstdint>

// The statistics object (0x006fe490: two live ScoreRecord records of 0xc0 bytes, two checkpoint copies, the bests) for players 3 and 4. score_record_of_player is base + panel *
// 0xc0, so panels 2 and 3 would write into the checkpoint copies of players 1 and 2: their records live in extra RAM instead.
namespace warriors::p9 {

// score_record_of_player(stats, human): the record of panel 2 or 3 is in extra RAM.
bool score_record_of_player(ps2::Cpu &c);
// score_checkpoint_store / score_checkpoint_load / score_records_clear: the extra records follow (they run first, the original then runs).
bool score_checkpoint_store(ps2::Cpu &c);
bool score_checkpoint_load(ps2::Cpu &c);
bool score_records_clear(ps2::Cpu &c);
// score_fold_player_two folds record 1 (the second player leaving) into record 0; while a player 3 or 4 leaves (set_leaving_panel) it folds that player's record instead.
bool score_fold_player_two(ps2::Cpu &c);
void set_leaving_panel(std::uint32_t panel);

}
