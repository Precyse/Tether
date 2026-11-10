#pragma once

#include "ps2/generated.hpp"

#include <cstdint>

// Entry points the generated code calls (apply_p9.py puts a call at the start of each hooked function; a hook that returns true has done the
// function's work and returned to the caller, one that returns false lets the original code run unchanged).
namespace warriors::p9 {

// pad_port_poll: pad records of the multitap slots come from the runtime's pads 3 and 4. True when handled.
bool pad_port_poll(ps2::Cpu &c);
// How many pad records pads_frame_step's loop moves on by (4, the original's, until pads 3 or 4 are plugged; then 1).
std::uint32_t pad_loop_step();

// Sets up the relocated storage of a snapshot made before it existed; called before any code that reads it (session_step and session_failure_check, then each frame).
void ensure_area(ps2::Cpu &c);
// Once per frame at pads_frame_step.
void frame_start(ps2::Cpu &c);
// session_state_init: the relocated storage starts empty.
void game_construct(ps2::Cpu &c);
// The offset that, added to the game state pointer, reaches the throw arcs (ThrowPath array of four, moved out of the game state block).
std::uint32_t throw_arc_offset();

// session_add_player: the arc of a third or fourth player is built when he joins (the game builds two).
bool session_add_player(ps2::Cpu &c);

// `slot * 0x5c` of the code that reaches the player state blocks inline: slots 2 and 3 land in their own blocks.
void state_mult(ps2::Cpu &c, int pipe, std::uint32_t slot, std::uint32_t stride);

// player_slot_state: panels 2 and 3 have their own player state blocks.
bool player_state(ps2::Cpu &c);

// Joining as player 3 or 4: crew_next_playable recruits a mission Warrior when the crew has none free; spawn_player_human places the new player beside the leader.
bool pick_next_player(ps2::Cpu &c);
bool spawn_player_human(ps2::Cpu &c);
// session_run_coop_callback: the level scripts' second player callback is not run for players 3 and 4.
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
bool loadout_amount(ps2::Cpu &c);
bool loadout_has_item(ps2::Cpu &c);
bool loadout_give_item(ps2::Cpu &c);
bool loadout_set_amount(ps2::Cpu &c);
bool loadout_clear_for_player(ps2::Cpu &c);
bool loadout_snapshot_store(ps2::Cpu &c);
bool loadout_snapshot_load(ps2::Cpu &c);
bool loadout_empty(ps2::Cpu &c);

}
