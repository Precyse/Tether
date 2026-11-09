"""Edit tables of the co-op front end (the CO-OP item of the main menu and the lobby, startup movies, load screens), read by apply_p10.py: FUNC_EDITS (function address,
old text, new text, count: matches inside that function only) and ENTRY_HOOKS (function address, statement: put after the function's label; an "if (...) return;" statement
ends the function when the hook handled it). Every text is the generated text it replaces and must match the stated number of times."""

HEADER = "ui_hooks.hpp"   # the hook header every edited unit includes

PM_MODE_INIT, PM_MODE_ON_COMMAND = 0x00209DA8, 0x0020A268

FUNC_EDITS = [
    # PM_Mode_Init, normal video: the row counts are copied from the game's data (STORY EXTRAS / QUICK RUMBLE); a co-op session makes them two rows of two.
    (PM_MODE_INIT, "    ldl(c.r[3], (c.r[6].w[0] + 7u));\n    ldr(c.r[3], (c.r[6].w[0]));\n",
     "    c.r[3].d[0] = warriors::ui::menu_rows(c.r[6].w[0]);\n", 1),
]

ENTRY_HOOKS = [
    (0x00204BA0, "warriors::ui::controller_update(c);"),                       # PM_Controller_Update
    (0x00209FE0, "if (warriors::ui::menu_add_coop_items(c)) goto L_0020A058;"),  # PM_Mode_Init: after STORY, instead of EXTRAS and QUICK RUMBLE
    (PM_MODE_ON_COMMAND, "if (warriors::ui::menu_command(c)) return;"),
    (0x00203300, "if (warriors::ui::lobby_init(c)) return;"),                  # PM_Continue_Init
    (0x002036D8, "warriors::ui::lobby_shutdown(c);"),                          # PM_Continue_Shutdown
    (0x00203800, "if (warriors::ui::lobby_command(c)) return;"),               # PM_Continue_OnCommand
    (0x002039C0, "warriors::ui::lobby_update(c);"),                            # PM_Continue_Update
    (0x00203A58, "warriors::ui::lobby_render(c);"),                            # PM_Continue_Render
    (0x0042A938, "warriors::ui::movie_play_entry(c);"),                        # Movie_Play: is this one of the startup movies
    (0x00429FE8, "if (warriors::ui::movie_skip(c)) return;"),                  # BinkMovie_Play: skip it
    (0x00207E28, "warriors::ui::greet_update(c);"),                            # PM_Greet_Update: the launcher's boot passes the title
    (0x0020A4B8, "warriors::ui::menu_update(c);"),                             # PM_Mode_Update: and the menu
    (0x0042A820, "if (warriors::ui::movie_check_skip(c)) return;"),            # Movie_CheckSkip: only the host's pad skips in a session
    (0x0039CBF0, "warriors::ui::scene_skip_vote(c);"),                         # SceneTask_Update: a skip needs every player's button
    (0x001DBEE0, "warriors::ui::pause_open(c);"),                              # PauseMenu_Open: whose menu it is
    (0x001DCB20, "warriors::ui::pause_close(c);"),                             # PauseMenu_Close
    (0x0011E878, "warriors::ui::level_frame(c);"),                             # Cameras_Update: every frame of a level
    (0x001612B0, "warriors::ui::load_screen_begin(c);"),                       # LoadScreen_Begin: a level load starts
    (0x00161378, "warriors::ui::load_screen_end(c);"),                         # LoadScreen_End: it is over
]
