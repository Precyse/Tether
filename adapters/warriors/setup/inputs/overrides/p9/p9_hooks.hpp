#pragma once

#include "ps2/generated.hpp"

#include <cstdint>

// Entry points the generated code calls (apply_p9.py puts a call at the start of each hooked function; a hook that returns true has done the
// function's work and returned to the caller, one that returns false lets the original code run unchanged).
namespace warriors::p9 {

// PadPort_Step: pad records of the multitap slots come from the runtime's pads 3 and 4. True when handled.
bool pad_port_step(ps2::Cpu &c);
// How many pad records Pads_Update's loop moves on by (4, the original's, until pads 3 or 4 are plugged; then 1).
std::uint32_t pad_loop_step();

// Sets up the relocated storage of a snapshot made before it existed; called before any code that reads it (GameState_Tick and CheckGameOver, then each frame).
void ensure_area(ps2::Cpu &c);
// Once per frame at Pads_Update.
void frame_start(ps2::Cpu &c);
// GameState_Construct: the relocated storage starts empty.
void game_construct(ps2::Cpu &c);
// The offset that, added to the game state pointer, reaches the throw arcs (ThrowArc array of four, moved out of the game state block).
std::uint32_t throw_arc_offset();

// PlayerList_Add: the arc of a third or fourth player is built when he joins (the game builds two).
bool player_list_add(ps2::Cpu &c);

// `slot * 0x5c` of the code that reaches the player state blocks inline: slots 2 and 3 land in their own blocks.
void state_mult(ps2::Cpu &c, int pipe, std::uint32_t slot, std::uint32_t stride);

// Human_GetPlayerState: panels 2 and 3 have their own player state blocks.
bool player_state(ps2::Cpu &c);

// Joining as player 3 or 4: Gang_PickNextPlayer recruits a mission Warrior when the crew has none free; Human_MakePlayer places the new player beside the leader.
bool pick_next_player(ps2::Cpu &c);
bool make_player(ps2::Cpu &c);
// GameState_CallMultiplayerCallback: the level scripts' second player callback is not run for players 3 and 4.
bool skip_extra_player_callback(ps2::Cpu &c);
// Once per frame: puts a player who joined in the last frame beside the leader.
void place_new_players(ps2::Cpu &c);

// Leaving: a player 3 or 4 whose pad is gone leaves through the game's own removal steps; leave_player does it for one list slot (also the entry for the pause menu).
void leave_player(ps2::Cpu &c, std::uint32_t slot);
void leave_check(ps2::Cpu &c);

// PS2_P9_TRACE: names a call with its first arguments.
void trace_call(ps2::Cpu &c, const char *name);
void trace_players(ps2::Cpu &c);

// Inventory (slots 2 and 3 have blocks of their own): the accessors take the slot, the others run first and extend the extra blocks.
bool inv_get_count(ps2::Cpu &c);
bool inv_has_item(ps2::Cpu &c);
bool inv_add_item(ps2::Cpu &c);
bool inv_set_count(ps2::Cpu &c);
bool inv_clear_player_checked(ps2::Cpu &c);
bool inv_save_checkpoint(ps2::Cpu &c);
bool inv_restore_checkpoint(ps2::Cpu &c);
bool inv_reset(ps2::Cpu &c);

}
