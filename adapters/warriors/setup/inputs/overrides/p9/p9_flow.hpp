#pragma once

#include "ps2/generated.hpp"

#include <cstdint>

// Level flows of players 3 and 4 (p9_flow.cpp): what the game's own level changes, reloads and scripted moves do to them. Entry hooks of p9_edits_flow.py; a hook that
// returns true has done the function's work, one that returns false lets the original run.
namespace warriors::p9 {

// Once per frame (pads_frame_step): the flows that rebuild or restart the level. Players 3 and 4 who were in come back after a level change, a checkpoint reload or a restart (the game's
// own join on their pad, session_match_player_count); a chapter restarted in place stands them up.
void flow_frame(ps2::Cpu &c);

// Test aid, PS2_P9_JOIN_ASSIST=1 (with =stop the run also ends once all four players are seated and in normal play): a pad whose player is not seated presses START for
// kJoinPressFrames every kJoinPressPeriod frames, but only while the game's own join gate is open (plain play, the level's script allows joining), pad 3 before pad 4: the
// timing of the press is reactive, the press itself and the join it asks for are the game's. `record` is the pad record (1 or 2) of the runtime's pad 3 or 4.
bool join_press_due(ps2::Cpu &c, std::uint32_t record);

// warp_object, warp_object_to_marker and warp_person_beside (the level scripts' Teleport, TeleportToFlag and HuTeleportNearHuman): the move of player 1 or 2 runs as
// the game does it, then players 3 and 4 who are left behind are put beside the player who moved.
bool warp_object(ps2::Cpu &c);
bool warp_object_to_marker(ps2::Cpu &c);
bool warp_person_beside(ps2::Cpu &c);

// trigger_box_step: players 3 and 4 set off the level scripts' player boxes (rail and fixed cameras, jump hints, chase starts) like players 1 and 2. trigger_box_pool_init:
// the boxes of the last level are gone, so are the occupants players 3 and 4 had in them.
bool trigger_box_step(ps2::Cpu &c);
void player_boxes_reset(ps2::Cpu &c);

}
