"""Edit tables of the co-op front end (the CO-OP item of the main menu and the lobby, startup movies, load screens), read by apply_p10.py: FUNC_EDITS (function address,
old text, new text, count: matches inside that function only) and ENTRY_HOOKS (function address, statement: put after the function's label; an "if (...) return;" statement
ends the function when the hook handled it). Every text is the generated text it replaces and must match the stated number of times."""

HEADER = "ui_hooks.hpp"   # the hook header every edited unit includes

MODE_PAGE_OPEN, MODE_PAGE_COMMAND = 0x00209DA8, 0x0020A268

FUNC_EDITS = [
    # mode_page_open, normal video: the row counts are copied from the game's data (STORY EXTRAS / QUICK RUMBLE); a co-op session makes them two rows of two.
    (MODE_PAGE_OPEN, "    ldl(c.r[3], (c.r[6].w[0] + 7u));\n    ldr(c.r[3], (c.r[6].w[0]));\n",
     "    c.r[3].d[0] = warriors::ui::menu_rows(c.r[6].w[0]);\n", 1),
]

ENTRY_HOOKS = [
    (0x00204BA0, "warriors::ui::controller_update(c);"),                       # controller_page_step
    (0x00209FE0, "if (warriors::ui::menu_add_coop_items(c)) goto L_0020A058;"),  # mode_page_open: after STORY, instead of EXTRAS and QUICK RUMBLE
    (MODE_PAGE_COMMAND, "if (warriors::ui::menu_command(c)) return;"),
    (0x00203300, "if (warriors::ui::lobby_init(c)) return;"),                  # continue_page_open
    (0x002036D8, "warriors::ui::lobby_shutdown(c);"),                          # continue_page_close
    (0x00203800, "if (warriors::ui::lobby_command(c)) return;"),               # continue_page_command
    (0x002039C0, "warriors::ui::lobby_update(c);"),                            # continue_page_step
    (0x00203A58, "warriors::ui::lobby_render(c);"),                            # continue_page_draw
    (0x0042A938, "warriors::ui::movie_play_entry(c);"),                        # video_play_clip: is this one of the startup movies
    (0x00429FE8, "if (warriors::ui::movie_skip(c)) return;"),                  # video_session_run: skip it
    (0x00207E28, "warriors::ui::greet_update(c);"),                            # greet_page_step: the launcher's boot passes the title
    (0x0020A4B8, "warriors::ui::menu_update(c);"),                             # mode_page_step: and the menu
    (0x0042A820, "if (warriors::ui::video_skip_poll(c)) return;"),            # video_skip_poll: only the host's pad skips in a session
    (0x0039CBF0, "warriors::ui::scene_skip_vote(c);"),                         # cutscene_task_step: a skip needs every player's button
    (0x001DBEE0, "warriors::ui::pause_open(c);"),                              # pause_screen_open: whose menu it is
    (0x001DCB20, "warriors::ui::pause_close(c);"),                             # pause_screen_close
    (0x0011E878, "warriors::ui::level_frame(c);"),                             # views_frame_tick: every frame of a level
    (0x001612B0, "warriors::ui::loading_screen_open(c);"),                       # loading_screen_open: a level load starts
    (0x00161378, "warriors::ui::loading_screen_close(c);"),                         # loading_screen_close: it is over
]
