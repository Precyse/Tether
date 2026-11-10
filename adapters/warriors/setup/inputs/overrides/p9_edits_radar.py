"""Edits of the generated radar fan-out, HUD lifecycle and overlay_step / overlay_draw code for the radars of players 3 and 4 (see p9_edits_ring.py, which merges this table
into the ring/radar area, and p9/p9_radar.cpp for what the hooks do).

Radar and crime panel of a panel number are reached as `hud + array + index * stride`: the index mults go through hud_mult (p9_hud.cpp) like the other HUD arrays. The
functions that apply one operation to both radars of the HUD are mirrored for HUD2 by an entry hook; the unrolled two-radar code of the level set-up, shut-down,
update and render gets one hook call at the label after the second radar's part.
"""

P9 = "warriors::p9::"

FAN_OUT_FUNCTIONS = (
    0x001B2990,   # minimap_blip_add
    0x001B2B18,   # minimap_blip_drop_by_item
    0x001B2B58,   # minimap_blip_drop_by_handle
    0x001B2BF0,   # minimap_blip_glyph_pin
    0x001B2C40,   # minimap_blip_size
    0x001B2CA0,   # minimap_blip_glyph
    0x001B2D80,   # minimap_blip_blink
    0x001B2E98,   # minimap_blip_modify
    0x001B2EE8,   # minimap_blip_add_person
    0x001B32E0,   # minimap_blip_style
    0x001B3610,   # minimap_mark_hostiles
)

ENTRY_HOOKS = [(function, "%sradar_mirror(c, 0x%08Xu);" % (P9, function)) for function in FAN_OUT_FUNCTIONS] + [
    (0x001C4A40, P9 + "radar_mirror_blip_type(c);"),            # minimap_widget_blip_kind (called for radar 1 by minimap_person_glyph_refresh, which also calls minimap_blip_glyph)
    (0x001B2E10, P9 + "radar_range(c);"),                       # minimap_range_set
    (0x001B40E0, P9 + "radar_zoom(c);"),                        # minimap_zoom_set
    (0x001B4328, P9 + "radar_toggle(c, true, true);"),          # minimap_show_all
    (0x001B43A8, P9 + "radar_toggle(c, false, true);"),         # minimap_hide_all
    (0x001B1F38, P9 + "radar_hide_blips(c);"),                  # overlay_conceal
    (0x001B21A8, P9 + "radar_hide_for_menu(c);"),               # overlay_conceal_for_menu
    (0x001AE980, P9 + "minimap_widget_free(c);"),                    # overlay_level_end
    (0x001B1688, P9 + "radar_render_reset();"),                       # overlay_draw: the list of extra radars to draw starts again
]

FUNC_EDITS = []


def edit(address, old, new, count=1):
    FUNC_EDITS.append((address, old, new, count))


def after_label(address, label, statement):
    """A hook call at the first statement after a label of the function (every path that reaches the label runs it)."""
    edit(address, "L_%08X:\n" % label, "L_%08X:\n    %s\n" % (label, statement))


# --- The radar and crime panel of a panel number (`index * 0x2940`, `index * 0xab0`) go through hud_mult, as the other per-player HUD arrays. -----------------
for function, count in ((0x001B2610, 2), (0x001B2658, 1), (0x001B26A8, 1), (0x001B2790, 1), (0x001B2520, 4), (0x0041B7A0, 1)):
    edit(function, "mmi::mult(c, 0, ", P9 + "hud_mult(c, 0, ", count)   # minimap_enable/disable/tint_blue/tint_grey, overlay_wanted_set, session_raise_scripted_wanted

# --- Set-up and update. -----------------------------------------------------------------------------------------------------------------------------------------
after_label(0x001AD588, 0x001AD878, P9 + "radar_build(c);")                                  # overlay_level_start: after the second radar's set-up
after_label(0x001AF010, 0x001AF5E8, P9 + "radar_toggle(c, false, false);")                   # overlay_step: radars disabled for a scene (twice), enabled after it
after_label(0x001AF010, 0x001AF640, P9 + "radar_toggle(c, true, false);")
after_label(0x001AF010, 0x001AF698, P9 + "radar_toggle(c, false, false);")
after_label(0x001AF010, 0x001B0178, P9 + "radar_shared_hide(c);")                            # shared screen: after minimap_widget_hide(radar 1)
after_label(0x001AF010, 0x001AFFFC, P9 + "radar_split_update(c, ld32(c.r[29].w[0] + 64u));")  # split screen: after radar 1 (listener 0 is at sp+64)
# overlay_draw, split screen: after radar 1 and its crime panel the radars and crime panels of players 3 and 4 are drawn by plain calls of the generated code (the display list
# can flush inside them, which it cannot inside a call a hook makes). The calls return to this label; radar_render_next hands each item out before it is drawn.
RADAR_DRAW = """for (;;) {
        const std::uint32_t item = %sradar_render_next();
        if (!item) break;
        set32(c.r[4], %sradar_render_target(item));
        c.r[31].sd[0] = static_cast<std::int32_t>(0x001B1A38u);
        if (item & 1u) { if (depth < kMaxDepth) unit_0031(c, 210u, depth + 1u); else { c.pc = 0x001C60B0u; return; } }
        else { if (depth < kMaxDepth) unit_002A(c, 267u, depth + 1u); else { c.pc = 0x001AABA8u; return; } }
        if (c.pc != 0x001B1A38u) [[unlikely]] return;
    }""" % (P9, P9)
after_label(0x001B1688, 0x001B1A38, RADAR_DRAW)
