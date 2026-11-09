#pragma once

#include "ps2/generated.hpp"

#include <cstdint>

// The entry points apply_ui.py puts into the generated front end of The Warriors (SLUS_212.15): the CO-OP item of the main menu and the lobby that
// replaces PM_Continue in a co-op session. Everything the lobby shows or decides lives in guest RAM and changes only through the pads, which are
// lockstep inputs, so every machine of a session computes the same menu (design\COOP_FRONTEND.md section 3.4). Outside a session none of these
// does anything and the game runs exactly as without them.
namespace warriors::ui {

// Entry of PM_Controller_Update (0x00204ba0, a0 = the flow controller): tells the overlay whether the title screen shows, adds the lobby's transitions.
void controller_update(ps2::Cpu &c);

// PM_Mode_Init (0x00209da8), the normal-video branch: the row counts it copies from the game's data (2 and 1; 2 and 2 with CO-OP), and the items after
// STORY (CO-OP, QUICK RUMBLE, EXTRAS) added in place of the original two, with CO-OP selected. True when the items were added here.
std::uint64_t menu_rows(std::uint32_t address);
bool menu_add_coop_items(ps2::Cpu &c);
// Entries of PM_Greet_Update (0x00207e28) and PM_Mode_Update (0x0020a4b8, a0 = the screen): in a co-op session the game boots straight into the lobby, the title
// pressing START and the menu choosing CO-OP by themselves, once per run.
void greet_update(ps2::Cpu &c);
void menu_update(ps2::Cpu &c);
// Entry of PM_Mode_OnCommand (0x0020a268, a0 = screen, a1 = command): accept on CO-OP opens the lobby (result 4). True when handled (c.pc is the return address).
bool menu_command(ps2::Cpu &c);

// PM_Continue (0x00203300 Init, 0x002036d8 Shutdown, 0x00203800 OnCommand, 0x002039c0 Update, 0x00203a58 Render), the lobby while a session is on.
// Init and OnCommand return true when they handled the call (c.pc is the return address); the others add to the original.
bool lobby_init(ps2::Cpu &c);
void lobby_shutdown(ps2::Cpu &c);
bool lobby_command(ps2::Cpu &c);
void lobby_update(ps2::Cpu &c);
void lobby_render(ps2::Cpu &c);

// The startup movies (ui_boot.cpp): entry of Movie_Play (0x0042a938, a0 = the movie's name) notes whether this one is skipped, entry of BinkMovie_Play (0x00429fe8,
// a0 = the movie object) then returns at once. Skipped unless --play-intro.
// Entry of Movie_CheckSkip (0x0042a820): in a co-op session only the host's pad skips a movie. True when handled.
bool movie_check_skip(ps2::Cpu &c);
void movie_play_entry(ps2::Cpu &c);
bool movie_skip(ps2::Cpu &c);

// The game's own level-load screen (ui_load.cpp): entries of LoadScreen_Begin (0x001612b0) and LoadScreen_End (0x00161378) tell the runtime, which shows every
// player's progress to all of them (netplay.hpp load_begin / load_end).
void load_screen_begin(ps2::Cpu &c);
void load_screen_end(ps2::Cpu &c);

// Entry of Cameras_Update (0x0011e878, every frame of a level, the front end's controller update does not run there): registers how a player's leaving or return is
// said in the game (ui_announce.cpp), the HUD's scroll-in line and its cue.
void level_frame(ps2::Cpu &c);

// Entry of SceneTask_Update (0x0039cbf0, a0 = the scene task, ui_scene_skip.cpp): in a session a skippable scene is skipped only while every player holds cross or START.
void scene_skip_vote(ps2::Cpu &c);

// Entries of PauseMenu_Open (0x001dbee0, a0 = the menu) and PauseMenu_Close (0x001dcb20, ui_pause.cpp): the overlay says whose pause menu is open.
void pause_open(ps2::Cpu &c);
void pause_close(ps2::Cpu &c);

// A co-op session is on: a session file was given and the lockstep link runs (or it is a local session). Same answer on every machine of a session.
bool coop_session();

}
