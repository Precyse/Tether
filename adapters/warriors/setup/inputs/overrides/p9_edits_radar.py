"""Edits of the generated radar fan-out, HUD lifecycle and HUD_Update / HUD_Render code for the radars of players 3 and 4 (see p9_edits_ring.py, which merges this table
into the ring/radar area, and p9/p9_radar.cpp for what the hooks do).

Radar and crime panel of a panel number are reached as `hud + array + index * stride`: the index mults go through hud_mult (p9_hud.cpp) like the other HUD arrays. The
functions that apply one operation to both radars of the HUD are mirrored for HUD2 by an entry hook; the unrolled two-radar code of the level set-up, shut-down,
update and render gets one hook call at the label after the second radar's part.
"""

P9 = "warriors::p9::"

FAN_OUT_FUNCTIONS = (
    0x001B2990,   # HUD_RadarAddBlip
    0x001B2B18,   # HUD_RadarRemoveItem
    0x001B2B58,   # HUD_RadarRemoveHandle
    0x001B2BF0,   # HUD_RadarSetBlipIconLock
    0x001B2C40,   # HUD_RadarSetBlipScale
    0x001B2CA0,   # HUD_RadarSetBlipIcon
    0x001B2D80,   # HUD_RadarSetBlipFlash
    0x001B2E98,   # HUD_RadarChangeBlip
    0x001B2EE8,   # HUD_RadarAddPlainHuman
    0x001B32E0,   # HUD_RadarSetBlipMode
    0x001B3610,   # HUD_RadarMarkEnemies
)

ENTRY_HOOKS = [(function, "%sradar_mirror(c, 0x%08Xu);" % (P9, function)) for function in FAN_OUT_FUNCTIONS] + [
    (0x001C4A40, P9 + "radar_mirror_blip_type(c);"),            # Radar_SetBlipType (called for radar 1 by HUD_RadarUpdateHumanIcon, which also calls HUD_RadarSetBlipIcon)
    (0x001B2E10, P9 + "radar_range(c);"),                       # HudManager_SetRadarRange
    (0x001B40E0, P9 + "radar_zoom(c);"),                        # HUD_SetRadarZoomScale
    (0x001B4328, P9 + "radar_toggle(c, true, true);"),          # HUD_RadarOn
    (0x001B43A8, P9 + "radar_toggle(c, false, true);"),         # HUD_RadarOff
    (0x001B1F38, P9 + "radar_hide_blips(c);"),                  # HUD_Hide
    (0x001B21A8, P9 + "radar_hide_for_menu(c);"),               # HUD_HideForMenu
    (0x001AE980, P9 + "radar_shutdown(c);"),                    # HUD_ShutdownLevel
]

FUNC_EDITS = []


def edit(address, old, new, count=1):
    FUNC_EDITS.append((address, old, new, count))


def after_label(address, label, statement):
    """A hook call at the first statement after a label of the function (every path that reaches the label runs it)."""
    edit(address, "L_%08X:\n" % label, "L_%08X:\n    %s\n" % (label, statement))


# --- The radar and crime panel of a panel number (`index * 0x2940`, `index * 0xab0`) go through hud_mult, as the other per-player HUD arrays. -----------------
for function, count in ((0x001B2610, 2), (0x001B2658, 1), (0x001B26A8, 1), (0x001B2790, 1), (0x001B2520, 4), (0x0041B7A0, 1)):
    edit(function, "mmi::mult(c, 0, ", P9 + "hud_mult(c, 0, ", count)   # HUD_RadarEnable/Disable/SetTintBlue/SetTintGrey, HUD_SetWanted, GameState_SpawnCustomCrime

# --- Set-up and update. -----------------------------------------------------------------------------------------------------------------------------------------
after_label(0x001AD588, 0x001AD878, P9 + "radar_build(c);")                                  # HUD_InitLevel: after the second radar's set-up
after_label(0x001AF010, 0x001AF5E8, P9 + "radar_toggle(c, false, false);")                   # HUD_Update: radars disabled for a scene (twice), enabled after it
after_label(0x001AF010, 0x001AF640, P9 + "radar_toggle(c, true, false);")
after_label(0x001AF010, 0x001AF698, P9 + "radar_toggle(c, false, false);")
after_label(0x001AF010, 0x001B0178, P9 + "radar_shared_hide(c);")                            # shared screen: after Radar_Hide(radar 1)
after_label(0x001AF010, 0x001AFFFC, P9 + "radar_split_update(c, ld32(c.r[29].w[0] + 64u));")  # split screen: after radar 1 (listener 0 is at sp+64)
after_label(0x001B1688, 0x001B1A38, P9 + "radar_split_render(c);")                           # HUD_Render, split screen: after radar 1 and its crime panel
