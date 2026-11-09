"""Edits of the generated HUD, PlayerHUD and join code for four players (see apply_p9.py for the mechanics, p9\\p9_hud.cpp for what the hooks do).

The HUD's per-player parts are arrays of two, reached as `hud + array + index * stride`: through a mult by the stride, or a pointer stepped by it. Each such
site calls hud_mult / array_step, which send index 2 and 3 to the second HUD object (entries 0 and 1 there). A loop over the panels counts to panel_count()
(2 until the third player exists), so a two player game runs the original code with the original values.
"""

P9 = "warriors::p9::"

ENTRY_HOOKS = [
    (0x0041A460, P9 + 'trace_call(c, "GameState_SyncPlayers");'),
    (0x00229C40, 'if (%smake_player(c)) return;' % P9),   # Human_MakePlayer
    (0x0022A770, 'if (%spick_next_player(c)) return;' % P9),   # Gang_PickNextPlayer
    (0x0041B148, 'if (%sskip_extra_player_callback(c)) return;' % P9),   # GameState_CallMultiplayerCallback
    (0x00144B88, 'if (c.r[4].w[0] == 0x5DD860u && c.r[5].w[0] == 0x800u) ' + P9 + 'trace_call(c, "Pad_IsHeld");'),
    (0x0020E058, P9 + 'trace_call(c, "PlayerHUD_Hide");'),
    (0x0020E028, P9 + 'trace_call(c, "PlayerHUD_Show");'),
    (0x001AF010, P9 + 'trace_call(c, "HUD_Update");'),
    (0x001B1688, P9 + 'trace_call(c, "HUD_Render");'),
    (0x00144BF0, 'if (c.r[5].w[0] == 0x100u) ' + P9 + 'trace_call(c, "Pad_IsNewlyPressed(select)");'),
    (0x004197A8, P9 + 'trace_players(c);'),
]

FUNC_EDITS = []


def edit(address, old, new, count=1):
    FUNC_EDITS.append((address, old, new, count))


def mult_to_hud(address, count=None, index_register=None):
    """Every `index * stride` mult of the function (or just those with the given index register) goes through hud_mult."""
    old = "mmi::mult(c, 0, c.r[%d].w[0]," % index_register if index_register is not None else "mmi::mult(c, 0, "
    edit(address, old, old.replace("mmi::mult", P9 + "hud_mult"), count)


def bound(address, register, signed, count=1):
    """`register < 2` of a loop over the panels becomes `register < panel_count()`."""
    if signed:
        old = "c.r[%d].sd[0] < 2ll ? 1ull : 0ull;" % register
        new = "c.r[%d].sd[0] < static_cast<std::int64_t>(%spanel_count()) ? 1ull : 0ull;" % (register, P9)
    else:
        old = "c.r[%d].d[0] < static_cast<std::uint64_t>(2ll) ? 1ull : 0ull;" % register
        new = "c.r[%d].d[0] < static_cast<std::uint64_t>(%spanel_count()) ? 1ull : 0ull;" % (register, P9)
    return old, new


def loop_bound(address, register, signed, count=1):
    old, new = bound(address, register, signed)
    edit(address, old, new, count)


def pointer_step(address, register, array_offset, stride, count=1):
    """`register += stride` of a pointer that walks a per-player array."""
    edit(address, "set32(c.r[%d], c.r[%d].w[0] + %du);" % (register, register, stride),
         "set32(c.r[%d], %sarray_step(c.r[%d].w[0], 0x%X, %du));" % (register, P9, register, array_offset, stride), count)


# --- The CrewStatus widget sat on the PlayerHUD pointer array (+0x10): it moves to extra RAM once HUD2 exists. -------------------------------------------
edit(0x001AD588, "set32(c.r[4], c.r[21].w[0] + 16u);\n    c.r[1].sd[0] = static_cast<std::int32_t>(0x3DA30000u);",
     "set32(c.r[4], %screw_widget(c.r[21].w[0]));\n    c.r[1].sd[0] = static_cast<std::int32_t>(0x3DA30000u);" % P9)           # HUD_InitLevel: CrewStatus_Setup
edit(0x001AE980, "set32(c.r[21], c.r[18].w[0] + 16u);", "set32(c.r[21], %screw_widget(c.r[18].w[0]));" % P9)                       # HUD_ShutdownLevel
edit(0x001AF010, "set32(c.r[4], c.r[20].w[0] + 16u);  if (depth < kMaxDepth) unit_0027(c, 297u",
     "set32(c.r[4], %screw_widget(c.r[20].w[0]));  if (depth < kMaxDepth) unit_0027(c, 297u" % P9)                           # HUD_Update
edit(0x001B1688, "set32(c.r[4], c.r[18].w[0] + 16u);  if (depth < kMaxDepth) unit_0028(c, 14u",
     "set32(c.r[4], %screw_widget(c.r[18].w[0]));  if (depth < kMaxDepth) unit_0028(c, 14u" % P9)                           # HUD_Render
edit(0x001B3960, "set32(c.r[16], c.r[4].w[0] + 16u);", "set32(c.r[16], %screw_widget(c.r[4].w[0]));" % P9)                          # HUD_TagAddHuman
edit(0x001B39A0, "set32(c.r[16], c.r[4].w[0] + 16u);", "set32(c.r[16], %screw_widget(c.r[4].w[0]));" % P9)                          # HUD_TagRemoveHuman

# --- Loops that walk the PlayerHUD pointer array count down from 1: down from the last panel. --------------------------------------------------------
for function in (0x001B1F38, 0x001B2030, 0x001B2088, 0x001B20F8, 0x001B2330, 0x001B2380):   # HUD_Hide, Hide/ShowPlayerPanelsBoth, Restore, ShowPanelsAfterScene, ApplyPanelsHide
    edit(function, "set32(c.r[17], 0u + 1u);", "set32(c.r[17], %slast_panel());" % P9)
edit(0x001B1688, "set32(c.r[17], 0u + 1u);\n    set32(c.r[5], ld32((c.r[16].w[0])));",
     "set32(c.r[17], %slast_panel());\n    set32(c.r[5], ld32((c.r[16].w[0])));" % P9)                                         # HUD_Render: the panels' render call

# --- Joining: a panel for the third and fourth player. ---------------------------------------------------------------------------------------------------
edit(0x001B2200, "c.r[2].d[0] = c.r[17].sd[0] < 2ll ? 1ull : 0ull;",
     "c.r[2].d[0] = c.r[17].sd[0] < static_cast<std::int64_t>(%spanel_count()) ? 1ull : 0ull;" % P9)                          # HUD_AttachPlayer: panel < 2
edit(0x00229C40, "c.r[2].d[0] = c.r[16].d[0] < static_cast<std::uint64_t>(2ll) ? 1ull : 0ull;\n    c.budget -= 3;\n    { const bool t = c.r[2].d[0] == 0ull; c.r[2].sd[0] = static_cast<std::int32_t>(0x00600000u);",
     "c.r[2].d[0] = c.r[16].d[0] < static_cast<std::uint64_t>(%spanel_count()) ? 1ull : 0ull;\n    c.budget -= 3;\n    { const bool t = c.r[2].d[0] == 0ull; c.r[2].sd[0] = static_cast<std::int32_t>(0x00600000u);" % P9)   # Human_MakePlayer: first free panel

# --- PlayerHUD: layout tables indexed by panel (stride 0x240 and 0xc0, bases 0x10): panels 2 and 3 read the table copy. ---------------------------------------
PLAYER_HUD_FUNCTIONS = (0x00212840, 0x00211EF8, 0x00213060, 0x00213718, 0x00213770, 0x00213E68, 0x00214138)   # (PlayerHUD_RefreshTallyMarks has no mult)
for function in PLAYER_HUD_FUNCTIONS:
    mult_to_hud(function)
edit(0x00212840, "set32(c.r[2], c.r[21].w[0] << 4);", "set32(c.r[2], %spanel_base_offset(c.r[21].w[0]));" % P9)             # PlayerHUD_Init: base of the panel
edit(0x00214138, "set32(c.r[4], c.r[4].w[0] << 4);", "set32(c.r[4], %spanel_base_offset(c.r[4].w[0]));" % P9)                # PlayerHUD_Update
edit(0x00214BC8, "set32(c.r[2], c.r[2].w[0] << 4);", "set32(c.r[2], %spanel_base_offset(c.r[2].w[0]));" % P9)                # PlayerHUD_RefreshTallyMarks
edit(0x00212840, "c.r[2].d[0] = c.r[21].d[0] < static_cast<std::uint64_t>(2ll) ? 1ull : 0ull;",
     "c.r[2].d[0] = c.r[21].d[0] < static_cast<std::uint64_t>(%spanel_count()) ? 1ull : 0ull;" % P9)                          # PlayerHUD_Init: panel < 2

# --- HUD_Update: the per-player widget loop (PlayerHUD, mash meter, lock-pick dial, stereo theft, status icons, action prompt) and the action hint loop. ------
loop_bound(0x001AF010, 16, signed=True)
mult_to_hud(0x001AF010, 5, 16)
mult_to_hud(0x001AF010, 1, 18)
mult_to_hud(0x001AF010, 1, 19)
pointer_step(0x001AF010, 17, 0xF420, 1344)

# --- HUD_Render: the same loops, and the action prompts' render loop. -----------------------------------------------------------------------------------
loop_bound(0x001B1688, 16, signed=False)
loop_bound(0x001B1688, 17, signed=False, count=2)
mult_to_hud(0x001B1688, 6, 16)
pointer_step(0x001B1688, 19, 0x135E0, 256)
pointer_step(0x001B1688, 20, 0x135E0, 256)
pointer_step(0x001B1688, 16, 0x6B40, 1424)

# --- HUD_ShutdownLevel / HUD_InitLevel: the per-player parts shut down and set up for every panel. -------------------------------------------------------
loop_bound(0x001AE980, 17, signed=True)
mult_to_hud(0x001AE980, 5, 17)
loop_bound(0x001AD588, 18, signed=False, count=2)   # the action prompts' set-up loop and the widgets' set-up loop
pointer_step(0x001AD588, 16, 0x6B40, 1424)
pointer_step(0x001AD588, 16, 0xEBC0, 1072)
pointer_step(0x001AD588, 17, 0x114C0, 1104)
pointer_step(0x001AD588, 20, 0xF420, 1344)
loop_bound(0x001AD588, 19, signed=False)
pointer_step(0x001AD588, 18, 0x19130, 19024)

# --- The small per-player accessors (mash meter, lock-pick dial, stereo theft, action prompts). ------------------------------------------------------------
for function in (0x001B3B68, 0x001B3BA8, 0x001B3C78, 0x001B2950):                 # HUD_MashMeterShow/Hide/Update, HUD_StereoTheftSet: panel < 2
    loop_bound(function, 5, signed=False)
    mult_to_hud(function)
for function in (0x001B39E0, 0x001B3B38, 0x001B3C00, 0x001B3C30, 0x001B2490, 0x001B3DD0, 0x001B3DF8):   # lock pick start/end, stereo start/end, action prompt
    mult_to_hud(function)

# --- Player state blocks addressed inline by list slot (`slot * 0x5c + game + 0x168`): slot 2's block would be the handle of player 4 (+0x234). -------------------
for function in (0x00254E78, 0x00264CF8, 0x00265DD0, 0x00418C68):   # Human_UpdateActions, Human_AddRage, Rage_NoteHit, GameState_ResetForLevel
    edit(function, "mmi::mult(c, 0, ", P9 + "state_mult(c, 0, ", None)
