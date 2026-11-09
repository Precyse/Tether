#pragma once

#include "ps2/generated.hpp"

#include <cstdint>

// Level flows of players 3 and 4 (p9_flow.cpp): what the game's own level changes, reloads and scripted moves do to them. Entry hooks of p9_edits_flow.py; a hook that
// returns true has done the function's work, one that returns false lets the original run.
namespace warriors::p9 {

// Once per frame (Pads_Update): the flows that rebuild or restart the level. Players 3 and 4 who were in come back after a level change, a checkpoint reload or a restart (the game's
// own join on their pad, SyncPlayers); a chapter restarted in place stands them up.
void flow_frame(ps2::Cpu &c);

// Object_Teleport, Object_TeleportToFlag and Human_TeleportNear (the level scripts' Teleport, TeleportToFlag and HuTeleportNearHuman): the move of player 1 or 2 runs as
// the game does it, then players 3 and 4 who are left behind are put beside the player who moved.
bool teleport_object(ps2::Cpu &c);
bool teleport_to_flag(ps2::Cpu &c);
bool teleport_near(ps2::Cpu &c);

// PlayerBox_Update: players 3 and 4 set off the level scripts' player boxes (rail and fixed cameras, jump hints, chase starts) like players 1 and 2. PlayerBoxPool_Create:
// the boxes of the last level are gone, so are the occupants players 3 and 4 had in them.
bool player_box_update(ps2::Cpu &c);
void player_boxes_reset(ps2::Cpu &c);

}
